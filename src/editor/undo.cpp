#include "editor.h"
#include "config.h"

// Undo works by copying the whole level before each change. Levels are small, so this is cheap,
// and it covers every kind of change without each one needing its own undo code

static UndoStep CurrentState(const EditorState &editor, const std::string &description)
{
    return { description, editor.level, editor.selected, editor.stateId };
}

static void Restore(EditorState &editor, const UndoStep &step)
{
    editor.level = step.level;
    editor.selected = (step.selected < (int)editor.level.entities.size()) ? step.selected : -1;
    editor.stateId = step.stateId;
    editor.unsavedChanges = (editor.stateId != editor.savedStateId);
    editor.dragging = false;
    editor.edit.edges.clear();  // Picked by vertex, which the restored shapes may number differently
    editor.edit.hover = GeoPick();  // And the face under the mouse may not be there any more
    GeometryChanged(editor);
    SyncWorldWithLevel(editor);
}

void RecordUndo(EditorState &editor, const std::string &description)
{
    editor.undoSteps.push_back(CurrentState(editor, description));
    // The oldest steps are dropped past the limit set in Preferences
    size_t limit = (size_t)(editorSettings.undoSteps > 1 ? editorSettings.undoSteps : 1);
    while (editor.undoSteps.size() > limit) editor.undoSteps.erase(editor.undoSteps.begin());
    editor.redoSteps.clear();

    // The change about to happen makes a new version
    editor.stateId = ++editor.nextStateId;
    editor.unsavedChanges = true;
}

void ResetUndoHistory(EditorState &editor)
{
    editor.undoSteps.clear();
    editor.redoSteps.clear();
    editor.stateId = ++editor.nextStateId;
    editor.savedStateId = editor.stateId;
    editor.unsavedChanges = false;
}

void MarkSaved(EditorState &editor)
{
    editor.savedStateId = editor.stateId;
    editor.unsavedChanges = false;
}

bool CanUndo(const EditorState &editor)
{
    return !editor.undoSteps.empty();
}

bool CanRedo(const EditorState &editor)
{
    return !editor.redoSteps.empty();
}

void Undo(EditorState &editor)
{
    if (!CanUndo(editor)) return;

    UndoStep step = editor.undoSteps.back();
    editor.undoSteps.pop_back();
    editor.redoSteps.push_back(CurrentState(editor, step.description));
    Restore(editor, step);
    SetStatus(editor, "Undid %s", step.description.c_str());
}

void Redo(EditorState &editor)
{
    if (!CanRedo(editor)) return;

    UndoStep step = editor.redoSteps.back();
    editor.redoSteps.pop_back();
    editor.undoSteps.push_back(CurrentState(editor, step.description));
    Restore(editor, step);
    SetStatus(editor, "Redid %s", step.description.c_str());
}

int GetHistoryPosition(const EditorState &editor)
{
    return (int)editor.undoSteps.size();
}

void JumpToHistory(EditorState &editor, int position)
{
    while (GetHistoryPosition(editor) > position && CanUndo(editor)) Undo(editor);
    while (GetHistoryPosition(editor) < position && CanRedo(editor)) Redo(editor);
}
