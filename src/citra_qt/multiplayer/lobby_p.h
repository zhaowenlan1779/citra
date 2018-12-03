// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <utility>
#include <QPainter>
#include <QPixmap>
#include <QStandardItem>
#include <QStandardItemModel>
#include "common/common_types.h"

namespace Column {
enum List {
    EXPAND,
    ROOM_NAME,
    GAME_NAME,
    HOST,
    MEMBER,
    TOTAL,
};
}

class LobbyItem : public QStandardItem {
public:
    LobbyItem() = default;
    explicit LobbyItem(const QString& string) : QStandardItem(string) {}
    virtual ~LobbyItem() override = default;
};

class LobbyItemName : public LobbyItem {
public:
    static const int NameRole = Qt::UserRole + 1;
    static const int PasswordRole = Qt::UserRole + 2;

    LobbyItemName() = default;
    explicit LobbyItemName(bool has_password, QString name) : LobbyItem() {
        setData(name, NameRole);
        setData(has_password, PasswordRole);
    }

    QVariant data(int role) const override {
        if (role == Qt::DecorationRole) {
            bool has_password = data(PasswordRole).toBool();
            return has_password ? QIcon::fromTheme("lock").pixmap(16) : QIcon();
        }
        if (role != Qt::DisplayRole) {
            return LobbyItem::data(role);
        }
        return data(NameRole).toString();
    }

    bool operator<(const QStandardItem& other) const override {
        return data(NameRole).toString().localeAwareCompare(other.data(NameRole).toString()) < 0;
    }
};

class LobbyItemDescription : public LobbyItem {
public:
    static const int DescriptionRole = Qt::UserRole + 1;

    LobbyItemDescription() = default;
    explicit LobbyItemDescription(QString description) {
        setData(description, DescriptionRole);
    }

    QVariant data(int role) const override {
        if (role != Qt::DisplayRole) {
            return LobbyItem::data(role);
        }
        auto description = data(DescriptionRole).toString();
        description.prepend("Description: ");
        return description;
    }

    bool operator<(const QStandardItem& other) const override {
        return data(DescriptionRole)
                   .toString()
                   .localeAwareCompare(other.data(DescriptionRole).toString()) < 0;
    }
};

class LobbyItemGame : public LobbyItem {
    Q_OBJECT

public:
    LobbyItemGame() = default;
    explicit LobbyItemGame(const std::vector<std::string>& game_names,
                           std::vector<QPixmap> smdh_icons) {

        int count = 0;
        for (std::size_t i = 0; i < smdh_icons.size(); ++i) {
            if (!smdh_icons[i].isNull())
                count++;
        }
        count = std::min(count, 5); // Limit displayed icons to 5 entries

        // Combine these pixmaps together to form a new one
        // Add 8 pixels space between them
        QPixmap pixmap(24 * count - 8, 16);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);

        for (int i = 0, j = 0; i < smdh_icons.size() && j < count; ++i) {
            if (smdh_icons[i].isNull())
                continue;
            painter.drawPixmap(24 * j - 8, 0, smdh_icons[i].scaled(16, 16, Qt::KeepAspectRatio));
            j++;
        }

        // Build the text
        QString text;

        if (count == 0 || game_names.size() == 1) {
            // Display game name if there is only one entry
            text = QString::fromStdString(game_names[0]);
            count = 1;
        }

        if (count < game_names.size()) {
            // Show a count of entries not displayed
            text = tr("and %1 more").arg(game_names.size() - count);
        }

        setData(pixmap, Qt::DecorationRole);
        setData(text, Qt::DisplayRole);
    }
};

class LobbyItemHost : public LobbyItem {
public:
    static const int HostUsernameRole = Qt::UserRole + 1;
    static const int HostIPRole = Qt::UserRole + 2;
    static const int HostPortRole = Qt::UserRole + 3;
    static const int HostVerifyUIDRole = Qt::UserRole + 4;

    LobbyItemHost() = default;
    explicit LobbyItemHost(QString username, QString ip, u16 port, QString verify_UID) {
        setData(username, HostUsernameRole);
        setData(ip, HostIPRole);
        setData(port, HostPortRole);
        setData(verify_UID, HostVerifyUIDRole);
    }

    QVariant data(int role) const override {
        if (role != Qt::DisplayRole) {
            return LobbyItem::data(role);
        }
        return data(HostUsernameRole).toString();
    }

    bool operator<(const QStandardItem& other) const override {
        return data(HostUsernameRole)
                   .toString()
                   .localeAwareCompare(other.data(HostUsernameRole).toString()) < 0;
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

class LobbyItemMemberList : public LobbyItem {
public:
    static const int MemberListRole = Qt::UserRole + 1;
    static const int MaxPlayerRole = Qt::UserRole + 2;

    LobbyItemMemberList() = default;
    explicit LobbyItemMemberList(QList<QVariant> members, u32 max_players) {
        setData(members, MemberListRole);
        setData(max_players, MaxPlayerRole);
    }

    QVariant data(int role) const override {
        if (role != Qt::DisplayRole) {
            return LobbyItem::data(role);
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
class LobbyItemExpandedMemberList : public LobbyItem {
public:
    static const int MemberListRole = Qt::UserRole + 1;

    LobbyItemExpandedMemberList() = default;
    explicit LobbyItemExpandedMemberList(QList<QVariant> members) {
        setData(members, MemberListRole);
    }

    QVariant data(int role) const override {
        if (role != Qt::DisplayRole) {
            return LobbyItem::data(role);
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

/**
 * An item of the preferred game list of a lobby.
 */
class LobbyItemPreferredGame : public LobbyItem {
public:
    static const int TitleIDRole = Qt::UserRole + 1;
    static const int GameNameRole = Qt::UserRole + 2;
    static const int GameIconRole = Qt::UserRole + 3;

    LobbyItemPreferredGame() = default;

    explicit LobbyItemPreferredGame(u64 title_id, QString game_name, QPixmap smdh_icon) {
        setData(static_cast<unsigned long long>(title_id), TitleIDRole);
        setData(game_name, GameNameRole);
        if (!smdh_icon.isNull()) {
            setData(smdh_icon, GameIconRole);
        }
    }

    QVariant data(int role) const override {
        if (role == Qt::DecorationRole) {
            auto val = data(GameIconRole);
            if (val.isValid()) {
                val = val.value<QPixmap>().scaled(16, 16, Qt::KeepAspectRatio);
            }
            return val;
        } else if (role != Qt::DisplayRole) {
            return LobbyItem::data(role);
        }
        return data(GameNameRole).toString();
    }

    bool operator<(const QStandardItem& other) const override {
        return data(GameNameRole)
                   .toString()
                   .localeAwareCompare(other.data(GameNameRole).toString()) < 0;
    }
};
