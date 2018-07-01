// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#ifdef ENABLE_WEB_SERVICE
#include "web_service/github_changelog.h"
#endif
#include "common/changelog_backend.h"
#include "common/scm_rev.h"

namespace WebService {
std::unique_ptr<Changelog::Backend> GetChangelogBackend() {
#ifdef ENABLE_WEB_SERVICE
    if (Common::g_build_name == "Nightly")
        return std::make_unique<GithubChangelog>(
            "https://api.github.com/repos/citra-emu/citra-nightly/compare",
            "https://raw.githubusercontent.com/citra-emu/citra-nightly");
    else if (Common::g_build_name == "Canary")
        return std::make_unique<GithubChangelog>(
            "https://api.github.com/repos/citra-emu/citra-canary/compare",
            "https://raw.githubusercontent.com/citra-emu/citra-canary");
    else
        return std::make_unique<Changelog::NullBackend>();
#else
    return std::make_unique<Changelog::NullBackend>();
#endif
}
} // namespace WebService
