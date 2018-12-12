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
    if (!member.username.empty()) {
        json["username"] = member.username;
    }
    json["nickname"] = member.nickname;
    json["gameName"] = member.game_name;
    json["gameTitleId"] = member.game_id;
}

void from_json(const nlohmann::json& json, Room::Member& member) {
    member.username = json.at("username").get<std::string>();
    member.nickname = json.at("nickname").get<std::string>();
    member.game_name = json.at("gameName").get<std::string>();
    member.game_id = json.at("gameTitleId").get<u64>();
}

void to_json(nlohmann::json& json, const Room::User& user) {
    json["id"] = user.id;
    json["username"] = user.username;
}

void from_json(const nlohmann::json& json, Room::User& user) {
    user.id = json.at("id").get<std::string>();
    user.username = json.at("username").get<std::string>();
}

void to_json(nlohmann::json& json, const Room& room) {
    json["port"] = room.port;
    json["netVersion"] = room.net_version;
}

void from_json(const nlohmann::json& json, Room& room) {
    room.id = json.at("id").get<std::string>();
    room.owner = json.at("owner").get<Room::User>();
    room.renter = json.at("renter").get<Room::User>();
    room.lobby_id = json.at("lobbyId").get<std::string>();
    room.name = json.at("name").get<std::string>();
    room.has_password = json.at("hasPassword").get<bool>();
    room.max_player = json.at("maxPlayers").get<u32>();
    room.ip = json.at("ipAddress").get<std::string>();
    room.port = json.at("portNumber").get<u16>();
    room.members = json.at("players").get<std::vector<Room::Member>>();
}

void from_json(const nlohmann::json& json, Lobby& lobby) {
    lobby.id = json.at("id").get<std::string>();
    lobby.name = json.at("name").get<std::string>();
    lobby.game_ids = json.at("gameIds").get<std::vector<std::string>>();
    lobby.player_count = json.at("playerCount").get<u32>();
}

} // namespace AnnounceMultiplayerRoom

namespace WebService {

void RoomJson::SetRoomInformation(const u16 port, const u32 net_version) {
    room.port = port;
    room.net_version = net_version;
}
void RoomJson::AddPlayer(const std::string& username, const std::string& nickname,
                         const AnnounceMultiplayerRoom::MacAddress& mac_address, const u64 game_id,
                         const std::string& game_name) {
    AnnounceMultiplayerRoom::Room::Member member;
    member.username = username;
    member.nickname = nickname;
    member.mac_address = mac_address;
    member.game_id = game_id;
    member.game_name = game_name;
    room.members.push_back(member);
}

Common::WebResult RoomJson::Update() {
    if (room.id.empty()) {
        LOG_ERROR(WebService, "Room must be registered to be updated");
        return Common::WebResult{Common::WebResult::Code::LibError, "Room is not registered"};
    }
    nlohmann::json json{{"players", room.members}};
    auto reply = client.PutJson(fmt::format("/multiplayer/rooms/{}", room.id), json.dump(), false);
    auto reply_json = nlohmann::json::parse(reply.returned_data);
    room.lobby_id = reply_json.at("lobbyId").get<std::string>();
    room.max_player = reply_json.at("maxPlayers").get<u32>();
    password = reply_json.at("password").get<std::string>();
    return reply;
}

void RoomJson::Register() {
    nlohmann::json json = room;
    auto reply = client.PostJson("/multiplayer/rooms", json.dump(), false).returned_data;
    if (reply.empty()) {
        LOG_ERROR(WebService, "Failed to register the room");
        return;
    }
    auto reply_json = nlohmann::json::parse(reply);
    room = reply_json.get<AnnounceMultiplayerRoom::Room>();
}

void RoomJson::ClearPlayers() {
    room.members.clear();
}

AnnounceMultiplayerRoom::LobbyList RoomJson::GetLobbyList() {
    auto reply = client.GetJson("/multiplayer/lobbies", true).returned_data;
    if (reply.empty()) {
        return {};
    }
    return nlohmann::json::parse(reply).get<AnnounceMultiplayerRoom::LobbyList>();
}

AnnounceMultiplayerRoom::RoomList RoomJson::GetRoomList() {
    auto reply = client.GetJson("/multiplayer/rooms", true).returned_data;
    if (reply.empty()) {
        return {};
    }
    return nlohmann::json::parse(reply).get<AnnounceMultiplayerRoom::RoomList>();
}

void RoomJson::Delete() {
    if (room.id.empty()) {
        LOG_ERROR(WebService, "Room must be registered to be deleted");
        return;
    }
    Common::DetachedTasks::AddTask(
        [host{this->host}, username{this->username}, token{this->token}, room_id{this->room.id}]() {
            // create a new client here because the this->client might be destroyed.
            Client{host, username, token}.DeleteJson(fmt::format("/multiplayer/rooms/{}", room_id),
                                                     "", false);
        });
}

AnnounceMultiplayerRoom::Room RoomJson::GetRoomInformation() const {
    return room;
}

std::string RoomJson::GetPassword() const {
    return password;
}

std::optional<AnnounceMultiplayerRoom::Room> RoomJson::ClaimRoom(const std::string& lobby_id,
                                                                 const std::string& password) {
    nlohmann::json json{{"lobbyId", lobby_id}, {"password", password}};
    auto reply = client.PutJson("/multiplayer/rooms/claim", json.dump(), false).returned_data;
    if (reply.empty()) {
        return {};
    }
    return nlohmann::json::parse(reply).get<AnnounceMultiplayerRoom::Room>();
}

} // namespace WebService
