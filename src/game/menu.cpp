#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include "menu.h"
#include "audio.h"
#include "binds.h"
#include "cvars.h"
#include "keys.h"
#include "settings.h"

#include <string>
#include <vector>

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

// ---------------------------------------------------------------------------------------------
// Options

// One column: a label on the left of each row, its control on the right
static const float OPTIONS_WIDTH = 460.0f;
static const float LABEL_WIDTH = 200.0f;
static const float ROW_HEIGHT = 36.0f;
static const float ROW_GAP = 10.0f;
static const float SECTION_HEIGHT = 30.0f;
static const float SECTION_GAP = 8.0f;
static const float CHECK_BOX_SIZE = 24.0f;
static const float SLIDER_VALUE_WIDTH = 60.0f;  // Room for the value text to the right of sliders
static const float BUTTON_WIDTH = 240.0f;
static const float BUTTON_HEIGHT = 50.0f;
static const float TAB_HEIGHT = 40.0f;

// The actions the Input tab lets you rebind, and the commands their keys run (see cfg/default.cfg)
struct BindAction
{
    const char *command;
    const char *label;
};

static const BindAction BIND_ACTIONS[] =
{
    { "+forward", "Move forward" },
    { "+back", "Move back" },
    { "+moveleft", "Move left" },
    { "+moveright", "Move right" },
    { "+jump", "Jump / fly up" },
    { "+duck", "Crouch / swim down" },
    { "fly", "Toggle fly" },
    { "togglecursor", "Free the mouse" },
    { "toggle r_showcolliders", "Show colliders" },
};
static const int BIND_ACTION_COUNT = (int)(sizeof(BIND_ACTIONS)/sizeof(BIND_ACTIONS[0]));

static int optionsTab = 0;          // 0 Video, 1 Audio, 2 Game, 3 Input
static int capturingAction = -1;    // Index into BIND_ACTIONS waiting for a key, or -1
static bool captureStarted = false; // The click that started capturing has been let go of, so mouse buttons can be bound
static bool settingsChanged = false;

// Settings are saved once the mouse button is up, so dragging a slider writes config.cfg once rather than every frame
static void SaveSettingsWhenReady(bool force)
{
    if (settingsChanged && (force || !IsMouseButtonDown(MOUSE_BUTTON_LEFT)))
    {
        SaveSettings();
        settingsChanged = false;
    }
}

bool IsOptionsMenuCapturingKey()
{
    return capturingAction >= 0;
}

static float DrawVideoOptions(float x, float y)
{
    const float controlX = x + LABEL_WIDTH + 20.0f;
    const float controlWidth = OPTIONS_WIDTH - LABEL_WIDTH - 20.0f;

    std::string resolutionNames;
    for (size_t i = 0; i < resolutions.size(); i++)
    {
        if (i > 0) resolutionNames += ";";
        resolutionNames += TextFormat("%dx%d", resolutions[i].width, resolutions[i].height);
    }
    int resolution = CurrentResolution();
    GuiLabel({ x, y, LABEL_WIDTH, ROW_HEIGHT }, "Resolution");
    GuiComboBox({ controlX, y, controlWidth, ROW_HEIGHT }, resolutionNames.c_str(), &resolution);
    y += ROW_HEIGHT + ROW_GAP;

    bool fullscreen = vid_fullscreen.value != 0.0f;
    GuiLabel({ x, y, LABEL_WIDTH, ROW_HEIGHT }, "Fullscreen");
    GuiCheckBox({ controlX, y + (ROW_HEIGHT - CHECK_BOX_SIZE)/2, CHECK_BOX_SIZE, CHECK_BOX_SIZE }, "", &fullscreen);
    y += ROW_HEIGHT + ROW_GAP;

    if (resolution != CurrentResolution() || fullscreen != (vid_fullscreen.value != 0.0f))
    {
        vid_width.value = (float)resolutions[resolution].width;
        vid_height.value = (float)resolutions[resolution].height;
        vid_fullscreen.value = fullscreen ? 1.0f : 0.0f;
        ApplyVideoSettings();
        settingsChanged = true;
    }

    // Post processing, which applies every frame (postfx.h)
    struct PostRow { const char *label; Cvar *cvar; float min, max; };
    const PostRow postRows[] = { { "Contrast", &r_contrast, 0.5f, 1.5f }, { "Saturation", &r_saturation, 0.0f, 2.0f },
                                 { "Film grain", &r_filmgrain, 0.0f, 1.0f }, { "Gamma", &r_gamma, 0.5f, 2.0f } };
    for (const PostRow &row : postRows)
    {
        float value = row.cvar->value;
        GuiLabel({ x, y, LABEL_WIDTH, ROW_HEIGHT }, row.label);
        GuiSliderBar({ controlX, y + 8, controlWidth - SLIDER_VALUE_WIDTH, ROW_HEIGHT - 16 }, nullptr,
                     TextFormat("%d%%", (int)(value*100.0f + 0.5f)), &value, row.min, row.max);
        y += ROW_HEIGHT + ROW_GAP;

        if (value != row.cvar->value)
        {
            row.cvar->value = value;
            settingsChanged = true;
        }
    }

    return y;
}

static float DrawAudioOptions(float x, float y)
{
    const float controlX = x + LABEL_WIDTH + 20.0f;
    const float controlWidth = OPTIONS_WIDTH - LABEL_WIDTH - 20.0f;

    // The master volume, then each channel's on top of it. UpdateAudio applies them
    struct VolumeRow { const char *label; Cvar *cvar; };
    const VolumeRow volumes[] = { { "Master volume", &volume }, { "World volume", &world_volume }, { "Music volume", &music_volume } };
    for (const VolumeRow &row : volumes)
    {
        float value = row.cvar->value;
        GuiLabel({ x, y, LABEL_WIDTH, ROW_HEIGHT }, row.label);
        GuiSliderBar({ controlX, y + 8, controlWidth - SLIDER_VALUE_WIDTH, ROW_HEIGHT - 16 }, nullptr,
                     TextFormat("%d%%", (int)(value*100.0f + 0.5f)), &value, 0.0f, 1.0f);
        y += ROW_HEIGHT + ROW_GAP;

        if (value != row.cvar->value)
        {
            row.cvar->value = value;
            settingsChanged = true;
        }
    }

    return y;
}

static float DrawGameOptions(float x, float y)
{
    const float controlX = x + LABEL_WIDTH + 20.0f;
    const float controlWidth = OPTIONS_WIDTH - LABEL_WIDTH - 20.0f;

    float lookSpeed = sensitivity.value;
    GuiLabel({ x, y, LABEL_WIDTH, ROW_HEIGHT }, "Mouse sensitivity");
    GuiSliderBar({ controlX, y + 8, controlWidth - SLIDER_VALUE_WIDTH, ROW_HEIGHT - 16 }, nullptr,
                 TextFormat("%.1f", lookSpeed), &lookSpeed, 0.5f, 10.0f);
    y += ROW_HEIGHT + ROW_GAP;

    bool invert = m_invert.value != 0.0f;
    GuiLabel({ x, y, LABEL_WIDTH, ROW_HEIGHT }, "Invert vertical aim");
    GuiCheckBox({ controlX, y + (ROW_HEIGHT - CHECK_BOX_SIZE)/2, CHECK_BOX_SIZE, CHECK_BOX_SIZE }, "", &invert);
    y += ROW_HEIGHT + ROW_GAP;

    if (lookSpeed != sensitivity.value || invert != (m_invert.value != 0.0f))
    {
        sensitivity.value = lookSpeed;
        m_invert.value = invert ? 1.0f : 0.0f;
        settingsChanged = true;
    }

    return y;
}

// The keys bound to a command, like "w" or "mouse4, f", or "" if none
static std::string KeysFor(const char *command)
{
    std::string keys;
    for (const auto &bind : GetBinds())
    {
        if (bind.second != command) continue;
        if (!keys.empty()) keys += ", ";
        keys += KeyToName(bind.first);
    }
    return keys;
}

// The first bindable key, mouse button or wheel movement pressed this frame, or -1
static int PressedBindableKey()
{
    for (int key = 1; key < 400; key++)
    {
        if (KeyToName(key) != "unknown" && IsInputPressed(key)) return key;
    }
    for (int button = MOUSE_BUTTON_LEFT; button <= MOUSE_BUTTON_EXTRA; button++)
    {
        if (IsInputPressed(KEY_CODE_MOUSE + button)) return KEY_CODE_MOUSE + button;
    }
    if (IsInputPressed(KEY_CODE_MOUSE_WHEEL_UP)) return KEY_CODE_MOUSE_WHEEL_UP;
    if (IsInputPressed(KEY_CODE_MOUSE_WHEEL_DOWN)) return KEY_CODE_MOUSE_WHEEL_DOWN;
    return -1;
}

// Waits for a key for the action being rebound. Esc cancels
static void UpdateKeyCapture()
{
    if (capturingAction < 0) return;

    // The click on the action's button is still down the frame capturing starts, so wait for it to be let go
    if (!captureStarted)
    {
        captureStarted = !IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        return;
    }

    if (IsKeyPressed(KEY_ESCAPE))
    {
        capturingAction = -1;
        return;
    }

    int key = PressedBindableKey();
    if (key < 0) return;

    // One key per action from here. Like Quake, binding a key takes it from whatever had it
    const char *command = BIND_ACTIONS[capturingAction].command;
    std::vector<int> oldKeys;
    for (const auto &bind : GetBinds()) if (bind.second == command) oldKeys.push_back(bind.first);
    for (int oldKey : oldKeys) RemoveBind(oldKey);
    SetBind(key, command);

    capturingAction = -1;
    SaveSettings();
}

static float DrawInputOptions(float x, float y)
{
    const float controlX = x + LABEL_WIDTH + 20.0f;
    const float controlWidth = OPTIONS_WIDTH - LABEL_WIDTH - 20.0f;
    const float rowGap = 6.0f;

    UpdateKeyCapture();

    GuiLine({ x, y, OPTIONS_WIDTH, SECTION_HEIGHT }, "Click an action, then press a key");
    y += SECTION_HEIGHT + SECTION_GAP;

    for (int i = 0; i < BIND_ACTION_COUNT; i++)
    {
        GuiLabel({ x, y, LABEL_WIDTH, ROW_HEIGHT }, BIND_ACTIONS[i].label);

        std::string keys = KeysFor(BIND_ACTIONS[i].command);
        const char *text = (capturingAction == i) ? "Press a key (Esc cancels)" : (keys.empty() ? "(none)" : keys.c_str());
        if (GuiButton({ controlX, y, controlWidth, ROW_HEIGHT }, text) && capturingAction < 0)
        {
            capturingAction = i;
            captureStarted = false;
        }
        y += ROW_HEIGHT + rowGap;
    }

    y += ROW_GAP;
    if (GuiButton({ controlX, y, controlWidth, ROW_HEIGHT }, "Reset to defaults") && capturingAction < 0)
    {
        ExecConfig("cfg/default.cfg", false);  // Starts with unbindall
        SaveSettings();
    }
    y += ROW_HEIGHT + ROW_GAP;

    return y;
}

bool DrawOptionsMenu()
{
    // Sized for the taller tab, so switching tabs doesn't move the title or Back
    const float inputHeight = SECTION_HEIGHT + SECTION_GAP + BIND_ACTION_COUNT*(ROW_HEIGHT + 6.0f) + ROW_GAP + ROW_HEIGHT + ROW_GAP;
    const float totalHeight = TAB_HEIGHT + ROW_GAP + inputHeight + 20.0f + BUTTON_HEIGHT;
    const float x = (GetScreenWidth() - OPTIONS_WIDTH)/2.0f;
    float y = (GetScreenHeight() - totalHeight)/2.0f + 30.0f;

    DrawMenuBackground("OPTIONS", y - 70);

    // Tabs, spanning the panel's width. Switching while waiting for a key cancels it
    const int tabCount = 4;
    float tabGap = (float)GuiGetStyle(TOGGLE, GROUP_PADDING);
    int tab = optionsTab;
    GuiToggleGroup({ x, y, (OPTIONS_WIDTH - (tabCount - 1)*tabGap)/tabCount, TAB_HEIGHT }, "Video;Audio;Game;Input", &tab);
    if (tab != optionsTab)
    {
        optionsTab = tab;
        capturingAction = -1;
    }
    float contentY = y + TAB_HEIGHT + ROW_GAP;

    switch (optionsTab)
    {
        case 0: DrawVideoOptions(x, contentY); break;
        case 1: DrawAudioOptions(x, contentY); break;
        case 2: DrawGameOptions(x, contentY); break;
        default: DrawInputOptions(x, contentY); break;
    }
    SaveSettingsWhenReady(false);

    float backY = contentY + inputHeight + 20.0f;
    bool back = GuiButton({ (GetScreenWidth() - BUTTON_WIDTH)/2.0f, backY, BUTTON_WIDTH, BUTTON_HEIGHT }, "Back") && capturingAction < 0;
    if (back) SaveSettingsWhenReady(true);
    return back;
}
