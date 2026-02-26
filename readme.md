# RGM - COMPLETE CROSS-PLATFORM README

<div align="center">
  <img src="assets/icons/rcorp.jpeg" alt="RGM Logo" width="200"/>
  <h1>RGM</h1>
  <p><strong>A complete cross-platform remote collaboration suite</strong></p>
  <p>Screen sharing + USB device sharing + Remote file access</p>
  <p><em>Works on Linux, Windows 10/11, and macOS</em></p>
  <p>Created by <a href="https://github.com/RR-Ralefaso">RR-Ralefaso (polaris)</a></p>
</div>

---

## 📋 TABLE OF CONTENTS

- [RGM - COMPLETE CROSS-PLATFORM README](#rgm-screen-share---complete-cross-platform-readme)
  - [📋 TABLE OF CONTENTS](#-table-of-contents)
  - [✨ FEATURES](#-features)
  - [🖥️ SYSTEM REQUIREMENTS](#️-system-requirements)
    - [Linux](#linux)
    - [Windows 10/11](#windows-1011)
    - [macOS](#macos)
  - [📦 INSTALLATION](#-installation)
    - [Linux (Ubuntu/Debian)](#linux-ubuntudebian)
    - [Linux (Fedora/RHEL)](#linux-fedorarhel)
    - [Linux (Arch)](#linux-arch)
    - [Windows 10/11](#windows-1011-1)
    - [macOS](#macos-1)
    - [Using the Built-in Installer](#using-the-built-in-installer)
  - [🔧 BUILDING FROM SOURCE](#-building-from-source)
    - [Directory Structure](#directory-structure)
    - [Build Commands](#build-commands)
    - [Compilation Output](#compilation-output)
  - [🚀 USAGE GUIDE](#-usage-guide)
    - [Basic Screen Sharing](#basic-screen-sharing)
    - [Using the Launcher](#using-the-launcher)
    - [Cross-Platform Notes](#cross-platform-notes)
  - [🔌 USB SHARING](#-usb-sharing)
    - [How It Works](#how-it-works)
    - [Platform-Specific USB Support](#platform-specific-usb-support)
    - [Sender USB Menu](#sender-usb-menu)
    - [Receiver Permission Dialog](#receiver-permission-dialog)
  - [📁 REMOTE FILE ACCESS](#-remote-file-access)
    - [How It Works](#how-it-works-1)
    - [Platform-Specific File Access](#platform-specific-file-access)
    - [Sender File Browser](#sender-file-browser)
    - [Drive/Storage Listing](#drivestorage-listing)
    - [File Operations](#file-operations)
    - [Receiver Permission Dialog](#receiver-permission-dialog-1)
  - [🛡️ PERMISSION SYSTEM](#️-permission-system-1)
    - [Permission Levels](#permission-levels)
    - [Permission Parameters](#permission-parameters)
    - [Cross-Platform Permission Storage](#cross-platform-permission-storage)
  - [📊 COMMAND REFERENCE](#-command-reference-1)
    - [Makefile Commands](#makefile-commands-1)
    - [Application Controls](#application-controls)
    - [USB Menu Commands](#usb-menu-commands-1)
    - [File Browser Commands](#file-browser-commands-1)
  - [🔍 TROUBLESHOOTING](#-troubleshooting-1)
    - [Common Issues](#common-issues)
    - [Firewall Configuration](#firewall-configuration)
    - [Diagnostic Commands](#diagnostic-commands)
  - [🌐 NETWORK REQUIREMENTS](#-network-requirements-1)
    - [Ports Used](#ports-used)
    - [Bandwidth Requirements](#bandwidth-requirements)
    - [Network Topology](#network-topology)
  - [👤 AUTHOR \& CREDITS](#-author--credits-1)
    - [Author](#author)
    - [Core Contributions](#core-contributions)
    - [Version History](#version-history)
    - [Acknowledgments](#acknowledgments)


---

## ✨ FEATURES

### Core Features

- ✓ **Zero Configuration** - Automatic discovery via SSDP (no IP addresses needed)
- ✓ **Cross-Platform** - Works on Linux, Windows 10/11, and macOS
- ✓ **High Performance** - 60 FPS screen streaming with minimal latency
- ✓ **Auto Resolution Detection** - Automatically detects and adapts to screen size
- ✓ **Splash Screen** - Professional startup with RGM logo (JPEG/PNG support)
- ✓ **Statistics Display** - Real-time FPS and bandwidth monitoring
- ✓ **Multiple Modes** - Launcher app with menu, or direct sender/receiver execution
- ✓ **Resizable Window** - Receiver window scales while maintaining aspect ratio
- ✓ **TCP Optimization** - Disabled Nagle's algorithm for reduced latency
- ✓ **Large Buffers** - 4MB socket buffers for smooth high-FPS streaming

### 🔌 USB Sharing Features

- ✓ **Remote USB Access** - Access USB devices connected to receiver
- ✓ **Permission Control** - Receiver grants/denies access to each device
- ✓ **Time-limited Access** - Permissions can expire automatically
- ✓ **Device Detection** - Automatically detects new USB devices
- ✓ **Multiple Device Support** - Share multiple USB devices simultaneously
- ✓ **USB Types Supported** - Flash drives, webcams, printers, security keys, etc.
- ✓ **Platform Support**:
  - **Linux**: Full USB/IP kernel support
  - **Windows**: USB over IP via usbipd-win
  - **macOS**: USB sharing via virtualhere or custom implementation

### 📁 Remote File Access Features

- ✓ **Browse Remote Files** - Navigate receiver's file system
- ✓ **Download Files** - Copy files from receiver to sender
- ✓ **Upload Files** - Copy files from sender to receiver
- ✓ **Drive/Storage Listing** - See all mounted drives and their space
- ✓ **Directory Navigation** - Browse through folders with pagination
- ✓ **File Information** - View file sizes, dates, permissions
- ✓ **Visual Icons** - File type icons for easy identification
- ✓ **Hidden File Support** - Option to show/hide hidden files
- ✓ **Cross-Platform Path Handling** - Automatic conversion between Windows and Unix paths

### 🛡️ Security Features

- ✓ **Granular Permissions** - Control exactly what can be accessed
- ✓ **Path Sanitization** - Prevents directory traversal attacks
- ✓ **Access Logging** - All access requests are logged
- ✓ **Read-Only Mode** - Grant view-only access when needed
- ✓ **Time Expiry** - Permissions auto-expire after set time
- ✓ **IP Tracking** - Know who is requesting access
- ✓ **Platform-Aware Security** - Respects OS-level permissions

---

## 🖥️ SYSTEM REQUIREMENTS

### Linux

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| **CPU** | 1 GHz | 2+ GHz multi-core |
| **RAM** | 256 MB | 512 MB+ |
| **Network** | 100 Mbps | 1 Gbps |
| **Storage** | 50 MB free | 100 MB free |
| **Display** | Any resolution | 1080p+ |
| **Kernel** | 3.10+ | 5.0+ |

**Required Packages:**

```bash
# Ubuntu/Debian
g++ make libx11-dev libsdl2-dev libsdl2-image-dev usbip

# Fedora/RHEL
gcc-c++ make libX11-devel SDL2-devel SDL2_image-devel usbip

# Arch Linux
gcc make libx11 sdl2 sdl2_image usbip
```

**Kernel Modules (for USB sharing):**

```bash
usbip-core
usbip-host
vhci-hcd
```

### Windows 10/11

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| **CPU** | 1.5 GHz | 2.5+ GHz multi-core |
| **RAM** | 512 MB | 1 GB+ |
| **Network** | 100 Mbps | 1 Gbps |
| **Storage** | 100 MB free | 200 MB free |
| **Display** | Any resolution | 1080p+ |
| **OS Version** | Windows 10 (build 1809+) | Windows 11 |

**Required Software:**

- [Visual Studio Build Tools](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022) (or MinGW-w64)
- [CMake](https://cmake.org/download/) (3.10+)
- [SDL2](https://www.libsdl.org/download-2.0.php) development libraries
- [SDL2_image](https://www.libsdl.org/projects/SDL_image/) development libraries
- [usbipd-win](https://github.com/dorssel/usbipd-win) (for USB sharing)

**PowerShell (as Administrator) to install dependencies:**

```powershell
# Install Chocolatey package manager
Set-ExecutionPolicy Bypass -Scope Process -Force
[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

# Install dependencies
choco install visualstudio2022-buildtools cmake sdl2 sdl2-image

# Install USB/IP for Windows
choco install usbipd-win
```

### macOS

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| **CPU** | Intel Core 2 | Apple Silicon M1/M2+ |
| **RAM** | 512 MB | 1 GB+ |
| **Network** | 100 Mbps | 1 Gbps |
| **Storage** | 100 MB free | 200 MB free |
| **Display** | Any resolution | Retina display |
| **OS Version** | macOS 10.15 (Catalina) | macOS 14 (Sonoma)+ |

**Required Software:**

- Xcode Command Line Tools
- [Homebrew](https://brew.sh/)
- SDL2 via Homebrew
- [virtualhere](https://www.virtualhere.com/) (for USB sharing) or custom implementation

**Terminal commands:**

```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install Homebrew
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install cmake sdl2 sdl2_image

# For USB sharing (optional - install virtualhere client)
brew install --cask virtualhere
```

---

## 📦 INSTALLATION

### Linux (Ubuntu/Debian)

```bash
# Clone or download the source
cd ~/Downloads/RGM

# Install all dependencies
sudo apt-get update
sudo apt-get install -y \
    g++ \
    make \
    libx11-dev \
    libsdl2-dev \
    libsdl2-image-dev \
    usbip

# Load USB/IP kernel modules
sudo modprobe usbip-core
sudo modprobe usbip-host
sudo modprobe vhci-hcd

# Build the application
make clean
make

# Run the launcher
./app
```

### Linux (Fedora/RHEL)

```bash
cd ~/Downloads/RGM

sudo dnf install -y \
    gcc-c++ \
    make \
    libX11-devel \
    SDL2-devel \
    SDL2_image-devel \
    usbip

sudo modprobe usbip-core
sudo modprobe usbip-host
sudo modprobe vhci-hcd

make clean
make
./app
```

### Linux (Arch)

```bash
cd ~/Downloads/RGM

sudo pacman -S --noconfirm \
    gcc \
    make \
    libx11 \
    sdl2 \
    sdl2_image \
    usbip

sudo modprobe usbip-core
sudo modprobe usbip-host
sudo modprobe vhci-hcd

make clean
make
./app
```

### Windows 10/11

**Option A: Using MinGW-w64 (Recommended)**

```powershell
# Open PowerShell as Administrator

# Install Chocolatey if not already installed
Set-ExecutionPolicy Bypass -Scope Process -Force
[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

# Install dependencies
choco install mingw make cmake sdl2 sdl2-image usbipd-win -y

# Clone/download RGM
cd C:\Users\%USERNAME%\Downloads\RGM

# Build (using provided Makefile.windows)
mingw32-make -f Makefile.windows clean
mingw32-make -f Makefile.windows

# Run
.\app.exe
```

**Option B: Using Visual Studio 2022**

```powershell
# Open "Developer Command Prompt for VS 2022"

cd C:\Users\%USERNAME%\Downloads\RGM

# Build using nmake
nmake -f Makefile.msvc clean
nmake -f Makefile.msvc

# Run
.\app.exe
```

### macOS

```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install Homebrew
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install cmake sdl2 sdl2_image

# Optional: Install virtualhere for USB sharing
brew install --cask virtualhere

# Clone/download RGM
cd ~/Downloads/RGM

# Build
make -f Makefile.macos clean
make -f Makefile.macos

# Run
./app
```

### Using the Built-in Installer

**All platforms** can use the built-in installer after building:

```bash
make install-deps
```

The installer automatically detects your operating system and installs the correct packages.

---

## 🔧 BUILDING FROM SOURCE

### Directory Structure

```
RGM/
├── Makefile                 # Linux build
├── Makefile.windows         # Windows MinGW build
├── Makefile.msvc            # Windows Visual Studio build
├── Makefile.macos           # macOS build
├── README.md                # This file
├── assets/
│   └── icons/
│       └── rcorp.jpeg       # RGM logo (JPEG/PNG format)
└── src/
    ├── app.cpp              # Launcher application
    ├── sender.cpp           # Screen sender
    ├── receiver.cpp         # Screen receiver
    ├── discover.h           # Discovery API
    ├── discover.cpp         # SSDP discovery engine
    ├── USB_share.h          # USB sharing interface
    ├── USB_share.cpp        # USB sharing implementation
    ├── remote_fs.h          # Remote file access interface
    ├── remote_fs.cpp        # Remote file access implementation
    ├── platform.h           # Cross-platform abstractions
    └── platform.cpp         # Platform-specific implementations
```

### Build Commands

| Command | Linux | Windows | macOS |
|---------|-------|---------|-------|
| Build all | `make` | `mingw32-make -f Makefile.windows` | `make -f Makefile.macos` |
| Build launcher | `make app` | `mingw32-make -f Makefile.windows app` | `make -f Makefile.macos app` |
| Build sender | `make sender` | `mingw32-make -f Makefile.windows sender` | `make -f Makefile.macos sender` |
| Build receiver | `make receiver` | `mingw32-make -f Makefile.windows receiver` | `make -f Makefile.macos receiver` |
| Clean | `make clean` | `mingw32-make -f Makefile.windows clean` | `make -f Makefile.macos clean` |
| Debug build | `make debug` | `mingw32-make -f Makefile.windows debug` | `make -f Makefile.macos debug` |
| Check environment | `make check` | `mingw32-make -f Makefile.windows check` | `make -f Makefile.macos check` |

### Compilation Output

After successful build, you'll have:

**Linux/macOS:**

- `app` - Launcher application
- `sender` - Screen sender + USB client + file client
- `receiver` - Screen receiver + USB server + file server

**Windows:**

- `app.exe` - Launcher application
- `sender.exe` - Screen sender
- `receiver.exe` - Screen receiver

---

## 🚀 USAGE GUIDE

### Basic Screen Sharing

**On the receiving computer (any platform):**

```bash
# Linux/macOS
./receiver

# Windows
.\receiver.exe
```

**On the sending computer (any platform):**

```bash
# Linux/macOS
./sender

# Windows
.\sender.exe
```

**Process:**

1. Sender automatically discovers receivers on the network (works across all platforms)
2. Select a receiver from the list
3. Screen sharing begins automatically
4. Press `Ctrl+C` (Linux/macOS) or `Ctrl+Break` (Windows) to stop

### Using the Launcher

```bash
# Linux/macOS
./app

# Windows
.\app.exe
```

The launcher provides a cross-platform menu:

```
╔═══════════════════════════════════════╗
║     RGM v2.1.0           ║
║        Cross-Platform Edition         ║
╠═══════════════════════════════════════╣
║                                       ║
║  1. 🎥 SEND SCREEN                    ║
║  2. 📺 RECEIVE SCREEN                 ║
║  3. 🔌 USB SHARING INFO               ║
║  4. 📁 REMOTE FILE ACCESS INFO        ║
║  5. 🌐 CROSS-PLATFORM HELP            ║
║  6. ❌ EXIT                           ║
║                                       ║
╚═══════════════════════════════════════╝
```

### Cross-Platform Notes

| Feature | Linux | Windows | macOS |
|---------|-------|---------|-------|
| **Screen Capture** | X11 | GDI/DirectX | Core Graphics |
| **Display** | SDL2 | SDL2 | SDL2 |
| **USB Sharing** | USB/IP kernel | usbipd-win | virtualhere* |
| **File Paths** | Unix (/home) | Windows (C:\) | Unix (/Users) |
| **Line Endings** | LF | CRLF | LF |
| **Case Sensitivity** | Yes | No | No (but preserves) |

*\* macOS USB sharing requires third-party software or custom implementation*

---

## 🔌 USB SHARING

### How It Works

RGM uses platform-specific USB over IP solutions:

```
┌─────────────┐          Network          ┌─────────────┐
│   SENDER    │ ◄─────── TCP 3240 ───────► │  RECEIVER   │
│ (USB Client)│                           │ (USB Server)│
└─────────────┘                           └─────────────┘
       │                                          │
       │                                    ┌─────┴─────┐
       │                                    │ USB Device│
       │                                    │(Flash Drive│
       │                                    └───────────┘
       ▼
┌─────────────┐
│ USB Device  │
│ appears     │
│ locally     │
└─────────────┘
```

### Platform-Specific USB Support

| Platform | Server Component | Client Component | Notes |
|----------|-----------------|------------------|-------|
| **Linux** | `usbipd` (kernel) | `usbip` (kernel) | Native, best performance |
| **Windows** | `usbipd-win` service | `usbip` client | Install via Chocolatey |
| **macOS** | `virtualhere` server | `virtualhere` client | Third-party, or custom |

### Sender USB Menu

After connecting to a receiver that supports USB sharing:

```
📦 USB DEVICE SHARING MENU
========================================
1. List available USB devices on receiver
2. Request access to a USB device
3. Attach to a USB device
4. Detach a USB device
5. Show attached devices status
6. Return to main menu
```

### Receiver Permission Dialog

When a sender requests USB access (same on all platforms):

```
🔐 USB ACCESS REQUEST
   Device: 1-1.3 - SanDisk Ultra USB 3.0 (0781:5583)
   From: 192.168.1.100 (sender-pc)
   Platform: Windows 11
   Mode: full
   
   Allow? (y/n/[t]ime-limited): y
   Grant for how many seconds? (0 = unlimited): 3600
   
✅ Access granted for 3600 seconds
```

---

## 📁 REMOTE FILE ACCESS

### How It Works

The remote file system module handles platform-specific path formats automatically:

```
┌─────────────┐          Network          ┌─────────────┐
│   SENDER    │ ◄─────── TCP 8082 ───────► │  RECEIVER   │
│ (File Client│                           │ (File Server│
└─────────────┘                           └─────────────┘
       │                                          │
       │                                    ┌─────┴─────┐
       │                                    │  Storage  │
       │                                    │  Drives   │
       │                                    │  Files    │
       │                                    └───────────┘
       ▼
┌─────────────┐
│ Downloaded  │
│ Files       │
└─────────────┘
```

### Platform-Specific File Access

| Feature | Linux | Windows | macOS |
|---------|-------|---------|-------|
| **Drive Letters** | No (mount points) | Yes (C:, D:, etc.) | No (mount points) |
| **Path Separator** | `/` | `\` (converted) | `/` |
| **Home Directory** | `/home/user` | `C:\Users\username` | `/Users/username` |
| **Removable Drives** | `/media/user/` | `D:\` | `/Volumes/` |
| **Permissions** | Unix rwx | ACLs | Unix rwx |

### Sender File Browser

The file browser automatically adapts to the remote platform:

**Browsing a Windows receiver from Linux:**

```
📁 REMOTE FILE BROWSER [Page 1] - Windows
========================================
Current path: C:\Users\John\Documents
----------------------------------------
   [0] 📁 Desktop/
   [1] 📁 Downloads/
   [2] 📁 Music/
   [3] 📄 report.pdf         2.45 MB 2024-01-15 14:30:22
   [4] 🖼️ photo.jpg          1.20 MB 2024-01-14 09:15:07
```

**Browsing a Linux receiver from Windows:**

```
📁 REMOTE FILE BROWSER [Page 1] - Linux
========================================
Current path: /home/john/Documents
----------------------------------------
   [0] 📁 Desktop/
   [1] 📁 Downloads/
   [2] 📁 Music/
   [3] 📄 report.pdf         2.45 MB 2024-01-15 14:30:22
```

### Drive/Storage Listing

**Windows Receiver:**

```
💾 REMOTE STORAGE DEVICES - Windows
========================================
   [0] 💾 [REMOVABLE] D:\ (SanDisk USB)
       29.72 GB total, 14.80 GB free
       FS: NTFS, LABEL: "USB DRIVE"
   
   [1] 💽 [FIXED] C:\
       237.64 GB total, 89.23 GB free
       FS: NTFS
```

**macOS Receiver:**

```
💾 REMOTE STORAGE DEVICES - macOS
========================================
   [0] 💾 [REMOVABLE] /Volumes/Untitled
       29.72 GB total, 14.80 GB free
       FS: ExFAT
   
   [1] 💽 [FIXED] / (Macintosh HD)
       476.94 GB total, 312.56 GB free
       FS: APFS
```

### File Operations

**Download a file (cross-platform):**

```
Enter file number to download: 3
Save as (local path): ./downloaded_report.pdf
📥 Downloading: 45% [=========>---------]
✅ Download complete: report.pdf (2.45 MB)
```

**Upload a file (cross-platform):**

```
Local file to upload: ./local_photo.jpg
Remote path (in C:\Users\John\Documents): vacation.jpg
📤 Uploading: 78% [=============>------]
✅ Upload complete: vacation.jpg (1.20 MB)
```

### Receiver Permission Dialog

The permission dialog shows the platform of the requester:

```
🔐 FILE ACCESS REQUEST
   Path: C:\Users\John\Documents
   Operation: list
   From: 192.168.1.100 (sender-pc) [Linux]
   
   Allow? (y/n/[p]artial): p
   Enter allowed paths (comma-separated): C:\Users\John\Documents,C:\Users\John\Downloads
   Access mode: [r]ead-only or [r/w] read-write? r
   Grant for how many seconds? (0 = unlimited): 1800
   
✅ Access granted for 1800 seconds
   Allowed paths: C:\Users\John\Documents, C:\Users\John\Downloads
   Mode: read-only
```

---

## 🛡️ PERMISSION SYSTEM

### Permission Levels

| Level | USB Sharing | File Access |
|-------|-------------|-------------|
| **Full** | Read/write to device | Read/write/delete |
| **Read-Write** | Read/write | Read/write (no delete) |
| **Read-Only** | Read only | View only |
| **List-Only** | - | See files but not open |

### Permission Parameters

| Parameter | Description | Example (Linux) | Example (Windows) |
|-----------|-------------|-----------------|-------------------|
| **Path/Device** | What is being accessed | `/home/user` | `C:\Users\user` |
| **Requester** | Who is asking | IP, hostname, platform | IP, hostname, platform |
| **Operation** | What they want | `read`, `write` | `read`, `write` |
| **Duration** | How long valid | 3600 seconds | 3600 seconds |
| **Scope** | What's included | Single file, directory | Single file, directory |
| **Mode** | Access level | Read-only | Read-only |

### Cross-Platform Permission Storage

```cpp
struct Permission {
    std::string path;           // Platform-specific path
    std::string requester_ip;
    std::string requester_platform; // "Linux", "Windows", "macOS"
    time_t granted_time;
    time_t expiry_time;
    AccessMode mode;
    std::vector<std::string> allowed_paths;
};
```

---

## 📊 COMMAND REFERENCE

### Makefile Commands

| Command | Linux | Windows | macOS |
|---------|-------|---------|-------|
| Build all | `make` | `mingw32-make -f Makefile.windows` | `make -f Makefile.macos` |
| Clean | `make clean` | `mingw32-make -f Makefile.windows clean` | `make -f Makefile.macos clean` |
| Run launcher | `make run` | `mingw32-make -f Makefile.windows run` | `make -f Makefile.macos run` |
| Run sender | `make run-sender` | `mingw32-make -f Makefile.windows run-sender` | `make -f Makefile.macos run-sender` |
| Run receiver | `make run-receiver` | `mingw32-make -f Makefile.windows run-receiver` | `make -f Makefile.macos run-receiver` |
| Check env | `make check` | `mingw32-make -f Makefile.windows check` | `make -f Makefile.macos check` |
| Install deps | `make install-deps` | `mingw32-make -f Makefile.windows install-deps` | `make -f Makefile.macos install-deps` |
| Help | `make help` | `mingw32-make -f Makefile.windows help` | `make -f Makefile.macos help` |

### Application Controls

| Action | Linux/macOS | Windows |
|--------|-------------|---------|
| Quit | `ESC` or `Q` | `ESC` or `Q` |
| Stop sending | `Ctrl+C` | `Ctrl+Break` |
| Select menu item | Number + Enter | Number + Enter |
| Navigate file browser | Number + Enter | Number + Enter |

### USB Menu Commands

| Command | Description |
|---------|-------------|
| `1` | List USB devices on receiver |
| `2` | Request access to a device |
| `3` | Attach to a device |
| `4` | Detach a device |
| `5` | Show attached devices |
| `6` | Return to main menu |

### File Browser Commands

| Command | Description |
|---------|-------------|
| `n` | Next page |
| `p` | Previous page |
| `g` | Go to path |
| `d` | Download file |
| `u` | Upload file |
| `i` | File info |
| `q` | Quit browser |
| `[number]` | Navigate to directory |

---

## 🔍 TROUBLESHOOTING

### Common Issues

| Problem | Linux Solution | Windows Solution | macOS Solution |
|---------|----------------|------------------|----------------|
| **No receivers found** | Check firewall: `sudo ufw allow 1900/udp` | Allow in Windows Defender Firewall | Check firewall: `sudo pfctl -d` |
| **USB not working** | `sudo modprobe usbip-core` | Install usbipd-win | Install virtualhere |
| **Permission denied** | Check sudo access | Run as Administrator | Check System Preferences |
| **Build fails** | Install build-essential | Install VS Build Tools | Install Xcode CLT |
| **Splash screen blank** | Install libsdl2-image-dev | Install SDL2_image.dll | `brew install sdl2_image` |

### Firewall Configuration

**Linux (UFW):**

```bash
sudo ufw allow 1900/udp
sudo ufw allow 8081/tcp
sudo ufw allow 8082/tcp
sudo ufw allow 3240/tcp
```

**Windows (PowerShell as Admin):**

```powershell
New-NetFirewallRule -DisplayName "RGM SSDP" -Direction Inbound -Protocol UDP -LocalPort 1900 -Action Allow
New-NetFirewallRule -DisplayName "RGM Video" -Direction Inbound -Protocol TCP -LocalPort 8081 -Action Allow
New-NetFirewallRule -DisplayName "RGM File" -Direction Inbound -Protocol TCP -LocalPort 8082 -Action Allow
New-NetFirewallRule -DisplayName "RGM USB" -Direction Inbound -Protocol TCP -LocalPort 3240 -Action Allow
```

**macOS:**

```bash
sudo pfctl -d  # Temporarily disable for testing
# Or configure /etc/pf.conf for permanent rules
```

### Diagnostic Commands

**Check if receiver is running:**

```bash
# Linux/macOS
ps aux | grep receiver

# Windows
tasklist | findstr receiver
```

**Check open ports:**

```bash
# Linux
netstat -tulpn | grep -E '1900|8081|8082|3240'

# Windows
netstat -an | findstr "1900 8081 8082 3240"

# macOS
lsof -i -P | grep -E '1900|8081|8082|3240'
```

**Test TCP connection:**

```bash
# Linux/macOS
telnet <receiver-ip> 8081

# Windows
Test-NetConnection -ComputerName <receiver-ip> -Port 8081
```

---

## 🌐 NETWORK REQUIREMENTS

### Ports Used

| Port | Protocol | Purpose | Direction |
|------|----------|---------|-----------|
| 1900 | UDP | SSDP Discovery | Multicast |
| 8081 | TCP | Video Streaming | Sender → Receiver |
| 8082 | TCP | File Transfer | Bidirectional |
| 3240 | TCP | USB/IP | Bidirectional |

### Bandwidth Requirements

| Resolution | FPS | Bandwidth |
|------------|-----|-----------|
| 1920×1080 | 60 | ~280 MB/s |
| 1920×1080 | 30 | ~140 MB/s |
| 1280×720 | 60 | ~125 MB/s |
| 1280×720 | 30 | ~62 MB/s |

*File transfers and USB add minimal overhead*

### Network Topology

- ✓ Works best on same subnet
- ✓ Supports WiFi (Ethernet recommended for 60 FPS)
- ✓ Multicast must be enabled
- ✓ Works across VLANs with multicast routing
- ✓ Cross-platform compatible - Windows ↔ Linux ↔ macOS

---

## 👤 AUTHOR & CREDITS

### Author

**RGM** is created and maintained by **[RR-Ralefaso (polaris)](https://github.com/RR-Ralefaso)** - an experienced Software Developer, Systems Analyst, and researcher focused on Applied Mathematics and Computer Science.

- **GitHub:** [@RR-Ralefaso](https://github.com/RR-Ralefaso)
- **Expertise:** Backend systems, AI/ML, Computer Vision, Systems Programming
- **Tech Stack:** C++, Python, Rust, Go, Java, and more

### Core Contributions

- 🏗️ **Architecture Design** - Modular, extensible, cross-platform design
- 🔍 **SSDP Discovery** - Zero-configuration networking across all platforms
- 🔌 **USB/IP Integration** - Remote USB device sharing (Linux native, Windows via usbipd-win)
- 📁 **File System Access** - Remote file management with platform abstraction
- 🛡️ **Permission System** - Granular access control with platform awareness
- 🎨 **SDL2 Interface** - Cross-platform graphics and input
- ⚡ **Performance** - High FPS streaming optimization for all OSes
- 🔄 **Path Conversion** - Automatic translation between Windows and Unix paths

### Version History

| Version | Date | Features |
|---------|------|----------|
| **v2.1.0** | 2024 | Remote file access, permission system, cross-platform support |
| **v2.0.0** | 2024 | USB sharing, SSDP discovery, SDL2 interface |
| **v1.0.0** | 2023 | Initial release, basic TCP streaming (Linux only) |

### Acknowledgments

Special thanks to:

- The SDL2 and SDL2_image development teams
- USB/IP Linux kernel community
- usbipd-win developers for Windows USB/IP
- virtualhere team for macOS USB sharing
- X11, Windows API, and macOS Core Graphics teams
- All contributors and users who provided feedback


---

<div align="center">
  <img src="assets/icons/rcorp.jpeg" alt="R-Corp Logo" width="150"/>
  <h3>RGM</h3>
  <p><em>Share your screen, your devices, and your files - across any platform.</em></p>
  <p><strong>Created by RR-Ralefaso (polaris)</strong></p>
  <p>
    <a href="https://github.com/RR-Ralefaso">GitHub</a> •
    <a href="https://github.com/RR-Ralefaso/RGM">Project Page</a> •
    <a href="#-table-of-contents">Back to Top</a>
  </p>
  <p>⭐ If you find this project useful, please consider giving it a star!</p>
</div>

---

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
