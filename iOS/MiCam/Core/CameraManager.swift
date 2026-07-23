import Foundation
import AVFoundation
import UIKit

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

public class CameraManager: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate {
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
    
    // Dynamically discover all physical camera devices without hardcoded assumptions
    public func discoverDevices() {
        var deviceTypes: [AVCaptureDevice.DeviceType] = [
            .builtInWideAngleCamera,
            .builtInTelephotoCamera,
            .builtInUltraWideCamera
        ]
        
        if #available(iOS 13.0, *) {
            deviceTypes.append(.builtInTripleCamera)
            deviceTypes.append(.builtInDualWideCamera)
        }
        
        let discoverySession = AVCaptureDevice.DiscoverySession(
            deviceTypes: deviceTypes,
            mediaType: .video,
            position: .unspecified
        )
        
        var descriptors: [CameraDeviceDescriptor] = []
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
            
            let desc = CameraDeviceDescriptor(
                id: dev.uniqueID,
                name: dev.localizedName,
                lensType: lens,
                position: dev.position,
                device: dev
            )
            descriptors.append(desc)
        }
        
        DispatchQueue.main.async {
            self.availableDevices = descriptors
            if self.currentDevice == nil, let first = descriptors.first {
                self.selectDevice(first)
            }
        }
    }
    
    public func selectDevice(_ descriptor: CameraDeviceDescriptor) {
        captureSession.beginConfiguration()
        
        if let currentInput = activeDeviceInput {
            captureSession.removeInput(currentInput)
        }
        
        do {
            let newInput = try AVCaptureDeviceInput(device: descriptor.device)
            if captureSession.canAddInput(newInput) {
                captureSession.addInput(newInput)
                activeDeviceInput = newInput
                currentDevice = descriptor
                
                // Query dynamic formats for this device
                populateFormats(for: descriptor.device)
            }
        } catch {
            print("[CameraManager] Failed to create input for device \(descriptor.name): \(error)")
        }
        
        if !captureSession.outputs.contains(videoDataOutput) {
            videoDataOutput.alwaysDiscardsLateVideoFrames = true
            videoDataOutput.videoSettings = [
                kCVPixelBufferPixelFormatTypeKey as String: Int(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)
            ]
            videoDataOutput.setSampleBufferDelegate(self, queue: captureQueue)
            
            if captureSession.canAddOutput(videoDataOutput) {
                captureSession.addOutput(videoDataOutput)
            }
        }
        
        captureSession.commitConfiguration()
    }
    
    // Interrogate formats directly from device to allow ANY resolution/FPS hardware allows (720p, 1080p, 4K, 60, 120, 240 fps)
    private func populateFormats(for device: AVCaptureDevice) {
        var formats: [CameraFormatDescriptor] = []
        
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
            
            let id = "\(dimensions.width)x\(dimensions.height)@\(Int(maxFps))fps"
            let desc = CameraFormatDescriptor(
                id: id,
                width: dimensions.width,
                height: dimensions.height,
                maxFps: maxFps,
                minFps: minFps,
                format: format
            )
            formats.append(desc)
        }
        
        // Sort descending by resolution and FPS
        formats.sort { ($0.width * $0.height, $0.maxFps) > ($1.width * $1.height, $1.maxFps) }
        
        DispatchQueue.main.async {
            self.availableFormats = formats
            if let best = formats.first {
                self.configureFormat(best, targetFps: min(best.maxFps, 60.0))
            }
        }
    }
    
    public func configureFormat(_ formatDesc: CameraFormatDescriptor, targetFps: Double) {
        guard let device = currentDevice?.device else { return }
        
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
    
    // Real-time Controls (Focus, Exposure, White Balance, Torch, Zoom, Stabilization)
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
