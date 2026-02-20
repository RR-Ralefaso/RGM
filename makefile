# ============================================================================
# SCREEN SHARE PROJECT MAKEFILE - COMPLETELY FIXED VERSION
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
    
    # SDL2 detection (multiple fallback methods)
    SDL2_CONFIG = $(shell which sdl2-config 2>/dev/null)
    ifneq ($(SDL2_CONFIG),)
        # Use sdl2-config if available (most reliable)
        SDL_CFLAGS := $(shell sdl2-config --cflags)
        SDL_LIBS := $(shell sdl2-config --libs)
    else ifneq ($(shell pkg-config --exists sdl2 2>/dev/null && echo 1),)
        # Fallback to pkg-config
        SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
        SDL_LIBS := $(shell pkg-config --libs sdl2)
    else
        # Manual fallback
        SDL_CFLAGS := -I/usr/include/SDL2 -D_REENTRANT
        SDL_LIBS := -lSDL2
    endif
    
    # Add SDL flags to CXXFLAGS for receiver
    CXXFLAGS += $(SDL_CFLAGS)
    
else ifeq ($(UNAME_S),Darwin)
    # macOS
    CXXFLAGS += -I/opt/homebrew/include/SDL2 -I/usr/local/include/SDL2
    LDFLAGS += -lpthread -lSDL2
    # On macOS, you might need:
    # LDFLAGS += -framework Cocoa -framework OpenGL
endif

# Windows (MinGW) detection
ifeq ($(OS),Windows_NT)
    CXXFLAGS += -DWIN32
    LDFLAGS += -lws2_32 -lgdi32 -lpthread
    # SDL2 for Windows
    SDL_LIBS := -lSDL2
    # Remove Linux-specific flags
    LDFLAGS := $(filter-out -lX11 -lpthread, $(LDFLAGS))
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
	@ls -la sender receiver 2>/dev/null || echo ""

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

# Run targets (receiver first!)
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

# Debug build (with symbols)
debug: CXXFLAGS += -g -O0
debug: all
	@echo "🔧 Debug build complete"

# Install dependencies (Ubuntu/Debian)
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

# Check if build directory exists
check:
	@echo "🔍 Checking build environment..."
	@echo "Platform: $(UNAME_S)"
	@echo "Compiler: $(CXX)"
	@echo "C++ Flags: $(CXXFLAGS)"
	@echo "Linker Flags: $(LDFLAGS)"
	@echo "SDL Flags: $(SDL_CFLAGS) $(SDL_LIBS)"
	@echo ""
	@echo "Checking for required libraries:"
	@echo -n "  X11: "
	@if [ "$(UNAME_S)" = "Linux" ]; then \
		if ldconfig -p | grep -q libX11; then \
			echo "✅"; \
		else \
			echo "❌ (install libx11-dev)"; \
		fi \
	else \
		echo "N/A"; \
	fi
	@echo -n "  SDL2: "
	@if ldconfig -p 2>/dev/null | grep -q libSDL2 || [ -f /usr/lib/libSDL2.so ] || [ -f /usr/local/lib/libSDL2.dylib ]; then \
		echo "✅"; \
	else \
		echo "❌ (install libsdl2-dev)"; \
	fi
	@echo ""

# Help target
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

# Phony targets
.PHONY: all clean run-receiver run-sender run-demo debug install-deps check help