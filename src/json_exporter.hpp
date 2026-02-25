#pragma once

#include "sfx_def.hpp"

#include <string>

bool write_sfx_json(const std::string& path, const std::string& name, const SfxDef& sfx);
