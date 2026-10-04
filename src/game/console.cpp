#include "console.h"
#include "cvars.h"
#include "game.h"
#include "raylib.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <sstream>
#include <string>
#include <vector>

static const int MAX_LINES = 1024;     // Oldest lines are dropped past this
static const int FONT_SIZE = 20;
static const int LINE_HEIGHT = 22;
static const int PADDING = 8;
static const float SLIDE_SPEED = 4.0f; // Opens/closes in 1/4 of a second
static const int SCROLL_STEP = 3;      // Lines per mouse wheel notch

struct ConsoleLine
{
    std::string text;
    Color color;
};

static std::deque<ConsoleLine> lines;
static std::vector<std::string> history;
static size_t historyIndex = 0;  // history.size() means "not browsing history"
static std::string input;
static size_t cursor = 0;        // Position in input, in characters
static bool isOpen = false;
static float openAmount = 0.0f;  // 0 = hidden, 1 = fully down, in between while sliding
static int scroll = 0;           // Lines scrolled back from the newest

static void AddLines(const std::string &text, Color color)
{
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) lines.push_back({ line, color });

    while ((int)lines.size() > MAX_LINES) lines.pop_front();
}

void ConsolePrintf(const char *format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    AddLines(buffer, RAYWHITE);
}

// Receives everything raylib logs, mirroring its normal terminal output into the console
static void TraceLogToConsole(int logLevel, const char *format, va_list args)
{
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);

    const char *prefix = "";
    Color color = LIGHTGRAY;
    switch (logLevel)
    {
        case LOG_TRACE: prefix = "TRACE: "; color = GRAY; break;
        case LOG_DEBUG: prefix = "DEBUG: "; color = GRAY; break;
        case LOG_INFO: prefix = "INFO: "; color = LIGHTGRAY; break;
        case LOG_WARNING: prefix = "WARNING: "; color = YELLOW; break;
        case LOG_ERROR: prefix = "ERROR: "; color = RED; break;
        case LOG_FATAL: prefix = "FATAL: "; color = RED; break;
        default: break;
    }

    printf("%s%s\n", prefix, buffer);
    AddLines(std::string(prefix) + buffer, color);
}

void ConsoleInit()
{
    SetTraceLogCallback(TraceLogToConsole);
}

void ConsoleClear()
{
    lines.clear();
    history.clear();
    historyIndex = 0;
    scroll = 0;
}

int ConsoleSaveLog(const char *path)
{
    std::string text;
    for (const ConsoleLine &line : lines) text += line.text + "\n";

    // Counted first, because SaveFileText logs a message of its own, which adds a line
    int lineCount = (int)lines.size();
    if (!SaveFileText(path, text.c_str())) return -1;
    return lineCount;
}

static void Execute(const std::string &line)
{
    AddLines("]" + line, WHITE);
    if (line.find_first_not_of(" \t") == std::string::npos) return;

    if (history.empty() || history.back() != line) history.push_back(line);
    ExecuteCommandText(line);
}

static bool KeyPressedOrRepeat(int key)
{
    return IsKeyPressed(key) || IsKeyPressedRepeat(key);
}

bool ConsoleUpdate()
{
    bool wasOpen = isOpen;

    // Esc also closes it, like in Quake 3
    if (IsKeyPressed(KEY_GRAVE) || (isOpen && IsKeyPressed(KEY_ESCAPE)))
    {
        isOpen = !isOpen;
        while (GetCharPressed() != 0) {}  // Throw away the character the toggle key just typed
    }

    float target = isOpen ? 1.0f : 0.0f;
    float step = SLIDE_SPEED*GetFrameTime();
    if (openAmount < target) openAmount = (openAmount + step > target) ? target : openAmount + step;
    else if (openAmount > target) openAmount = (openAmount - step < target) ? target : openAmount - step;

    if (!isOpen) return wasOpen;

    // Typing. Only plain ASCII, since commands are ASCII and the default font is too
    int character;
    while ((character = GetCharPressed()) != 0)
    {
        if (character < 32 || character > 126) continue;
        input.insert(cursor, 1, (char)character);
        cursor++;
    }

    if (KeyPressedOrRepeat(KEY_BACKSPACE) && cursor > 0) input.erase(--cursor, 1);
    if (KeyPressedOrRepeat(KEY_DELETE) && cursor < input.size()) input.erase(cursor, 1);
    if (KeyPressedOrRepeat(KEY_LEFT) && cursor > 0) cursor--;
    if (KeyPressedOrRepeat(KEY_RIGHT) && cursor < input.size()) cursor++;
    if (IsKeyPressed(KEY_HOME)) cursor = 0;
    if (IsKeyPressed(KEY_END)) cursor = input.size();

    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
    {
        Execute(input);
        input.clear();
        cursor = 0;
        scroll = 0;
        historyIndex = history.size();
    }

    // Command history
    if (KeyPressedOrRepeat(KEY_UP) && historyIndex > 0)
    {
        input = history[--historyIndex];
        cursor = input.size();
    }
    if (KeyPressedOrRepeat(KEY_DOWN) && historyIndex < history.size())
    {
        historyIndex++;
        input = (historyIndex < history.size()) ? history[historyIndex] : "";
        cursor = input.size();
    }

    // Scrollback
    if (KeyPressedOrRepeat(KEY_PAGE_UP)) scroll += 5;
    if (KeyPressedOrRepeat(KEY_PAGE_DOWN)) scroll -= 5;
    scroll += (int)GetMouseWheelMove()*SCROLL_STEP;
    int maxScroll = (int)lines.size() - 1;
    if (scroll > maxScroll) scroll = maxScroll;
    if (scroll < 0) scroll = 0;

    return true;
}

void ConsoleDraw()
{
    if (openAmount <= 0.0f) return;

    const int width = GetScreenWidth();
    const int height = (int)(GetScreenHeight()*0.5f*openAmount);

    DrawRectangle(0, 0, width, height, Fade(BLACK, 0.85f));
    DrawRectangle(0, height - 2, width, 2, Color{ 200, 60, 40, 255 });

    // Game name in the bottom right corner
    DrawText(GAME_NAME, width - MeasureText(GAME_NAME, 10) - PADDING, height - 16, 10, GRAY);

    // Input line, with a blinking cursor
    const int inputY = height - LINE_HEIGHT - PADDING - 6;
    DrawText(("]" + input).c_str(), PADDING, inputY, FONT_SIZE, RAYWHITE);
    if ((int)(GetTime()*2.0)%2 == 0)
    {
        int cursorX = PADDING + MeasureText(("]" + input.substr(0, cursor)).c_str(), FONT_SIZE);
        DrawRectangle(cursorX, inputY, 2, FONT_SIZE, RAYWHITE);
    }

    // Log, newest at the bottom, going up until it runs off the top
    int y = inputY - LINE_HEIGHT;
    if (scroll > 0)
    {
        // Quake 3 shows carets when scrolled back
        DrawText("^   ^   ^   ^   ^   ^   ^   ^", PADDING, y, FONT_SIZE, GRAY);
        y -= LINE_HEIGHT;
    }

    for (int i = (int)lines.size() - 1 - scroll; i >= 0 && y > -LINE_HEIGHT; i--, y -= LINE_HEIGHT)
    {
        DrawText(lines[i].text.c_str(), PADDING, y, FONT_SIZE, lines[i].color);
    }
}
