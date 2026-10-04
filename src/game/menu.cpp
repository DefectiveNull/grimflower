#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include "menu.h"
#include "settings.h"

#include <string>

// Translucent white overlay over the game, plus a centered title
static void DrawMenuBackground(const char *title, float titleY)
{
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(WHITE, 0.5f));
    DrawText(title, (GetScreenWidth() - MeasureText(title, 40))/2, (int)titleY, 40, DARKGRAY);
    GuiSetStyle(DEFAULT, TEXT_SIZE, 20);
}

// Shared layout: translucent overlay, a title, and the three buttons
static MenuAction DrawMenu(const char *title, const char *continueLabel)
{
    const int screenWidth = GetScreenWidth();
    const int screenHeight = GetScreenHeight();

    const float buttonWidth = 240.0f;
    const float buttonHeight = 50.0f;
    const float spacing = 16.0f;
    const float x = (screenWidth - buttonWidth)/2.0f;
    const float y = screenHeight/2.0f - (3*buttonHeight + 2*spacing)/2.0f;

    DrawMenuBackground(title, y - 70);

    MenuAction action = MENU_NONE;
    if (GuiButton({ x, y, buttonWidth, buttonHeight }, continueLabel)) action = MENU_CONTINUE;
    if (GuiButton({ x, y + (buttonHeight + spacing), buttonWidth, buttonHeight }, "Options")) action = MENU_OPTIONS;
    if (GuiButton({ x, y + 2*(buttonHeight + spacing), buttonWidth, buttonHeight }, "Quit")) action = MENU_QUIT;

    return action;
}

MenuAction DrawMainMenu(const char *title)
{
    MenuAction action = DrawMenu(title, "Play");

    const char *credit = "Created by Defective Null";
    DrawText(credit, (GetScreenWidth() - MeasureText(credit, 20))/2, GetScreenHeight() - 40, 20, DARKGRAY);

    return action;
}

MenuAction DrawPauseMenu()
{
    return DrawMenu("PAUSED", "Resume");
}

bool DrawOptionsMenu()
{
    // One column: a label on the left of each row, its control on the right
    const float width = 460.0f;
    const float labelWidth = 200.0f;
    const float rowHeight = 36.0f;
    const float rowGap = 10.0f;
    const float sectionHeight = 30.0f;
    const float sectionGap = 8.0f;
    const float checkBoxSize = 24.0f;
    const float sliderValueWidth = 60.0f;  // Room for the value text to the right of sliders
    const float buttonWidth = 240.0f;
    const float buttonHeight = 50.0f;

    const float totalHeight = 3*(sectionHeight + sectionGap) + 5*(rowHeight + rowGap) + 20.0f + buttonHeight;
    const float x = (GetScreenWidth() - width)/2.0f;
    const float controlX = x + labelWidth + 20.0f;
    const float controlWidth = width - labelWidth - 20.0f;
    float y = (GetScreenHeight() - totalHeight)/2.0f + 30.0f;

    DrawMenuBackground("OPTIONS", y - 70);

    // Video
    GuiLine({ x, y, width, sectionHeight }, "Video");
    y += sectionHeight + sectionGap;

    std::string resolutionNames;
    for (size_t i = 0; i < resolutions.size(); i++)
    {
        if (i > 0) resolutionNames += ";";
        resolutionNames += TextFormat("%dx%d", resolutions[i].width, resolutions[i].height);
    }
    int resolution = settings.resolution;
    GuiLabel({ x, y, labelWidth, rowHeight }, "Resolution");
    GuiComboBox({ controlX, y, controlWidth, rowHeight }, resolutionNames.c_str(), &resolution);
    y += rowHeight + rowGap;

    bool fullscreen = settings.fullscreen;
    GuiLabel({ x, y, labelWidth, rowHeight }, "Fullscreen");
    GuiCheckBox({ controlX, y + (rowHeight - checkBoxSize)/2, checkBoxSize, checkBoxSize }, "", &fullscreen);
    y += rowHeight + rowGap;

    if (resolution != settings.resolution || fullscreen != settings.fullscreen)
    {
        settings.resolution = resolution;
        settings.fullscreen = fullscreen;
        ApplyVideoSettings();
    }

    // Audio
    GuiLine({ x, y, width, sectionHeight }, "Audio");
    y += sectionHeight + sectionGap;

    float volume = settings.masterVolume;
    GuiLabel({ x, y, labelWidth, rowHeight }, "Master volume");
    GuiSliderBar({ controlX, y + 8, controlWidth - sliderValueWidth, rowHeight - 16 }, nullptr,
                 TextFormat("%d%%", (int)(volume*100.0f + 0.5f)), &volume, 0.0f, 1.0f);
    y += rowHeight + rowGap;

    if (volume != settings.masterVolume)
    {
        settings.masterVolume = volume;
        ApplyAudioSettings();
    }

    // Game
    GuiLine({ x, y, width, sectionHeight }, "Game");
    y += sectionHeight + sectionGap;

    GuiLabel({ x, y, labelWidth, rowHeight }, "Mouse sensitivity");
    GuiSliderBar({ controlX, y + 8, controlWidth - sliderValueWidth, rowHeight - 16 }, nullptr,
                 TextFormat("%.1f", settings.mouseSensitivity), &settings.mouseSensitivity, 0.5f, 10.0f);
    y += rowHeight + rowGap;

    GuiLabel({ x, y, labelWidth, rowHeight }, "Invert vertical aim");
    GuiCheckBox({ controlX, y + (rowHeight - checkBoxSize)/2, checkBoxSize, checkBoxSize }, "", &settings.invertMouseY);
    y += rowHeight + rowGap;

    y += 20.0f;
    return GuiButton({ (GetScreenWidth() - buttonWidth)/2.0f, y, buttonWidth, buttonHeight }, "Back");
}
