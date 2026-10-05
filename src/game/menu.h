#pragma once

enum MenuAction
{
    MENU_NONE,
    MENU_CONTINUE,  // Play (main menu) or Resume (pause menu)
    MENU_OPTIONS,
    MENU_QUIT
};

// Both menus draw over the current frame. Call between BeginDrawing/EndDrawing.
// They return the button clicked this frame, or MENU_NONE.
MenuAction DrawMainMenu(const char *title);
MenuAction DrawPauseMenu();

// Two tabs: General (video, audio and game settings, applied as soon as they change) and Input
// (rebinding keys). Changes are saved to config.cfg. Returns true when Back is clicked
bool DrawOptionsMenu();

// True while the Input tab waits for a key, so Esc cancels that instead of closing the options
bool IsOptionsMenuCapturingKey();
