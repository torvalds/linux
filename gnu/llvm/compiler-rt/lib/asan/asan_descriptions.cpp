//===-- asan_descriptions.cpp -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// ASan functions for getting information about an address and/or printing it.
//===----------------------------------------------------------------------===//

#include "asan_descriptions.h"
#include "asan_mapping.h"
#include "asan_report.h"
#include "asan_stack.h"
#include "sanitizer_common/sanitizer_stackdepot.h"

namespace __asan {

AsanThreadIdAndName::AsanThreadIdAndName(AsanThreadContext *t) {
  Init(t->tid, t->name);
}

AsanThreadIdAndName::AsanThreadIdAndName(u32 tid) {
  if (tid == kInvalidTid) {
    Init(tid, "");
  } else {
    asanThreadRegistry().CheckLocked();
    AsanThreadContext *t = GetThreadContextByTidLocked(tid);
    Init(tid, t->name);
  }
}

void AsanThreadIdAndName::Init(u32 tid, const char *tname) {
  int len = internal_snprintf(name, sizeof(name), "T%d", tid);
  CHECK(((unsigned int)len) < sizeof(name));
  if (tname[0] != '\0')
    internal_snprintf(&name[len], sizeof(name) - len, " (%s)", tname);
}

void DescribeThread(AsanThreadContext *context) {
  CHECK(context);
  asanThreadRegistry().CheckLocked();
  // No need to announce the main thread.
  if (context->tid == kMainTid || context->announced) {
    return;
  }
  context->announced = true;
  InternalScopedString str;
  str.AppendF("Thread %s", AsanThreadIdAndName(context).c_str());
  if (context->parent_tid == kInvalidTid) {
    str.Append(" created by unknown thread\n");
    Printf("%s", str.data());
    return;
  }
  str.AppendF(" created by %s here:\n",
              AsanThreadIdAndName(context->parent_tid).c_str());
  Printf("%s", str.data());
  StackDepotGet(context->stack_id).Print();
  // Recursively described parent thread if needed.
  if (flags()->print_full_thread_history) {
    AsanThreadContext *parent_context =
        GetThreadContextByTidLocked(context->parent_tid);
    DescribeThread(parent_context);
  }
}

// Shadow descriptions
static bool GetShadowKind(uptr addr, ShadowKind *shadow_kind) {
  CHECK(!AddrIsInMem(addr));
  if (AddrIsInShadowGap(addr)) {
    *shadow_kind = kShadowKindGap;
  } else if (AddrIsInHighShadow(addr)) {
    *shadow_kind = kShadowKindHigh;
  } else if (AddrIsInLowShadow(addr)) {
    *shadow_kind = kShadowKindLow;
  } else {
    return false;
  }
  return true;
}

bool DescribeAddressIfShadow(uptr addr) {
  ShadowAddressDescription descr;
  if (!GetShadowAddressInformation(addr, &descr)) return false;
  descr.Print();
  return true;
}

bool GetShadowAddressInformation(uptr addr, ShadowAddressDescription *descr) {
  if (AddrIsInMem(addr)) return false;
  ShadowKind shadow_kind;
  if (!GetShadowKind(addr, &shadow_kind)) return false;
  if (shadow_kind != kShadowKindGap) descr->shadow_byte = *(u8 *)addr;
  descr->addr = addr;
  descr->kind = shadow_kind;
  return true;
}

// Heap descriptions
static void GetAccessToHeapChunkInformation(ChunkAccess *descr,
                                            AsanChunkView chunk, uptr addr,
                                            uptr access_size) {
  descr->bad_addr = addr;
  if (chunk.AddrIsAtLeft(addr, access_size, &descr->offset)) {
    descr->access_type = kAccessTypeLeft;
  } else if (chunk.AddrIsAtRight(addr, access_size, &descr->offset)) {
    descr->access_type = kAccessTypeRight;
    if (descr->offset < 0) {
      descr->bad_addr -= descr->offset;
      descr->offset = 0;
    }
  } else if (chunk.AddrIsInside(addr, access_size, &descr->offset)) {
    descr->access_type = kAccessTypeInside;
  } else {
    descr->access_type = kAccessTypeUnknown;
  }
  descr->chunk_begin = chunk.Beg();
  descr->chunk_size = chunk.UsedSize();
  descr->user_requested_alignment = chunk.UserRequestedAlignment();
  descr->alloc_type = chunk.GetAllocType();
}

static void PrintHeapChunkAccess(uptr addr, const ChunkAccess &descr) {
  Decorator d;
  InternalScopedString str;
  str.Append(d.Location());
  switch (descr.access_type) {
    case kAccessTypeLeft:
      str.AppendF("%p is located %zd bytes before", (void *)descr.bad_addr,
                  descr.offset);
      break;
    case kAccessTypeRight:
      str.AppendF("%p is located %zd bytes after", (void *)descr.bad_addr,
                  descr.offset);
      break;
    case kAccessTypeInside:
      str.AppendF("%p is located %zd bytes inside of", (void *)descr.bad_addr,
                  descr.offset);
      break;
    case kAccessTypeUnknown:
      str.AppendF(
          "%p is located somewhere around (this is AddressSanitizer bug!)",
          (void *)descr.bad_addr);
  }
  str.AppendF(" %zu-byte region [%p,%p)\n", descr.chunk_size,
              (void *)descr.chunk_begin,
              (void *)(descr.chunk_begin + descr.chunk_size));
  str.Append(d.Default());
  Printf("%s", str.data());
}

bool GetHeapAddressInformation(uptr addr, uptr access_size,
                               HeapAddressDescription *descr) {
  AsanChunkView chunk = FindHeapChunkByAddress(addr);
  if (!chunk.IsValid()) {
    return false;
  }
  descr->addr = addr;
  GetAccessToHeapChunkInformation(&descr->chunk_access, chunk, addr,
                                  access_size);
  CHECK_NE(chunk.AllocTid(), kInvalidTid);
  descr->alloc_tid = chunk.AllocTid();
  descr->alloc_stack_id = chunk.GetAllocStackId();
  descr->free_tid = chunk.FreeTid();
  if (descr->free_tid != kInvalidTid)
    descr->free_stack_id = chunk.GetFreeStackId();
  return true;
}

static StackTrace GetStackTraceFromId(u32 id) {
  CHECK(id);
  StackTrace res = StackDepotGet(id);
  CHECK(res.trace);
  return res;
}

bool DescribeAddressIfHeap(uptr addr, uptr access_size) {
  HeapAddressDescription descr;
  if (!GetHeapAddressInformation(addr, access_size, &descr)) {
    Printf(
        "AddressSanitizer can not describe address in more detail "
        "(wild memory access suspected).\n");
    return false;
  }
  descr.Print();
  return true;
}

// Stack descriptions
bool GetStackAddressInformation(uptr addr, uptr access_size,
                                StackAddressDescription *descr) {
  AsanThread *t = FindThreadByStackAddress(addr);
  if (!t) return false;

  descr->addr = addr;
  descr->tid = t->tid();
  // Try to fetch precise stack frame for this access.
  AsanThread::StackFrameAccess access;
  if (!t->GetStackFrameAccessByAddr(addr, &access)) {
    descr->frame_descr = nullptr;
    return true;
  }

  descr->offset = access.offset;
  descr->access_size = access_size;
  descr->frame_pc = access.frame_pc;
  descr->frame_descr = access.frame_descr;

#if SANITIZER_PPC64V1
  // On PowerPC64 ELFv1, the address of a function actually points to a
  // three-doubleword data structure with the first field containing
  // the address of the function's code.
  descr->frame_pc = *reinterpret_cast<uptr *>(descr->frame_pc);
#endif
  descr->frame_pc += 16;

  return true;
}

static void PrintAccessAndVarIntersection(const StackVarDescr &var, uptr addr,
                                          uptr access_size, uptr prev_var_end,
                                          uptr next_var_beg) {
  uptr var_end = var.beg + var.size;
  uptr addr_end = addr + access_size;
  const char *pos_descr = nullptr;
  // If the variable [var.beg, var_end) is the nearest variable to the
  // current memory access, indicate it in the log.
  if (addr >= var.beg) {
    if (addr_end <= var_end)
      pos_descr = "is inside";  // May happen if this is a use-after-return.
    else if (addr < var_end)
      pos_descr = "partially overflows";
    else if (addr_end <= next_var_beg &&
             next_var_beg - addr_end >= addr - var_end)
      pos_descr = "overflows";
  } else {
    if (addr_end > var.beg)
      pos_descr = "partially underflows";
    else if (addr >= prev_var_end && addr - prev_var_end >= var.beg - addr_end)
      pos_descr = "underflows";
  }
  InternalScopedString str;
  str.AppendF("    [%zd, %zd)", var.beg, var_end);
  // Render variable name.
  str.Append(" '");
  for (uptr i = 0; i < var.name_len; ++i) {
    str.AppendF("%c", var.name_pos[i]);
  }
  str.Append("'");
  if (var.line > 0) {
    str.AppendF(" (line %zd)", var.line);
  }
  if (pos_descr) {
    Decorator d;
    // FIXME: we may want to also print the size of the access here,
    // but in case of accesses generated by memset it may be confusing.
    str.AppendF("%s <== Memory access at offset %zd %s this variable%s\n",
                d.Location(), addr, pos_descr, d.Default());
  } else {
    str.Append("\n");
  }
  Printf("%s", str.data());
}

bool DescribeAddressIfStack(uptr addr, uptr access_size) {
  StackAddressDescription descr;
  if (!GetStackAddressInformation(addr, access_size, &descr)) return false;
  descr.Print();
  return true;
}

// Global descriptions
static void DescribeAddressRelativeToGlobal(uptr addr, uptr access_size,
                                            const __asan_global &g) {
  InternalScopedString str;
  Decorator d;
  str.Append(d.Location());
  if (addr < g.beg) {
    str.AppendF("%p is located %zd bytes before", (void *)addr, g.beg - addr);
  } else if (addr + access_size > g.beg + g.size) {
    if (addr < g.beg + g.size) addr = g.beg + g.size;
    str.AppendF("%p is located %zd bytes after", (void *)addr,
                addr - (g.beg + g.size));
  } else {
    // Can it happen?
    str.AppendF("%p is located %zd bytes inside of", (void *)addr,
                addr - g.beg);
  }
  str.AppendF(" global variable '%s' defined in '",
              MaybeDemangleGlobalName(g.name));
  PrintGlobalLocation(&str, g, /*print_module_name=*/false);
  str.AppendF("' (%p) of size %zu\n", (void *)g.beg, g.size);
  str.Append(d.Default());
  PrintGlobalNameIfASCII(&str, g);
  Printf("%s", str.data());
}

bool GetGlobalAddressInformation(uptr addr, uptr access_size,
                                 GlobalAddressDescription *descr) {
  descr->addr = addr;
  int globals_num = GetGlobalsForAddress(addr, descr->globals, descr->reg_sites,
                                         ARRAY_SIZE(descr->globals));
  descr->size = globals_num;
  descr->access_size = access_size;
  return globals_num != 0;
}

bool DescribeAddressIfGlobal(uptr addr, uptr access_size,
                             const char *bug_type) {
  GlobalAddressDescription descr;
  if (!GetGlobalAddressInformation(addr, access_size, &descr)) return false;

  descr.Print(bug_type);
  return true;
}

void ShadowAddressDescription::Print() const {
  Printf("Address %p is located in the %s area.\n", (void *)addr,
         ShadowNames[kind]);
}

void GlobalAddressDescription::Print(const char *bug_type) const {
  for (int i = 0; i < size; i++) {
    DescribeAddressRelativeToGlobal(addr, access_size, globals[i]);
    if (bug_type &&
        0 == internal_strcmp(bug_type, "initialization-order-fiasco") &&
        reg_sites[i]) {
      Printf("  registered at:\n");
      StackDepotGet(reg_sites[i]).Print();
    }
  }
}

bool GlobalAddressDescription::PointsInsideTheSameVariable(
    const GlobalAddressDescription &other) const {
  if (size == 0 || other.size == 0) return false;

  for (uptr i = 0; i < size; i++) {
    const __asan_global &a = globals[i];
    for (uptr j = 0; j < other.size; j++) {
      const __asan_global &b = other.globals[j];
      if (a.beg == b.beg &&
          a.beg <= addr &&
          b.beg <= other.addr &&
          (addr + access_size) < (a.beg + a.size) &&
          (other.addr + other.access_size) < (b.beg + b.size))
        return true;
    }
  }

  return false;
}

void StackAddressDescription::Print() const {
  Decorator d;
  Printf("%s", d.Location());
  Printf("Address %p is located in stack of thread %s", (void *)addr,
         AsanThreadIdAndName(tid).c_str());

  if (!frame_descr) {
    Printf("%s\n", d.Default());
    return;
  }
  Printf(" at offset %zu in frame%s\n", offset, d.Default());

  // Now we print the frame where the alloca has happened.
  // We print this frame as a stack trace with one element.
  // The symbolizer may print more than one frame if inlining was involved.
  // The frame numbers may be different than those in the stack trace printed
  // previously. That's unfortunate, but I have no better solution,
  // especially given that the alloca may be from entirely different place
  // (e.g. use-after-scope, or different thread's stack).
  Printf("%s", d.Default());
  StackTrace alloca_stack(&frame_pc, 1);
  alloca_stack.Print();

  InternalMmapVector<StackVarDescr> vars;
  vars.reserve(16);
  if (!ParseFrameDescription(frame_descr, &vars)) {
    Printf(
        "AddressSanitizer can't parse the stack frame "
        "descriptor: |%s|\n",
        frame_descr);
    // 'addr' is a stack address, so return true even if we can't parse frame
    return;
  }
  uptr n_objects = vars.size();
  // Report the number of stack objects.
  Printf("  This frame has %zu object(s):\n", n_objects);

  // Report all objects in this frame.
  for (uptr i = 0; i < n_objects; i++) {
    uptr prev_var_end = i ? vars[i - 1].beg + vars[i - 1].size : 0;
    uptr next_var_beg = i + 1 < n_objects ? vars[i + 1].beg : ~(0UL);
    PrintAccessAndVarIntersection(vars[i], offset, access_size, prev_var_end,
                                  next_var_beg);
  }
  Printf(
      "HINT: this may be a false positive if your program uses "
      "some custom stack unwind mechanism, swapcontext or vfork\n");
  if (SANITIZER_WINDOWS)
    Printf("      (longjmp, SEH and C++ exceptions *are* supported)\n");
  else
    Printf("      (longjmp and C++ exceptions *are* supported)\n");

  DescribeThread(GetThreadContextByTidLocked(tid));
}

void HeapAddressDescription::Print() const {
  PrintHeapChunkAccess(addr, chunk_access);

  asanThreadRegistry().CheckLocked();
  AsanThreadContext *alloc_thread = GetThreadContextByTidLocked(alloc_tid);
  StackTrace alloc_stack = GetStackTraceFromId(alloc_stack_id);

  Decorator d;
  AsanThreadContext *free_thread = nullptr;
  if (free_tid != kInvalidTid) {
    free_thread = GetThreadContextByTidLocked(free_tid);
    Printf("%sfreed by thread %s here:%s\n", d.Allocation(),
           AsanThreadIdAndName(free_thread).c_str(), d.Default());
    StackTrace free_stack = GetStackTraceFromId(free_stack_id);
    free_stack.Print();
    Printf("%spreviously allocated by thread %s here:%s\n", d.Allocation(),
           AsanThreadIdAndName(alloc_thread).c_str(), d.Default());
  } else {
    Printf("%sallocated by thread %s here:%s\n", d.Allocation(),
           AsanThreadIdAndName(alloc_thread).c_str(), d.Default());
  }
  alloc_stack.Print();
  DescribeThread(GetCurrentThread());
  if (free_thread) DescribeThread(free_thread);
  DescribeThread(alloc_thread);
}

AddressDescription::AddressDescription(uptr addr, uptr access_size,
                                       bool shouldLockThreadRegistry) {
  if (GetShadowAddressInformation(addr, &data.shadow)) {
    data.kind = kAddressKindShadow;
    return;
  }
  if (GetHeapAddressInformation(addr, access_size, &data.heap)) {
    data.kind = kAddressKindHeap;
    return;
  }

  bool isStackMemory = false;
  if (shouldLockThreadRegistry) {
    ThreadRegistryLock l(&asanThreadRegistry());
    isStackMemory = GetStackAddressInformation(addr, access_size, &data.stack);
  } else {
    isStackMemory = GetStackAddressInformation(addr, access_size, &data.stack);
  }
  if (isStackMemory) {
    data.kind = kAddressKindStack;
    return;
  }

  if (GetGlobalAddressInformation(addr, access_size, &data.global)) {
    data.kind = kAddressKindGlobal;
    return;
  }
  data.kind = kAddressKindWild;
  data.wild.addr = addr;
  data.wild.access_size = access_size;
}

void WildAddressDescription::Print() const {
  Printf("Address %p is a wild pointer inside of access range of size %p.\n",
         (void *)addr, (void *)access_size);
}

void PrintAddressDescription(uptr addr, uptr access_size,
                             const char *bug_type) {
  ShadowAddressDescription shadow_descr;
  if (GetShadowAddressInformation(addr, &shadow_descr)) {
    shadow_descr.Print();
    return;
  }

  GlobalAddressDescription global_descr;
  if (GetGlobalAddressInformation(addr, access_size, &global_descr)) {
    global_descr.Print(bug_type);
    return;
  }

  StackAddressDescription stack_descr;
  if (GetStackAddressInformation(addr, access_size, &stack_descr)) {
    stack_descr.Print();
    return;
  }

  HeapAddressDescription heap_descr;
  if (GetHeapAddressInformation(addr, access_size, &heap_descr)) {
    heap_descr.Print();
    return;
  }

  // We exhausted our possibilities. Bail out.
  Printf(
      "AddressSanitizer can not describe address in more detail "
      "(wild memory access suspected).\n");
}
}  // namespace __asan
