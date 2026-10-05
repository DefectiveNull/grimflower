#include "editor.h"
#include "raygui.h"
#include "entitylist.h"
#include "style.h"

#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

static const float PADDING = 10.0f;
static const float ROW_HEIGHT = 28.0f;
static const float LABEL_WIDTH = 96.0f;
static const float HEADER_HEIGHT = 24.0f;  // GuiPanel's title bar

// The field being typed in, and what's been typed so far. One at a time, -1 for none
static int activeField = -1;
static char editBuffer[256];
static int editedEntity = -1;  // Fields are reset when the selection changes

static int listScroll = 0;     // First entity shown in the list
static int sliderDrag = -1;    // Slider being dragged, so a whole drag is one undo step

// A text box showing value. Clicking it starts editing; Enter or clicking elsewhere finishes.
// Returns true when editing finishes, with the typed text in editBuffer
static bool EditableField(int id, Rectangle bounds, const std::string &value)
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

static Rectangle PanelRect()
{
    return { (float)(GetScreenWidth() - DETAILS_PANEL_WIDTH), (float)MENU_BAR_HEIGHT,
             (float)DETAILS_PANEL_WIDTH, (float)(GetScreenHeight() - MENU_BAR_HEIGHT - STATUS_BAR_HEIGHT) };
}

static void DrawEntityDetails(EditorState &editor, Rectangle panel, float y)
{
    LevelEntity &entity = editor.level.entities[editor.selected];
    const EntityClassInfo *entityClass = FindEntityClass(entity.classname);
    const float x = panel.x + PADDING;
    const float width = panel.width - 2*PADDING;
    const float fieldX = x + LABEL_WIDTH;
    const float fieldWidth = width - LABEL_WIDTH;
    const Color warning = { 230, 41, 55, 255 };
    std::string help;  // Description of the parameter under the mouse, shown at the bottom

    GuiLabel({ x, y, width, ROW_HEIGHT }, TextFormat("Entity %d of %d", editor.selected + 1, (int)editor.level.entities.size()));
    y += ROW_HEIGHT;

    GuiLabel({ x, y, LABEL_WIDTH, ROW_HEIGHT }, "Class");
    GuiLabel({ fieldX, y, fieldWidth, ROW_HEIGHT }, entity.classname.c_str());
    y += ROW_HEIGHT;

    // What it is, from entitylist.txt
    if (entityClass) y += DrawUITextWrapped(entityClass->description.c_str(), x, y, width, 14, UIMutedTextColor());
    else y += DrawUITextWrapped("Not in entitylist.txt, so the game doesn't know this classname and will skip it", x, y, width, 14, warning);
    y += 6;

    // Origin, one field per axis
    GuiLabel({ x, y, LABEL_WIDTH, ROW_HEIGHT - 4 }, "Origin");
    float *axes[3] = { &entity.origin.x, &entity.origin.y, &entity.origin.z };
    float axisWidth = (fieldWidth - 8)/3.0f;
    for (int axis = 0; axis < 3; axis++)
    {
        Rectangle field = { fieldX + axis*(axisWidth + 4), y, axisWidth, ROW_HEIGHT - 4 };
        if (EditableField(axis, field, TextFormat("%g", *axes[axis])))
        {
            char *end;
            float value = strtof(editBuffer, &end);
            if (end == editBuffer) SetStatus(editor, "\"%s\" isn't a number", editBuffer);
            else if (value != *axes[axis])
            {
                RecordUndo(editor, "Move " + entity.classname);
                *axes[axis] = value;
            }
        }
    }
    y += ROW_HEIGHT;

    // Every other parameter, typed as JSON: numbers like 5, arrays like [255, 150, 60]. Anything that
    // isn't valid JSON is kept as text
    GuiLine({ x, y, width, 20 }, "Parameters");
    y += 24;

    // Sets a parameter, as one undo step. continuing: part of a slider drag that already made its undo step
    auto setParameter = [&](const std::string &name, const nlohmann::ordered_json &value, bool continuing)
    {
        if (entity.properties.contains(name) && entity.properties[name] == value) return;
        if (!continuing) RecordUndo(editor, "Change " + entity.classname + " " + name);
        entity.properties[name] = value;
    };

    // A parameter row: its name, then a way to edit it that suits its type. Colors open a color picker,
    // single numbers get a slider plus a box for typing exact values, and anything else is typed as JSON
    int id = 10;
    auto parameterRow = [&](const std::string &name, const EntityParamInfo *param, bool isSet, Color labelColor, const std::string &description)
    {
        Rectangle row = { x, y, width, ROW_HEIGHT - 4 };
        if (CheckCollisionPointRec(GetMousePosition(), row)) help = name + ": " + description;
        DrawUIText(name.c_str(), x, y + (ROW_HEIGHT - 4 - 16)/2, 16, labelColor);

        std::string type = param ? param->type : "";
        std::string shown = isSet ? entity.properties[name].dump() : DefaultParamValue(*param).dump();
        Rectangle field = { fieldX, y, fieldWidth, ROW_HEIGHT - 4 };
        int fieldId = id++;

        if (type == "color")
        {
            // A swatch of the color. Clicking opens the color picker
            Color color = EntityColorOrDefault(entity, name.c_str(), WHITE);
            if (GuiButton(field, TextFormat("        %d, %d, %d", color.r, color.g, color.b))) OpenColorPicker(editor, editor.selected, name);
            Rectangle swatch = { field.x + 5, field.y + 4, 26, field.height - 8 };
            DrawRectangleRec(swatch, color);
            DrawRectangleLinesEx(swatch, 1.0f, GetColor(GuiGetStyle(DEFAULT, LINE_COLOR)));
        }
        else if (type == "number" || type == "radius" || type == "angle")
        {
            // Slider, for dragging
            float min, max;
            GetParamRange(*param, min, max);
            const float valueWidth = 64.0f;
            float value = EntityNumberOrDefault(entity, name.c_str(), 0.0f);
            float sliderValue = value;
            GuiSliderBar({ fieldX, y + 6, fieldWidth - valueWidth - 6, ROW_HEIGHT - 16 }, nullptr, nullptr, &sliderValue, min, max);
            if (sliderValue != value)
            {
                // Angles snap to whole degrees, other numbers to hundredths, so files don't fill up with long decimals
                nlohmann::ordered_json rounded;
                if (type == "angle") rounded = (int)roundf(sliderValue);
                else rounded = roundf(sliderValue*100.0f)/100.0;
                setParameter(name, rounded, sliderDrag == fieldId);
                sliderDrag = fieldId;
            }

            // Box, for typing an exact value. It isn't limited to the slider's range
            if (EditableField(fieldId, { fieldX + fieldWidth - valueWidth, y, valueWidth, ROW_HEIGHT - 4 }, shown))
            {
                char *end;
                double typed = strtod(editBuffer, &end);
                if (end == editBuffer) SetStatus(editor, "\"%s\" isn't a number", editBuffer);
                else setParameter(name, typed, false);
            }
        }
        else if (type == "direction")
        {
            // Typed as [x, y, z], or aimed with Point At: the next click on the level points it there
            const float buttonWidth = 76.0f;
            if (EditableField(fieldId, { fieldX, y, fieldWidth - buttonWidth - 6, ROW_HEIGHT - 4 }, shown))
            {
                nlohmann::ordered_json value = nlohmann::ordered_json::parse(editBuffer, nullptr, false);
                if (value.is_discarded() || !value.is_array() || value.size() != 3) SetStatus(editor, "A direction is three numbers, like [0.5, -1, 0.3]");
                else setParameter(name, value, false);
            }

            bool active = editor.pointing && editor.pointEntity == editor.selected && editor.pointParameter == name;
            Rectangle button = { fieldX + fieldWidth - buttonWidth, y, buttonWidth, ROW_HEIGHT - 4 };
            if (GuiButton(button, active ? "Cancel" : "Point At"))
            {
                if (active) CancelPointAt(editor);
                else StartPointAt(editor, editor.selected, name);
            }
        }
        else if (type == "vector")
        {
            // Typed as three numbers, like "-1 0 -1". Brackets and commas are fine too
            nlohmann::ordered_json current = isSet ? entity.properties[name] : DefaultParamValue(*param);
            std::string numbers;
            if (current.is_array()) for (const auto &n : current) numbers += (numbers.empty() ? "" : " ") + n.dump();
            if (EditableField(fieldId, field, numbers))
            {
                std::string typed = editBuffer;
                for (char &c : typed) if (c == '[' || c == ']' || c == ',') c = ' ';
                std::istringstream values(typed);
                double v[3];
                std::string extra;
                if (!(values >> v[0] >> v[1] >> v[2]) || (values >> extra)) SetStatus(editor, "%s is three numbers, like -1 0 -1", name.c_str());
                else setParameter(name, nlohmann::ordered_json::array({ v[0], v[1], v[2] }), false);
            }
        }
        else if (type == "text")
        {
            // Shown and typed without the quotes JSON would put around it
            nlohmann::ordered_json current = isSet ? entity.properties[name] : DefaultParamValue(*param);
            std::string text = current.is_string() ? current.get<std::string>() : current.dump();
            if (EditableField(fieldId, field, text)) setParameter(name, std::string(editBuffer), false);
        }
        else if (EditableField(fieldId, field, shown))
        {
            nlohmann::ordered_json value = nlohmann::ordered_json::parse(editBuffer, nullptr, false);
            if (value.is_discarded()) value = std::string(editBuffer);
            setParameter(name, value, false);
        }

        y += ROW_HEIGHT;
    };

    // The ones the level file has. Copied first, since editing one can change the list
    std::vector<std::string> names;
    for (auto &property : entity.properties.items()) names.push_back(property.key());
    for (const std::string &name : names)
    {
        const EntityParamInfo *param = entityClass ? FindEntityParam(*entityClass, name) : nullptr;
        if (param) parameterRow(name, param, true, UITextColor(), param->description + " (" + param->type + ")");
        else parameterRow(name, nullptr, true, warning, "The game doesn't use this parameter");
    }

    // The ones it doesn't have, showing the default the game uses. Changing one adds it
    if (entityClass)
    {
        for (const EntityParamInfo &param : entityClass->params)
        {
            if (std::find(names.begin(), names.end(), param.name) != names.end()) continue;
            parameterRow(param.name, &param, false, UIMutedTextColor(), param.description + " (" + param.type + ", not set: using the default)");
        }
    }

    if (entity.properties.empty() && (!entityClass || entityClass->params.empty()))
    {
        GuiLabel({ x, y, width, ROW_HEIGHT }, "No parameters");
        y += ROW_HEIGHT;
    }

    y += 10;
    float buttonWidth = (width - 8)/2.0f;
    if (GuiButton({ x, y, buttonWidth, 30 }, "Deselect")) editor.selected = -1;
    if (GuiButton({ x + buttonWidth + 8, y, buttonWidth, 30 }, "Delete Entity")) DeleteSelectedEntity(editor);
    y += 44;

    if (!help.empty()) DrawUITextWrapped(help.c_str(), x, y, width, 14, UIMutedTextColor());
}

static void DrawLevelDetails(EditorState &editor, Rectangle panel, float y)
{
    const Level &level = editor.level;
    const float x = panel.x + PADDING;
    const float width = panel.width - 2*PADDING;
    const float valueX = x + LABEL_WIDTH;
    const float valueWidth = width - LABEL_WIDTH;

    GuiLabel({ x, y, LABEL_WIDTH, ROW_HEIGHT }, "Map name");
    GuiLabel({ valueX, y, valueWidth, ROW_HEIGHT }, level.info.name.c_str());
    y += ROW_HEIGHT;

    GuiLabel({ x, y, LABEL_WIDTH, ROW_HEIGHT }, "File");
    GuiLabel({ valueX, y, valueWidth, ROW_HEIGHT }, editor.levelPath.empty() ? "Not saved yet" : ToDataPath(editor, editor.levelPath).c_str());
    y += ROW_HEIGHT;

    GuiLabel({ x, y, LABEL_WIDTH, ROW_HEIGHT }, "World model");
    GuiLabel({ valueX, y, valueWidth, ROW_HEIGHT }, level.worldModel.empty() ? "None" : level.worldModel.c_str());
    y += ROW_HEIGHT;

    GuiLabel({ x, y, LABEL_WIDTH, ROW_HEIGHT }, "Scale");
    GuiLabel({ valueX, y, valueWidth, ROW_HEIGHT }, TextFormat("%g", level.worldScale));
    y += ROW_HEIGHT + 6;

    // Entity list, click one to select it. Scrolls with the mouse wheel
    GuiLine({ x, y, width, 20 }, TextFormat("Entities (%d)", (int)level.entities.size()));
    y += 26;

    if (level.entities.empty())
    {
        GuiLabel({ x, y, width, ROW_HEIGHT }, "None yet, see Edit > Create Entity");
        return;
    }

    int visible = (int)((panel.y + panel.height - PADDING - y)/ROW_HEIGHT);
    int maxScroll = (int)level.entities.size() - visible;
    if (maxScroll < 0) maxScroll = 0;
    if (CheckCollisionPointRec(GetMousePosition(), panel) && !GuiIsLocked()) listScroll -= (int)GetMouseWheelMove();
    if (listScroll > maxScroll) listScroll = maxScroll;
    if (listScroll < 0) listScroll = 0;

    int alignment = GuiGetStyle(BUTTON, TEXT_ALIGNMENT);
    GuiSetStyle(BUTTON, TEXT_ALIGNMENT, TEXT_ALIGN_LEFT);
    for (int i = listScroll; i < (int)level.entities.size() && i < listScroll + visible; i++)
    {
        const LevelEntity &entity = level.entities[i];
        const char *text = TextFormat("  %d. %s  (%.1f %.1f %.1f)", i + 1, entity.classname.c_str(), entity.origin.x, entity.origin.y, entity.origin.z);
        if (GuiButton({ x, y, width, ROW_HEIGHT - 4 }, text)) editor.selected = i;
        y += ROW_HEIGHT;
    }
    GuiSetStyle(BUTTON, TEXT_ALIGNMENT, alignment);
}

void DrawDetailsPanel(EditorState &editor)
{
    if (editor.selected >= (int)editor.level.entities.size()) editor.selected = -1;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) sliderDrag = -1;  // A drag ends when the button is let go
    if (editor.selected != editedEntity)
    {
        activeField = -1;
        editedEntity = editor.selected;
    }

    Rectangle panel = PanelRect();
    GuiPanel(panel, "Details");

    float y = panel.y + HEADER_HEIGHT + PADDING;
    if (editor.selected >= 0) DrawEntityDetails(editor, panel, y);
    else DrawLevelDetails(editor, panel, y);

    if (activeField >= 0) editor.textEditing = true;
}
