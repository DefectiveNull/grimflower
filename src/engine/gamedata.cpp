#include "gamedata.h"
#include "raylib.h"
#include "miniz.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static mz_zip_archive zip;
static bool mounted = false;
static std::string mountDir;  // Working directory when mounted, with '/' slashes and a trailing '/'

// Turns a path raylib was asked to load into a path inside the zip
static std::string ToZipPath(const char *fileName)
{
    std::string path = fileName;
    for (char &c : path) if (c == '\\') c = '/';

    // raylib sometimes builds absolute paths from the working directory, so make those relative again
    if (path.size() >= mountDir.size() && _strnicmp(path.c_str(), mountDir.c_str(), mountDir.size()) == 0)
    {
        path = path.substr(mountDir.size());
    }

    // Resolve "." and ".." parts, since zip entries are stored with plain paths
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= path.size())
    {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        std::string part = path.substr(start, end - start);
        if (part == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!part.empty() && part != ".") parts.push_back(part);
        start = end + 1;
    }

    std::string result;
    for (size_t i = 0; i < parts.size(); i++) result += (i > 0 ? "/" : "") + parts[i];
    return result;
}

static int FindInZip(const char *fileName)
{
    if (!mounted) return -1;
    return mz_zip_reader_locate_file(&zip, ToZipPath(fileName).c_str(), nullptr, 0);  // Not case sensitive
}

// The project folder, when the game runs from inside it, with a trailing '/'. Empty otherwise
static std::string projectFolder;

// Where a file would be in the project folder, or "" if there's no project folder or it isn't there
static std::string ProjectPath(const char *fileName)
{
    if (projectFolder.empty()) return "";

    std::string relative = ToZipPath(fileName);
    if (relative.empty() || relative.find(':') != std::string::npos || relative[0] == '/') return "";  // Absolute paths aren't game data
    std::string path = projectFolder + relative;
    return FileExists(path.c_str()) ? path : "";
}

// Reads a whole file from disk. Adds extraBytes of zeroes at the end
static unsigned char *ReadDiskFile(const char *path, int *dataSize, int extraBytes)
{
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return nullptr;

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    unsigned char *data = (unsigned char *)RL_CALLOC(size + extraBytes, 1);  // raylib frees it with RL_FREE
    if (data != nullptr && fread(data, 1, size, file) == (size_t)size)
    {
        *dataSize = (int)size;
    }
    else
    {
        RL_FREE(data);
        data = nullptr;
    }

    fclose(file);
    return data;
}

// Reads a file from the project folder, then the zip, then from disk next to the game.
// Adds extraBytes of zeroes at the end
static unsigned char *ReadGameDataFile(const char *fileName, int *dataSize, int extraBytes)
{
    *dataSize = 0;

    // Loose files in the project folder come first, so changes show up without repacking the zip
    std::string projectPath = ProjectPath(fileName);
    if (!projectPath.empty())
    {
        unsigned char *data = ReadDiskFile(projectPath.c_str(), dataSize, extraBytes);
        if (data != nullptr)
        {
            TraceLog(LOG_INFO, "GAMEDATA: [%s] Loaded from the project folder", fileName);
            return data;
        }
    }

    int index = FindInZip(fileName);
    if (index >= 0)
    {
        mz_zip_archive_file_stat stat;
        if (mz_zip_reader_file_stat(&zip, index, &stat))
        {
            size_t size = (size_t)stat.m_uncomp_size;
            unsigned char *data = (unsigned char *)RL_CALLOC(size + extraBytes, 1);
            if (data != nullptr && mz_zip_reader_extract_to_mem(&zip, index, data, size, 0))
            {
                *dataSize = (int)size;
                TraceLog(LOG_INFO, "GAMEDATA: [%s] Loaded from gamedata", fileName);
                return data;
            }
            RL_FREE(data);
        }
        TraceLog(LOG_WARNING, "GAMEDATA: [%s] Failed to extract from gamedata", fileName);
        return nullptr;
    }

    unsigned char *data = ReadDiskFile(fileName, dataSize, extraBytes);
    if (data != nullptr) TraceLog(LOG_INFO, "GAMEDATA: [%s] Loaded from disk", fileName);
    else TraceLog(LOG_WARNING, "GAMEDATA: [%s] Not in the project folder, gamedata or on disk", fileName);
    return data;
}

static unsigned char *LoadGameDataFile(const char *fileName, int *dataSize)
{
    return ReadGameDataFile(fileName, dataSize, 0);
}

static char *LoadGameDataText(const char *fileName)
{
    int size;
    return (char *)ReadGameDataFile(fileName, &size, 1);  // The extra zero byte ends the string
}

bool MountGameData(const char *zipPath)
{
    mountDir = GetWorkingDirectory();
    for (char &c : mountDir) if (c == '\\') c = '/';
    if (mountDir.empty() || mountDir.back() != '/') mountDir += '/';

    // Every raylib load goes through these, even if the zip fails to open, so the disk fallback is the same
    SetLoadFileDataCallback(LoadGameDataFile);
    SetLoadFileTextCallback(LoadGameDataText);

    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, zipPath, 0))
    {
        TraceLog(LOG_WARNING, "GAMEDATA: [%s] Couldn't open, loading files from disk only", zipPath);
        return false;
    }

    mounted = true;
    TraceLog(LOG_INFO, "GAMEDATA: [%s] Mounted, %u files", zipPath, mz_zip_reader_get_num_files(&zip));
    return true;
}

void UnmountGameData()
{
    if (mounted) mz_zip_reader_end(&zip);
    mounted = false;
}

bool GameDataFileExists(const char *fileName)
{
    return !ProjectPath(fileName).empty() || FindInZip(fileName) >= 0 || FileExists(fileName);
}

bool UseProjectFolder()
{
    // build/grimflower.exe sits inside the project, so look in the game's folder and the ones above it
    std::string folder = GetApplicationDirectory();
    for (int i = 0; i < 4; i++)
    {
        if (DirectoryExists((folder + "/maps").c_str()) && DirectoryExists((folder + "/models").c_str()))
        {
            projectFolder = folder;
            for (char &c : projectFolder) if (c == '\\') c = '/';
            if (projectFolder.back() != '/') projectFolder += '/';
            TraceLog(LOG_INFO, "GAMEDATA: [%s] Project folder found, its files are used before gamedata.zip", projectFolder.c_str());
            return true;
        }
        folder = GetPrevDirectoryPath(folder.c_str());
    }
    return false;
}
