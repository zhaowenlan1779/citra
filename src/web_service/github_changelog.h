// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <future>
#include <string>
#include "common/changelog_backend.h"

namespace WebService {

class GithubChangelog : public Changelog::Backend {
public:
    std::future<std::string> GetCommitLog(const std::string& start_tag, const std::string& end_tag,
                                          bool include_changed_lines,
                                          std::function<void()> func) override;
    std::future<std::string> GetCanaryMergeChange(const std::string& start_tag,
                                                  const std::string& end_tag,
                                                  std::function<void()> func) override;
    GithubChangelog(const std::string& compare_endpoint, const std::string& raw_endpoint);

private:
    const std::string compare_endpoint;
    const std::string raw_endpoint;
};

} // namespace WebService
