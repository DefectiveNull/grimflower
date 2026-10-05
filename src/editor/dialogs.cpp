#include "editor.h"
#include "entitylist.h"
#include "config.h"
#include "filedialog.h"
#include "raygui.h"
#include "raymath.h"
#include "style.h"

#include <cstdio>
#include <cstdlib>

static const float TITLE_BAR_HEIGHT = 24.0f;
static const float PADDING = 14.0f;
static const float ROW_HEIGHT = 30.0f;
static const float LABEL_WIDTH = 120.0f;

// Text being typed into the current dialog's fields
static char modelInput[256];
static char scaleInput[32];
static char nameInput[128];
static char skyboxInput[256];

// Which text field has the keyboard: one at a time, -1 for none
static int activeField = -1;

// Preferences: which tab is open, and which bind (if any) is waiting for a key to be pressed
static int preferencesTab = 0;
static int bindScroll = 0;
static int capturingBind = -1;

// A text box that takes the keyboard when clicked, and lets go on Enter or a click elsewhere
static void TextField(int id, Rectangle bounds, char *text, int size)
{
    if (GuiTextBox(bounds, text, size, activeField == id)) activeField = (activeField == id) ? -1 : id;
}

void OpenDialog(EditorState &editor, DialogId dialog)
{
    editor.dialog = dialog;
    editor.openMenu = MENU_NONE;
    activeField = -1;

    const Level &level = editor.level;
    snprintf(modelInput, sizeof(modelInput), "%s", level.worldModel.empty() ? "model/" : level.worldModel.c_str());
    snprintf(scaleInput, sizeof(scaleInput), "%g", level.worldScale);
    snprintf(nameInput, sizeof(nameInput), "%s", level.info.name.c_str());
    snprintf(skyboxInput, sizeof(skyboxInput), "%s", level.info.skybox.c_str());
}

static void CloseDialog(EditorState &editor)
{
    editor.dialog = DIALOG_NONE;
    activeField = -1;
}

// Centered window. Returns its bounds; closed is set when its X button is clicked
static Rectangle DialogWindow(const char *title, float width, float height, bool &closed)
{
    Rectangle bounds = { (GetScreenWidth() - width)/2.0f, (GetScreenHeight() - height)/2.0f, width, height };
    closed = GuiWindowBox(bounds, title);
    return bounds;
}

// Rows of label + content, top to bottom inside a dialog
struct Layout
{
    Rectangle bounds;
    float y;

    Rectangle Label() const { return { bounds.x + PADDING, y, LABEL_WIDTH, ROW_HEIGHT - 4.0f }; }
    Rectangle Field(float rightMargin = 0.0f) const
    {
        return { bounds.x + PADDING + LABEL_WIDTH, y, bounds.width - 2*PADDING - LABEL_WIDTH - rightMargin, ROW_HEIGHT - 4.0f };
    }
    void Next(float height = ROW_HEIGHT) { y += height; }
};

static Layout StartLayout(Rectangle bounds)
{
    return { bounds, bounds.y + TITLE_BAR_HEIGHT + PADDING };
}

// Buttons along the bottom right of a dialog, right to left
static Rectangle BottomButton(Rectangle bounds, int indexFromRight)
{
    const float width = 110.0f;
    const float height = 30.0f;
    return { bounds.x + bounds.width - PADDING - (indexFromRight + 1)*width - indexFromRight*8.0f,
             bounds.y + bounds.height - PADDING - height, width, height };
}

static void DrawCreateEntityDialog(EditorState &editor)
{
    // Every entity type the game has, from entitylist.txt
    const std::vector<EntityClassInfo> &classes = GetEntityClassList();
    const float rowHeight = 50.0f;

    bool closed;
    float height = TITLE_BAR_HEIGHT + 2*PADDING + 28 + (classes.empty() ? 60 : classes.size()*rowHeight) + 50;
    Rectangle bounds = DialogWindow("Create Entity", 680, height, closed);
    Layout layout = StartLayout(bounds);

    if (classes.empty())
    {
        GuiLabel({ bounds.x + PADDING, layout.y, bounds.width - 2*PADDING, 20 }, "No entity types: entitylist.txt is missing or empty.");
        GuiLabel({ bounds.x + PADDING, layout.y + 24, bounds.width - 2*PADDING, 20 }, "Build the game with build.bat to write it. The editor picks it up by itself.");
    }
    else
    {
        GuiLabel({ bounds.x + PADDING, layout.y, bounds.width - 2*PADDING, 20 }, "Placed where the middle of the view meets the level:");
        layout.Next(28);

        for (const EntityClassInfo &entityClass : classes)
        {
            Rectangle button = { bounds.x + PADDING, layout.y, 170, 30 };
            if (GuiButton(button, entityClass.classname.c_str()))
            {
                CreateEntity(editor, entityClass.classname.c_str());
                CloseDialog(editor);
                return;
            }
            float textX = button.x + button.width + 12;
            DrawUITextWrapped(entityClass.description.c_str(), textX, layout.y + 6, bounds.x + bounds.width - PADDING - textX, 15, UITextColor());
            layout.Next(rowHeight);
        }
    }

    if (GuiButton(BottomButton(bounds, 0), "Cancel") || closed) CloseDialog(editor);
}

static void DrawCreateWorldDialog(EditorState &editor)
{
    bool closed;
    Rectangle bounds = DialogWindow("Create World", 620, 200, closed);
    Layout layout = StartLayout(bounds);

    GuiLabel({ bounds.x + PADDING, layout.y, bounds.width - 2*PADDING, 20 },
             "OBJ used for drawing and collision. Each object in it must be convex");
    layout.Next(30);

    GuiLabel(layout.Label(), "World model");
    TextField(0, layout.Field(100), modelInput, sizeof(modelInput));
    Rectangle browse = { layout.Field().x + layout.Field().width - 94, layout.y, 94, ROW_HEIGHT - 4 };
    if (GuiButton(browse, "Browse..."))
    {
        std::string path;
        if (ShowOpenFileDialog(GetWindowHandle(), "Choose World Model", "OBJ models (*.obj)\0*.obj\0", editor.dataRoot + "/model", path))
        {
            snprintf(modelInput, sizeof(modelInput), "%s", ToDataPath(editor, path).c_str());
        }
    }
    layout.Next();

    GuiLabel(layout.Label(), "Scale");
    TextField(1, layout.Field(250), scaleInput, sizeof(scaleInput));
    GuiLabel({ layout.Field(250).x + layout.Field(250).width + 10, layout.y, 240, ROW_HEIGHT - 4 }, "0.03125 = Quake units");
    layout.Next();

    if (GuiButton(BottomButton(bounds, 1), "Create"))
    {
        float scale = (float)atof(scaleInput);
        if (scale <= 0.0f) SetStatus(editor, "Scale has to be a number above 0");
        else if (CreateWorld(editor, modelInput, scale)) CloseDialog(editor);
    }
    if (GuiButton(BottomButton(bounds, 0), "Cancel") || closed) CloseDialog(editor);
}

static void DrawWorldSettingsDialog(EditorState &editor)
{
    const Level &level = editor.level;

    bool closed;
    Rectangle bounds = DialogWindow("Global World Settings", 560, 320, closed);
    Layout layout = StartLayout(bounds);

    GuiLabel(layout.Label(), "Map name");
    TextField(0, layout.Field(), nameInput, sizeof(nameInput));
    layout.Next();

    GuiLabel(layout.Label(), "Skybox");
    TextField(1, layout.Field(100), skyboxInput, sizeof(skyboxInput));
    Rectangle browse = { layout.Field().x + layout.Field().width - 94, layout.y, 94, ROW_HEIGHT - 4 };
    if (GuiButton(browse, "Browse..."))
    {
        std::string path;
        if (ShowOpenFileDialog(GetWindowHandle(), "Choose Skybox", "Images (*.png;*.tga;*.jpg;*.hdr)\0*.png;*.tga;*.jpg;*.hdr\0All files (*.*)\0*.*\0",
                               editor.dataRoot + "/textures", path))
        {
            snprintf(skyboxInput, sizeof(skyboxInput), "%s", ToDataPath(editor, path).c_str());
        }
    }
    layout.Next();

    // Kept up to date by the editor, so shown but not editable
    GuiLine({ bounds.x + PADDING, layout.y, bounds.width - 2*PADDING, 16 }, nullptr);
    layout.Next(20);

    GuiLabel(layout.Label(), "Created");
    GuiLabel(layout.Field(), level.info.created.empty() ? "Not saved yet" : level.info.created.c_str());
    layout.Next();

    GuiLabel(layout.Label(), "Last saved");
    GuiLabel(layout.Field(), level.info.saved.empty() ? "Not saved yet" : level.info.saved.c_str());
    layout.Next();

    GuiLabel(layout.Label(), "Entities");
    GuiLabel(layout.Field(), TextFormat("%d placed", (int)level.entities.size()));
    layout.Next();

    GuiLabel(layout.Label(), "World model");
    GuiLabel(layout.Field(), level.worldModel.empty() ? "None, see Edit > Create World" : TextFormat("%s at scale %g", level.worldModel.c_str(), level.worldScale));
    layout.Next();

    if (GuiButton(BottomButton(bounds, 1), "Apply"))
    {
        if (editor.level.info.name != nameInput || editor.level.info.skybox != skyboxInput)
        {
            RecordUndo(editor, "Change world settings");
            editor.level.info.name = nameInput;
            editor.level.info.skybox = skyboxInput;
            SetStatus(editor, "World settings changed");
        }
        CloseDialog(editor);
    }
    if (GuiButton(BottomButton(bounds, 0), "Cancel") || closed) CloseDialog(editor);
}

static int historyScroll = 0;

static void DrawUndoHistoryDialog(EditorState &editor)
{
    bool closed;
    Rectangle bounds = DialogWindow("Undo History", 440, 480, closed);
    Layout layout = StartLayout(bounds);

    GuiLabel({ bounds.x + PADDING, layout.y, bounds.width - 2*PADDING, 20 }, "Click a step to go back or forward to it");
    layout.Next(28);

    // The start, then every change. Undone changes stay listed after the current one, so they can be redone
    std::vector<std::string> entries;
    entries.push_back("Start of history");
    for (const UndoStep &step : editor.undoSteps) entries.push_back(step.description);
    for (auto it = editor.redoSteps.rbegin(); it != editor.redoSteps.rend(); ++it) entries.push_back("(undone) " + it->description);

    std::vector<char *> text;
    for (std::string &entry : entries) text.push_back(&entry[0]);

    Rectangle list = { bounds.x + PADDING, layout.y, bounds.width - 2*PADDING, bounds.y + bounds.height - layout.y - PADDING - 44 };
    int position = GetHistoryPosition(editor);
    int active = position;
    int focus = -1;
    GuiListViewEx(list, text.data(), (int)text.size(), &historyScroll, &active, &focus);
    if (active >= 0 && active != position) JumpToHistory(editor, active);

    if (GuiButton(BottomButton(bounds, 0), "Close") || closed) CloseDialog(editor);
}

// A row with a slider and its value. Returns true when the value changed
static bool SliderRow(Layout &layout, const char *label, float *value, float min, float max, const char *format)
{
    GuiLabel(layout.Label(), label);
    Rectangle field = layout.Field(80);
    float before = *value;
    GuiSliderBar({ field.x, field.y + 7, field.width, field.height - 14 }, nullptr, nullptr, value, min, max);
    GuiLabel({ field.x + field.width + 12, field.y, 70, field.height }, TextFormat(format, *value));
    layout.Next(ROW_HEIGHT + 6);
    return *value != before;
}

// A row with a checkbox. Returns true when it was clicked
static bool CheckRow(Layout &layout, const char *label, bool *value)
{
    GuiLabel(layout.Label(), label);
    Rectangle field = layout.Field();
    bool before = *value;
    GuiCheckBox({ field.x, field.y + (field.height - 20)/2, 20, 20 }, "", value);
    layout.Next(ROW_HEIGHT + 6);
    return *value != before;
}

static void DrawGeneralPreferences(Layout &layout)
{
    bool dark = IsDarkMode();
    if (CheckRow(layout, "Dark mode", &dark)) ToggleDarkMode();

    float undoSteps = (float)editorSettings.undoSteps;
    if (SliderRow(layout, "Undo steps", &undoSteps, 10.0f, 1000.0f, "%.0f"))
    {
        editorSettings.undoSteps = (int)undoSteps;
        MarkSettingsChanged();
    }
}

static void DrawViewPreferences(Layout &layout)
{
    if (SliderRow(layout, "Field of view", &editorSettings.fieldOfView, 40.0f, 110.0f, "%.0f deg")) MarkSettingsChanged();
    if (SliderRow(layout, "Camera speed", &editorSettings.moveSpeed, 1.0f, 40.0f, "%.1f")) MarkSettingsChanged();
    if (CheckRow(layout, "Show grid", &editorSettings.showGrid)) MarkSettingsChanged();
    if (CheckRow(layout, "Show labels", &editorSettings.showLabels)) MarkSettingsChanged();
}

static void DrawInputPreferences(EditorState &editor, Layout &layout)
{
    // Sensitivity is radians per pixel, shown x1000 so the numbers read like 3 instead of 0.003
    float sensitivity = editorSettings.lookSensitivity*1000.0f;
    if (SliderRow(layout, "Look sensitivity", &sensitivity, 0.5f, 10.0f, "%.1f"))
    {
        editorSettings.lookSensitivity = sensitivity/1000.0f;
        MarkSettingsChanged();
    }
    if (CheckRow(layout, "Invert look", &editorSettings.invertLook)) MarkSettingsChanged();

    GuiLine({ layout.bounds.x + PADDING, layout.y, layout.bounds.width - 2*PADDING, 20 }, "Key binds");
    layout.Next(26);

    // One row per command: what it does, its keys, then Change and Clear. Scrolls with the mouse wheel
    std::vector<BindableCommand> commands = GetBindableCommands();
    const float rowHeight = 30.0f;
    Rectangle list = { layout.bounds.x + PADDING, layout.y, layout.bounds.width - 2*PADDING,
                       layout.bounds.y + layout.bounds.height - layout.y - PADDING - 48 };
    int visible = (int)(list.height/rowHeight);
    int maxScroll = (int)commands.size() - visible;
    if (CheckCollisionPointRec(GetMousePosition(), list)) bindScroll -= (int)GetMouseWheelMove();
    if (bindScroll > maxScroll) bindScroll = maxScroll;
    if (bindScroll < 0) bindScroll = 0;

    const float buttonWidth = 64.0f;
    const float labelWidth = 200.0f;
    for (int i = bindScroll; i < (int)commands.size() && i < bindScroll + visible; i++)
    {
        float y = list.y + (i - bindScroll)*rowHeight;
        const BindableCommand &command = commands[i];

        GuiLabel({ list.x, y, labelWidth, rowHeight - 4 }, command.label);

        Rectangle keys = { list.x + labelWidth, y, list.width - labelWidth - 2*buttonWidth - 12, rowHeight - 4 };
        if (capturingBind == i)
        {
            DrawUIText("Press a key... (Esc cancels)", keys.x, keys.y + 6, 15, ORANGE);

            std::string combo;
            if (CaptureKeyCombo(combo))
            {
                BindCommand(command.name, combo);
                SetStatus(editor, "%s is now bound to %s", command.label, combo.c_str());
                capturingBind = -1;
            }
        }
        else
        {
            std::string bound = GetCommandBinds(command.name);
            DrawUIText(bound.empty() ? "(none)" : bound.c_str(), keys.x, keys.y + 6, 15, bound.empty() ? UIMutedTextColor() : UITextColor());
        }

        if (GuiButton({ keys.x + keys.width + 6, y, buttonWidth, rowHeight - 4 }, "Change")) capturingBind = i;
        if (GuiButton({ keys.x + keys.width + 12 + buttonWidth, y, buttonWidth, rowHeight - 4 }, "Clear"))
        {
            UnbindCommand(command.name);
            MarkSettingsChanged();
            capturingBind = -1;
        }
    }

    if (GuiButton({ layout.bounds.x + PADDING, layout.bounds.y + layout.bounds.height - PADDING - 30, 160, 30 }, "Reset to Defaults"))
    {
        ResetBindsToDefaults();
        capturingBind = -1;
        SetStatus(editor, "Key binds reset to the defaults");
    }
}

static void DrawPreferencesDialog(EditorState &editor)
{
    bool closed;
    Rectangle bounds = DialogWindow("Preferences", 620, 600, closed);
    Layout layout = StartLayout(bounds);

    // Tabs
    int tab = preferencesTab;
    GuiToggleGroup({ bounds.x + PADDING, layout.y, 120, 30 }, "General;View;Input", &preferencesTab);
    if (preferencesTab != tab) capturingBind = -1;
    layout.Next(44);

    switch (preferencesTab)
    {
        case 0: DrawGeneralPreferences(layout); break;
        case 1: DrawViewPreferences(layout); break;
        case 2: DrawInputPreferences(editor, layout); break;
    }

    DrawUIText("Saved to editor.cfg as you change them", bounds.x + PADDING + (preferencesTab == 2 ? 176 : 0),
               bounds.y + bounds.height - PADDING - 22, 14, UIMutedTextColor());

    if (GuiButton(BottomButton(bounds, 0), "Close") || closed)
    {
        capturingBind = -1;
        CloseDialog(editor);
    }
}

// Placeholder for the raylib logo: a black square outline with "raylib" in the bottom right
static void DrawRaylibLogo(float x, float y, float size)
{
    float border = size/16.0f;
    DrawRectangle((int)x, (int)y, (int)size, (int)size, BLACK);
    DrawRectangle((int)(x + border), (int)(y + border), (int)(size - 2*border), (int)(size - 2*border), RAYWHITE);

    int fontSize = (int)(size*0.22f);
    float width = MeasureUIText("raylib", (float)fontSize);
    DrawUIText("raylib", x + size - 2*border - width, y + size - 2*border - fontSize, (float)fontSize, BLACK);
}

static void DrawEditorInfoDialog(EditorState &editor)
{
    bool closed;
    Rectangle bounds = DialogWindow("Editor Info", 600, 330, closed);
    Layout layout = StartLayout(bounds);

    const float logoSize = 128.0f;
    DrawRaylibLogo(bounds.x + PADDING, layout.y, logoSize);

    float textX = bounds.x + PADDING + logoSize + 20;
    float textWidth = bounds.width - (textX - bounds.x) - PADDING;
    DrawUIText(EDITOR_NAME, textX, layout.y, 22, UITextColor());
    GuiLabel({ textX, layout.y + 26, textWidth, 20 }, "Version " EDITOR_VERSION);
    GuiLabel({ textX, layout.y + 50, textWidth, 20 }, TextFormat("raylib %s, raygui %s", RAYLIB_VERSION, RAYGUI_VERSION));
    GuiLabel({ textX, layout.y + 74, textWidth, 20 }, "Created by Claude Opus 5.5");

    const char *about[] = {
        "A level editor for Grimflower. Levels are JSON files",
        "in maps/ that say which world model to use, where entities like",
        "the player start, the sun and lights go, and map-wide settings",
        "like the name and skybox. Levels are drawn with the game's lighting.",
    };
    float y = layout.y + logoSize + 16;
    for (const char *line : about)
    {
        GuiLabel({ bounds.x + PADDING, y, bounds.width - 2*PADDING, 20 }, line);
        y += 20;
    }

    if (GuiButton(BottomButton(bounds, 0), "Close") || closed) CloseDialog(editor);
}

static void DrawUnsavedChangesDialog(EditorState &editor)
{
    bool closed;
    Rectangle bounds = DialogWindow("Unsaved Changes", 460, 170, closed);
    Layout layout = StartLayout(bounds);

    GuiLabel({ bounds.x + PADDING, layout.y, bounds.width - 2*PADDING, 24 },
             TextFormat("\"%s\" has unsaved changes. Save them first?", editor.level.info.name.c_str()));

    // Close the dialog before running the action, since the action may open a dialog of its own
    PendingAction action = editor.pendingAction;
    if (GuiButton(BottomButton(bounds, 2), "Save"))
    {
        CloseDialog(editor);
        if (SaveCurrentLevel(editor, false)) RunAction(editor, action);
    }
    else if (GuiButton(BottomButton(bounds, 1), "Don't Save"))
    {
        CloseDialog(editor);
        RunAction(editor, action);
    }
    else if (GuiButton(BottomButton(bounds, 0), "Cancel") || closed)
    {
        CloseDialog(editor);
    }
}

// The color picker, for an entity's color parameter. The color changes live while picking, so the
// lighting updates in the viewport; OK keeps it as one undo step, Cancel puts back what was there
static int pickerEntity = -1;
static std::string pickerParameter;
static bool pickerHadValue;                    // Whether the level file had this parameter set
static nlohmann::ordered_json pickerOriginal;  // Its value before picking, if it had one
static Color pickerOriginalColor;
static Vector3 pickerHsv;                      // Kept as hue/saturation/value, so the hue doesn't drift on greys
static char rgbInput[32];

static LevelEntity *PickerEntity(EditorState &editor)
{
    if (pickerEntity < 0 || pickerEntity >= (int)editor.level.entities.size()) return nullptr;
    return &editor.level.entities[pickerEntity];
}

static void SetPickerValue(EditorState &editor, Color color)
{
    LevelEntity *entity = PickerEntity(editor);
    if (entity) entity->properties[pickerParameter] = { color.r, color.g, color.b };
}

static void RestorePickerColor(EditorState &editor)
{
    LevelEntity *entity = PickerEntity(editor);
    if (entity == nullptr) return;
    if (pickerHadValue) entity->properties[pickerParameter] = pickerOriginal;
    else entity->properties.erase(pickerParameter);
}

void OpenColorPicker(EditorState &editor, int entityIndex, const std::string &parameter)
{
    if (entityIndex < 0 || entityIndex >= (int)editor.level.entities.size()) return;
    const LevelEntity &entity = editor.level.entities[entityIndex];

    OpenDialog(editor, DIALOG_COLOR_PICKER);
    pickerEntity = entityIndex;
    pickerParameter = parameter;
    pickerHadValue = entity.properties.contains(parameter);
    pickerOriginal = pickerHadValue ? entity.properties[parameter] : nlohmann::ordered_json();
    pickerOriginalColor = EntityColorOrDefault(entity, parameter.c_str(), WHITE);
    pickerHsv = ColorToHSV(pickerOriginalColor);
    snprintf(rgbInput, sizeof(rgbInput), "%d %d %d", pickerOriginalColor.r, pickerOriginalColor.g, pickerOriginalColor.b);
}

static void DrawColorPickerDialog(EditorState &editor)
{
    LevelEntity *entity = PickerEntity(editor);
    if (entity == nullptr)
    {
        CloseDialog(editor);
        return;
    }

    bool closed;
    Rectangle bounds = DialogWindow(TextFormat("%s %s", entity->classname.c_str(), pickerParameter.c_str()), 380, 400, closed);
    Layout layout = StartLayout(bounds);

    // Saturation/value square, with the hue bar to its right
    GuiColorPickerHSV({ bounds.x + PADDING, layout.y, bounds.width - 2*PADDING - 34, 230 }, nullptr, &pickerHsv);
    Color color = ColorFromHSV(pickerHsv.x, pickerHsv.y, pickerHsv.z);
    layout.Next(244);

    // Before and after, side by side
    Rectangle oldSwatch = { bounds.x + PADDING, layout.y, 50, 30 };
    Rectangle newSwatch = { oldSwatch.x + 50, layout.y, 50, 30 };
    DrawRectangleRec(oldSwatch, pickerOriginalColor);
    DrawRectangleRec(newSwatch, color);
    DrawRectangleLinesEx({ oldSwatch.x, oldSwatch.y, 100, 30 }, 1.0f, GetColor(GuiGetStyle(DEFAULT, LINE_COLOR)));

    // Exact values, typed as "r g b"
    bool wasTyping = (activeField == 0);
    GuiLabel({ newSwatch.x + 64, layout.y, 40, 30 }, "RGB");
    TextField(0, { newSwatch.x + 100, layout.y, bounds.x + bounds.width - PADDING - newSwatch.x - 100, 30 }, rgbInput, sizeof(rgbInput));
    if (wasTyping && activeField != 0)
    {
        int r, g, b;
        if (sscanf(rgbInput, "%d %d %d", &r, &g, &b) == 3)
        {
            color = { (unsigned char)Clamp((float)r, 0, 255), (unsigned char)Clamp((float)g, 0, 255), (unsigned char)Clamp((float)b, 0, 255), 255 };
            pickerHsv = ColorToHSV(color);
        }
        else
        {
            SetStatus(editor, "Type the color as three numbers from 0 to 255, like 255 150 60");
        }
    }
    else if (activeField != 0)
    {
        snprintf(rgbInput, sizeof(rgbInput), "%d %d %d", color.r, color.g, color.b);  // Follows the picker unless being typed in
    }
    SetPickerValue(editor, color);  // Live, so the viewport shows it while picking

    if (GuiButton(BottomButton(bounds, 1), "OK"))
    {
        // Put the old value back, then make the change as one undo step
        RestorePickerColor(editor);
        if (color.r != pickerOriginalColor.r || color.g != pickerOriginalColor.g || color.b != pickerOriginalColor.b)
        {
            RecordUndo(editor, "Change " + entity->classname + " " + pickerParameter);
            SetPickerValue(editor, color);
        }
        CloseDialog(editor);
    }
    else if (GuiButton(BottomButton(bounds, 0), "Cancel") || closed)
    {
        RestorePickerColor(editor);
        CloseDialog(editor);
    }
}

void DrawDialog(EditorState &editor)
{
    if (editor.dialog == DIALOG_NONE) return;

    // Darken everything behind the dialog
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.35f));

    // Esc closes the dialog, unless it's being used to stop typing in a field. For the color picker
    // it's the same as Cancel, so the color goes back to what it was
    // While Preferences waits for a key to bind, Esc only stops waiting
    if (IsKeyPressed(KEY_ESCAPE) && capturingBind >= 0)
    {
        capturingBind = -1;
    }
    else if (IsKeyPressed(KEY_ESCAPE) && activeField < 0)
    {
        if (editor.dialog == DIALOG_COLOR_PICKER) RestorePickerColor(editor);
        CloseDialog(editor);
        return;
    }

    switch (editor.dialog)
    {
        case DIALOG_CREATE_ENTITY: DrawCreateEntityDialog(editor); break;
        case DIALOG_CREATE_WORLD: DrawCreateWorldDialog(editor); break;
        case DIALOG_WORLD_SETTINGS: DrawWorldSettingsDialog(editor); break;
        case DIALOG_PREFERENCES: DrawPreferencesDialog(editor); break;
        case DIALOG_EDITOR_INFO: DrawEditorInfoDialog(editor); break;
        case DIALOG_UNSAVED_CHANGES: DrawUnsavedChangesDialog(editor); break;
        case DIALOG_UNDO_HISTORY: DrawUndoHistoryDialog(editor); break;
        case DIALOG_COLOR_PICKER: DrawColorPickerDialog(editor); break;
        default: break;
    }

    if (activeField >= 0) editor.textEditing = true;
}
