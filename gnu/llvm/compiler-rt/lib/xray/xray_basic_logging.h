//===-- xray_basic_logging.h ----------------------------------------------===//
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
#ifndef XRAY_XRAY_INMEMORY_LOG_H
#define XRAY_XRAY_INMEMORY_LOG_H

#include "xray/xray_log_interface.h"

/// Basic (Naive) Mode
/// ==================
///
/// This implementation hooks in through the XRay logging implementation
/// framework. The Basic Mode implementation will keep appending to a file as
/// soon as the thread-local buffers are full. It keeps minimal in-memory state
/// and does the minimum filtering required to keep log files smaller.

namespace __xray {

XRayLogInitStatus basicLoggingInit(size_t BufferSize, size_t BufferMax,
                                   void *Options, size_t OptionsSize);
XRayLogInitStatus basicLoggingFinalize();

void basicLoggingHandleArg0RealTSC(int32_t FuncId, XRayEntryType Entry);
void basicLoggingHandleArg0EmulateTSC(int32_t FuncId, XRayEntryType Entry);
void basicLoggingHandleArg1RealTSC(int32_t FuncId, XRayEntryType Entry,
                                   uint64_t Arg1);
void basicLoggingHandleArg1EmulateTSC(int32_t FuncId, XRayEntryType Entry,
                                      uint64_t Arg1);
XRayLogFlushStatus basicLoggingFlush();
XRayLogInitStatus basicLoggingReset();

} // namespace __xray

#endif // XRAY_XRAY_INMEMORY_LOG_H
