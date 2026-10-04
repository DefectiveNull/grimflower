#pragma once

// Quake 3 style drop-down console, toggled with the key left of 1 (` on US layouts)

// Starts capturing raylib's log messages. Call before InitWindow so startup messages show up too
void ConsoleInit();

// Handles the toggle key and typing. Returns true if the console took the keyboard this frame,
// in which case the game should ignore its own input
bool ConsoleUpdate();

// Draws the console over everything else. Call last, between BeginDrawing/EndDrawing
void ConsoleDraw();

// Prints a line to the console, printf style
void ConsolePrintf(const char *format, ...);

// Empties the log and the command history
void ConsoleClear();

// Writes the whole log to a text file. Returns the number of lines written, or -1 on failure
int ConsoleSaveLog(const char *path);
