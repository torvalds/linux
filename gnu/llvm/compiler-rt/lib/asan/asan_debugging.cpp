//===-- asan_debugging.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// This file contains various functions that are generally useful to call when
// using a debugger (LLDB, GDB).
//===----------------------------------------------------------------------===//

#include "asan_allocator.h"
#include "asan_descriptions.h"
#include "asan_flags.h"
#include "asan_internal.h"
#include "asan_mapping.h"
#include "asan_report.h"
#include "asan_thread.h"
#include "sanitizer_common/sanitizer_stackdepot.h"

namespace {
using namespace __asan;

static void FindInfoForStackVar(uptr addr, const char *frame_descr, uptr offset,
                                char *name, uptr name_size,
                                uptr *region_address, uptr *region_size) {
  InternalMmapVector<StackVarDescr> vars;
  vars.reserve(16);
  if (!ParseFrameDescription(frame_descr, &vars)) {
    return;
  }

  for (uptr i = 0; i < vars.size(); i++) {
    if (offset <= vars[i].beg + vars[i].size) {
      // We use name_len + 1 because strlcpy will guarantee a \0 at the end, so
      // if we're limiting the copy due to name_len, we add 1 to ensure we copy
      // the whole name and then terminate with '\0'.
      internal_strlcpy(name, vars[i].name_pos,
                       Min(name_size, vars[i].name_len + 1));
      *region_address = addr - (offset - vars[i].beg);
      *region_size = vars[i].size;
      return;
    }
  }
}

uptr AsanGetStack(uptr addr, uptr *trace, u32 size, u32 *thread_id,
                         bool alloc_stack) {
  AsanChunkView chunk = FindHeapChunkByAddress(addr);
  if (!chunk.IsValid()) return 0;

  StackTrace stack(nullptr, 0);
  if (alloc_stack) {
    if (chunk.AllocTid() == kInvalidTid) return 0;
    stack = StackDepotGet(chunk.GetAllocStackId());
    if (thread_id) *thread_id = chunk.AllocTid();
  } else {
    if (chunk.FreeTid() == kInvalidTid) return 0;
    stack = StackDepotGet(chunk.GetFreeStackId());
    if (thread_id) *thread_id = chunk.FreeTid();
  }

  if (trace && size) {
    size = Min(size, Min(stack.size, kStackTraceMax));
    for (uptr i = 0; i < size; i++)
      trace[i] = StackTrace::GetPreviousInstructionPc(stack.trace[i]);

    return size;
  }

  return 0;
}

}  // namespace

SANITIZER_INTERFACE_ATTRIBUTE
const char *__asan_locate_address(uptr addr, char *name, uptr name_size,
                                  uptr *region_address_ptr,
                                  uptr *region_size_ptr) {
  AddressDescription descr(addr);
  uptr region_address = 0;
  uptr region_size = 0;
  const char *region_kind = nullptr;
  if (name && name_size > 0) name[0] = 0;

  if (auto shadow = descr.AsShadow()) {
    // region_{address,size} are already 0
    switch (shadow->kind) {
      case kShadowKindLow:
        region_kind = "low shadow";
        break;
      case kShadowKindGap:
        region_kind = "shadow gap";
        break;
      case kShadowKindHigh:
        region_kind = "high shadow";
        break;
    }
  } else if (auto heap = descr.AsHeap()) {
    region_kind = "heap";
    region_address = heap->chunk_access.chunk_begin;
    region_size = heap->chunk_access.chunk_size;
  } else if (auto stack = descr.AsStack()) {
    region_kind = "stack";
    if (!stack->frame_descr) {
      // region_{address,size} are already 0
    } else {
      FindInfoForStackVar(addr, stack->frame_descr, stack->offset, name,
                          name_size, &region_address, &region_size);
    }
  } else if (auto global = descr.AsGlobal()) {
    region_kind = "global";
    auto &g = global->globals[0];
    internal_strlcpy(name, g.name, name_size);
    region_address = g.beg;
    region_size = g.size;
  } else {
    // region_{address,size} are already 0
    region_kind = "heap-invalid";
  }

  CHECK(region_kind);
  if (region_address_ptr) *region_address_ptr = region_address;
  if (region_size_ptr) *region_size_ptr = region_size;
  return region_kind;
}

SANITIZER_INTERFACE_ATTRIBUTE
uptr __asan_get_alloc_stack(uptr addr, uptr *trace, uptr size, u32 *thread_id) {
  return AsanGetStack(addr, trace, size, thread_id, /* alloc_stack */ true);
}

SANITIZER_INTERFACE_ATTRIBUTE
uptr __asan_get_free_stack(uptr addr, uptr *trace, uptr size, u32 *thread_id) {
  return AsanGetStack(addr, trace, size, thread_id, /* alloc_stack */ false);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __asan_get_shadow_mapping(uptr *shadow_scale, uptr *shadow_offset) {
  if (shadow_scale)
    *shadow_scale = ASAN_SHADOW_SCALE;
  if (shadow_offset)
    *shadow_offset = ASAN_SHADOW_OFFSET;
}
