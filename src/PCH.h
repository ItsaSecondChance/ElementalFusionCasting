#pragma once

// Load the C++ standard-library facilities required by REL headers before
// CommonLibSSE's generated plugin metadata translation unit is compiled.
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>

// CommonLibSSE-NG's generated SKSEPluginInfo metadata uses the "sv" suffix.
// The generated translation unit needs this namespace visible through the PCH.
using namespace std::literals;
