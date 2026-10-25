#pragma once
#include <string>

#include "settings.h"

namespace oldr {

std::wstring ConfigDirectory(); 
void LoadSettings(Settings& s);
void SaveSettings(const Settings& s);
bool SetRunAtStartup(bool enable); 

}  
