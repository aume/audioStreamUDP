#pragma once

// Forces the whole app (title bar, file choosers, native menus) into macOS
// dark appearance regardless of the system Light/Dark setting, to match
// JamLink's dark UI. No-op on other platforms.
void forceDarkAppearance();
