//===- CtxInstrProfiling.cpp - contextual instrumented PGO ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CtxInstrProfiling.h"
#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_dense_map.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_mutex.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_thread_safety.h"
#include "sanitizer_common/sanitizer_vector.h"

#include <assert.h>

using namespace __ctx_profile;

namespace {
// Keep track of all the context roots we actually saw, so we can then traverse
// them when the user asks for the profile in __llvm_ctx_profile_fetch
__sanitizer::SpinMutex AllContextsMutex;
SANITIZER_GUARDED_BY(AllContextsMutex)
__sanitizer::Vector<ContextRoot *> AllContextRoots;

// utility to taint a pointer by setting the LSB. There is an assumption
// throughout that the addresses of contexts are even (really, they should be
// align(8), but "even"-ness is the minimum assumption)
// "scratch contexts" are buffers that we return in certain cases - they are
// large enough to allow for memory safe counter access, but they don't link
// subcontexts below them (the runtime recognizes them and enforces that)
ContextNode *markAsScratch(const ContextNode *Ctx) {
  return reinterpret_cast<ContextNode *>(reinterpret_cast<uint64_t>(Ctx) | 1);
}

// Used when getting the data from TLS. We don't *really* need to reset, but
// it's a simpler system if we do.
template <typename T> inline T consume(T &V) {
  auto R = V;
  V = {0};
  return R;
}

// We allocate at least kBuffSize Arena pages. The scratch buffer is also that
// large.
constexpr size_t kPower = 20;
constexpr size_t kBuffSize = 1 << kPower;

// Highly unlikely we need more than kBuffSize for a context.
size_t getArenaAllocSize(size_t Needed) {
  if (Needed >= kBuffSize)
    return 2 * Needed;
  return kBuffSize;
}

// verify the structural integrity of the context
bool validate(const ContextRoot *Root) {
  // all contexts should be laid out in some arena page. Go over each arena
  // allocated for this Root, and jump over contained contexts based on
  // self-reported sizes.
  __sanitizer::DenseMap<uint64_t, bool> ContextStartAddrs;
  for (const auto *Mem = Root->FirstMemBlock; Mem; Mem = Mem->next()) {
    const auto *Pos = Mem->start();
    while (Pos < Mem->pos()) {
      const auto *Ctx = reinterpret_cast<const ContextNode *>(Pos);
      if (!ContextStartAddrs.insert({reinterpret_cast<uint64_t>(Ctx), true})
               .second)
        return false;
      Pos += Ctx->size();
    }
  }

  // Now traverse the contexts again the same way, but validate all nonull
  // subcontext addresses appear in the set computed above.
  for (const auto *Mem = Root->FirstMemBlock; Mem; Mem = Mem->next()) {
    const auto *Pos = Mem->start();
    while (Pos < Mem->pos()) {
      const auto *Ctx = reinterpret_cast<const ContextNode *>(Pos);
      for (uint32_t I = 0; I < Ctx->callsites_size(); ++I)
        for (auto *Sub = Ctx->subContexts()[I]; Sub; Sub = Sub->next())
          if (!ContextStartAddrs.find(reinterpret_cast<uint64_t>(Sub)))
            return false;

      Pos += Ctx->size();
    }
  }
  return true;
}

inline ContextNode *allocContextNode(char *Place, GUID Guid,
                                     uint32_t NrCounters, uint32_t NrCallsites,
                                     ContextNode *Next = nullptr) {
  assert(reinterpret_cast<uint64_t>(Place) % ExpectedAlignment == 0);
  return new (Place) ContextNode(Guid, NrCounters, NrCallsites, Next);
}

void resetContextNode(ContextNode &Node) {
  // FIXME(mtrofin): this is std::memset, which we can probably use if we
  // drop/reduce the dependency on sanitizer_common.
  for (uint32_t I = 0; I < Node.counters_size(); ++I)
    Node.counters()[I] = 0;
  for (uint32_t I = 0; I < Node.callsites_size(); ++I)
    for (auto *Next = Node.subContexts()[I]; Next; Next = Next->next())
      resetContextNode(*Next);
}

void onContextEnter(ContextNode &Node) { ++Node.counters()[0]; }

} // namespace

// the scratch buffer - what we give when we can't produce a real context (the
// scratch isn't "real" in that it's expected to be clobbered carelessly - we
// don't read it). The other important thing is that the callees from a scratch
// context also get a scratch context.
// Eventually this can be replaced with per-function buffers, a'la the typical
// (flat) instrumented FDO buffers. The clobbering aspect won't apply there, but
// the part about determining the nature of the subcontexts does.
__thread char __Buffer[kBuffSize] = {0};

#define TheScratchContext                                                      \
  markAsScratch(reinterpret_cast<ContextNode *>(__Buffer))

// init the TLSes
__thread void *volatile __llvm_ctx_profile_expected_callee[2] = {nullptr,
                                                                 nullptr};
__thread ContextNode **volatile __llvm_ctx_profile_callsite[2] = {0, 0};

__thread ContextRoot *volatile __llvm_ctx_profile_current_context_root =
    nullptr;

Arena::Arena(uint32_t Size) : Size(Size) {
  __sanitizer::internal_memset(start(), 0, Size);
}

// FIXME(mtrofin): use malloc / mmap instead of sanitizer common APIs to reduce
// the dependency on the latter.
Arena *Arena::allocateNewArena(size_t Size, Arena *Prev) {
  assert(!Prev || Prev->Next == nullptr);
  Arena *NewArena = new (__sanitizer::InternalAlloc(
      Size + sizeof(Arena), /*cache=*/nullptr, /*alignment=*/ExpectedAlignment))
      Arena(Size);
  if (Prev)
    Prev->Next = NewArena;
  return NewArena;
}

void Arena::freeArenaList(Arena *&A) {
  assert(A);
  for (auto *I = A; I != nullptr;) {
    auto *Current = I;
    I = I->Next;
    __sanitizer::InternalFree(Current);
  }
  A = nullptr;
}

// If this is the first time we hit a callsite with this (Guid) particular
// callee, we need to allocate.
ContextNode *getCallsiteSlow(GUID Guid, ContextNode **InsertionPoint,
                             uint32_t NrCounters, uint32_t NrCallsites) {
  auto AllocSize = ContextNode::getAllocSize(NrCounters, NrCallsites);
  auto *Mem = __llvm_ctx_profile_current_context_root->CurrentMem;
  char *AllocPlace = Mem->tryBumpAllocate(AllocSize);
  if (!AllocPlace) {
    // if we failed to allocate on the current arena, allocate a new arena,
    // and place it on __llvm_ctx_profile_current_context_root->CurrentMem so we
    // find it from now on for other cases when we need to getCallsiteSlow.
    // Note that allocateNewArena will link the allocated memory in the list of
    // Arenas.
    __llvm_ctx_profile_current_context_root->CurrentMem = Mem =
        Mem->allocateNewArena(getArenaAllocSize(AllocSize), Mem);
    AllocPlace = Mem->tryBumpAllocate(AllocSize);
  }
  auto *Ret = allocContextNode(AllocPlace, Guid, NrCounters, NrCallsites,
                               *InsertionPoint);
  *InsertionPoint = Ret;
  return Ret;
}

ContextNode *__llvm_ctx_profile_get_context(void *Callee, GUID Guid,
                                            uint32_t NrCounters,
                                            uint32_t NrCallsites) {
  // fast "out" if we're not even doing contextual collection.
  if (!__llvm_ctx_profile_current_context_root)
    return TheScratchContext;

  // also fast "out" if the caller is scratch. We can see if it's scratch by
  // looking at the interior pointer into the subcontexts vector that the caller
  // provided, which, if the context is scratch, so is that interior pointer
  // (because all the address calculations are using even values. Or more
  // precisely, aligned - 8 values)
  auto **CallsiteContext = consume(__llvm_ctx_profile_callsite[0]);
  if (!CallsiteContext || isScratch(CallsiteContext))
    return TheScratchContext;

  // if the callee isn't the expected one, return scratch.
  // Signal handler(s) could have been invoked at any point in the execution.
  // Should that have happened, and had it (the handler) be built with
  // instrumentation, its __llvm_ctx_profile_get_context would have failed here.
  // Its sub call graph would have then populated
  // __llvm_ctx_profile_{expected_callee | callsite} at index 1.
  // The normal call graph may be impacted in that, if the signal handler
  // happened somewhere before we read the TLS here, we'd see the TLS reset and
  // we'd also fail here. That would just mean we would loose counter values for
  // the normal subgraph, this time around. That should be very unlikely, but if
  // it happens too frequently, we should be able to detect discrepancies in
  // entry counts (caller-callee). At the moment, the design goes on the
  // assumption that is so unfrequent, though, that it's not worth doing more
  // for that case.
  auto *ExpectedCallee = consume(__llvm_ctx_profile_expected_callee[0]);
  if (ExpectedCallee != Callee)
    return TheScratchContext;

  auto *Callsite = *CallsiteContext;
  // in the case of indirect calls, we will have all seen targets forming a
  // linked list here. Find the one corresponding to this callee.
  while (Callsite && Callsite->guid() != Guid) {
    Callsite = Callsite->next();
  }
  auto *Ret = Callsite ? Callsite
                       : getCallsiteSlow(Guid, CallsiteContext, NrCounters,
                                         NrCallsites);
  if (Ret->callsites_size() != NrCallsites ||
      Ret->counters_size() != NrCounters)
    __sanitizer::Printf("[ctxprof] Returned ctx differs from what's asked: "
                        "Context: %p, Asked: %lu %u %u, Got: %lu %u %u \n",
                        reinterpret_cast<void *>(Ret), Guid, NrCallsites,
                        NrCounters, Ret->guid(), Ret->callsites_size(),
                        Ret->counters_size());
  onContextEnter(*Ret);
  return Ret;
}

// This should be called once for a Root. Allocate the first arena, set up the
// first context.
void setupContext(ContextRoot *Root, GUID Guid, uint32_t NrCounters,
                  uint32_t NrCallsites) {
  __sanitizer::GenericScopedLock<__sanitizer::SpinMutex> Lock(
      &AllContextsMutex);
  // Re-check - we got here without having had taken a lock.
  if (Root->FirstMemBlock)
    return;
  const auto Needed = ContextNode::getAllocSize(NrCounters, NrCallsites);
  auto *M = Arena::allocateNewArena(getArenaAllocSize(Needed));
  Root->FirstMemBlock = M;
  Root->CurrentMem = M;
  Root->FirstNode = allocContextNode(M->tryBumpAllocate(Needed), Guid,
                                     NrCounters, NrCallsites);
  AllContextRoots.PushBack(Root);
}

ContextNode *__llvm_ctx_profile_start_context(
    ContextRoot *Root, GUID Guid, uint32_t Counters,
    uint32_t Callsites) SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  if (!Root->FirstMemBlock) {
    setupContext(Root, Guid, Counters, Callsites);
  }
  if (Root->Taken.TryLock()) {
    __llvm_ctx_profile_current_context_root = Root;
    onContextEnter(*Root->FirstNode);
    return Root->FirstNode;
  }
  // If this thread couldn't take the lock, return scratch context.
  __llvm_ctx_profile_current_context_root = nullptr;
  return TheScratchContext;
}

void __llvm_ctx_profile_release_context(ContextRoot *Root)
    SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  if (__llvm_ctx_profile_current_context_root) {
    __llvm_ctx_profile_current_context_root = nullptr;
    Root->Taken.Unlock();
  }
}

void __llvm_ctx_profile_start_collection() {
  size_t NrMemUnits = 0;
  __sanitizer::GenericScopedLock<__sanitizer::SpinMutex> Lock(
      &AllContextsMutex);
  for (uint32_t I = 0; I < AllContextRoots.Size(); ++I) {
    auto *Root = AllContextRoots[I];
    __sanitizer::GenericScopedLock<__sanitizer::StaticSpinMutex> Lock(
        &Root->Taken);
    for (auto *Mem = Root->FirstMemBlock; Mem; Mem = Mem->next())
      ++NrMemUnits;

    resetContextNode(*Root->FirstNode);
  }
  __sanitizer::Printf("[ctxprof] Initial NrMemUnits: %zu \n", NrMemUnits);
}

bool __llvm_ctx_profile_fetch(void *Data,
                              bool (*Writer)(void *W, const ContextNode &)) {
  assert(Writer);
  __sanitizer::GenericScopedLock<__sanitizer::SpinMutex> Lock(
      &AllContextsMutex);

  for (int I = 0, E = AllContextRoots.Size(); I < E; ++I) {
    auto *Root = AllContextRoots[I];
    __sanitizer::GenericScopedLock<__sanitizer::StaticSpinMutex> TakenLock(
        &Root->Taken);
    if (!validate(Root)) {
      __sanitizer::Printf("[ctxprof] Contextual Profile is %s\n", "invalid");
      return false;
    }
    if (!Writer(Data, *Root->FirstNode))
      return false;
  }
  return true;
}

void __llvm_ctx_profile_free() {
  __sanitizer::GenericScopedLock<__sanitizer::SpinMutex> Lock(
      &AllContextsMutex);
  for (int I = 0, E = AllContextRoots.Size(); I < E; ++I)
    for (auto *A = AllContextRoots[I]->FirstMemBlock; A;) {
      auto *C = A;
      A = A->next();
      __sanitizer::InternalFree(C);
    }
  AllContextRoots.Reset();
}
