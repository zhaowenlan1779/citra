// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QInputDialog>
#include <QList>
#include <QtConcurrent/QtConcurrentRun>
#include "citra_qt/game_list_p.h"
#include "citra_qt/main.h"
#include "citra_qt/multiplayer/client_room.h"
#include "citra_qt/multiplayer/lobby.h"
#include "citra_qt/multiplayer/lobby_p.h"
#include "citra_qt/multiplayer/message.h"
#include "citra_qt/multiplayer/state.h"
#include "citra_qt/multiplayer/validation.h"
#include "citra_qt/ui_settings.h"
#include "common/logging/log.h"
#include "core/hle/service/cfg/cfg.h"
#include "core/settings.h"
#include "network/network.h"
#ifdef ENABLE_WEB_SERVICE
#include "web_service/web_backend.h"
#endif

Lobby::Lobby(QWidget* parent, QStandardItemModel* list,
             std::shared_ptr<Core::AnnounceMultiplayerSession> session)
    : QDialog(parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowSystemMenuHint),
      ui(std::make_unique<Ui::Lobby>()), announce_multiplayer_session(session) {
    ui->setupUi(this);

    // setup the watcher for background connections
    watcher = new QFutureWatcher<void>;

    lobby_list_model = new QStandardItemModel(ui->lobby_list);
    room_list_model = new QStandardItemModel(ui->room_list);

    // Create a proxy to the game list to get the list of games owned
    game_list = new QStandardItemModel;

    for (int i = 0; i < list->rowCount(); i++) {
        auto parent = list->item(i, 0);
        for (int j = 0; j < parent->rowCount(); j++) {
            game_list->appendRow(parent->child(j)->clone());
        }
    }

    lobby_list_proxy = new LobbyListFilterProxyModel(this, game_list);
    lobby_list_proxy->setSourceModel(lobby_list_model);
    lobby_list_proxy->setDynamicSortFilter(true);
    lobby_list_proxy->setSortLocaleAware(true);
    ui->lobby_list->setModel(lobby_list_proxy);
    ui->lobby_list->header()->setSectionResizeMode(QHeaderView::Interactive);
    ui->lobby_list->header()->stretchLastSection();
    ui->lobby_list->setSelectionMode(QHeaderView::SingleSelection);
    ui->lobby_list->setSelectionBehavior(QHeaderView::SelectRows);
    ui->lobby_list->setSortingEnabled(true);
    ui->lobby_list->setEditTriggers(QHeaderView::NoEditTriggers);
    ui->lobby_list->setEditTriggers(QHeaderView::NoEditTriggers);

    room_list_proxy = new RoomListFilterProxyModel(this, game_list);
    room_list_proxy->setSourceModel(room_list_model);
    room_list_proxy->setDynamicSortFilter(true);
    room_list_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    room_list_proxy->setSortLocaleAware(true);
    ui->room_list->setModel(room_list_proxy);
    ui->room_list->header()->setSectionResizeMode(QHeaderView::Interactive);
    ui->room_list->header()->stretchLastSection();
    ui->room_list->setAlternatingRowColors(true);
    ui->room_list->setSelectionMode(QHeaderView::SingleSelection);
    ui->room_list->setSelectionBehavior(QHeaderView::SelectRows);
    ui->room_list->setVerticalScrollMode(QHeaderView::ScrollPerPixel);
    ui->room_list->setHorizontalScrollMode(QHeaderView::ScrollPerPixel);
    ui->room_list->setSortingEnabled(true);
    ui->room_list->setEditTriggers(QHeaderView::NoEditTriggers);
    ui->room_list->setExpandsOnDoubleClick(false);
    ui->room_list->setContextMenuPolicy(Qt::CustomContextMenu);

    ui->nickname->setValidator(validation.GetNickname());
    ui->nickname->setText(UISettings::values.nickname);
    if (ui->nickname->text().isEmpty() && !Settings::values.citra_username.empty()) {
        // Use Citra Web Service user name as nickname by default
        ui->nickname->setText(QString::fromStdString(Settings::values.citra_username));
    }

    // UI Buttons
    connect(ui->refresh_list, &QPushButton::pressed, this, &Lobby::RefreshLobby);
    connect(ui->hide_full, &QCheckBox::stateChanged, room_list_proxy,
            &RoomListFilterProxyModel::SetFilterFull);
    connect(ui->search, &QLineEdit::textChanged, room_list_proxy,
            &RoomListFilterProxyModel::SetFilterSearch);
    connect(ui->lobby_list, &QTreeView::clicked, this, &Lobby::PopulateRoomList);
    connect(ui->room_list, &QTreeView::doubleClicked, this, &Lobby::OnJoinRoom);
    connect(ui->room_list, &QTreeView::clicked, this, &Lobby::OnExpandRoom);

    // Actions
    connect(&lobby_list_watcher, &QFutureWatcher<AnnounceMultiplayerRoom::LobbyList>::finished,
            this, [this] {
                if (room_list_watcher.isFinished())
                    OnRefreshLobby();
            });
    connect(&room_list_watcher, &QFutureWatcher<AnnounceMultiplayerRoom::RoomList>::finished, this,
            [this] {
                if (lobby_list_watcher.isFinished())
                    OnRefreshLobby();
            });

    // manually start a refresh when the window is opening
    // TODO(jroweboy): if this refresh is slow for people with bad internet, then don't do it as
    // part of the constructor, but offload the refresh until after the window shown. perhaps emit a
    // refreshroomlist signal from places that open the lobby
    RefreshLobby();
}

void Lobby::RetranslateUi() {
    ui->retranslateUi(this);
}

QString Lobby::PasswordPrompt() {
    bool ok;
    const QString text = QInputDialog::getText(this, tr("Password Required to Join"),
                                               tr("Password:"), QLineEdit::Password, "", &ok);
    return ok ? text : QString();
}

void Lobby::OnExpandRoom(const QModelIndex& index) {
    QModelIndex member_index = room_list_proxy->index(index.row(), Column::MEMBER);
    auto member_list =
        room_list_proxy->data(member_index, RoomListItemMemberList::MemberListRole).toList();
}

void Lobby::OnJoinRoom(const QModelIndex& source) {
    if (const auto member = Network::GetRoomMember().lock()) {
        // Prevent the user from trying to join a room while they are already joining.
        if (member->GetState() == Network::RoomMember::State::Joining) {
            return;
        } else if (member->GetState() == Network::RoomMember::State::Joined) {
            // And ask if they want to leave the room if they are already in one.
            if (!NetworkMessage::WarnDisconnect()) {
                return;
            }
        }
    }
    QModelIndex index = source;
    // If the user double clicks on a child row (aka the player list) then use the parent instead
    if (source.parent() != QModelIndex()) {
        index = source.parent();
    }
    if (!ui->nickname->hasAcceptableInput()) {
        NetworkMessage::ShowError(NetworkMessage::USERNAME_NOT_VALID);
        return;
    }

    // Get a password to pass if the room is password protected
    QModelIndex password_index = room_list_proxy->index(index.row(), Column::ROOM_NAME);
    bool has_password =
        room_list_proxy->data(password_index, RoomListItemName::PasswordRole).toBool();
    const std::string password = has_password ? PasswordPrompt().toStdString() : "";
    if (has_password && password.empty()) {
        return;
    }

    QModelIndex connection_index = room_list_proxy->index(index.row(), Column::OWNER);
    const std::string nickname = ui->nickname->text().toStdString();
    const std::string ip =
        room_list_proxy->data(connection_index, RoomListItemOwner::IPRole).toString().toStdString();
    int port = room_list_proxy->data(connection_index, RoomListItemOwner::PortRole).toInt();
    const std::string id = room_list_proxy->data(connection_index, RoomListItemOwner::RoomIdRole)
                               .toString()
                               .toStdString();

    // attempt to connect in a different thread
    QFuture<void> f = QtConcurrent::run([nickname, ip, port, password, id] {
        std::string token;
#ifdef ENABLE_WEB_SERVICE
        if (!Settings::values.citra_username.empty() && !Settings::values.citra_token.empty()) {
            WebService::Client client(Settings::values.web_api_url, Settings::values.citra_username,
                                      Settings::values.citra_token);
            token = client.GetExternalJWT(id).returned_data;
            if (token.empty()) {
                LOG_ERROR(WebService, "Could not get external JWT, verification may fail");
            } else {
                LOG_INFO(WebService, "Successfully requested external JWT: size={}", token.size());
            }
        }
#endif
        if (auto room_member = Network::GetRoomMember().lock()) {
            room_member->Join(nickname, Service::CFG::GetConsoleIdHash(Core::System::GetInstance()),
                              ip.c_str(), port, 0, Network::NoPreferredMac, password, token);
        }
    });
    watcher->setFuture(f);

    // TODO(jroweboy): disable widgets and display a connecting while we wait

    // Save settings
    UISettings::values.nickname = ui->nickname->text();
    UISettings::values.ip =
        room_list_proxy->data(connection_index, RoomListItemOwner::IPRole).toString();
    UISettings::values.port =
        room_list_proxy->data(connection_index, RoomListItemOwner::PortRole).toString();
    Settings::Apply();
}

void Lobby::ResetLobbyListModel() {
    lobby_list_model->clear();
    lobby_list_model->insertColumns(0, 1);
    lobby_list_model->setHeaderData(0, Qt::Horizontal, tr("Lobbies"), Qt::DisplayRole);
}

void Lobby::ResetRoomListModel() {
    room_list_model->clear();
    room_list_model->insertColumns(0, Column::TOTAL);
    room_list_model->setHeaderData(Column::EXPAND, Qt::Horizontal, "", Qt::DisplayRole);
    room_list_model->setHeaderData(Column::ROOM_NAME, Qt::Horizontal, tr("Room"), Qt::DisplayRole);
    room_list_model->setHeaderData(Column::OWNER, Qt::Horizontal, tr("Owner"), Qt::DisplayRole);
    room_list_model->setHeaderData(Column::RENTER, Qt::Horizontal, tr("Renter"), Qt::DisplayRole);
    room_list_model->setHeaderData(Column::MEMBER, Qt::Horizontal, tr("Players"), Qt::DisplayRole);
}

void Lobby::RefreshLobby() {
    if (auto session = announce_multiplayer_session.lock()) {
        ResetRoomListModel();
        ResetLobbyListModel();
        ui->refresh_list->setEnabled(false);
        ui->refresh_list->setText(tr("Refreshing"));
        lobby_list_watcher.setFuture(
            QtConcurrent::run([session]() { return session->GetLobbyList(); }));
        room_list_watcher.setFuture(
            QtConcurrent::run([session]() { return session->GetRoomList(); }));
    } else {
        // TODO(jroweboy): Display an error box about announce couldn't be started
    }
}

void Lobby::PopulateRoomList(const QModelIndex& index) {
    if (index == previous_index)
        return;
    previous_index = index;

    ResetRoomListModel();
    auto lobby_id = lobby_list_model->itemFromIndex(index)
                        ->data(LobbyListItem::LobbyIdRole)
                        .toString()
                        .toStdString();

    for (auto room : lobbies[lobby_id]) {
        QList<QVariant> members;
        for (auto member : room.members) {
            QVariant var;
            var.setValue(LobbyMember{QString::fromStdString(member.username),
                                     QString::fromStdString(member.nickname), member.game_id,
                                     QString::fromStdString(member.game_name)});
            members.append(var);
        }

        auto first_item = new RoomListItem();
        auto row = QList<QStandardItem*>({
            first_item,
            new RoomListItemName(room.has_password, QString::fromStdString(room.name)),
            new RoomListItemOwner(QString::fromStdString(room.owner.username),
                                  QString::fromStdString(room.ip), room.port,
                                  QString::fromStdString(room.id)),
            new RoomListItemRenter(QString::fromStdString(room.renter.username)),
            new RoomListItemMemberList(members, room.max_player),
        });
        room_list_model->appendRow(row);
        // To make the rows expandable, add the member data as a child of the first column of the
        // rows with people in them and have qt set them to colspan after the room_list_model is
        // finished resetting
        if (!room.members.empty()) {
            first_item->appendRow(new RoomListItemExpandedMemberList(members));
        }
    }

    // Reenable the refresh button and resize the columns
    ui->refresh_list->setEnabled(true);
    ui->refresh_list->setText(tr("Refresh List"));
    ui->room_list->header()->stretchLastSection();
    for (int i = 0; i < Column::TOTAL - 1; ++i) {
        ui->room_list->resizeColumnToContents(i);
    }

    // Set the member list child items to span all columns
    for (int i = 0; i < room_list_proxy->rowCount(); i++) {
        auto parent = room_list_model->item(i, 0);
        for (int j = 0; j < parent->rowCount(); j++) {
            ui->room_list->setFirstColumnSpanned(j, room_list_proxy->index(i, 0), true);
        }
    }
}

void Lobby::OnRefreshLobby() {
    lobby_list = lobby_list_watcher.result();
    room_list = room_list_watcher.result();

    lobbies.clear();
    for (auto room : room_list) {
        lobbies[room.lobby_id].push_back(std::move(room));
    }

    QString previous_lobby;
    if (previous_index.isValid())
        previous_lobby = lobby_list_model->itemFromIndex(previous_index)
                             ->data(LobbyListItem::LobbyIdRole)
                             .toString();

    for (auto lobby : lobby_list) {
        QStringList game_ids;
        for (const auto& game_id : lobby.game_ids) {
            game_ids << QString::fromStdString(game_id);
        }
        lobby_list_model->appendRow(new LobbyListItem(QString::fromStdString(lobby.id),
                                                      QString::fromStdString(lobby.name),
                                                      lobby.player_count, game_ids));
    }
}

LobbyListFilterProxyModel::LobbyListFilterProxyModel(QWidget* parent, QStandardItemModel* list)
    : QSortFilterProxyModel(parent), game_list(list) {}

bool LobbyListFilterProxyModel::filterAcceptsRow(int sourceRow,
                                                 const QModelIndex& sourceParent) const {
    // filter by game owned
    if (filter_owned) {
        QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
        QList<QModelIndex> owned_games;
        for (int r = 0; r < game_list->rowCount(); ++r) {
            owned_games.append(QModelIndex(game_list->index(r, 0)));
        }
        auto game_ids =
            sourceModel()->data(index, LobbyListItem::LobbyGameIdListRole).toStringList();
        if (game_ids.isEmpty()) {
            return false;
        }
        bool owned = false;
        for (const auto& game : owned_games) {
            auto game_id = game_list->data(game, GameListItemPath::ProgramIdRole).toLongLong();
            if (std::find(game_ids.begin(), game_ids.end(), QString::number(game_id, 16)) !=
                game_ids.end()) {

                owned = true;
                break;
            }
        }
        if (!owned) {
            return false;
        }
    }

    return true;
}

void LobbyListFilterProxyModel::SetFilterOwned(bool filter) {
    filter_owned = filter;
    invalidate();
}

void LobbyListFilterProxyModel::sort(int column, Qt::SortOrder order) {
    sourceModel()->sort(column, order);
}

RoomListFilterProxyModel::RoomListFilterProxyModel(QWidget* parent, QStandardItemModel* list)
    : QSortFilterProxyModel(parent), game_list(list) {}

bool RoomListFilterProxyModel::filterAcceptsRow(int sourceRow,
                                                const QModelIndex& sourceParent) const {
    // Prioritize filters by fastest to compute

    // pass over any child rows (aka row that shows the players in the room)
    if (sourceParent != QModelIndex()) {
        return true;
    }

    // filter by filled rooms
    if (filter_full) {
        QModelIndex member_list = sourceModel()->index(sourceRow, Column::MEMBER, sourceParent);
        int player_count = sourceModel()
                               ->data(member_list, RoomListItemMemberList::MemberListRole)
                               .toList()
                               .size();
        int max_players =
            sourceModel()->data(member_list, RoomListItemMemberList::MaxPlayerRole).toInt();
        if (player_count >= max_players) {
            return false;
        }
    }

    // filter by search parameters
    if (!filter_search.isEmpty()) {
        QModelIndex room_name = sourceModel()->index(sourceRow, Column::ROOM_NAME, sourceParent);
        QModelIndex host_name = sourceModel()->index(sourceRow, Column::OWNER, sourceParent);
        bool room_name_match = sourceModel()
                                   ->data(room_name, RoomListItemName::NameRole)
                                   .toString()
                                   .contains(filter_search, filterCaseSensitivity());
        bool username_match = sourceModel()
                                  ->data(host_name, RoomListItemOwner::OwnerUsernameRole)
                                  .toString()
                                  .contains(filter_search, filterCaseSensitivity());
        if (!room_name_match && !username_match) {
            return false;
        }
    }

    return true;
}

void RoomListFilterProxyModel::sort(int column, Qt::SortOrder order) {
    sourceModel()->sort(column, order);
}

void RoomListFilterProxyModel::SetFilterFull(bool filter) {
    filter_full = filter;
    invalidate();
}

void RoomListFilterProxyModel::SetFilterSearch(const QString& filter) {
    filter_search = filter;
    invalidate();
}
