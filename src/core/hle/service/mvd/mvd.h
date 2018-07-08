// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/service/service.h"

namespace Service {
namespace MVD {

/// Initializes all MVD services.
void InstallInterfaces(SM::ServiceManager& service_manager);

} // namespace MVD
} // namespace Service
