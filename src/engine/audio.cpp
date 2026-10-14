#include "audio.h"
#include "console.h"
#include "raylib.h"

#include <map>
#include <string>
#include <vector>

// { name, description, value, default value }
Cvar volume = { "volume", "Master volume, 0 to 1", 1.0f, 1.0f };
Cvar world_volume = { "world_volume", "Volume of world sounds like footsteps and doors, 0 to 1", 1.0f, 1.0f };
Cvar music_volume = { "music_volume", "Volume of the music, 0 to 1", 0.7f, 0.7f };

static Cvar *const AUDIO_CVARS[] = { &volume, &world_volume, &music_volume };

static bool audioReady = false;

// World sounds by path, loaded the first time they play. A path that failed maps to an empty sound,
// so it isn't tried again every time
static std::map<std::string, Sound> worldSounds;
static float appliedWorldVolume = -1.0f;  // What the cached sounds are set to

// The music track, streamed from a copy of the file in memory, since raylib's music loader would
// otherwise read straight from disk and miss gamedata.zip
static Music music = { 0 };
static unsigned char *musicData = nullptr;
static bool musicLoaded = false;
static std::string musicPath;

// Ambient sounds, streamed and looping like music, but on the world channel. A stopped one leaves a free
// slot, so handles stay valid while others start and stop
struct AmbientSound
{
    Music stream;
    unsigned char *data;  // The file in memory, which the stream reads from
    float volume;         // Before the world channel's volume
    bool playing;
};
static std::vector<AmbientSound> ambientSounds;

static float Clamp01(float value)
{
    return (value < 0.0f) ? 0.0f : (value > 1.0f) ? 1.0f : value;
}

// ---------------------------------------------------------------------------------------------
// Console commands

static void Cmd_Play(const std::vector<std::string> &args)
{
    if (args.size() < 2)
    {
        ConsolePrintf("Usage: play <file>, like play sound/door.wav");
        return;
    }
    if (!PlayWorldSound(args[1].c_str())) ConsolePrintf("Couldn't play %s", args[1].c_str());
}

static void Cmd_Music(const std::vector<std::string> &args)
{
    if (args.size() < 2)
    {
        if (musicLoaded) ConsolePrintf("Playing %s. music stop stops it", musicPath.c_str());
        else ConsolePrintf("Usage: music <file> to play a track, like music music/theme.ogg, or music stop");
        return;
    }

    if (args[1] == "stop") StopMusic();
    else if (!PlayMusic(args[1].c_str(), true)) ConsolePrintf("Couldn't play %s", args[1].c_str());
}

static const ConsoleCommand AUDIO_COMMANDS[] =
{
    { "play", "Plays a sound on the world channel: play <file>", Cmd_Play },
    { "music", "Plays a looping track on the music channel: music <file>, or music stop", Cmd_Music },
};

// ---------------------------------------------------------------------------------------------

void InitAudio()
{
    InitAudioDevice();
    audioReady = IsAudioDeviceReady();
    if (!audioReady) TraceLog(LOG_WARNING, "AUDIO: No audio device, the game will be silent");

    AddCvars(AUDIO_CVARS, (int)(sizeof(AUDIO_CVARS)/sizeof(AUDIO_CVARS[0])));
    AddCommands(AUDIO_COMMANDS, (int)(sizeof(AUDIO_COMMANDS)/sizeof(AUDIO_COMMANDS[0])));
}

void CloseAudio()
{
    StopMusic();
    for (int i = 0; i < (int)ambientSounds.size(); i++) StopAmbientSound(i);
    ambientSounds.clear();
    for (auto &entry : worldSounds)
    {
        if (entry.second.frameCount > 0) UnloadSound(entry.second);
    }
    worldSounds.clear();
    if (audioReady) CloseAudioDevice();
    audioReady = false;
}

void UpdateAudio()
{
    if (!audioReady) return;

    volume.value = Clamp01(volume.value);
    world_volume.value = Clamp01(world_volume.value);
    music_volume.value = Clamp01(music_volume.value);
    SetMasterVolume(volume.value);

    if (world_volume.value != appliedWorldVolume)
    {
        for (auto &entry : worldSounds)
        {
            if (entry.second.frameCount > 0) SetSoundVolume(entry.second, world_volume.value);
        }
        appliedWorldVolume = world_volume.value;
    }

    if (musicLoaded)
    {
        SetMusicVolume(music, music_volume.value);
        UpdateMusicStream(music);
    }

    for (AmbientSound &ambient : ambientSounds)
    {
        if (!ambient.playing) continue;
        SetMusicVolume(ambient.stream, ambient.volume*world_volume.value);
        UpdateMusicStream(ambient.stream);
    }
}

int StartAmbientSound(const char *path)
{
    if (!audioReady) return -1;

    int size = 0;
    unsigned char *data = LoadFileData(path, &size);
    if (data == nullptr)
    {
        TraceLog(LOG_WARNING, "AUDIO: [%s] Couldn't find the ambient sound", path);
        return -1;
    }
    Music stream = LoadMusicStreamFromMemory(GetFileExtension(path), data, size);
    if (!IsMusicValid(stream))
    {
        TraceLog(LOG_WARNING, "AUDIO: [%s] Couldn't load the ambient sound, it needs to be .ogg, .mp3, .wav, .flac or .qoa", path);
        UnloadFileData(data);
        return -1;
    }
    stream.looping = true;
    SetMusicVolume(stream, 0.0f);
    PlayMusicStream(stream);

    AmbientSound ambient = { stream, data, 0.0f, true };
    for (size_t i = 0; i < ambientSounds.size(); i++)
    {
        if (ambientSounds[i].playing) continue;
        ambientSounds[i] = ambient;
        return (int)i;
    }
    ambientSounds.push_back(ambient);
    return (int)ambientSounds.size() - 1;
}

void SetAmbientSound(int handle, float volume, float pan)
{
    if (handle < 0 || handle >= (int)ambientSounds.size() || !ambientSounds[handle].playing) return;
    AmbientSound &ambient = ambientSounds[handle];
    ambient.volume = Clamp01(volume);
    SetMusicVolume(ambient.stream, ambient.volume*world_volume.value);
    SetMusicPan(ambient.stream, (pan < -1.0f) ? -1.0f : (pan > 1.0f) ? 1.0f : pan);
}

void StopAmbientSound(int handle)
{
    if (handle < 0 || handle >= (int)ambientSounds.size() || !ambientSounds[handle].playing) return;
    AmbientSound &ambient = ambientSounds[handle];
    StopMusicStream(ambient.stream);
    UnloadMusicStream(ambient.stream);
    UnloadFileData(ambient.data);
    ambient = {};
}

bool PlayWorldSound(const char *path)
{
    if (!audioReady) return false;

    auto cached = worldSounds.find(path);
    if (cached == worldSounds.end())
    {
        // LoadSound reads through the game data
        Sound sound = LoadSound(path);
        if (sound.frameCount > 0) SetSoundVolume(sound, world_volume.value);
        else TraceLog(LOG_WARNING, "AUDIO: [%s] Couldn't load the sound", path);
        cached = worldSounds.emplace(path, sound).first;
    }

    if (cached->second.frameCount == 0) return false;
    PlaySound(cached->second);
    return true;
}

bool PlayMusic(const char *path, bool loop)
{
    if (!audioReady) return false;

    int size = 0;
    unsigned char *data = LoadFileData(path, &size);
    if (data == nullptr) return false;

    Music track = LoadMusicStreamFromMemory(GetFileExtension(path), data, size);
    if (!IsMusicValid(track))
    {
        TraceLog(LOG_WARNING, "AUDIO: [%s] Couldn't load the music, it needs to be .ogg, .mp3, .wav, .flac or .qoa", path);
        UnloadFileData(data);
        return false;
    }

    // The new track loaded, so let go of the old one
    StopMusic();
    music = track;
    musicData = data;  // The stream reads from it as it plays
    musicLoaded = true;
    musicPath = path;

    music.looping = loop;
    SetMusicVolume(music, music_volume.value);
    PlayMusicStream(music);
    TraceLog(LOG_INFO, "AUDIO: [%s] Playing music", path);
    return true;
}

void StopMusic()
{
    if (!musicLoaded) return;
    StopMusicStream(music);
    UnloadMusicStream(music);
    UnloadFileData(musicData);
    music = { 0 };
    musicData = nullptr;
    musicLoaded = false;
    musicPath.clear();
}
