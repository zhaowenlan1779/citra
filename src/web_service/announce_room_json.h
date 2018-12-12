// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <string>
#include "common/announce_multiplayer_room.h"
#include "web_service/web_backend.h"

namespace WebService {

/**
 * Implementation of AnnounceMultiplayerRoom::Backend that (de)serializes room information into/from
 * JSON, and submits/gets it to/from the Citra web service
 */
class RoomJson : public AnnounceMultiplayerRoom::Backend {
public:
    RoomJson(const std::string& host, const std::string& username, const std::string& token)
        : client(host, username, token), host(host), username(username), token(token) {}
    ~RoomJson() = default;
    void SetRoomInformation(const u16 port, const u32 net_version) override;
    void AddPlayer(const std::string& username, const std::string& nickname,
                   const AnnounceMultiplayerRoom::MacAddress& mac_address, const u64 game_id,
                   const std::string& game_name) override;
    Common::WebResult Update() override;
    void Register() override;
    void ClearPlayers() override;
    AnnounceMultiplayerRoom::LobbyList GetLobbyList() override;
    AnnounceMultiplayerRoom::RoomList GetRoomList() override;
    void Delete() override;
    AnnounceMultiplayerRoom::Room GetRoomInformation() const override;
    std::string GetPassword() const override;
    std::optional<AnnounceMultiplayerRoom::Room> ClaimRoom(const std::string& lobby_id,
                                                           const std::string& password) override;

private:
    AnnounceMultiplayerRoom::Room room;
    Client client;
    std::string host;
    std::string username;
    std::string token;
    std::string password;
};

} // namespace WebService
