import SwiftUI
import AVFoundation

struct ContentView: View {
    @ObservedObject var cameraManager = CameraManager.shared
    @ObservedObject var streamer = NetworkStreamer.shared
    
    @State private var torchOn: Bool = false
    @State private var zoomFactor: CGFloat = 1.0
    @State private var lastRearLensIndex: Int = 0 // Remembers the last non-front lens so the
    // Front button can flip back to it (see the lens button action below).
    @State private var showSettingsSheet: Bool = false
    
    var body: some View {
        ZStack {
            // Edge-to-edge dark backdrop
            Color.black.edgesIgnoringSafeArea(.all)
            
            // 1. Fullscreen Camera Viewport
            CameraPreviewView(session: cameraManager.captureSession)
                .edgesIgnoringSafeArea(.all)
            
            // 2. Camera Grid Overlay (Rule of Thirds)
            GeometryReader { geo in
                Path { path in
                    // Vertical grid lines
                    path.move(to: CGPoint(x: geo.size.width / 3, y: 0))
                    path.addLine(to: CGPoint(x: geo.size.width / 3, y: geo.size.height))
                    path.move(to: CGPoint(x: (geo.size.width / 3) * 2, y: 0))
                    path.addLine(to: CGPoint(x: (geo.size.width / 3) * 2, y: geo.size.height))
                    
                    // Horizontal grid lines
                    path.move(to: CGPoint(x: 0, y: geo.size.height / 3))
                    path.addLine(to: CGPoint(x: geo.size.width, y: geo.size.height / 3))
                    path.move(to: CGPoint(x: 0, y: (geo.size.height / 3) * 2))
                    path.addLine(to: CGPoint(x: geo.size.width, y: (geo.size.height / 3) * 2))
                }
                .stroke(Color.white.opacity(0.12), lineWidth: 1)
            }
            .edgesIgnoringSafeArea(.all)
            
            // 3. Pro HUD Controls Overlay
            VStack {
                // Top Header Bar
                HStack(spacing: 12) {
                    // Logo & App Name
                    HStack(spacing: 8) {
                        Image(systemName: "camera.aperture")
                            .font(.system(size: 18, weight: .bold))
                            .foregroundColor(Color(red: 0.0, green: 0.94, blue: 1.0))
                        Text("MiCam Pro")
                            .font(.system(size: 16, weight: .bold, design: .rounded))
                            .foregroundColor(.white)
                    }
                    .padding(.horizontal, 12)
                    .padding(.vertical, 8)
                    .background(Color.black.opacity(0.65))
                    .cornerRadius(20)
                    .overlay(
                        RoundedRectangle(cornerRadius: 20)
                            .stroke(Color.white.opacity(0.15), lineWidth: 1)
                    )
                    
                    Spacer()
                    
                    // Connection Mode Pill
                    HStack(spacing: 6) {
                        Circle()
                            .fill(streamer.isConnected ? Color.green : Color.orange)
                            .frame(width: 8, height: 8)
                        Text(streamer.isConnected ? streamer.currentConnectionType : "READY TO STREAM")
                            .font(.system(size: 11, weight: .bold))
                            .foregroundColor(.white)
                    }
                    .padding(.horizontal, 12)
                    .padding(.vertical, 8)
                    .background(streamer.isConnected ? Color.green.opacity(0.25) : Color.black.opacity(0.65))
                    .cornerRadius(20)
                    .overlay(
                        RoundedRectangle(cornerRadius: 20)
                            .stroke(streamer.isConnected ? Color.green.opacity(0.5) : Color.white.opacity(0.15), lineWidth: 1)
                    )
                }
                .padding(.horizontal, 16)
                .padding(.top, 45)
                
                // Live Stream Metrics Banner
                HStack {
                    if let fmt = cameraManager.currentFormat {
                        HStack(spacing: 12) {
                            Text("\(fmt.width)x\(fmt.height)")
                                .font(.system(size: 12, weight: .bold, design: .monospaced))
                                .foregroundColor(Color(red: 0.0, green: 0.94, blue: 1.0))
                            
                            Text("\(Int(cameraManager.currentFps)) FPS")
                                .font(.system(size: 12, weight: .bold, design: .monospaced))
                                .foregroundColor(.white)
                            
                            Text("ZERO-COPY VT")
                                .font(.system(size: 10, weight: .bold))
                                .padding(.horizontal, 6)
                                .padding(.vertical, 2)
                                .background(Color.purple.opacity(0.4))
                                .cornerRadius(4)
                        }
                        .padding(.horizontal, 12)
                        .padding(.vertical, 6)
                        .background(Color.black.opacity(0.6))
                        .cornerRadius(12)
                    }
                    Spacer()
                }
                .padding(.horizontal, 16)
                .padding(.top, 4)
                
                Spacer()
                
                // Lens Selector Dial & Optics Bar
                VStack(spacing: 14) {
                    // Quick Lens Switcher Pills (0.5x, 1x, 3x, Front)
                    //
                    // The highlighted button now reflects cameraManager.currentDevice directly
                    // instead of a separate selectedLensIndex state that was set optimistically
                    // the instant a button was tapped, regardless of whether the underlying
                    // AVCaptureSession switch actually succeeded. That desync is what made the
                    // Front button look "stuck": it visually stayed highlighted as selected
                    // even on attempts where the real device never changed, so tapping it again
                    // (meant to flip back to the rear lens) evaluated against the wrong state
                    // and looked like it did nothing.
                    HStack(spacing: 14) {
                        ForEach(Array(cameraManager.availableDevices.enumerated()), id: \.element.id) { index, device in
                            let isSelected = cameraManager.currentDevice?.id == device.id
                            Button(action: {
                                print("[ContentView] Lens tap: button=\(device.lensType) tappedIndex=\(index) currentDevice=\(String(describing: cameraManager.currentDevice?.lensType)) lastRearLensIndex=\(lastRearLensIndex)")
                                if device.lensType == .front, cameraManager.currentDevice?.lensType == .front {
                                    // Already on the front camera and this IS the front button -
                                    // treat it as a flip-camera toggle back to whichever rear
                                    // lens was active before.
                                    let backIndex = lastRearLensIndex
                                    guard cameraManager.availableDevices.indices.contains(backIndex) else {
                                        print("[ContentView] Toggle: backIndex \(backIndex) out of range (count=\(cameraManager.availableDevices.count))")
                                        return
                                    }
                                    let target = cameraManager.availableDevices[backIndex]
                                    print("[ContentView] Toggle: switching back to \(target.lensType) at index \(backIndex)")
                                    cameraManager.selectDevice(target)
                                    return
                                }
                                if device.lensType != .front {
                                    lastRearLensIndex = index
                                }
                                print("[ContentView] Normal select: \(device.lensType)")
                                cameraManager.selectDevice(device)
                            }) {
                                Text(lensDisplayName(for: device))
                                    .font(.system(size: 13, weight: isSelected ? .bold : .medium))
                                    .frame(minWidth: 44, minHeight: 44)
                                    .foregroundColor(isSelected ? .black : .white)
                                    .background(isSelected ? Color(red: 0.0, green: 0.94, blue: 1.0) : Color.black.opacity(0.6))
                                    .clipShape(Circle())
                                    .overlay(
                                        Circle()
                                            .stroke(isSelected ? Color(red: 0.0, green: 0.94, blue: 1.0) : Color.white.opacity(0.2), lineWidth: 1.5)
                                    )
                            }
                        }
                    }
                    
                    // Controls Slider & Toggles
                    HStack(spacing: 24) {
                        // Torch Toggle
                        Button(action: {
                            torchOn.toggle()
                            cameraManager.setTorch(enabled: torchOn)
                        }) {
                            VStack(spacing: 4) {
                                Image(systemName: torchOn ? "flashlight.on.fill" : "flashlight.off.fill")
                                    .font(.system(size: 20))
                                Text("Torch")
                                    .font(.system(size: 10, weight: .bold))
                            }
                            .foregroundColor(torchOn ? .yellow : .white)
                            .frame(width: 60, height: 60)
                            .background(Color.black.opacity(0.65))
                            .cornerRadius(16)
                        }
                        
                        // Zoom 2x Quick Toggle
                        Button(action: {
                            zoomFactor = (zoomFactor == 1.0) ? 2.0 : 1.0
                            cameraManager.setZoom(factor: zoomFactor)
                        }) {
                            VStack(spacing: 4) {
                                Text("\(String(format: "%.1f", zoomFactor))x")
                                    .font(.system(size: 16, weight: .bold, design: .monospaced))
                                Text("Zoom")
                                    .font(.system(size: 10, weight: .bold))
                            }
                            .foregroundColor(.white)
                            .frame(width: 60, height: 60)
                            .background(Color.black.opacity(0.65))
                            .cornerRadius(16)
                        }
                        
                        // Settings Sheet Toggle
                        Button(action: {
                            showSettingsSheet.toggle()
                        }) {
                            VStack(spacing: 4) {
                                Image(systemName: "slider.horizontal.3")
                                    .font(.system(size: 20))
                                Text("Settings")
                                    .font(.system(size: 10, weight: .bold))
                            }
                            .foregroundColor(.white)
                            .frame(width: 60, height: 60)
                            .background(Color.black.opacity(0.65))
                            .cornerRadius(16)
                        }
                    }
                }
                .padding(.bottom, 30)
            }
        }
        .sheet(isPresented: $showSettingsSheet) {
            SettingsSheetView(cameraManager: cameraManager, streamer: streamer)
        }
        .onAppear {
            cameraManager.startSession()
            streamer.startServer()
        }
    }
    
    private func lensDisplayName(for device: CameraDeviceDescriptor) -> String {
        switch device.lensType {
        case .ultraWide: return "0.5x"
        case .wide: return "1x"
        case .telephoto: return "3x"
        case .front: return "Front"
        case .macro: return "Macro"
        }
    }
}

// Pro Camera Settings Modal Sheet
struct SettingsSheetView: View {
    @ObservedObject var cameraManager: CameraManager
    @ObservedObject var streamer: NetworkStreamer
    @Environment(\.presentationMode) var presentationMode
    
    @State private var selectedFps: Double = 30.0
    
    var body: some View {
        NavigationView {
            ZStack {
                Color(red: 0.05, green: 0.05, blue: 0.08).edgesIgnoringSafeArea(.all)
                
                ScrollView {
                    VStack(alignment: .leading, spacing: 20) {
                        resolutionSection
                        fpsSection
                        serverStatusSection
                    }
                    .padding(20)
                }
            }
            .navigationBarTitle("MiCam Pro Studio Settings", displayMode: .inline)
            .navigationBarItems(trailing: Button("Done") {
                presentationMode.wrappedValue.dismiss()
            })
        }
    }
    
    private var resolutionSection: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("STREAM RESOLUTION")
                .font(.system(size: 11, weight: .bold))
                .foregroundColor(Color(red: 0.0, green: 0.94, blue: 1.0))
            
            ForEach(cameraManager.availableFormats, id: \.id) { fmt in
                formatRow(for: fmt)
            }
        }
    }
    
    private func formatRow(for fmt: CameraFormatDescriptor) -> some View {
        let isSelected = cameraManager.currentFormat?.id == fmt.id
        return Button(action: {
            cameraManager.configureFormat(fmt, targetFps: selectedFps)
        }) {
            HStack {
                Image(systemName: fmt.isPortrait ? "rectangle.portrait" : "rectangle")
                    .font(.system(size: 12))
                    .foregroundColor(.gray)
                Text("\(fmt.width) x \(fmt.height)")
                    .font(.system(size: 14, weight: .bold, design: .monospaced))
                    .foregroundColor(.white)
                Spacer()
                if isSelected {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundColor(Color(red: 0.0, green: 0.94, blue: 1.0))
                }
            }
            .padding(14)
            .background(Color.white.opacity(0.06))
            .cornerRadius(12)
        }
    }
    
    private var fpsSection: some View {
        // Only offers FPS values the currently selected format actually supports - picking an
        // unsupported rate used to crash the app (AVFoundation raises an uncatchable exception
        // when activeVideoMinFrameDuration/MaxFrameDuration falls outside the active format's
        // own videoSupportedFrameRateRanges).
        let options = cameraManager.currentFormat.map { cameraManager.availableFpsOptions(for: $0) } ?? [24.0, 30.0]

        return VStack(alignment: .leading, spacing: 10) {
            Text("TARGET FPS")
                .font(.system(size: 11, weight: .bold))
                .foregroundColor(Color(red: 0.0, green: 0.94, blue: 1.0))

            HStack(spacing: 12) {
                ForEach(options, id: \.self) { fps in
                    fpsButton(fps: fps)
                }
            }
        }
        .onAppear {
            if let firstOption = options.first, !options.contains(selectedFps) {
                selectedFps = firstOption
            }
        }
    }
    
    private func fpsButton(fps: Double) -> some View {
        let isSelected = selectedFps == fps
        return Button(action: {
            selectedFps = fps
            if let currentFmt = cameraManager.currentFormat {
                cameraManager.configureFormat(currentFmt, targetFps: selectedFps)
            }
        }) {
            Text("\(Int(fps)) FPS")
                .font(.system(size: 13, weight: .bold))
                .frame(maxWidth: .infinity)
                .padding(.vertical, 10)
                .background(isSelected ? Color(red: 0.0, green: 0.94, blue: 1.0) : Color.white.opacity(0.08))
                .foregroundColor(isSelected ? .black : .white)
                .cornerRadius(10)
        }
    }
    
    private var serverStatusSection: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("STREAM SERVER STATUS")
                .font(.system(size: 11, weight: .bold))
                .foregroundColor(Color(red: 0.0, green: 0.94, blue: 1.0))
            
            VStack(spacing: 8) {
                HStack {
                    Text("Transport")
                        .foregroundColor(.gray)
                    Spacer()
                    Text("USB usbmuxd / WiFi")
                        .foregroundColor(.white)
                        .bold()
                }
                HStack {
                    Text("Port")
                        .foregroundColor(.gray)
                    Spacer()
                    Text("50000")
                        .foregroundColor(Color(red: 0.0, green: 0.94, blue: 1.0))
                        .bold()
                }
            }
            .font(.system(size: 13))
            .padding(14)
            .background(Color.white.opacity(0.06))
            .cornerRadius(12)
        }
    }
}

struct CameraPreviewView: UIViewRepresentable {
    let session: AVCaptureSession
    
    func makeUIView(context: Context) -> PreviewContainerView {
        let view = PreviewContainerView()
        view.videoPreviewLayer.session = session
        view.videoPreviewLayer.videoGravity = .resizeAspectFill
        return view
    }
    
    func updateUIView(_ uiView: PreviewContainerView, context: Context) {}
}

class PreviewContainerView: UIView {
    override class var layerClass: AnyClass {
        return AVCaptureVideoPreviewLayer.self
    }
    
    var videoPreviewLayer: AVCaptureVideoPreviewLayer {
        return layer as! AVCaptureVideoPreviewLayer
    }
}
