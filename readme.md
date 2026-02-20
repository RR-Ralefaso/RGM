<p align="center">
  <img src="assets/icons/RGM.png" width="50" alt="RGM Logo">
</p>

# RGM - Ralefaso GlassMirror

## Why I Built This

I got tired of trying to use my Windows laptop as a temporary monitor for my Linux device (alongside Android devices). I wanted a simple, cross-platform solution to mirror screens across devices without complicated setup or proprietary software.

**RGM** is my answer: a lightweight, zero-configuration screen sharing system that just works.

## About

- **Built with**: C++11
- **Network Protocol**: SSDP discovery + TCP streaming
- **Resolution**: 1280x720 @ 10 FPS
- **Author**: [RR-Ralefaso (polaris)](https://github.com/RR-Ralefaso)

## Current Features

✅ **Zero-configuration discovery** - Receivers automatically advertise themselves, senders automatically find them  
✅ **Cross-platform** - Works on Windows, Linux, and macOS  
✅ **Real-time streaming** - 10 FPS screen capture and display  
✅ **Simple to use** - Just compile and run, no configuration files  
✅ **SSDP-based** - Uses standard UPnP discovery protocol  

## Future Plans

Transform the receiver into a **device extender** - not just mirroring, but using the receiver's hardware (GPU, CPU, peripherals) for the sender's workloads.

## Supported Platforms

| Desktop | Mobile (Planned) |
|---------|------------------|
| Windows | iOS |
| Linux   | Android |
| macOS   | |

## How It Works

**RGM** consists of two components:

1. **Receiver** (`receiver.cpp`) - Runs on the display device, advertises itself via SSDP, and shows the incoming video stream using SDL2
2. **Sender** (`sender.cpp`) - Runs on the source device, discovers receivers on the network, captures the screen, and streams it

The system uses **SSDP (Simple Service Discovery Protocol)** to automatically find receivers - no need to manually enter IP addresses!

## Compilation

> [!IMPORTANT]
> **Order of Operations:** ALWAYS run the **Receiver** first on the display device, then run the **Sender** on the source device.

### Prerequisites

#### Linux

```bash
# Install required libraries (Debian/Ubuntu)
sudo apt update
sudo apt install libx11-dev libsdl2-dev g++ make

# For Fedora/RHEL
sudo dnf install libX11-devel SDL2-devel gcc-c++ make

# For Arch
sudo pacman -S libx11 sdl2 gcc make


```

### MacOS

```bash
 # Install SDL2 using Homebrew
brew install sdl2

```

### windows
  - Download SDL2 development libraries from libsdl.org
  - Extract to a known location (e.g., C:\SDL2)



## BUIlD INSTRUCTIONS

## linux

```bash
# Build sender
g++ -std=c++11 -o sender src/sender.cpp src/discover.cpp -lX11 -lpthread

# Build receiver  
g++ -std=c++11 -o receiver src/receiver.cpp src/discover.cpp $(sdl2-config --cflags --libs) -lpthread

# Or use the Makefile
make all
```

## MacOS

```bash
  # Build sender
  g++ -std=c++11 -o sender src/sender.cpp src/discover.cpp -lpthread

  # Build receiver
  g++ -std=c++11 -o receiver src/receiver.cpp src/discover.cpp $(sdl2-config --cflags --libs) -lpthread

  # Or use the Makefile
  make all

```

## Windows (MinGW)

```bash
  # Build sender
  g++ -std=c++11 -o sender.exe src/sender.cpp src/discover.cpp -lws2_32 -lgdi32 -lpthread

  # Build receiver
  g++ -std=c++11 -o receiver.exe src/receiver.cpp src/discover.cpp -lws2_32 -lSDL2 -lpthread


```

## Windows (Visual Studio)

 ```bash
  cl src/sender.cpp src/discover.cpp /I"C:\SDL2\include" /link /LIBPATH:"C:\SDL2\lib\x64" ws2_32.lib /std:c++11 /EHsc /out:sender.exe

  cl src/receiver.cpp src/discover.cpp /I"C:\SDL2\include" /link /LIBPATH:"C:\SDL2\lib\x64" SDL2.lib SDL2main.lib ws2_32.lib shell32.lib /std:c++11 /EHsc /out:receiver.exe


  ```

# Using the Makefile (Linux/macOS)

### The included Makefile handles platform detection automatically (im amazing i know):

  ```bash

    # Check your build environment
  make check

  # Install dependencies (Linux only)
  make install-deps

  # Build everything
  make all

  # Build individually
  make sender
  make receiver

  # Run (in separate terminals)
  make run-receiver  # Terminal 1 - display device
  make run-sender    # Terminal 2 - source device

  # Clean build files
  make clean


  ```

## Running the Application

1. Start the receiver on the device that will display the screen:
    - ./receiver
2. Start the sender on the device whose screen you want to share:
    - ./sender
3. The sender will automatically discover any receivers on the network and prompt you to select one
4. Streaming begins automatically!

# Troubleshooting

## No receivers found?

- Ensure both devices are on the same network
- Check firewall settings (allow UDP port 1900 and TCP port 8081)
- Make sure receiver is running BEFORE the sender


## Connection fails?

- Verify the IP address shown during discovery
- Test basic connectivity: ping <receiver_ip>
- Check if TCP port 8081 is accessible: telnet <receiver_ip> 8081


## Poor performance?

- The current version uses uncompressed RGB24 (high bandwidth)
- For best results, use a wired Ethernet connection
- Reduce resolution or FPS in the source code if needed


# PROJECT STRUCTURE :


rgm/
├── src/
│   ├── sender.cpp      # Screen capture and streaming
│   ├── receiver.cpp    # Video display and SSDP advertising
│   ├── discover.cpp    # SSDP discovery implementation
│   └── discover.h      # Discovery API
├── Makefile            # Platform-aware build system
├──  - - - 
└── assets/
    └── icons/          # Project icons

