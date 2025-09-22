//===--- CtxInstrContextNode.h - Contextual Profile Node --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//==============================================================================
//
// NOTE!
// llvm/lib/ProfileData/CtxInstrContextNode.h and
//   compiler-rt/lib/ctx_profile/CtxInstrContextNode.h
// must be exact copies of eachother
//
// compiler-rt creates these objects as part of the instrumentation runtime for
// contextual profiling. LLVM only consumes them to convert a contextual tree
// to a bitstream.
//
//==============================================================================

/// The contextual profile is a directed tree where each node has one parent. A
/// node (ContextNode) corresponds to a function activation. The root of the
/// tree is at a function that was marked as entrypoint to the compiler. A node
/// stores counter values for edges and a vector of subcontexts. These are the
/// contexts of callees. The index in the subcontext vector corresponds to the
/// index of the callsite (as was instrumented via llvm.instrprof.callsite). At
/// that index we find a linked list, potentially empty, of ContextNodes. Direct
/// calls will have 0 or 1 values in the linked list, but indirect callsites may
/// have more.
///
/// The ContextNode has a fixed sized header describing it - the GUID of the
/// function, the size of the counter and callsite vectors. It is also an
/// (intrusive) linked list for the purposes of the indirect call case above.
///
/// Allocation is expected to happen on an Arena. The allocation lays out inline
/// the counter and subcontexts vectors. The class offers APIs to correctly
/// reference the latter.
///
/// The layout is as follows:
///
/// [[declared fields][counters vector][vector of ptrs to subcontexts]]
///
/// See also documentation on the counters and subContexts members below.
///
/// The structure of the ContextNode is known to LLVM, because LLVM needs to:
///   (1) increment counts, and
///   (2) form a GEP for the position in the subcontext list of a callsite
/// This means changes to LLVM contextual profile lowering and changes here
/// must be coupled.
/// Note: the header content isn't interesting to LLVM (other than its size)
///
/// Part of contextual collection is the notion of "scratch contexts". These are
/// buffers that are "large enough" to allow for memory-safe acceses during
/// counter increments - meaning the counter increment code in LLVM doesn't need
/// to be concerned with memory safety. Their subcontexts never get populated,
/// though. The runtime code here produces and recognizes them.

#ifndef LLVM_PROFILEDATA_CTXINSTRCONTEXTNODE_H
#define LLVM_PROFILEDATA_CTXINSTRCONTEXTNODE_H

#include <stdint.h>
#include <stdlib.h>

namespace llvm {
namespace ctx_profile {
using GUID = uint64_t;

class ContextNode final {
  const GUID Guid;
  ContextNode *const Next;
  const uint32_t NrCounters;
  const uint32_t NrCallsites;

public:
  ContextNode(GUID Guid, uint32_t NrCounters, uint32_t NrCallsites,
              ContextNode *Next = nullptr)
      : Guid(Guid), Next(Next), NrCounters(NrCounters),
        NrCallsites(NrCallsites) {}

  static inline size_t getAllocSize(uint32_t NrCounters, uint32_t NrCallsites) {
    return sizeof(ContextNode) + sizeof(uint64_t) * NrCounters +
           sizeof(ContextNode *) * NrCallsites;
  }

  // The counters vector starts right after the static header.
  uint64_t *counters() {
    ContextNode *addr_after = &(this[1]);
    return reinterpret_cast<uint64_t *>(addr_after);
  }

  uint32_t counters_size() const { return NrCounters; }
  uint32_t callsites_size() const { return NrCallsites; }

  const uint64_t *counters() const {
    return const_cast<ContextNode *>(this)->counters();
  }

  // The subcontexts vector starts right after the end of the counters vector.
  ContextNode **subContexts() {
    return reinterpret_cast<ContextNode **>(&(counters()[NrCounters]));
  }

  ContextNode *const *subContexts() const {
    return const_cast<ContextNode *>(this)->subContexts();
  }

  GUID guid() const { return Guid; }
  ContextNode *next() const { return Next; }

  size_t size() const { return getAllocSize(NrCounters, NrCallsites); }

  uint64_t entrycount() const { return counters()[0]; }
};
} // namespace ctx_profile
} // namespace llvm
#endif