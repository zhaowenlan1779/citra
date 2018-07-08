// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <fmt/format.h>
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/hle/ipc.h"
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/handle_table.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/server_port.h"
#include "core/hle/kernel/server_session.h"
#include "core/hle/service/ac/ac.h"
#include "core/hle/service/act/act.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/apt/apt.h"
#include "core/hle/service/boss/boss.h"
#include "core/hle/service/cam/cam.h"
#include "core/hle/service/cecd/cecd.h"
#include "core/hle/service/cfg/cfg.h"
#include "core/hle/service/csnd_snd.h"
#include "core/hle/service/dlp/dlp.h"
#include "core/hle/service/dsp_dsp.h"
#include "core/hle/service/err_f.h"
#include "core/hle/service/frd/frd.h"
#include "core/hle/service/fs/archive.h"
#include "core/hle/service/fs/fs_user.h"
#include "core/hle/service/gsp/gsp.h"
#include "core/hle/service/gsp/gsp_lcd.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/http_c.h"
#include "core/hle/service/ir/ir.h"
#include "core/hle/service/ldr_ro/ldr_ro.h"
#include "core/hle/service/mic_u.h"
#include "core/hle/service/mvd/mvd.h"
#include "core/hle/service/ndm/ndm_u.h"
#include "core/hle/service/news/news.h"
#include "core/hle/service/nfc/nfc.h"
#include "core/hle/service/nim/nim.h"
#include "core/hle/service/ns/ns.h"
#include "core/hle/service/nwm/nwm.h"
#include "core/hle/service/pm/pm.h"
#include "core/hle/service/ptm/ptm.h"
#include "core/hle/service/pxi/pxi.h"
#include "core/hle/service/qtm/qtm.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/sm/srv.h"
#include "core/hle/service/soc_u.h"
#include "core/hle/service/ssl_c.h"
#include "core/hle/service/y2r_u.h"

using Kernel::ClientPort;
using Kernel::ServerPort;
using Kernel::ServerSession;
using Kernel::SharedPtr;

namespace Service {

std::unordered_map<std::string, SharedPtr<ClientPort>> g_kernel_named_ports;

/**
 * Creates a function string for logging, complete with the name (or header code, depending
 * on what's passed in) the port name, and all the cmd_buff arguments.
 */
static std::string MakeFunctionString(const char* name, const char* port_name,
                                      const u32* cmd_buff) {
    // Number of params == bits 0-5 + bits 6-11
    int num_params = (cmd_buff[0] & 0x3F) + ((cmd_buff[0] >> 6) & 0x3F);

    std::string function_string =
        Common::StringFromFormat("function '%s': port=%s", name, port_name);
    for (int i = 1; i <= num_params; ++i) {
        function_string += Common::StringFromFormat(", cmd_buff[%i]=0x%X", i, cmd_buff[i]);
    }
    return function_string;
}

Interface::Interface(u32 max_sessions) : max_sessions(max_sessions) {}
Interface::~Interface() = default;

void Interface::HandleSyncRequest(SharedPtr<ServerSession> server_session) {
    // TODO(Subv): Make use of the server_session in the HLE service handlers to distinguish which
    // session triggered each command.

    u32* cmd_buff = Kernel::GetCommandBuffer();
    auto itr = m_functions.find(cmd_buff[0]);

    if (itr == m_functions.end() || itr->second.func == nullptr) {
        std::string function_name = (itr == m_functions.end())
                                        ? Common::StringFromFormat("0x%08X", cmd_buff[0])
                                        : itr->second.name;
        LOG_ERROR(Service, "unknown / unimplemented {}",
                  MakeFunctionString(function_name.c_str(), GetPortName().c_str(), cmd_buff));

        // TODO(bunnei): Hack - ignore error
        cmd_buff[1] = 0;
        return;
    }
    LOG_TRACE(Service, "{}", MakeFunctionString(itr->second.name, GetPortName().c_str(), cmd_buff));

    itr->second.func(this);
}

void Interface::Register(const FunctionInfo* functions, size_t n) {
    m_functions.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        // Usually this array is sorted by id already, so hint to instead at the end
        m_functions.emplace_hint(m_functions.cend(), functions[i].id, functions[i]);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ServiceFrameworkBase::ServiceFrameworkBase(const char* service_name, u32 max_sessions,
                                           InvokerFn* handler_invoker)
    : service_name(service_name), max_sessions(max_sessions), handler_invoker(handler_invoker) {}

ServiceFrameworkBase::~ServiceFrameworkBase() = default;

void ServiceFrameworkBase::InstallAsService(SM::ServiceManager& service_manager) {
    ASSERT(port == nullptr);
    port = service_manager.RegisterService(service_name, max_sessions).Unwrap();
    port->SetHleHandler(shared_from_this());
}

void ServiceFrameworkBase::InstallAsNamedPort() {
    ASSERT(port == nullptr);
    SharedPtr<ServerPort> server_port;
    SharedPtr<ClientPort> client_port;
    std::tie(server_port, client_port) = ServerPort::CreatePortPair(max_sessions, service_name);
    server_port->SetHleHandler(shared_from_this());
    AddNamedPort(service_name, std::move(client_port));
}

void ServiceFrameworkBase::RegisterHandlersBase(const FunctionInfoBase* functions, size_t n) {
    handlers.reserve(handlers.size() + n);
    for (size_t i = 0; i < n; ++i) {
        // Usually this array is sorted by id already, so hint to insert at the end
        handlers.emplace_hint(handlers.cend(), functions[i].expected_header, functions[i]);
    }
}

void ServiceFrameworkBase::ReportUnimplementedFunction(u32* cmd_buf, const FunctionInfoBase* info) {
    IPC::Header header{cmd_buf[0]};
    int num_params = header.normal_params_size + header.translate_params_size;
    std::string function_name = info == nullptr ? fmt::format("{:#08x}", cmd_buf[0]) : info->name;

    fmt::memory_buffer buf;
    fmt::format_to(buf, "function '{}': port='{}' cmd_buf={{[0]={:#x}", function_name, service_name,
                   cmd_buf[0]);
    for (int i = 1; i <= num_params; ++i) {
        fmt::format_to(buf, ", [{}]={:#x}", i, cmd_buf[i]);
    }
    buf.push_back('}');

    LOG_ERROR(Service, "unknown / unimplemented {}", fmt::to_string(buf));
    // TODO(bunnei): Hack - ignore error
    cmd_buf[1] = 0;
}

void ServiceFrameworkBase::HandleSyncRequest(SharedPtr<ServerSession> server_session) {
    u32* cmd_buf = Kernel::GetCommandBuffer();

    u32 header_code = cmd_buf[0];
    auto itr = handlers.find(header_code);
    const FunctionInfoBase* info = itr == handlers.end() ? nullptr : &itr->second;
    if (info == nullptr || info->handler_callback == nullptr) {
        return ReportUnimplementedFunction(cmd_buf, info);
    }

    // TODO(yuriks): The kernel should be the one handling this as part of translation after
    // everything else is migrated
    Kernel::HLERequestContext context(std::move(server_session));
    context.PopulateFromIncomingCommandBuffer(cmd_buf, *Kernel::g_current_process,
                                              Kernel::g_handle_table);

    LOG_TRACE(Service, "{}", MakeFunctionString(info->name, GetServiceName().c_str(), cmd_buf));
    handler_invoker(this, info->handler_callback, context);

    auto thread = Kernel::GetCurrentThread();
    ASSERT(thread->status == THREADSTATUS_RUNNING || thread->status == THREADSTATUS_WAIT_HLE_EVENT);
    // Only write the response immediately if the thread is still running. If the HLE handler put
    // the thread to sleep then the writing of the command buffer will be deferred to the wakeup
    // callback.
    if (thread->status == THREADSTATUS_RUNNING) {
        context.WriteToOutgoingCommandBuffer(cmd_buf, *Kernel::g_current_process,
                                             Kernel::g_handle_table);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Module interface

// TODO(yuriks): Move to kernel
void AddNamedPort(std::string name, SharedPtr<ClientPort> port) {
    g_kernel_named_ports.emplace(std::move(name), std::move(port));
}

static void AddNamedPort(Interface* interface_) {
    SharedPtr<ServerPort> server_port;
    SharedPtr<ClientPort> client_port;
    std::tie(server_port, client_port) =
        ServerPort::CreatePortPair(interface_->GetMaxSessions(), interface_->GetPortName());

    server_port->SetHleHandler(std::shared_ptr<Interface>(interface_));
    AddNamedPort(interface_->GetPortName(), std::move(client_port));
}

void AddService(Interface* interface_) {
    auto server_port = Core::System::GetInstance()
                           .ServiceManager()
                           .RegisterService(interface_->GetPortName(), interface_->GetMaxSessions())
                           .Unwrap();
    server_port->SetHleHandler(std::shared_ptr<Interface>(interface_));
}

/// Initialize ServiceManager
void Init(std::shared_ptr<SM::ServiceManager>& sm) {
    SM::ServiceManager::InstallInterfaces(sm);

    ERR::InstallInterfaces();

    PXI::InstallInterfaces(*sm);
    NS::InstallInterfaces(*sm);
    AC::InstallInterfaces(*sm);
    LDR::InstallInterfaces(*sm);
    MIC::InstallInterfaces(*sm);
    NWM::InstallInterfaces(*sm);

    FS::InstallInterfaces(*sm);
    FS::ArchiveInit();
    ACT::InstallInterfaces(*sm);
    AM::InstallInterfaces(*sm);
    APT::InstallInterfaces(*sm);
    BOSS::Init();
    CAM::InstallInterfaces(*sm);
    CECD::InstallInterfaces(*sm);
    CFG::InstallInterfaces(*sm);
    DLP::InstallInterfaces(*sm);
    FRD::InstallInterfaces(*sm);
    GSP::InstallInterfaces(*sm);
    HID::InstallInterfaces(*sm);
    IR::InstallInterfaces(*sm);
    MVD::InstallInterfaces(*sm);
    NDM::InstallInterfaces(*sm);
    NEWS::InstallInterfaces(*sm);
    NFC::InstallInterfaces(*sm);
    NIM::InstallInterfaces(*sm);
    NWM::Init();
    PTM::InstallInterfaces(*sm);
    QTM::InstallInterfaces(*sm);

    CSND::InstallInterfaces(*sm);
    AddService(new DSP_DSP::Interface);
    AddService(new HTTP::HTTP_C);
    PM::InstallInterfaces(*sm);
    AddService(new SOC::SOC_U);
    SSL::InstallInterfaces(*sm);
    Y2R::InstallInterfaces(*sm);

    LOG_DEBUG(Service, "initialized OK");
}

/// Shutdown ServiceManager
void Shutdown() {
    BOSS::Shutdown();
    FS::ArchiveShutdown();

    g_kernel_named_ports.clear();
    LOG_DEBUG(Service, "shutdown OK");
}
} // namespace Service
