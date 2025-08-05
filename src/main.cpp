#pragma once

#include <iostream>
#include <cstring>
#include <algorithm>
#include <windows.h>

// v3.2.18
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"

// v1.92.1
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_sdl3.h"
#include "imgui/imgui_impl_sdlrenderer3.h"

// v3.19.3
#include "tinyfiledialogs/tinyfiledialogs.h"


SDL_Window* window;
SDL_Renderer* renderer;
SDL_Event e;

const ImGuiWindowFlags mainWindowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar;

const int WIDTH = 720, HEIGHT = 720;
bool running = true;
char pathYTDLP[1024]{};
char pathOutput[1024]{};
bool valueMP3 = false;
bool valueMP4 = false;
std::string outputLog{};


void log(std::string msg) {
    std::cout << msg << std::endl;
}


void debug(std::string msg) {
    std::cout << "[DEBUG] " << msg << std::endl;
}


int dump() {
    /* create a pipe for stdout/stderr redirection */
    SECURITY_ATTRIBUTES secAttr{};
    HANDLE hRead = nullptr;
    HANDLE hWrite = nullptr;

    secAttr.nLength = sizeof(SECURITY_ATTRIBUTES); // set the size of the struct
    secAttr.bInheritHandle = TRUE; // allow the handle to be inherited by child processes
    secAttr.lpSecurityDescriptor = nullptr; // no custom security rules, just default permissions

    if (!CreatePipe(&hRead, &hWrite, &secAttr, 0)) { // create the actual pipe
        debug("Failed to create pipe");
        return 1;
    }

    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0); // prevent the child process from inheriting the read end of the pipe

    /* setup STARTUPINFO with new pipe */
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    char cmdLine[4096];

    si.cb = sizeof(STARTUPINFOA); // set the size of the struct
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; // specify which fields to use
    si.hStdOutput = hWrite; // redirect child output to the new pipe
    si.hStdError = hWrite; // redirect child error to the new pipe
    si.wShowWindow = SW_HIDE; // specify to hide the window

    std::string command = "C://Users//david//Downloads//yt-dlp.exe --version";
    strcpy_s(cmdLine, command.c_str());

    bool success = CreateProcessA(
        nullptr,             // lpApplicationName (null = take from cmdLine)
        cmdLine,             // full command line (must be mutable)
        nullptr,             // process security attributes (default)
        nullptr,             // thread security attributes (default)
        TRUE,                // inherit handles (required for our pipe to work!)
        CREATE_NO_WINDOW,    // don’t open a new console window
        nullptr,             // environment (null = inherit from parent)
        nullptr,             // current directory (null = inherit)
        &si,                 // startup info (controls redirection, visibility)
        &pi                  // receives process info
    );

    CloseHandle(hWrite); // only read now

    if (!success) { // make sure the process actually started
        debug("Failed to create process");
        CloseHandle(hRead);
        return 1;
    }

    /* read from the pipe while the child is running */
    char buffer[4096]; // temporary buffer that ReadFile writes into
    DWORD bytesRead; // number of bytes successfully read by ReadFile
    std::string lineBuffer; // accumulates output across reads, allowing full lines to be extracted

    while (true) { // loop until the pipe is closed (i.e., the child process exits)
        bool readSuccess = ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr); // read from the pipe (child's stdout/stderr) into the buffer

        if (!readSuccess || bytesRead == 0) { // either the read failed, or there's nothing to read currently
            DWORD exitCode = 0; // holds the child process's current exit code
            if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) { // check to see if child process has exited
                break;
            }
            else { // child is still running but hasn't output anything yet — wait briefly
                Sleep(10);
                continue;
            }
        }

        buffer[bytesRead] = '\0'; // null-terminate the buffer so it can be treated as a C string
        lineBuffer += buffer; // append the new data to the accumulated line buffer

        size_t pos = 0; // cursor position inside the line buffer
        while ((pos = lineBuffer.find('\n')) != std::string::npos) {  // search for a complete line that ends with the newline character
            outputLog.append(lineBuffer.substr(0, pos + 1)); // extract and log the full line, including the newline
            lineBuffer.erase(0, pos + 1); // remove the logged line from the buffer and repeat
        }
    }


    /* cleanup */
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, 600000); // wait 10 minutes
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return 0;
}


int init() {
    // SDL components
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL could not initialize. SDL error: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("YoinkTube", WIDTH, HEIGHT, SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
        SDL_Log("SDL could not initialize window. SDL error: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        SDL_Log("SDL could not initialize renderer. SDL error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_zero(e);


    // imgui components
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    return 0;
}


void shutdown() {
    // imgui components
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    // SDL components
    if (renderer) { SDL_DestroyRenderer(renderer); renderer = nullptr; }
    if (window) { SDL_DestroyWindow(window); window = nullptr; }
    SDL_Quit();
}


void draw() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(WIDTH, 200));
    ImGui::Begin("main", nullptr, mainWindowFlags);
    ImGui::SeparatorText("File Paths");
    ImGui::Text("YT-DLP Executable Path");
    ImGui::SameLine();
    ImGui::InputText("##pathYTDLP", pathYTDLP, IM_ARRAYSIZE(pathYTDLP));
    ImGui::SameLine();
    if (ImGui::Button("Open##ytdlp")) {
        const char* filterPatterns[1] = { "*.exe" };
        char* openFileName = tinyfd_openFileDialog("Select the YT-DLP.exe to use", nullptr, 1, filterPatterns, "Executable File (*.exe)", 1);
        if (openFileName) {
            strcpy_s(pathYTDLP, sizeof(pathYTDLP), openFileName);
            std::replace(std::begin(pathYTDLP), std::end(pathYTDLP), '\\', '/');
        }
    }
    ImGui::Text("Output Path");
    ImGui::SameLine();
    ImGui::InputText("##pathOutput", pathOutput, IM_ARRAYSIZE(pathOutput));
    ImGui::SameLine();
    if (ImGui::Button("Open##output")) {
        char* openFolderName = tinyfd_selectFolderDialog("Select a folder to output files", nullptr);
        if (openFolderName) {
            strcpy_s(pathOutput, sizeof(pathOutput), openFolderName);
            std::replace(std::begin(pathOutput), std::end(pathOutput), '\\', '/');
        }
    }
    ImGui::SeparatorText("Media Options");
    ImGui::Text("Media Type");
    ImGui::SameLine();
    ImGui::Checkbox("MP3", &valueMP3);
    ImGui::SameLine();
    ImGui::Checkbox("MP4", &valueMP4);
    ImGui::Text("MP3 Options");
    ImGui::Text("MP4 Options");
    if (ImGui::Button("Yoink")) {
        int success = dump();
        if (success == 1) {
            debug("Attempt failed");
        }
    }
    ImGui::End();

    ImGui::Begin("Log");
    ImGui::Text(outputLog.c_str());
    ImGui::End();
}


int main(int argc, char* args[]) {
    debug("YoinkTube starting");

    if (init() == 1) {
        debug("YoinkTube failed");
        shutdown();
        exit(1);
    }

    while (running) {
        // setup imgui to create a new frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // poll all events
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) {
                running = false;
                break;
            }
        }

        // clear the screen
        SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255);
        SDL_RenderClear(renderer);

        // render imgui
        draw();
        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        // render sdl and show the screen
        SDL_RenderPresent(renderer);

        // sleep briefly to reduce CPU load
        SDL_Delay(10);
    }

    shutdown();
    debug("YoinkTube terminating");
    return 0;
}