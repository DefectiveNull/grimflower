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

// Video, Audio and Game settings, applied as soon as they change. Returns true when Back is clicked
bool DrawOptionsMenu();
