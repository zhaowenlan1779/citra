// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstring>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/ncch_container.h"
#include "core/file_sys/title_metadata.h"
#include "core/hle/ipc.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/client_session.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/handle_table.h"
#include "core/hle/kernel/server_session.h"
#include "core/hle/kernel/session.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/am_app.h"
#include "core/hle/service/am/am_net.h"
#include "core/hle/service/am/am_sys.h"
#include "core/hle/service/am/am_u.h"
#include "core/hle/service/fs/archive.h"
#include "core/loader/loader.h"
#include "core/loader/smdh.h"

namespace Service {
namespace AM {

constexpr u16 PLATFORM_CTR = 0x0004;
constexpr u16 CATEGORY_SYSTEM = 0x0010;
constexpr u16 CATEGORY_DLP = 0x0001;
constexpr u8 VARIATION_SYSTEM = 0x02;
constexpr u32 TID_HIGH_UPDATE = 0x0004000E;
constexpr u32 TID_HIGH_DLC = 0x0004008C;

struct TitleInfo {
    u64_le tid;
    u64_le size;
    u16_le version;
    u16_le unused;
    u32_le type;
};

static_assert(sizeof(TitleInfo) == 0x18, "Title info structure size is wrong");

struct ContentInfo {
    u16_le index;
    u16_le type;
    u32_le content_id;
    u64_le size;
    u64_le romfs_size;
};

static_assert(sizeof(ContentInfo) == 0x18, "Content info structure size is wrong");

struct TicketInfo {
    u64_le title_id;
    u64_le ticket_id;
    u16_le version;
    u16_le unused;
    u32_le size;
};

static_assert(sizeof(TicketInfo) == 0x18, "Ticket info structure size is wrong");

ResultVal<size_t> CIAFile::Read(u64 offset, size_t length, u8* buffer) const {
    UNIMPLEMENTED();
    return MakeResult<size_t>(length);
}

ResultVal<size_t> CIAFile::WriteTitleMetadata(u64 offset, size_t length, const u8* buffer) {
    container.LoadTitleMetadata(data, container.GetTitleMetadataOffset());
    FileSys::TitleMetadata tmd = container.GetTitleMetadata();
    tmd.Print();

    // If a TMD already exists for this app (ie 00000000.tmd), the incoming TMD
    // will be the same plus one, (ie 00000001.tmd), both will be kept until
    // the install is finalized and old contents can be discarded.
    if (FileUtil::Exists(GetTitleMetadataPath(media_type, tmd.GetTitleID())))
        is_update = true;

    std::string tmd_path = GetTitleMetadataPath(media_type, tmd.GetTitleID(), is_update);

    // Create content/ folder if it doesn't exist
    std::string tmd_folder;
    Common::SplitPath(tmd_path, &tmd_folder, nullptr, nullptr);
    FileUtil::CreateFullPath(tmd_folder);

    // Save TMD so that we can start getting new .app paths
    if (tmd.Save(tmd_path) != Loader::ResultStatus::Success)
        return FileSys::ERROR_INSUFFICIENT_SPACE;

    // Create any other .app folders which may not exist yet
    std::string app_folder;
    Common::SplitPath(GetTitleContentPath(media_type, tmd.GetTitleID(),
                                          FileSys::TMDContentIndex::Main, is_update),
                      &app_folder, nullptr, nullptr);
    FileUtil::CreateFullPath(app_folder);

    content_written.resize(container.GetTitleMetadata().GetContentCount());
    install_state = CIAInstallState::TMDLoaded;

    return MakeResult<size_t>(length);
}

ResultVal<size_t> CIAFile::WriteContentData(u64 offset, size_t length, const u8* buffer) {
    // Data is not being buffered, so we have to keep track of how much of each <ID>.app
    // has been written since we might get a written buffer which contains multiple .app
    // contents or only part of a larger .app's contents.
    u64 offset_max = offset + length;
    for (int i = 0; i < container.GetTitleMetadata().GetContentCount(); i++) {
        if (content_written[i] < container.GetContentSize(i)) {
            // The size, minimum unwritten offset, and maximum unwritten offset of this content
            u64 size = container.GetContentSize(i);
            u64 range_min = container.GetContentOffset(i) + content_written[i];
            u64 range_max = container.GetContentOffset(i) + size;

            // The unwritten range for this content is beyond the buffered data we have
            // or comes before the buffered data we have, so skip this content ID.
            if (range_min > offset_max || range_max < offset)
                continue;

            // Figure out how much of this content ID we have just recieved/can write out
            u64 available_to_write = std::min(offset_max, range_max) - range_min;

            // Since the incoming TMD has already been written, we can use GetTitleContentPath
            // to get the content paths to write to.
            FileSys::TitleMetadata tmd = container.GetTitleMetadata();
            FileUtil::IOFile file(GetTitleContentPath(media_type, tmd.GetTitleID(), i, is_update),
                                  content_written[i] ? "ab" : "wb");

            if (!file.IsOpen())
                return FileSys::ERROR_INSUFFICIENT_SPACE;

            file.WriteBytes(buffer + (range_min - offset), available_to_write);

            // Keep tabs on how much of this content ID has been written so new range_min
            // values can be calculated.
            content_written[i] += available_to_write;
            LOG_DEBUG(Service_AM, "Wrote {:x} to content {}, total {:x}", available_to_write, i,
                      content_written[i]);
        }
    }

    return MakeResult<size_t>(length);
}

ResultVal<size_t> CIAFile::Write(u64 offset, size_t length, bool flush, const u8* buffer) {
    written += length;

    // TODO(shinyquagsire23): Can we assume that things will only be written in sequence?
    // Does AM send an error if we write to things out of order?
    // Or does it just ignore offsets and assume a set sequence of incoming data?

    // The data in CIAs is always stored CIA Header > Cert > Ticket > TMD > Content > Meta.
    // The CIA Header describes Cert, Ticket, TMD, total content sizes, and TMD is needed for
    // content sizes so it ends up becoming a problem of keeping track of how much has been
    // written and what we have been able to pick up.
    if (install_state == CIAInstallState::InstallStarted) {
        size_t buf_copy_size = std::min(length, FileSys::CIA_HEADER_SIZE);
        size_t buf_max_size =
            std::min(static_cast<size_t>(offset + length), FileSys::CIA_HEADER_SIZE);
        data.resize(buf_max_size);
        memcpy(data.data() + offset, buffer, buf_copy_size);

        // We have enough data to load a CIA header and parse it.
        if (written >= FileSys::CIA_HEADER_SIZE) {
            container.LoadHeader(data);
            container.Print();
            install_state = CIAInstallState::HeaderLoaded;
        }
    }

    // If we don't have a header yet, we can't pull offsets of other sections
    if (install_state == CIAInstallState::InstallStarted)
        return MakeResult<size_t>(length);

    // If we have been given data before (or including) .app content, pull it into
    // our buffer, but only pull *up to* the content offset, no further.
    if (offset < container.GetContentOffset()) {
        size_t buf_loaded = data.size();
        size_t copy_offset = std::max(static_cast<size_t>(offset), buf_loaded);
        size_t buf_offset = buf_loaded - offset;
        size_t buf_copy_size =
            std::min(length, static_cast<size_t>(container.GetContentOffset() - offset)) -
            buf_loaded;
        size_t buf_max_size = std::min(offset + length, container.GetContentOffset());
        data.resize(buf_max_size);
        memcpy(data.data() + copy_offset, buffer + buf_offset, buf_copy_size);
    }

    // TODO(shinyquagsire23): Write out .tik files to nand?

    // The end of our TMD is at the beginning of Content data, so ensure we have that much
    // buffered before trying to parse.
    if (written >= container.GetContentOffset() && install_state != CIAInstallState::TMDLoaded) {
        auto result = WriteTitleMetadata(offset, length, buffer);
        if (result.Failed())
            return result;
    }

    // Content data sizes can only be retrieved from TMD data
    if (install_state != CIAInstallState::TMDLoaded)
        return MakeResult<size_t>(length);

    // From this point forward, data will no longer be buffered in data
    auto result = WriteContentData(offset, length, buffer);
    if (result.Failed())
        return result;

    return MakeResult<size_t>(length);
}

u64 CIAFile::GetSize() const {
    return written;
}

bool CIAFile::SetSize(u64 size) const {
    return false;
}

bool CIAFile::Close() const {
    bool complete = true;
    for (size_t i = 0; i < container.GetTitleMetadata().GetContentCount(); i++) {
        if (content_written[i] < container.GetContentSize(i))
            complete = false;
    }

    // Install aborted
    if (!complete) {
        LOG_ERROR(Service_AM, "CIAFile closed prematurely, aborting install...");
        FileUtil::DeleteDir(GetTitlePath(media_type, container.GetTitleMetadata().GetTitleID()));
        return true;
    }

    // Clean up older content data if we installed newer content on top
    std::string old_tmd_path =
        GetTitleMetadataPath(media_type, container.GetTitleMetadata().GetTitleID(), false);
    std::string new_tmd_path =
        GetTitleMetadataPath(media_type, container.GetTitleMetadata().GetTitleID(), true);
    if (FileUtil::Exists(new_tmd_path) && old_tmd_path != new_tmd_path) {
        FileSys::TitleMetadata old_tmd;
        FileSys::TitleMetadata new_tmd;

        old_tmd.Load(old_tmd_path);
        new_tmd.Load(new_tmd_path);

        // For each content ID in the old TMD, check if there is a matching ID in the new
        // TMD. If a CIA contains (and wrote to) an identical ID, it should be kept while
        // IDs which only existed for the old TMD should be deleted.
        for (u16 old_index = 0; old_index < old_tmd.GetContentCount(); old_index++) {
            bool abort = false;
            for (u16 new_index = 0; new_index < new_tmd.GetContentCount(); new_index++) {
                if (old_tmd.GetContentIDByIndex(old_index) ==
                    new_tmd.GetContentIDByIndex(new_index)) {
                    abort = true;
                }
            }
            if (abort)
                break;

            FileUtil::Delete(GetTitleContentPath(media_type, old_tmd.GetTitleID(), old_index));
        }

        FileUtil::Delete(old_tmd_path);
    }
    return true;
}

void CIAFile::Flush() const {}

InstallStatus InstallCIA(const std::string& path,
                         std::function<ProgressCallback>&& update_callback) {
    LOG_INFO(Service_AM, "Installing {}...", path);

    if (!FileUtil::Exists(path)) {
        LOG_ERROR(Service_AM, "File {} does not exist!", path);
        return InstallStatus::ErrorFileNotFound;
    }

    FileSys::CIAContainer container;
    if (container.Load(path) == Loader::ResultStatus::Success) {
        Service::AM::CIAFile installFile(
            Service::AM::GetTitleMediaType(container.GetTitleMetadata().GetTitleID()));

        for (size_t i = 0; i < container.GetTitleMetadata().GetContentCount(); i++) {
            if (container.GetTitleMetadata().GetContentTypeByIndex(i) &
                FileSys::TMDContentTypeFlag::Encrypted) {
                LOG_ERROR(Service_AM, "File {} is encrypted! Aborting...", path);
                return InstallStatus::ErrorEncrypted;
            }
        }

        FileUtil::IOFile file(path, "rb");
        if (!file.IsOpen())
            return InstallStatus::ErrorFailedToOpenFile;

        std::array<u8, 0x10000> buffer;
        size_t total_bytes_read = 0;
        while (total_bytes_read != file.GetSize()) {
            size_t bytes_read = file.ReadBytes(buffer.data(), buffer.size());
            auto result = installFile.Write(static_cast<u64>(total_bytes_read), bytes_read, true,
                                            static_cast<u8*>(buffer.data()));

            if (update_callback)
                update_callback(total_bytes_read, file.GetSize());
            if (result.Failed()) {
                LOG_ERROR(Service_AM, "CIA file installation aborted with error code {:08x}",
                          result.Code().raw);
                return InstallStatus::ErrorAborted;
            }
            total_bytes_read += bytes_read;
        }
        installFile.Close();

        LOG_INFO(Service_AM, "Installed {} successfully.", path);
        return InstallStatus::Success;
    }

    LOG_ERROR(Service_AM, "CIA file {} is invalid!", path);
    return InstallStatus::ErrorInvalid;
}

Service::FS::MediaType GetTitleMediaType(u64 titleId) {
    u16 platform = static_cast<u16>(titleId >> 48);
    u16 category = static_cast<u16>((titleId >> 32) & 0xFFFF);
    u8 variation = static_cast<u8>(titleId & 0xFF);

    if (platform != PLATFORM_CTR)
        return Service::FS::MediaType::NAND;

    if (category & CATEGORY_SYSTEM || category & CATEGORY_DLP || variation & VARIATION_SYSTEM)
        return Service::FS::MediaType::NAND;

    return Service::FS::MediaType::SDMC;
}

std::string GetTitleMetadataPath(Service::FS::MediaType media_type, u64 tid, bool update) {
    std::string content_path = GetTitlePath(media_type, tid) + "content/";

    if (media_type == Service::FS::MediaType::GameCard) {
        LOG_ERROR(Service_AM, "Invalid request for nonexistent gamecard title metadata!");
        return "";
    }

    // The TMD ID is usually held in the title databases, which we don't implement.
    // For now, just scan for any .tmd files which exist, the smallest will be the
    // base ID and the largest will be the (currently installing) update ID.
    constexpr u32 MAX_TMD_ID = 0xFFFFFFFF;
    u32 base_id = MAX_TMD_ID;
    u32 update_id = 0;
    FileUtil::FSTEntry entries;
    FileUtil::ScanDirectoryTree(content_path, entries);
    for (const FileUtil::FSTEntry& entry : entries.children) {
        std::string filename_filename, filename_extension;
        Common::SplitPath(entry.virtualName, nullptr, &filename_filename, &filename_extension);

        if (filename_extension == ".tmd") {
            u32 id = std::stoul(filename_filename.c_str(), nullptr, 16);
            base_id = std::min(base_id, id);
            update_id = std::max(update_id, id);
        }
    }

    // If we didn't find anything, default to 00000000.tmd for it to be created.
    if (base_id == MAX_TMD_ID)
        base_id = 0;

    // Update ID should be one more than the last, if it hasn't been created yet.
    if (base_id == update_id)
        update_id++;

    return content_path + Common::StringFromFormat("%08x.tmd", (update ? update_id : base_id));
}

std::string GetTitleContentPath(Service::FS::MediaType media_type, u64 tid, u16 index,
                                bool update) {
    std::string content_path = GetTitlePath(media_type, tid) + "content/";

    if (media_type == Service::FS::MediaType::GameCard) {
        // TODO(shinyquagsire23): get current app file if TID matches?
        LOG_ERROR(Service_AM, "Request for gamecard partition {} content path unimplemented!",
                  static_cast<u32>(index));
        return "";
    }

    std::string tmd_path = GetTitleMetadataPath(media_type, tid, update);

    u32 content_id = 0;
    FileSys::TitleMetadata tmd;
    if (tmd.Load(tmd_path) == Loader::ResultStatus::Success) {
        content_id = tmd.GetContentIDByIndex(index);

        // TODO(shinyquagsire23): how does DLC actually get this folder on hardware?
        // For now, check if the second (index 1) content has the optional flag set, for most
        // apps this is usually the manual and not set optional, DLC has it set optional.
        // All .apps (including index 0) will be in the 00000000/ folder for DLC.
        if (tmd.GetContentCount() > 1 &&
            tmd.GetContentTypeByIndex(1) & FileSys::TMDContentTypeFlag::Optional) {
            content_path += "00000000/";
        }
    }

    return Common::StringFromFormat("%s%08x.app", content_path.c_str(), content_id);
}

std::string GetTitlePath(Service::FS::MediaType media_type, u64 tid) {
    u32 high = static_cast<u32>(tid >> 32);
    u32 low = static_cast<u32>(tid & 0xFFFFFFFF);

    if (media_type == Service::FS::MediaType::NAND || media_type == Service::FS::MediaType::SDMC)
        return Common::StringFromFormat("%s%08x/%08x/", GetMediaTitlePath(media_type).c_str(), high,
                                        low);

    if (media_type == Service::FS::MediaType::GameCard) {
        // TODO(shinyquagsire23): get current app path if TID matches?
        LOG_ERROR(Service_AM, "Request for gamecard title path unimplemented!");
        return "";
    }

    return "";
}

std::string GetMediaTitlePath(Service::FS::MediaType media_type) {
    if (media_type == Service::FS::MediaType::NAND)
        return Common::StringFromFormat("%s%s/title/", FileUtil::GetUserPath(D_NAND_IDX).c_str(),
                                        SYSTEM_ID);

    if (media_type == Service::FS::MediaType::SDMC)
        return Common::StringFromFormat("%sNintendo 3DS/%s/%s/title/",
                                        FileUtil::GetUserPath(D_SDMC_IDX).c_str(), SYSTEM_ID,
                                        SDCARD_ID);

    if (media_type == Service::FS::MediaType::GameCard) {
        // TODO(shinyquagsire23): get current app parent folder if TID matches?
        LOG_ERROR(Service_AM, "Request for gamecard parent path unimplemented!");
        return "";
    }

    return "";
}

void Module::ScanForTitles(Service::FS::MediaType media_type) {
    am_title_list[static_cast<u32>(media_type)].clear();

    std::string title_path = GetMediaTitlePath(media_type);

    FileUtil::FSTEntry entries;
    FileUtil::ScanDirectoryTree(title_path, entries, 1);
    for (const FileUtil::FSTEntry& tid_high : entries.children) {
        for (const FileUtil::FSTEntry& tid_low : tid_high.children) {
            std::string tid_string = tid_high.virtualName + tid_low.virtualName;

            if (tid_string.length() == TITLE_ID_VALID_LENGTH) {
                u64 tid = std::stoull(tid_string.c_str(), nullptr, 16);

                FileSys::NCCHContainer container(GetTitleContentPath(media_type, tid));
                if (container.Load() == Loader::ResultStatus::Success)
                    am_title_list[static_cast<u32>(media_type)].push_back(tid);
            }
        }
    }
}

void Module::ScanForAllTitles() {
    ScanForTitles(Service::FS::MediaType::NAND);
    ScanForTitles(Service::FS::MediaType::SDMC);
}

Module::Interface::Interface(std::shared_ptr<Module> am, const char* name, u32 max_session)
    : ServiceFramework(name, max_session), am(std::move(am)) {}

Module::Interface::~Interface() = default;

void Module::Interface::GetNumPrograms(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0001, 1, 0); // 0x00010040
    u32 media_type = rp.Pop<u8>();

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(am->am_title_list[media_type].size());
}

void Module::Interface::FindDLCContentInfos(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x1002, 4, 4); // 0x10020104

    auto media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    u64 title_id = rp.Pop<u64>();
    u32 content_count = rp.Pop<u32>();
    auto& content_requested_in = rp.PopMappedBuffer();
    auto& content_info_out = rp.PopMappedBuffer();

    // Validate that only DLC TIDs are passed in
    u32 tid_high = static_cast<u32>(title_id >> 32);
    if (tid_high != TID_HIGH_DLC) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 4);
        rb.Push(ResultCode(ErrCodes::InvalidTIDInList, ErrorModule::AM,
                           ErrorSummary::InvalidArgument, ErrorLevel::Usage));
        rb.PushMappedBuffer(content_requested_in);
        rb.PushMappedBuffer(content_info_out);
        return;
    }

    std::vector<u16_le> content_requested(content_count);
    content_requested_in.Read(content_requested.data(), 0, content_count * sizeof(u16));

    std::string tmd_path = GetTitleMetadataPath(media_type, title_id);

    u32 content_read = 0;
    FileSys::TitleMetadata tmd;
    if (tmd.Load(tmd_path) == Loader::ResultStatus::Success) {
        std::size_t write_offset = 0;
        // Get info for each content index requested
        for (size_t i = 0; i < content_count; i++) {
            std::shared_ptr<FileUtil::IOFile> romfs_file;
            u64 romfs_offset = 0;
            u64 romfs_size = 0;

            FileSys::NCCHContainer ncch_container(GetTitleContentPath(media_type, title_id, i));
            ncch_container.ReadRomFS(romfs_file, romfs_offset, romfs_size);

            ContentInfo content_info = {};
            content_info.index = static_cast<u16>(i);
            content_info.type = tmd.GetContentTypeByIndex(content_requested[i]);
            content_info.content_id = tmd.GetContentIDByIndex(content_requested[i]);
            content_info.size = tmd.GetContentSizeByIndex(content_requested[i]);
            content_info.romfs_size = romfs_size;

            content_info_out.Write(&content_info, write_offset, sizeof(ContentInfo));
            write_offset += sizeof(ContentInfo);
            content_read++;
        }
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 4);
    rb.Push(RESULT_SUCCESS);
    rb.PushMappedBuffer(content_requested_in);
    rb.PushMappedBuffer(content_info_out);
}

void Module::Interface::ListDLCContentInfos(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x1003, 5, 2); // 0x10030142

    u32 content_count = rp.Pop<u32>();
    auto media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    u64 title_id = rp.Pop<u64>();
    u32 start_index = rp.Pop<u32>();
    auto& content_info_out = rp.PopMappedBuffer();

    // Validate that only DLC TIDs are passed in
    u32 tid_high = static_cast<u32>(title_id >> 32);
    if (tid_high != TID_HIGH_DLC) {
        IPC::RequestBuilder rb = rp.MakeBuilder(2, 2);
        rb.Push(ResultCode(ErrCodes::InvalidTIDInList, ErrorModule::AM,
                           ErrorSummary::InvalidArgument, ErrorLevel::Usage));
        rb.Push<u32>(0);
        rb.PushMappedBuffer(content_info_out);
        return;
    }

    std::string tmd_path = GetTitleMetadataPath(media_type, title_id);

    u32 copied = 0;
    FileSys::TitleMetadata tmd;
    if (tmd.Load(tmd_path) == Loader::ResultStatus::Success) {
        copied = std::min(content_count, static_cast<u32>(tmd.GetContentCount()));
        std::size_t write_offset = 0;
        for (u32 i = start_index; i < copied; i++) {
            std::shared_ptr<FileUtil::IOFile> romfs_file;
            u64 romfs_offset = 0;
            u64 romfs_size = 0;

            FileSys::NCCHContainer ncch_container(GetTitleContentPath(media_type, title_id, i));
            ncch_container.ReadRomFS(romfs_file, romfs_offset, romfs_size);

            ContentInfo content_info = {};
            content_info.index = static_cast<u16>(i);
            content_info.type = tmd.GetContentTypeByIndex(i);
            content_info.content_id = tmd.GetContentIDByIndex(i);
            content_info.size = tmd.GetContentSizeByIndex(i);
            content_info.romfs_size = romfs_size;

            content_info_out.Write(&content_info, write_offset, sizeof(ContentInfo));
            write_offset += sizeof(ContentInfo);
        }
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 2);
    rb.Push(RESULT_SUCCESS);
    rb.Push(copied);
    rb.PushMappedBuffer(content_info_out);
}

void Module::Interface::DeleteContents(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x1004, 4, 2); // 0x10040102
    u8 media_type = rp.Pop<u8>();
    u64 title_id = rp.Pop<u64>();
    u32 content_count = rp.Pop<u32>();
    auto& content_ids_in = rp.PopMappedBuffer();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(RESULT_SUCCESS);
    rb.PushMappedBuffer(content_ids_in);
    LOG_WARNING(Service_AM, "(STUBBED) media_type={}, title_id=0x{:016x}, content_count={}",
                media_type, title_id, content_count);
}

void Module::Interface::GetProgramList(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0002, 2, 2); // 0x00020082

    u32 count = rp.Pop<u32>();
    u8 media_type = rp.Pop<u8>();
    auto& title_ids_output = rp.PopMappedBuffer();

    if (media_type > 2) {
        IPC::RequestBuilder rb = rp.MakeBuilder(2, 2);
        rb.Push<u32>(-1); // TODO(shinyquagsire23): Find the right error code
        rb.Push<u32>(0);
        rb.PushMappedBuffer(title_ids_output);
        return;
    }

    u32 media_count = static_cast<u32>(am->am_title_list[media_type].size());
    u32 copied = std::min(media_count, count);

    title_ids_output.Write(am->am_title_list[media_type].data(), 0, copied * sizeof(u64));

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 2);
    rb.Push(RESULT_SUCCESS);
    rb.Push(copied);
    rb.PushMappedBuffer(title_ids_output);
}

ResultCode GetTitleInfoFromList(const std::vector<u64>& title_id_list,
                                Service::FS::MediaType media_type,
                                Kernel::MappedBuffer& title_info_out) {
    std::size_t write_offset = 0;
    for (u32 i = 0; i < title_id_list.size(); i++) {
        std::string tmd_path = GetTitleMetadataPath(media_type, title_id_list[i]);

        TitleInfo title_info = {};
        title_info.tid = title_id_list[i];

        FileSys::TitleMetadata tmd;
        if (tmd.Load(tmd_path) == Loader::ResultStatus::Success) {
            // TODO(shinyquagsire23): This is the total size of all files this process owns,
            // including savefiles and other content. This comes close but is off.
            title_info.size = tmd.GetContentSizeByIndex(FileSys::TMDContentIndex::Main);
            title_info.version = tmd.GetTitleVersion();
            title_info.type = tmd.GetTitleType();
        } else {
            return ResultCode(ErrorDescription::NotFound, ErrorModule::AM,
                              ErrorSummary::InvalidState, ErrorLevel::Permanent);
        }
        title_info_out.Write(&title_info, write_offset, sizeof(TitleInfo));
        write_offset += sizeof(TitleInfo);
    }

    return RESULT_SUCCESS;
}

void Module::Interface::GetProgramInfos(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0003, 2, 4); // 0x00030084

    auto media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    u32 title_count = rp.Pop<u32>();
    auto& title_id_list_buffer = rp.PopMappedBuffer();
    auto& title_info_out = rp.PopMappedBuffer();

    std::vector<u64> title_id_list(title_count);
    title_id_list_buffer.Read(title_id_list.data(), 0, title_count * sizeof(u64));

    ResultCode result = GetTitleInfoFromList(title_id_list, media_type, title_info_out);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 4);
    rb.Push(result);
    rb.PushMappedBuffer(title_id_list_buffer);
    rb.PushMappedBuffer(title_info_out);
}

void Module::Interface::DeleteUserProgram(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0004, 3, 0);
    auto media_type = rp.PopEnum<FS::MediaType>();
    u64 title_id = rp.Pop<u64>();
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    u16 category = static_cast<u16>((title_id >> 32) & 0xFFFF);
    u8 variation = static_cast<u8>(title_id & 0xFF);
    if (category & CATEGORY_SYSTEM || category & CATEGORY_DLP || variation & VARIATION_SYSTEM) {
        LOG_ERROR(Service_AM, "Trying to uninstall system app");
        rb.Push(ResultCode(ErrCodes::TryingToUninstallSystemApp, ErrorModule::AM,
                           ErrorSummary::InvalidArgument, ErrorLevel::Usage));
        return;
    }
    LOG_INFO(Service_AM, "Deleting title 0x{:016x}", title_id);
    std::string path = GetTitlePath(media_type, title_id);
    if (!FileUtil::Exists(path)) {
        rb.Push(ResultCode(ErrorDescription::NotFound, ErrorModule::AM, ErrorSummary::InvalidState,
                           ErrorLevel::Permanent));
        LOG_ERROR(Service_AM, "Title not found");
        return;
    }
    bool success = FileUtil::DeleteDirRecursively(path);
    am->ScanForAllTitles();
    rb.Push(RESULT_SUCCESS);
    if (!success)
        LOG_ERROR(Service_AM, "FileUtil::DeleteDirRecursively unexpectedly failed");
}

void Module::Interface::GetProductCode(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0005, 3, 0);
    FS::MediaType media_type = rp.PopEnum<FS::MediaType>();
    u64 title_id = rp.Pop<u64>();
    std::string path = GetTitleContentPath(media_type, title_id);

    if (!FileUtil::Exists(path)) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultCode(ErrorDescription::NotFound, ErrorModule::AM, ErrorSummary::InvalidState,
                           ErrorLevel::Permanent));
    } else {
        struct ProductCode {
            u8 code[0x10];
        };

        ProductCode product_code;

        IPC::RequestBuilder rb = rp.MakeBuilder(6, 0);
        FileSys::NCCHContainer ncch(path);
        ncch.Load();
        std::memcpy(&product_code.code, &ncch.ncch_header.product_code, 0x10);
        rb.Push(RESULT_SUCCESS);
        rb.PushRaw(product_code);
    }
}

void Module::Interface::GetDLCTitleInfos(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x1005, 2, 4); // 0x10050084

    auto media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    u32 title_count = rp.Pop<u32>();
    auto& title_id_list_buffer = rp.PopMappedBuffer();
    auto& title_info_out = rp.PopMappedBuffer();

    std::vector<u64> title_id_list(title_count);
    title_id_list_buffer.Read(title_id_list.data(), 0, title_count * sizeof(u64));

    ResultCode result = RESULT_SUCCESS;

    // Validate that DLC TIDs were passed in
    for (u32 i = 0; i < title_count; i++) {
        u32 tid_high = static_cast<u32>(title_id_list[i] >> 32);
        if (tid_high != TID_HIGH_DLC) {
            result = ResultCode(ErrCodes::InvalidTIDInList, ErrorModule::AM,
                                ErrorSummary::InvalidArgument, ErrorLevel::Usage);
            break;
        }
    }

    if (result.IsSuccess()) {
        result = GetTitleInfoFromList(title_id_list, media_type, title_info_out);
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 4);
    rb.Push(result);
    rb.PushMappedBuffer(title_id_list_buffer);
    rb.PushMappedBuffer(title_info_out);
}

void Module::Interface::GetPatchTitleInfos(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x100D, 2, 4); // 0x100D0084

    auto media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    u32 title_count = rp.Pop<u32>();
    auto& title_id_list_buffer = rp.PopMappedBuffer();
    auto& title_info_out = rp.PopMappedBuffer();

    std::vector<u64> title_id_list(title_count);
    title_id_list_buffer.Read(title_id_list.data(), 0, title_count * sizeof(u64));

    ResultCode result = RESULT_SUCCESS;

    // Validate that update TIDs were passed in
    for (u32 i = 0; i < title_count; i++) {
        u32 tid_high = static_cast<u32>(title_id_list[i] >> 32);
        if (tid_high != TID_HIGH_UPDATE) {
            result = ResultCode(ErrCodes::InvalidTIDInList, ErrorModule::AM,
                                ErrorSummary::InvalidArgument, ErrorLevel::Usage);
            break;
        }
    }

    if (result.IsSuccess()) {
        result = GetTitleInfoFromList(title_id_list, media_type, title_info_out);
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 4);
    rb.Push(result);
    rb.PushMappedBuffer(title_id_list_buffer);
    rb.PushMappedBuffer(title_info_out);
}

void Module::Interface::ListDataTitleTicketInfos(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x1007, 4, 2); // 0x10070102
    u32 ticket_count = rp.Pop<u32>();
    u64 title_id = rp.Pop<u64>();
    u32 start_index = rp.Pop<u32>();
    auto& ticket_info_out = rp.PopMappedBuffer();

    std::size_t write_offset = 0;
    for (u32 i = 0; i < ticket_count; i++) {
        TicketInfo ticket_info = {};
        ticket_info.title_id = title_id;
        ticket_info.version = 0; // TODO
        ticket_info.size = 0;    // TODO

        ticket_info_out.Write(&ticket_info, write_offset, sizeof(TicketInfo));
        write_offset += sizeof(TicketInfo);
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 2);
    rb.Push(RESULT_SUCCESS);
    rb.Push(ticket_count);
    rb.PushMappedBuffer(ticket_info_out);

    LOG_WARNING(Service_AM,
                "(STUBBED) ticket_count=0x{:08X}, title_id=0x{:016x}, start_index=0x{:08X}",
                ticket_count, title_id, start_index);
}

void Module::Interface::GetDLCContentInfoCount(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x1001, 3, 0); // 0x100100C0
    auto media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    u64 title_id = rp.Pop<u64>();

    // Validate that only DLC TIDs are passed in
    u32 tid_high = static_cast<u32>(title_id >> 32);
    if (tid_high != TID_HIGH_DLC) {
        IPC::RequestBuilder rb = rp.MakeBuilder(2, 2);
        rb.Push(ResultCode(ErrCodes::InvalidTID, ErrorModule::AM, ErrorSummary::InvalidArgument,
                           ErrorLevel::Usage));
        rb.Push<u32>(0);
        return;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(RESULT_SUCCESS); // No error

    std::string tmd_path = GetTitleMetadataPath(media_type, title_id);

    FileSys::TitleMetadata tmd;
    if (tmd.Load(tmd_path) == Loader::ResultStatus::Success) {
        rb.Push<u32>(tmd.GetContentCount());
    } else {
        rb.Push<u32>(1); // Number of content infos plus one
        LOG_WARNING(Service_AM, "(STUBBED) called media_type={}, title_id=0x{:016x}",
                    static_cast<u32>(media_type), title_id);
    }
}

void Module::Interface::DeleteTicket(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0007, 2, 0); // 0x00070080
    u64 title_id = rp.Pop<u64>();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_AM, "(STUBBED) called title_id=0x{:016x}", title_id);
}

void Module::Interface::GetNumTickets(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0008, 0, 0); // 0x00080000
    u32 ticket_count = 0;

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(RESULT_SUCCESS);
    rb.Push(ticket_count);
    LOG_WARNING(Service_AM, "(STUBBED) called ticket_count=0x{:08x}", ticket_count);
}

void Module::Interface::GetTicketList(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0009, 2, 2); // 0x00090082
    u32 ticket_list_count = rp.Pop<u32>();
    u32 ticket_index = rp.Pop<u32>();
    auto& ticket_tids_out = rp.PopMappedBuffer();

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 2);
    rb.Push(RESULT_SUCCESS);
    rb.Push(ticket_list_count);
    rb.PushMappedBuffer(ticket_tids_out);
    LOG_WARNING(Service_AM, "(STUBBED) ticket_list_count=0x{:08x}, ticket_index=0x{:08x}",
                ticket_list_count, ticket_index);
}

void Module::Interface::QueryAvailableTitleDatabase(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0019, 1, 0); // 0x190040
    u8 media_type = rp.Pop<u8>();

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(RESULT_SUCCESS); // No error
    rb.Push(true);

    LOG_WARNING(Service_AM, "(STUBBED) media_type={}", media_type);
}

void Module::Interface::CheckContentRights(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0025, 3, 0); // 0x2500C0
    u64 tid = rp.Pop<u64>();
    u16 content_index = rp.Pop<u16>();

    // TODO(shinyquagsire23): Read tickets for this instead?
    bool has_rights =
        FileUtil::Exists(GetTitleContentPath(Service::FS::MediaType::SDMC, tid, content_index));

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(RESULT_SUCCESS); // No error
    rb.Push(has_rights);

    LOG_WARNING(Service_AM, "(STUBBED) tid={:016x}, content_index={}", tid, content_index);
}

void Module::Interface::CheckContentRightsIgnorePlatform(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x002D, 3, 0); // 0x2D00C0
    u64 tid = rp.Pop<u64>();
    u16 content_index = rp.Pop<u16>();

    // TODO(shinyquagsire23): Read tickets for this instead?
    bool has_rights =
        FileUtil::Exists(GetTitleContentPath(Service::FS::MediaType::SDMC, tid, content_index));

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(RESULT_SUCCESS); // No error
    rb.Push(has_rights);

    LOG_WARNING(Service_AM, "(STUBBED) tid={:016x}, content_index={}", tid, content_index);
}

void Module::Interface::BeginImportProgram(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0402, 1, 0); // 0x04020040
    auto media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());

    if (am->cia_installing) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultCode(ErrCodes::CIACurrentlyInstalling, ErrorModule::AM,
                           ErrorSummary::InvalidState, ErrorLevel::Permanent));
        return;
    }

    // Create our CIAFile handle for the app to write to, and while the app writes
    // Citra will store contents out to sdmc/nand
    const FileSys::Path cia_path = {};
    auto file =
        std::make_shared<Service::FS::File>(std::make_unique<CIAFile>(media_type), cia_path);

    am->cia_installing = true;

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(RESULT_SUCCESS); // No error
    rb.PushCopyObjects(file->Connect());

    LOG_WARNING(Service_AM, "(STUBBED) media_type={}", static_cast<u32>(media_type));
}

void Module::Interface::EndImportProgram(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0405, 0, 2); // 0x04050002
    auto cia = rp.PopObject<Kernel::ClientSession>();

    am->ScanForAllTitles();

    am->cia_installing = false;
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(RESULT_SUCCESS);
}

ResultVal<std::shared_ptr<Service::FS::File>> GetFileFromSession(
    Kernel::SharedPtr<Kernel::ClientSession> file_session) {
    // Step up the chain from ClientSession->ServerSession and then
    // cast to File. For AM on 3DS, invalid handles actually hang the system.

    if (file_session->parent == nullptr) {
        LOG_WARNING(Service_AM, "Invalid file handle!");
        return Kernel::ERR_INVALID_HANDLE;
    }

    Kernel::SharedPtr<Kernel::ServerSession> server = file_session->parent->server;
    if (server == nullptr) {
        LOG_WARNING(Service_AM, "File handle ServerSession disconnected!");
        return Kernel::ERR_SESSION_CLOSED_BY_REMOTE;
    }

    if (server->hle_handler != nullptr) {
        auto file = std::dynamic_pointer_cast<Service::FS::File>(server->hle_handler);

        // TODO(shinyquagsire23): This requires RTTI, use service calls directly instead?
        if (file != nullptr)
            return MakeResult<std::shared_ptr<Service::FS::File>>(file);

        LOG_ERROR(Service_AM, "Failed to cast handle to FSFile!");
        return Kernel::ERR_INVALID_HANDLE;
    }

    // Probably the best bet if someone is LLEing the fs service is to just have them LLE AM
    // while they're at it, so not implemented.
    LOG_ERROR(Service_AM, "Given file handle does not have an HLE handler!");
    return Kernel::ERR_NOT_IMPLEMENTED;
}

void Module::Interface::GetProgramInfoFromCia(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0408, 1, 2); // 0x04080042
    auto media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    auto cia = rp.PopObject<Kernel::ClientSession>();

    auto file_res = GetFileFromSession(cia);
    if (!file_res.Succeeded()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(file_res.Code());
        return;
    }

    auto file = file_res.Unwrap();
    FileSys::CIAContainer container;
    if (container.Load(*file->backend) != Loader::ResultStatus::Success) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultCode(ErrCodes::InvalidCIAHeader, ErrorModule::AM,
                           ErrorSummary::InvalidArgument, ErrorLevel::Permanent));
        return;
    }

    FileSys::TitleMetadata tmd = container.GetTitleMetadata();
    TitleInfo title_info = {};
    container.Print();

    // TODO(shinyquagsire23): Sizes allegedly depend on the mediatype, and will double
    // on some mediatypes. Since this is more of a required install size we'll report
    // what Citra needs, but it would be good to be more accurate here.
    title_info.tid = tmd.GetTitleID();
    title_info.size = tmd.GetContentSizeByIndex(FileSys::TMDContentIndex::Main);
    title_info.version = tmd.GetTitleVersion();
    title_info.type = tmd.GetTitleType();

    IPC::RequestBuilder rb = rp.MakeBuilder(8, 0);
    rb.Push(RESULT_SUCCESS);
    rb.PushRaw<TitleInfo>(title_info);
}

void Module::Interface::GetSystemMenuDataFromCia(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0409, 0, 4); // 0x04090004
    auto cia = rp.PopObject<Kernel::ClientSession>();
    auto& output_buffer = rp.PopMappedBuffer();

    auto file_res = GetFileFromSession(cia);
    if (!file_res.Succeeded()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
        rb.Push(file_res.Code());
        rb.PushMappedBuffer(output_buffer);
        return;
    }

    std::size_t output_buffer_size = std::min(output_buffer.GetSize(), sizeof(Loader::SMDH));

    auto file = file_res.Unwrap();
    FileSys::CIAContainer container;
    if (container.Load(*file->backend) != Loader::ResultStatus::Success) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
        rb.Push(ResultCode(ErrCodes::InvalidCIAHeader, ErrorModule::AM,
                           ErrorSummary::InvalidArgument, ErrorLevel::Permanent));
        rb.PushMappedBuffer(output_buffer);
        return;
    }
    std::vector<u8> temp(output_buffer_size);

    //  Read from the Meta offset + 0x400 for the 0x36C0-large SMDH
    auto read_result = file->backend->Read(
        container.GetMetadataOffset() + FileSys::CIA_METADATA_SIZE, temp.size(), temp.data());
    if (read_result.Failed() || *read_result != temp.size()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
        rb.Push(ResultCode(ErrCodes::InvalidCIAHeader, ErrorModule::AM,
                           ErrorSummary::InvalidArgument, ErrorLevel::Permanent));
        rb.PushMappedBuffer(output_buffer);
        return;
    }

    output_buffer.Write(temp.data(), 0, temp.size());

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(RESULT_SUCCESS);
    rb.PushMappedBuffer(output_buffer);
}

void Module::Interface::GetDependencyListFromCia(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x040A, 0, 2); // 0x040A0002
    auto cia = rp.PopObject<Kernel::ClientSession>();

    auto file_res = GetFileFromSession(cia);
    if (!file_res.Succeeded()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(file_res.Code());
        return;
    }

    auto file = file_res.Unwrap();
    FileSys::CIAContainer container;
    if (container.Load(*file->backend) != Loader::ResultStatus::Success) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultCode(ErrCodes::InvalidCIAHeader, ErrorModule::AM,
                           ErrorSummary::InvalidArgument, ErrorLevel::Permanent));
        return;
    }

    std::vector<u8> buffer(FileSys::CIA_DEPENDENCY_SIZE);
    std::memcpy(buffer.data(), container.GetDependencies().data(), buffer.size());

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(RESULT_SUCCESS);
    rb.PushStaticBuffer(buffer, 0);
}

void Module::Interface::GetTransferSizeFromCia(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x040B, 0, 2); // 0x040B0002
    auto cia = rp.PopObject<Kernel::ClientSession>();

    auto file_res = GetFileFromSession(cia);
    if (!file_res.Succeeded()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(file_res.Code());
        return;
    }

    auto file = file_res.Unwrap();
    FileSys::CIAContainer container;
    if (container.Load(*file->backend) != Loader::ResultStatus::Success) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultCode(ErrCodes::InvalidCIAHeader, ErrorModule::AM,
                           ErrorSummary::InvalidArgument, ErrorLevel::Permanent));
        return;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(3, 0);
    rb.Push(RESULT_SUCCESS);
    rb.Push(container.GetMetadataOffset());
}

void Module::Interface::GetCoreVersionFromCia(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x040C, 0, 2); // 0x040C0002
    auto cia = rp.PopObject<Kernel::ClientSession>();

    auto file_res = GetFileFromSession(cia);
    if (!file_res.Succeeded()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(file_res.Code());
        return;
    }

    auto file = file_res.Unwrap();
    FileSys::CIAContainer container;
    if (container.Load(*file->backend) != Loader::ResultStatus::Success) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultCode(ErrCodes::InvalidCIAHeader, ErrorModule::AM,
                           ErrorSummary::InvalidArgument, ErrorLevel::Permanent));
        return;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(RESULT_SUCCESS);
    rb.Push(container.GetCoreVersion());
}

void Module::Interface::GetRequiredSizeFromCia(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x040D, 1, 2); // 0x040D0042
    auto media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    auto cia = rp.PopObject<Kernel::ClientSession>();

    auto file_res = GetFileFromSession(cia);
    if (!file_res.Succeeded()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(file_res.Code());
        return;
    }

    auto file = file_res.Unwrap();
    FileSys::CIAContainer container;
    if (container.Load(*file->backend) != Loader::ResultStatus::Success) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultCode(ErrCodes::InvalidCIAHeader, ErrorModule::AM,
                           ErrorSummary::InvalidArgument, ErrorLevel::Permanent));
        return;
    }

    // TODO(shinyquagsire23): Sizes allegedly depend on the mediatype, and will double
    // on some mediatypes. Since this is more of a required install size we'll report
    // what Citra needs, but it would be good to be more accurate here.
    IPC::RequestBuilder rb = rp.MakeBuilder(3, 0);
    rb.Push(RESULT_SUCCESS);
    rb.Push(container.GetTitleMetadata().GetContentSizeByIndex(FileSys::TMDContentIndex::Main));
}

void Module::Interface::DeleteProgram(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0410, 3, 0);
    auto media_type = rp.PopEnum<FS::MediaType>();
    u64 title_id = rp.Pop<u64>();
    LOG_INFO(Service_AM, "Deleting title 0x{:016x}", title_id);
    std::string path = GetTitlePath(media_type, title_id);
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    if (!FileUtil::Exists(path)) {
        rb.Push(ResultCode(ErrorDescription::NotFound, ErrorModule::AM, ErrorSummary::InvalidState,
                           ErrorLevel::Permanent));
        LOG_ERROR(Service_AM, "Title not found");
        return;
    }
    bool success = FileUtil::DeleteDirRecursively(path);
    am->ScanForAllTitles();
    rb.Push(RESULT_SUCCESS);
    if (!success)
        LOG_ERROR(Service_AM, "FileUtil::DeleteDirRecursively unexpectedly failed");
}

void Module::Interface::GetMetaSizeFromCia(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0413, 0, 2); // 0x04130002
    auto cia = rp.PopObject<Kernel::ClientSession>();

    auto file_res = GetFileFromSession(cia);
    if (!file_res.Succeeded()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(file_res.Code());
        return;
    }

    auto file = file_res.Unwrap();
    FileSys::CIAContainer container;
    if (container.Load(*file->backend) != Loader::ResultStatus::Success) {

        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultCode(ErrCodes::InvalidCIAHeader, ErrorModule::AM,
                           ErrorSummary::InvalidArgument, ErrorLevel::Permanent));
        return;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(RESULT_SUCCESS);
    rb.Push(container.GetMetadataSize());
}

void Module::Interface::GetMetaDataFromCia(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0414, 1, 4); // 0x04140044

    u32 output_size = rp.Pop<u32>();
    auto cia = rp.PopObject<Kernel::ClientSession>();
    auto& output_buffer = rp.PopMappedBuffer();

    auto file_res = GetFileFromSession(cia);
    if (!file_res.Succeeded()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
        rb.Push(file_res.Code());
        rb.PushMappedBuffer(output_buffer);
        return;
    }
    // Don't write beyond the actual static buffer size.
    output_size = std::min(static_cast<u32>(output_buffer.GetSize()), output_size);

    auto file = file_res.Unwrap();
    FileSys::CIAContainer container;
    if (container.Load(*file->backend) != Loader::ResultStatus::Success) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
        rb.Push(ResultCode(ErrCodes::InvalidCIAHeader, ErrorModule::AM,
                           ErrorSummary::InvalidArgument, ErrorLevel::Permanent));
        rb.PushMappedBuffer(output_buffer);
        return;
    }

    //  Read from the Meta offset for the specified size
    std::vector<u8> temp(output_size);
    auto read_result = file->backend->Read(container.GetMetadataOffset(), output_size, temp.data());
    if (read_result.Failed() || *read_result != output_size) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultCode(ErrCodes::InvalidCIAHeader, ErrorModule::AM,
                           ErrorSummary::InvalidArgument, ErrorLevel::Permanent));
        return;
    }

    output_buffer.Write(temp.data(), 0, output_size);
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(RESULT_SUCCESS);
    rb.PushMappedBuffer(output_buffer);
}

Module::Module() {
    ScanForAllTitles();
}

Module::~Module() = default;

void InstallInterfaces(SM::ServiceManager& service_manager) {
    auto am = std::make_shared<Module>();
    std::make_shared<AM_APP>(am)->InstallAsService(service_manager);
    std::make_shared<AM_NET>(am)->InstallAsService(service_manager);
    std::make_shared<AM_SYS>(am)->InstallAsService(service_manager);
    std::make_shared<AM_U>(am)->InstallAsService(service_manager);
}

} // namespace AM

} // namespace Service
