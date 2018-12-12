// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <vector>
#include "announce_multiplayer_session.h"
#include "common/announce_multiplayer_room.h"
#include "common/assert.h"
#include "core/settings.h"
#include "network/network.h"

#ifdef ENABLE_WEB_SERVICE
#include "web_service/announce_room_json.h"
#endif

namespace Core {

// Time between room is announced to web_service
static constexpr std::chrono::seconds announce_time_interval(15);

AnnounceMultiplayerSession::AnnounceMultiplayerSession() {
#ifdef ENABLE_WEB_SERVICE
    backend = std::make_unique<WebService::RoomJson>(Settings::values.web_api_url,
                                                     Settings::values.citra_username,
                                                     Settings::values.citra_token);
#else
    backend = std::make_unique<AnnounceMultiplayerRoom::NullBackend>();
#endif
}

void AnnounceMultiplayerSession::Register() {
    std::shared_ptr<Network::Room> room = Network::GetRoom().lock();
    if (!room) {
        return;
    }
    if (room->GetState() != Network::Room::State::Open) {
        return;
    }
    backend->SetRoomInformation(room->GetRoomInformation().port, Network::network_version);
    backend->Register();
    LOG_INFO(WebService, "Room has been registered");
}

void AnnounceMultiplayerSession::Start() {
    if (announce_multiplayer_thread) {
        Stop();
    }
    shutdown_event.Reset();
    announce_multiplayer_thread =
        std::make_unique<std::thread>(&AnnounceMultiplayerSession::AnnounceMultiplayerLoop, this);
}

void AnnounceMultiplayerSession::Stop() {
    if (announce_multiplayer_thread) {
        shutdown_event.Set();
        announce_multiplayer_thread->join();
        announce_multiplayer_thread.reset();
        backend->Delete();
    }
}

AnnounceMultiplayerSession::CallbackHandle AnnounceMultiplayerSession::BindErrorCallback(
    std::function<void(const Common::WebResult&)> function) {
    std::lock_guard<std::mutex> lock(callback_mutex);
    auto handle = std::make_shared<std::function<void(const Common::WebResult&)>>(function);
    error_callbacks.insert(handle);
    return handle;
}

void AnnounceMultiplayerSession::UnbindErrorCallback(CallbackHandle handle) {
    std::lock_guard<std::mutex> lock(callback_mutex);
    error_callbacks.erase(handle);
}

AnnounceMultiplayerSession::~AnnounceMultiplayerSession() {
    Stop();
}

void AnnounceMultiplayerSession::AnnounceMultiplayerLoop() {
    Register();
    auto update_time = std::chrono::steady_clock::now();
    std::future<Common::WebResult> future;
    while (!shutdown_event.WaitUntil(update_time)) {
        update_time += announce_time_interval;
        std::shared_ptr<Network::Room> room = Network::GetRoom().lock();
        if (!room) {
            break;
        }
        if (room->GetState() != Network::Room::State::Open) {
            break;
        }
        // Add player data
        backend->ClearPlayers();
        for (const auto& member : room->GetRoomMemberList()) {
            backend->AddPlayer(member.username, member.nickname, member.mac_address,
                               member.game_info.id, member.game_info.name);
        }
        Common::WebResult result = backend->Update();
        if (result.result_code != Common::WebResult::Code::Success) {
            std::lock_guard<std::mutex> lock(callback_mutex);
            for (auto callback : error_callbacks) {
                (*callback)(result);
            }
        }
        if (result.result_string == "404") {
            // Needs to register the room again
            Register();
        } else {
            // Update the room with data from announce service
            room->SetRoomInformation(backend->GetRoomInformation());
            room->SetPassword(backend->GetPassword());
        }
    }
}

AnnounceMultiplayerRoom::RoomList AnnounceMultiplayerSession::GetRoomList() {
    return backend->GetRoomList();
}

AnnounceMultiplayerRoom::LobbyList AnnounceMultiplayerSession::GetLobbyList() {
    return backend->GetLobbyList();
}

} // namespace Core
