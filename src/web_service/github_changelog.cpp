// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <regex>
#include <unordered_map>
#include "common/string_util.h"
#include "web_service/github_changelog.h"
#include "web_service/json.h"
#include "web_service/web_backend.h"

namespace WebService {

std::future<std::string> GithubChangelog::GetCommitLog(const std::string& start_tag,
                                                       const std::string& end_tag,
                                                       bool include_changed_lines,
                                                       std::function<void()> func) {
    auto get_func = [=](const std::string& reply) -> std::string {
        if (reply.empty()) {
            func();
            return "";
        }

        nlohmann::json json = nlohmann::json::parse(reply);
        std::string result;
        try {
            std::string compare_url = json.at("html_url");
            result = Common::StringFromFormat("<h2>Citra Changelog: from %s to %s</h2>",
                                              start_tag.c_str(), end_tag.c_str());
            if (json.at("total_commits").get<int>() > 250) {
                result += "<br>There are so many changes! Due to Github's limitations, we can only "
                          "get you some of them."
                          "<br>You can view the full list of changes <a href=\"" +
                          compare_url + "\">here</a>.<br>";
            }
            for (const auto& commit : json.at("commits").get<std::vector<nlohmann::json>>()) {
                if (commit.at("parents").size() > 1)
                    continue; // Skip merge commits
                std::string sha = commit.at("sha").get<std::string>().substr(0, 7);
                std::string commit_url = commit.at("html_url");
                std::string author = commit.at("commit").at("author").at("name");
                std::string author_url = commit.at("author").at("html_url");
                if (author == "citrabot")
                    continue; // Skip bot commits
                std::string message = commit.at("commit").at("message");
                size_t pos = message.find('\n');
                if (pos < message.size()) {
                    message = message.substr(0, pos); // Get only title
                }
                result += Common::StringFromFormat(
                    R"(<br>[<a href="%s">%s</a>] <a href="%s">%s</a>: %s)", commit_url.c_str(),
                    sha.c_str(), author_url.c_str(), author.c_str(), message.c_str());
            }
            if (include_changed_lines) {
                int files_changed = 0, lines_changed = 0;
                for (const auto& file : json.at("files").get<std::vector<nlohmann::json>>()) {
                    files_changed++;
                    lines_changed += file.at("changes").get<int>();
                }
                result += Common::StringFromFormat(
                    R"(<br>During this time, <a href="%s">%d files and %d lines</a> have been changed!)",
                    compare_url.c_str(), files_changed, lines_changed);
            }
            LOG_INFO(WebService, "Successfully generated commit log");
        } catch (nlohmann::json::exception& e) {
            return "";
        }

        func();
        return result;
    };

    return GetJson<std::string>(get_func,
                                Common::StringFromFormat("%s/%s...%s", compare_endpoint.c_str(),
                                                         start_tag.c_str(), end_tag.c_str()),
                                true);
}

std::future<std::string> GithubChangelog::GetCanaryMergeChange(const std::string& start_tag,
                                                               const std::string& end_tag,
                                                               std::function<void()> func) {
    std::string start_url =
        Common::StringFromFormat("%s/%s/README.md", raw_endpoint.c_str(), start_tag.c_str());
    std::string end_url =
        Common::StringFromFormat("%s/%s/README.md", raw_endpoint.c_str(), end_tag.c_str());
    LOG_INFO(WebService, "start: {}, end: {}", start_url, end_url);
    auto get_func = [=](const std::string& reply) -> std::string {
        std::string start_md = reply;
        std::string end_md =
            GetPlain<std::string>([](const std::string& reply_) -> std::string { return reply_; },
                                  end_url, true)
                .get();
        if (start_md.empty() || end_md.empty()) {
            func();
            return "";
        }
        static std::regex merge_entry_regex(
            R"(\|\[(\d+)]\((.+)\)\|\[([0-9a-f]{7})\]\((.+)\)\|(.+)\|\[(.+)]\((.+)\)\|Yes\|\n)");
        std::string result;
        result = "<br>The following is diff of canary-merged PRs:";
        std::smatch match_results;
        struct PREntry {
            std::string name;
            std::string html_url;
            std::string commit;
            std::string commit_url;
            std::string author;
            std::string author_url;
        };
        std::unordered_map<int, PREntry> start_prs;
        std::unordered_map<int, PREntry> end_prs;

        const int number_threshold = 3000; // To ignore PRs from the citra-canary repo

        while (std::regex_search(start_md, match_results, merge_entry_regex)) {
            if (match_results.length() < 8) {
                LOG_ERROR(WebService, "match_results is too short");
                start_md = match_results.suffix();
                continue;
            }
            int number = std::atoi(match_results[1].str().c_str());
            if (number >= number_threshold) {
                PREntry entry{match_results[5], match_results[2], match_results[3],
                              match_results[4], match_results[6], match_results[7]};
                start_prs.emplace(number, entry);
            }
            start_md = match_results.suffix();
        }
        while (std::regex_search(end_md, match_results, merge_entry_regex)) {
            if (match_results.length() < 8) {
                LOG_ERROR(WebService, "match_results is too short");
                end_md = match_results.suffix();
                continue;
            }
            int number = std::atoi(match_results[1].str().c_str());
            if (number >= number_threshold) {
                PREntry entry{match_results[5], match_results[2], match_results[3],
                              match_results[4], match_results[6], match_results[7]};
                end_prs.emplace(number, entry);
            }
            end_md = match_results.suffix();
        }

        for (const auto& pr : start_prs) {
            if (!end_prs.count(pr.first)) {
                result += Common::StringFromFormat(
                    R"(<br>[Removed] <a href="%s">#%d</a> by <a href="%s">%s</a>: <a href="%s">%s</a> %s )",
                    pr.second.html_url.c_str(), pr.first, pr.second.author_url.c_str(),
                    pr.second.author.c_str(), pr.second.commit_url.c_str(),
                    pr.second.commit.c_str(), pr.second.name.c_str());
            } else {
                const auto& new_pr = end_prs.at(pr.first);
                if (new_pr.commit == pr.second.commit)
                    continue;
                result += Common::StringFromFormat(
                    R"(<br>[Updated] <a href="%s">#%d</a> by <a href="%s">%s</a>: <a href="%s">%s</a> -> <a href="%s">%s</a> %s )",
                    pr.second.html_url.c_str(), pr.first, pr.second.author_url.c_str(),
                    pr.second.author.c_str(), pr.second.commit_url.c_str(),
                    pr.second.commit.c_str(), new_pr.commit_url.c_str(), new_pr.commit.c_str(),
                    pr.second.name.c_str());
            }
        }
        for (const auto& pr : end_prs) {
            if (!start_prs.count(pr.first)) {
                result += Common::StringFromFormat(
                    R"(<br>[New] <a href="%s">#%d</a> by <a href="%s">%s</a>: <a href="%s">%s</a> %s )",
                    pr.second.html_url.c_str(), pr.first, pr.second.author_url.c_str(),
                    pr.second.author.c_str(), pr.second.commit_url.c_str(),
                    pr.second.commit.c_str(), pr.second.name.c_str());
            }
        }

        func();
        return result;
    };

    return GetPlain<std::string>(get_func, start_url, true);
}

GithubChangelog::GithubChangelog(const std::string& compare_endpoint_,
                                 const std::string& raw_endpoint_)
    : compare_endpoint(compare_endpoint_), raw_endpoint(raw_endpoint_) {}

} // namespace WebService
