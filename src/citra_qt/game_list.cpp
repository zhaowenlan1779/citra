// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cinttypes>
#include <QApplication>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QModelIndex>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QThreadPool>
#include <QToolButton>
#include <QTreeView>
#include "citra_qt/game_list.h"
#include "citra_qt/game_list_p.h"
#include "citra_qt/main.h"
#include "citra_qt/ui_settings.h"
#include "common/common_paths.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/hle/service/fs/archive.h"
#include "core/loader/loader.h"

GameList::SearchField::KeyReleaseEater::KeyReleaseEater(GameList* gamelist) {
    this->gamelist = gamelist;
    edit_filter_text_old = "";
}

// EventFilter in order to process systemkeys while editing the searchfield
bool GameList::SearchField::KeyReleaseEater::eventFilter(QObject* obj, QEvent* event) {
    // If it isn't a KeyRelease event then continue with standard event processing
    if (event->type() != QEvent::KeyRelease)
        return QObject::eventFilter(obj, event);

    QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
    QString edit_filter_text = gamelist->search_field->edit_filter->text().toLower();

    // If the searchfield's text hasn't changed special function keys get checked
    // If no function key changes the searchfield's text the filter doesn't need to get reloaded
    if (edit_filter_text == edit_filter_text_old) {
        switch (keyEvent->key()) {
        // Escape: Resets the searchfield
        case Qt::Key_Escape: {
            if (edit_filter_text_old.isEmpty()) {
                return QObject::eventFilter(obj, event);
            } else {
                gamelist->search_field->edit_filter->clear();
                edit_filter_text = "";
            }
            break;
        }
        // Return and Enter
        // If the enter key gets pressed first checks how many and which entry is visible
        // If there is only one result launch this game
        case Qt::Key_Return:
        case Qt::Key_Enter: {
            if (gamelist->search_field->visible == 1) {
                QString file_path = gamelist->getLastFilterResultItem();

                // To avoid loading error dialog loops while confirming them using enter
                // Also users usually want to run a diffrent game after closing one
                gamelist->search_field->edit_filter->setText("");
                edit_filter_text = "";
                emit gamelist->GameChosen(file_path);
            } else {
                return QObject::eventFilter(obj, event);
            }
            break;
        }
        default:
            return QObject::eventFilter(obj, event);
        }
    }
    edit_filter_text_old = edit_filter_text;
    return QObject::eventFilter(obj, event);
}

void GameList::SearchField::setFilterResult(int visible, int total) {
    this->visible = visible;
    this->total = total;

    QString result_of_text = tr("of");
    QString result_text;
    if (total == 1) {
        result_text = tr("result");
    } else {
        result_text = tr("results");
    }
    label_filter_result->setText(
        QString("%1 %2 %3 %4").arg(visible).arg(result_of_text).arg(total).arg(result_text));
}

QString GameList::getLastFilterResultItem() {
    QStandardItem* folder;
    QStandardItem* child;
    QString file_path;
    int folderCount = item_model->rowCount();
    for (int i = 0; i < folderCount; ++i) {
        folder = item_model->item(i, 0);
        QModelIndex folder_index = folder->index();
        int childrenCount = folder->rowCount();
        for (int j = 0; j < childrenCount; ++j) {
            if (!tree_view->isRowHidden(j, folder_index)) {
                child = folder->child(j, 0);
                file_path = child->data(GameListItemPath::FullPathRole).toString();
            }
        }
    }
    return file_path;
}

void GameList::SearchField::clear() {
    edit_filter->setText("");
}

void GameList::SearchField::setFocus() {
    if (edit_filter->isVisible()) {
        edit_filter->setFocus();
    }
}

GameList::SearchField::SearchField(GameList* parent) : QWidget{parent} {
    KeyReleaseEater* keyReleaseEater = new KeyReleaseEater(parent);
    layout_filter = new QHBoxLayout;
    layout_filter->setMargin(8);
    label_filter = new QLabel;
    label_filter->setText(tr("Filter:"));
    edit_filter = new QLineEdit;
    edit_filter->setText("");
    edit_filter->setPlaceholderText(tr("Enter pattern to filter"));
    edit_filter->installEventFilter(keyReleaseEater);
    edit_filter->setClearButtonEnabled(true);
    connect(edit_filter, &QLineEdit::textChanged, parent, &GameList::onTextChanged);
    label_filter_result = new QLabel;
    button_filter_close = new QToolButton(this);
    button_filter_close->setText("X");
    button_filter_close->setCursor(Qt::ArrowCursor);
    button_filter_close->setStyleSheet("QToolButton{ border: none; padding: 0px; color: "
                                       "#000000; font-weight: bold; background: #F0F0F0; }"
                                       "QToolButton:hover{ border: none; padding: 0px; color: "
                                       "#EEEEEE; font-weight: bold; background: #E81123}");
    connect(button_filter_close, &QToolButton::clicked, parent, &GameList::onFilterCloseClicked);
    layout_filter->setSpacing(10);
    layout_filter->addWidget(label_filter);
    layout_filter->addWidget(edit_filter);
    layout_filter->addWidget(label_filter_result);
    layout_filter->addWidget(button_filter_close);
    setLayout(layout_filter);
}

/**
 * Checks if all words separated by spaces are contained in another string
 * This offers a word order insensitive search function
 *
 * @param String that gets checked if it contains all words of the userinput string
 * @param String containing all words getting checked
 * @return true if the haystack contains all words of userinput
 */
bool GameList::containsAllWords(QString haystack, QString userinput) {
    QStringList userinput_split = userinput.split(" ", QString::SplitBehavior::SkipEmptyParts);
    return std::all_of(userinput_split.begin(), userinput_split.end(),
                       [haystack](QString s) { return haystack.contains(s); });
}

// Syncs the expanded state of Game Directories with settings to persist across sessions
void GameList::onItemExpanded(const QModelIndex& item) {
    GameListItemType type = item.data(GameListItem::TypeRole).value<GameListItemType>();
    if (type == GameListItemType::CustomDir || type == GameListItemType::InstalledDir ||
        type == GameListItemType::SystemDir)
        item.data(GameListDir::GameDirRole).value<UISettings::GameDir*>()->expanded =
            tree_view->isExpanded(item);
}

// Event in order to filter the gamelist after editing the searchfield
void GameList::onTextChanged(const QString& newText) {
    int folderCount = tree_view->model()->rowCount();
    QString edit_filter_text = newText.toLower();
    QStandardItem* folder;
    QStandardItem* child;
    int childrenTotal = 0;
    QModelIndex root_index = item_model->invisibleRootItem()->index();

    // If the searchfield is empty every item is visible
    // Otherwise the filter gets applied
    if (edit_filter_text.isEmpty()) {
        for (int i = 0; i < folderCount; ++i) {
            folder = item_model->item(i, 0);
            QModelIndex folder_index = folder->index();
            int childrenCount = folder->rowCount();
            for (int j = 0; j < childrenCount; ++j) {
                ++childrenTotal;
                tree_view->setRowHidden(j, folder_index, false);
            }
        }
        search_field->setFilterResult(childrenTotal, childrenTotal);
    } else {
        QString file_path, file_name, file_title, file_programmid;
        int result_count = 0;
        for (int i = 0; i < folderCount; ++i) {
            folder = item_model->item(i, 0);
            QModelIndex folder_index = folder->index();
            int childrenCount = folder->rowCount();
            for (int j = 0; j < childrenCount; ++j) {
                ++childrenTotal;
                child = folder->child(j, 0);
                file_path = child->data(GameListItemPath::FullPathRole).toString().toLower();
                file_name = file_path.mid(file_path.lastIndexOf("/") + 1);
                file_title = child->data(GameListItemPath::TitleRole).toString().toLower();
                file_programmid = child->data(GameListItemPath::ProgramIdRole).toString().toLower();

                // Only items which filename in combination with its title contains all words
                // that are in the searchfield will be visible in the gamelist
                // The search is case insensitive because of toLower()
                // I decided not to use Qt::CaseInsensitive in containsAllWords to prevent
                // multiple conversions of edit_filter_text for each game in the gamelist
                if (containsAllWords(file_name.append(" ").append(file_title), edit_filter_text) ||
                    (file_programmid.count() == 16 && edit_filter_text.contains(file_programmid))) {
                    tree_view->setRowHidden(j, folder_index, false);
                    ++result_count;
                } else {
                    tree_view->setRowHidden(j, folder_index, true);
                }
                search_field->setFilterResult(result_count, childrenTotal);
            }
        }
    }
}

void GameList::onUpdateThemedIcons() {
    for (int i = 0; i < item_model->invisibleRootItem()->rowCount(); i++) {
        QStandardItem* child = item_model->invisibleRootItem()->child(i);

        switch (child->data(GameListItem::TypeRole).value<GameListItemType>()) {
        case GameListItemType::InstalledDir:
            child->setData(QIcon::fromTheme("sd_card").pixmap(48), Qt::DecorationRole);
            break;
        case GameListItemType::SystemDir:
            child->setData(QIcon::fromTheme("chip").pixmap(48), Qt::DecorationRole);
            break;
        case GameListItemType::CustomDir: {
            const UISettings::GameDir* game_dir =
                child->data(GameListDir::GameDirRole).value<UISettings::GameDir*>();
            QString icon_name = QFileInfo::exists(game_dir->path) ? "folder" : "bad_folder";
            child->setData(QIcon::fromTheme(icon_name).pixmap(48), Qt::DecorationRole);
            break;
        }
        case GameListItemType::AddDir:
            child->setData(QIcon::fromTheme("plus").pixmap(48), Qt::DecorationRole);
            break;
        }
    }
}

void GameList::onFilterCloseClicked() {
    main_window->filterBarSetChecked(false);
}

GameList::GameList(GMainWindow* parent) : QWidget{parent} {
    watcher = new QFileSystemWatcher(this);
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, &GameList::RefreshGameDirectory);

    this->main_window = parent;
    layout = new QVBoxLayout;
    tree_view = new QTreeView;
    search_field = new SearchField(this);
    item_model = new QStandardItemModel(tree_view);
    tree_view->setModel(item_model);

    tree_view->setAlternatingRowColors(true);
    tree_view->setSelectionMode(QHeaderView::SingleSelection);
    tree_view->setSelectionBehavior(QHeaderView::SelectRows);
    tree_view->setVerticalScrollMode(QHeaderView::ScrollPerPixel);
    tree_view->setHorizontalScrollMode(QHeaderView::ScrollPerPixel);
    tree_view->setSortingEnabled(true);
    tree_view->setEditTriggers(QHeaderView::NoEditTriggers);
    tree_view->setUniformRowHeights(true);
    tree_view->setContextMenuPolicy(Qt::CustomContextMenu);

    item_model->insertColumns(0, COLUMN_COUNT);
    item_model->setHeaderData(COLUMN_NAME, Qt::Horizontal, "Name");
    item_model->setHeaderData(COLUMN_COMPATIBILITY, Qt::Horizontal, "Compatibility");
    item_model->setHeaderData(COLUMN_REGION, Qt::Horizontal, "Region");
    item_model->setHeaderData(COLUMN_FILE_TYPE, Qt::Horizontal, "File type");
    item_model->setHeaderData(COLUMN_SIZE, Qt::Horizontal, "Size");
    item_model->setSortRole(GameListItemPath::TitleRole);

    connect(main_window, &GMainWindow::UpdateThemedIcons, this, &GameList::onUpdateThemedIcons);
    connect(tree_view, &QTreeView::activated, this, &GameList::ValidateEntry);
    connect(tree_view, &QTreeView::customContextMenuRequested, this, &GameList::PopupContextMenu);
    connect(tree_view, &QTreeView::expanded, this, &GameList::onItemExpanded);
    connect(tree_view, &QTreeView::collapsed, this, &GameList::onItemExpanded);

    // We must register all custom types with the Qt Automoc system so that we are able to use
    // it with signals/slots. In this case, QList falls under the umbrells of custom types.
    qRegisterMetaType<QList<QStandardItem*>>("QList<QStandardItem*>");

    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(tree_view);
    layout->addWidget(search_field);
    setLayout(layout);
}

GameList::~GameList() {
    emit ShouldCancelWorker();
}

void GameList::setFilterFocus() {
    if (tree_view->model()->rowCount() > 0) {
        search_field->setFocus();
    }
}

void GameList::setFilterVisible(bool visibility) {
    search_field->setVisible(visibility);
}

void GameList::clearFilter() {
    search_field->clear();
}

void GameList::AddDirEntry(GameListDir* entry_items) {
    item_model->invisibleRootItem()->appendRow(entry_items);
    tree_view->setExpanded(
        entry_items->index(),
        entry_items->data(GameListDir::GameDirRole).value<UISettings::GameDir*>()->expanded);
}

void GameList::AddEntry(const QList<QStandardItem*>& entry_items, GameListDir* parent) {
    parent->appendRow(entry_items);
}

void GameList::ValidateEntry(const QModelIndex& item) {
    auto selected = item.sibling(item.row(), 0);

    switch (selected.data(GameListItem::TypeRole).value<GameListItemType>()) {
    case GameListItemType::Game: {
        QString file_path = selected.data(GameListItemPath::FullPathRole).toString();
        if (file_path.isEmpty())
            return;
        QFileInfo file_info(file_path);
        if (!file_info.exists() || file_info.isDir())
            return;
        // Users usually want to run a different game after closing one
        search_field->clear();
        emit GameChosen(file_path);
        break;
    }
    case GameListItemType::AddDir:
        emit AddDirectory();
        break;
    }
}

bool GameList::isEmpty() {
    for (int i = 0; i < item_model->rowCount(); i++) {
        const QStandardItem* child = item_model->invisibleRootItem()->child(i);
        GameListItemType type = static_cast<GameListItemType>(child->type());
        if (!child->hasChildren() &&
            (type == GameListItemType::InstalledDir || type == GameListItemType::SystemDir)) {
            item_model->invisibleRootItem()->removeRow(child->row());
            i--;
        };
    }
    return !item_model->invisibleRootItem()->hasChildren();
}

void GameList::DonePopulating(QStringList watch_list) {
    emit ShowList(!isEmpty());

    item_model->invisibleRootItem()->appendRow(new GameListAddDir());

    // Clear out the old directories to watch for changes and add the new ones
    auto watch_dirs = watcher->directories();
    if (!watch_dirs.isEmpty()) {
        watcher->removePaths(watch_dirs);
    }
    // Workaround: Add the watch paths in chunks to allow the gui to refresh
    // This prevents the UI from stalling when a large number of watch paths are added
    // Also artificially caps the watcher to a certain number of directories
    constexpr int LIMIT_WATCH_DIRECTORIES = 5000;
    constexpr int SLICE_SIZE = 25;
    int len = std::min(watch_list.length(), LIMIT_WATCH_DIRECTORIES);
    for (int i = 0; i < len; i += SLICE_SIZE) {
        watcher->addPaths(watch_list.mid(i, i + SLICE_SIZE));
        QCoreApplication::processEvents();
    }
    tree_view->setEnabled(true);
    int folderCount = tree_view->model()->rowCount();
    int childrenTotal = 0;
    for (int i = 0; i < folderCount; ++i) {
        int childrenCount = item_model->item(i, 0)->rowCount();
        for (int j = 0; j < childrenCount; ++j) {
            ++childrenTotal;
        }
    }
    search_field->setFilterResult(childrenTotal, childrenTotal);
    if (childrenTotal > 0) {
        search_field->setFocus();
    }
}

void GameList::PopupContextMenu(const QPoint& menu_location) {
    QModelIndex item = tree_view->indexAt(menu_location);
    if (!item.isValid())
        return;

    auto selected = item.sibling(item.row(), 0);
    QMenu context_menu;
    switch (selected.data(GameListItem::TypeRole).value<GameListItemType>()) {
    case GameListItemType::Game:
        AddGamePopup(context_menu, selected.data(GameListItemPath::ProgramIdRole).toULongLong());
        break;
    case GameListItemType::CustomDir:
        AddPermDirPopup(context_menu, selected);
        AddCustomDirPopup(context_menu, selected);
        break;
    case GameListItemType::InstalledDir:
    case GameListItemType::SystemDir:
        AddPermDirPopup(context_menu, selected);
        break;
    }
    context_menu.exec(tree_view->viewport()->mapToGlobal(menu_location));
}

void GameList::AddGamePopup(QMenu& context_menu, u64 program_id) {
    QAction* open_save_location = context_menu.addAction(tr("Open Save Data Location"));
    QAction* open_application_location = context_menu.addAction(tr("Open Application Location"));
    QAction* open_update_location = context_menu.addAction(tr("Open Update Data Location"));
    QAction* navigate_to_gamedb_entry = context_menu.addAction(tr("Navigate to GameDB entry"));

    open_save_location->setEnabled(program_id != 0);
    open_application_location->setVisible(FileUtil::Exists(
        Service::AM::GetTitleContentPath(Service::FS::MediaType::SDMC, program_id)));
    open_update_location->setEnabled(0x00040000'00000000 <= program_id &&
                                     program_id <= 0x00040000'FFFFFFFF);
    auto it = std::find_if(
        compatibility_list.begin(), compatibility_list.end(),
        [program_id](const std::pair<std::string, std::pair<QString, QString>>& element) {
            std::string pid = Common::StringFromFormat("%016" PRIX64, program_id);
            return element.first == pid;
        });
    navigate_to_gamedb_entry->setVisible(it != compatibility_list.end());

    connect(open_save_location, &QAction::triggered, [this, program_id] {
        emit OpenFolderRequested(program_id, GameListOpenTarget::SAVE_DATA);
    });
    connect(open_application_location, &QAction::triggered, [this, program_id] {
        emit OpenFolderRequested(program_id, GameListOpenTarget::APPLICATION);
    });
    connect(open_update_location, &QAction::triggered, [this, program_id] {
        emit OpenFolderRequested(program_id, GameListOpenTarget::UPDATE_DATA);
    });
    connect(navigate_to_gamedb_entry, &QAction::triggered, [this, program_id]() {
        emit NavigateToGamedbEntryRequested(program_id, compatibility_list);
    });
};

void GameList::AddCustomDirPopup(QMenu& context_menu, QModelIndex selected) {
    UISettings::GameDir& game_dir =
        *selected.data(GameListDir::GameDirRole).value<UISettings::GameDir*>();

    QAction* deep_scan = context_menu.addAction(tr("Scan Subfolders"));
    QAction* delete_dir = context_menu.addAction(tr("Remove Game Directory"));

    deep_scan->setCheckable(true);
    deep_scan->setChecked(game_dir.deep_scan);

    connect(deep_scan, &QAction::triggered, [this, &game_dir] {
        game_dir.deep_scan = !game_dir.deep_scan;
        PopulateAsync(UISettings::values.game_dirs);
    });
    connect(delete_dir, &QAction::triggered, [this, &game_dir, selected] {
        UISettings::values.game_dirs.removeOne(game_dir);
        item_model->invisibleRootItem()->removeRow(selected.row());
    });
}

void GameList::AddPermDirPopup(QMenu& context_menu, QModelIndex selected) {
    UISettings::GameDir& game_dir =
        *selected.data(GameListDir::GameDirRole).value<UISettings::GameDir*>();

    QAction* move_up = context_menu.addAction(tr(u8"\U000025b2 Move Up"));
    QAction* move_down = context_menu.addAction(tr(u8"\U000025bc Move Down "));
    QAction* open_directory_location = context_menu.addAction(tr("Open Directory Location"));

    int row = selected.row();

    move_up->setEnabled(row > 0);
    move_down->setEnabled(row < item_model->rowCount() - 2);

    connect(move_up, &QAction::triggered, [this, selected, row, &game_dir] {
        // find the indices of the items in settings and swap them
        UISettings::values.game_dirs.swap(
            UISettings::values.game_dirs.indexOf(game_dir),
            UISettings::values.game_dirs.indexOf(*selected.sibling(selected.row() - 1, 0)
                                                      .data(GameListDir::GameDirRole)
                                                      .value<UISettings::GameDir*>()));
        // move the treeview items
        QList<QStandardItem*> item = item_model->takeRow(row);
        item_model->invisibleRootItem()->insertRow(row - 1, item);
        tree_view->setExpanded(selected, game_dir.expanded);
    });

    connect(move_down, &QAction::triggered, [this, selected, row, &game_dir] {
        // find the indices of the items in settings and swap them
        UISettings::values.game_dirs.swap(
            UISettings::values.game_dirs.indexOf(game_dir),
            UISettings::values.game_dirs.indexOf(*selected.sibling(selected.row() + 1, 0)
                                                      .data(GameListDir::GameDirRole)
                                                      .value<UISettings::GameDir*>()));
        // move the treeview items
        QList<QStandardItem*> item = item_model->takeRow(row);
        item_model->invisibleRootItem()->insertRow(row + 1, item);
        tree_view->setExpanded(selected, game_dir.expanded);
    });

    connect(open_directory_location, &QAction::triggered,
            [this, game_dir] { emit OpenDirectory(game_dir.path); });
}

void GameList::LoadCompatibilityList() {
    QFile compat_list{":compatibility_list/compatibility_list.json"};

    if (!compat_list.open(QFile::ReadOnly | QFile::Text)) {
        LOG_ERROR(Frontend, "Unable to open game compatibility list");
        return;
    }

    if (compat_list.size() == 0) {
        LOG_ERROR(Frontend, "Game compatibility list is empty");
        return;
    }

    const QByteArray content = compat_list.readAll();
    if (content.isEmpty()) {
        LOG_ERROR(Frontend, "Unable to completely read game compatibility list");
        return;
    }

    const QString string_content = content;
    QJsonDocument json = QJsonDocument::fromJson(string_content.toUtf8());
    QJsonArray arr = json.array();

    for (const QJsonValue& value : arr) {
        QJsonObject game = value.toObject();

        if (game.contains("compatibility") && game["compatibility"].isDouble()) {
            int compatibility = game["compatibility"].toInt();
            QString directory = game["directory"].toString();
            QJsonArray ids = game["releases"].toArray();

            for (const QJsonValue& value : ids) {
                QJsonObject object = value.toObject();
                QString id = object["id"].toString();
                compatibility_list.insert(
                    std::make_pair(id.toUpper().toStdString(),
                                   std::make_pair(QString::number(compatibility), directory)));
            }
        }
    }
}

QStandardItemModel* GameList::GetModel() const {
    return item_model;
}

void GameList::PopulateAsync(QList<UISettings::GameDir>& game_dirs) {
    tree_view->setEnabled(false);
    // Delete any rows that might already exist if we're repopulating
    item_model->removeRows(0, item_model->rowCount());
    search_field->clear();

    emit ShouldCancelWorker();

    GameListWorker* worker = new GameListWorker(game_dirs, compatibility_list);

    connect(worker, &GameListWorker::EntryReady, this, &GameList::AddEntry, Qt::QueuedConnection);
    connect(worker, &GameListWorker::DirEntryReady, this, &GameList::AddDirEntry,
            Qt::QueuedConnection);
    connect(worker, &GameListWorker::Finished, this, &GameList::DonePopulating,
            Qt::QueuedConnection);
    // Use DirectConnection here because worker->Cancel() is thread-safe and we want it to
    // cancel without delay.
    connect(this, &GameList::ShouldCancelWorker, worker, &GameListWorker::Cancel,
            Qt::DirectConnection);

    QThreadPool::globalInstance()->start(worker);
    current_worker = std::move(worker);
}

void GameList::SaveInterfaceLayout() {
    UISettings::values.gamelist_header_state = tree_view->header()->saveState();
}

void GameList::LoadInterfaceLayout() {
    auto header = tree_view->header();
    if (!header->restoreState(UISettings::values.gamelist_header_state)) {
        // We are using the name column to display icons and titles
        // so make it as large as possible as default.
        header->resizeSection(COLUMN_NAME, header->width());
    }

    item_model->sort(header->sortIndicatorSection(), header->sortIndicatorOrder());
}

const QStringList GameList::supported_file_extensions = {"3ds", "3dsx", "elf", "axf",
                                                         "cci", "cxi",  "app"};

static bool HasSupportedFileExtension(const std::string& file_name) {
    QFileInfo file = QFileInfo(file_name.c_str());
    return GameList::supported_file_extensions.contains(file.suffix(), Qt::CaseInsensitive);
}

void GameList::RefreshGameDirectory() {
    if (!UISettings::values.game_dirs.isEmpty() && current_worker != nullptr) {
        LOG_INFO(Frontend, "Change detected in the games directory. Reloading game list.");
        PopulateAsync(UISettings::values.game_dirs);
    }
}

void GameListWorker::AddFstEntriesToGameList(const std::string& dir_path, unsigned int recursion,
                                             GameListDir* parent_dir) {
    const auto callback = [this, recursion, parent_dir](unsigned* num_entries_out,
                                                        const std::string& directory,
                                                        const std::string& virtual_name) -> bool {
        std::string physical_name = directory + DIR_SEP + virtual_name;

        if (stop_processing)
            return false; // Breaks the callback loop.

        bool is_dir = FileUtil::IsDirectory(physical_name);
        if (!is_dir && HasSupportedFileExtension(physical_name)) {
            std::unique_ptr<Loader::AppLoader> loader = Loader::GetLoader(physical_name);
            if (!loader)
                return true;

            u64 program_id = 0;
            loader->ReadProgramId(program_id);

            std::vector<u8> smdh = [program_id, &loader]() -> std::vector<u8> {
                std::vector<u8> original_smdh;
                loader->ReadIcon(original_smdh);

                if (program_id < 0x00040000'00000000 || program_id > 0x00040000'FFFFFFFF)
                    return original_smdh;

                std::string update_path = Service::AM::GetTitleContentPath(
                    Service::FS::MediaType::SDMC, program_id + 0x0000000E'00000000);

                if (!FileUtil::Exists(update_path))
                    return original_smdh;

                std::unique_ptr<Loader::AppLoader> update_loader = Loader::GetLoader(update_path);

                if (!update_loader)
                    return original_smdh;

                std::vector<u8> update_smdh;
                update_loader->ReadIcon(update_smdh);
                return update_smdh;
            }();

            auto it = std::find_if(
                compatibility_list.begin(), compatibility_list.end(),
                [program_id](const std::pair<std::string, std::pair<QString, QString>>& element) {
                    std::string pid = Common::StringFromFormat("%016" PRIX64, program_id);
                    return element.first == pid;
                });

            // The game list uses this as compatibility number for untested games
            QString compatibility("99");
            if (it != compatibility_list.end())
                compatibility = it->second.first;

            emit EntryReady(
                {
                    new GameListItemPath(QString::fromStdString(physical_name), smdh, program_id),
                    new GameListItemCompat(compatibility),
                    new GameListItemRegion(smdh),
                    new GameListItem(
                        QString::fromStdString(Loader::GetFileTypeString(loader->GetFileType()))),
                    new GameListItemSize(FileUtil::GetSize(physical_name)),
                },
                parent_dir);

        } else if (is_dir && recursion > 0) {
            watch_list.append(QString::fromStdString(physical_name));
            AddFstEntriesToGameList(physical_name, recursion - 1, parent_dir);
        }

        return true;
    };

    FileUtil::ForeachDirectoryEntry(nullptr, dir_path, callback);
}

void GameListWorker::run() {
    stop_processing = false;
    for (UISettings::GameDir& game_dir : game_dirs) {
        if (game_dir.path == "INSTALLED") {
            QString path = QString(FileUtil::GetUserPath(D_SDMC_IDX).c_str()) +
                           "Nintendo "
                           "3DS/00000000000000000000000000000000/"
                           "00000000000000000000000000000000/title/00040000";
            watch_list.append(path);
            GameListDir* game_list_dir = new GameListDir(game_dir, GameListItemType::InstalledDir);
            emit DirEntryReady({game_list_dir});
            AddFstEntriesToGameList(path.toStdString(), 2, game_list_dir);
        } else if (game_dir.path == "SYSTEM") {
            QString path = QString(FileUtil::GetUserPath(D_NAND_IDX).c_str()) +
                           "00000000000000000000000000000000/title/00040010";
            watch_list.append(path);
            GameListDir* game_list_dir = new GameListDir(game_dir, GameListItemType::SystemDir);
            emit DirEntryReady({game_list_dir});
            AddFstEntriesToGameList(std::string(FileUtil::GetUserPath(D_NAND_IDX).c_str()) +
                                        "00000000000000000000000000000000/title/00040010",
                                    2, game_list_dir);
        } else {
            watch_list.append(game_dir.path);
            GameListDir* game_list_dir = new GameListDir(game_dir);
            emit DirEntryReady({game_list_dir});
            AddFstEntriesToGameList(game_dir.path.toStdString(), game_dir.deep_scan ? 256 : 0,
                                    game_list_dir);
        }
    };
    emit Finished(watch_list);
}

void GameListWorker::Cancel() {
    this->disconnect();
    stop_processing = true;
}

GameListPlaceholder::GameListPlaceholder(GMainWindow* parent) : QWidget{parent} {
    this->main_window = parent;

    connect(main_window, &GMainWindow::UpdateThemedIcons, this,
            &GameListPlaceholder::onUpdateThemedIcons);

    layout = new QVBoxLayout;
    image = new QLabel;
    text = new QLabel;
    layout->setAlignment(Qt::AlignCenter);
    image->setPixmap(QIcon::fromTheme("plus_folder").pixmap(200));

    text->setText(tr("Double-click to add a new folder to the game list "));
    QFont font = text->font();
    font.setPointSize(20);
    text->setFont(font);
    text->setAlignment(Qt::AlignHCenter);
    image->setAlignment(Qt::AlignHCenter);

    layout->addWidget(image);
    layout->addWidget(text);
    setLayout(layout);
}

GameListPlaceholder::~GameListPlaceholder() = default;

void GameListPlaceholder::onUpdateThemedIcons() {
    image->setPixmap(QIcon::fromTheme("plus_folder").pixmap(200));
}

void GameListPlaceholder::mouseDoubleClickEvent(QMouseEvent* event) {
    emit GameListPlaceholder::AddDirectory();
}
