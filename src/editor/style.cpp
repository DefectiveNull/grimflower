#include "style.h"

#include <string>

// raygui's implementation lives here, since the dark style needs access to its internals
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include "style_dark.h"

static const char *FONT_PATH = "fonts/Roboto-Regular.ttf";
static const int FONT_LOAD_SIZE = 32;  // Loaded large and scaled down with mipmaps, so it stays smooth at every size
static const int UI_TEXT_SIZE = 16;

static Font font = { 0 };
static bool fontLoaded = false;
static bool darkMode = false;

// Puts back the editor's font after loading a style, since styles can bring fonts of their own
static void ApplyFont(bool freeStyleFont)
{
    if (freeStyleFont)
    {
        // The dark style loads its own pixel font and draws shapes from its texture. Point shape
        // drawing back at raylib's default font, then free the style's font, before switching
        Font defaultFont = GetFontDefault();
        Rectangle white = defaultFont.recs[95];
        SetShapesTexture(defaultFont.texture, { white.x + 2, white.y + 2, 1, 1 });
        UnloadFont(GuiGetFont());
    }

    GuiSetFont(fontLoaded ? font : GetFontDefault());
    GuiSetStyle(DEFAULT, TEXT_SIZE, UI_TEXT_SIZE);
    GuiSetStyle(DEFAULT, TEXT_SPACING, 0);
}

void LoadEditorStyle()
{
    font = LoadFontEx(FONT_PATH, FONT_LOAD_SIZE, nullptr, 0);
    fontLoaded = (font.texture.id != GetFontDefault().texture.id);
    if (fontLoaded)
    {
        GenTextureMipmaps(&font.texture);
        SetTextureFilter(font.texture, TEXTURE_FILTER_TRILINEAR);
    }
    else
    {
        TraceLog(LOG_WARNING, "STYLE: [%s] Couldn't load, using raylib's default font", FONT_PATH);
    }

    SetDarkMode(false);
}

void UnloadEditorStyle()
{
    if (fontLoaded) UnloadFont(font);
    fontLoaded = false;
}

void SetDarkMode(bool dark)
{
    // GuiLoadStyleDefault() frees whatever font raygui has unless it's raylib's default, which would
    // free Roboto. So give raygui the default font while loading a style, and Roboto back after
    GuiSetFont(GetFontDefault());

    darkMode = dark;
    if (dark)
    {
        GuiLoadStyleDark();
        ApplyFont(true);
    }
    else
    {
        GuiLoadStyleDefault();
        ApplyFont(false);
    }
}

bool IsDarkMode()
{
    return darkMode;
}

void DrawUIText(const char *text, float x, float y, float size, Color color)
{
    DrawTextEx(fontLoaded ? font : GetFontDefault(), text, { x, y }, size, 0.0f, color);
}

float MeasureUIText(const char *text, float size)
{
    return MeasureTextEx(fontLoaded ? font : GetFontDefault(), text, size, 0.0f).x;
}

Color UITextColor()
{
    return GetColor(GuiGetStyle(LABEL, TEXT_COLOR_NORMAL));
}

Color UIMutedTextColor()
{
    return ColorLerp(UITextColor(), GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)), 0.4f);
}

Color ViewportBackground()
{
    return darkMode ? Color{ 34, 34, 38, 255 } : RAYWHITE;
}

// Draws text word-wrapped to a width, and returns how tall it came out
float DrawUITextWrapped(const char *text, float x, float y, float width, float size, Color color)
{
    std::string line, word;
    float lineY = y;
    auto flush = [&]()
    {
        if (!line.empty()) DrawUIText(line.c_str(), x, lineY, size, color);
        lineY += size + 3;
        line.clear();
    };

    for (const char *c = text; ; c++)
    {
        if (*c == ' ' || *c == '\0')
        {
            std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && MeasureUIText(candidate.c_str(), size) > width) { flush(); line = word; }
            else line = candidate;
            word.clear();
            if (*c == '\0') break;
        }
        else
        {
            word += *c;
        }
    }
    flush();
    return lineY - y;
}
