#include "settings.h"
#include "raylib.h"

Settings settings = {
    0,      // resolution, set by InitSettings
    false,  // fullscreen
    1.0f,   // masterVolume
    3.0f,   // mouseSensitivity
    false   // invertMouseY
};

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

    settings.resolution = 0;
    for (size_t i = 0; i < resolutions.size(); i++)
    {
        if (resolutions[i].width == GetScreenWidth() && resolutions[i].height == GetScreenHeight()) settings.resolution = (int)i;
    }
    settings.fullscreen = IsWindowFullscreen();
}

void ApplyVideoSettings()
{
    Resolution r = resolutions[settings.resolution];

    if (settings.fullscreen)
    {
        // Fullscreen uses the window size as the monitor's video mode, so size the window first
        SetWindowSize(r.width, r.height);
        if (!IsWindowFullscreen()) ToggleFullscreen();
    }
    else
    {
        if (IsWindowFullscreen()) ToggleFullscreen();
        SetWindowSize(r.width, r.height);

        // Keep the window centered on its monitor
        int monitor = GetCurrentMonitor();
        Vector2 monitorPosition = GetMonitorPosition(monitor);
        SetWindowPosition((int)monitorPosition.x + (GetMonitorWidth(monitor) - r.width)/2,
                          (int)monitorPosition.y + (GetMonitorHeight(monitor) - r.height)/2);
    }

    TraceLog(LOG_INFO, "SETTINGS: Video set to %dx%d, %s", r.width, r.height, settings.fullscreen ? "fullscreen" : "windowed");
}

void ApplyAudioSettings()
{
    SetMasterVolume(settings.masterVolume);
}
