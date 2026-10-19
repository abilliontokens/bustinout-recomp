#pragma once

// The game's own console cheats, offered in the F1 panel (overlay.cpp). Each one is entered the way
// its pad code enters it (src/game/cheats.cpp), so what it does is the game's own: nothing here is a
// setting, nothing is saved, and nothing happens until the player clicks.
namespace cheats {

// Rows of the game's cheat-code table (sub_000720A0 writes it at 0x310B9E). Entering row n toggles
// bit n of the game's cheat mask at 0x310C0E, which the cheat gnome reads as SimAntics globals 46
// and 47. The Xbox button sequence of each row is beside it; the button names are the game's own
// pad layout from sub_00126270.
enum Code : int {
    kGnome = 0,      // R L Down Black Left B - the cheat gnome appears at the house
    kMoney = 1,      // L Black Right X Left - adds Give Money to the gnome
    kMotives = 2,    // R Left B Down White - adds Fill Motives to the gnome
    kObjects = 3,    // Black Up Y Down R - unlock all objects
    kLocations = 4,  // Black Down R L Down Y - unlock all locations
    kSocials = 5,    // L R A Down Black - unlock all socials
    kSkins = 6,      // L B A Black Down Y - unlock all skins
    kTeamPhoto = 7,  // R Black Down - team picture
};

// any thread: the game is initialised, its cheat latch is clear and a code can be accepted on the
// next frame
bool InGame();

// any thread: the code is on - the game's own bit, with a click the title thread has not taken yet
// already counted, so a checkbox shows what was asked for
bool On(Code c);
// render thread: enter the code again (on <-> off) on the title thread's next frame, exactly as the
// pad sequence would; nothing is queued outside the game
void Toggle(Code c);

}  // namespace cheats
