#pragma once

#include <cstdint>

#include "core/input.h"

// keyboard and mouse as the console's pad on port 0 (input_kbm.cpp)
namespace kbm {

#if defined(_WIN32)
// app::GameInfo::message: the window thread, after ImGui. Reads key and mouse messages and never
// takes one, so ImGui and the host's own F3 still see them; always returns false. The wheel
// arrives here as WM_MOUSEWHEEL.
bool Message(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);
#else
// a mouse-wheel notch (+1 zoom in, -1 out), from whoever owns the host's event pump this frame
// (overlay::Draw forwards ImGui's wheel while the settings panel is closed). On Windows the
// wheel arrives as WM_MOUSEWHEEL in Message instead, so this exists only on the SDL host.
void NoteWheel(int notches);
// D11: the mouse position/buttons, sampled by the window thread (the only thread
// SDL_GetMouseState may be called on) and read by guest polling threads through
// atomics in input_kbm.cpp. Called from the SDL host's event loop each iteration.
void NoteMouse(float x, float y, bool right, bool middle);
#endif

// app::GameInfo::pad: any guest thread, each poll. ORs the keyboard and mouse into port 0 and
// reports that port connected, pad or no pad. Never clears what a real controller pressed.
void Apply(int port, input::Pad& pad);

}  // namespace kbm
