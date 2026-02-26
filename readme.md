
# RGM - README

A zero-configuration screen sharing application that allows you to share your 
screen across a network with minimal setup. Uses SSDP for automatic discovery.

```

╔════════════════════════════════════════════════════════════════════════════╗
║                            TABLE OF CONTENTS                               ║
╠════════════════════════════════════════════════════════════════════════════╣
║ 1. FEATURES                                                                ║
║ 2. SYSTEM REQUIREMENTS                                                     ║
║ 3. INSTALLATION                                                            ║
║ 4. BUILDING FROM SOURCE                                                    ║
║ 5. USAGE GUIDE                                                             ║
║ 6. COMMAND LINE REFERENCE                                                  ║
║ 7. TROUBLESHOOTING                                                         ║
║ 8. NETWORK REQUIREMENTS                                                    ║
║ 9. FUTURE UPGRADE POINTS                                                   ║
║ 10. LICENSE & CREDITS                                                      ║
╚════════════════════════════════════════════════════════════════════════════╝

```

---

## 1. FEATURES

- ✓ **Zero Configuration** - Automatic discovery via SSDP (no IP addresses needed)
- ✓ **Cross-Platform** - Works on Linux, with Windows support planned
- ✓ **High Performance** - 60 FPS streaming with minimal latency
- ✓ **Auto Resolution Detection** - Automatically detects and adapts to screen size
- ✓ **Splash Screen** - Professional startup with RGM logo
- ✓ **Statistics Display** - Real-time FPS and bandwidth monitoring
- ✓ **Multiple Modes** - Launcher app with menu, or direct sender/receiver execution
- ✓ **Resizable Window** - Receiver window can be scaled while maintaining aspect ratio
- ✓ **TCP Optimization** - Disabled Nagle's algorithm for reduced latency
- ✓ **Large Buffers** - 4MB socket buffers for smooth high-FPS streaming

---

## 2. SYSTEM REQUIREMENTS

### Hardware
- **CPU:** Any modern processor (1GHz+ recommended)
- **RAM:** 256MB minimum, 512MB recommended
- **Network:** 100Mbps Ethernet or good WiFi (1Gbps recommended for 60 FPS)
- **Display:** Any resolution supported (tested up to 4K)

### Software (Linux)
- **Operating System:** Linux (Ubuntu/Debian, Fedora, Arch, etc.)
- **Compiler:** g++ 4.8+ with C++11 support
- **Libraries:**
  - libX11-dev (X11 development libraries)
  - libsdl2-dev (SDL2 for graphics)
  - make (build tool)
  - pthread (threading support)

---

## 3. INSTALLATION

### A. Quick Install (Ubuntu/Debian)

```bash
# Clone or download the source
cd ~/RGM

# Install dependencies
sudo apt-get update
sudo apt-get install g++ make libx11-dev libsdl2-dev

# Build the application
make clean
make

# Run the launcher
./app
```

### B. Other Distributions

**Fedora/RHEL/CentOS:**

```bash
sudo yum install gcc-c++ make libX11-devel SDL2-devel
```

**Arch Linux:**

```bash
sudo pacman -S gcc make libx11 sdl2
```

**Or use the built-in installer:**

```bash
make install-deps
```

---

## 4. BUILDING FROM SOURCE

### Directory Structure

```
RGM/
├── Makefile              # Build configuration
├── src/
│   ├── app.cpp          # Launcher application
│   ├── sender.cpp       # Screen sender
│   ├── receiver.cpp     # Screen receiver
│   ├── discover.cpp     # SSDP discovery engine
│   └── discover.h       # Discovery API
├── assets/
│   └── icons/
│       └── RGM.png      # Optional logo (BMP format)
└── README.md            # This file
```

### Build Commands

```bash
# Clean build (removes all binaries and object files)
make clean

# Build everything (default)
make

# Build specific components
make app          # Build only the launcher
make sender       # Build only the sender
make receiver     # Build only the receiver

# Build with debug symbols
make debug

# Check build environment
make check
```

### Compilation Output

After successful build, you'll have these executables:

- `app`       - Launcher application (menu-driven)
- `sender`    - Screen sender (streams your screen)
- `receiver`  - Screen receiver (displays received stream)

---

## 5. USAGE GUIDE

### A. Basic Usage (Recommended)

**On the receiving computer:**

```bash
./receiver
# or
make run-receiver
```

**On the sending computer:**

```bash
./sender
# or
make run-sender
```

1. The sender will automatically discover receivers on the network
2. Select the receiver from the list
3. Streaming begins automatically

### B. Using the Launcher

```bash
./app
# or
make run
```

The launcher provides a menu:

```
╔═══════════════════════════════════════╗
║        RGM SCREEN SHARE v2.0          ║
╠═══════════════════════════════════════╣
║                                       ║
║  1. 🎥 SEND SCREEN                    ║
║  2. 📺 RECEIVE SCREEN                 ║
║  3. ❌ EXIT                           ║
║                                       ║
╚═══════════════════════════════════════╝
```

### C. Demo Mode

To see instructions for running a demo:

```bash
make run-demo
```

This shows how to run sender and receiver in separate terminals.

---

## 6. COMMAND LINE REFERENCE

### Makefile Commands

| Command | Description |
|---------|-------------|
| `make` | Build all components |
| `make app` | Build launcher only |
| `make sender` | Build sender only |
| `make receiver` | Build receiver only |
| `make clean` | Remove build files |
| `make run` | Run launcher app |
| `make run-sender` | Run sender directly |
| `make run-receiver` | Run receiver directly |
| `make run-demo` | Show demo instructions |
| `make debug` | Build with debug symbols |
| `make check` | Check build environment |
| `make install-deps` | Install dependencies |
| `make help` | Show this help |

### Application Controls (Receiver)

- `ESC` or `Q` - Quit the receiver
- Window resize - Automatically scales the video
- Close window - Stop receiving

### Application Controls (Sender)

- `Ctrl+C` - Stop sending and exit
- Automatic discovery - Lists available receivers

---

## 7. TROUBLESHOOTING

| Problem | Solution |
|---------|----------|
| **"No receivers found!"** | ✓ Ensure receiver is running on another machine<br>✓ Check firewall settings (UDP 1900, TCP 8081)<br>✓ Verify both machines are on the same network<br>✓ Try disabling firewall temporarily for testing<br>✓ Run `make check` to verify build environment |
| **"Connection refused" or timeout** | ✓ Verify receiver is running (`ps aux \| grep receiver`)<br>✓ Check if port 8081 is open: `netstat -tulpn \| grep 8081`<br>✓ Try telnet: `telnet <receiver-ip> 8081`<br>✓ Check for other applications using port 8081 |
| **Poor performance or low FPS** | ✓ Check network speed (1Gbps recommended for 60 FPS)<br>✓ Reduce screen resolution if necessary<br>✓ Close bandwidth-heavy applications<br>✓ Use wired Ethernet instead of WiFi<br>✓ Check CPU usage on both machines |
| **Build fails with "missing separator"** | ✓ Ensure Makefile uses tabs, not spaces, for indentation<br>✓ Run `make clean` before rebuilding |
| **SDL2 not found during build** | ✓ Install SDL2: `sudo apt-get install libsdl2-dev`<br>✓ On Fedora: `sudo yum install SDL2-devel`<br>✓ On Arch: `sudo pacman -S sdl2` |
| **X11 errors on Linux** | ✓ Install X11 dev: `sudo apt-get install libx11-dev`<br>✓ Ensure you're running in a graphical environment<br>✓ Check DISPLAY variable: `echo $DISPLAY` |

---

## 8. NETWORK REQUIREMENTS

### Ports Used

- **UDP 1900** - SSDP discovery (multicast)
- **TCP 8081** - Video streaming

### Firewall Configuration

**For Linux (using iptables):**

```bash
# Allow SSDP discovery
sudo iptables -A INPUT -p udp --dport 1900 -j ACCEPT
sudo iptables -A OUTPUT -p udp --sport 1900 -j ACCEPT

# Allow video stream
sudo iptables -A INPUT -p tcp --dport 8081 -j ACCEPT
sudo iptables -A OUTPUT -p tcp --sport 8081 -j ACCEPT
```

**For Ubuntu with UFW:**

```bash
sudo ufw allow 1900/udp
sudo ufw allow 8081/tcp
```

### Network Topology

- Works best on same subnet (broadcast domain)
- Supports WiFi but Ethernet recommended for 60 FPS
- Multicast must be enabled on network switches
- Works across VLANs if multicast routing is configured

### Bandwidth Requirements

| Resolution | FPS | Bandwidth (approx) |
|------------|-----|-------------------|
| 1920x1080 | 60 | ~280 MB/s |
| 1920x1080 | 30 | ~140 MB/s |
| 1280x720 | 60 | ~125 MB/s |
| 1280x720 | 30 | ~62 MB/s |

*Note: Actual bandwidth depends on screen content complexity*

---

## 9. FUTURE UPGRADE POINTS

The codebase is designed with future enhancements in mind:

### Planned Features

- [ ] **Video Compression** - H.264/H.265 support for bandwidth reduction
- [ ] **Audio Capture** - Stream system audio along with video
- [ ] **Encryption** - SSL/TLS for secure streaming
- [ ] **Multi-monitor Support** - Select which display to share
- [ ] **Region Selection** - Share only a portion of the screen
- [ ] **Adaptive FPS** - Adjust based on network conditions
- [ ] **Pause/Resume** - Control streaming without disconnecting
- [ ] **Authentication** - Password protection for receivers
- [ ] **Recording** - Save streams to disk on receiver side
- [ ] **Picture-in-Picture** - Multiple simultaneous senders
- [ ] **Wayland Support** - Additional display server support
- [ ] **GPU Acceleration** - DirectX/OpenGL/Vulkan capture

---

## 10. AUTHOR & CREDITS

### Author

**RGM** is created and maintained by **[RR-Ralefaso (polaris)](https://github.com/RR-Ralefaso)** - an experienced Software Developer, Systems Analyst, and aspiring researcher focused on Applied Mathematics and Computer Science.

- **GitHub:** [@RR-Ralefaso](https://github.com/RR-Ralefaso)
- **Areas of Expertise:** Backend systems, AI/ML, Computer Vision, Systems Programming
- **Technical Stack:** C++, Python, Rust, Go, Java, and more

### Core Contributions

- Architecture design and implementation
- SSDP discovery protocol integration
- Cross-platform socket handling
- SDL2 integration for display
- Performance optimizations

### Acknowledgments

Special thanks to:

- The open-source community
- SDL2 development team
- X11 and network programming communities
- All contributors and users

### Version History

| Version | Date | Changes |
|---------|------|---------|
| v2.0 | 2024 | Complete rewrite with SSDP discovery, SDL2 interface, improved performance |
| v1.0 | 2023 | Initial release with basic TCP streaming |

### Support

For issues, questions, or contributions:

- Check the troubleshooting section first
- Verify network and firewall settings
- Ensure all dependencies are installed
- Run `make check` for environment diagnostics

### Acknowledgments

Special thanks to:

- The SDL2 development team
- X11 and network programming communities
- Open-source contributors

---

```
      ╔══════════════════════════════════════╗
      ║  Thank you for using RGM             ║
      ║     Share your screen, simply.       ║
      ╚══════════════════════════════════════╝
```


---

<div align="center">
  <img src="assets/icons/rcorp.jpeg" alt="R-Corp Logo" width="100"/>
  <p><strong>Created by RR-Ralefaso (polaris)</strong></p>
  <p> ROOT ACCESS FOR EVERYONE </p>
</div>


```

╔════════════════════════════════════════════════════════════════════════════╗
║                                                                            ║
║   "Seeking to solve complex business problems through analytical          ║
║    precision and elegant code - on any platform."                         ║
║                                                                            ║
║                    - RR-Ralefaso (polaris)                                ║
║                                                                            ║
╚════════════════════════════════════════════════════════════════════════════╝

🌐 **Linux** • **Windows 10/11** • **macOS** - One codebase, all platforms.

```
