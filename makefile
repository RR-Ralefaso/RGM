# ============================================================================
# RGM PROJECT MAKEFILE  –  Screen Extender / Screen Share Application
# ============================================================================
#
# Platforms  : Linux · macOS · Windows (MinGW / MSYS2)
# C++ std    : C++17
#
# Key targets
#   make                – build all components (default)
#   make app            – build launcher only
#   make sender         – build sender only
#   make receiver       – build receiver only
#   make debug          – build with debug symbols
#   make clean          – remove build artefacts
#   make check          – verify build environment + assets
#   make install-deps   – install system dependencies (Linux / macOS)
#   make run            – run launcher
#   make run-sender     – run sender directly
#   make run-receiver   – run receiver directly
#   make run-demo       – print setup instructions
#   make help           – print this summary
#
# ============================================================================

# --- RUNTIME OPTIMIZATIONS ---
# -O3: Maximum compiler optimization loops for performance critical math/streaming.
# -march=native: Utilizes native instruction sets (AVX, SSE) on the host computer.
# -flto: Link-Time Optimization. Allows global inline optimizations across modules.
OPT_FLAGS := -O3 -march=native -flto

CXX      = g++
CC       = gcc
CXXFLAGS = -std=c++17 -Wall -Wextra -Wno-unused-parameter $(OPT_FLAGS)
CFLAGS   = $(OPT_FLAGS) -Wall -Wextra
SRCDIR   = src
BUILDDIR = build

# ============================================================================
# PLATFORM DETECTION
# ============================================================================

UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)

# --------------------------------------------------------------------------
# Linux
# --------------------------------------------------------------------------
ifeq ($(UNAME_S),Linux)
    PLATFORM := linux
    EXE      :=

    # SDL2 + SDL2_image – prefer pkg-config (gives both), then sdl2-config+bare
    ifneq ($(shell pkg-config --exists sdl2 SDL2_image 2>/dev/null && echo 1),)
        SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_image)
        SDL_LIBS   := $(shell pkg-config --libs   sdl2 SDL2_image)
    else ifneq ($(shell pkg-config --exists sdl2 2>/dev/null && echo 1),)
        SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
        SDL_LIBS   := $(shell pkg-config --libs sdl2) -lSDL2_image
    else ifneq ($(shell which sdl2-config 2>/dev/null),)
        SDL_CFLAGS := $(shell sdl2-config --cflags)
        SDL_LIBS   := $(shell sdl2-config --libs) -lSDL2_image
    else
        SDL_CFLAGS := -I/usr/include/SDL2 -D_REENTRANT
        SDL_LIBS   := -lSDL2 -lSDL2_image
    endif

    ifeq ($(shell pkg-config --exists SDL2_image 2>/dev/null || test -f /usr/include/SDL2/SDL_image.h && echo 1),)
        $(warning SDL2_image not found! Run: sudo apt install libsdl2-image-dev)
        $(warning Then re-run make.)
    endif

    CXXFLAGS += $(SDL_CFLAGS)
    SENDER_EXTRA   := -lX11
    RECEIVER_EXTRA :=
    APP_EXTRA      :=
    LDFLAGS_COMMON := $(SDL_LIBS) -lpthread $(OPT_FLAGS)
endif

# --------------------------------------------------------------------------
# macOS
# --------------------------------------------------------------------------
ifeq ($(UNAME_S),Darwin)
    PLATFORM := macos
    EXE      :=

    BREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo /usr/local)

    SDL_CFLAGS := -I$(BREW_PREFIX)/include/SDL2
    SDL_LIBS   := -L$(BREW_PREFIX)/lib -lSDL2 -lSDL2_image

    CXXFLAGS += $(SDL_CFLAGS)

    SENDER_EXTRA   := -framework CoreGraphics -framework CoreFoundation
    RECEIVER_EXTRA :=
    APP_EXTRA      :=
    LDFLAGS_COMMON := $(SDL_LIBS) -lpthread $(OPT_FLAGS)
endif

# --------------------------------------------------------------------------
# Windows (MinGW / MSYS2)
# --------------------------------------------------------------------------
ifeq ($(OS),Windows_NT)
    PLATFORM := windows
    EXE      := .exe

    SDL2_PREFIX ?= C:/msys64/mingw64

    SDL_CFLAGS := -I$(SDL2_PREFIX)/include/SDL2
    SDL_LIBS   := -L$(SDL2_PREFIX)/lib -lSDL2 -lSDL2_image

    CXXFLAGS += $(SDL_CFLAGS) -DWIN32_LEAN_AND_MEAN
    CFLAGS   += $(SDL_CFLAGS) -DWIN32_LEAN_AND_MEAN

    SENDER_EXTRA   := -lgdi32
    RECEIVER_EXTRA :=
    APP_EXTRA      :=
    LDFLAGS_COMMON := $(SDL_LIBS) -lws2_32 -liphlpapi -lpthread $(OPT_FLAGS)
endif

# ============================================================================
# BINARY NAMES & OBJECTS
# ============================================================================

SENDER_BIN   := sender$(EXE)
RECEIVER_BIN := receiver$(EXE)
APP_BIN      := app$(EXE)

OBJ_DISCOVER := $(BUILDDIR)/discover.o
OBJ_GPU      := $(BUILDDIR)/gpu_accelerate.o
OBJ_PORTS    := $(BUILDDIR)/ports.o
OBJ_APP      := $(BUILDDIR)/app.o
OBJ_SENDER   := $(BUILDDIR)/sender.o
OBJ_RECEIVER := $(BUILDDIR)/receiver.o

# ============================================================================
# PHONY TARGET DECLARATIONS
# ============================================================================

.PHONY: all clean debug sender receiver app run run-sender run-receiver run-demo check install-deps install-sdl2-image help

# Default Target
all: $(SENDER_BIN) $(RECEIVER_BIN) $(APP_BIN)
	@echo "========================================="
	@echo "Build complete!"
	@echo "  $(SENDER_BIN)   $(RECEIVER_BIN)   $(APP_BIN)"
	@echo "========================================="

# Directory Generation Rule (Safe for concurrent parallel -j compilation)
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# ============================================================================
# COMPILATION RULES (Using High-Performance Pattern Matching)
# ============================================================================

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Header Change Tracking Maps
$(OBJ_DISCOVER): $(SRCDIR)/discover.h
$(OBJ_PORTS): $(SRCDIR)/ports.h
$(OBJ_GPU): $(SRCDIR)/gpu_accelerate.h
$(OBJ_APP): $(SRCDIR)/app.cpp
$(OBJ_SENDER): $(SRCDIR)/discover.h $(SRCDIR)/gpu_accelerate.h $(SRCDIR)/ports.h
$(OBJ_RECEIVER): $(SRCDIR)/discover.h $(SRCDIR)/gpu_accelerate.h $(SRCDIR)/ports.h

# ============================================================================
# TARGET LINKING
# ============================================================================

$(SENDER_BIN): $(OBJ_SENDER) $(OBJ_DISCOVER) $(OBJ_GPU) $(OBJ_PORTS)
	$(CXX) $^ -o $@ $(LDFLAGS_COMMON) $(SENDER_EXTRA)
	@echo "Built $(SENDER_BIN)"

$(RECEIVER_BIN): $(OBJ_RECEIVER) $(OBJ_DISCOVER) $(OBJ_GPU) $(OBJ_PORTS)
	$(CXX) $^ -o $@ $(LDFLAGS_COMMON) $(RECEIVER_EXTRA)
	@echo "Built $(RECEIVER_BIN)"

$(APP_BIN): $(OBJ_APP) $(OBJ_DISCOVER)
	$(CXX) $^ -o $@ $(LDFLAGS_COMMON) $(APP_EXTRA)
	@echo "Built $(APP_BIN)"

# Aliases
sender:   $(SENDER_BIN)
receiver: $(RECEIVER_BIN)
app:      $(APP_BIN)

# ============================================================================
# DEBUG TARGET (Disables heavy optimization flags, injects debug items)
# ============================================================================

debug: OPT_FLAGS := -g -O0 -DDEBUG
debug: clean all
	@echo "Debug build complete"

# ============================================================================
# CLEANING
# ============================================================================

clean:
	rm -rf $(BUILDDIR)
	rm -f $(SENDER_BIN) $(RECEIVER_BIN) $(APP_BIN)
	rm -f sender.exe receiver.exe app.exe
	@echo "Cleaned"

# ============================================================================
# RUN HOOKS
# ============================================================================

run: $(APP_BIN)
	@echo "========================================="
	@echo "  RGM LAUNCHER"
	@echo "========================================="
	./$(APP_BIN)

run-sender: $(SENDER_BIN)
	@echo "========================================="
	@echo "  RGM SENDER  (screen extender source)"
	@echo "========================================="
	./$(SENDER_BIN)

run-receiver: $(RECEIVER_BIN)
	@echo "========================================="
	@echo "  RGM RECEIVER  (extended display)"
	@echo "========================================="
	./$(RECEIVER_BIN)

run-demo:
	@echo ""
	@echo "========================================="
	@echo "  RGM SCREEN EXTENDER - QUICK START"
	@echo "========================================="
	@echo "  Step 1  On the RECEIVER machine:"
	@echo "            make run-receiver"
	@echo ""
	@echo "  Step 2  On the SENDER machine:"
	@echo "            make run-sender"
	@echo "          Then choose:"
	@echo "            1  Extend Right  (receiver = right monitor)"
	@echo "            2  Extend Below  (receiver = bottom monitor)"
	@echo "            3  Mirror        (duplicate screen)"
	@echo ""
	@echo "  Firewall ports required:"
	@echo "    UDP 1900   SSDP discovery"
	@echo "    TCP 8081   video stream"
	@echo "    TCP 8082   compute offload (CPU)"
	@echo "    TCP 8083   port inspector"
	@echo "========================================="

# ============================================================================
# DIAGNOSTICS & SYSTEM ENVIRONMENT TARGETS
# ============================================================================

check:
	@echo "========================================="
	@echo "  BUILD ENVIRONMENT CHECK"
	@echo "========================================="
	@echo "  Platform : $(UNAME_S)"
	@echo "  Compiler : $(CXX)"
	@echo "  CXXFLAGS : $(CXXFLAGS)"
	@echo "  LDFLAGS  : $(LDFLAGS_COMMON)"
	@echo "========================================="
	@echo "  Source files:"
	@for f in app.cpp sender.cpp receiver.cpp discover.cpp discover.h \
               gpu_accelerate.c gpu_accelerate.h ports.cpp ports.h; do \
        if [ -f $(SRCDIR)/$$f ]; then \
            echo "    OK  $$f"; \
        else \
            echo "    MISSING  $$f"; \
        fi; \
    done
	@echo "========================================="
	@echo "  Assets:"
	@if [ -f assets/icons/rcorp.jpeg ]; then \
        echo "    OK  assets/icons/rcorp.jpeg  (splash logo)"; \
    else \
        echo "    MISSING  assets/icons/rcorp.jpeg  <-- splash will use fallback"; \
    fi
	@if [ -f assets/icons/RGM.png ]; then \
        echo "    OK  assets/icons/RGM.png  (fallback logo)"; \
    else \
        echo "    MISSING  assets/icons/RGM.png"; \
    fi
	@echo "========================================="
	@echo "  SDL2 / SDL2_image:"
	@if pkg-config --exists sdl2 2>/dev/null; then \
        echo "    OK  SDL2 $$(pkg-config --modversion sdl2)"; \
    else \
        echo "    NOT FOUND via pkg-config (may still link if installed)"; \
    fi
	@if pkg-config --exists SDL2_image 2>/dev/null; then \
        echo "    OK  SDL2_image $$(pkg-config --modversion SDL2_image)"; \
    else \
        echo "    NOT FOUND  SDL2_image  <-- required for rcorp.jpeg splash"; \
    fi
	@echo "========================================="

install-deps:
	@echo "Installing dependencies..."
	@if command -v apt-get >/dev/null 2>&1; then \
        sudo apt-get update && \
        sudo apt-get install -y g++ make libx11-dev libsdl2-dev libsdl2-image-dev; \
	elif command -v dnf >/dev/null 2>&1; then \
        sudo dnf install -y gcc-c++ make libX11-devel SDL2-devel SDL2_image-devel; \
	elif command -v yum >/dev/null 2>&1; then \
        sudo yum install -y gcc-c++ make libX11-devel SDL2-devel SDL2_image-devel; \
	elif command -v pacman >/dev/null 2>&1; then \
        sudo pacman -S --noconfirm gcc make libx11 sdl2 sdl2_image; \
	elif command -v brew >/dev/null 2>&1; then \
        brew install sdl2 sdl2_image; \
	else \
        echo "Unsupported package manager.  Install manually:"; \
        echo "  g++ / clang++ (C++17)"; \
        echo "  libX11-dev  (Linux screen capture)"; \
        echo "  libSDL2-dev"; \
        echo "  libSDL2_image-dev  (required for rcorp.jpeg splash)"; \
	fi
	@echo "Done"

install-sdl2-image:
	@echo "Installing SDL2_image (required for rcorp.jpeg splash)..."
	@if command -v apt-get >/dev/null 2>&1; then \
        sudo apt-get install -y libsdl2-image-dev; \
	elif command -v dnf >/dev/null 2>&1; then \
        sudo dnf install -y SDL2_image-devel; \
	elif command -v yum >/dev/null 2>&1; then \
        sudo yum install -y SDL2_image-devel; \
	elif command -v pacman >/dev/null 2>&1; then \
        sudo pacman -S --noconfirm sdl2_image; \
	elif command -v brew >/dev/null 2>&1; then \
        brew install sdl2_image; \
	else \
        echo "Install SDL2_image manually for your system."; \
	fi
	@echo "Done. Re-run: make"

help:
	@echo ""
	@echo "╔═══════════════════════════════════════════════╗"
	@echo "║        RGM (Ralefaso GlassMirror)             ║"
	@echo "╚═══════════════════════════════════════════════╝"
	@echo ""
	@echo "BUILD:"
	@echo "  make              Build all (sender, receiver, app) dynamically optimized"
	@echo "  make sender       Build sender only"
	@echo "  make receiver     Build receiver only"
	@echo "  make app          Build launcher only"
	@echo "  make debug        Build with -g -O0 -DDEBUG"
	@echo "  make clean        Remove all build output"
	@echo ""
	@echo "RUN:"
	@echo "  make run          Launch the menu-driven launcher"
	@echo "  make run-sender   Run sender directly"
	@echo "  make run-receiver Run receiver directly"
	@echo "  make run-demo     Print quick-start instructions"
	@echo ""
	@echo "SETUP:"
	@echo "  make check        Verify environment, sources, assets"
	@echo "  make install-deps       Install all system dependencies"
	@echo "  make install-sdl2-image Install SDL2_image only (quick fix)"
	@echo "  make help         Show this message"
	@echo ""
	@echo "DISPLAY MODES (chosen at runtime by sender):"
	@echo "  1  Extend Right   receiver appears as right monitor"
	@echo "  2  Extend Below   receiver appears as bottom monitor"
	@echo "  3  Mirror         duplicate sender screen"
	@echo ""