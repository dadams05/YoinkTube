#pragma once

#include <iostream>
#include <cstring>
#include <string>
#include <thread>
#include <fstream>
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

SDL_Event e;
SDL_Window* window;
SDL_Renderer* renderer;
SDL_PropertiesID properties;
SDL_DisplayID currentDisplay;


ImFont* guiFont;
ImFont* consoleFont;
constexpr float WIDGET_ROUNDING = 2.5f;
constexpr ImU32 LIGHT_GRAY = IM_COL32(200, 200, 200, 255);
constexpr ImU32 GRAY = IM_COL32(150, 150, 150, 255);
constexpr ImU32 DARK_GRAY = IM_COL32(100, 100, 100, 255);
constexpr ImGuiWindowFlags mainWindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar;


bool running = true;

bool yoinking = false;
char ytLink[1024]{};
char pathYTDLP[1024]{};
char pathFF[1024]{};
char pathOutput[1024]{};
bool checkAudio = false;
bool checkVideo = false;
bool checkPlaylist = false;

const int BASE_WIDTH = 800, BASE_HEIGHT = 500;
int width = BASE_WIDTH, height = BASE_HEIGHT;
float scale = 1.0f;
bool logScroll = false;

std::deque<std::string> logLines;
const size_t MAX_LOG_LINES = 10000;


void log(const std::string& msg) {
    logLines.push_back("[Yoink] " + msg);
    while (logLines.size() > MAX_LOG_LINES)
        logLines.pop_front();
    logScroll = true;
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
            logScroll = true;
        }
    }


    /* cleanup */
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, 600000); // wait 10 minutes
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}


void rescale(bool recenter) {
    float currentScale = SDL_GetWindowDisplayScale(window);

    if (currentScale != scale) {
        // rescale the window
        scale = currentScale;
        width = BASE_WIDTH * scale;
        height = BASE_HEIGHT * scale;
        SDL_SetWindowSize(window, width, height);
        if (recenter) {
            SDL_Rect displayBounds;
            SDL_GetDisplayBounds(SDL_GetDisplayForWindow(window), &displayBounds);
            int newX = displayBounds.x + (displayBounds.w - (width)) / 2;
            int newY = displayBounds.y + (displayBounds.h - (height)) / 2;
            SDL_SetWindowPosition(window, newX, newY);
        }
        // rescale fonts
        ImGuiIO& io = ImGui::GetIO();
        io.FontGlobalScale = scale;
    }
}


int init() {
    // SDL components
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL could not initialize. SDL error: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("YoinkTube", BASE_WIDTH, BASE_HEIGHT, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        SDL_Log("SDL could not initialize window. SDL error: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_SetWindowResizable(window, false);

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
    io.IniFilename = NULL;
    guiFont = io.Fonts->AddFontFromFileTTF("res/Segoe-UI-Variable-Static-Text.ttf", 18.0f);
    if (guiFont == nullptr) {
        log("Could not load font for GUI");
        return 1;
    }
    consoleFont = io.Fonts->AddFontFromFileTTF("res/CascadiaMono-VariableFont_wght.ttf", 14.0f);
    if (consoleFont == nullptr) {
        log("Could not load font for subconsole");
        return 1;
    }
    io.FontDefault = guiFont;
    

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    io.Fonts->Build();
    rescale(true); // rescale the entire gui


    std::string line;
    std::ifstream s("./config.txt");
    if (s.is_open()) {
        while (std::getline(s, line)) {
            size_t foundPos;

            foundPos = line.find("[Link]");
            if (foundPos != std::string::npos) {
                std::string sub = line.substr(foundPos + strlen("[Link]"));
                if (sub.size() < sizeof(ytLink)) {
                    memcpy_s(ytLink, sizeof(ytLink), sub.c_str(), sub.size() + 1);
                }
                else {
                    std::cerr << "ytLink buffer is too small for the string." << std::endl;
                }
                continue;  // Continue to next line (optional)
            }

            foundPos = line.find("[Output]");
            if (foundPos != std::string::npos) {
                std::string sub = line.substr(foundPos + strlen("[Output]"));
                if (sub.size() < sizeof(pathOutput)) {
                    memcpy_s(pathOutput, sizeof(pathOutput), sub.c_str(), sub.size() + 1);
                }
                else {
                    std::cerr << "pathOutput buffer is too small for the string." << std::endl;
                }
                continue;
            }

            foundPos = line.find("[FFmpeg]");
            if (foundPos != std::string::npos) {
                std::string sub = line.substr(foundPos + strlen("[FFmpeg]"));
                if (sub.size() < sizeof(pathFF)) {
                    memcpy_s(pathFF, sizeof(pathFF), sub.c_str(), sub.size() + 1);
                }
                else {
                    std::cerr << "pathFF buffer is too small for the string." << std::endl;
                }
                continue;
            }

            foundPos = line.find("[YTDLP]");
            if (foundPos != std::string::npos) {
                std::string sub = line.substr(foundPos + strlen("[YTDLP]"));
                if (sub.size() < sizeof(pathYTDLP)) {
                    memcpy_s(pathYTDLP, sizeof(pathYTDLP), sub.c_str(), sub.size() + 1);
                }
                else {
                    std::cerr << "pathYTDLP buffer is too small for the string." << std::endl;
                }
                continue;
            }
        }
    }
    s.close();







    return 0;
}


void shutdown() {
    // write to file
    std::ofstream s("./config.txt");
    if (s.is_open()) {
        std::cout << "writnig" << std::endl;
        s << "[Link]" << std::string(ytLink) << "\n";
        s << "[Output]" << std::string(pathOutput) << "\n";
        s << "[FFmpeg]" << std::string(pathFF) << "\n";
        s << "[YTDLP]" << std::string(pathYTDLP) << "\n";
        s.close();
    }
    

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
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, LIGHT_GRAY);
    int textInputWidth = width / 1.3;
    ImGui::PushItemWidth(textInputWidth);
}


void popDrawInputText() {
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(1);
    ImGui::PopItemWidth();
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
    
    ImVec2 inputTextRef, buttonRefPos, buttonRefSize, remaining;
    int mainWindowHeight = height / 2.2;
    int dummyHeight = 10 * scale;
    int dummyHeight2 = 15 * scale;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(width, mainWindowHeight));
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

    ImGui::Dummy(ImVec2(0, dummyHeight));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, dummyHeight));

    ImGui::Text("YouTube Link");
    ImGui::SameLine();
    ImGui::SetCursorPosX(inputTextRef.x);
    pushDrawInputText();
    ImGui::InputText("##ytLink", ytLink, IM_ARRAYSIZE(ytLink));
    popDrawInputText();
    ImGui::SameLine();
    pushDrawButton();
    if (ImGui::Button("Clear")) {
        memset(ytLink, '\0', sizeof(ytLink));
    }
    popDrawButton();

    ImGui::Text("Options");
    ImGui::SameLine();
    ImGui::SetCursorPosX(inputTextRef.x);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, WIDGET_ROUNDING);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, IM_COL32(0, 0, 0, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, LIGHT_GRAY);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, GRAY);
    ImGui::Checkbox("Audio", &checkAudio);
    ImGui::SameLine();
    ImGui::Checkbox("Video", &checkVideo);
    ImGui::SameLine();
    ImGui::Checkbox("Playlist", &checkPlaylist);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    remaining = ImGui::GetItemRectMin();

    float windowWidth = ImGui::GetWindowSize().x;
    float windowHeight = ImGui::GetWindowSize().y;
    float buttonWidth = ImGui::CalcTextSize("Yoink").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
    ImGui::SetCursorPosY(windowHeight - ((windowHeight - remaining.y) * 0.5f));
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
    ImGui::End(); // end of main window
    
    ImGui::SetNextWindowPos(ImVec2(0, mainWindowHeight));
    ImGui::SetNextWindowSize(ImVec2(width, height - mainWindowHeight));
    ImGui::PushStyleColor(ImGuiCol_Border, 0);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, LIGHT_GRAY);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, LIGHT_GRAY);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 255));
    ImGui::Begin("Log", nullptr, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    ImGui::PushFont(consoleFont);
    for (const std::string& line : logLines) {
        ImGui::TextUnformatted(line.c_str());
    }
    if (logScroll) {
        ImGui::SetScrollHereY(1.0f);
        logScroll = false;
    }
    ImGui::PopFont();
    ImGui::PopStyleColor(5);
    ImGui::End();
}


int main(int argc, char* args[]) {
    log("YoinkTube Application Started");

    if (init() == 1) {
        log("YoinkTube failed");
        shutdown();
        exit(1);
    }

    while (running) {
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        //std::cout << w << " " << h << std::endl;
        // poll events
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) {
                running = false;
                break;
            } else if (e.type == SDL_EVENT_WINDOW_MOVED) {
                rescale(false);
            } else if (e.type == SDL_EVENT_WINDOW_RESIZED) {
                SDL_GetWindowSize(window, &width, &height);
            }
        }

        // setup imgui to create a new frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // clear the screen
        SDL_SetRenderDrawColor(renderer, 250, 200, 152, 255);
        SDL_RenderClear(renderer);

        // render content
        draw();
        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        // render sdl, show the screen, and sleep briefly to reduce CPU load
        SDL_RenderPresent(renderer);
        SDL_Delay(10);
    }

    shutdown();
    return 0;
}
