//===-- xray_fdr_logging.h ------------------------------------------------===//
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
#ifndef XRAY_XRAY_FDR_LOGGING_H
#define XRAY_XRAY_FDR_LOGGING_H

#include "xray/xray_log_interface.h"
#include "xray_fdr_log_records.h"

// FDR (Flight Data Recorder) Mode
// ===============================
//
// The XRay whitepaper describes a mode of operation for function call trace
// logging that involves writing small records into an in-memory circular
// buffer, that then gets logged to disk on demand. To do this efficiently and
// capture as much data as we can, we use smaller records compared to the
// default mode of always writing fixed-size records.

namespace __xray {
XRayLogInitStatus fdrLoggingInit(size_t BufferSize, size_t BufferMax,
                                 void *Options, size_t OptionsSize);
XRayLogInitStatus fdrLoggingFinalize();
void fdrLoggingHandleArg0(int32_t FuncId, XRayEntryType Entry);
void fdrLoggingHandleArg1(int32_t FuncId, XRayEntryType Entry, uint64_t Arg1);
XRayLogFlushStatus fdrLoggingFlush();
XRayLogInitStatus fdrLoggingReset();

} // namespace __xray

#endif // XRAY_XRAY_FDR_LOGGING_H
