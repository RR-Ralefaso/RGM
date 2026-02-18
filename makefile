# ============================================================================
# SCREEN SHARE PROJECT MAKEFILE
# ============================================================================
# Supports Windows (MinGW/MSVC), Linux, and macOS
# Source files are located in the 'src' directory
# Features:
#   - Automatic platform detection
#   - SDL2 configuration (pkg-config on Linux/macOS)
#   - Thread support (-pthread)
#   - Debug and release builds
#   - Cross-platform cleanup
# ============================================================================

# --- Platform Detection ---
ifeq ($(OS),Windows_NT)
    PLATFORM := Windows
    # Check if we're using MinGW or MSVC
    ifeq ($(findstring mingw,$(CXX)),mingw)
        MINGW := 1
    endif
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        PLATFORM := Linux
    endif
    ifeq ($(UNAME_S),Darwin)
        PLATFORM := macOS
    endif
endif

# --- Project Configuration ---
CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2
DEBUG_FLAGS = -g -DDEBUG
LDFLAGS = 
EXE_EXT = 
RM = rm -f

# --- Directories ---
SRCDIR = src
BUILDDIR = build
BINDIR = .

# Create build directory if it doesn't exist
$(shell mkdir -p $(BUILDDIR))

# --- Platform-Specific Settings ---
ifeq ($(PLATFORM),Windows)
    EXE_EXT = .exe
    RM = del /Q /F 2>nul || true
    
    # SDL2 paths - ADJUST THESE TO YOUR INSTALLATION
    SDL_PATH = C:/SDL2
    SDL_INC = -I$(SDL_PATH)/include
    SDL_LIB = -L$(SDL_PATH)/lib -lmingw32 -lSDL2main -lSDL2 -lws2_32 -lgdi32 -lshell32
    
    # Additional Windows libraries
    LIBS = $(SDL_LIB) -static-libgcc -static-libstdc++
    
    # Windows-specific flags
    CXXFLAGS += -D_WIN32_WINNT=0x0601 -DWIN32_LEAN_AND_MEAN
    
else ifeq ($(PLATFORM),Linux)
    # Linux: Use pkg-config for SDL2
    SDL_INC = $(shell pkg-config --cflags sdl2)
    SDL_LIB = $(shell pkg-config --libs sdl2)
    LIBS = $(SDL_LIB) -lX11 -lpthread
    CXXFLAGS += -pthread
    
else ifeq ($(PLATFORM),macOS)
    # macOS: Use framework paths
    SDL_INC = $(shell sdl2-config --cflags 2>/dev/null) -I/Library/Frameworks/SDL2.framework/Headers
    SDL_LIB = -framework SDL2 -framework Cocoa -framework OpenGL -lpthread
    CXXFLAGS += -pthread -mmacosx-version-min=10.9
endif

# --- Complete Compiler Flags ---
CXXFLAGS += $(SDL_INC)
LDFLAGS += $(LIBS)
CPPFLAGS = -I$(SRCDIR)

# --- Source Files ---
COMMON_SRC = $(SRCDIR)/discover.cpp
SENDER_SRC = $(SRCDIR)/sender.cpp
RECEIVER_SRC = $(SRCDIR)/receiver.cpp

# --- Object Files (in build directory) ---
COMMON_OBJ = $(BUILDDIR)/discover.o
SENDER_OBJ = $(BUILDDIR)/sender.o $(COMMON_OBJ)
RECEIVER_OBJ = $(BUILDDIR)/receiver.o $(COMMON_OBJ)

# --- Targets ---
TARGETS = $(BINDIR)/sender$(EXE_EXT) $(BINDIR)/receiver$(EXE_EXT)

# ============================================================================
# BUILD RULES
# ============================================================================

# Default target
all: $(TARGETS)
	@echo "✅ Build complete! Executables are in current directory."

# Sender executable
$(BINDIR)/sender$(EXE_EXT): $(SENDER_OBJ)
	@echo "🔨 Building sender for $(PLATFORM)..."
	$(CXX) $(CXXFLAGS) -o $@ $(SENDER_OBJ) $(LDFLAGS)
	@echo "✅ Sender built successfully: $@"

# Receiver executable
$(BINDIR)/receiver$(EXE_EXT): $(RECEIVER_OBJ)
	@echo "🔨 Building receiver for $(PLATFORM)..."
	$(CXX) $(CXXFLAGS) -o $@ $(RECEIVER_OBJ) $(LDFLAGS)
	@echo "✅ Receiver built successfully: $@"

# Generic rule for compiling .cpp to .o in build directory
$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp $(SRCDIR)/%.h
	@echo "⚙️  Compiling $<..."
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# Special rule for discover.cpp (no header dependency for compilation, but we'll still check)
$(BUILDDIR)/discover.o: $(SRCDIR)/discover.cpp $(SRCDIR)/discover.h
	@echo "⚙️  Compiling $<..."
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# ============================================================================
# UTILITY TARGETS
# ============================================================================

# Debug build
debug: CXXFLAGS += $(DEBUG_FLAGS)
debug: all
	@echo "🐛 Debug build complete"

# Clean build files
clean:
	@echo "🧹 Cleaning build artifacts..."
	$(RM) $(BUILDDIR)/*.o $(BINDIR)/sender$(EXE_EXT) $(BINDIR)/receiver$(EXE_EXT)
	@echo "✅ Clean complete"

# Clean everything (including build directory)
distclean: clean
	@echo "🧹 Removing build directory..."
	$(RM) -r $(BUILDDIR)
	@echo "✅ Deep clean complete"

# Run receiver
run-receiver: $(BINDIR)/receiver$(EXE_EXT)
	@echo "📺 Starting receiver..."
	cd $(BINDIR) && ./receiver$(EXE_EXT)

# Run sender
run-sender: $(BINDIR)/sender$(EXE_EXT)
	@echo "🎥 Starting sender..."
	cd $(BINDIR) && ./sender$(EXE_EXT)

# Install dependencies (Linux only)
install-deps-linux:
	@echo "📦 Installing Linux dependencies..."
	sudo apt-get update
	sudo apt-get install -y g++ make libsdl2-dev libx11-dev pkg-config

# Install dependencies (macOS with Homebrew)
install-deps-macos:
	@echo "📦 Installing macOS dependencies..."
	brew install sdl2

# Copy SDL2.dll for Windows (helpful for MinGW builds)
copy-sdl-dll:
ifeq ($(PLATFORM),Windows)
	@if exist "$(SDL_PATH)\\bin\\SDL2.dll" ( \
		echo "📋 Copying SDL2.dll from $(SDL_PATH)\\bin\\..." && \
		copy "$(SDL_PATH)\\bin\\SDL2.dll" $(BINDIR) \
	) else ( \
		echo "⚠️  SDL2.dll not found at $(SDL_PATH)\\bin\\SDL2.dll" && \
		echo "Please copy SDL2.dll manually to $(BINDIR)" \
	)
else
	@echo "⚠️  copy-sdl-dll is only needed on Windows"
endif

# Show directory structure
tree:
	@echo "📁 Project structure:"
	@echo "  ."
	@echo "  ├── Makefile"
	@echo "  ├── README.md"
	@echo "  ├── src/"
	@echo "  │   ├── sender.cpp"
	@echo "  │   ├── receiver.cpp"
	@echo "  │   ├── discover.cpp"
	@echo "  │   └── discover.h"
	@echo "  ├── build/ (created during build)"
	@echo "  ├── sender$(EXE_EXT) (after build)"
	@echo "  └── receiver$(EXE_EXT) (after build)"

# Help
help:
	@echo "============================================================================"
	@echo "SCREEN SHARE PROJECT MAKEFILE"
	@echo "============================================================================"
	@echo "Platform detected: $(PLATFORM)"
	@echo "Source directory: $(SRCDIR)"
	@echo "Build directory: $(BUILDDIR)"
	@echo ""
	@echo "Available targets:"
	@echo "  all              - Build both sender and receiver (default)"
	@echo "  sender           - Build only the sender"
	@echo "  receiver         - Build only the receiver"
	@echo "  debug            - Build with debug symbols"
	@echo "  clean            - Remove object files and executables"
	@echo "  distclean        - Deep clean (removes build directory)"
	@echo "  run-receiver     - Build and run receiver"
	@echo "  run-sender       - Build and run sender"
	@echo "  install-deps-linux  - Install Linux dependencies (requires sudo)"
	@echo "  install-deps-macos  - Install macOS dependencies (requires Homebrew)"
	@echo "  copy-sdl-dll      - Copy SDL2.dll to binary directory (Windows only)"
	@echo "  tree             - Show project directory structure"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Configuration:"
	@echo "  CXX=$(CXX)"
	@echo "  CXXFLAGS=$(CXXFLAGS)"
	@echo "  LDFLAGS=$(LDFLAGS)"
	@echo "============================================================================"

# ============================================================================
# DEPENDENCY GENERATION
# ============================================================================
# Generate dependency files automatically
$(BUILDDIR)/%.d: $(SRCDIR)/%.cpp
	@$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MM -MT $(@:.d=.o) $< > $@

# Include dependency files if they exist
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),distclean)
-include $(BUILDDIR)/*.d
endif
endif

# ============================================================================
# PLATFORM-SPECIFIC WORKAROUNDS
# ============================================================================

# Windows: Handle path separators and DLL copying
ifeq ($(PLATFORM),Windows)
# Ensure SDL2.dll is available for running
run-receiver: $(BINDIR)/receiver$(EXE_EXT) copy-sdl-dll-check
	@echo "📺 Starting receiver..."
	cd $(BINDIR) && .\receiver$(EXE_EXT)

run-sender: $(BINDIR)/sender$(EXE_EXT) copy-sdl-dll-check
	@echo "🎥 Starting sender..."
	cd $(BINDIR) && .\sender$(EXE_EXT)

copy-sdl-dll-check:
	@if not exist "$(BINDIR)\\SDL2.dll" ( \
		echo "⚠️  SDL2.dll not found in $(BINDIR)" && \
		echo "Run 'make copy-sdl-dll' to copy it" \
	)
endif

.PHONY: all clean distclean debug help run-receiver run-sender
.PHONY: install-deps-linux install-deps-macos copy-sdl-dll tree
.PHONY: copy-sdl-dll-check

# ============================================================================
# END OF MAKEFILE
# ============================================================================