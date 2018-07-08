// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/hle/service/cecd/cecd_s.h"

namespace Service {
namespace CECD {

CECD_S::CECD_S(std::shared_ptr<Module> cecd)
    : Module::Interface(std::move(cecd), "cecd:s", DefaultMaxSessions) {
    static const FunctionInfo functions[] = {
        // cecd:u shared commands
        // clang-format off
        {0x000100C2, nullptr, "OpenRawFile"},
        {0x00020042, nullptr, "ReadRawFile"},
        {0x00030104, nullptr, "ReadMessage"},
        {0x00040106, nullptr, "ReadMessageWithHMAC"},
        {0x00050042, nullptr, "WriteRawFile"},
        {0x00060104, nullptr, "WriteMessage"},
        {0x00070106, nullptr, "WriteMessageWithHMAC"},
        {0x00080102, nullptr, "Delete"},
        {0x000A00C4, nullptr, "GetSystemInfo"},
        {0x000B0040, nullptr, "RunCommand"},
        {0x000C0040, nullptr, "RunCommandAlt"},
        {0x000E0000, &CECD_S::GetCecStateAbbreviated, "GetCecStateAbbreviated"},
        {0x000F0000, &CECD_S::GetCecInfoEventHandle, "GetCecInfoEventHandle"},
        {0x00100000, &CECD_S::GetChangeStateEventHandle, "GetChangeStateEventHandle"},
        {0x00110104, nullptr, "OpenAndWrite"},
        {0x00120104, nullptr, "OpenAndRead"},
        // clang-format on
    };

    RegisterHandlers(functions);
}

} // namespace CECD
} // namespace Service
