import Foundation
import Network
import UIKit

public protocol NetworkStreamerDelegate: AnyObject {
    func streamer(_ streamer: NetworkStreamer, didReceiveControlCommand command: MiCamControlCommand)
    func streamer(_ streamer: NetworkStreamer, clientStateChanged isConnected: Bool, connectionType: String)
}

public class NetworkStreamer {
    public static let shared = NetworkStreamer()
    public weak var delegate: NetworkStreamerDelegate?
    
    private var listener: NWListener?
    private var activeConnection: NWConnection?
    private let queue = DispatchQueue(label: "com.micam.networkQueue", qos: .userInteractive)
    
    @Published public private(set) var isConnected: Bool = false
    @Published public private(set) var currentConnectionType: String = "Disconnected"
    
    public init() {}
    
    public func startServer(port: UInt16 = ProtocolConstants.defaultPort) {
        stopServer()
        
        do {
            let nwPort = NWEndpoint.Port(rawValue: port)!
            let parameters = NWParameters.tcp
            parameters.allowLocalEndpointReuse = true
            
            listener = try NWListener(using: parameters, on: nwPort)
            
            // Advertise via Bonjour / mDNS for automatic WiFi discovery on Windows
            let deviceName = UIDevice.current.name
            listener?.service = NWListener.Service(name: deviceName, type: ProtocolConstants.bonjourType)
            
            listener?.stateUpdateHandler = { state in
                switch state {
                case .ready:
                    print("[NetworkStreamer] Server listening on port \(port) and advertising Bonjour '\(deviceName)'")
                case .failed(let error):
                    print("[NetworkStreamer] Server failed with error: \(error)")
                default:
                    break
                }
            }
            
            listener?.newConnectionHandler = { [weak self] connection in
                self?.handleNewConnection(connection)
            }
            
            listener?.start(queue: queue)
        } catch {
            print("[NetworkStreamer] Failed to start listener: \(error)")
        }
    }
    
    public func stopServer() {
        activeConnection?.cancel()
        activeConnection = nil
        listener?.cancel()
        listener = nil
        
        DispatchQueue.main.async {
            self.isConnected = false
            self.currentConnectionType = "Disconnected"
        }
    }
    
    private func handleNewConnection(_ connection: NWConnection) {
        if activeConnection != nil {
            activeConnection?.cancel()
        }
        
        activeConnection = connection
        connection.stateUpdateHandler = { [weak self] state in
            guard let self = self else { return }
            switch state {
            case .ready:
                let endpointStr = "\(connection.endpoint)"
                let isUsb = endpointStr.contains("127.0.0.1") || endpointStr.contains("localhost")
                let connType = isUsb ? "USB (usbmuxd)" : "WiFi (mDNS)"
                
                DispatchQueue.main.async {
                    self.isConnected = true
                    self.currentConnectionType = connType
                    self.delegate?.streamer(self, clientStateChanged: true, connectionType: connType)
                }
                print("[NetworkStreamer] Client connected via \(connType)")
                self.readControlLoop(connection: connection)
                
            case .failed, .cancelled:
                DispatchQueue.main.async {
                    self.isConnected = false
                    self.currentConnectionType = "Disconnected"
                    self.delegate?.streamer(self, clientStateChanged: false, connectionType: "Disconnected")
                }
                print("[NetworkStreamer] Client disconnected")
                
            default:
                break
            }
        }
        
        connection.start(queue: queue)
    }
    
    // Low-latency packet transmission
    public func sendVideoFrame(naluData: Data, isKeyFrame: Bool, width: UInt16, height: UInt16, fps: UInt16, codec: MiCamCodecType, timestampUs: UInt64) {
        guard let connection = activeConnection, isConnected else { return }
        
        let frameHeader = MiCamVideoFrameHeader(codec: codec, isKeyFrame: isKeyFrame, width: width, height: height, fps: fps)
        let frameHeaderData = frameHeader.toData()
        
        let payloadSize = UInt32(frameHeaderData.count + naluData.count)
        let packetHeader = MiCamPacketHeader(type: .videoFrameData, subType: codec.rawValue, payloadSize: payloadSize, timestampUs: timestampUs)
        
        var packetData = Data()
        packetData.reserveCapacity(Int(16 + payloadSize))
        packetData.append(packetHeader.toData())
        packetData.append(frameHeaderData)
        packetData.append(naluData)
        
        connection.send(content: packetData, completion: .contentProcessed({ error in
            if let error = error {
                print("[NetworkStreamer] Send error: \(error)")
            }
        }))
    }
    
    // Command listener loop for remote settings adjustment from Windows
    private func readControlLoop(connection: NWConnection) {
        connection.receive(minimumIncompleteLength: 16, maximumLength: 65536) { [weak self] content, _, isComplete, error in
            guard let self = self else { return }
            
            if let data = content, data.count >= 16 {
                self.processIncomingPacket(data)
            }
            
            if !isComplete && error == nil {
                self.readControlLoop(connection: connection)
            }
        }
    }
    
    private func processIncomingPacket(_ data: Data) {
        let magic = data.withUnsafeBytes { $0.load(fromByteOffset: 0, as: UInt32.self).bigEndian }
        guard magic == ProtocolConstants.magic else { return }
        
        let typeRaw = data[4]
        guard let type = MiCamPacketType(rawValue: typeRaw) else { return }
        
        if type == .videoConfigCmd && data.count >= 16 + MemoryLayout<MiCamControlCommand>.size {
            let cmdData = data.subdata(in: 16..<16 + MemoryLayout<MiCamControlCommand>.size)
            let cmd = cmdData.withUnsafeBytes { $0.load(as: MiCamControlCommand.self) }
            delegate?.streamer(self, didReceiveControlCommand: cmd)
        }
    }
}
