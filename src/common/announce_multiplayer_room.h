// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "common/common_types.h"
#include "common/web_result.h"

namespace AnnounceMultiplayerRoom {

using MacAddress = std::array<u8, 6>;

struct Room {
    struct Member {
        std::string username;
        std::string nickname;
        MacAddress mac_address;
        std::string game_name;
        u64 game_id;
    };
    struct User {
        std::string id;
        std::string username;
    };
    std::string id;
    std::string lobby_id;
    std::string name;
    User owner;
    User renter;
    std::string ip;
    u16 port;
    u32 max_player;
    u32 net_version;
    bool has_password;

    std::vector<Member> members;
};
using RoomList = std::vector<Room>;

struct Lobby {
    std::string id;
    std::string name;
    std::vector<std::string> game_ids;
    u32 player_count;
};
using LobbyList = std::vector<Lobby>;

/**
 * A AnnounceMultiplayerRoom interface class. A backend to submit/get to/from a web service should
 * implement this interface.
 */
class Backend : NonCopyable {
public:
    virtual ~Backend() = default;

    /**
     * Sets the Information that gets used for the announce
     * @param port The port of the room
     * @param net_version The version of the libNetwork that gets used
     */
    virtual void SetRoomInformation(const u16 port, const u32 net_version) = 0;

    /**
     * Adds a player information to the data that gets announced
     * @param nickname The nickname of the player
     * @param mac_address The MAC Address of the player
     * @param game_id The title id of the game the player plays
     * @param game_name The name of the game the player plays
     */
    virtual void AddPlayer(const std::string& username, const std::string& nickname,
                           const MacAddress& mac_address, const u64 game_id,
                           const std::string& game_name) = 0;

    /**
     * Updates the data in the announce service. Re-register the room when required.
     * The data of the room may be updated by the announce service after this call.
     * @result The result of the update attempt
     */
    virtual Common::WebResult Update() = 0;

    /**
     * Registers the data in the announce service.
     * The data of the room may be updated by the announce service after this call.
     */
    virtual void Register() = 0;

    /**
     * Empties the stored players
     */
    virtual void ClearPlayers() = 0;

    /**
     * Get the lobby information from the announce service
     * @result A list of all lobbies the announce service has
     */
    virtual LobbyList GetLobbyList() = 0;

    /**
     * Get the room information from the announce service
     * @result A list of all rooms the announce service has
     */
    virtual RoomList GetRoomList() = 0;

    /**
     * Sends a delete message to the announce service
     */
    virtual void Delete() = 0;

    /**
     * Gets the current room information.
     * Some of the data might be set by the announce service
     */
    virtual Room GetRoomInformation() const = 0;

    /**
     * Gets the password set by the announce service to update the room
     */
    virtual std::string GetPassword() const = 0;

    /**
     * Tries to claim an empty room.
     * @param lobby_id the lobby to set this room to
     * @param password the password to set
     * @return A Room if successful, std::nullopt otherwise
     */
    virtual std::optional<Room> ClaimRoom(const std::string& lobby_id,
                                          const std::string& password) = 0;
};

/**
 * Empty implementation of AnnounceMultiplayerRoom interface that drops all data. Used when a
 * functional backend implementation is not available.
 */
class NullBackend : public Backend {
public:
    ~NullBackend() = default;
    void SetRoomInformation(const u16 /*port*/, const u32 /*net_version*/) override {}
    void AddPlayer(const std::string& /*username*/, const std::string& /*nickname*/,
                   const MacAddress& /*mac_address*/, const u64 /*game_id*/,
                   const std::string& /*game_name*/) override {}
    Common::WebResult Update() override {
        return Common::WebResult{Common::WebResult::Code::NoWebservice, "WebService is missing"};
    }
    void Register() override {}
    void ClearPlayers() override {}
    LobbyList GetLobbyList() override {
        return LobbyList{};
    }
    RoomList GetRoomList() override {
        return RoomList{};
    }
    void Delete() override {}
    Room GetRoomInformation() const override {
        return Room{};
    }
    std::string GetPassword() const override {
        return "";
    }
    std::optional<Room> ClaimRoom(const std::string& /*lobby_id*/,
                                  const std::string& /*password*/) override {
        return {};
    }
};

} // namespace AnnounceMultiplayerRoom
