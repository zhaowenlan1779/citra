// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/changelog_backend.h"

namespace WebService {
std::unique_ptr<WebService::Changelog::Backend> GetChangelogBackend();
}
