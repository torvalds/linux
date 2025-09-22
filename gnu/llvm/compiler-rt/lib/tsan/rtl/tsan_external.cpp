//===-- tsan_external.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#include "tsan_rtl.h"
#include "sanitizer_common/sanitizer_ptrauth.h"

#if !SANITIZER_GO
#  include "tsan_interceptors.h"
#endif

namespace __tsan {

#define CALLERPC ((uptr)__builtin_return_address(0))

struct TagData {
  const char *object_type;
  const char *header;
};

static TagData registered_tags[kExternalTagMax] = {
  {},
  {"Swift variable", "Swift access race"},
};
static atomic_uint32_t used_tags{kExternalTagFirstUserAvailable};
static TagData *GetTagData(uptr tag) {
  // Invalid/corrupted tag?  Better return NULL and let the caller deal with it.
  if (tag >= atomic_load(&used_tags, memory_order_relaxed)) return nullptr;
  return &registered_tags[tag];
}

const char *GetObjectTypeFromTag(uptr tag) {
  TagData *tag_data = GetTagData(tag);
  return tag_data ? tag_data->object_type : nullptr;
}

const char *GetReportHeaderFromTag(uptr tag) {
  TagData *tag_data = GetTagData(tag);
  return tag_data ? tag_data->header : nullptr;
}

uptr TagFromShadowStackFrame(uptr pc) {
  uptr tag_count = atomic_load(&used_tags, memory_order_relaxed);
  void *pc_ptr = (void *)pc;
  if (pc_ptr < GetTagData(0) || pc_ptr > GetTagData(tag_count - 1))
    return 0;
  return (TagData *)pc_ptr - GetTagData(0);
}

#if !SANITIZER_GO

// We need to track tags for individual memory accesses, but there is no space
// in the shadow cells for them.  Instead we push/pop them onto the thread
// traces and ignore the extra tag frames when printing reports.
static void PushTag(ThreadState *thr, uptr tag) {
  FuncEntry(thr, (uptr)&registered_tags[tag]);
}
static void PopTag(ThreadState *thr) { FuncExit(thr); }

static void ExternalAccess(void *addr, uptr caller_pc, uptr tsan_caller_pc,
                           void *tag, AccessType typ) {
  CHECK_LT(tag, atomic_load(&used_tags, memory_order_relaxed));
  bool in_ignored_lib;
  if (caller_pc && libignore()->IsIgnored(caller_pc, &in_ignored_lib))
    return;

  ThreadState *thr = cur_thread();
  if (caller_pc) FuncEntry(thr, caller_pc);
  PushTag(thr, (uptr)tag);
  MemoryAccess(thr, tsan_caller_pc, (uptr)addr, 1, typ);
  PopTag(thr);
  if (caller_pc) FuncExit(thr);
}

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void *__tsan_external_register_tag(const char *object_type) {
  uptr new_tag = atomic_fetch_add(&used_tags, 1, memory_order_relaxed);
  CHECK_LT(new_tag, kExternalTagMax);
  GetTagData(new_tag)->object_type = internal_strdup(object_type);
  char header[127] = {0};
  internal_snprintf(header, sizeof(header), "race on %s", object_type);
  GetTagData(new_tag)->header = internal_strdup(header);
  return (void *)new_tag;
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_external_register_header(void *tag, const char *header) {
  CHECK_GE((uptr)tag, kExternalTagFirstUserAvailable);
  CHECK_LT((uptr)tag, kExternalTagMax);
  atomic_uintptr_t *header_ptr =
      (atomic_uintptr_t *)&GetTagData((uptr)tag)->header;
  header = internal_strdup(header);
  char *old_header =
      (char *)atomic_exchange(header_ptr, (uptr)header, memory_order_seq_cst);
  Free(old_header);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_external_assign_tag(void *addr, void *tag) {
  CHECK_LT(tag, atomic_load(&used_tags, memory_order_relaxed));
  Allocator *a = allocator();
  MBlock *b = nullptr;
  if (a->PointerIsMine((void *)addr)) {
    void *block_begin = a->GetBlockBegin((void *)addr);
    if (block_begin) b = ctx->metamap.GetBlock((uptr)block_begin);
  }
  if (b) {
    b->tag = (uptr)tag;
  }
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_external_read(void *addr, void *caller_pc, void *tag) {
  ExternalAccess(addr, STRIP_PAC_PC(caller_pc), CALLERPC, tag, kAccessRead);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __tsan_external_write(void *addr, void *caller_pc, void *tag) {
  ExternalAccess(addr, STRIP_PAC_PC(caller_pc), CALLERPC, tag, kAccessWrite);
}
}  // extern "C"

#endif  // !SANITIZER_GO

}  // namespace __tsan
