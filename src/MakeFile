CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2
SDLFLAGS = `sdl2-config --cflags --libs`

all: sender receiver

sender: sender.cpp discover.cpp
	$(CXX) -o sender $^ $(SDLFLAGS) -lX11 $(CXXFLAGS)

receiver: receiver.cpp discover.cpp
	$(CXX) -o receiver $^ $(SDLFLAGS) $(CXXFLAGS)

clean:
	rm -f sender receiver

.PHONY: all clean
