// Level editor for Raylib Quake Clone: a separate program from the game, built to build/editor.exe

#include "raylib.h"
#include "raygui.h"

#include "config.h"
#include "editor.h"
#include "entitylist.h"
#include "style.h"

// The project folder, which has maps/ and model/ in it. The editor runs from build/, so it looks
// in its own folder and then the ones above it
static std::string FindDataRoot()
{
    std::string folder = GetApplicationDirectory();
    for (int i = 0; i < 4; i++)
    {
        if (DirectoryExists((folder + "/maps").c_str()) && DirectoryExists((folder + "/model").c_str())) return folder;
        folder = GetPrevDirectoryPath(folder.c_str());
    }

    TraceLog(LOG_WARNING, "EDITOR: Couldn't find the project folder with maps/ and model/, using the editor's own folder");
    return GetApplicationDirectory();
}

static void DrawStatusBar(const EditorState &editor)
{
    const int y = GetScreenHeight() - STATUS_BAR_HEIGHT;
    DrawRectangle(0, y, GetScreenWidth(), STATUS_BAR_HEIGHT, GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));
    DrawLine(0, y, GetScreenWidth(), y, GetColor(GuiGetStyle(DEFAULT, LINE_COLOR)));

    // Recent messages, then a reminder of the controls
    bool recent = !editor.status.empty() && GetTime() - editor.statusTime < 6.0;
    const char *text = recent ? editor.status.c_str()
                              : "Hold right mouse: look around, with WASD/Q/E to move (Shift: faster) | Left click: select | Delete: delete entity";
    DrawUIText(text, 8, (float)(y + (STATUS_BAR_HEIGHT - 16)/2), 16, recent ? UITextColor() : UIMutedTextColor());

    const char *count = TextFormat("%d entities", (int)editor.level.entities.size());
    DrawUIText(count, GetScreenWidth() - MeasureUIText(count, 16) - 8, (float)(y + (STATUS_BAR_HEIGHT - 16)/2), 16, UIMutedTextColor());
}

// Keys run whatever editor.cfg binds them to. Esc isn't bindable, like in the game: it closes menus
// and deselects
static void HandleKeys(EditorState &editor)
{
    UpdateEditorBinds(editor);

    if (IsKeyPressed(KEY_ESCAPE))
    {
        if (editor.openMenu != MENU_NONE) editor.openMenu = MENU_NONE;
        else if (editor.pointing) CancelPointAt(editor);
        else editor.selected = -1;
    }
}

int main()
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1600, 900, EDITOR_NAME);
    SetWindowMinSize(1000, 600);
    SetExitKey(KEY_NULL);  // Esc closes menus and dialogs instead
    SetTargetFPS(144);

    // Levels refer to models and textures relative to the project folder, so work from there
    std::string dataRoot = FindDataRoot();
    ChangeDirectory(dataRoot.c_str());

    LoadEditorStyle();  // Roboto and the light style

    EditorState editor;
    InitEditor(editor, dataRoot);
    LoadEditorConfig(editor);  // Binds and settings, from editor.cfg next to the editor

    // Every entity type the game has. build.bat writes this from the game's own entity table
    if (!LoadEntityList("entitylist.txt")) SetStatus(editor, "entitylist.txt is missing, so no entities can be created. Build the game with build.bat");

    std::string windowTitle;
    while (!editor.quit)
    {
        if (WindowShouldClose()) RequestAction(editor, ACTION_EXIT);
        UpdateConfigSaving();

        // Keys are off while typing or in a dialog
        if (editor.dialog == DIALOG_NONE && !editor.textEditing) HandleKeys(editor);
        else ReleaseEditorBinds();
        editor.textEditing = false;  // Set again while drawing if a text field still has the keyboard

        if (editor.dialog == DIALOG_NONE) UpdateViewport(editor);

        BeginDrawing();
            ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));
            DrawViewport(editor);

            // Only the topmost thing takes clicks: a dialog, then an open menu, then everything else
            if (editor.dialog != DIALOG_NONE || editor.openMenu != MENU_NONE) GuiLock();
            DrawDetailsPanel(editor);
            DrawStatusBar(editor);

            if (editor.dialog == DIALOG_NONE) GuiUnlock();
            DrawMenuBar(editor);
            DrawMenuDropdown(editor);
            GuiUnlock();

            DrawDialog(editor);
        EndDrawing();

        std::string title = std::string(EDITOR_NAME) + " - " + editor.level.info.name + (editor.unsavedChanges ? " *" : "");
        if (title != windowTitle)
        {
            SetWindowTitle(title.c_str());
            windowTitle = title;
        }
    }

    SaveEditorConfig();
    ShutdownEditor(editor);
    UnloadViewport();
    UnloadEditorStyle();
    CloseWindow();
    return 0;
}
