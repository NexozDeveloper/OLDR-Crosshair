// OLDR Crosshair - settings persistence (INI in %APPDATA%) and Windows startup entry.
#pragma once
#include <string>

#include "settings.h"

namespace oldr {

std::wstring ConfigDirectory();                 // %APPDATA%\OLDR Crosshair (created on demand)
void LoadSettings(Settings& s);
void SaveSettings(const Settings& s);
bool SetRunAtStartup(bool enable);              // HKCU\...\Run

}  // namespace oldr
