//===-- xray_log_interface.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a function call tracing system.
//
//===----------------------------------------------------------------------===//
#include "xray/xray_log_interface.h"

#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_mutex.h"
#include "xray/xray_interface.h"
#include "xray_defs.h"

namespace __xray {
static SpinMutex XRayImplMutex;
static XRayLogImpl CurrentXRayImpl{nullptr, nullptr, nullptr, nullptr};
static XRayLogImpl *GlobalXRayImpl = nullptr;

// This is the default implementation of a buffer iterator, which always yields
// a null buffer.
XRayBuffer NullBufferIterator(XRayBuffer) XRAY_NEVER_INSTRUMENT {
  return {nullptr, 0};
}

// This is the global function responsible for iterating through given buffers.
atomic_uintptr_t XRayBufferIterator{
    reinterpret_cast<uintptr_t>(&NullBufferIterator)};

// We use a linked list of Mode to XRayLogImpl mappings. This is a linked list
// when it should be a map because we're avoiding having to depend on C++
// standard library data structures at this level of the implementation.
struct ModeImpl {
  ModeImpl *Next;
  const char *Mode;
  XRayLogImpl Impl;
};

static ModeImpl SentinelModeImpl{
    nullptr, nullptr, {nullptr, nullptr, nullptr, nullptr}};
static ModeImpl *ModeImpls = &SentinelModeImpl;
static const ModeImpl *CurrentMode = nullptr;

} // namespace __xray

using namespace __xray;

void __xray_log_set_buffer_iterator(XRayBuffer (*Iterator)(XRayBuffer))
    XRAY_NEVER_INSTRUMENT {
  atomic_store(&__xray::XRayBufferIterator,
               reinterpret_cast<uintptr_t>(Iterator), memory_order_release);
}

void __xray_log_remove_buffer_iterator() XRAY_NEVER_INSTRUMENT {
  __xray_log_set_buffer_iterator(&NullBufferIterator);
}

XRayLogRegisterStatus
__xray_log_register_mode(const char *Mode,
                         XRayLogImpl Impl) XRAY_NEVER_INSTRUMENT {
  if (Impl.flush_log == nullptr || Impl.handle_arg0 == nullptr ||
      Impl.log_finalize == nullptr || Impl.log_init == nullptr)
    return XRayLogRegisterStatus::XRAY_INCOMPLETE_IMPL;

  SpinMutexLock Guard(&XRayImplMutex);
  // First, look for whether the mode already has a registered implementation.
  for (ModeImpl *it = ModeImpls; it != &SentinelModeImpl; it = it->Next) {
    if (!internal_strcmp(Mode, it->Mode))
      return XRayLogRegisterStatus::XRAY_DUPLICATE_MODE;
  }
  auto *NewModeImpl = static_cast<ModeImpl *>(InternalAlloc(sizeof(ModeImpl)));
  NewModeImpl->Next = ModeImpls;
  NewModeImpl->Mode = internal_strdup(Mode);
  NewModeImpl->Impl = Impl;
  ModeImpls = NewModeImpl;
  return XRayLogRegisterStatus::XRAY_REGISTRATION_OK;
}

XRayLogRegisterStatus
__xray_log_select_mode(const char *Mode) XRAY_NEVER_INSTRUMENT {
  SpinMutexLock Guard(&XRayImplMutex);
  for (ModeImpl *it = ModeImpls; it != &SentinelModeImpl; it = it->Next) {
    if (!internal_strcmp(Mode, it->Mode)) {
      CurrentMode = it;
      CurrentXRayImpl = it->Impl;
      GlobalXRayImpl = &CurrentXRayImpl;
      __xray_set_handler(it->Impl.handle_arg0);
      return XRayLogRegisterStatus::XRAY_REGISTRATION_OK;
    }
  }
  return XRayLogRegisterStatus::XRAY_MODE_NOT_FOUND;
}

const char *__xray_log_get_current_mode() XRAY_NEVER_INSTRUMENT {
  SpinMutexLock Guard(&XRayImplMutex);
  if (CurrentMode != nullptr)
    return CurrentMode->Mode;
  return nullptr;
}

void __xray_set_log_impl(XRayLogImpl Impl) XRAY_NEVER_INSTRUMENT {
  if (Impl.log_init == nullptr || Impl.log_finalize == nullptr ||
      Impl.handle_arg0 == nullptr || Impl.flush_log == nullptr) {
    SpinMutexLock Guard(&XRayImplMutex);
    GlobalXRayImpl = nullptr;
    CurrentMode = nullptr;
    __xray_remove_handler();
    __xray_remove_handler_arg1();
    return;
  }

  SpinMutexLock Guard(&XRayImplMutex);
  CurrentXRayImpl = Impl;
  GlobalXRayImpl = &CurrentXRayImpl;
  __xray_set_handler(Impl.handle_arg0);
}

void __xray_remove_log_impl() XRAY_NEVER_INSTRUMENT {
  SpinMutexLock Guard(&XRayImplMutex);
  GlobalXRayImpl = nullptr;
  __xray_remove_handler();
  __xray_remove_handler_arg1();
}

XRayLogInitStatus __xray_log_init(size_t BufferSize, size_t MaxBuffers,
                                  void *Args,
                                  size_t ArgsSize) XRAY_NEVER_INSTRUMENT {
  SpinMutexLock Guard(&XRayImplMutex);
  if (!GlobalXRayImpl)
    return XRayLogInitStatus::XRAY_LOG_UNINITIALIZED;
  return GlobalXRayImpl->log_init(BufferSize, MaxBuffers, Args, ArgsSize);
}

XRayLogInitStatus __xray_log_init_mode(const char *Mode, const char *Config)
    XRAY_NEVER_INSTRUMENT {
  SpinMutexLock Guard(&XRayImplMutex);
  if (!GlobalXRayImpl)
    return XRayLogInitStatus::XRAY_LOG_UNINITIALIZED;

  if (Config == nullptr)
    return XRayLogInitStatus::XRAY_LOG_UNINITIALIZED;

  // Check first whether the current mode is the same as what we expect.
  if (CurrentMode == nullptr || internal_strcmp(CurrentMode->Mode, Mode) != 0)
    return XRayLogInitStatus::XRAY_LOG_UNINITIALIZED;

  // Here we do some work to coerce the pointer we're provided, so that
  // the implementations that still take void* pointers can handle the
  // data provided in the Config argument.
  return GlobalXRayImpl->log_init(
      0, 0, const_cast<void *>(static_cast<const void *>(Config)), 0);
}

XRayLogInitStatus
__xray_log_init_mode_bin(const char *Mode, const char *Config,
                         size_t ConfigSize) XRAY_NEVER_INSTRUMENT {
  SpinMutexLock Guard(&XRayImplMutex);
  if (!GlobalXRayImpl)
    return XRayLogInitStatus::XRAY_LOG_UNINITIALIZED;

  if (Config == nullptr)
    return XRayLogInitStatus::XRAY_LOG_UNINITIALIZED;

  // Check first whether the current mode is the same as what we expect.
  if (CurrentMode == nullptr || internal_strcmp(CurrentMode->Mode, Mode) != 0)
    return XRayLogInitStatus::XRAY_LOG_UNINITIALIZED;

  // Here we do some work to coerce the pointer we're provided, so that
  // the implementations that still take void* pointers can handle the
  // data provided in the Config argument.
  return GlobalXRayImpl->log_init(
      0, 0, const_cast<void *>(static_cast<const void *>(Config)), ConfigSize);
}

XRayLogInitStatus __xray_log_finalize() XRAY_NEVER_INSTRUMENT {
  SpinMutexLock Guard(&XRayImplMutex);
  if (!GlobalXRayImpl)
    return XRayLogInitStatus::XRAY_LOG_UNINITIALIZED;
  return GlobalXRayImpl->log_finalize();
}

XRayLogFlushStatus __xray_log_flushLog() XRAY_NEVER_INSTRUMENT {
  SpinMutexLock Guard(&XRayImplMutex);
  if (!GlobalXRayImpl)
    return XRayLogFlushStatus::XRAY_LOG_NOT_FLUSHING;
  return GlobalXRayImpl->flush_log();
}

XRayLogFlushStatus __xray_log_process_buffers(
    void (*Processor)(const char *, XRayBuffer)) XRAY_NEVER_INSTRUMENT {
  // We want to make sure that there will be no changes to the global state for
  // the log by synchronising on the XRayBufferIteratorMutex.
  if (!GlobalXRayImpl)
    return XRayLogFlushStatus::XRAY_LOG_NOT_FLUSHING;
  auto Iterator = reinterpret_cast<XRayBuffer (*)(XRayBuffer)>(
      atomic_load(&XRayBufferIterator, memory_order_acquire));
  auto Buffer = (*Iterator)(XRayBuffer{nullptr, 0});
  auto Mode = CurrentMode ? CurrentMode->Mode : nullptr;
  while (Buffer.Data != nullptr) {
    (*Processor)(Mode, Buffer);
    Buffer = (*Iterator)(Buffer);
  }
  return XRayLogFlushStatus::XRAY_LOG_FLUSHED;
}
