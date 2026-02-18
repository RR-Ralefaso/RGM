# ============================================================================
# SCREEN SHARE PROJECT MAKEFILE
# ============================================================================

CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -I src
LDFLAGS = 
SRCDIR = src
BUILDDIR = build

# Platform detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    # Linux: Add X11 and pthread
    LDFLAGS += -lX11 -lpthread
    # SDL2 flags
    SDL_FLAGS = $(shell pkg-config --cflags --libs sdl2)
endif
ifeq ($(UNAME_S),Darwin)
    # macOS
    LDFLAGS += -framework Cocoa -lpthread
    SDL_FLAGS = $(shell sdl2-config --cflags --libs)
endif

# Create build directory
$(shell mkdir -p $(BUILDDIR))

all: sender receiver
	@echo "✅ Build complete! Executables are in current directory:"
	@ls -la sender receiver 2>/dev/null || echo "⚠️  Check for errors above"

# Sender (doesn't need SDL2)
sender: $(BUILDDIR)/sender.o $(BUILDDIR)/discover.o
	$(CXX) -o $@ $^ $(LDFLAGS)
	@echo "✅ Created sender"

# Receiver (needs SDL2)
receiver: $(BUILDDIR)/receiver.o $(BUILDDIR)/discover.o
	$(CXX) -o $@ $^ $(LDFLAGS) $(SDL_FLAGS)
	@echo "✅ Created receiver"

# Compile rules
$(BUILDDIR)/sender.o: $(SRCDIR)/sender.cpp $(SRCDIR)/discover.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/receiver.o: $(SRCDIR)/receiver.cpp $(SRCDIR)/discover.h
	$(CXX) $(CXXFLAGS) $(SDL_FLAGS) -c $< -o $@

$(BUILDDIR)/discover.o: $(SRCDIR)/discover.cpp $(SRCDIR)/discover.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(BUILDDIR)/*.o sender receiver
	@echo "✅ Cleaned"

# Run targets
run-receiver: receiver
	./receiver

run-sender: sender
	./sender

.PHONY: all clean run-receiver run-sender