#include "settings.h"
#include "console.h"
#include "raylib.h"

// { name, description, value, default value }. The window starts at 1280x720, see main.cpp
Cvar vid_width = { "vid_width", "Window width in pixels, from the options menu's resolutions", 1280.0f, 1280.0f };
Cvar vid_height = { "vid_height", "Window height in pixels", 720.0f, 720.0f };
Cvar vid_fullscreen = { "vid_fullscreen", "Fullscreen at the vid_width x vid_height resolution (0/1)", 0.0f, 0.0f };
Cvar sensitivity = { "sensitivity", "Mouse look speed, 3 = 0.003 radians per pixel", 3.0f, 3.0f };
Cvar m_invert = { "m_invert", "Inverts vertical aim (0/1)", 0.0f, 0.0f };

// The volumes are the engine's, see src/engine/audio.cpp
static Cvar *const SETTINGS_CVARS[] = { &vid_width, &vid_height, &vid_fullscreen, &sensitivity, &m_invert };

std::vector<Resolution> resolutions;

void InitSettings()
{
    static const Resolution common[] = {
        { 1280, 720 }, { 1366, 768 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 }
    };

    int monitor = GetCurrentMonitor();
    resolutions.clear();
    for (Resolution r : common)
    {
        if (r.width <= GetMonitorWidth(monitor) && r.height <= GetMonitorHeight(monitor)) resolutions.push_back(r);
    }
    if (resolutions.empty()) resolutions.push_back({ GetScreenWidth(), GetScreenHeight() });

    AddCvars(SETTINGS_CVARS, (int)(sizeof(SETTINGS_CVARS)/sizeof(SETTINGS_CVARS[0])));
}

int CurrentResolution()
{
    for (size_t i = 0; i < resolutions.size(); i++)
    {
        if (resolutions[i].width == (int)vid_width.value && resolutions[i].height == (int)vid_height.value) return (int)i;
    }
    return 0;
}

void ApplyVideoSettings()
{
    int width = (int)vid_width.value;
    int height = (int)vid_height.value;
    bool fullscreen = vid_fullscreen.value != 0.0f;

    // A size typed in the console or config.cfg that isn't in the list still works, and shows up in the menu
    if (width < 320 || height < 240)
    {
        ConsolePrintf("vid_width and vid_height are too small, using 1280x720");
        width = 1280;
        height = 720;
        vid_width.value = (float)width;
        vid_height.value = (float)height;
    }
    bool listed = false;
    for (Resolution r : resolutions) listed = listed || (r.width == width && r.height == height);
    if (!listed) resolutions.push_back({ width, height });

    if (width == GetScreenWidth() && height == GetScreenHeight() && fullscreen == IsWindowFullscreen()) return;  // Nothing to change

    if (fullscreen)
    {
        // Fullscreen uses the window size as the monitor's video mode, so size the window first
        SetWindowSize(width, height);
        if (!IsWindowFullscreen()) ToggleFullscreen();
    }
    else
    {
        if (IsWindowFullscreen()) ToggleFullscreen();
        SetWindowSize(width, height);

        // Keep the window centered on its monitor
        int monitor = GetCurrentMonitor();
        Vector2 monitorPosition = GetMonitorPosition(monitor);
        SetWindowPosition((int)monitorPosition.x + (GetMonitorWidth(monitor) - width)/2,
                          (int)monitorPosition.y + (GetMonitorHeight(monitor) - height)/2);
    }

    TraceLog(LOG_INFO, "SETTINGS: Video set to %dx%d, %s", width, height, fullscreen ? "fullscreen" : "windowed");
}

void SaveSettings()
{
    if (!SaveConfig(nullptr)) ConsolePrintf("Couldn't save the options to config.cfg");
}
