// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include "common/common_types.h"
#include "common/logging/log.h"
#include "core/hle/kernel/address_arbiter.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/thread.h"
#include "core/memory.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// Kernel namespace

namespace Kernel {

void AddressArbiter::WaitThread(SharedPtr<Thread> thread, VAddr wait_address) {
    thread->wait_address = wait_address;
    thread->status = THREADSTATUS_WAIT_ARB;
    waiting_threads.emplace_back(std::move(thread));
}

void AddressArbiter::ResumeAllThreads(VAddr address) {
    // Determine which threads are waiting on this address, those should be woken up.
    auto itr = std::stable_partition(waiting_threads.begin(), waiting_threads.end(),
                                     [address](const auto& thread) {
                                         ASSERT_MSG(thread->status == THREADSTATUS_WAIT_ARB,
                                                    "Inconsistent AddressArbiter state");
                                         return thread->wait_address != address;
                                     });

    // Wake up all the found threads
    std::for_each(itr, waiting_threads.end(), [](auto& thread) { thread->ResumeFromWait(); });

    // Remove the woken up threads from the wait list.
    waiting_threads.erase(itr, waiting_threads.end());
}

SharedPtr<Thread> AddressArbiter::ResumeHighestPriorityThread(VAddr address) {
    // Determine which threads are waiting on this address, those should be considered for wakeup.
    auto matches_start = std::stable_partition(
        waiting_threads.begin(), waiting_threads.end(), [address](const auto& thread) {
            ASSERT_MSG(thread->status == THREADSTATUS_WAIT_ARB,
                       "Inconsistent AddressArbiter state");
            return thread->wait_address != address;
        });

    // Iterate through threads, find highest priority thread that is waiting to be arbitrated.
    // Note: The real kernel will pick the first thread in the list if more than one have the
    // same highest priority value. Lower priority values mean higher priority.
    auto itr = std::min_element(matches_start, waiting_threads.end(),
                                [](const auto& lhs, const auto& rhs) {
                                    return lhs->current_priority < rhs->current_priority;
                                });

    if (itr == waiting_threads.end())
        return nullptr;

    auto thread = *itr;
    thread->ResumeFromWait();

    waiting_threads.erase(itr);
    return thread;
}

AddressArbiter::AddressArbiter() {}
AddressArbiter::~AddressArbiter() {}

SharedPtr<AddressArbiter> AddressArbiter::Create(std::string name) {
    SharedPtr<AddressArbiter> address_arbiter(new AddressArbiter);

    address_arbiter->name = std::move(name);

    return address_arbiter;
}

ResultCode AddressArbiter::ArbitrateAddress(SharedPtr<Thread> thread, ArbitrationType type,
                                            VAddr address, s32 value, u64 nanoseconds) {

    auto timeout_callback = [this](ThreadWakeupReason reason, SharedPtr<Thread> thread,
                                   SharedPtr<WaitObject> object) {
        ASSERT(reason == ThreadWakeupReason::Timeout);
        // Remove the newly-awakened thread from the Arbiter's waiting list.
        waiting_threads.erase(std::remove(waiting_threads.begin(), waiting_threads.end(), thread),
                              waiting_threads.end());
    };

    switch (type) {

    // Signal thread(s) waiting for arbitrate address...
    case ArbitrationType::Signal:
        // Negative value means resume all threads
        if (value < 0) {
            ResumeAllThreads(address);
        } else {
            // Resume first N threads
            for (int i = 0; i < value; i++)
                ResumeHighestPriorityThread(address);
        }
        break;

    // Wait current thread (acquire the arbiter)...
    case ArbitrationType::WaitIfLessThan:
        if ((s32)Memory::Read32(address) < value) {
            WaitThread(std::move(thread), address);
        }
        break;
    case ArbitrationType::WaitIfLessThanWithTimeout:
        if ((s32)Memory::Read32(address) < value) {
            thread->wakeup_callback = timeout_callback;
            thread->WakeAfterDelay(nanoseconds);
            WaitThread(std::move(thread), address);
        }
        break;
    case ArbitrationType::DecrementAndWaitIfLessThan: {
        s32 memory_value = Memory::Read32(address);
        if (memory_value < value) {
            // Only change the memory value if the thread should wait
            Memory::Write32(address, (s32)memory_value - 1);
            WaitThread(std::move(thread), address);
        }
        break;
    }
    case ArbitrationType::DecrementAndWaitIfLessThanWithTimeout: {
        s32 memory_value = Memory::Read32(address);
        if (memory_value < value) {
            // Only change the memory value if the thread should wait
            Memory::Write32(address, (s32)memory_value - 1);
            thread->wakeup_callback = timeout_callback;
            thread->WakeAfterDelay(nanoseconds);
            WaitThread(std::move(thread), address);
        }
        break;
    }

    default:
        LOG_ERROR(Kernel, "unknown type={}", static_cast<u32>(type));
        return ERR_INVALID_ENUM_VALUE_FND;
    }

    // The calls that use a timeout seem to always return a Timeout error even if they did not put
    // the thread to sleep
    if (type == ArbitrationType::WaitIfLessThanWithTimeout ||
        type == ArbitrationType::DecrementAndWaitIfLessThanWithTimeout) {

        return RESULT_TIMEOUT;
    }
    return RESULT_SUCCESS;
}

} // namespace Kernel
