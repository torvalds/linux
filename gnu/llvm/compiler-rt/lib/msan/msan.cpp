//===-- msan.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
// MemorySanitizer runtime.
//===----------------------------------------------------------------------===//

#include "msan.h"

#include "msan_chained_origin_depot.h"
#include "msan_origin.h"
#include "msan_poisoning.h"
#include "msan_report.h"
#include "msan_thread.h"
#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_interface_internal.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_procmaps.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "sanitizer_common/sanitizer_symbolizer.h"
#include "ubsan/ubsan_flags.h"
#include "ubsan/ubsan_init.h"

// ACHTUNG! No system header includes in this file.

using namespace __sanitizer;

// Globals.
static THREADLOCAL int msan_expect_umr = 0;
static THREADLOCAL int msan_expected_umr_found = 0;

// Function argument shadow. Each argument starts at the next available 8-byte
// aligned address.
SANITIZER_INTERFACE_ATTRIBUTE
THREADLOCAL u64 __msan_param_tls[kMsanParamTlsSize / sizeof(u64)];

// Function argument origin. Each argument starts at the same offset as the
// corresponding shadow in (__msan_param_tls). Slightly weird, but changing this
// would break compatibility with older prebuilt binaries.
SANITIZER_INTERFACE_ATTRIBUTE
THREADLOCAL u32 __msan_param_origin_tls[kMsanParamTlsSize / sizeof(u32)];

SANITIZER_INTERFACE_ATTRIBUTE
THREADLOCAL u64 __msan_retval_tls[kMsanRetvalTlsSize / sizeof(u64)];

SANITIZER_INTERFACE_ATTRIBUTE
THREADLOCAL u32 __msan_retval_origin_tls;

alignas(16) SANITIZER_INTERFACE_ATTRIBUTE THREADLOCAL u64
    __msan_va_arg_tls[kMsanParamTlsSize / sizeof(u64)];

alignas(16) SANITIZER_INTERFACE_ATTRIBUTE THREADLOCAL u32
    __msan_va_arg_origin_tls[kMsanParamTlsSize / sizeof(u32)];

SANITIZER_INTERFACE_ATTRIBUTE
THREADLOCAL u64 __msan_va_arg_overflow_size_tls;

SANITIZER_INTERFACE_ATTRIBUTE
THREADLOCAL u32 __msan_origin_tls;

extern "C" SANITIZER_WEAK_ATTRIBUTE const int __msan_track_origins;

int __msan_get_track_origins() {
  return &__msan_track_origins ? __msan_track_origins : 0;
}

extern "C" SANITIZER_WEAK_ATTRIBUTE const int __msan_keep_going;

namespace __msan {

static THREADLOCAL int is_in_symbolizer_or_unwinder;
static void EnterSymbolizerOrUnwider() { ++is_in_symbolizer_or_unwinder; }
static void ExitSymbolizerOrUnwider() { --is_in_symbolizer_or_unwinder; }
bool IsInSymbolizerOrUnwider() { return is_in_symbolizer_or_unwinder; }

struct UnwinderScope {
  UnwinderScope() { EnterSymbolizerOrUnwider(); }
  ~UnwinderScope() { ExitSymbolizerOrUnwider(); }
};

static Flags msan_flags;

Flags *flags() { return &msan_flags; }

int msan_inited = 0;
bool msan_init_is_running;

int msan_report_count = 0;

// Array of stack origins.
// FIXME: make it resizable.
// Although BSS memory doesn't cost anything until used, it is limited to 2GB
// in some configurations (e.g., "relocation R_X86_64_PC32 out of range:
// ... is not in [-2147483648, 2147483647]; references section '.bss'").
// We use kNumStackOriginDescrs * (sizeof(char*) + sizeof(uptr)) == 64MB.
#if SANITIZER_PPC
// soft_rss_limit test (release_origin.c) fails on PPC if kNumStackOriginDescrs
// is too high
static const uptr kNumStackOriginDescrs = 1 * 1024 * 1024;
#else
static const uptr kNumStackOriginDescrs = 4 * 1024 * 1024;
#endif  // SANITIZER_PPC
static const char *StackOriginDescr[kNumStackOriginDescrs];
static uptr StackOriginPC[kNumStackOriginDescrs];
static atomic_uint32_t NumStackOriginDescrs;

void Flags::SetDefaults() {
#define MSAN_FLAG(Type, Name, DefaultValue, Description) Name = DefaultValue;
#include "msan_flags.inc"
#undef MSAN_FLAG
}

// keep_going is an old name for halt_on_error,
// and it has inverse meaning.
class FlagHandlerKeepGoing final : public FlagHandlerBase {
  bool *halt_on_error_;

 public:
  explicit FlagHandlerKeepGoing(bool *halt_on_error)
      : halt_on_error_(halt_on_error) {}
  bool Parse(const char *value) final {
    bool tmp;
    FlagHandler<bool> h(&tmp);
    if (!h.Parse(value)) return false;
    *halt_on_error_ = !tmp;
    return true;
  }
  bool Format(char *buffer, uptr size) final {
    const char *keep_going_str = (*halt_on_error_) ? "false" : "true";
    return FormatString(buffer, size, keep_going_str);
  }
};

static void RegisterMsanFlags(FlagParser *parser, Flags *f) {
#define MSAN_FLAG(Type, Name, DefaultValue, Description) \
  RegisterFlag(parser, #Name, Description, &f->Name);
#include "msan_flags.inc"
#undef MSAN_FLAG

  FlagHandlerKeepGoing *fh_keep_going = new (GetGlobalLowLevelAllocator())
      FlagHandlerKeepGoing(&f->halt_on_error);
  parser->RegisterHandler("keep_going", fh_keep_going,
                          "deprecated, use halt_on_error");
}

static void InitializeFlags() {
  SetCommonFlagsDefaults();
  {
    CommonFlags cf;
    cf.CopyFrom(*common_flags());
    cf.external_symbolizer_path = GetEnv("MSAN_SYMBOLIZER_PATH");
    cf.malloc_context_size = 20;
    cf.handle_ioctl = true;
    // FIXME: test and enable.
    cf.check_printf = false;
    cf.intercept_tls_get_addr = true;
    OverrideCommonFlags(cf);
  }

  Flags *f = flags();
  f->SetDefaults();

  FlagParser parser;
  RegisterMsanFlags(&parser, f);
  RegisterCommonFlags(&parser);

#if MSAN_CONTAINS_UBSAN
  __ubsan::Flags *uf = __ubsan::flags();
  uf->SetDefaults();

  FlagParser ubsan_parser;
  __ubsan::RegisterUbsanFlags(&ubsan_parser, uf);
  RegisterCommonFlags(&ubsan_parser);
#endif

  // Override from user-specified string.
  parser.ParseString(__msan_default_options());
#if MSAN_CONTAINS_UBSAN
  const char *ubsan_default_options = __ubsan_default_options();
  ubsan_parser.ParseString(ubsan_default_options);
#endif

  parser.ParseStringFromEnv("MSAN_OPTIONS");
#if MSAN_CONTAINS_UBSAN
  ubsan_parser.ParseStringFromEnv("UBSAN_OPTIONS");
#endif

  InitializeCommonFlags();

  if (Verbosity()) ReportUnrecognizedFlags();

  if (common_flags()->help) parser.PrintFlagDescriptions();

  // Check if deprecated exit_code MSan flag is set.
  if (f->exit_code != -1) {
    if (Verbosity())
      Printf("MSAN_OPTIONS=exit_code is deprecated! "
             "Please use MSAN_OPTIONS=exitcode instead.\n");
    CommonFlags cf;
    cf.CopyFrom(*common_flags());
    cf.exitcode = f->exit_code;
    OverrideCommonFlags(cf);
  }

  // Check flag values:
  if (f->origin_history_size < 0 ||
      f->origin_history_size > Origin::kMaxDepth) {
    Printf(
        "Origin history size invalid: %d. Must be 0 (unlimited) or in [1, %d] "
        "range.\n",
        f->origin_history_size, Origin::kMaxDepth);
    Die();
  }
  // Limiting to kStackDepotMaxUseCount / 2 to avoid overflow in
  // StackDepotHandle::inc_use_count_unsafe.
  if (f->origin_history_per_stack_limit < 0 ||
      f->origin_history_per_stack_limit > kStackDepotMaxUseCount / 2) {
    Printf(
        "Origin per-stack limit invalid: %d. Must be 0 (unlimited) or in [1, "
        "%d] range.\n",
        f->origin_history_per_stack_limit, kStackDepotMaxUseCount / 2);
    Die();
  }
  if (f->store_context_size < 1) f->store_context_size = 1;
}

void PrintWarningWithOrigin(uptr pc, uptr bp, u32 origin) {
  if (msan_expect_umr) {
    // Printf("Expected UMR\n");
    __msan_origin_tls = origin;
    msan_expected_umr_found = 1;
    return;
  }

  ++msan_report_count;

  GET_FATAL_STACK_TRACE_PC_BP(pc, bp);

  u32 report_origin =
    (__msan_get_track_origins() && Origin::isValidId(origin)) ? origin : 0;
  ReportUMR(&stack, report_origin);

  if (__msan_get_track_origins() && !Origin::isValidId(origin)) {
    Printf(
        "  ORIGIN: invalid (%x). Might be a bug in MemorySanitizer origin "
        "tracking.\n    This could still be a bug in your code, too!\n",
        origin);
  }
}

void UnpoisonParam(uptr n) {
  internal_memset(__msan_param_tls, 0, n * sizeof(*__msan_param_tls));
}

// Backup MSan runtime TLS state.
// Implementation must be async-signal-safe.
// Instances of this class may live on the signal handler stack, and data size
// may be an issue.
void ScopedThreadLocalStateBackup::Backup() {
  va_arg_overflow_size_tls = __msan_va_arg_overflow_size_tls;
}

void ScopedThreadLocalStateBackup::Restore() {
  // A lame implementation that only keeps essential state and resets the rest.
  __msan_va_arg_overflow_size_tls = va_arg_overflow_size_tls;

  internal_memset(__msan_param_tls, 0, sizeof(__msan_param_tls));
  internal_memset(__msan_retval_tls, 0, sizeof(__msan_retval_tls));
  internal_memset(__msan_va_arg_tls, 0, sizeof(__msan_va_arg_tls));
  internal_memset(__msan_va_arg_origin_tls, 0,
                  sizeof(__msan_va_arg_origin_tls));

  if (__msan_get_track_origins()) {
    internal_memset(&__msan_retval_origin_tls, 0,
                    sizeof(__msan_retval_origin_tls));
    internal_memset(__msan_param_origin_tls, 0,
                    sizeof(__msan_param_origin_tls));
  }
}

void UnpoisonThreadLocalState() {
}

const char *GetStackOriginDescr(u32 id, uptr *pc) {
  CHECK_LT(id, kNumStackOriginDescrs);
  if (pc) *pc = StackOriginPC[id];
  return StackOriginDescr[id];
}

u32 ChainOrigin(u32 id, StackTrace *stack) {
  MsanThread *t = GetCurrentThread();
  if (t && t->InSignalHandler())
    return id;

  Origin o = Origin::FromRawId(id);
  stack->tag = StackTrace::TAG_UNKNOWN;
  Origin chained = Origin::CreateChainedOrigin(o, stack);
  return chained.raw_id();
}

// Current implementation separates the 'id_ptr' from the 'descr' and makes
// 'descr' constant.
// Previous implementation 'descr' is created at compile time and contains
// '----' in the beginning.  When we see descr for the first time we replace
// '----' with a uniq id and set the origin to (id | (31-th bit)).
static inline void SetAllocaOrigin(void *a, uptr size, u32 *id_ptr, char *descr,
                                   uptr pc) {
  static const u32 dash = '-';
  static const u32 first_timer =
      dash + (dash << 8) + (dash << 16) + (dash << 24);
  u32 id = *id_ptr;
  if (id == 0 || id == first_timer) {
    u32 idx = atomic_fetch_add(&NumStackOriginDescrs, 1, memory_order_relaxed);
    CHECK_LT(idx, kNumStackOriginDescrs);
    StackOriginDescr[idx] = descr;
    StackOriginPC[idx] = pc;
    id = Origin::CreateStackOrigin(idx).raw_id();
    *id_ptr = id;
  }
  __msan_set_origin(a, size, id);
}

}  // namespace __msan

void __sanitizer::BufferedStackTrace::UnwindImpl(
    uptr pc, uptr bp, void *context, bool request_fast, u32 max_depth) {
  using namespace __msan;
  MsanThread *t = GetCurrentThread();
  if (!t || !StackTrace::WillUseFastUnwind(request_fast)) {
    // Block reports from our interceptors during _Unwind_Backtrace.
    UnwinderScope sym_scope;
    return Unwind(max_depth, pc, bp, context, t ? t->stack_top() : 0,
                  t ? t->stack_bottom() : 0, false);
  }
  if (StackTrace::WillUseFastUnwind(request_fast))
    Unwind(max_depth, pc, bp, nullptr, t->stack_top(), t->stack_bottom(), true);
  else
    Unwind(max_depth, pc, 0, context, 0, 0, false);
}

// Interface.

using namespace __msan;

#define MSAN_MAYBE_WARNING(type, size)              \
  void __msan_maybe_warning_##size(type s, u32 o) { \
    GET_CALLER_PC_BP;                               \
    if (UNLIKELY(s)) {                              \
      PrintWarningWithOrigin(pc, bp, o);            \
      if (__msan::flags()->halt_on_error) {         \
        Printf("Exiting\n");                        \
        Die();                                      \
      }                                             \
    }                                               \
  }

MSAN_MAYBE_WARNING(u8, 1)
MSAN_MAYBE_WARNING(u16, 2)
MSAN_MAYBE_WARNING(u32, 4)
MSAN_MAYBE_WARNING(u64, 8)

#define MSAN_MAYBE_STORE_ORIGIN(type, size)                       \
  void __msan_maybe_store_origin_##size(type s, void *p, u32 o) { \
    if (UNLIKELY(s)) {                                            \
      if (__msan_get_track_origins() > 1) {                       \
        GET_CALLER_PC_BP;                                         \
        GET_STORE_STACK_TRACE_PC_BP(pc, bp);                      \
        o = ChainOrigin(o, &stack);                               \
      }                                                           \
      *(u32 *)MEM_TO_ORIGIN((uptr)p & ~3UL) = o;                  \
    }                                                             \
  }

MSAN_MAYBE_STORE_ORIGIN(u8, 1)
MSAN_MAYBE_STORE_ORIGIN(u16, 2)
MSAN_MAYBE_STORE_ORIGIN(u32, 4)
MSAN_MAYBE_STORE_ORIGIN(u64, 8)

void __msan_warning() {
  GET_CALLER_PC_BP;
  PrintWarningWithOrigin(pc, bp, 0);
  if (__msan::flags()->halt_on_error) {
    if (__msan::flags()->print_stats)
      ReportStats();
    Printf("Exiting\n");
    Die();
  }
}

void __msan_warning_noreturn() {
  GET_CALLER_PC_BP;
  PrintWarningWithOrigin(pc, bp, 0);
  if (__msan::flags()->print_stats)
    ReportStats();
  Printf("Exiting\n");
  Die();
}

void __msan_warning_with_origin(u32 origin) {
  GET_CALLER_PC_BP;
  PrintWarningWithOrigin(pc, bp, origin);
  if (__msan::flags()->halt_on_error) {
    if (__msan::flags()->print_stats)
      ReportStats();
    Printf("Exiting\n");
    Die();
  }
}

void __msan_warning_with_origin_noreturn(u32 origin) {
  GET_CALLER_PC_BP;
  PrintWarningWithOrigin(pc, bp, origin);
  if (__msan::flags()->print_stats)
    ReportStats();
  Printf("Exiting\n");
  Die();
}

static void OnStackUnwind(const SignalContext &sig, const void *,
                          BufferedStackTrace *stack) {
  stack->Unwind(StackTrace::GetNextInstructionPc(sig.pc), sig.bp, sig.context,
                common_flags()->fast_unwind_on_fatal);
}

static void MsanOnDeadlySignal(int signo, void *siginfo, void *context) {
  HandleDeadlySignal(siginfo, context, GetTid(), &OnStackUnwind, nullptr);
}

static void CheckUnwind() {
  GET_FATAL_STACK_TRACE_PC_BP(StackTrace::GetCurrentPc(), GET_CURRENT_FRAME());
  stack.Print();
}

void __msan_init() {
  CHECK(!msan_init_is_running);
  if (msan_inited) return;
  msan_init_is_running = 1;
  SanitizerToolName = "MemorySanitizer";

  AvoidCVE_2016_2143();

  CacheBinaryName();
  InitializeFlags();

  // Install tool-specific callbacks in sanitizer_common.
  SetCheckUnwindCallback(CheckUnwind);

  __sanitizer_set_report_path(common_flags()->log_path);

  InitializeInterceptors();
  InstallAtForkHandler();
  CheckASLR();
  InitTlsSize();
  InstallDeadlySignalHandlers(MsanOnDeadlySignal);
  InstallAtExitHandler(); // Needs __cxa_atexit interceptor.

  DisableCoreDumperIfNecessary();
  if (StackSizeIsUnlimited()) {
    VPrintf(1, "Unlimited stack, doing reexec\n");
    // A reasonably large stack size. It is bigger than the usual 8Mb, because,
    // well, the program could have been run with unlimited stack for a reason.
    SetStackSizeLimitInBytes(32 * 1024 * 1024);
    ReExec();
  }

  __msan_clear_on_return();
  if (__msan_get_track_origins())
    VPrintf(1, "msan_track_origins\n");
  if (!InitShadowWithReExec(__msan_get_track_origins())) {
    Printf("FATAL: MemorySanitizer can not mmap the shadow memory.\n");
    Printf("FATAL: Make sure to compile with -fPIE and to link with -pie.\n");
    Printf("FATAL: Disabling ASLR is known to cause this error.\n");
    Printf("FATAL: If running under GDB, try "
           "'set disable-randomization off'.\n");
    DumpProcessMap();
    Die();
  }

  Symbolizer::GetOrInit()->AddHooks(EnterSymbolizerOrUnwider,
                                    ExitSymbolizerOrUnwider);

  InitializeCoverage(common_flags()->coverage, common_flags()->coverage_dir);

  MsanTSDInit(MsanTSDDtor);

  MsanAllocatorInit();

  MsanThread *main_thread = MsanThread::Create(nullptr, nullptr);
  SetCurrentThread(main_thread);
  main_thread->Init();

#if MSAN_CONTAINS_UBSAN
  __ubsan::InitAsPlugin();
#endif

  VPrintf(1, "MemorySanitizer init done\n");

  msan_init_is_running = 0;
  msan_inited = 1;
}

void __msan_set_keep_going(int keep_going) {
  flags()->halt_on_error = !keep_going;
}

void __msan_set_expect_umr(int expect_umr) {
  if (expect_umr) {
    msan_expected_umr_found = 0;
  } else if (!msan_expected_umr_found) {
    GET_CALLER_PC_BP;
    GET_FATAL_STACK_TRACE_PC_BP(pc, bp);
    ReportExpectedUMRNotFound(&stack);
    Die();
  }
  msan_expect_umr = expect_umr;
}

void __msan_print_shadow(const void *x, uptr size) {
  if (!MEM_IS_APP(x)) {
    Printf("Not a valid application address: %p\n", x);
    return;
  }

  DescribeMemoryRange(x, size);
}

void __msan_dump_shadow(const void *x, uptr size) {
  if (!MEM_IS_APP(x)) {
    Printf("Not a valid application address: %p\n", x);
    return;
  }

  unsigned char *s = (unsigned char*)MEM_TO_SHADOW(x);
  Printf("%p[%p]  ", (void *)s, x);
  for (uptr i = 0; i < size; i++)
    Printf("%x%x ", s[i] >> 4, s[i] & 0xf);
  Printf("\n");
}

sptr __msan_test_shadow(const void *x, uptr size) {
  if (!MEM_IS_APP(x)) return -1;
  unsigned char *s = (unsigned char *)MEM_TO_SHADOW((uptr)x);
  if (__sanitizer::mem_is_zero((const char *)s, size))
    return -1;
  // Slow path: loop through again to find the location.
  for (uptr i = 0; i < size; ++i)
    if (s[i])
      return i;
  return -1;
}

void __msan_check_mem_is_initialized(const void *x, uptr size) {
  if (!__msan::flags()->report_umrs) return;
  sptr offset = __msan_test_shadow(x, size);
  if (offset < 0)
    return;

  GET_CALLER_PC_BP;
  ReportUMRInsideAddressRange(__func__, x, size, offset);
  __msan::PrintWarningWithOrigin(pc, bp,
                                 __msan_get_origin(((const char *)x) + offset));
  if (__msan::flags()->halt_on_error) {
    Printf("Exiting\n");
    Die();
  }
}

int __msan_set_poison_in_malloc(int do_poison) {
  int old = flags()->poison_in_malloc;
  flags()->poison_in_malloc = do_poison;
  return old;
}

int __msan_has_dynamic_component() { return false; }

NOINLINE
void __msan_clear_on_return() {
  __msan_param_tls[0] = 0;
}

void __msan_partial_poison(const void* data, void* shadow, uptr size) {
  internal_memcpy((void*)MEM_TO_SHADOW((uptr)data), shadow, size);
}

void __msan_load_unpoisoned(const void *src, uptr size, void *dst) {
  internal_memcpy(dst, src, size);
  __msan_unpoison(dst, size);
}

void __msan_set_origin(const void *a, uptr size, u32 origin) {
  if (__msan_get_track_origins()) SetOrigin(a, size, origin);
}

void __msan_set_alloca_origin(void *a, uptr size, char *descr) {
  SetAllocaOrigin(a, size, reinterpret_cast<u32 *>(descr), descr + 4,
                  GET_CALLER_PC());
}

void __msan_set_alloca_origin4(void *a, uptr size, char *descr, uptr pc) {
  // Intentionally ignore pc and use return address. This function is here for
  // compatibility, in case program is linked with library instrumented by
  // older clang.
  SetAllocaOrigin(a, size, reinterpret_cast<u32 *>(descr), descr + 4,
                  GET_CALLER_PC());
}

void __msan_set_alloca_origin_with_descr(void *a, uptr size, u32 *id_ptr,
                                         char *descr) {
  SetAllocaOrigin(a, size, id_ptr, descr, GET_CALLER_PC());
}

void __msan_set_alloca_origin_no_descr(void *a, uptr size, u32 *id_ptr) {
  SetAllocaOrigin(a, size, id_ptr, nullptr, GET_CALLER_PC());
}

u32 __msan_chain_origin(u32 id) {
  GET_CALLER_PC_BP;
  GET_STORE_STACK_TRACE_PC_BP(pc, bp);
  return ChainOrigin(id, &stack);
}

u32 __msan_get_origin(const void *a) {
  if (!__msan_get_track_origins()) return 0;
  uptr x = (uptr)a;
  uptr aligned = x & ~3ULL;
  uptr origin_ptr = MEM_TO_ORIGIN(aligned);
  return *(u32*)origin_ptr;
}

int __msan_origin_is_descendant_or_same(u32 this_id, u32 prev_id) {
  Origin o = Origin::FromRawId(this_id);
  while (o.raw_id() != prev_id && o.isChainedOrigin())
    o = o.getNextChainedOrigin(nullptr);
  return o.raw_id() == prev_id;
}

u32 __msan_get_umr_origin() {
  return __msan_origin_tls;
}

u16 __sanitizer_unaligned_load16(const uu16 *p) {
  internal_memcpy(&__msan_retval_tls[0], (void *)MEM_TO_SHADOW((uptr)p),
                  sizeof(uu16));
  if (__msan_get_track_origins())
    __msan_retval_origin_tls = GetOriginIfPoisoned((uptr)p, sizeof(*p));
  return *p;
}
u32 __sanitizer_unaligned_load32(const uu32 *p) {
  internal_memcpy(&__msan_retval_tls[0], (void *)MEM_TO_SHADOW((uptr)p),
                  sizeof(uu32));
  if (__msan_get_track_origins())
    __msan_retval_origin_tls = GetOriginIfPoisoned((uptr)p, sizeof(*p));
  return *p;
}
u64 __sanitizer_unaligned_load64(const uu64 *p) {
  internal_memcpy(&__msan_retval_tls[0], (void *)MEM_TO_SHADOW((uptr)p),
                  sizeof(uu64));
  if (__msan_get_track_origins())
    __msan_retval_origin_tls = GetOriginIfPoisoned((uptr)p, sizeof(*p));
  return *p;
}
void __sanitizer_unaligned_store16(uu16 *p, u16 x) {
  static_assert(sizeof(uu16) == sizeof(u16), "incompatible types");
  u16 s;
  internal_memcpy(&s, &__msan_param_tls[1], sizeof(uu16));
  internal_memcpy((void *)MEM_TO_SHADOW((uptr)p), &s, sizeof(uu16));
  if (s && __msan_get_track_origins())
    if (uu32 o = __msan_param_origin_tls[2])
      SetOriginIfPoisoned((uptr)p, (uptr)&s, sizeof(s), o);
  *p = x;
}
void __sanitizer_unaligned_store32(uu32 *p, u32 x) {
  static_assert(sizeof(uu32) == sizeof(u32), "incompatible types");
  u32 s;
  internal_memcpy(&s, &__msan_param_tls[1], sizeof(uu32));
  internal_memcpy((void *)MEM_TO_SHADOW((uptr)p), &s, sizeof(uu32));
  if (s && __msan_get_track_origins())
    if (uu32 o = __msan_param_origin_tls[2])
      SetOriginIfPoisoned((uptr)p, (uptr)&s, sizeof(s), o);
  *p = x;
}
void __sanitizer_unaligned_store64(uu64 *p, u64 x) {
  u64 s = __msan_param_tls[1];
  *(uu64 *)MEM_TO_SHADOW((uptr)p) = s;
  if (s && __msan_get_track_origins())
    if (uu32 o = __msan_param_origin_tls[2])
      SetOriginIfPoisoned((uptr)p, (uptr)&s, sizeof(s), o);
  *p = x;
}

void __msan_set_death_callback(void (*callback)(void)) {
  SetUserDieCallback(callback);
}

void __msan_start_switch_fiber(const void *bottom, uptr size) {
  MsanThread *t = GetCurrentThread();
  if (!t) {
    VReport(1, "__msan_start_switch_fiber called from unknown thread\n");
    return;
  }
  t->StartSwitchFiber((uptr)bottom, size);
}

void __msan_finish_switch_fiber(const void **bottom_old, uptr *size_old) {
  MsanThread *t = GetCurrentThread();
  if (!t) {
    VReport(1, "__msan_finish_switch_fiber called from unknown thread\n");
    return;
  }
  t->FinishSwitchFiber((uptr *)bottom_old, (uptr *)size_old);

  internal_memset(__msan_param_tls, 0, sizeof(__msan_param_tls));
  internal_memset(__msan_retval_tls, 0, sizeof(__msan_retval_tls));
  internal_memset(__msan_va_arg_tls, 0, sizeof(__msan_va_arg_tls));

  if (__msan_get_track_origins()) {
    internal_memset(__msan_param_origin_tls, 0,
                    sizeof(__msan_param_origin_tls));
    internal_memset(&__msan_retval_origin_tls, 0,
                    sizeof(__msan_retval_origin_tls));
    internal_memset(__msan_va_arg_origin_tls, 0,
                    sizeof(__msan_va_arg_origin_tls));
  }
}

SANITIZER_INTERFACE_WEAK_DEF(const char *, __msan_default_options, void) {
  return "";
}

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_print_stack_trace() {
  GET_FATAL_STACK_TRACE_PC_BP(StackTrace::GetCurrentPc(), GET_CURRENT_FRAME());
  stack.Print();
}
} // extern "C"
