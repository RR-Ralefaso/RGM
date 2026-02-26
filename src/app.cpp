/**
 * APP.CPP - RGM SCREEN SHARE LAUNCHER
 *
 * This is the main entry point for the RGM Screen Share application.
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

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

// Version information
#define VERSION "2.0.0"
#define APP_NAME "RGM Screen Share"

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
    std::cout << "    RGM SCREEN SHARE LAUNCHER v" << VERSION << "\n";
    std::cout << "========================================\n"
              << COLOR_RESET;

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        std::cerr << COLOR_RED << "⚠️  Could not initialize SDL: " << SDL_GetError() << COLOR_RESET << std::endl;
        return;
    }

    // Create splash window
    SDL_Window *splashWindow = SDL_CreateWindow(
        APP_NAME,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SPLASH_WIDTH,
        SPLASH_HEIGHT,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP);

    if (!splashWindow)
    {
        std::cerr << COLOR_RED << "⚠️  Could not create splash window: " << SDL_GetError() << COLOR_RESET << std::endl;
        SDL_Quit();
        return;
    }

    SDL_Renderer *splashRenderer = SDL_CreateRenderer(splashWindow, -1,
                                                      SDL_RENDERER_ACCELERATED);

    if (!splashRenderer)
    {
        std::cerr << COLOR_RED << "⚠️  Could not create splash renderer: " << SDL_GetError() << COLOR_RESET << std::endl;
        SDL_DestroyWindow(splashWindow);
        SDL_Quit();
        return;
    }

    // Set background color
    SDL_SetRenderDrawColor(splashRenderer, 70, 130, 180, 255);
    SDL_RenderClear(splashRenderer);

    // Try to load RGM.png
    SDL_Surface *image = nullptr;
    const char *possiblePaths[] = {
        "assets/icons/RGM.png",
        "../assets/icons/RGM.png",
        "./assets/icons/RGM.png",
        "/usr/share/rgm/icons/RGM.png",
        "RGM.png",
        nullptr};

    for (int i = 0; possiblePaths[i] != nullptr && !image; i++)
    {
        std::ifstream file(possiblePaths[i]);
        if (file.good())
        {
            file.close();
            image = SDL_LoadBMP(possiblePaths[i]);
            if (image)
            {
                std::cout << COLOR_GREEN << "✅ Loaded logo from: " << possiblePaths[i] << COLOR_RESET << std::endl;
            }
        }
    }

    if (!image)
    {
        // Create a simple colored surface
        image = SDL_CreateRGBSurface(0, SPLASH_WIDTH - 40, SPLASH_HEIGHT - 40, 32, 0, 0, 0, 0);
        if (image)
        {
            SDL_FillRect(image, NULL, SDL_MapRGB(image->format, 100, 149, 237));
        }
    }

    if (image)
    {
        SDL_Texture *texture = SDL_CreateTextureFromSurface(splashRenderer, image);
        if (texture)
        {
            SDL_Rect destRect;
            destRect.w = image->w;
            destRect.h = image->h;
            destRect.x = (SPLASH_WIDTH - destRect.w) / 2;
            destRect.y = (SPLASH_HEIGHT - destRect.h) / 2;

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
        std::cout << "║        RGM SCREEN SHARE v" << VERSION << "         ║\n";
        std::cout << "╠═══════════════════════════════════════╣\n";
        std::cout << "║                                       ║\n";
        std::cout << "║  " << COLOR_GREEN << "1. 🎥 SEND SCREEN" << COLOR_CYAN << "                 ║\n";
        std::cout << "║  " << COLOR_YELLOW << "2. 📺 RECEIVE SCREEN" << COLOR_CYAN << "              ║\n";
        std::cout << "║  " << COLOR_RED << "3. ❌ EXIT" << COLOR_CYAN << "                        ║\n";
        std::cout << "║                                       ║\n";
        std::cout << "╚═══════════════════════════════════════╝\n"
                  << COLOR_RESET;

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

        if (choice < 1 || choice > 3)
        {
            std::cout << COLOR_RED << "❌ Invalid choice. Please enter 1, 2, or 3.\n"
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
        std::cout << COLOR_RED << "\n⚠️  Missing executables!\n"
                  << COLOR_RESET;
        if (!senderExists)
            std::cout << "   - sender" << (!senderExists ? "❌" : "✅") << std::endl;
        if (!receiverExists)
            std::cout << "   - receiver" << (!receiverExists ? "❌" : "✅") << std::endl;
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
        case 3:
            std::cout << COLOR_GREEN << "\n👋 Thank you for using RGM Screen Share!\n"
                      << COLOR_RESET;
            return 0;
        }
    }

    return 0;
}