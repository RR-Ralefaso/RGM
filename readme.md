<p align="center">
  <img src="assets/icons/RGM.png" width="50" alt="RGM Logo">
</p>


# RPGM

## Reason for Building

 - Ralefaso GlassMirror
 - **RGM** was created because i got annoyed trying make my windows device(laptop) act as a temporary monitor for my Linux device alongside with an android device .I came up with the idea to make it easier to do so across devices so i can use it


## About
 - Built with **C++**
 - Created by **[RR-Ralefaso (polaris)](https://github.com/RR-Ralefaso)**


## Future Plans

Transform the receiver into a **device extender** - not just mirroring, but using the receiver's hardware (GPU, CPU, peripherals) for the sender's workloads.



## Platforms Supported

| Desktop | Mobile |
|---------|--------|
| Windows | iOS |
| Linux   | Android |
| macOS   | |


## Features

- **Cross-platform**: Windows, Linux, macOS (desktop)
- **Mobile support**: iOS, Android
- **Low latency**: Built for real-time screen mirroring
- **Simple binaries**: Just compile and run

# Compile  

> [!IMPORTANT]  
> **Order of Operations:** ALWAYS run the **Receiver** first on the display device, then run the **Sender** on the source device.


  ## Linux
    - sudo apt install libx11-dev libsdl2-dev #needed once

    - g++ -o sender sender.cpp `sdl2-config --cflags --libs` -lX11 -std=c++17

    - g++ -o receiver receiver.cpp `sdl2-config --cflags --libs` -std=c++17

    - ./receiver  #run first on the receiver device if Linux
    - ./sender   #run first on the sender device if linux

  ## macOS
    - brew install sdl2 #needed once
    
    - g++ -o sender sender.cpp $(sdl2-config --cflags --libs) -framework ApplicationServices -framework Cocoa -std=c++17

    - g++ -o receiver receiver.cpp $(sdl2-config --cflags --libs) -std=c++17

    - ./receiver #run first on the receiver device if Linux
    - ./sender   #run first on the sender device if MacOS
  ## Windows (cmd)
  >[!NOTE]
  > You must replace C:\SDL2 with the actual path where you extracted the SDL2 development files. Windows requires SDL2main.lib and shell32.lib to initialize the windowing system correctly.

    - cl sender.cpp /I"C:\SDL2\include" /link /LIBPATH:"C:\SDL2\lib\x64" SDL2.lib SDL2main.lib ws2_32.lib shell32.lib /std:c++17 /EHsc /out:sender.exe

    - cl receiver.cpp /I"C:\SDL2\include" /link /LIBPATH:"C:\SDL2\lib\x64" SDL2.lib SDL2main.lib ws2_32.lib shell32.lib /std:c++17 /EHsc /out:receiver.exe

    - receiver.exe #run first on the receiver device if windows
    - sender.exe #run first on the sender device if windows

# MOBILE
  - iOS: Xcode project + CoreGraphics.framework
  - Android: Android Studio NDK + EGL/GLESv2

-----


<p align="center">
  <img src="assets/icons/rcorp.jpeg" width="50" alt="R-Corp Logo">
  <br>
  <b>ROOT ACCESS FOR EVERYONE</b>
  
</p>


