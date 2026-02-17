# Auto-Discovery Screen Sharing - Professional Project Structure
CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2
SDLFLAGS = `sdl2-config --cflags --libs`
SRCDIR = src

# Build executables in parent directory from src/ sources
all: sender receiver

# Sender: src/sender.cpp + src/discover.cpp → ./sender
sender: $(SRCDIR)/sender.cpp $(SRCDIR)/discover.cpp $(SRCDIR)/discover.h
	$(CXX) -o $@ $(SRCDIR)/sender.cpp $(SRCDIR)/discover.cpp $(SDLFLAGS) -lX11 $(CXXFLAGS)

# Receiver: src/receiver.cpp + src/discover.cpp → ./receiver  
receiver: $(SRCDIR)/receiver.cpp $(SRCDIR)/discover.cpp $(SRCDIR)/discover.h
	$(CXX) -o $@ $(SRCDIR)/receiver.cpp $(SRCDIR)/discover.cpp $(SDLFLAGS) $(CXXFLAGS)

# Clean executables from parent directory
clean:
	rm -f sender receiver

.PHONY: all clean
