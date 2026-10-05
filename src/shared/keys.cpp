#include "keys.h"
#include "raylib.h"

#include <cctype>

struct KeyName
{
    const char *name;
    int key;
};

// Names that aren't just the character on the key. Letters and digits are handled separately
static const KeyName KEY_NAMES[] = {
    { "space", KEY_SPACE }, { "tab", KEY_TAB }, { "enter", KEY_ENTER }, { "escape", KEY_ESCAPE },
    { "backspace", KEY_BACKSPACE }, { "capslock", KEY_CAPS_LOCK },
    { "uparrow", KEY_UP }, { "downarrow", KEY_DOWN }, { "leftarrow", KEY_LEFT }, { "rightarrow", KEY_RIGHT },
    { "shift", KEY_LEFT_SHIFT }, { "ctrl", KEY_LEFT_CONTROL }, { "alt", KEY_LEFT_ALT },
    { "ins", KEY_INSERT }, { "del", KEY_DELETE }, { "home", KEY_HOME }, { "end", KEY_END },
    { "pgup", KEY_PAGE_UP }, { "pgdn", KEY_PAGE_DOWN },
    { "f1", KEY_F1 }, { "f2", KEY_F2 }, { "f3", KEY_F3 }, { "f4", KEY_F4 }, { "f5", KEY_F5 }, { "f6", KEY_F6 },
    { "f7", KEY_F7 }, { "f8", KEY_F8 }, { "f9", KEY_F9 }, { "f10", KEY_F10 }, { "f11", KEY_F11 }, { "f12", KEY_F12 },
    { "-", KEY_MINUS }, { "=", KEY_EQUAL }, { "[", KEY_LEFT_BRACKET }, { "]", KEY_RIGHT_BRACKET },
    { "semicolon", KEY_SEMICOLON }, { "'", KEY_APOSTROPHE }, { ",", KEY_COMMA }, { ".", KEY_PERIOD },
    { "/", KEY_SLASH }, { "\\", KEY_BACKSLASH },
    { "kp_0", KEY_KP_0 }, { "kp_1", KEY_KP_1 }, { "kp_2", KEY_KP_2 }, { "kp_3", KEY_KP_3 }, { "kp_4", KEY_KP_4 },
    { "kp_5", KEY_KP_5 }, { "kp_6", KEY_KP_6 }, { "kp_7", KEY_KP_7 }, { "kp_8", KEY_KP_8 }, { "kp_9", KEY_KP_9 },
    { "kp_enter", KEY_KP_ENTER }, { "kp_plus", KEY_KP_ADD }, { "kp_minus", KEY_KP_SUBTRACT },
    { "mouse1", KEY_CODE_MOUSE + MOUSE_BUTTON_LEFT }, { "mouse2", KEY_CODE_MOUSE + MOUSE_BUTTON_RIGHT },
    { "mouse3", KEY_CODE_MOUSE + MOUSE_BUTTON_MIDDLE }, { "mouse4", KEY_CODE_MOUSE + MOUSE_BUTTON_SIDE },
    { "mouse5", KEY_CODE_MOUSE + MOUSE_BUTTON_EXTRA },
    { "mwheelup", KEY_CODE_MOUSE_WHEEL_UP }, { "mwheeldown", KEY_CODE_MOUSE_WHEEL_DOWN },
};

int KeyFromName(const std::string &name)
{
    std::string lower;
    for (char c : name) lower += (char)tolower((unsigned char)c);

    if (lower.size() == 1 && lower[0] >= 'a' && lower[0] <= 'z') return KEY_A + (lower[0] - 'a');
    if (lower.size() == 1 && lower[0] >= '0' && lower[0] <= '9') return KEY_ZERO + (lower[0] - '0');

    for (const KeyName &entry : KEY_NAMES)
    {
        if (lower == entry.name) return entry.key;
    }
    return -1;
}

std::string KeyToName(int key)
{
    if (key >= KEY_A && key <= KEY_Z) return std::string(1, (char)('a' + (key - KEY_A)));
    if (key >= KEY_ZERO && key <= KEY_NINE) return std::string(1, (char)('0' + (key - KEY_ZERO)));

    for (const KeyName &entry : KEY_NAMES)
    {
        if (key == entry.key) return entry.name;
    }
    return "unknown";
}

// The right-hand twin of a modifier key, or -1
static int OtherSide(int key)
{
    switch (key)
    {
        case KEY_LEFT_SHIFT: return KEY_RIGHT_SHIFT;
        case KEY_LEFT_CONTROL: return KEY_RIGHT_CONTROL;
        case KEY_LEFT_ALT: return KEY_RIGHT_ALT;
        default: return -1;
    }
}

bool IsInputPressed(int key)
{
    if (key == KEY_CODE_MOUSE_WHEEL_UP) return GetMouseWheelMove() > 0.0f;
    if (key == KEY_CODE_MOUSE_WHEEL_DOWN) return GetMouseWheelMove() < 0.0f;
    if (key >= KEY_CODE_MOUSE) return IsMouseButtonPressed(key - KEY_CODE_MOUSE);

    int other = OtherSide(key);
    if (other < 0) return IsKeyPressed(key);

    // Either side counts, but holding one and then pressing the other isn't a new press
    return (IsKeyPressed(key) && !IsKeyDown(other)) || (IsKeyPressed(other) && !IsKeyDown(key));
}

bool IsInputReleased(int key)
{
    if (key == KEY_CODE_MOUSE_WHEEL_UP || key == KEY_CODE_MOUSE_WHEEL_DOWN) return IsInputPressed(key);
    if (key >= KEY_CODE_MOUSE) return IsMouseButtonReleased(key - KEY_CODE_MOUSE);

    int other = OtherSide(key);
    if (other < 0) return IsKeyReleased(key);

    // Released once neither side is held
    return (IsKeyReleased(key) || IsKeyReleased(other)) && !IsKeyDown(key) && !IsKeyDown(other);
}
