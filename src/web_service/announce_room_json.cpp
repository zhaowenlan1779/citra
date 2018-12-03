// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <future>
#include <json.hpp>
#include "common/detached_tasks.h"
#include "common/logging/log.h"
#include "web_service/announce_room_json.h"
#include "web_service/web_backend.h"

namespace AnnounceMultiplayerRoom {

void to_json(nlohmann::json& json, const Room::Member& member) {
    json["username"] = member.username;
    json["nickname"] = member.nickname;
    json["avatarUrl"] = member.avatar_url;
    json["gameName"] = member.game_name;
    json["gameId"] = member.game_id;
}

void from_json(const nlohmann::json& json, Room::Member& member) {
    member.username = json.at("username").get<std::string>();
    member.nickname = json.at("nickname").get<std::string>();
    member.avatar_url = json.at("avatarUrl").get<std::string>();
    member.game_name = json.at("gameName").get<std::string>();
    member.game_id = json.at("gameId").get<u64>();
}

void to_json(nlohmann::json& json, const Room& room) {
    json["port"] = room.port;
    json["name"] = room.name;
    json["description"] = room.description;
    json["preferredGameNames"] = room.preferred_games;
    json["preferredGameIds"] = room.preferred_game_ids;
    json["maxPlayers"] = room.max_player;
    json["netVersion"] = room.net_version;
    json["hasPassword"] = room.has_password;
    if (room.members.size() > 0) {
        nlohmann::json member_json = room.members;
        json["players"] = member_json;
    }
}

void from_json(const nlohmann::json& json, Room& room) {
    room.verify_UID = json.at("externalGuid").get<std::string>();
    room.ip = json.at("address").get<std::string>();
    room.name = json.at("name").get<std::string>();
    try {
        room.description = json.at("description").get<std::string>();
    } catch (const nlohmann::detail::out_of_range& e) {
        room.description = "";
        LOG_DEBUG(Network, "Room \'{}\' doesn't contain a description", room.name);
    }
    room.owner = json.at("owner").get<std::string>();
    room.port = json.at("port").get<u16>();
    room.preferred_games = json.at("preferredGameNames").get<std::vector<std::string>>();
    room.preferred_game_ids = json.at("preferredGameIds").get<std::vector<u64>>();
    room.max_player = json.at("maxPlayers").get<u32>();
    room.net_version = json.at("netVersion").get<u32>();
    room.has_password = json.at("hasPassword").get<bool>();
    try {
        room.members = json.at("players").get<std::vector<Room::Member>>();
    } catch (const nlohmann::detail::out_of_range& e) {
        LOG_DEBUG(Network, "Out of range {}", e.what());
    }
}

} // namespace AnnounceMultiplayerRoom

namespace WebService {

void RoomJson::SetRoomInformation(const std::string& name, const std::string& description,
                                  const u16 port, const u32 max_player, const u32 net_version,
                                  const bool has_password,
                                  const std::vector<std::string>& preferred_games,
                                  const std::vector<u64>& preferred_game_ids) {
    room.name = name;
    room.description = description;
    room.port = port;
    room.max_player = max_player;
    room.net_version = net_version;
    room.has_password = has_password;
    room.preferred_games = preferred_games;
    room.preferred_game_ids = preferred_game_ids;
}
void RoomJson::AddPlayer(const std::string& username, const std::string& nickname,
                         const std::string& avatar_url,
                         const AnnounceMultiplayerRoom::MacAddress& mac_address, const u64 game_id,
                         const std::string& game_name) {
    AnnounceMultiplayerRoom::Room::Member member;
    member.username = username;
    member.nickname = nickname;
    member.avatar_url = avatar_url;
    member.mac_address = mac_address;
    member.game_id = game_id;
    member.game_name = game_name;
    room.members.push_back(member);
}

Common::WebResult RoomJson::Update() {
    if (room_id.empty()) {
        LOG_ERROR(WebService, "Room must be registered to be updated");
        return Common::WebResult{Common::WebResult::Code::LibError, "Room is not registered"};
    }
    nlohmann::json json{{"players", room.members}};
    return client.PostJson(fmt::format("/lobby2/{}", room_id), json.dump(), false);
}

std::string RoomJson::Register() {
    nlohmann::json json = room;
    auto reply = client.PostJson("/lobby2", json.dump(), false).returned_data;
    if (reply.empty()) {
        return "";
    }
    auto reply_json = nlohmann::json::parse(reply);
    room = reply_json.get<AnnounceMultiplayerRoom::Room>();
    room_id = reply_json.at("id").get<std::string>();
    return room.verify_UID;
}

void RoomJson::ClearPlayers() {
    room.members.clear();
}

AnnounceMultiplayerRoom::RoomList RoomJson::GetRoomList() {
    auto reply = client.GetJson("/lobby2", true).returned_data;
    if (reply.empty()) {
        return {};
    }
    return nlohmann::json::parse(reply).at("rooms").get<AnnounceMultiplayerRoom::RoomList>();
}

void RoomJson::Delete() {
    if (room_id.empty()) {
        LOG_ERROR(WebService, "Room must be registered to be deleted");
        return;
    }
    Common::DetachedTasks::AddTask(
        [host{this->host}, username{this->username}, token{this->token}, room_id{this->room_id}]() {
            // create a new client here because the this->client might be destroyed.
            Client{host, username, token}.DeleteJson(fmt::format("/lobby2/{}", room_id), "", false);
        });
}

} // namespace WebService
