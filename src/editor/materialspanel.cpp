// The details panel's Materials tab: every diffuse texture in textures/, by subfolder, and for the one
// picked, its bump map, specular map and gloss map (materials.h). Changes show in the view right away,
// and are saved to .materials, which the game reads when it loads a world

#include "editor.h"
#include "filedialog.h"
#include "materials.h"
#include "raygui.h"
#include "style.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

static const float PADDING = 10.0f;
static const float ROW_HEIGHT = 28.0f;
static const float LABEL_WIDTH = 96.0f;
static const float THUMBNAIL_SIZE = 22.0f;

// Files with these endings are maps, not diffuse textures, so they're left out of the list
static const char *MAP_SUFFIXES[] = { "_normal", "_bump", "_spec", "_specular", "_gloss", "_glossy" };

struct TextureFolder
{
    std::string name;                   // Below textures/, like "dev"
    std::vector<std::string> textures;  // Game data paths, like "textures/dev/wall128.tga"
    bool collapsed = false;
};

static MaterialLibrary library;
static bool libraryLoaded = false;
static bool librarySaveWaiting = false;  // Changed while a slider was being dragged, saved when it's let go
static std::vector<TextureFolder> folders;
static bool scanned = false;
static std::string picked;               // The texture whose material is shown, empty for none
static std::map<std::string, Texture> thumbnails;
static float scroll = 0.0f;

// The text field being typed in, and what's been typed so far
static int activeField = -1;
static char editBuffer[256];

static std::string Lowercase(std::string text)
{
    for (char &c : text) c = (char)tolower((unsigned char)c);
    return text;
}

static bool IsMapName(const std::string &path)
{
    std::string name = Lowercase(GetFileNameWithoutExt(path.c_str()));
    for (const char *suffix : MAP_SUFFIXES)
    {
        size_t length = strlen(suffix);
        if (name.size() > length && name.compare(name.size() - length, length, suffix) == 0) return true;
    }
    return false;
}

// Finds every image in textures/, grouped by the folder it's in. Ones used as a material's map are left out
static void ScanTextures(EditorState &editor)
{
    folders.clear();
    std::set<std::string> usedAsMaps;
    for (const auto &[name, def] : library)
    {
        for (const std::string *map : { &def.bumpmap, &def.specularmap, &def.glossmap }) if (!map->empty()) usedAsMaps.insert(*map);
    }

    std::map<std::string, std::vector<std::string>> byFolder;
    FilePathList files = LoadDirectoryFilesEx("textures", ".png;.tga;.jpg;.jpeg;.bmp", true);
    for (unsigned int i = 0; i < files.count; i++)
    {
        std::string path = ToDataPath(editor, files.paths[i]);
        for (char &c : path) if (c == '\\') c = '/';
        if (path.compare(0, 9, "textures/") != 0) path = "textures/" + std::string(GetFileName(files.paths[i]));
        if (IsMapName(path) || usedAsMaps.count(path)) continue;

        std::string folder = GetDirectoryPath(path.c_str());
        for (char &c : folder) if (c == '\\') c = '/';
        folder = (folder.size() > 9) ? folder.substr(9) : "(textures)";
        byFolder[folder].push_back(path);
    }
    UnloadDirectoryFiles(files);

    for (auto &[name, textures] : byFolder)
    {
        std::sort(textures.begin(), textures.end());
        folders.push_back({ name, textures });
    }
    scanned = true;
}

static Texture Thumbnail(const std::string &path)
{
    auto it = thumbnails.find(path);
    if (it != thumbnails.end()) return it->second;
    Texture texture = { 0 };
    if (!LoadGameTexture(path.c_str(), texture)) texture = { 0 };
    thumbnails[path] = texture;
    return texture;
}

// After a material changes: the view shows it right away. Changing a map loads it; a number is just copied
static void ShowChange(EditorState &editor, bool mapsChanged)
{
    if (!editor.worldLoaded) return;
    if (mapsChanged)
    {
        ApplyMaterials(editor.world, library);
        return;
    }
    for (WorldMaterial &material : editor.world.materials)
    {
        if (material.diffusePath == picked) material.def = FindMaterial(library, picked);
    }
}

static void Save(EditorState &editor)
{
    librarySaveWaiting = false;
    if (SaveMaterialLibrary(library)) SetStatus(editor, "Saved %s", MATERIALS_PATH);
    else SetStatus(editor, "Couldn't save %s", MATERIALS_PATH);
}

// A text box showing a map's path. Clicking it starts typing; Enter or a click elsewhere finishes, and
// returns true with the typed text in editBuffer
static bool PathField(int id, Rectangle bounds, const std::string &value)
{
    if (activeField == id)
    {
        if (GuiTextBox(bounds, editBuffer, sizeof(editBuffer), true))
        {
            activeField = -1;
            return true;
        }
        return false;
    }
    char shown[256];
    snprintf(shown, sizeof(shown), "%s", value.c_str());
    if (GuiTextBox(bounds, shown, sizeof(shown), false))
    {
        activeField = id;
        snprintf(editBuffer, sizeof(editBuffer), "%s", value.c_str());
    }
    return false;
}

// A map's row: its path, typed or picked with Browse. Returns true if it changed, with the new path in path
static bool MapRow(EditorState &editor, int id, const char *label, float x, float y, float width, std::string &path)
{
    GuiLabel({ x, y, LABEL_WIDTH, ROW_HEIGHT - 4 }, label);
    const float buttonWidth = 30.0f;
    Rectangle field = { x + LABEL_WIDTH, y, width - LABEL_WIDTH - buttonWidth - 4, ROW_HEIGHT - 4 };
    if (PathField(id, field, path))
    {
        std::string typed = editBuffer;
        for (char &c : typed) if (c == '\\') c = '/';
        if (typed != path)
        {
            if (!typed.empty() && !FileExists(typed.c_str())) SetStatus(editor, "There's no %s, check the path", typed.c_str());
            path = typed;
            return true;
        }
    }
    if (GuiButton({ field.x + field.width + 4, y, buttonWidth, ROW_HEIGHT - 4 }, "..."))
    {
        std::string chosen;
        if (ShowOpenFileDialog(GetWindowHandle(), label, "Images (*.png;*.tga;*.jpg;*.bmp)\0*.png;*.tga;*.jpg;*.jpeg;*.bmp\0All files (*.*)\0*.*\0",
                               editor.dataRoot + "/textures", chosen))
        {
            chosen = ToDataPath(editor, chosen);
            for (char &c : chosen) if (c == '\\') c = '/';
            if (chosen != path)
            {
                path = chosen;
                return true;
            }
        }
    }
    return false;
}

// A slider plus a box for typing an exact value. Returns true if it changed
static bool NumberRow(int id, const char *label, float x, float y, float width, float &value, float max)
{
    GuiLabel({ x, y, LABEL_WIDTH, ROW_HEIGHT - 4 }, label);
    const float valueWidth = 56.0f;
    float slider = value;
    GuiSliderBar({ x + LABEL_WIDTH, y + 6, width - LABEL_WIDTH - valueWidth - 6, ROW_HEIGHT - 16 }, nullptr, nullptr, &slider, 0.0f, max);
    bool changed = false;
    if (slider != value)
    {
        value = roundf(slider*100.0f)/100.0f;
        changed = true;
    }
    if (PathField(id, { x + width - valueWidth, y, valueWidth, ROW_HEIGHT - 4 }, TextFormat("%g", value)))
    {
        char *end;
        float typed = strtof(editBuffer, &end);
        if (end != editBuffer && typed >= 0.0f && typed != value)
        {
            value = typed;
            changed = true;
        }
    }
    return changed;
}

// The picked texture's material. Returns its height
static float DrawMaterialFields(EditorState &editor, float x, float y, float width)
{
    float top = y;
    MaterialDef def = FindMaterial(library, picked);
    bool changed = false, mapsChanged = false;

    if (MapRow(editor, 0, "Bump map", x, y, width, def.bumpmap)) changed = mapsChanged = true;
    y += ROW_HEIGHT;
    bool openGL = def.bumpmapOpenGL;
    GuiCheckBox({ x + LABEL_WIDTH, y + 5, 16, 16 }, "OpenGL style (green points up)", &openGL);
    if (openGL != def.bumpmapOpenGL)
    {
        def.bumpmapOpenGL = openGL;
        changed = true;
    }
    y += ROW_HEIGHT;

    std::string oldSpecularMap = def.specularmap;
    if (MapRow(editor, 1, "Specular map", x, y, width, def.specularmap))
    {
        changed = mapsChanged = true;
        if (oldSpecularMap.empty() && !def.specularmap.empty() && def.specular == 0.0f) def.specular = 1.0f;  // So the map shows
    }
    y += ROW_HEIGHT;
    if (NumberRow(2, "Specular", x, y, width, def.specular, 2.0f)) changed = true;
    y += ROW_HEIGHT;

    if (MapRow(editor, 3, "Gloss map", x, y, width, def.glossmap)) changed = mapsChanged = true;
    y += ROW_HEIGHT;
    if (NumberRow(4, "Gloss", x, y, width, def.gloss, 1.0f)) changed = true;
    y += ROW_HEIGHT;
    if (NumberRow(5, "Reflection", x, y, width, def.reflection, 1.0f)) changed = true;
    y += ROW_HEIGHT;

    y += DrawUITextWrapped("Specular is how shiny it is (0: not at all), times the specular map. Gloss is how sharp the "
                           "highlight is, times the gloss map. Reflection mirrors the nearest env_cubemap, masked by the "
                           "specular map", x, y, width, 14, UIMutedTextColor()) + 6;

    if (changed)
    {
        library[picked] = def;
        ShowChange(editor, mapsChanged);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) librarySaveWaiting = true;  // A slider drag: saved once it's let go
        else Save(editor);
        if (mapsChanged) scanned = false;  // A texture used as a map now leaves the list
    }
    return y - top;
}

void DrawMaterialsPanel(EditorState &editor, Rectangle area)
{
    if (!libraryLoaded)
    {
        library = LoadMaterialLibrary();
        libraryLoaded = true;
    }
    if (!scanned) ScanTextures(editor);
    if (librarySaveWaiting && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) Save(editor);

    const float x = area.x + PADDING;
    const float width = area.width - 2*PADDING;
    float y = area.y;

    DrawUIText(TextFormat("Saved to %s", MATERIALS_PATH), x, y + 6, 14, UIMutedTextColor());
    if (GuiButton({ x + width - 80, y, 80, ROW_HEIGHT - 4 }, "Refresh"))
    {
        library = LoadMaterialLibrary();
        for (auto &[path, texture] : thumbnails) if (texture.id != 0) UnloadTexture(texture);
        thumbnails.clear();
        ScanTextures(editor);
        ShowChange(editor, true);
        SetStatus(editor, "Reloaded the textures and %s", MATERIALS_PATH);
    }
    y += ROW_HEIGHT + 4;

    // The list scrolls inside the rest of the panel
    Rectangle list = { area.x, y, area.width, area.y + area.height - y };
    if (CheckCollisionPointRec(GetMousePosition(), list) && !GuiIsLocked()) scroll -= GetMouseWheelMove()*ROW_HEIGHT*3;
    if (scroll < 0.0f) scroll = 0.0f;

    BeginScissorMode((int)list.x, (int)list.y, (int)list.width, (int)list.height);
    float rowY = list.y - scroll;
    bool mouseInList = CheckCollisionPointRec(GetMousePosition(), list);
    bool wasLocked = GuiIsLocked();  // A dialog is open
    if (!mouseInList && !wasLocked) GuiLock();  // Rows scrolled out of sight can't be clicked

    if (folders.empty())
    {
        DrawUITextWrapped("No textures yet: put .png or .tga files in textures/, in subfolders for categories, then click Refresh",
                          x, rowY, width, 14, UIMutedTextColor());
    }

    int alignment = GuiGetStyle(BUTTON, TEXT_ALIGNMENT);
    for (TextureFolder &folder : folders)
    {
        // Folder header: click to fold it away
        GuiSetStyle(BUTTON, TEXT_ALIGNMENT, TEXT_ALIGN_LEFT);
        if (GuiButton({ x, rowY, width, ROW_HEIGHT - 4 }, TextFormat("%s %s  (%d)", folder.collapsed ? "+" : "-", folder.name.c_str(), (int)folder.textures.size())))
        {
            folder.collapsed = !folder.collapsed;
        }
        GuiSetStyle(BUTTON, TEXT_ALIGNMENT, alignment);
        rowY += ROW_HEIGHT;
        if (folder.collapsed) continue;

        for (const std::string &path : folder.textures)
        {
            bool isPicked = (path == picked);
            bool visible = rowY + ROW_HEIGHT > list.y && rowY < list.y + list.height;
            Rectangle row = { x + 8, rowY, width - 8, ROW_HEIGHT - 2 };
            if (visible)
            {
                bool hovered = mouseInList && CheckCollisionPointRec(GetMousePosition(), row);
                if (isPicked || hovered) DrawRectangleRec(row, Fade(GetColor(GuiGetStyle(BUTTON, BASE_COLOR_FOCUSED)), isPicked ? 0.9f : 0.5f));
                if (hovered && !GuiIsLocked() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                {
                    picked = isPicked ? "" : path;
                    activeField = -1;
                }

                Texture thumbnail = Thumbnail(path);
                Rectangle thumb = { row.x + 2, row.y + (row.height - THUMBNAIL_SIZE)/2, THUMBNAIL_SIZE, THUMBNAIL_SIZE };
                if (thumbnail.id != 0) DrawTexturePro(thumbnail, { 0, 0, (float)thumbnail.width, (float)thumbnail.height }, thumb, { 0, 0 }, 0.0f, WHITE);
                DrawUIText(GetFileName(path.c_str()), thumb.x + THUMBNAIL_SIZE + 8, row.y + (row.height - 16)/2, 16, UITextColor());

                // Which parts it has: B(ump), S(pecular), G(loss), R(eflection)
                MaterialDef def = FindMaterial(library, path);
                std::string parts;
                if (!def.bumpmap.empty()) parts += "B ";
                if (!def.specularmap.empty() || def.specular > 0.0f) parts += "S ";
                if (!def.glossmap.empty()) parts += "G ";
                if (def.reflection > 0.0f) parts += "R";
                if (!parts.empty()) DrawUIText(parts.c_str(), row.x + row.width - MeasureUIText(parts.c_str(), 14) - 6, row.y + (row.height - 14)/2, 14, UIMutedTextColor());
            }
            rowY += ROW_HEIGHT;

            if (isPicked) rowY += DrawMaterialFields(editor, x + 8, rowY + 2, width - 8) + 6;
        }
    }

    if (!wasLocked) GuiUnlock();
    EndScissorMode();

    // Stop scrolling past the end
    float contentHeight = rowY + scroll - list.y;
    float maxScroll = fmaxf(contentHeight - list.height, 0.0f);
    if (scroll > maxScroll) scroll = maxScroll;

    if (activeField >= 0) editor.textEditing = true;
}

std::string GetPickedMaterial()
{
    return picked;
}
