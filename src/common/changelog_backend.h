// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <future>
#include <string>
#include "common/logging/log.h"

namespace WebService::Changelog {

/// Backend for generating changelogs
class Backend {
public:
    /**
     * Generates a commit log between start_tag and end_tag
     * @param start_tag Base tag (e.g. nightly-777)
     * @param end_tag Compare tag (e.g. nightly-788)
     * @param func Callback function when operation is finished
     */
    virtual std::future<std::string> GetCommitLog(const std::string& start_tag,
                                                  const std::string& end_tag,
                                                  bool include_changed_lines,
                                                  std::function<void()> func) = 0;

    /**
     * Generates diff for canary-merge PRs
     * @param start_tag Base tag (e.g. canary-500)
     * @param end_tag Compare tag (e.g. canary-520)
     * @param func Callback function when operation is finished
     */
    virtual std::future<std::string> GetCanaryMergeChange(const std::string& start_tag,
                                                          const std::string& end_tag,
                                                          std::function<void()> func) = 0;
};

/// A null backend used only when no backend is available.
class NullBackend : public Backend {
public:
    std::future<std::string> GetCommitLog(const std::string& start_tag, const std::string& end_tag,
                                          bool include_changed_lines,
                                          std::function<void()> func) override {
        return std::async(std::launch::deferred, [func]() {
            LOG_ERROR(WebService, "missing Changelog backend");
            func();
            return std::string("");
        });
    }

    std::future<std::string> GetCanaryMergeChange(const std::string& start_tag,
                                                  const std::string& end_tag,
                                                  std::function<void()> func) override {
        return std::async(std::launch::deferred, [func]() {
            LOG_ERROR(WebService, "missing Changelog backend");
            func();
            return std::string("");
        });
    }
};

} // namespace WebService::Changelog
