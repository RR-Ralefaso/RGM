# RGM - Remote Screen Sharing

<div align="center">

**A zero-configuration screen sharing application with automatic network discovery**

[Star this Project](https://github.com/RR-Ralefaso/RGM) • [Become a Sponsor](https://github.com/sponsors/RR-Ralefaso) • [Report Issue](https://github.com/RR-Ralefaso/RGM/issues)

Your support helps maintain and improve RGM for everyone.

</div>

---

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Architecture](#architecture)
- [How It Works](#how-it-works)
- [System Requirements](#system-requirements)
- [Installation](#installation)
- [Building from Source](#building-from-source)
- [Usage Guide](#usage-guide)
- [Network Configuration](#network-configuration)
- [Performance Characteristics](#performance-characteristics)
- [Troubleshooting](#troubleshooting)
- [Roadmap](#roadmap)
- [Support the Project](#support-the-project)
- [License](#license)

---

## Overview

RGM is a lightweight, cross-platform screen sharing solution that eliminates complex network configuration. It implements SSDP (Simple Service Discovery Protocol) to automatically detect receiving hosts on your local network, enabling seamless screen streaming with minimal latency and high frame rates.

The application is designed for professionals who need to share screens across multiple machines without the overhead of configuring IP addresses, firewall rules, or connection parameters.

---

## Features

| Category | Capability |
|----------|------------|
| **Discovery** | Zero-configuration SSDP automatic detection |
| **Performance** | 60 FPS streaming, 4MB socket buffers, TCP_NODELAY optimization |
| **Platform Support** | Linux, Windows 10/11, macOS 10.15+ |
| **Display Handling** | Auto-resolution detection, resizable window, aspect ratio preservation |
| **Monitoring** | Real-time FPS counter, bandwidth utilization display |
| **User Interface** | Splash screen, menu-driven launcher, direct executable mode |

---

## Architecture

### Component Structure

```
RGM/
├── makefile                 # Build configuration
├── src/                     # Source code
│   ├── app.cpp             # Launcher application with menu interface
│   ├── sender.cpp          # Screen capture and transmission
│   ├── receiver.cpp        # Display and rendering
│   ├── discover.cpp        # SSDP protocol implementation
│   └── discover.h          # Discovery API definitions
├── assets/                  # Resources
│   └── icons/              # Application icons
│       ├── rcorp.jpeg      # Corporate logo
│       └── RGM.png         # Application logo
├── build/                   # Compiled objects
│   ├── app.o
│   ├── discover.o
│   ├── receiver.o
│   └── sender.o
├── app                      # Launcher executable
├── sender                   # Sender executable
├── receiver                 # Receiver executable
└── readme.md                # Documentation
```

### Application Components

| Executable | Description | Primary Function |
|------------|-------------|------------------|
| `app` | Launcher | Menu-driven interface for selecting send/receive mode |
| `sender` | Screen Sender | Captures display and streams to receiver |
| `receiver` | Screen Receiver | Displays incoming stream with hardware acceleration |

---

## How It Works

### Discovery Protocol (SSDP)

RGM implements the Simple Service Discovery Protocol (SSDP) to eliminate manual IP configuration. SSDP is the same discovery protocol used by Universal Plug and Play (UPnP) devices.

#### Step-by-Step Discovery Process

**1. Receiver Initialization**

```
[Receiver] → Joins multicast group 239.255.255.250:1900
           → Listens for M-SEARCH requests
           → Ready to respond to discovery queries
```

When a receiver starts, it performs the following network operations:

- Creates a UDP socket bound to port 1900
- Joins the multicast group 239.255.255.250 using IP_ADD_MEMBERSHIP
- Listens for incoming SSDP messages
- Prepares device description responses

**2. Sender Discovery Broadcast**

```
[Sender] → Broadcasts M-SEARCH * HTTP/1.1
         → To multicast address 239.255.255.250:1900
         → With MAN: "ssdp:discover" header
         → Every 5 seconds
```

The sender constructs an SSDP search request:

```
M-SEARCH * HTTP/1.1
HOST: 239.255.255.250:1900
MAN: "ssdp:discover"
MX: 3
ST: rgm:screenreceiver
USER-AGENT: RGM/2.0
```

**3. Receiver Response**

```
[Receiver] → Sends HTTP/1.1 200 OK
           → Includes device UUID
           → Provides IP address and port
           → Describes capabilities
```

Each receiver responds with:

```
HTTP/1.1 200 OK
CACHE-CONTROL: max-age=1800
LOCATION: http://192.168.1.102:8081/description.xml
SERVER: RGM/2.0
ST: rgm:screenreceiver
USN: uuid:RGM-12345678-1234-1234-1234-123456789012
```

**4. Connection Establishment**

```
[Sender]   → Selects receiver from list
           → Initiates TCP handshake on port 8081
           → Negotiates streaming parameters
[Receiver] → Accepts connection
           → Prepares display buffer
           → Acknowledges ready state
```

### Streaming Architecture

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│                 │     │                  │     │                 │     │                 │
│  Screen Capture │────▶│   4MB Socket     │────▶│  TCP Stream     │────▶│  SDL2 Renderer  │
│                 │     │    Buffer        │     │  (Port 8081)    │     │                 │
│                 │     │                  │     │                 │     │                 │
└────────┬────────┘     └────────┬─────────┘     └────────┬────────┘     └────────┬────────┘
         │                       │                        │                       │
         ▼                       ▼                        ▼                       ▼
   ┌─────────────┐         ┌─────────────┐          ┌─────────────┐         ┌─────────────┐
   │   X11       │         │  Overflow   │          │  Nagle's    │         │  Hardware   │
   │   Win32     │         │  Protection │          │  Disabled   │         │  Acceleration│
   │   CoreGr.   │         │             │          │  TCP_NODELAY│         │             │
   └─────────────┘         └─────────────┘          └─────────────┘         └─────────────┘
```

### Data Flow Pipeline

#### 1. Screen Capture Layer

**Linux (X11)**

```cpp
// Pseudocode for X11 capture
Display* display = XOpenDisplay(NULL);
Window root = DefaultRootWindow(display);
XImage* image = XGetImage(display, root, 0, 0, 
                          width, height, AllPlanes, ZPixmap);
// Extract raw RGB data from image->data
```

**Windows (GDI/DirectX)**

```cpp
// Pseudocode for Windows capture
HDC hdcScreen = GetDC(NULL);
HDC hdcMem = CreateCompatibleDC(hdcScreen);
HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
SelectObject(hdcMem, hBitmap);
BitBlt(hdcMem, 0, 0, width, height, hdcScreen, 0, 0, SRCCOPY);
// Extract bitmap bits
```

**macOS (CoreGraphics)**

```cpp
// Pseudocode for macOS capture
CGImageRef image = CGDisplayCreateImage(displayID);
CFDataRef data = CGDataProviderCopyData(CGImageGetDataProvider(image));
// Extract raw pixel data from CFDataRef
```

#### 2. Buffer Management

The application uses 4MB socket buffers to ensure smooth streaming:

```cpp
// Socket buffer configuration
int send_buffer_size = 4 * 1024 * 1024;  // 4MB
setsockopt(socket, SOL_SOCKET, SO_SNDBUF, 
           &send_buffer_size, sizeof(send_buffer_size));

int recv_buffer_size = 4 * 1024 * 1024;  // 4MB
setsockopt(socket, SOL_SOCKET, SO_RCVBUF,
           &recv_buffer_size, sizeof(recv_buffer_size));
```

#### 3. TCP Optimization

Nagle's algorithm is disabled to reduce latency:

```cpp
int flag = 1;
setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, 
           &flag, sizeof(flag));
```

This ensures that small screen updates are sent immediately rather than being delayed to coalesce with other data.

#### 4. Rendering Pipeline

The receiver uses SDL2 for hardware-accelerated display:

```cpp
// SDL2 initialization
SDL_Init(SDL_INIT_VIDEO);
SDL_Window* window = SDL_CreateWindow("RGM Receiver", 
                                      SDL_WINDOWPOS_UNDEFINED,
                                      SDL_WINDOWPOS_UNDEFINED,
                                      width, height,
                                      SDL_WINDOW_RESIZABLE);
SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, 
                                            SDL_RENDERER_ACCELERATED);

// Texture for video frames
SDL_Texture* texture = SDL_CreateTexture(renderer,
                                         SDL_PIXELFORMAT_RGB24,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         width, height);
```

### Protocol Details

#### SSDP Message Format

**Search Request (Sender)**

```
M-SEARCH * HTTP/1.1
HOST: 239.255.255.250:1900
MAN: "ssdp:discover"
MX: 3
ST: rgm:screenreceiver
```

**Search Response (Receiver)**

```
HTTP/1.1 200 OK
CACHE-CONTROL: max-age=1800
DATE: [timestamp]
LOCATION: http://[receiver-ip]:8081/rgm.xml
SERVER: RGM/2.0 [OS/version]
ST: rgm:screenreceiver
USN: uuid:[device-uuid]::urn:rgm:screenreceiver
```

#### Streaming Protocol

Once TCP connection is established, data transmission follows this format:

| Field | Size | Description |
|-------|------|-------------|
| Magic Number | 4 bytes | 0x52474D ('RGMD') |
| Frame Number | 4 bytes | Incrementing counter |
| Timestamp | 8 bytes | Microseconds since epoch |
| Width | 4 bytes | Frame width in pixels |
| Height | 4 bytes | Frame height in pixels |
| Data Size | 4 bytes | Size of pixel data |
| Pixel Data | Variable | Raw RGB24 data |

### Message Sequence Chart

```
Sender                          Network                         Receiver
  |                               |                               |
  | ----- M-SEARCH (multicast) -->|                               |
  |                               |                               |
  |                               | <---- NOTIFY (if running) ----|
  |                               |                               |
  | <---- 200 OK (from receiver) -|                               |
  |                               |                               |
  | ------- TCP SYN ------------> |                               |
  | <------ TCP SYN-ACK --------- |                               |
  | ------- TCP ACK ------------> |                               |
  |                               |                               |
  | -------- Stream Setup ------->|                               |
  | <------ Ready Acknowledge ----|                               |
  |                               |                               |
  | ======== Video Frames =======>|                               |
  | ======== Video Frames =======>|                               |
  | ======== Video Frames =======>|                               |
  |                               |                               |
  | ------- TCP FIN ------------> |                               |
  | <------ TCP ACK --------------|                               |
  | <------ TCP FIN --------------|                               |
  | ------- TCP ACK ------------> |                               |
  |                               |                               |
```

### Performance Optimizations

1. **Zero-Copy Architecture**
   - Screen capture writes directly to buffer
   - No intermediate copying of frame data

2. **Buffer Overflow Protection**
   - 4MB buffers absorb network jitter
   - Drop frames when buffer exceeds threshold

3. **TCP_NODELAY**
   - Disables Nagle's algorithm
   - Reduces latency for small screen updates

4. **Multi-threading**
   - Capture thread: Reads screen continuously
   - Network thread: Transmits data asynchronously
   - Render thread: Displays frames independently

---

## System Requirements

### Hardware Specifications

| Component | Minimum | Recommended |
|-----------|---------|--------------|
| Processor | 1 GHz single-core | 2 GHz dual-core |
| Memory | 256 MB | 512 MB |
| Network | 100 Mbps | 1 Gbps |
| Display | 800x600 | 1920x1080 |

### Software Requirements by Platform

#### Linux

| Requirement | Specification |
|-------------|---------------|
| Distribution | Ubuntu 18.04+, Debian 10+, Fedora 32+, Arch Linux |
| Compiler | GCC 4.8+ with C++11 support |
| Libraries | libX11-dev, libsdl2-dev, pthread |
| Build Tool | make |

#### Windows

| Requirement | Specification |
|-------------|---------------|
| Version | Windows 10 (build 1903+) or Windows 11 |
| Compiler | MinGW-w64 7.0+ or MSVC 2019+ |
| SDK | Windows SDK 10.0+ |
| Libraries | SDL2 development libraries |

#### macOS

| Requirement | Specification |
|-------------|---------------|
| Version | macOS Catalina (10.15) or newer |
| Compiler | Clang 12.0+ (Xcode 12+) |
| Libraries | SDL2 (via Homebrew) |
| Optional | XQuartz for X11 fallback |

---

## Installation

### Linux (Ubuntu/Debian)

```bash
# Clone the repository
git clone https://github.com/RR-Ralefaso/RGM.git
cd RGM

# Install dependencies
sudo apt update
sudo apt install g++ make libx11-dev libsdl2-dev

# Build the application
make clean
make

# Run the launcher
./app
```

### Linux (Fedora/RHEL/CentOS)

```bash
sudo dnf install gcc-c++ make libX11-devel SDL2-devel
git clone https://github.com/RR-Ralefaso/RGM.git
cd RGM
make
./app
```

### Linux (Arch)

```bash
sudo pacman -S gcc make libx11 sdl2
git clone https://github.com/RR-Ralefaso/RGM.git
cd RGM
make
./app
```

### Windows

```bash
# Using MinGW-w64
git clone https://github.com/RR-Ralefaso/RGM.git
cd RGM

# Build (ensure SDL2 is properly configured)
mingw32-make

# Run
./app.exe
```

### macOS

```bash
# Install Homebrew if not present
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install sdl2 make

# Build
git clone https://github.com/RR-Ralefaso/RGM.git
cd RGM
make
./app
```

---

## Building from Source

### Build Commands

| Command | Description |
|---------|-------------|
| `make` | Build all components (default) |
| `make clean` | Remove object files and binaries |
| `make app` | Build launcher only |
| `make sender` | Build sender only |
| `make receiver` | Build receiver only |
| `make debug` | Build with debug symbols |
| `make check` | Verify build environment |

### Build Output

After successful compilation, the following executables are created in the root directory:

- `app` - Launcher application
- `sender` - Screen streaming utility
- `receiver` - Display utility

Object files are stored in the `build/` directory.

---

## Usage Guide

### Basic Operation

#### On the Receiving Machine

```bash
./receiver
```

Expected output:

```
[RGM Receiver v2.0]
Listening for senders on port 8081
SSDP discovery enabled
Ready to receive connections
```

#### On the Sending Machine

```bash
./sender
```

Interactive session:

```
Scanning network for receivers...
Found 2 receiver(s):
[1] Office-PC (192.168.1.102)
[2] Living-Room (192.168.1.105)
Select receiver (1-2): 1
Connecting to 192.168.1.102:8081...
Connection established. Streaming at 1920x1080 @ 60 FPS
```

### Using the Launcher

```bash
./app
```

The launcher presents a menu interface:

```
╔═══════════════════════════════════════╗
║             RGM v2.0                  ║
╠═══════════════════════════════════════╣
║                                       ║
║  1. SEND SCREEN                       ║
║  2. RECEIVE SCREEN                    ║
║  3. EXIT                              ║
║                                       ║
╚═══════════════════════════════════════╝
```

### Application Controls

#### Receiver Controls

| Key | Action |
|-----|--------|
| ESC or Q | Quit application |
| Window resize | Scale video (maintains aspect ratio) |
| Close window | Stop receiving |

#### Sender Controls

| Key | Action |
|-----|--------|
| Ctrl+C | Graceful shutdown |
| Number keys | Select receiver from list |

---

## Network Configuration

### Firewall Rules

#### Linux (iptables)

```bash
# Allow SSDP discovery
sudo iptables -A INPUT -p udp --dport 1900 -j ACCEPT
sudo iptables -A OUTPUT -p udp --sport 1900 -j ACCEPT

# Allow video streaming
sudo iptables -A INPUT -p tcp --dport 8081 -j ACCEPT
sudo iptables -A OUTPUT -p tcp --sport 8081 -j ACCEPT
```

#### Linux (UFW)

```bash
sudo ufw allow 1900/udp comment 'RGM SSDP Discovery'
sudo ufw allow 8081/tcp comment 'RGM Video Stream'
sudo ufw reload
```

#### Windows (PowerShell - Administrator)

```powershell
New-NetFirewallRule -DisplayName "RGM Discovery (UDP)" `
  -Direction Inbound -Protocol UDP -LocalPort 1900 -Action Allow

New-NetFirewallRule -DisplayName "RGM Stream (TCP)" `
  -Direction Inbound -Protocol TCP -LocalPort 8081 -Action Allow
```

#### macOS

```bash
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add /path/to/receiver
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add /path/to/sender
```

### Network Requirements

- All devices must be on the same subnet
- Multicast must be enabled on network switches
- Ethernet connection recommended for 60 FPS streaming
- WiFi 5GHz band recommended over 2.4GHz

---

## Performance Characteristics

### Bandwidth Requirements

| Resolution | 30 FPS | 60 FPS |
|------------|--------|--------|
| 1280x720 (720p) | ~125 MB/s | ~250 MB/s |
| 1920x1080 (1080p) | ~280 MB/s | ~560 MB/s |
| 2560x1440 (1440p) | ~500 MB/s | ~1 GB/s |
| 3840x2160 (4K) | ~1.1 GB/s | ~2.2 GB/s |

*Note: Actual bandwidth depends on screen content complexity. Static content requires less bandwidth than rapidly changing video.*

### Latency Characteristics

| Network Type | Average Latency |
|--------------|-----------------|
| Wired Ethernet (1 Gbps) | < 5 ms |
| WiFi 5 GHz (ac) | 10-15 ms |
| WiFi 2.4 GHz (n) | 20-30 ms |

---

## Troubleshooting

| Issue | Diagnostic | Solution |
|-------|------------|----------|
| No receivers found | Check network connectivity | Verify firewall allows UDP 1900 |
| Connection refused | Verify receiver is running | Check if port 8081 is open |
| Low FPS | Monitor network utilization | Use wired Ethernet connection |
| Build failures | Run `make check` | Install missing dependencies |

### Diagnostic Commands

```bash
# Check if receiver is listening
netstat -tulpn | grep 8081

# Test connectivity
telnet <receiver-ip> 8081

# Verify build environment
make check
```

---

## Roadmap

### Planned Enhancements

- [ ] Video compression (H.264/H.265) for bandwidth reduction
- [ ] Audio capture and streaming
- [ ] Encryption (TLS/SSL) for secure transmission
- [ ] Multi-monitor selection
- [ ] Region selection for partial screen sharing
- [ ] Adaptive FPS based on network conditions
- [ ] Session pause/resume functionality
- [ ] Password authentication
- [ ] Stream recording capability
- [ ] Wayland display server support
- [ ] GPU-accelerated capture

---

## Support the Project

<div align="center">

### Show Your Support

| Action | Impact |
|--------|--------|
| [Star on GitHub](https://github.com/RR-Ralefaso/RGM) | Increases project visibility |
| [Become a Sponsor](https://github.com/sponsors/RR-Ralefaso) | Funds ongoing development |
| [Report Issues](https://github.com/RR-Ralefaso/RGM/issues) | Helps improve stability |
| [Contribute Code](https://github.com/RR-Ralefaso/RGM/pulls) | Accelerates feature development |

Your support enables:

- Regular maintenance and bug fixes
- New feature development
- Cross-platform improvements
- Documentation updates
- Performance optimizations

</div>

---

## License

Copyright © 2024 RR-Ralefaso (polaris)

This project is open source software. All rights reserved.

---

<div align="center">

```
╔════════════════════════════════════════════════════════════════╗
║                                                                ║
║   "Seeking to solve complex business problems through          ║
║    analytical precision and elegant code - on any platform."   ║
║                                                                ║
║                    - RR-Ralefaso (polaris)                     ║
║                                                                ║
╚════════════════════════════════════════════════════════════════╝
```

**Linux • Windows 10/11 • macOS - One codebase, all platforms.**

[Star on GitHub](https://github.com/RR-Ralefaso/RGM) • [Sponsor Development](https://github.com/sponsors/RR-Ralefaso)

</div>
