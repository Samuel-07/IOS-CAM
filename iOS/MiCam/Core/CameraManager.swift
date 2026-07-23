import Foundation
import AVFoundation
import UIKit
import Combine

public struct CameraDeviceDescriptor: Identifiable, Hashable {
    public let id: String
    public let name: String
    public let lensType: MiCamCameraLens
    public let position: AVCaptureDevice.Position
    public let device: AVCaptureDevice
    
    public static func == (lhs: CameraDeviceDescriptor, rhs: CameraDeviceDescriptor) -> Bool {
        return lhs.id == rhs.id
    }
    
    public func hash(into hasher: inout Hasher) {
        hasher.combine(id)
    }
}

public struct CameraFormatDescriptor: Identifiable, Hashable {
    public let id: String
    public let width: Int32
    public let height: Int32
    public let maxFps: Double
    public let minFps: Double
    public let format: AVCaptureDevice.Format
    /// True for the portrait (vertical) presentation of this resolution - same underlying
    /// sensor format as its landscape counterpart, delivered rotated via the capture
    /// connection rather than a distinct AVCaptureDevice.Format (the sensor itself has no
    /// separate "portrait mode").
    public let isPortrait: Bool

    public static func == (lhs: CameraFormatDescriptor, rhs: CameraFormatDescriptor) -> Bool {
        return lhs.id == rhs.id
    }

    public func hash(into hasher: inout Hasher) {
        hasher.combine(id)
    }
}

public protocol CameraManagerDelegate: AnyObject {
    func cameraManager(_ manager: CameraManager, didOutput pixelBuffer: CVPixelBuffer, presentationTimeStamp: CMTime)
}

public class CameraManager: NSObject, ObservableObject, AVCaptureVideoDataOutputSampleBufferDelegate, VideoEncoderDelegate {
    public static let shared = CameraManager()

    public weak var delegate: CameraManagerDelegate?

    public private(set) var captureSession = AVCaptureSession()
    private var activeDeviceInput: AVCaptureDeviceInput?
    private var videoDataOutput = AVCaptureVideoDataOutput()
    private let captureQueue = DispatchQueue(label: "com.micam.captureQueue", qos: .userInteractive)

    // Encodes every captured frame to H.264 and forwards it to NetworkStreamer - this is the
    // piece that was previously entirely missing: CameraManager captured frames and
    // VideoEncoder could encode them, but nothing ever connected the two, so no video was ever
    // actually sent to Windows regardless of any USB/WiFi/OBS fix.
    private let videoEncoder = VideoEncoder()

    @Published public private(set) var availableDevices: [CameraDeviceDescriptor] = []
    @Published public private(set) var currentDevice: CameraDeviceDescriptor?
    @Published public private(set) var availableFormats: [CameraFormatDescriptor] = []
    @Published public private(set) var currentFormat: CameraFormatDescriptor?
    @Published public private(set) var currentFps: Double = 30.0

    override init() {
        super.init()
        videoEncoder.delegate = self
        discoverDevices()
    }
    
    // Discover physical camera devices with distinct single lens entries (0.5x, 1x, 3x, Front)
    public func discoverDevices() {
        let deviceTypes: [AVCaptureDevice.DeviceType] = [
            .builtInWideAngleCamera,
            .builtInTelephotoCamera,
            .builtInUltraWideCamera
        ]
        
        let discoverySession = AVCaptureDevice.DiscoverySession(
            deviceTypes: deviceTypes,
            mediaType: .video,
            position: .unspecified
        )
        
        var descriptors: [CameraDeviceDescriptor] = []
        var seenLensTypes = Set<MiCamCameraLens>()
        
        for dev in discoverySession.devices {
            let lens: MiCamCameraLens
            if dev.position == .front {
                lens = .front
            } else {
                switch dev.deviceType {
                case .builtInUltraWideCamera: lens = .ultraWide
                case .builtInTelephotoCamera: lens = .telephoto
                case .builtInWideAngleCamera: lens = .wide
                default: lens = .wide
                }
            }
            
            if !seenLensTypes.contains(lens) {
                seenLensTypes.insert(lens)
                let desc = CameraDeviceDescriptor(
                    id: dev.uniqueID,
                    name: dev.localizedName,
                    lensType: lens,
                    position: dev.position,
                    device: dev
                )
                descriptors.append(desc)
            }
        }
        
        DispatchQueue.main.async {
            self.availableDevices = descriptors
            if self.currentDevice == nil, let first = descriptors.first {
                self.selectDevice(first)
            }
        }
    }
    
    public func selectDevice(_ descriptor: CameraDeviceDescriptor) {
        captureQueue.async { [weak self] in
            guard let self = self else { return }
            
            self.captureSession.beginConfiguration()
            
            if let currentInput = self.activeDeviceInput {
                self.captureSession.removeInput(currentInput)
            }
            
            do {
                let newInput = try AVCaptureDeviceInput(device: descriptor.device)
                if self.captureSession.canAddInput(newInput) {
                    self.captureSession.addInput(newInput)
                    self.activeDeviceInput = newInput
                    
                    DispatchQueue.main.async {
                        self.currentDevice = descriptor
                    }
                    
                    self.populateFormats(for: descriptor.device)
                }
            } catch {
                print("[CameraManager] Failed to create input for device \(descriptor.name): \(error)")
            }
            
            if !self.captureSession.outputs.contains(self.videoDataOutput) {
                self.videoDataOutput.alwaysDiscardsLateVideoFrames = true
                self.videoDataOutput.videoSettings = [
                    kCVPixelBufferPixelFormatTypeKey as String: Int(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)
                ]
                self.videoDataOutput.setSampleBufferDelegate(self, queue: self.captureQueue)
                
                if self.captureSession.canAddOutput(self.videoDataOutput) {
                    self.captureSession.addOutput(self.videoDataOutput)
                }
            }
            
            self.captureSession.commitConfiguration()
        }
    }
    
    // Filter formats so ONLY the 6 standard broadcast resolutions appear (4K, 2K, 1080p, 720p,
    // 1024x768 XGA, 640x480 VGA), each offered in both landscape and portrait (vertical).
    private func populateFormats(for device: AVCaptureDevice) {
        let targetResolutions: [(width: Int32, height: Int32)] = [
            (3840, 2160), // 4K Ultra HD
            (2560, 1440), // 2K Quad HD
            (1920, 1080), // 1080p Full HD
            (1280, 720),  // 720p HD
            (1024, 768),  // XGA 4:3
            (640, 480)    // VGA 4:3
        ]

        struct Candidate {
            let format: AVCaptureDevice.Format
            let width: Int32
            let height: Int32
            let maxFps: Double
            let minFps: Double
            let isExact: Bool
        }

        // One candidate per target "slot" - picking the tightest-fitting real format for each,
        // never a label that lies about what the sensor is actually producing. Previously this
        // stored the TARGET's dimensions as the descriptor's width/height even when the actual
        // chosen AVCaptureDevice.Format was a different (larger, different-aspect) native size -
        // that mismatch meant the video encoder could be configured for e.g. 1920x1080 while the
        // camera was actually delivering 1920x1440 buffers, which is what caused the reported
        // "screen vibrating" corruption and FPS ranges silently not matching what was requested.
        var bestPerTarget: [String: Candidate] = [:]

        for format in device.formats {
            let dimensions = CMVideoFormatDescriptionGetDimensions(format.formatDescription)
            let fpsRanges = format.videoSupportedFrameRateRanges

            var maxFps: Double = 30.0
            var minFps: Double = 24.0
            for range in fpsRanges {
                if range.maxFrameRate > maxFps { maxFps = range.maxFrameRate }
                if range.minFrameRate < minFps { minFps = range.minFrameRate }
            }

            for target in targetResolutions {
                let isExact = dimensions.width == target.width && dimensions.height == target.height
                let isSuperset = dimensions.width >= target.width && dimensions.height >= target.height
                guard isExact || isSuperset else { continue }

                let key = "\(target.width)x\(target.height)"
                let candidate = Candidate(format: format, width: dimensions.width, height: dimensions.height, maxFps: maxFps, minFps: minFps, isExact: isExact)

                if let existing = bestPerTarget[key] {
                    let existingArea = Int64(existing.width) * Int64(existing.height)
                    let candidateArea = Int64(dimensions.width) * Int64(dimensions.height)
                    if candidate.isExact && !existing.isExact {
                        bestPerTarget[key] = candidate
                    } else if candidate.isExact == existing.isExact {
                        if candidateArea < existingArea {
                            bestPerTarget[key] = candidate // tighter fit, less cropping
                        } else if candidateArea == existingArea && candidate.maxFps > existing.maxFps {
                            bestPerTarget[key] = candidate
                        }
                    }
                } else {
                    bestPerTarget[key] = candidate
                }
                break
            }
        }

        // Guarantee all 6 target slots are represented even if nothing matched exactly.
        if let fallbackFormat = device.formats.first {
            let fallbackDims = CMVideoFormatDescriptionGetDimensions(fallbackFormat.formatDescription)
            for target in targetResolutions {
                let key = "\(target.width)x\(target.height)"
                if bestPerTarget[key] == nil {
                    bestPerTarget[key] = Candidate(format: fallbackFormat, width: fallbackDims.width, height: fallbackDims.height, maxFps: 60.0, minFps: 24.0, isExact: false)
                }
            }
        }

        var formatMap: [String: CameraFormatDescriptor] = [:]
        for (_, c) in bestPerTarget {
            let landscapeKey = "\(c.width)x\(c.height)"
            formatMap[landscapeKey] = CameraFormatDescriptor(
                id: landscapeKey, width: c.width, height: c.height,
                maxFps: c.maxFps, minFps: c.minFps, format: c.format, isPortrait: false
            )
            let portraitKey = "\(c.height)x\(c.width)_portrait"
            formatMap[portraitKey] = CameraFormatDescriptor(
                id: portraitKey, width: c.height, height: c.width,
                maxFps: c.maxFps, minFps: c.minFps, format: c.format, isPortrait: true
            )
        }

        var cleanFormats = Array(formatMap.values)
        cleanFormats.sort { ($0.width * $0.height) > ($1.width * $1.height) }

        DispatchQueue.main.async {
            self.availableFormats = cleanFormats
            if let best = cleanFormats.first(where: { $0.width == 1920 && !$0.isPortrait }) ?? cleanFormats.first {
                self.configureFormat(best, targetFps: 60.0)
            }
        }
    }
    
    public func configureFormat(_ formatDesc: CameraFormatDescriptor, targetFps: Double) {
        captureQueue.async { [weak self] in
            guard let self = self, let device = self.currentDevice?.device else { return }

            // AVFoundation raises an uncatchable NSException (not a Swift `Error`) if you set
            // activeVideoMin/MaxFrameDuration outside what THIS SPECIFIC format actually
            // supports - do/catch here does nothing to stop that crash. The UI's FPS list is
            // built from this same format's ranges (see availableFpsOptions), but clamp again
            // here as a hard guarantee against ever handing AVFoundation an invalid value.
            let ranges = formatDesc.format.videoSupportedFrameRateRanges
            guard let matchingRange = ranges.first(where: { targetFps >= $0.minFrameRate && targetFps <= $0.maxFrameRate })
                ?? ranges.max(by: { $0.maxFrameRate < $1.maxFrameRate }) else {
                print("[CameraManager] Format \(formatDesc.width)x\(formatDesc.height) has no supported frame rate ranges")
                return
            }
            let clampedFps = min(max(targetFps, matchingRange.minFrameRate), matchingRange.maxFrameRate)

            do {
                try device.lockForConfiguration()
                device.activeFormat = formatDesc.format

                let frameDuration = CMTime(value: 1, timescale: CMTimeScale(clampedFps))
                device.activeVideoMinFrameDuration = frameDuration
                device.activeVideoMaxFrameDuration = frameDuration

                device.unlockForConfiguration()

                // Rotate the delivered buffers for portrait selections - the sensor format
                // itself is unchanged (still formatDesc.height x formatDesc.width natively),
                // the connection rotation is what actually swaps what captureOutput receives.
                self.applyOrientation(isPortrait: formatDesc.isPortrait, position: device.position)

                self.videoEncoder.configure(width: formatDesc.width, height: formatDesc.height, codec: .h264, fps: Int32(clampedFps))

                DispatchQueue.main.async {
                    self.currentFormat = formatDesc
                    self.currentFps = clampedFps
                }
                print("[CameraManager] Configured format: \(formatDesc.width)x\(formatDesc.height) @ \(clampedFps) FPS\(formatDesc.isPortrait ? " (portrait)" : "")")
            } catch {
                print("[CameraManager] Failed to lock device for configuration: \(error)")
            }
        }
    }

    // Portrait (vertical) output angle differs by camera position because the front camera's
    // sensor is mounted mirrored relative to the back camera - 90 degrees on the back camera
    // is 270 on the front for the same physical "top of phone = top of frame" result.
    private func applyOrientation(isPortrait: Bool, position: AVCaptureDevice.Position) {
        guard let connection = videoDataOutput.connection(with: .video) else { return }
        let portraitAngle: CGFloat = (position == .front) ? 270 : 90
        let angle: CGFloat = isPortrait ? portraitAngle : 0

        if #available(iOS 17.0, *) {
            if connection.isVideoRotationAngleSupported(angle) {
                connection.videoRotationAngle = angle
            }
        } else if connection.isVideoOrientationSupported {
            connection.videoOrientation = isPortrait ? .portrait : .landscapeRight
        }
    }

    // Real, per-format FPS choices (not a hardcoded list) - only rates this exact format
    // supports, so the Settings UI can never offer a value that would crash configureFormat.
    public func availableFpsOptions(for formatDesc: CameraFormatDescriptor) -> [Double] {
        let standardRates: [Double] = [24, 30, 60, 120, 240]
        let ranges = formatDesc.format.videoSupportedFrameRateRanges
        let supported = standardRates.filter { rate in
            ranges.contains { rate >= $0.minFrameRate && rate <= $0.maxFrameRate }
        }
        if !supported.isEmpty { return supported }
        // No standard rate lands inside a supported range (unusual format) - fall back to the
        // format's own max, so there's always at least one valid, non-crashing choice.
        return ranges.map { $0.maxFrameRate }.max().map { [$0] } ?? [30]
    }
    
    // Real-time Controls (Focus, Exposure, White Balance, Torch, Zoom)
    public func setTorch(enabled: Bool) {
        guard let device = currentDevice?.device, device.hasTorch else { return }
        do {
            try device.lockForConfiguration()
            device.torchMode = enabled ? .on : .off
            device.unlockForConfiguration()
        } catch {}
    }
    
    public func setZoom(factor: CGFloat) {
        guard let device = currentDevice?.device else { return }
        do {
            try device.lockForConfiguration()
            let clamped = min(max(factor, device.minAvailableVideoZoomFactor), device.maxAvailableVideoZoomFactor)
            device.videoZoomFactor = clamped
            device.unlockForConfiguration()
        } catch {}
    }
    
    public func setFocusPoint(_ point: CGPoint) {
        guard let device = currentDevice?.device, device.isFocusPointOfInterestSupported else { return }
        do {
            try device.lockForConfiguration()
            device.focusPointOfInterest = point
            device.focusMode = .autoFocus
            device.unlockForConfiguration()
        } catch {}
    }
    
    public func startSession() {
        if !captureSession.isRunning {
            DispatchQueue.global(qos: .userInitiated).async {
                self.captureSession.startRunning()
            }
        }
    }
    
    public func stopSession() {
        if captureSession.isRunning {
            captureSession.stopRunning()
        }
    }
    
    // AVCaptureVideoDataOutputSampleBufferDelegate
    public func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
        guard let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
        let pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer)
        delegate?.cameraManager(self, didOutput: pixelBuffer, presentationTimeStamp: pts)

        // This is the actual video transmission path: every captured frame is handed to the
        // hardware H.264 encoder, whose output reaches Windows via videoEncoder(_:didOutputSampleData:...).
        videoEncoder.encode(pixelBuffer: pixelBuffer, pts: pts)
    }

    // VideoEncoderDelegate
    public func videoEncoder(_ encoder: VideoEncoder, didOutputSampleData data: Data, isKeyFrame: Bool, timestampUs: UInt64) {
        let width = UInt16(currentFormat?.width ?? 1920)
        let height = UInt16(currentFormat?.height ?? 1080)
        let fps = UInt16(currentFps)
        NetworkStreamer.shared.sendVideoFrame(
            naluData: data, isKeyFrame: isKeyFrame, width: width, height: height,
            fps: fps, codec: .h264, timestampUs: timestampUs
        )
    }
}
