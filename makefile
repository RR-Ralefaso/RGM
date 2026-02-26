# ============================================================================
# RGM PROJECT MAKEFILE - SCREEN SHARE APPLICATION
# ============================================================================

# Compiler and basic flags
CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2
LDFLAGS = 
SRCDIR = src
BUILDDIR = build

# Create build directory if missing
$(shell mkdir -p $(BUILDDIR))

# ============================================================================
# PLATFORM DETECTION
# ============================================================================

UNAME_S := $(shell uname -s)

# Linux specific
ifeq ($(UNAME_S),Linux)
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
endif

# macOS specific
ifeq ($(UNAME_S),Darwin)
	CXXFLAGS += -I/opt/homebrew/include/SDL2 -I/usr/local/include/SDL2
	LDFLAGS += -lpthread -lSDL2
endif

# Windows specific
ifeq ($(OS),Windows_NT)
	CXXFLAGS += -DWIN32
	LDFLAGS += -lws2_32 -lgdi32 -lpthread
	SDL_LIBS := -lSDL2
endif

# ============================================================================
# CHECK FOR SOURCE FILES
# ============================================================================

# Default target - show help if no source files
all: check_files
	@echo "========================================="
	@echo "🎬 Build complete!"
	@echo "========================================="

check_files:
	@echo "========================================="
	@echo "🔍 Checking available components..."
	@echo "========================================="

# Build app if available
app: $(BUILDDIR)/app.o $(BUILDDIR)/discover.o
	$(CXX) -o $@ $(BUILDDIR)/app.o $(BUILDDIR)/discover.o $(LDFLAGS) $(SDL_LIBS)
	@echo "✅ Built app launcher"

# Build sender if available
sender: $(BUILDDIR)/sender.o $(BUILDDIR)/discover.o
	$(CXX) -o $@ $(BUILDDIR)/sender.o $(BUILDDIR)/discover.o $(LDFLAGS) $(SDL_LIBS)
	@echo "✅ Built sender"

# Build receiver if available
receiver: $(BUILDDIR)/receiver.o $(BUILDDIR)/discover.o
	$(CXX) -o $@ $(BUILDDIR)/receiver.o $(BUILDDIR)/discover.o $(LDFLAGS) $(SDL_LIBS)
	@echo "✅ Built receiver"

# Object file rules
$(BUILDDIR)/app.o: $(SRCDIR)/app.cpp $(SRCDIR)/discover.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/sender.o: $(SRCDIR)/sender.cpp $(SRCDIR)/discover.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/receiver.o: $(SRCDIR)/receiver.cpp $(SRCDIR)/discover.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/discover.o: $(SRCDIR)/discover.cpp $(SRCDIR)/discover.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Conditional builds based on file existence
ifeq ($(shell test -f $(SRCDIR)/app.cpp && echo 1),1)
all: app
	@echo "✅ App launcher built"
endif

ifeq ($(shell test -f $(SRCDIR)/sender.cpp && echo 1),1)
all: sender
	@echo "✅ Sender built"
endif

ifeq ($(shell test -f $(SRCDIR)/receiver.cpp && echo 1),1)
all: receiver
	@echo "✅ Receiver built"
endif

# ============================================================================
# CONVENIENCE TARGETS
# ============================================================================

# Clean
clean:
	rm -rf $(BUILDDIR) app sender receiver app.exe sender.exe receiver.exe
	@echo "✅ Cleaned build files"

# Run app (if available)
run: app
	@if [ -f ./app ]; then \
		echo "========================================="; \
		echo "🎬 RGM  LAUNCHER"; \
		echo "========================================="; \
		./app; \
	else \
		echo "❌ App launcher not built. Run 'make' first."; \
	fi

# Run receiver (if available)
run-receiver: receiver
	@if [ -f ./receiver ]; then \
		echo "========================================="; \
		echo "📺 STARTING RECEIVER"; \
		echo "========================================="; \
		./receiver; \
	else \
		echo "❌ Receiver not built. Run 'make' first."; \
	fi

# Run sender (if available)
run-sender: sender
	@if [ -f ./sender ]; then \
		echo "========================================="; \
		echo "🎥 STARTING SENDER"; \
		echo "========================================="; \
		./sender; \
	else \
		echo "❌ Sender not built. Run 'make' first."; \
	fi

# Demo instructions
run-demo:
	@echo ""
	@echo "========================================="
	@echo "🐳 SCREEN SHARE DEMO"
	@echo "========================================="
	@echo "📡 Terminal 1 (Receiver): make run-receiver"
	@echo "📡 Terminal 2 (Sender):   make run-sender"
	@echo "⚠️  BOTH must be on the SAME network!"
	@echo "========================================="

# Debug build
debug: CXXFLAGS += -g -O0 -DDEBUG
debug: clean all
	@echo "🔧 Debug build complete"

# Check build environment
check:
	@echo "========================================="
	@echo "🔍 BUILD ENVIRONMENT CHECK"
	@echo "========================================="
	@echo "Platform:       $(UNAME_S)"
	@echo "Compiler:       $(CXX)"
	@echo "C++ Flags:      $(CXXFLAGS)"
	@echo "Linker Flags:   $(LDFLAGS)"
	@echo "========================================="
	@echo "📁 Source files in $(SRCDIR)/:"
	@for file in app.cpp sender.cpp receiver.cpp discover.cpp discover.h; do \
		if [ -f $(SRCDIR)/$$file ]; then \
			echo "  ✅ $$file"; \
		else \
			echo "  ❌ $$file"; \
		fi \
	done
	@echo "========================================="
	@if [ -f assets/icons/rcorp.jpeg ]; then \
		echo "✅ rcorp.jpeg found in assets/icons/"; \
	else \
		echo "⚠️  rcorp.jpeg not found (optional)"; \
	fi
	@echo "========================================="

# Install dependencies
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
	@echo "✅ Dependencies installation completed"

# Help
help:
	@echo ""
	@echo "╔════════════════════════════════════════════════╗"
	@echo "║     RGM SCREEN SHARE - MAKEFILE HELP          ║"
	@echo "╚════════════════════════════════════════════════╝"
	@echo ""
	@echo "BUILD COMMANDS:"
	@echo "  make           - Build all available components"
	@echo "  make app       - Build app launcher"
	@echo "  make sender    - Build sender"
	@echo "  make receiver  - Build receiver"
	@echo "  make debug     - Build with debug symbols"
	@echo ""
	@echo "RUN COMMANDS:"
	@echo "  make run           - Run app launcher"
	@echo "  make run-receiver  - Run receiver directly"
	@echo "  make run-sender    - Run sender directly"
	@echo "  make run-demo      - Show demo instructions"
	@echo ""
	@echo "MAINTENANCE:"
	@echo "  make clean         - Remove build files"
	@echo "  make check         - Check build environment"
	@echo "  make install-deps  - Install dependencies"
	@echo "  make help          - Show this help"
	@echo ""

.PHONY: all clean run run-receiver run-sender run-demo debug install-deps check help