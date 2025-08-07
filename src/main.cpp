#pragma once

#include <iostream>
#include <cstring>
#include <thread>
#include <deque>
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

ImFont* guiFont;
ImFont* consoleFont;
constexpr float WIDGET_ROUNDING = 2.5f;
constexpr ImU32 LIGHT_GRAY = IM_COL32(200, 200, 200, 255);
constexpr ImU32 GRAY = IM_COL32(150, 150, 150, 255);
constexpr ImU32 DARK_GRAY = IM_COL32(100, 100, 100, 255);
constexpr ImGuiWindowFlags mainWindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar;

const int WIDTH = 720, HEIGHT = 720;
bool running = true;
bool autoscroll = false;
bool yoinking = false;
char ytLink[2048] = "https://www.youtube.com/watch?v=HpyZEzrDf4c";
char pathYTDLP[1024] = "C:/Users/david/Downloads/yt-dlp.exe";
char pathFF[1024] = "C:/Users/david/Downloads";
char pathOutput[1024] = "C:/Users/david/Downloads";
bool checkAudio = false;
bool checkVideo = false;
bool checkPlaylist = false;
std::deque<std::string> logLines;
const size_t MAX_LOG_LINES = 10000; // adjust this based on memory budget


void log(const std::string& msg) {
    logLines.push_back("[Yoink] " + msg);
    if (logLines.size() > MAX_LOG_LINES)
        logLines.pop_front();
    autoscroll = true;
}


int checkFields() {
    short allow = 0;
    if (ytLink[0] == '\0') {
        log("ERROR: No YouTube link provided");
        allow = 1;
    }
    if (pathYTDLP[0] == '\0') {
        log("ERROR: YT-DLP path is empty");
        allow = 1;
    }
    if (pathFF[0] == '\0') {
        log("ERROR: FFmpeg/ffprobe path is empty");
        allow = 1;
    }
    if (pathOutput[0] == '\0') {
        log("ERROR: Output path is empty");
        allow = 1;
    }
    if (!checkAudio && !checkVideo) {
        log("ERROR: At least one Media Type needs to be selected");
        allow = 1;
    }
    return allow;
}


void yoink(std::string command) {
    logLines.clear();
    /* create a pipe for stdout/stderr redirection */
    SECURITY_ATTRIBUTES secAttr{};
    HANDLE hRead = nullptr;
    HANDLE hWrite = nullptr;

    secAttr.nLength = sizeof(SECURITY_ATTRIBUTES); // set the size of the struct
    secAttr.bInheritHandle = TRUE; // allow the handle to be inherited by child processes
    secAttr.lpSecurityDescriptor = nullptr; // no custom security rules, just default permissions

    if (!CreatePipe(&hRead, &hWrite, &secAttr, 0)) { // create the actual pipe
        log("Failed to create pipe");
        return;
    }

    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0); // prevent the child process from inheriting the read end of the pipe

    /* setup STARTUPINFO with new pipe */
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    char cmdLine[8192];
    strcpy_s(cmdLine, command.c_str());

    si.cb = sizeof(STARTUPINFOA); // set the size of the struct
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; // specify which fields to use
    si.hStdOutput = hWrite; // redirect child output to the new pipe
    si.hStdError = hWrite; // redirect child error to the new pipe
    si.wShowWindow = SW_HIDE; // specify to hide the window

    std::string workingDir = "C:\\";
    bool success = CreateProcessA(
        nullptr,             // lpApplicationName (null = take from cmdLine)
        cmdLine,             // full command line (must be mutable)
        nullptr,             // process security attributes (default)
        nullptr,             // thread security attributes (default)
        TRUE,                // inherit handles (required for our pipe to work!)
        CREATE_NO_WINDOW,    // don’t open a new console window
        nullptr,             // environment (null = inherit from parent)
        workingDir.c_str(),             // current directory (null = inherit)
        &si,                 // startup info (controls redirection, visibility)
        &pi                  // receives process info
    );

    CloseHandle(hWrite); // only read now

    if (!success) { // make sure the process actually started
        log("Failed to create process");
        CloseHandle(hRead);
        return;
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
        while ((pos = lineBuffer.find_first_of("\r\n")) != std::string::npos) {
            logLines.push_back(lineBuffer.substr(0, pos + 1));
            lineBuffer.erase(0, pos + 1);
            autoscroll = true;
        }
    }


    /* cleanup */
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, 600000); // wait 10 minutes
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
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
    ImGui::StyleColorsLight();

    ImGuiIO& io = ImGui::GetIO(); (void)io;
    guiFont = io.Fonts->AddFontFromFileTTF("res/Segoe-UI-Variable-Static-Text.ttf", 26.0f);
    if (guiFont == nullptr) {
        log("Could not load font for GUI");
        return 1;
    }
    consoleFont = io.Fonts->AddFontFromFileTTF("res/CascadiaMono-VariableFont_wght.ttf", 18.0f);
    if (consoleFont == nullptr) {
        log("Could not load font for subconsole");
        return 1;
    }

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


void pushDrawInputText() {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, WIDGET_ROUNDING);
}


void popDrawInputText() {
    ImGui::PopStyleVar(2);
}


void pushDrawButton() {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, WIDGET_ROUNDING);
    ImGui::PushStyleColor(ImGuiCol_Button, LIGHT_GRAY);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GRAY);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, DARK_GRAY);
}


void popDrawButton() {
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(1);
}


void draw() {
    int windowHeight = 318;
    ImVec2 inputTextRef, buttonRefPos, buttonRefSize;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(WIDTH, windowHeight));
    ImGui::Begin("main", nullptr, mainWindowFlags);

    ImGui::Text("YT-DLP Executable");
    ImGui::SameLine();
    pushDrawInputText();
    ImGui::InputText("##pathYTDLP", pathYTDLP, IM_ARRAYSIZE(pathYTDLP));
    popDrawInputText();
    inputTextRef = ImGui::GetItemRectMin(); // to line up other input text widgets
    ImGui::SameLine();
    pushDrawButton();
    if (ImGui::Button("Open##ytdlp")) {
        const char* filterPatterns[1] = { "*.exe" };
        char* openFileName = tinyfd_openFileDialog("Select the yt-dlp.exe to use", nullptr, 1, filterPatterns, "Executable File (*.exe)", 1);
        if (openFileName) {
            strcpy_s(pathYTDLP, sizeof(pathYTDLP), openFileName);
            std::replace(std::begin(pathYTDLP), std::end(pathYTDLP), '\\', '/');
        }
    }
    popDrawButton();
    buttonRefPos = ImGui::GetItemRectMin();
    buttonRefSize = ImGui::GetItemRectSize();

    ImGui::Text("FFmpeg/ffprobe");
    ImGui::SameLine();
    ImGui::SetCursorPosX(inputTextRef.x);
    pushDrawInputText();
    ImGui::InputText("##pathFF", pathFF, IM_ARRAYSIZE(pathFF));
    popDrawInputText();
    ImGui::SameLine();
    pushDrawButton();
    if (ImGui::Button("Open##ff")) {
        char* openFolderName = tinyfd_selectFolderDialog("Select the folder with FFmpeg and ffprobe", nullptr);
        if (openFolderName) {
            strcpy_s(pathFF, sizeof(pathFF), openFolderName);
            std::replace(std::begin(pathFF), std::end(pathFF), '\\', '/');
        }
    }
    popDrawButton();

    ImGui::Text("Output");
    ImGui::SameLine();
    ImGui::SetCursorPosX(inputTextRef.x);
    pushDrawInputText();
    ImGui::InputText("##pathOutput", pathOutput, IM_ARRAYSIZE(pathOutput));
    popDrawInputText();
    ImGui::SameLine();
    pushDrawButton();
    if (ImGui::Button("Open##output")) {
        char* openFolderName = tinyfd_selectFolderDialog("Select a folder to output files", nullptr);
        if (openFolderName) {
            strcpy_s(pathOutput, sizeof(pathOutput), openFolderName);
            std::replace(std::begin(pathOutput), std::end(pathOutput), '\\', '/');
        }
    }
    popDrawButton();

    ImGui::Dummy(ImVec2(0, 10));
    //ImGui::SeparatorText("Options");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 10));

    ImGui::Text("Options");
    ImGui::SameLine();
    ImGui::SetCursorPosX(inputTextRef.x);
    ImGui::Checkbox("Audio", &checkAudio);
    ImGui::SameLine();
    ImGui::Checkbox("Video", &checkVideo);
    ImGui::SameLine();
    ImGui::Checkbox("Playlist", &checkPlaylist);

    ImGui::Text("YouTube Link");
    ImGui::SameLine();
    ImGui::SetCursorPosX(inputTextRef.x);
    ImGui::SetNextItemWidth(buttonRefPos.x + buttonRefSize.x - inputTextRef.x);
    pushDrawInputText();
    ImGui::InputText("##ytLink", ytLink, IM_ARRAYSIZE(ytLink));
    popDrawInputText();
    ImGui::Dummy(ImVec2(0, 25));

    float windowWidth = ImGui::GetWindowSize().x;
    float buttonWidth = ImGui::CalcTextSize("Yoink").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
    if (yoinking) ImGui::BeginDisabled();
    pushDrawButton();
    if (ImGui::Button("Yoink") && !yoinking) {
        std::thread([] {
            if (checkFields() != 1) {
                yoinking = true;
                log("Yoink started");
                std::string command;

                if (checkAudio && checkVideo) {
                    command = "\"" + std::string(pathYTDLP) + "\"";
                    command += " --no-progress";
                    command += " --ffmpeg-location \"" + std::string(pathFF) + "\"";
                    command += " --path \"" + std::string(pathOutput) + "\"";
                    command += " --output \"%(title)s.%(ext)s\"";
                    command += " --format \"bestaudio[ext=m4a]/bestaudio/best\" --extract-audio --audio-format mp3 --audio-quality 0";
                    command += " " + std::string(ytLink);
                    yoink(command);
                    
                    command = "\"" + std::string(pathYTDLP) + "\"";
                    command += " --no-progress";
                    command += " --ffmpeg-location \"" + std::string(pathFF) + "\"";
                    command += " --path \"" + std::string(pathOutput) + "\"";
                    command += " --output \"%(title)s.%(ext)s\"";
                    command += " --format \"bv*+ba/b\"";
                    command += " " + std::string(ytLink);
                    yoink(command);
                } else if (checkAudio && !checkVideo) {
                    command = "\"" + std::string(pathYTDLP) + "\"";
                    command += " --no-progress";
                    command += " --ffmpeg-location \"" + std::string(pathFF) + "\"";
                    command += " --path \"" + std::string(pathOutput) + "\"";
                    command += " --output \"%(title)s.%(ext)s\"";
                    command += " --format \"bestaudio[ext=m4a]/bestaudio/best\" --extract-audio --audio-format mp3 --audio-quality 0";
                    command += " " + std::string(ytLink);
                    yoink(command);
                } else if (checkVideo && !checkAudio) {
                    command = "\"" + std::string(pathYTDLP) + "\"";
                    command += " --no-progress";
                    command += " --ffmpeg-location \"" + std::string(pathFF) + "\"";
                    command += " --path \"" + std::string(pathOutput) + "\"";
                    command += " --output \"%(title)s.%(ext)s\"";
                    command += " --format \"bv*+ba/b\"";
                    command += " " + std::string(ytLink);
                    yoink(command);
                }

                yoinking = false;
                log("Yoink complete");
            }
        }).detach(); // run in background
    }
    popDrawButton();
    if (yoinking) ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, 25));
    ImGui::End(); // end of main window
    
    ImGui::SetNextWindowPos(ImVec2(0, windowHeight));
    ImGui::SetNextWindowSize(ImVec2(WIDTH, HEIGHT - windowHeight));
    ImGui::Begin("Log", nullptr, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
    ImGui::PushFont(consoleFont);
    for (const std::string& line : logLines) {
        ImGui::TextUnformatted(line.c_str());
    }
    if (autoscroll) {
        ImGui::SetScrollHereY(1.0f);
        autoscroll = false;
    }
    ImGui::PopFont();
    ImGui::End();
}


int main(int argc, char* args[]) {
    log("YoinkTube started");

    if (init() == 1) {
        log("YoinkTube failed");
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
    return 0;
}