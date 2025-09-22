//===-- xray_profile_collector.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// This file defines the interface for a data collection service, for XRay
// profiling. What we implement here is an in-process service where
// FunctionCallTrie instances can be handed off by threads, to be
// consolidated/collected.
//
//===----------------------------------------------------------------------===//
#ifndef XRAY_XRAY_PROFILE_COLLECTOR_H
#define XRAY_XRAY_PROFILE_COLLECTOR_H

#include "xray_function_call_trie.h"

#include "xray/xray_log_interface.h"

namespace __xray {

/// The ProfileCollectorService implements a centralised mechanism for
/// collecting FunctionCallTrie instances, indexed by thread ID. On demand, the
/// ProfileCollectorService can be queried for the most recent state of the
/// data, in a form that allows traversal.
namespace profileCollectorService {

/// Posts the FunctionCallTrie associated with a specific Thread ID. This
/// will:
///
/// Moves the collection of FunctionCallTrie, Allocators, and Buffers associated
/// with a thread's data to the queue. This takes ownership of the memory
/// associated with a thread, and manages those exclusively.
///
void post(BufferQueue *Q, FunctionCallTrie &&T,
          FunctionCallTrie::Allocators &&A,
          FunctionCallTrie::Allocators::Buffers &&B, tid_t TId);

/// The serialize will process all FunctionCallTrie instances in memory, and
/// turn those into specifically formatted blocks, each describing the
/// function call trie's contents in a compact form. In memory, this looks
/// like the following layout:
///
///   - block size (32 bits)
///   - block number (32 bits)
///   - thread id (64 bits)
///   - list of records:
///     - function ids in leaf to root order, terminated by
///       0 (32 bits per function id)
///     - call count (64 bit)
///     - cumulative local time (64 bit)
///     - record delimiter (64 bit, 0x0)
///
void serialize();

/// The reset function will clear out any internal memory held by the
/// service. The intent is to have the resetting be done in calls to the
/// initialization routine, or explicitly through the flush log API.
void reset();

/// This nextBuffer function is meant to implement the iterator functionality,
/// provided in the XRay API.
XRayBuffer nextBuffer(XRayBuffer B);

} // namespace profileCollectorService

} // namespace __xray

#endif // XRAY_XRAY_PROFILE_COLLECTOR_H
