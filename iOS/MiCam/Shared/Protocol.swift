import Foundation

public struct ProtocolConstants {
    public static let magic: UInt32 = 0x4D43414D // "MCAM"
    public static let defaultPort: UInt16 = 50000
    public static let bonjourType = "_micam._tcp"
    public static let bonjourDomain = "local."
}

public enum MiCamPacketType: UInt8 {
    case handshakeReq   = 0x01
    case handshakeResp  = 0x02
    case videoConfigCmd = 0x10
    case videoConfigAck = 0x11
    case videoFrameData = 0x20
    case audioFrameData = 0x30
    case telemetryInfo  = 0x40
    case ping           = 0xFE
    case pong           = 0xFF
}

public enum MiCamCodecType: UInt8 {
    case h264 = 0x01
    case hevc = 0x02
}

public enum MiCamCameraLens: UInt8 {
    case wide         = 0x00
    case ultraWide    = 0x01
    case telephoto    = 0x02
    case front        = 0x03
    case macro        = 0x04
}

public struct MiCamPacketHeader {
    public var magic: UInt32 = ProtocolConstants.magic
    public var type: UInt8
    public var subType: UInt8 = 0
    public var reserved: UInt16 = 0
    public var payloadSize: UInt32
    public var timestampUs: UInt64
    
    public init(type: MiCamPacketType, subType: UInt8 = 0, payloadSize: UInt32, timestampUs: UInt64 = UInt64(Date().timeIntervalSince1970 * 1_000_000)) {
        self.type = type.rawValue
        self.subType = subType
        self.payloadSize = payloadSize
        self.timestampUs = timestampUs
    }
    
    public func toData() -> Data {
        var data = Data()
        var m = magic.bigEndian
        var t = type
        var st = subType
        var res = reserved.bigEndian
        var ps = payloadSize.bigEndian
        var ts = timestampUs.bigEndian
        
        data.append(Data(bytes: &m, count: 4))
        data.append(Data(bytes: &t, count: 1))
        data.append(Data(bytes: &st, count: 1))
        data.append(Data(bytes: &res, count: 2))
        data.append(Data(bytes: &ps, count: 4))
        data.append(Data(bytes: &ts, count: 8))
        return data
    }
}

public struct MiCamVideoFrameHeader {
    public var codec: UInt8
    public var isKeyFrame: UInt8
    public var width: UInt16
    public var height: UInt16
    public var fps: UInt16
    public var reserved: UInt16 = 0
    
    public init(codec: MiCamCodecType, isKeyFrame: Bool, width: UInt16, height: UInt16, fps: UInt16) {
        self.codec = codec.rawValue
        self.isKeyFrame = isKeyFrame ? 1 : 0
        self.width = width
        self.height = height
        self.fps = fps
    }
    
    public func toData() -> Data {
        var data = Data()
        var c = codec
        var kf = isKeyFrame
        var w = width.bigEndian
        var h = height.bigEndian
        var f = fps.bigEndian
        var res = reserved.bigEndian
        
        data.append(Data(bytes: &c, count: 1))
        data.append(Data(bytes: &kf, count: 1))
        data.append(Data(bytes: &w, count: 2))
        data.append(Data(bytes: &h, count: 2))
        data.append(Data(bytes: &f, count: 2))
        data.append(Data(bytes: &res, count: 2))
        return data
    }
}

public struct MiCamControlCommand {
    public var lens: UInt8
    public var targetWidth: UInt16
    public var targetHeight: UInt16
    public var targetFps: UInt16
    public var torchEnabled: UInt8
    public var zoomFactor: Float
    
    public init(lens: MiCamCameraLens = .wide, targetWidth: UInt16 = 1920, targetHeight: UInt16 = 1080, targetFps: UInt16 = 60, torchEnabled: Bool = false, zoomFactor: Float = 1.0) {
        self.lens = lens.rawValue
        self.targetWidth = targetWidth
        self.targetHeight = targetHeight
        self.targetFps = targetFps
        self.torchEnabled = torchEnabled ? 1 : 0
        self.zoomFactor = zoomFactor
    }
}

