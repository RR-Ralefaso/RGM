# ============================================================================
# SCREEN SHARE PROJECT MAKEFILE - FIXED VERSION
# ============================================================================

CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -I $(SRCDIR) -I/usr/include/SDL2 -D_REENTRANT
LDFLAGS = 
SRCDIR = src
BUILDDIR = build

# Platform detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    # Linux: X11 for sender screen capture, pthread for threads
    LDFLAGS += -lX11 -lpthread 
    # SDL2 detection (multiple fallback methods)
    ifneq ($(SDL2_CFLAGS),)
        SDL_CFLAGS := $(SDL2_CFLAGS)
        SDL_LIBS := $(SDL2_LIBS)
    else ifneq ($(shell pkg-config --exists sdl2 && echo 1),)
        SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
        SDL_LIBS := $(shell pkg-config --libs sdl2)
    else
        SDL_CFLAGS := -I/usr/include/SDL2 -D_REENTRANT
        SDL_LIBS := -lSDL2
    endif
endif
ifeq ($(UNAME_S),Darwin)
    CXXFLAGS += -I/opt/homebrew/include/SDL2
    LDFLAGS += -framework Cocoa -lpthread -lSDL2
endif

# Create build directory if missing
$(shell mkdir -p $(BUILDDIR))

# Default target
all: sender receiver
	@echo "✅ Build complete! Run with: make run-receiver & make run-sender"
	@ls -la sender receiver

# Executables
sender: $(BUILDDIR)/sender.o $(BUILDDIR)/discover.o
	$(CXX) -o $@ $^ $(LDFLAGS)
	@echo "✅ Built sender"

receiver: $(BUILDDIR)/receiver.o $(BUILDDIR)/discover.o
	$(CXX) -o $@ $^ $(LDFLAGS) $(SDL_LIBS)
	@echo "✅ Built receiver"

# Object files - FIXED PATHS
$(BUILDDIR)/sender.o: $(SRCDIR)/sender.cpp $(SRCDIR)/discover.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/receiver.o: $(SRCDIR)/receiver.cpp $(SRCDIR)/discover.h
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(BUILDDIR)/discover.o: $(SRCDIR)/discover.cpp $(SRCDIR)/discover.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Convenience targets
clean:
	rm -rf $(BUILDDIR) sender receiver
	@echo "✅ Cleaned build files"

# Run targets (receiver first!)
run-receiver: receiver
	./receiver

run-sender: sender
	./sender

run-demo: receiver sender
	@echo "🐳 Demo: Terminal 1: ./receiver"
	@echo "🐳 Demo: Terminal 2: ./sender (on same WiFi)"
	@echo "📡 Both must be on SAME WiFi network!"

# Phony targets
.PHONY: all clean run-receiver run-sender run-demo
