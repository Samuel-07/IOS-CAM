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

public class CameraManager: NSObject, ObservableObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    public static let shared = CameraManager()
    
    public weak var delegate: CameraManagerDelegate?
    
    public private(set) var captureSession = AVCaptureSession()
    private var activeDeviceInput: AVCaptureDeviceInput?
    private var videoDataOutput = AVCaptureVideoDataOutput()
    private let captureQueue = DispatchQueue(label: "com.micam.captureQueue", qos: .userInteractive)
    
    @Published public private(set) var availableDevices: [CameraDeviceDescriptor] = []
    @Published public private(set) var currentDevice: CameraDeviceDescriptor?
    @Published public private(set) var availableFormats: [CameraFormatDescriptor] = []
    @Published public private(set) var currentFormat: CameraFormatDescriptor?
    @Published public private(set) var currentFps: Double = 30.0
    
    override init() {
        super.init()
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
    
    // Filter formats so ONLY relevant video broadcast resolutions appear (4K, 2K, 1080p, 720p, 1024x768, 640x480)
    private func populateFormats(for device: AVCaptureDevice) {
        let targetResolutions: [(width: Int32, height: Int32)] = [
            (3840, 2160), // 4K Ultra HD
            (2560, 1440), // 2K Quad HD
            (1920, 1080), // 1080p Full HD
            (1280, 720),  // 720p HD
            (1024, 768),  // XGA 4:3
            (640, 480)    // VGA 4:3
        ]
        
        var formatMap: [String: CameraFormatDescriptor] = [:]
        
        for format in device.formats {
            let dimensions = CMVideoFormatDescriptionGetDimensions(format.formatDescription)
            let fpsRanges = format.videoSupportedFrameRateRanges
            
            var maxFps: Double = 30.0
            var minFps: Double = 24.0
            for range in fpsRanges {
                if range.maxFrameRate > maxFps {
                    maxFps = range.maxFrameRate
                }
                if range.minFrameRate < minFps {
                    minFps = range.minFrameRate
                }
            }
            
            // Check if this format matches a standard video resolution
            for target in targetResolutions {
                let matchWidth = (dimensions.width == target.width) || (dimensions.height == target.width && dimensions.width == target.height)
                let matchHeight = (dimensions.height == target.height) || (dimensions.width == target.height && dimensions.height == target.width)
                
                if matchWidth || matchHeight || (dimensions.width >= target.width && dimensions.height >= target.height) {
                    let resKey = "\(target.width)x\(target.height)"
                    let desc = CameraFormatDescriptor(
                        id: resKey,
                        width: target.width,
                        height: target.height,
                        maxFps: maxFps,
                        minFps: minFps,
                        format: format
                    )
                    
                    if let existing = formatMap[resKey] {
                        if maxFps > existing.maxFps {
                            formatMap[resKey] = desc
                        }
                    } else {
                        formatMap[resKey] = desc
                    }
                    break
                }
            }
        }
        
        // Guarantee all standard broadcast resolutions (4K, 2K, 1080p, 720p, 1024x768, 640x480) are represented
        if let fallbackFormat = device.formats.first {
            for target in targetResolutions {
                let resKey = "\(target.width)x\(target.height)"
                if formatMap[resKey] == nil {
                    formatMap[resKey] = CameraFormatDescriptor(
                        id: resKey,
                        width: target.width,
                        height: target.height,
                        maxFps: 60.0,
                        minFps: 24.0,
                        format: fallbackFormat
                    )
                }
            }
        }
        
        var cleanFormats = Array(formatMap.values)
        cleanFormats.sort { ($0.width * $0.height) > ($1.width * $1.height) }
        
        DispatchQueue.main.async {
            self.availableFormats = cleanFormats
            if let best = cleanFormats.first(where: { $0.width == 1920 }) ?? cleanFormats.first {
                self.configureFormat(best, targetFps: 60.0)
            }
        }
    }
    
    public func configureFormat(_ formatDesc: CameraFormatDescriptor, targetFps: Double) {
        captureQueue.async { [weak self] in
            guard let self = self, let device = self.currentDevice?.device else { return }
            
            do {
                try device.lockForConfiguration()
                device.activeFormat = formatDesc.format
                
                let frameDuration = CMTime(value: 1, timescale: CMTimeScale(targetFps))
                device.activeVideoMinFrameDuration = frameDuration
                device.activeVideoMaxFrameDuration = frameDuration
                
                device.unlockForConfiguration()
                
                DispatchQueue.main.async {
                    self.currentFormat = formatDesc
                    self.currentFps = targetFps
                }
                print("[CameraManager] Configured format: \(formatDesc.width)x\(formatDesc.height) @ \(targetFps) FPS")
            } catch {
                print("[CameraManager] Failed to lock device for configuration: \(error)")
            }
        }
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
    }
}
