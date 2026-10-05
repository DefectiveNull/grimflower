#include "hotreload.h"
#include "entitylist.h"
#include "raylib.h"

#include <string>

static const char *ENTITY_LIST_PATH = "entitylist.txt";
static const double CHECK_INTERVAL = 0.5;  // Seconds between looking at the files
static const double SETTLE_TIME = 0.5;     // A changed file has to stay the same this long first, so it isn't read while still being written

// What a file looks like on disk. The size too, since the time only counts whole seconds
struct FileStamp
{
    long modTime;  // 0 if the file is missing
    int length;
};

static bool SameStamp(FileStamp a, FileStamp b)
{
    return a.modTime == b.modTime && a.length == b.length;
}

static FileStamp GetFileStamp(const std::string &path)
{
    if (path.empty() || !FileExists(path.c_str())) return { 0, 0 };
    return { GetFileModTime(path.c_str()), GetFileLength(path.c_str()) };
}

struct WatchedFile
{
    std::string path;
    FileStamp loaded;     // The file when it was last loaded
    FileStamp pending;    // The file at the last check, to tell when it stops changing
    double pendingSince;
};

static WatchedFile entityList;
static WatchedFile worldModel;
static bool started = false;
static double lastCheck = 0.0;

// Starts watching a file as it is now, without reloading it
static void Watch(WatchedFile &file, const std::string &path)
{
    file.path = path;
    file.loaded = file.pending = GetFileStamp(path);
    file.pendingSince = GetTime();
}

// True once the file has changed since it was loaded and then stayed the same for SETTLE_TIME.
// A missing file never counts, so deleting it keeps what's loaded
static bool HasSettledChange(WatchedFile &file)
{
    FileStamp now = GetFileStamp(file.path);
    if (!SameStamp(now, file.pending))
    {
        file.pending = now;
        file.pendingSince = GetTime();
        return false;
    }

    if (now.modTime == 0 || SameStamp(now, file.loaded) || GetTime() - file.pendingSince < SETTLE_TIME) return false;

    // Counts as loaded even if loading it fails, so a broken file isn't retried until it changes again
    file.loaded = now;
    return true;
}

void UpdateHotReload(EditorState &editor)
{
    if (!started)
    {
        Watch(entityList, ENTITY_LIST_PATH);
        started = true;
    }

    if (GetTime() - lastCheck < CHECK_INTERVAL) return;
    lastCheck = GetTime();

    if (HasSettledChange(entityList))
    {
        if (LoadEntityList(ENTITY_LIST_PATH)) SetStatus(editor, "%s changed, reloaded %d entity types", ENTITY_LIST_PATH, (int)GetEntityClassList().size());
        else SetStatus(editor, "%s changed but couldn't be read, kept the previous entity types", ENTITY_LIST_PATH);
    }

    // Opening a level, Create World or an undo can switch to another model, which was just loaded
    if (worldModel.path != editor.level.worldModel)
    {
        Watch(worldModel, editor.level.worldModel);
    }
    else if (HasSettledChange(worldModel) && ReloadWorld(editor))
    {
        SetStatus(editor, "%s changed, reloaded it with %d collision brushes", worldModel.path.c_str(), (int)editor.world.colliders.size());
    }
}
