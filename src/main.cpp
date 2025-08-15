#pragma once

#include <iostream>
#include <cstring>
#include <string>
#include <thread>
#include <fstream>
#include <deque>
#include <algorithm>
#include <windows.h>

// v3.19.3
#include "tinyfiledialogs/tinyfiledialogs.h"

// v3.2.18
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"

SDL_Event e;
SDL_Window* window;
SDL_Renderer* renderer;
SDL_PropertiesID properties;
SDL_DisplayID currentDisplay;

// v1.92.1
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_sdl3.h"
#include "imgui/imgui_impl_sdlrenderer3.h"

ImFont* guiFont;
ImFont* consoleFont;
constexpr float WIDGET_ROUNDING = 2.5f;
constexpr ImU32 LIGHT_GRAY = IM_COL32(200, 200, 200, 255);
constexpr ImU32 GRAY = IM_COL32(150, 150, 150, 255);
constexpr ImU32 DARK_GRAY = IM_COL32(100, 100, 100, 255);
constexpr ImGuiWindowFlags mainWindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar;

bool running = true;
bool yoinking = false;
bool checkAudio = false;
bool checkVideo = false;
bool checkPlaylist = false;
char ytLink[2048]{};
char pathYTDLP[2048]{};
char pathFF[2048]{};
char pathOutput[2048]{};

const int BASE_WIDTH = 800, BASE_HEIGHT = 500;
const int BASE_DUMMY_HEIGHT_1 = 10, BASE_DUMMY_HEIGHT_2 = 15;
int width = BASE_WIDTH, height = BASE_HEIGHT;
float scale = 1.0f;
int mainWindowHeight = height / 2.2f;
int dummyHeight = BASE_DUMMY_HEIGHT_1 * scale;
int dummyHeight2 = BASE_DUMMY_HEIGHT_2 * scale;

std::deque<std::string> logLines;
const size_t MAX_LOG_LINES = 10000;
bool scroll = false;


/* application functions */

void log(const std::string& msg) {
    logLines.push_back("[Yoink] " + msg);
    std::cout << "[Yoink] " << msg << std::endl;
    while (logLines.size() > MAX_LOG_LINES)
        logLines.pop_front();
    scroll = true;
}

bool checkFields() {
    bool allow = true;
    if (ytLink[0] == '\0') {
        log("ERROR: No YouTube link provided");
        allow = false;
    }
    if (pathYTDLP[0] == '\0') {
        log("ERROR: YT-DLP path is empty");
        allow = false;
    }
    if (pathFF[0] == '\0') {
        log("ERROR: FFmpeg/FFprobe path is empty");
        allow = false;
    }
    if (pathOutput[0] == '\0') {
        log("ERROR: Output path is empty");
        allow = false;
    }
    if (!checkAudio && !checkVideo) {
        log("ERROR: At least one media type (MP3/MP4) needs to be selected");
        allow = false;
    }
    return allow;
}

std::string createAudioCommand() {
    std::string command = "\"" + std::string(pathYTDLP) + "\"";
    command += " --no-progress";
    command += " --ffmpeg-location \"" + std::string(pathFF) + "\"";
    command += " --path \"" + std::string(pathOutput) + "\"";
    command += " --output \"%(title)s.%(ext)s\"";
    command += " --format \"bestaudio[ext=m4a]/bestaudio/best\" --extract-audio --audio-format mp3 --audio-quality 0";
    command += " " + std::string(ytLink);
    return command;
}

std::string createVideoCommand() {
    std::string command = "\"" + std::string(pathYTDLP) + "\"";
    command += " --no-progress";
    command += " --ffmpeg-location \"" + std::string(pathFF) + "\"";
    command += " --path \"" + std::string(pathOutput) + "\"";
    command += " --output \"%(title)s.%(ext)s\"";
    command += " --format \"bv*+ba/b\"";
    command += " " + std::string(ytLink);
    return command;
}

void yoink(std::string command) {
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
        nullptr,             // current directory (null = inherit)
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
            } else { // child is still running but hasn't output anything yet — wait briefly
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
            scroll = true;
        }
    }

    /* cleanup */
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, 600000); // wait 10 minutes
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}


/* draw functions */

void setupInputText(bool setup = true) {
    if (setup) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, WIDGET_ROUNDING);
        ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, LIGHT_GRAY);
        float textInputWidth = width / 1.3f;
        ImGui::PushItemWidth(textInputWidth);
    } else {
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(1);
        ImGui::PopItemWidth();
    }
}

void setupButton(bool setup = true) {
    if (setup) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, WIDGET_ROUNDING);
        ImGui::PushStyleColor(ImGuiCol_Button, LIGHT_GRAY);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GRAY);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, DARK_GRAY);
    } else {
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(1);
    }
}

void rescale(bool recenter) {
    float currentScale = SDL_GetWindowDisplayScale(window);

    if (currentScale != scale) {
        // rescale the window
        scale = currentScale;
        width = int(BASE_WIDTH * scale);
        height = int(BASE_HEIGHT * scale);
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
        // other components
        mainWindowHeight = height / 2.2f;
        dummyHeight = BASE_DUMMY_HEIGHT_1 * scale;
        dummyHeight2 = BASE_DUMMY_HEIGHT_2 * scale;
    }
}

void draw() {
    ImVec2 inputTextRefPos, buttonRefPos, buttonRefSize, remainingSpace;
    
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(float(width), mainWindowHeight));
    ImGui::Begin("main", nullptr, mainWindowFlags);
    // input text for the path to the yt-dlp.exe
    ImGui::Text("YT-DLP Executable");
    ImGui::SameLine();
    setupInputText();
    ImGui::InputText("##pathYTDLP", pathYTDLP, IM_ARRAYSIZE(pathYTDLP));
    setupInputText(false);
    inputTextRefPos = ImGui::GetItemRectMin(); // to line up other input text widgets
    ImGui::SameLine();
    setupButton();
    if (ImGui::Button("Open##ytdlp")) {
        const char* filterPatterns[1] = { "*.exe" };
        char* openFileName = tinyfd_openFileDialog("Select the yt-dlp.exe to use", nullptr, 1, filterPatterns, "Executable File (*.exe)", 1);
        if (openFileName) {
            strcpy_s(pathYTDLP, sizeof(pathYTDLP), openFileName);
            std::replace(std::begin(pathYTDLP), std::end(pathYTDLP), '\\', '/');
        }
    }
    setupButton(false);
    buttonRefPos = ImGui::GetItemRectMin();
    buttonRefSize = ImGui::GetItemRectSize();
    // input text for the path to the directory with ffmpeg.exe and ffprobe.exe
    ImGui::Text("FFmpeg/FFprobe");
    ImGui::SameLine();
    ImGui::SetCursorPosX(inputTextRefPos.x);
    setupInputText();
    ImGui::InputText("##pathFF", pathFF, IM_ARRAYSIZE(pathFF));
    setupInputText(false);
    ImGui::SameLine();
    setupButton();
    if (ImGui::Button("Open##ff")) {
        char* openFolderName = tinyfd_selectFolderDialog("Select the folder with FFmpeg and ffprobe", nullptr);
        if (openFolderName) {
            strcpy_s(pathFF, sizeof(pathFF), openFolderName);
            std::replace(std::begin(pathFF), std::end(pathFF), '\\', '/');
        }
    }
    setupButton(false);
    // input text for the output folder for the user
    ImGui::Text("Output");
    ImGui::SameLine();
    ImGui::SetCursorPosX(inputTextRefPos.x);
    setupInputText();
    ImGui::InputText("##pathOutput", pathOutput, IM_ARRAYSIZE(pathOutput));
    setupInputText(false);
    ImGui::SameLine();
    setupButton();
    if (ImGui::Button("Open##output")) {
        char* openFolderName = tinyfd_selectFolderDialog("Select a folder to output files", nullptr);
        if (openFolderName) {
            strcpy_s(pathOutput, sizeof(pathOutput), openFolderName);
            std::replace(std::begin(pathOutput), std::end(pathOutput), '\\', '/');
        }
    }
    setupButton(false);
    // padding
    ImGui::Dummy(ImVec2(0, dummyHeight));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, dummyHeight));
    // input text to the youtube link to download
    ImGui::Text("YouTube Link");
    ImGui::SameLine();
    ImGui::SetCursorPosX(inputTextRefPos.x);
    setupInputText();
    ImGui::InputText("##ytLink", ytLink, IM_ARRAYSIZE(ytLink));
    setupInputText(false);
    ImGui::SameLine();
    setupButton();
    if (ImGui::Button("Clear")) {
        memset(ytLink, '\0', sizeof(ytLink));
    }
    setupButton(false);
    // options for the user to choose: mp3, mp4, and playlist download
    ImGui::Text("Options");
    ImGui::SameLine();
    ImGui::SetCursorPosX(inputTextRefPos.x);
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
    remainingSpace = ImGui::GetItemRectMin();
    // yoink button to start the process and download
    float windowWidth = ImGui::GetWindowSize().x;
    float windowHeight = ImGui::GetWindowSize().y;
    float buttonWidth = ImGui::CalcTextSize("Yoink").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
    ImGui::SetCursorPosY(windowHeight - ((windowHeight - remainingSpace.y) * 0.5f));
    if (yoinking) ImGui::BeginDisabled();
    setupButton();
    if (ImGui::Button("Yoink") && !yoinking) {
        std::thread([] {
            if (checkFields()) {
                yoinking = true;
                log("Yoink started");
                std::string command;
                logLines.clear();

                if (checkAudio && checkVideo) {
                    yoink(createAudioCommand());
                    yoink(createVideoCommand());
                } else if (checkAudio && !checkVideo) {
                    yoink(createAudioCommand());
                } else if (checkVideo && !checkAudio) {
                    yoink(createVideoCommand());
                }

                yoinking = false;
                log("Yoink complete");
            }
        }).detach(); // run in background
    }
    setupButton(false);
    if (yoinking) ImGui::EndDisabled();
    ImGui::End(); // end of main window
    // window for custom subconsole
    ImGui::SetNextWindowPos(ImVec2(0, mainWindowHeight));
    ImGui::SetNextWindowSize(ImVec2(float(width), height - mainWindowHeight));
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
    if (scroll) {
        ImGui::SetScrollHereY(1.0f);
        scroll = false;
    }
    ImGui::PopFont();
    ImGui::PopStyleColor(5);
    ImGui::End();
}


/* control functions */

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

bool init() {
    // SDL components
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL could not initialize. SDL error: %s\n", SDL_GetError());
        return false;
    }

    window = SDL_CreateWindow("YoinkTube", BASE_WIDTH, BASE_HEIGHT, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        SDL_Log("SDL could not initialize window. SDL error: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_SetWindowResizable(window, false);

    renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        SDL_Log("SDL could not initialize renderer. SDL error: %s\n", SDL_GetError());
        return false;
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
        log("ImGui could not load font for GUI");
        return false;
    }
    consoleFont = io.Fonts->AddFontFromFileTTF("res/CascadiaMono-VariableFont_wght.ttf", 14.0f);
    if (consoleFont == nullptr) {
        log("ImGui could not load font for subconsole");
        return false;
    }
    io.FontDefault = guiFont;

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // load user data
    std::ifstream configFile("./config.txt");
    if (configFile.is_open()) {
        std::string line;
        long foundPos;
        while (std::getline(configFile, line)) {
            foundPos = line.find("]");
            if (foundPos != std::string::npos) {
                std::string key = line.substr(1, foundPos - 1);
                std::string value = line.substr(foundPos + 1, sizeof(line) - foundPos);
                if (key == "FFmpeg") {
                    memcpy_s(pathFF, sizeof(pathFF), value.c_str(), value.size() + 1);
                } else if (key == "Output") {
                    memcpy_s(pathOutput, sizeof(pathOutput), value.c_str(), value.size() + 1);
                } else if (key == "YTDLP") {
                    memcpy_s(pathYTDLP, sizeof(pathYTDLP), value.c_str(), value.size() + 1);
                }
            }
        }
        configFile.close();
    }

    rescale(true);
    return true;
}

int main(int argc, char* args[]) {
    log("YoinkTube Application Started");

    if (!init()) {
        log("YoinkTube failed");
        shutdown();
        exit(1);
    }

    while (running) {
        // poll events
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) {
                // write config
                std::ofstream s("./config.txt");
                if (s.is_open()) {
                    s << "[FFmpeg]" << std::string(pathFF) << "\n"; 
                    s << "[Output]" << std::string(pathOutput) << "\n";
                    s << "[YTDLP]" << std::string(pathYTDLP) << "\n";
                    s.close();
                }

                running = false;
                break;
            } else if (e.type == SDL_EVENT_WINDOW_MOVED) {
                rescale(false);
            }
        }

        // setup imgui to create a new frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // clear the screen
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
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
