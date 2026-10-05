#pragma once

// Key binds, like Quake's: each key can run a console command. A command starting with + runs
// on press, and the same command with - instead runs on release, so "+forward" holds a button
// down for as long as the key is held

#include <map>
#include <string>

void SetBind(int key, const std::string &command);
void RemoveBind(int key);
void ClearBinds();
const std::map<int, std::string> &GetBinds();  // Key code to command

// Runs the commands of keys pressed or released this frame. Call while the game has the controls
void UpdateBinds();

// Runs the - command of every key still held, so nothing stays stuck down when the game loses the
// controls, like when the console or the pause menu opens
void ReleaseBinds();
