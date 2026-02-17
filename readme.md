<p align="center">
  <img src="assets/icons/RGM.png" width="50" alt="RGM Logo">
</p>


# RPGM

## Reason for Building

 - Ralefaso GlassMirror
 - **RGM** was created because i got annoyed trying make my windows device(laptop) act as a temporary monitor for my Linux device alongside with an android device .I came up with the idea to make it easier to do so across devices so i can use it


## About

 - built with  c++
 - created by RR-Ralefaso (polaris)


## platforms
 - Windows
 - Linux
 - macos



# Compile 
 **ALWAYS RUN RECEIVER FIRST THEN SENDER**
  ## Linux
    - sudo apt install libx11-dev libsdl2-dev #needed once
    - g++ -o sender sender.cpp `sdl2-config --cflags --libs` -lX11 -std=c++17 -pthread
    - g++ -o receiver receiver.cpp `sdl2-config --cflags --libs` -std=c++17 -pthread
    - ./receiver  #run first on the receiver device if Linux
    - ./sender   #run first on the sender device if linux

  ## macOS
    - brew install sdl2 #needed once
    - g++ -o sender sender.cpp `sdl2-config --cflags --libs` -framework ApplicationServices -std=c++17
    - g++ -o receiver receiver.cpp `sdl2-config --cflags --libs` -std=c++17
    - ./receiver #run first on the receiver device if Linux
    - ./sender   #run first on the sender device if MacOS
  ## Windows (cmd)
    - cl sender.cpp SDL2.lib ws2_32.lib /std:c++17 /EHsc
    - cl receiver.cpp SDL2.lib ws2_32.lib /std:c++17 /EHsc
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




