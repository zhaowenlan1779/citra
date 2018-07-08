// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/hle/ipc_helpers.h"
#include "core/hle/service/qtm/qtm_s.h"

namespace Service {
namespace QTM {

QTM_S::QTM_S() : ServiceFramework("qtm:s", 2) {
    static const FunctionInfo functions[] = {
        // clang-format off
        // qtm common commands
        {0x00010080, nullptr, "GetHeadtrackingInfoRaw"},
        {0x00020080, nullptr, "GetHeadtrackingInfo"},
        // clang-format on
    };

    RegisterHandlers(functions);
}

} // namespace QTM
} // namespace Service
