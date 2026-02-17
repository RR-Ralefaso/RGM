# --- OS Detection ---
ifeq ($(OS),Windows_NT)
    PLATFORM := Windows
    # Adjust these paths to where your SDL2 is located
    SDL_INC := -I"C:/SDL2/include"
    SDL_LIB := -L"C:/SDL2/lib/x64" -lSDL2 -lSDL2main -lws2_32 -lshell32
    EXE_EXT := .exe
    RM := del /Q
else
    UNAME_S := $(shell uname -s)
    EXE_EXT := 
    RM := rm -f
    ifeq ($(UNAME_S),Linux)
        PLATFORM := Linux
        SDL_INC := $(shell sdl2-config --cflags)
        SDL_LIB := $(shell sdl2-config --libs) -lX11
    endif
    ifeq ($(UNAME_S),Darwin)
        PLATFORM := macOS
        SDL_INC := $(shell sdl2-config --cflags)
        SDL_LIB := $(shell sdl2-config --libs) -framework ApplicationServices -framework Cocoa
    endif
endif

# --- Project Config ---
CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2 $(SDL_INC)
LIBS = $(SDL_LIB)
SRCDIR = src

TARGETS = sender$(EXE_EXT) receiver$(EXE_EXT)

all: $(TARGETS)

# Sender
sender$(EXE_EXT): $(SRCDIR)/sender.cpp $(SRCDIR)/discover.cpp $(SRCDIR)/discover.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRCDIR)/sender.cpp $(SRCDIR)/discover.cpp $(LIBS)

# Receiver
receiver$(EXE_EXT): $(SRCDIR)/receiver.cpp $(SRCDIR)/discover.cpp $(SRCDIR)/discover.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRCDIR)/receiver.cpp $(SRCDIR)/discover.cpp $(LIBS)

clean:
	$(RM) $(TARGETS)

.PHONY: all clean