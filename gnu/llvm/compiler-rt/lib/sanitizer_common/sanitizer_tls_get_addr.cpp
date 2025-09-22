//===-- sanitizer_tls_get_addr.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Handle the __tls_get_addr call.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_tls_get_addr.h"

#include "sanitizer_allocator_interface.h"
#include "sanitizer_atomic.h"
#include "sanitizer_flags.h"
#include "sanitizer_platform_interceptors.h"

namespace __sanitizer {
#if SANITIZER_INTERCEPT_TLS_GET_ADDR

// The actual parameter that comes to __tls_get_addr
// is a pointer to a struct with two words in it:
struct TlsGetAddrParam {
  uptr dso_id;
  uptr offset;
};

// This must be static TLS
__attribute__((tls_model("initial-exec")))
static __thread DTLS dtls;

// Make sure we properly destroy the DTLS objects:
// this counter should never get too large.
static atomic_uintptr_t number_of_live_dtls;

static const uptr kDestroyedThread = -1;

static void DTLS_Deallocate(DTLS::DTVBlock *block) {
  VReport(2, "__tls_get_addr: DTLS_Deallocate %p\n", (void *)block);
  UnmapOrDie(block, sizeof(DTLS::DTVBlock));
  atomic_fetch_sub(&number_of_live_dtls, 1, memory_order_relaxed);
}

static DTLS::DTVBlock *DTLS_NextBlock(atomic_uintptr_t *cur) {
  uptr v = atomic_load(cur, memory_order_acquire);
  if (v == kDestroyedThread)
    return nullptr;
  DTLS::DTVBlock *next = (DTLS::DTVBlock *)v;
  if (next)
    return next;
  DTLS::DTVBlock *new_dtv =
      (DTLS::DTVBlock *)MmapOrDie(sizeof(DTLS::DTVBlock), "DTLS_NextBlock");
  uptr prev = 0;
  if (!atomic_compare_exchange_strong(cur, &prev, (uptr)new_dtv,
                                      memory_order_seq_cst)) {
    UnmapOrDie(new_dtv, sizeof(DTLS::DTVBlock));
    return (DTLS::DTVBlock *)prev;
  }
  uptr num_live_dtls =
      atomic_fetch_add(&number_of_live_dtls, 1, memory_order_relaxed);
  VReport(2, "__tls_get_addr: DTLS_NextBlock %p %zd\n", (void *)&dtls,
          num_live_dtls);
  return new_dtv;
}

static DTLS::DTV *DTLS_Find(uptr id) {
  VReport(2, "__tls_get_addr: DTLS_Find %p %zd\n", (void *)&dtls, id);
  static constexpr uptr kPerBlock = ARRAY_SIZE(DTLS::DTVBlock::dtvs);
  DTLS::DTVBlock *cur = DTLS_NextBlock(&dtls.dtv_block);
  if (!cur)
    return nullptr;
  for (; id >= kPerBlock; id -= kPerBlock) cur = DTLS_NextBlock(&cur->next);
  return cur->dtvs + id;
}

void DTLS_Destroy() {
  if (!common_flags()->intercept_tls_get_addr) return;
  VReport(2, "__tls_get_addr: DTLS_Destroy %p\n", (void *)&dtls);
  DTLS::DTVBlock *block = (DTLS::DTVBlock *)atomic_exchange(
      &dtls.dtv_block, kDestroyedThread, memory_order_release);
  while (block) {
    DTLS::DTVBlock *next =
        (DTLS::DTVBlock *)atomic_load(&block->next, memory_order_acquire);
    DTLS_Deallocate(block);
    block = next;
  }
}

#if defined(__powerpc64__) || defined(__mips__)
// This is glibc's TLS_DTV_OFFSET:
// "Dynamic thread vector pointers point 0x8000 past the start of each
//  TLS block." (sysdeps/<arch>/dl-tls.h)
static const uptr kDtvOffset = 0x8000;
#elif defined(__riscv)
// This is glibc's TLS_DTV_OFFSET:
// "Dynamic thread vector pointers point 0x800 past the start of each
// TLS block." (sysdeps/riscv/dl-tls.h)
static const uptr kDtvOffset = 0x800;
#else
static const uptr kDtvOffset = 0;
#endif

extern "C" {
SANITIZER_WEAK_ATTRIBUTE
uptr __sanitizer_get_allocated_size(const void *p);

SANITIZER_WEAK_ATTRIBUTE
const void *__sanitizer_get_allocated_begin(const void *p);
}

DTLS::DTV *DTLS_on_tls_get_addr(void *arg_void, void *res,
                                uptr static_tls_begin, uptr static_tls_end) {
  if (!common_flags()->intercept_tls_get_addr) return 0;
  TlsGetAddrParam *arg = reinterpret_cast<TlsGetAddrParam *>(arg_void);
  uptr dso_id = arg->dso_id;
  DTLS::DTV *dtv = DTLS_Find(dso_id);
  if (!dtv || dtv->beg)
    return 0;
  uptr tls_size = 0;
  uptr tls_beg = reinterpret_cast<uptr>(res) - arg->offset - kDtvOffset;
  VReport(2,
          "__tls_get_addr: %p {0x%zx,0x%zx} => %p; tls_beg: %p; sp: %p "
          "num_live_dtls %zd\n",
          (void *)arg, arg->dso_id, arg->offset, res, (void *)tls_beg,
          (void *)&tls_beg,
          atomic_load(&number_of_live_dtls, memory_order_relaxed));
  if (dtls.last_memalign_ptr == tls_beg) {
    tls_size = dtls.last_memalign_size;
    VReport(2, "__tls_get_addr: glibc <=2.24 suspected; tls={%p,0x%zx}\n",
            (void *)tls_beg, tls_size);
  } else if (tls_beg >= static_tls_begin && tls_beg < static_tls_end) {
    // This is the static TLS block which was initialized / unpoisoned at thread
    // creation.
    VReport(2, "__tls_get_addr: static tls: %p\n", (void *)tls_beg);
    tls_size = 0;
  } else if (const void *start =
                 __sanitizer_get_allocated_begin((void *)tls_beg)) {
    tls_beg = (uptr)start;
    tls_size = __sanitizer_get_allocated_size(start);
    VReport(2, "__tls_get_addr: glibc >=2.25 suspected; tls={%p,0x%zx}\n",
            (void *)tls_beg, tls_size);
  } else {
    VReport(2, "__tls_get_addr: Can't guess glibc version\n");
    // This may happen inside the DTOR of main thread, so just ignore it.
    tls_size = 0;
  }
  dtv->beg = tls_beg;
  dtv->size = tls_size;
  return dtv;
}

void DTLS_on_libc_memalign(void *ptr, uptr size) {
  if (!common_flags()->intercept_tls_get_addr) return;
  VReport(2, "DTLS_on_libc_memalign: %p 0x%zx\n", ptr, size);
  dtls.last_memalign_ptr = reinterpret_cast<uptr>(ptr);
  dtls.last_memalign_size = size;
}

DTLS *DTLS_Get() { return &dtls; }

bool DTLSInDestruction(DTLS *dtls) {
  return atomic_load(&dtls->dtv_block, memory_order_relaxed) ==
         kDestroyedThread;
}

#else
void DTLS_on_libc_memalign(void *ptr, uptr size) {}
DTLS::DTV *DTLS_on_tls_get_addr(void *arg, void *res,
  unsigned long, unsigned long) { return 0; }
DTLS *DTLS_Get() { return 0; }
void DTLS_Destroy() {}
bool DTLSInDestruction(DTLS *dtls) {
  UNREACHABLE("dtls is unsupported on this platform!");
}

#endif  // SANITIZER_INTERCEPT_TLS_GET_ADDR

}  // namespace __sanitizer
