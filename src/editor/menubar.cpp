#include "editor.h"
#include "raygui.h"
#include "config.h"
#include "style.h"

static const float MENU_BUTTON_WIDTH = 64.0f;
static const float DROPDOWN_WIDTH = 230.0f;
static const float ITEM_HEIGHT = 28.0f;

struct MenuItem
{
    const char *label;                        // nullptr for a separator line
    const char *shortcut;                     // Shown on the right, or nullptr
    const char *(*status)();                  // Or text that changes, like "On"/"Off", shown in the same place
    bool (*enabled)(const EditorState &);     // Greyed out when this returns false. nullptr: always enabled
};

static const char *DarkModeStatus() { return IsDarkMode() ? "On" : "Off"; }

static const MenuItem FILE_ITEMS[] = {
    { "New Level", "Ctrl+N", nullptr },
    { "Open Level...", "Ctrl+O", nullptr },
    { "Save", "Ctrl+S", nullptr },
    { "Save As...", "Ctrl+Shift+S", nullptr },
    { nullptr, nullptr, nullptr },
    { "Exit Editor", nullptr, nullptr },
};

static const MenuItem EDIT_ITEMS[] = {
    { "Undo", "Ctrl+Z", nullptr, CanUndo },
    { "Redo", "Ctrl+Y", nullptr, CanRedo },
    { "Undo History...", nullptr, nullptr, nullptr },
    { nullptr, nullptr, nullptr, nullptr },
    { "Create Entity...", nullptr, nullptr, nullptr },
    { "Create World...", nullptr, nullptr, nullptr },
    { "Reload World Model", nullptr, nullptr, nullptr },
    { "Global World Settings...", nullptr, nullptr, nullptr },
    { nullptr, nullptr, nullptr, nullptr },
    { "Dark Mode", nullptr, DarkModeStatus, nullptr },
    { "Preferences...", nullptr, nullptr, nullptr },
};

static const MenuItem ABOUT_ITEMS[] = {
    { "Editor Info", nullptr, nullptr },
};

static const char *MENU_NAMES[] = { "", "File", "Edit", "About" };

static void GetMenuItems(MenuId menu, const MenuItem *&items, int &count)
{
    switch (menu)
    {
        case MENU_FILE: items = FILE_ITEMS; count = sizeof(FILE_ITEMS)/sizeof(FILE_ITEMS[0]); break;
        case MENU_EDIT: items = EDIT_ITEMS; count = sizeof(EDIT_ITEMS)/sizeof(EDIT_ITEMS[0]); break;
        case MENU_ABOUT: items = ABOUT_ITEMS; count = sizeof(ABOUT_ITEMS)/sizeof(ABOUT_ITEMS[0]); break;
        default: items = nullptr; count = 0; break;
    }
}

static Rectangle MenuButtonRect(MenuId menu)
{
    return { 4.0f + (menu - MENU_FILE)*MENU_BUTTON_WIDTH, 2.0f, MENU_BUTTON_WIDTH - 4.0f, MENU_BAR_HEIGHT - 4.0f };
}

static Rectangle DropdownRect(MenuId menu)
{
    const MenuItem *items;
    int count;
    GetMenuItems(menu, items, count);

    float height = 4.0f;
    for (int i = 0; i < count; i++) height += items[i].label ? ITEM_HEIGHT : 9.0f;
    Rectangle button = MenuButtonRect(menu);
    return { button.x, (float)MENU_BAR_HEIGHT, DROPDOWN_WIDTH, height };
}

static void RunMenuItem(EditorState &editor, MenuId menu, int item)
{
    if (menu == MENU_FILE)
    {
        switch (item)
        {
            case 0: RequestAction(editor, ACTION_NEW_LEVEL); break;
            case 1: RequestAction(editor, ACTION_OPEN_LEVEL); break;
            case 2: SaveCurrentLevel(editor, false); break;
            case 3: SaveCurrentLevel(editor, true); break;
            case 5: RequestAction(editor, ACTION_EXIT); break;
        }
    }
    else if (menu == MENU_EDIT)
    {
        switch (item)
        {
            case 0: Undo(editor); break;
            case 1: Redo(editor); break;
            case 2: OpenDialog(editor, DIALOG_UNDO_HISTORY); break;
            case 4: OpenDialog(editor, DIALOG_CREATE_ENTITY); break;
            case 5: OpenDialog(editor, DIALOG_CREATE_WORLD); break;
            case 6: ReloadWorld(editor); break;
            case 7: OpenDialog(editor, DIALOG_WORLD_SETTINGS); break;
            case 9: ToggleDarkMode(); break;
            case 10: OpenDialog(editor, DIALOG_PREFERENCES); break;
        }
    }
    else if (menu == MENU_ABOUT)
    {
        if (item == 0) OpenDialog(editor, DIALOG_EDITOR_INFO);
    }
}

void DrawMenuBar(EditorState &editor)
{
    DrawRectangle(0, 0, GetScreenWidth(), MENU_BAR_HEIGHT, GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));
    DrawLine(0, MENU_BAR_HEIGHT - 1, GetScreenWidth(), MENU_BAR_HEIGHT - 1, GetColor(GuiGetStyle(DEFAULT, LINE_COLOR)));

    for (int menu = MENU_FILE; menu <= MENU_ABOUT; menu++)
    {
        Rectangle rect = MenuButtonRect((MenuId)menu);
        bool open = (editor.openMenu == menu);

        // Toggles stay pressed while their menu is open
        bool pressed = open;
        GuiToggle(rect, MENU_NAMES[menu], &pressed);
        if (pressed != open) editor.openMenu = pressed ? (MenuId)menu : MENU_NONE;

        // Like most menu bars: while one menu is open, hovering another opens that one instead
        if (!GuiIsLocked() && editor.openMenu != MENU_NONE && editor.openMenu != menu && CheckCollisionPointRec(GetMousePosition(), rect))
        {
            editor.openMenu = (MenuId)menu;
        }
    }

    // Level name, and a * when there are unsaved changes
    const char *title = TextFormat("%s%s", editor.level.info.name.c_str(), editor.unsavedChanges ? " *" : "");
    float titleWidth = MeasureUIText(title, 16);
    DrawUIText(title, (GetScreenWidth() - titleWidth)/2, (MENU_BAR_HEIGHT - 16)/2.0f, 16, UITextColor());
}

void DrawMenuDropdown(EditorState &editor)
{
    if (editor.openMenu == MENU_NONE) return;

    MenuId menu = editor.openMenu;
    const MenuItem *items;
    int count;
    GetMenuItems(menu, items, count);
    Rectangle bounds = DropdownRect(menu);

    DrawRectangleRec(bounds, GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));
    DrawRectangleLinesEx(bounds, 1.0f, GetColor(GuiGetStyle(DEFAULT, LINE_COLOR)));

    int alignment = GuiGetStyle(BUTTON, TEXT_ALIGNMENT);
    GuiSetStyle(BUTTON, TEXT_ALIGNMENT, TEXT_ALIGN_LEFT);

    float y = bounds.y + 2.0f;
    int clicked = -1;
    for (int i = 0; i < count; i++)
    {
        if (items[i].label == nullptr)
        {
            DrawLine((int)bounds.x + 6, (int)y + 4, (int)(bounds.x + bounds.width) - 6, (int)y + 4, GetColor(GuiGetStyle(DEFAULT, LINE_COLOR)));
            y += 9.0f;
            continue;
        }

        Rectangle item = { bounds.x + 2.0f, y, bounds.width - 4.0f, ITEM_HEIGHT - 2.0f };
        bool enabled = !items[i].enabled || items[i].enabled(editor);
        if (!enabled) GuiDisable();
        if (GuiButton(item, TextFormat("  %s", items[i].label))) clicked = i;
        GuiEnable();
        const char *rightText = items[i].status ? items[i].status() : items[i].shortcut;
        if (rightText)
        {
            float width = MeasureUIText(rightText, 14);
            DrawUIText(rightText, item.x + item.width - width - 8, item.y + (item.height - 14)/2, 14, UIMutedTextColor());
        }
        y += ITEM_HEIGHT;
    }

    GuiSetStyle(BUTTON, TEXT_ALIGNMENT, alignment);

    if (clicked >= 0)
    {
        editor.openMenu = MENU_NONE;
        RunMenuItem(editor, menu, clicked);
    }
    else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        // Clicking anywhere outside the menu closes it
        Rectangle bar = { 0.0f, 0.0f, (float)GetScreenWidth(), (float)MENU_BAR_HEIGHT };
        if (!CheckCollisionPointRec(GetMousePosition(), bounds) && !CheckCollisionPointRec(GetMousePosition(), bar)) editor.openMenu = MENU_NONE;
    }
}
