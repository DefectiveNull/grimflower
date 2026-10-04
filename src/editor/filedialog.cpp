// Kept in its own file because windows.h clashes with raylib.h, so they can't be included together

#include "filedialog.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

static std::string ToWindowsPath(std::string path)
{
    for (char &c : path) if (c == '/') c = '\\';
    return path;
}

bool ShowOpenFileDialog(void *owner, const char *title, const char *filter, const std::string &startFolder, std::string &path)
{
    char file[MAX_PATH] = "";
    std::string folder = ToWindowsPath(startFolder);

    OPENFILENAMEA dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = (HWND)owner;
    dialog.lpstrTitle = title;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrInitialDir = folder.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;  // NOCHANGEDIR: keep the editor's working directory

    if (!GetOpenFileNameA(&dialog)) return false;
    path = file;
    return true;
}

bool ShowSaveFileDialog(void *owner, const char *title, const char *filter, const char *defaultExtension,
                        const std::string &startFolder, std::string &path)
{
    char file[MAX_PATH] = "";
    std::string folder = ToWindowsPath(startFolder);

    OPENFILENAMEA dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = (HWND)owner;
    dialog.lpstrTitle = title;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrInitialDir = folder.c_str();
    dialog.lpstrDefExt = defaultExtension;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetSaveFileNameA(&dialog)) return false;
    path = file;
    return true;
}
