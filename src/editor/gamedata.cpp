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

// Reads a file from the zip, or from disk if it isn't there. Adds extraBytes of zeroes at the end
static unsigned char *ReadGameDataFile(const char *fileName, int *dataSize, int extraBytes)
{
    *dataSize = 0;

    int index = FindInZip(fileName);
    if (index >= 0)
    {
        mz_zip_archive_file_stat stat;
        if (mz_zip_reader_file_stat(&zip, index, &stat))
        {
            size_t size = (size_t)stat.m_uncomp_size;
            unsigned char *data = (unsigned char *)RL_CALLOC(size + extraBytes, 1);  // raylib frees it with RL_FREE
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

    FILE *file = fopen(fileName, "rb");
    if (file == nullptr)
    {
        TraceLog(LOG_WARNING, "GAMEDATA: [%s] Not in gamedata or on disk", fileName);
        return nullptr;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    unsigned char *data = (unsigned char *)RL_CALLOC(size + extraBytes, 1);
    if (data != nullptr && fread(data, 1, size, file) == (size_t)size)
    {
        *dataSize = (int)size;
        TraceLog(LOG_INFO, "GAMEDATA: [%s] Loaded from disk", fileName);
    }
    else
    {
        RL_FREE(data);
        data = nullptr;
    }

    fclose(file);
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
    return FindInZip(fileName) >= 0 || FileExists(fileName);
}
