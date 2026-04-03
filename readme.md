# RGM (Ralefaso GlassMirror)

<div align="center">

**A zero-configuration screen extender with automatic network discovery**

[Star this Project](https://github.com/RR-Ralefaso/RGM) • [Become a Sponsor](https://github.com/sponsors/RR-Ralefaso) • [Report Issue](https://github.com/RR-Ralefaso/RGM/issues)

Your support helps maintain and improve RGM for everyone.

</div>

---

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Architecture](#architecture)
- [How It Works](#how-it-works)
- [Display Modes](#display-modes)
- [System Requirements](#system-requirements)
- [Installation](#installation)
- [Building from Source](#building-from-source)
- [Usage Guide](#usage-guide)
- [Network Configuration](#network-configuration)
- [Performance Characteristics](#performance-characteristics)
- [Troubleshooting](#troubleshooting)
- [Roadmap](#roadmap)
- [Support the Project](#support-the-project)

---

## Overview

RGM (Ralefaso GlassMirror) is a lightweight, cross-platform **screen extender** that turns any networked machine into a wireless second monitor. It implements SSDP (Simple Service Discovery Protocol) to automatically detect receiver hosts on your local network, requiring zero manual IP configuration.

The receiver machine opens a **borderless fullscreen window** that presents itself as a natural display extension — positioned logically to the right of, or below, the sender's screen. The result looks and behaves like a real second monitor plugged into the sender machine.

A mirror mode (classic screen duplication) is also available for presentations and demos.

---

## Features

| Category | Capability |
|----------|------------|
| **Screen Extension** | Extend Right, Extend Below, or Mirror modes — chosen per session |
| **Discovery** | Zero-configuration SSDP automatic detection, no IP setup needed |
| **Performance** | 60 FPS streaming, 4 MB socket buffers, TCP_NODELAY optimisation |
| **CPU Offload** | RLE frame compression and colour correction offloaded to the receiver's CPU (TCP 8082) |
| **Port Inspector** | Full remote port inspection: list, query, and kill processes on any receiver port (TCP 8083) |
| **Platform Support** | Linux (X11), Windows 10/11, macOS 10.15+ |
| **Display Handling** | Auto-resolution handshake, borderless fullscreen, aspect-ratio scaling |
| **Splash Screen** | rcorp.jpeg corporate logo displayed at launch via SDL2_image |
| **Monitoring** | Real-time FPS, bandwidth, source/destination resolution, real measured offload timing |
| **User Interface** | Splash screen, menu-driven launcher, direct executable mode |

---

## Architecture

### Component Structure

```
RGM/
├── makefile                     # Cross-platform build (Linux/macOS/Windows)
├── src/                         # Source code
│   ├── app.cpp                  # Launcher: splash + menu
│   ├── sender.cpp               # Screen capture, extender handshake, stream, port inspector client
│   ├── receiver.cpp             # Fullscreen display, SSDP advertiser, compute svc, port svc
│   ├── discover.cpp             # SSDP discovery engine
│   ├── discover.h               # Discovery API
│   ├── gpu_accelerate.c         # Remote CPU offload (RLE compress / colorfix) with real timing
│   ├── gpu_accelerate.h         # Compute offload API
│   ├── ports.cpp                # Remote port inspection service (server + client)
│   └── ports.h                  # Port inspector API
├── assets/
│   └── icons/
│       ├── rcorp.jpeg           # Corporate splash logo  ← shown at startup
│       └── RGM.png              # Fallback splash logo
├── build/                       # Compiled object files
├── sender                       # Sender executable
├── receiver                     # Receiver executable
├── app                          # Launcher executable
└── readme.md
```

### Executables

| Executable | Role |
|------------|------|
| `app` | Menu launcher — choose send / receive mode |
| `sender` | Captures local display, negotiates mode, streams frames, runs port inspector client |
| `receiver` | Advertises via SSDP, opens fullscreen window, renders frames, runs compute and port services |

### Network Ports

| Port | Protocol | Purpose |
|------|----------|---------|
| 1900 | UDP multicast | SSDP discovery (M-SEARCH / NOTIFY) |
| 8081 | TCP | Video frame stream |
| 8082 | TCP | CPU compute offload service (RLE compress / colour fix) |
| 8083 | TCP | Remote port inspection service |

---

## How It Works

### 1 — Discovery (SSDP)

RGM uses the same multicast discovery protocol as UPnP/DLNA — no manual IP entry required.

```
Receiver  →  joins 239.255.255.250:1900
           →  sends periodic NOTIFY announcements
           →  listens for M-SEARCH queries

Sender    →  broadcasts M-SEARCH to 239.255.255.250:1900
           →  collects 200 OK responses
           →  lists discovered receivers
           →  user picks one
```

### 2 — Extended Handshake

Once the user selects a receiver and a display mode, the sender opens a TCP connection to port 8081 and exchanges an **extended handshake**:

```
Sender → Receiver   (16 bytes, network byte order)
  uint32  sender_width
  uint32  sender_height
  uint32  fps
  uint32  mode          0=mirror  1=extend-right  2=extend-below

Receiver → Sender   (12 bytes, network byte order)
  uint32  receiver_width
  uint32  receiver_height
  uint32  status        0=OK
```

The sender uses the receiver's reported resolution to display the combined virtual desktop layout in the terminal.

### 3 — Display Window Strategy

| Mode | Window behaviour |
|------|-----------------|
| Extend Right / Below | `SDL_WINDOW_FULLSCREEN_DESKTOP` — borderless, covers the entire receiver display, appears as a physical second monitor |
| Mirror | Normal resizable window, scaled to fit |

In extend modes the receiver also draws a subtle 2-pixel blue edge glow on the side that logically joins to the sender's screen (left edge for extend-right, top edge for extend-below).

### 4 — Frame Stream

Every frame:

```
Sender  →  captures screen (X11 / GDI / CoreGraphics)
        →  optionally RLE-compresses via compute offload service
        →  sends  [uint32 frame_size] [frame_bytes]

Receiver →  reads size header
         →  reads frame_bytes
         →  RLE-decompresses if frame_size < raw_size
         →  SDL_UpdateTexture → SDL_RenderCopy → SDL_RenderPresent
```

### 5 — CPU Compute Offload Service (port 8082)

The receiver runs a second TCP server on port 8082 (`gpu_accelerate.c`). The sender connects to it optionally before streaming begins. All work is performed on the **receiver's CPU** — the sender sends raw pixel data, the receiver processes it and sends back the result, so processing load is shifted off the sender machine.

Timing is measured with `clock_gettime(CLOCK_MONOTONIC)` on Linux/macOS and `QueryPerformanceCounter` on Windows, so the `ms_elapsed` values reported are real measured durations, not estimates.

| Operation | Code | Description |
|-----------|------|-------------|
| PING | 0xFF | Heartbeat / handshake check |
| COMPRESS | 0x01 | RLE-compress a raw RGB frame (receiver CPU) |
| COLORCONV | 0x03 | BGR → RGB channel swap (receiver CPU) |

If the compute service is unavailable the sender falls back silently to uncompressed local frames.

### 6 — Port Inspection Service (port 8083)

The receiver runs a third TCP server on port 8083 (`ports.cpp`). Once connected, the sender can interactively inspect every listening socket on the receiver machine:

| Operation | Description |
|-----------|-------------|
| LIST_TCP | All TCP sockets with PID, process name, state, addresses |
| LIST_UDP | All UDP sockets |
| LIST_ALL | TCP + UDP combined |
| GET_PORT | Details for one specific port number |
| KILL_PORT | Send SIGTERM to the process owning a port |

Port data is collected natively per platform:

| Platform | Method |
|----------|--------|
| Linux | `/proc/net/tcp`, `tcp6`, `udp`, `udp6` + inode→PID mapping via `/proc/PID/fd` |
| macOS | `lsof -nP -iTCP -iUDP` |
| Windows | `GetExtendedTcpTable` / `GetExtendedUdpTable` (iphlpapi) + `CreateToolhelp32Snapshot` |

### Streaming Architecture

```
┌─────────────┐   handshake   ┌──────────────────────────────────────────┐
│   SENDER    │  ──────────►  │              RECEIVER                    │
│             │               │                                          │
│ capture     │  frame data   │  RLE decode → SDL2 texture               │
│ (X11/GDI/  │  ──────────►  │  → fullscreen borderless window          │
│  CG)        │  TCP 8081     │    (extend-right / below / mirror)       │
│             │               │                                          │
│ RLE via     │  compute proto│  gpu_service_run() on TCP 8082           │
│ CPU offload │  ──────────►  │  (RLE compress / color fix, real timing) │
│             │  TCP 8082     │                                          │
│ port cmds   │  port proto   │  ports_service_run() on TCP 8083         │
│ interactive │  ──────────►  │  (list/query/kill receiver ports)        │
└─────────────┘  TCP 8083     └──────────────────────────────────────────┘
```

---

## Display Modes

### Extend Right *(default)*

The receiver's display appears logically to the **right** of the sender's screen. The receiver opens a borderless fullscreen window with a blue left-edge indicator showing where the screens join.

```
┌────────────────┬────────────────┐
│                │                │
│  SENDER        │  RECEIVER      │
│  (your machine)│  (extended)    │
│                │◄ blue edge     │
└────────────────┴────────────────┘
```

### Extend Below

The receiver's display appears **below** the sender's screen, with a blue top-edge indicator.

```
┌────────────────────────────────┐
│         SENDER                 │
└────────────────────────────────┘
  ▲ blue edge
┌────────────────────────────────┐
│         RECEIVER               │
└────────────────────────────────┘
```

### Mirror

The receiver displays an exact duplicate of the sender's screen in a normal resizable window. Use this for presentations.

---

## System Requirements

### Hardware

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| CPU | 1 GHz | 2 GHz dual-core |
| RAM | 256 MB | 512 MB |
| Network | 100 Mbps | 1 Gbps wired |
| Display | 800×600 | 1920×1080 |

### Software by Platform

#### Linux

| Requirement | Detail |
|-------------|--------|
| Distribution | Ubuntu 18.04+, Debian 10+, Fedora 32+, Arch |
| Compiler | GCC 8+ (C++17) |
| Libraries | libX11-dev, libsdl2-dev, **libsdl2-image-dev**, pthread |
| Build tool | make |

#### Windows

| Requirement | Detail |
|-------------|--------|
| Version | Windows 10 build 1903+ or Windows 11 |
| Compiler | MinGW-w64 (MSYS2) or MSVC 2019+ |
| Libraries | SDL2, **SDL2_image** (from MSYS2 packages) |
| SDK | Windows SDK 10.0+ (iphlpapi required for port inspector) |

#### macOS

| Requirement | Detail |
|-------------|--------|
| Version | macOS Catalina 10.15+ |
| Compiler | Clang 12+ (Xcode 12+) |
| Libraries | `brew install sdl2 sdl2_image` |
| Screen capture | CoreGraphics (built-in) |

> **Note:** `SDL2_image` is required on all platforms for the rcorp.jpeg splash screen.

---

## Installation

### Linux (Ubuntu / Debian)

```bash
git clone https://github.com/RR-Ralefaso/RGM.git
cd RGM
sudo apt update
sudo apt install -y g++ make libx11-dev libsdl2-dev libsdl2-image-dev
make install-deps
make
./app
```

### Linux (Fedora / RHEL)

```bash
sudo dnf install gcc-c++ make libX11-devel SDL2-devel SDL2_image-devel
git clone https://github.com/RR-Ralefaso/RGM.git
cd RGM && make && ./app
```

### Linux (Arch)

```bash
sudo pacman -S gcc make libx11 sdl2 sdl2_image
git clone https://github.com/RR-Ralefaso/RGM.git
cd RGM && make install-deps && make && ./app
```

### macOS

```bash
# Install Homebrew if needed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

brew install sdl2 sdl2_image
git clone https://github.com/RR-Ralefaso/RGM.git
cd RGM && make install-deps && make && ./app
```

### Windows (MSYS2 / MinGW)

```bash
# In MSYS2 MinGW64 shell
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image

git clone https://github.com/RR-Ralefaso/RGM.git
cd RGM
make install-deps
make
./app.exe
```

---

## Building from Source

### Build Commands

| Command | Description |
|---------|-------------|
| `make` | Build all components |
| `make sender` | Build sender only |
| `make receiver` | Build receiver only |
| `make app` | Build launcher only |
| `make debug` | Build with `-g -O0 -DDEBUG` |
| `make clean` | Remove all build output |
| `make check` | Verify environment, sources, and assets |
| `make install-deps` | Install system dependencies |

### Build Output

```
RGM/
├── sender      (or sender.exe)
├── receiver    (or receiver.exe)
├── app         (or app.exe)
└── build/
    ├── sender.o
    ├── receiver.o
    ├── app.o
    ├── discover.o
    ├── gpu_accelerate.o
    └── ports.o
```

---

## Usage Guide

### Quick Start

**On the receiver machine** (the machine that will act as the second monitor):

```bash
./receiver
```

Expected output:
```
========================================
  RGM RECEIVER v2.0.1
========================================
  Local IP   : 192.168.1.105
  My display : 1920x1080
  Stream TCP : 8081
  Compute TCP: 8082
  Ports TCP  : 8083
  SSDP UDP   : 239.255.255.250:1900
  GPU Stats  : gpu_stats.json
  Modes      : extend-right | extend-below | mirror
========================================
Waiting for sender on TCP 8081 ...
```

**On the sender machine** (the machine whose screen you want to extend):

```bash
./sender
```

Interactive session:
```
Discovering receivers...
  Found: 192.168.1.105:8081 – testing...
  Connection OK
Discovery complete: 1 receiver(s) found

RECEIVERS FOUND:
  [0] 192.168.1.105:8081

  Select display mode:
  1  Extend Right  (receiver = right monitor)
  2  Extend Below  (receiver = bottom monitor)
  3  Mirror        (duplicate screen)
  Choice [1]:

Mode: Extend Right

Remote compute (CPU offload) active
Port inspector active  (press 'p' during stream)

Extended desktop active:
  Sender:   1920x1080
  Receiver: 1920x1080
  Layout:   Extend Right
  Total:    3840x1080

Streaming – Ctrl+C to stop
```

The receiver's display immediately goes fullscreen and begins showing the sender's screen content.

### Using the Launcher

```bash
./app
```

The launcher shows the rcorp.jpeg splash screen then presents:

```
╔════════════════════════════════╗
║      RGM v2.0.2                ║
╠════════════════════════════════╣
║                                ║
║  1.  SEND SCREEN               ║
║  2.  RECEIVE SCREEN            ║
║  0.  EXIT                      ║
║                                ║
╚════════════════════════════════╝
```

### Receiver Controls

| Key | Action |
|-----|--------|
| ESC or Q | Disconnect and exit |
| F11 | Toggle fullscreen (extend modes) |
| Close window | Stop receiving |

### Sender Controls

| Key / Input | Action |
|-------------|--------|
| Ctrl+C | Graceful shutdown |
| Number at prompt | Select receiver from list |
| 1 / 2 / 3 at mode prompt | Choose Extend Right / Extend Below / Mirror |
| `p` at stream prompt | Open Port Inspector menu |

### Port Inspector Menu

When the sender connects to a receiver, it also connects to the port inspection service on TCP 8083. Press `p` (or enter it at the prompt) to open the interactive menu:

```
╔════════════════════════════════╗
║   RECEIVER PORT INSPECTOR      ║
╠════════════════════════════════╣
║  1. List all TCP ports         ║
║  2. List all UDP ports         ║
║  3. List ALL ports             ║
║  4. Query specific port        ║
║  5. Kill process on port       ║
║  0. Back to stream             ║
╚════════════════════════════════╝
```

The table output shows protocol, local address:port, remote address:port, TCP state, PID, and process name — all read live from the receiver OS. The kill option sends `SIGTERM` (Linux/macOS) or `TerminateProcess` (Windows) to the process owning the specified port.

---

## Network Configuration

### Firewall Rules

#### Linux (UFW)

```bash
sudo ufw allow 1900/udp comment 'RGM SSDP'
sudo ufw allow 8081/tcp comment 'RGM Video Stream'
sudo ufw allow 8082/tcp comment 'RGM Compute Offload'
sudo ufw allow 8083/tcp comment 'RGM Port Inspector'
sudo ufw reload
```

#### Linux (iptables)

```bash
sudo iptables -A INPUT -p udp --dport 1900 -j ACCEPT
sudo iptables -A INPUT -p tcp --dport 8081 -j ACCEPT
sudo iptables -A INPUT -p tcp --dport 8082 -j ACCEPT
sudo iptables -A INPUT -p tcp --dport 8083 -j ACCEPT
```

#### Windows (PowerShell — Administrator)

```powershell
New-NetFirewallRule -DisplayName "RGM SSDP"     -Direction Inbound -Protocol UDP -LocalPort 1900 -Action Allow
New-NetFirewallRule -DisplayName "RGM Stream"   -Direction Inbound -Protocol TCP -LocalPort 8081 -Action Allow
New-NetFirewallRule -DisplayName "RGM Compute"  -Direction Inbound -Protocol TCP -LocalPort 8082 -Action Allow
New-NetFirewallRule -DisplayName "RGM Ports"    -Direction Inbound -Protocol TCP -LocalPort 8083 -Action Allow
```

#### macOS

```bash
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add /path/to/receiver
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add /path/to/sender
```

### Network Requirements

- All devices must be on the same subnet
- Multicast must be enabled on network switches (IGMP snooping)
- Wired Ethernet recommended for 60 FPS at 1080p
- WiFi 5 GHz (802.11ac) workable for lower resolutions

---

## Performance Characteristics

### Bandwidth (uncompressed RGB24)

| Resolution | 30 FPS | 60 FPS |
|------------|--------|--------|
| 1280×720   | ~125 MB/s | ~250 MB/s |
| 1920×1080  | ~280 MB/s | ~560 MB/s |
| 2560×1440  | ~500 MB/s | ~1 GB/s |

> CPU offload RLE compression typically reduces bandwidth by 30–70% for desktop content (text, UI). Video content compresses less. The compression ratio and time are reported accurately based on real measurements.

### Latency

| Connection | Typical Latency |
|------------|----------------|
| Wired 1 Gbps | < 5 ms |
| WiFi 5 GHz  | 10–15 ms |
| WiFi 2.4 GHz | 20–35 ms |

### Compute Offload Stats

The receiver writes live statistics to `gpu_stats.json` every 60 seconds and prints a summary to the console every 30 seconds. Stats include total operations, bytes processed in/out, real average compression ratio, actual milliseconds per operation, and per-client connection counts.

---

## Troubleshooting

| Problem | Check | Solution |
|---------|-------|----------|
| No receivers found | Network connectivity | Verify firewall allows UDP 1900 on receiver |
| Connection refused | Receiver running? | Check port 8081 is open; restart receiver |
| Black screen on receiver | Handshake exchange | Ensure both binaries are the same version |
| Low FPS | Network utilisation | Use wired Ethernet; enable CPU offload |
| Compute offload unavailable | Port 8082 blocked | Allow TCP 8082 in firewall on receiver |
| Port inspector unavailable | Port 8083 blocked | Allow TCP 8083 in firewall on receiver |
| rcorp.jpeg not showing | Asset path | Place `rcorp.jpeg` in `assets/icons/`; run `make check` |
| SDL_image not found | Missing library | Run `make install-deps` or install `libsdl2-image-dev` |
| Build fails on Windows | Missing iphlpapi | Ensure Windows SDK is installed; iphlpapi is linked automatically |
| Build fails on macOS | Homebrew paths | Run `brew install sdl2 sdl2_image`; check `BREW_PREFIX` in makefile |

### Diagnostic Commands

```bash
# Verify receiver is listening on all four ports
netstat -tulpn | grep -E '8081|8082|8083|1900'

# Test TCP reachability
nc -zv <receiver-ip> 8081
nc -zv <receiver-ip> 8082
nc -zv <receiver-ip> 8083

#ensure all dependancies are installed 
make install-deps

# Check assets and source files
make check

# Full rebuild
make clean && make
```

---

## Roadmap

- [x] Screen mirroring (original)
- [x] Screen extender — Extend Right
- [x] Screen extender — Extend Below
- [x] Remote CPU offload (RLE compression + colour correction)
- [x] Real measured offload timing (clock_gettime / QueryPerformanceCounter)
- [x] Extended handshake (resolution exchange)
- [x] rcorp.jpeg splash via SDL2_image
- [x] macOS CoreGraphics capture
- [x] Remote port inspection service (TCP 8083)
- [x] Per-client GPU stats tracking + JSON export
- [ ] app hosting onto receiver making receiver run the heavy tasks on it rather than everything running on sender
- [ ] H.264/H.265 compression for bandwidth reduction
- [ ] Audio capture and streaming
- [ ] TLS encryption
- [ ] Multi-monitor source selection
- [ ] Partial screen region selection
- [ ] Adaptive FPS based on network conditions
- [ ] Wayland display server support
- [ ] Mouse pointer handoff across display boundary
- [ ] remote access ,full or partial , to all the storage of the Receiver

---

## Support the Project

<div align="center">

| Action | Impact |
|--------|--------|
| [Star on GitHub](https://github.com/RR-Ralefaso/RGM) | Increases project visibility |
| [Become a Sponsor](https://github.com/sponsors/RR-Ralefaso) | Funds ongoing development |
| [Report Issues](https://github.com/RR-Ralefaso/RGM/issues) | Helps improve stability |
| [Contribute Code](https://github.com/RR-Ralefaso/RGM/pulls) | Accelerates feature development |

</div>

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

**Linux • Windows 10/11 • macOS — One codebase, all platforms.**

[Star on GitHub](https://github.com/RR-Ralefaso/RGM) • [Sponsor Development](https://github.com/sponsors/RR-Ralefaso)

</div>
