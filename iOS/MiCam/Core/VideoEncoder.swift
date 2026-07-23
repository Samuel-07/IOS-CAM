import Foundation
import VideoToolbox
import CoreMedia

public protocol VideoEncoderDelegate: AnyObject {
    func videoEncoder(_ encoder: VideoEncoder, didOutputSampleData data: Data, isKeyFrame: Bool, timestampUs: UInt64)
}

public class VideoEncoder {
    public weak var delegate: VideoEncoderDelegate?
    
    private var compressionSession: VTCompressionSession?
    private var width: Int32 = 1920
    private var height: Int32 = 1080
    private var codec: MiCamCodecType = .h264
    private var fps: Int32 = 30
    private var bitrate: Int32 = 8000000 // 8 Mbps default
    
    public init() {}
    
    deinit {
        stopSession()
    }
    
    public func configure(width: Int32, height: Int32, codec: MiCamCodecType = .h264, fps: Int32 = 30, bitrate: Int32 = 8000000) {
        stopSession()
        
        self.width = width
        self.height = height
        self.codec = codec
        self.fps = fps
        self.bitrate = bitrate
        
        let codecType: CMVideoCodecType = (codec == .hevc) ? kCMVideoCodecType_HEVC : kCMVideoCodecType_H264
        
        let status = VTCompressionSessionCreate(
            allocator: kCFAllocatorDefault,
            width: width,
            height: height,
            codecType: codecType,
            encoderSpecification: nil,
            imageBufferAttributes: [
                kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
            ] as CFDictionary,
            compressedDataAllocator: nil,
            outputCallback: compressionOutputCallback,
            refcon: Unmanaged.passUnretained(self).toOpaque(),
            compressionSessionOut: &compressionSession
        )
        
        guard status == noErr, let session = compressionSession else {
            print("[VideoEncoder] Failed to create VTCompressionSession status: \(status)")
            return
        }
        
        // Zero latency & low-overhead configuration
        VTSessionSetProperty(session, key: kVTCompressionPropertyKey_RealTime, value: kCFBooleanTrue)
        VTSessionSetProperty(session, key: kVTCompressionPropertyKey_AllowFrameReordering, value: kCFBooleanFalse) // Disable B-frames for minimum latency
        VTSessionSetProperty(session, key: kVTCompressionPropertyKey_MaxKeyFrameInterval, value: fps as CFNumber)
        VTSessionSetProperty(session, key: kVTCompressionPropertyKey_AverageBitRate, value: bitrate as CFNumber)
        VTSessionSetProperty(session, key: kVTCompressionPropertyKey_ExpectedFrameRate, value: fps as CFNumber)
        
        VTCompressionSessionPrepareForFrames(session)
        print("[VideoEncoder] Compression Session Initialized \(width)x\(height) @ \(fps) FPS, Bitrate: \(bitrate)")
    }
    
    public func encode(pixelBuffer: CVPixelBuffer, pts: CMTime) {
        guard let session = compressionSession else { return }
        
        var flags: VTEncodeInfoFlags = []
        let status = VTCompressionSessionEncodeFrame(
            session,
            imageBuffer: pixelBuffer,
            presentationTimeStamp: pts,
            duration: .invalid,
            frameProperties: nil,
            sourceFrameRefcon: nil,
            infoFlagsOut: &flags
        )
        
        if status != noErr {
            print("[VideoEncoder] VTCompressionSessionEncodeFrame error: \(status)")
        }
    }
    
    public func stopSession() {
        if let session = compressionSession {
            VTCompressionSessionInvalidate(session)
            compressionSession = nil
        }
    }
    
    fileprivate func handleEncodedFrame(status: OSStatus, infoFlags: VTEncodeInfoFlags, sampleBuffer: CMSampleBuffer?) {
        guard status == noErr, let sampleBuffer = sampleBuffer, CMSampleBufferDataIsReady(sampleBuffer) else { return }
        
        let isKeyFrame: Bool
        if let attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, createIfNecessary: false) as? [[CFString: Any]],
           let notSync = attachments.first?[kCMSampleAttachmentKey_NotSync] as? Bool {
            isKeyFrame = !notSync
        } else {
            isKeyFrame = true
        }
        
        guard let dataBuffer = CMSampleBufferGetDataBuffer(sampleBuffer) else { return }
        
        var totalLength: Int = 0
        var dataPointer: UnsafeMutablePointer<Char>?
        let lengthStatus = CMBlockBufferGetDataPointer(dataBuffer, atOffset: 0, lengthAtOffsetOut: nil, totalLengthOut: &totalLength, dataPointerOut: &dataPointer)
        
        guard lengthStatus == noErr, let ptr = dataPointer else { return }
        
        // Extract raw Annex-B NALUs
        var naluData = Data()
        if isKeyFrame {
            if let parameterSetData = extractFormatDescriptionParameterSets(sampleBuffer: sampleBuffer) {
                naluData.append(parameterSetData)
            }
        }
        
        // Convert length-prefixed NALUs to Annex-B start codes (0x00 0x00 0x00 0x01)
        var bufferOffset = 0
        let naluHeaderLength = 4
        while bufferOffset < totalLength - naluHeaderLength {
            var naluLength: UInt32 = 0
            memcpy(&naluLength, ptr + bufferOffset, 4)
            naluLength = CFSwapInt32BigToHost(naluLength)
            
            let startCode: [UInt8] = [0x00, 0x00, 0x00, 0x01]
            naluData.append(contentsOf: startCode)
            naluData.append(Data(bytes: ptr + bufferOffset + naluHeaderLength, count: Int(naluLength)))
            
            bufferOffset += Int(naluHeaderLength + naluLength)
        }
        
        let timestampUs = UInt64(CMSampleBufferGetPresentationTimeStamp(sampleBuffer).seconds * 1_000_000)
        delegate?.videoEncoder(self, didOutputSampleData: naluData, isKeyFrame: isKeyFrame, timestampUs: timestampUs)
    }
    
    private func extractFormatDescriptionParameterSets(sampleBuffer: CMSampleBuffer) -> Data? {
        guard let format = CMSampleBufferGetFormatDescription(sampleBuffer) else { return nil }
        var result = Data()
        let startCode: [UInt8] = [0x00, 0x00, 0x00, 0x01]
        
        var paramCount: Int = 0
        CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, atIndex: 0, parameterSetPointerOut: nil, parameterSetSizeOut: nil, parameterSetCountOut: &paramCount, naluHeaderLengthOut: nil)
        
        for i in 0..<paramCount {
            var ptr: UnsafePointer<UInt8>?
            var size: Int = 0
            CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, atIndex: i, parameterSetPointerOut: &ptr, parameterSetSizeOut: &size, parameterSetCountOut: nil, naluHeaderLengthOut: nil)
            if let ptr = ptr {
                result.append(contentsOf: startCode)
                result.append(Data(bytes: ptr, count: size))
            }
        }
        return result.isEmpty ? nil : result
    }
}

private func compressionOutputCallback(
    outputCallbackRefCon: UnsafeMutableRawPointer?,
    sourceFrameRefCon: UnsafeMutableRawPointer?,
    status: OSStatus,
    infoFlags: VTEncodeInfoFlags,
    sampleBuffer: CMSampleBuffer?
) {
    guard let refCon = outputCallbackRefCon else { return }
    let encoder = Unmanaged<VideoEncoder>.fromOpaque(refCon).takeUnretainedValue()
    encoder.handleEncodedFrame(status: status, infoFlags: infoFlags, sampleBuffer: sampleBuffer)
}
