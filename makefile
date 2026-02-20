# ============================================================================
# RGM PROJECT MAKEFILE
# ============================================================================

CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2
LDFLAGS = 
SRCDIR = src
BUILDDIR = build

# Create build directory if missing
$(shell mkdir -p $(BUILDDIR))

# Platform detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    # Linux: X11 for sender screen capture, pthread for threads
    LDFLAGS += -lX11 -lpthread
    
    # SDL2 detection
    SDL2_CONFIG = $(shell which sdl2-config 2>/dev/null)
    ifneq ($(SDL2_CONFIG),)
        SDL_CFLAGS := $(shell sdl2-config --cflags)
        SDL_LIBS := $(shell sdl2-config --libs)
    else ifneq ($(shell pkg-config --exists sdl2 2>/dev/null && echo 1),)
        SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
        SDL_LIBS := $(shell pkg-config --libs sdl2)
    else
        SDL_CFLAGS := -I/usr/include/SDL2 -D_REENTRANT
        SDL_LIBS := -lSDL2
    endif
    
    CXXFLAGS += $(SDL_CFLAGS)
    
else ifeq ($(UNAME_S),Darwin)
    # macOS
    CXXFLAGS += -I/opt/homebrew/include/SDL2 -I/usr/local/include/SDL2
    LDFLAGS += -lpthread -lSDL2
endif

# Windows (MinGW) detection
ifeq ($(OS),Windows_NT)
    CXXFLAGS += -DWIN32
    LDFLAGS += -lws2_32 -lgdi32 -lpthread
    SDL_LIBS := -lSDL2
endif

# Default target
all: sender receiver
	@echo ""
	@echo "========================================="
	@echo "✅ Build complete!"
	@echo "========================================="
	@echo "Run with:"
	@echo "  make run-receiver  (start receiver first)"
	@echo "  make run-sender    (then start sender)"
	@echo "========================================="

# Executables
sender: $(BUILDDIR)/sender.o $(BUILDDIR)/discover.o
	$(CXX) -o $@ $^ $(LDFLAGS)
	@echo "✅ Built sender"

receiver: $(BUILDDIR)/receiver.o $(BUILDDIR)/discover.o
	$(CXX) -o $@ $^ $(LDFLAGS) $(SDL_LIBS)
	@echo "✅ Built receiver"

# Object files
$(BUILDDIR)/sender.o: $(SRCDIR)/sender.cpp $(SRCDIR)/discover.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/receiver.o: $(SRCDIR)/receiver.cpp $(SRCDIR)/discover.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/discover.o: $(SRCDIR)/discover.cpp $(SRCDIR)/discover.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Convenience targets
clean:
	rm -rf $(BUILDDIR) sender receiver
	@echo "✅ Cleaned build files"

run-receiver: receiver
	@echo ""
	@echo "========================================="
	@echo "📺 STARTING RECEIVER"
	@echo "========================================="
	./receiver

run-sender: sender
	@echo ""
	@echo "========================================="
	@echo "🎥 STARTING SENDER"
	@echo "========================================="
	./sender

run-demo: receiver sender
	@echo ""
	@echo "========================================="
	@echo "🐳 SCREEN SHARE DEMO"
	@echo "========================================="
	@echo "📡 Terminal 1 (Receiver): make run-receiver"
	@echo "📡 Terminal 2 (Sender):   make run-sender"
	@echo "⚠️  BOTH must be on the SAME network!"
	@echo "========================================="

debug: CXXFLAGS += -g -O0
debug: all
	@echo "🔧 Debug build complete"

install-deps:
	@echo "📦 Installing dependencies..."
	@if command -v apt-get >/dev/null 2>&1; then \
		sudo apt-get update && \
		sudo apt-get install -y g++ make libx11-dev libsdl2-dev; \
	elif command -v yum >/dev/null 2>&1; then \
		sudo yum install -y gcc-c++ make libX11-devel SDL2-devel; \
	elif command -v pacman >/dev/null 2>&1; then \
		sudo pacman -S --noconfirm gcc make libx11 sdl2; \
	else \
		echo "⚠️  Unsupported package manager. Install manually:"; \
		echo "   - g++ (compiler)"; \
		echo "   - libX11 (X11 development libraries)"; \
		echo "   - SDL2 (Simple DirectMedia Layer)"; \
	fi
	@echo "✅ Dependencies installed (hopefully)"

check:
	@echo "🔍 Checking build environment..."
	@echo "Platform: $(UNAME_S)"
	@echo "Compiler: $(CXX)"
	@echo "C++ Flags: $(CXXFLAGS)"
	@echo "Linker Flags: $(LDFLAGS)"
	@echo ""

help:
	@echo "SCREEN SHARE MAKEFILE COMMANDS:"
	@echo "  make all           - Build sender and receiver"
	@echo "  make sender        - Build only sender"
	@echo "  make receiver      - Build only receiver"
	@echo "  make run-receiver  - Run receiver"
	@echo "  make run-sender    - Run sender"
	@echo "  make run-demo      - Show demo instructions"
	@echo "  make debug         - Build with debug symbols"
	@echo "  make install-deps  - Install dependencies (Linux)"
	@echo "  make check         - Check build environment"
	@echo "  make clean         - Clean build files"
	@echo "  make help          - Show this help"

.PHONY: all clean run-receiver run-sender run-demo debug install-deps check help