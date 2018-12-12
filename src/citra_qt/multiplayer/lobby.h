// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <unordered_map>
#include <QDialog>
#include <QFutureWatcher>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include "citra_qt/multiplayer/validation.h"
#include "common/announce_multiplayer_room.h"
#include "core/announce_multiplayer_session.h"
#include "network/network.h"
#include "ui_lobby.h"

class LobbyModel;
class LobbyListFilterProxyModel;
class RoomListFilterProxyModel;

/**
 * Listing of all public games pulled from services. The lobby should be simple enough for users to
 * find the game they want to play, and join it.
 */
class Lobby : public QDialog {
    Q_OBJECT

public:
    explicit Lobby(QWidget* parent, QStandardItemModel* list,
                   std::shared_ptr<Core::AnnounceMultiplayerSession> session);
    ~Lobby() = default;

    void RetranslateUi();

public slots:
    /**
     * Begin the process to pull the latest room list from web services. After the listing is
     * returned from web services, `LobbyRefreshed` will be signalled
     */
    void RefreshLobby();

private slots:
    /**
     * Pulls the list of rooms from network and fills out the lobby model with the results
     */
    void OnRefreshLobby();

    /**
     * Handler for single clicking on a room in the list. Expands the treeitem to show player
     * information for the people in the room
     *
     * index - The row of the proxy model that the user wants to join.
     */
    void OnExpandRoom(const QModelIndex&);

    /**
     * Handler for double clicking on a room in the list. Gathers the host ip and port and attempts
     * to connect. Will also prompt for a password in case one is required.
     *
     * index - The row of the proxy model that the user wants to join.
     */
    void OnJoinRoom(const QModelIndex&);

signals:
    void StateChanged(const Network::RoomMember::State&);

private:
    /**
     * Removes all entries in the lobby list model before refreshing.
     */
    void ResetLobbyListModel();

    /**
     * Removes all entries in the room list model before refreshing.
     */
    void ResetRoomListModel();

    /**
     * Loads the room list of the current selected lobby.
     */
    void PopulateRoomList(const QModelIndex& index);

    /**
     * Prompts for a password. Returns an empty QString if the user either did not provide a
     * password or if the user closed the window.
     */
    QString PasswordPrompt();

    QStandardItemModel* lobby_list_model;
    QStandardItemModel* room_list_model;
    QStandardItemModel* game_list;
    LobbyListFilterProxyModel* lobby_list_proxy;
    RoomListFilterProxyModel* room_list_proxy;

    QFutureWatcher<AnnounceMultiplayerRoom::LobbyList> lobby_list_watcher;
    QFutureWatcher<AnnounceMultiplayerRoom::RoomList> room_list_watcher;
    std::weak_ptr<Core::AnnounceMultiplayerSession> announce_multiplayer_session;
    std::unique_ptr<Ui::Lobby> ui;
    QFutureWatcher<void>* watcher;
    Validation validation;
    AnnounceMultiplayerRoom::LobbyList lobby_list;
    AnnounceMultiplayerRoom::RoomList room_list;
    std::unordered_map<std::string, AnnounceMultiplayerRoom::RoomList> lobbies;

    QModelIndex previous_index;
};

class LobbyListFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit LobbyListFilterProxyModel(QWidget* parent, QStandardItemModel* list);
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    void sort(int column, Qt::SortOrder order) override;

public slots:
    void SetFilterOwned(bool);

private:
    QStandardItemModel* game_list;
    bool filter_owned = false;
};

/**
 * Proxy Model for filtering the room list
 */
class RoomListFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT;

public:
    explicit RoomListFilterProxyModel(QWidget* parent, QStandardItemModel* list);
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    void sort(int column, Qt::SortOrder order) override;

public slots:
    // void SetFilterOwned(bool);
    void SetFilterFull(bool);
    void SetFilterSearch(const QString&);

private:
    QStandardItemModel* game_list;
    // bool filter_owned = false;
    bool filter_full = false;
    QString filter_search;
};
