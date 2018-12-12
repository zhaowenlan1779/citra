// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <utility>
#include <QPixmap>
#include <QStandardItem>
#include <QStandardItemModel>
#include "common/common_types.h"

namespace Column {
enum List {
    EXPAND,
    ROOM_NAME,
    OWNER,
    RENTER,
    MEMBER,
    TOTAL,
};
}

class RoomListItem : public QStandardItem {
public:
    RoomListItem() = default;
    explicit RoomListItem(const QString& string) : QStandardItem(string) {}
    virtual ~RoomListItem() override = default;
};

class RoomListItemName : public RoomListItem {
public:
    static const int NameRole = Qt::UserRole + 1;
    static const int PasswordRole = Qt::UserRole + 2;

    RoomListItemName() = default;
    explicit RoomListItemName(bool has_password, QString name) : RoomListItem() {
        setData(name, NameRole);
        setData(has_password, PasswordRole);
    }

    QVariant data(int role) const override {
        if (role == Qt::DecorationRole) {
            bool has_password = data(PasswordRole).toBool();
            return has_password ? QIcon::fromTheme("lock").pixmap(16) : QIcon();
        }
        if (role != Qt::DisplayRole) {
            return RoomListItem::data(role);
        }
        return data(NameRole).toString();
    }

    bool operator<(const QStandardItem& other) const override {
        return data(NameRole).toString().localeAwareCompare(other.data(NameRole).toString()) < 0;
    }
};

class RoomListItemOwner : public RoomListItem {
public:
    static const int OwnerUsernameRole = Qt::UserRole + 1;
    static const int IPRole = Qt::UserRole + 2;
    static const int PortRole = Qt::UserRole + 3;
    static const int RoomIdRole = Qt::UserRole + 4;

    RoomListItemOwner() = default;
    explicit RoomListItemOwner(QString username, QString ip, u16 port, QString id) {
        setData(username, OwnerUsernameRole);
        setData(ip, IPRole);
        setData(port, PortRole);
        setData(id, RoomIdRole);
    }

    QVariant data(int role) const override {
        if (role != Qt::DisplayRole) {
            return RoomListItem::data(role);
        }
        return data(OwnerUsernameRole).toString();
    }

    bool operator<(const QStandardItem& other) const override {
        return data(OwnerUsernameRole)
                   .toString()
                   .localeAwareCompare(other.data(OwnerUsernameRole).toString()) < 0;
    }
};

class RoomListItemRenter : public RoomListItem {
public:
    RoomListItemRenter() = default;
    explicit RoomListItemRenter(QString username) {
        setData(username, Qt::DisplayRole);
    }
};

class LobbyMember {
public:
    LobbyMember() = default;
    LobbyMember(const LobbyMember& other) = default;
    explicit LobbyMember(QString username, QString nickname, u64 title_id, QString game_name)
        : username(std::move(username)), nickname(std::move(nickname)), title_id(title_id),
          game_name(std::move(game_name)) {}
    ~LobbyMember() = default;

    QString GetName() const {
        if (username.isEmpty() || username == nickname) {
            return nickname;
        } else {
            return QString("%1 (%2)").arg(nickname, username);
        }
    }
    u64 GetTitleId() const {
        return title_id;
    }
    QString GetGameName() const {
        return game_name;
    }

private:
    QString username;
    QString nickname;
    u64 title_id;
    QString game_name;
};

Q_DECLARE_METATYPE(LobbyMember);

class RoomListItemMemberList : public RoomListItem {
public:
    static const int MemberListRole = Qt::UserRole + 1;
    static const int MaxPlayerRole = Qt::UserRole + 2;

    RoomListItemMemberList() = default;
    explicit RoomListItemMemberList(QList<QVariant> members, u32 max_players) {
        setData(members, MemberListRole);
        setData(max_players, MaxPlayerRole);
    }

    QVariant data(int role) const override {
        if (role != Qt::DisplayRole) {
            return RoomListItem::data(role);
        }
        auto members = data(MemberListRole).toList();
        return QString("%1 / %2").arg(QString::number(members.size()),
                                      data(MaxPlayerRole).toString());
    }

    bool operator<(const QStandardItem& other) const override {
        // sort by rooms that have the most players
        int left_members = data(MemberListRole).toList().size();
        int right_members = other.data(MemberListRole).toList().size();
        return left_members < right_members;
    }
};

/**
 * Member information for when a lobby is expanded in the UI
 */
class RoomListItemExpandedMemberList : public RoomListItem {
public:
    static const int MemberListRole = Qt::UserRole + 1;

    RoomListItemExpandedMemberList() = default;
    explicit RoomListItemExpandedMemberList(QList<QVariant> members) {
        setData(members, MemberListRole);
    }

    QVariant data(int role) const override {
        if (role != Qt::DisplayRole) {
            return RoomListItem::data(role);
        }
        auto members = data(MemberListRole).toList();
        QString out;
        bool first = true;
        for (const auto& member : members) {
            if (!first)
                out += '\n';
            const auto& m = member.value<LobbyMember>();
            if (m.GetGameName().isEmpty()) {
                out += QString(QObject::tr("%1 is not playing a game")).arg(m.GetName());
            } else {
                out += QString(QObject::tr("%1 is playing %2")).arg(m.GetName(), m.GetGameName());
            }
            first = false;
        }
        return out;
    }
};

class RoomListItemCreateRoom : public RoomListItem {
public:
    explicit RoomListItemCreateRoom() {
        setData(QIcon::fromTheme("plus").pixmap(32), Qt::DecorationRole);
        setData("Create Room", Qt::DisplayRole);
    }
};

class LobbyListItem : public QStandardItem {
public:
    static const int LobbyIdRole = Qt::UserRole + 1;
    static const int LobbyNameRole = Qt::UserRole + 2;
    static const int LobbyPlayerCountRole = Qt::UserRole + 3;
    static const int LobbyGameIdListRole = Qt::UserRole + 4;

    LobbyListItem() = default;
    explicit LobbyListItem(QString lobby_id, QString name, u32 player_count, QStringList game_ids) {
        setData(lobby_id, LobbyIdRole);
        setData(name, LobbyNameRole);
        setData(player_count, LobbyPlayerCountRole);
        setData(game_ids, LobbyGameIdListRole);
    }

    QVariant data(int role) const override {
        if (role == Qt::DisplayRole) {
            return QObject::tr("%1\n      %2 players")
                .arg(data(LobbyNameRole).toString(), data(LobbyPlayerCountRole).toString());
        } else if (role == Qt::DecorationRole) {
            return QIcon::fromTheme("no_avatar").pixmap(32);
        } else {
            return QStandardItem::data(role);
        }
    }
};
