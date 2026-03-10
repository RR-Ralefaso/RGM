/**
 * APP.CPP - RGM LAUNCHER
 *
 * This is the main entry point for the RGM application.
 * It displays the RGM splash screen and provides a menu for users to
 * choose between sender and receiver modes.
 */

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <fstream>
#include <cstdlib>
#include <SDL2/SDL.h>
/* SDL2_image: required for rcorp.jpeg splash (install libsdl2-image-dev) */
#if __has_include(<SDL2/SDL_image.h>)
#  include <SDL2/SDL_image.h>
#  define HAVE_SDL_IMAGE 1
#else
#  define HAVE_SDL_IMAGE 0
#  warning "SDL2_image not found – splash will use fallback rectangle."
#  warning "Fix: sudo apt install libsdl2-image-dev  (or: make install-sdl2-image)"
#endif     /* PNG / JPEG loading */

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

// Version information
#define VERSION "2.0.0"
#define APP_NAME "RGM"

// Splash screen constants
#define SPLASH_WIDTH 500
#define SPLASH_HEIGHT 300
#define SPLASH_DISPLAY_TIME 2000 // 2 seconds

// Menu colors (ANSI)
#define COLOR_RESET "\033[0m"
#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_BLUE "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN "\033[36m"
#define COLOR_WHITE "\033[37m"
#define COLOR_BOLD "\033[1m"

/**
 * Display RGM splash screen
 */
void showSplashScreen()
{
    std::cout << COLOR_CYAN << COLOR_BOLD;
    std::cout << "========================================\n";
    std::cout << "    RGM LAUNCHER v" << VERSION << "\n";
    std::cout << "========================================\n" << COLOR_RESET;

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        std::cerr << COLOR_RED << "⚠  SDL init failed: " << SDL_GetError() << COLOR_RESET << std::endl;
        return;
    }

#if HAVE_SDL_IMAGE
    int img_flags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(img_flags) & img_flags) != img_flags)
        std::cerr << COLOR_RED << "⚠  SDL_image: " << IMG_GetError() << COLOR_RESET << std::endl;
#endif

    SDL_Window *splashWindow = SDL_CreateWindow(
        APP_NAME,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SPLASH_WIDTH, SPLASH_HEIGHT,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP);

    if (!splashWindow)
    {
#if HAVE_SDL_IMAGE
        IMG_Quit();
#endif
        SDL_Quit(); return;
    }

    SDL_Renderer *splashRenderer = SDL_CreateRenderer(splashWindow, -1, SDL_RENDERER_ACCELERATED);
    if (!splashRenderer)
    {
        SDL_DestroyWindow(splashWindow);
#if HAVE_SDL_IMAGE
        IMG_Quit();
#endif
        SDL_Quit(); return;
    }

    /* Dark background */
    SDL_SetRenderDrawColor(splashRenderer, 18, 18, 28, 255);
    SDL_RenderClear(splashRenderer);
    SDL_RenderPresent(splashRenderer);

    /* Load splash logo – rcorp.jpeg (brand logo) FIRST, RGM.png as fallback.
     * IMG_Load handles JPEG and PNG natively; no ifstream pre-check needed. */
    SDL_Surface *image = nullptr;
    const char *possiblePaths[] = {
        "../assets/icons/rcorp.jpeg",   /* running from build/        */
        "assets/icons/rcorp.jpeg",      /* running from project root  */
        "../assets/icons/RGM.png",
        "assets/icons/RGM.png",
#ifndef _WIN32
        "/usr/share/rgm/icons/rcorp.jpeg",
        "/usr/share/rgm/icons/RGM.png",
#endif
        nullptr};

#if HAVE_SDL_IMAGE
    for (int i = 0; possiblePaths[i] != nullptr && !image; i++)
    {
        image = IMG_Load(possiblePaths[i]);
        if (image)
            std::cout << COLOR_GREEN << "✓ Logo: " << possiblePaths[i]
                      << COLOR_RESET << std::endl;
    }
#endif

    if (!image)
    {
        image = SDL_CreateRGBSurface(0, SPLASH_WIDTH - 40, SPLASH_HEIGHT - 40, 32, 0, 0, 0, 0);
        if (image)
            SDL_FillRect(image, NULL, SDL_MapRGB(image->format, 46, 91, 171));
    }

    if (image)
    {
        SDL_Texture *texture = SDL_CreateTextureFromSurface(splashRenderer, image);
        if (texture)
        {
            /* Scale to fit window preserving aspect ratio */
            int tw, th;
            SDL_QueryTexture(texture, NULL, NULL, &tw, &th);
            float sx = (float)(SPLASH_WIDTH - 20) / (float)tw;
            float sy = (float)(SPLASH_HEIGHT - 20) / (float)th;
            float sc = (sx < sy) ? sx : sy; if (sc > 1.0f) sc = 1.0f;
            SDL_Rect destRect;
            destRect.w = (int)(tw * sc);
            destRect.h = (int)(th * sc);
            destRect.x = (SPLASH_WIDTH  - destRect.w) / 2;
            destRect.y = (SPLASH_HEIGHT - destRect.h) / 2;

            SDL_SetRenderDrawColor(splashRenderer, 18, 18, 28, 255);
            SDL_RenderClear(splashRenderer);
            SDL_RenderCopy(splashRenderer, texture, NULL, &destRect);
            SDL_RenderPresent(splashRenderer);

            SDL_Delay(SPLASH_DISPLAY_TIME);
            SDL_DestroyTexture(texture);
        }
        SDL_FreeSurface(image);
    }
    else
    {
        SDL_RenderPresent(splashRenderer);
        SDL_Delay(SPLASH_DISPLAY_TIME);
    }

    SDL_DestroyRenderer(splashRenderer);
    SDL_DestroyWindow(splashWindow);
#if HAVE_SDL_IMAGE
    IMG_Quit();
#endif
    SDL_Quit();
}

/**
 * Display the main menu
 */
int showMenu()
{
    int choice = -1;
    std::string input;

    while (choice < 0 || choice > 2)
    {
#ifdef _WIN32
        system("cls");
#else
        system("clear");
#endif

        std::cout << COLOR_CYAN << COLOR_BOLD;
        std::cout << "╔═══════════════════════════════════════╗\n";
        std::cout << "║          RGM LAUNCHER v" << VERSION << "          ║\n";
        std::cout << "╠═══════════════════════════════════════╣\n";
        std::cout << "║                                       ║\n";
        std::cout << "║  " << COLOR_GREEN  << "1.  SEND SCREEN            " << COLOR_CYAN << "║\n";
        std::cout << "║  " << COLOR_YELLOW << "2.  RECEIVE SCREEN         " << COLOR_CYAN << "║\n";
        std::cout << "║  " << COLOR_RED    << "0.  EXIT                   " << COLOR_CYAN << "║\n";
        std::cout << "║                                       ║\n";
        std::cout << "╚═══════════════════════════════════════╝\n" << COLOR_RESET;

        std::cout << COLOR_BOLD << "\nEnter your choice (1-3): " << COLOR_RESET;
        std::getline(std::cin, input);

        try
        {
            choice = std::stoi(input);
        }
        catch (...)
        {
            choice = -1;
        }

        if (choice < 0 || choice > 2)
        {
            std::cout << COLOR_RED << "❌ Invalid choice. Please enter 0, 1, or 2.\n"
                      << COLOR_RESET;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

    }

    return choice;
}

/**
 * Run sender
 */
void runSender()
{
    std::cout << COLOR_GREEN << "\n🎥 Starting Sender mode..." << COLOR_RESET << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));

#ifdef _WIN32
    STARTUPINFO si = {sizeof(si)};
    PROCESS_INFORMATION pi;

    if (CreateProcess("sender.exe", NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    else
    {
        std::cerr << COLOR_RED << "❌ Failed to start sender.exe" << COLOR_RESET << std::endl;
    }
#else
    pid_t pid = fork();

    if (pid == 0)
    {
        execl("./sender", "sender", (char *)NULL);
        std::cerr << COLOR_RED << "❌ Failed to start sender" << COLOR_RESET << std::endl;
        exit(1);
    }
    else if (pid > 0)
    {
        int status;
        waitpid(pid, &status, 0);
    }
    else
    {
        std::cerr << COLOR_RED << "❌ Failed to fork process" << COLOR_RESET << std::endl;
    }
#endif

    std::cout << COLOR_YELLOW << "\nSender finished. Press Enter to continue..." << COLOR_RESET;
    std::cin.get();
}

/**
 * Run receiver
 */
void runReceiver()
{
    std::cout << COLOR_YELLOW << "\n📺 Starting Receiver mode..." << COLOR_RESET << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));

#ifdef _WIN32
    STARTUPINFO si = {sizeof(si)};
    PROCESS_INFORMATION pi;

    if (CreateProcess("receiver.exe", NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    else
    {
        std::cerr << COLOR_RED << "❌ Failed to start receiver.exe" << COLOR_RESET << std::endl;
    }
#else
    pid_t pid = fork();

    if (pid == 0)
    {
        execl("./receiver", "receiver", (char *)NULL);
        std::cerr << COLOR_RED << "❌ Failed to start receiver" << COLOR_RESET << std::endl;
        exit(1);
    }
    else if (pid > 0)
    {
        int status;
        waitpid(pid, &status, 0);
    }
    else
    {
        std::cerr << COLOR_RED << "❌ Failed to fork process" << COLOR_RESET << std::endl;
    }
#endif

    std::cout << COLOR_YELLOW << "\nReceiver finished. Press Enter to continue..." << COLOR_RESET;
    std::cin.get();
}

/**
 * Check if executables exist
 */
bool checkExecutables()
{
    bool senderExists = false;
    bool receiverExists = false;

#ifdef _WIN32
    senderExists = (std::ifstream("sender.exe").good());
    receiverExists = (std::ifstream("receiver.exe").good());
#else
    senderExists = (std::ifstream("./sender").good());
    receiverExists = (std::ifstream("./receiver").good());
#endif

    if (!senderExists || !receiverExists)
    {
        std::cout << COLOR_RED << "\n⚠  Missing executables!\n"
                  << COLOR_RESET;
        if (!senderExists)
            std::cout << "   - sender" << (!senderExists ? "❌" : "✓") << std::endl;
        if (!receiverExists)
            std::cout << "   - receiver" << (!receiverExists ? "❌" : "✓") << std::endl;
        std::cout << COLOR_YELLOW << "\nPlease run 'make' first to build the applications.\n"
                  << COLOR_RESET;
        return false;
    }

    return true;
}

/**
 * Main function
 */
int main()
{
    // Show splash screen
    showSplashScreen();

    // Check for executables
    if (!checkExecutables())
    {
        std::cout << "\nPress Enter to exit...";
        std::cin.get();
        return 1;
    }

    // Main menu loop
    while (true)
    {
        int choice = showMenu();

        switch (choice)
        {
        case 1:
            runSender();
            break;
        case 2:
            runReceiver();
            break;
        case 0:
            std::cout << COLOR_GREEN << "\nThank you for using RGM!\n"
                      << COLOR_RESET;
            return 0;
        }
    }

    return 0;
}