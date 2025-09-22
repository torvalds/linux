//===- MemorySanitizer.cpp - detector of uninitialized reads --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file is a part of MemorySanitizer, a detector of uninitialized
/// reads.
///
/// The algorithm of the tool is similar to Memcheck
/// (https://static.usenix.org/event/usenix05/tech/general/full_papers/seward/seward_html/usenix2005.html)
/// We associate a few shadow bits with every byte of the application memory,
/// poison the shadow of the malloc-ed or alloca-ed memory, load the shadow,
/// bits on every memory read, propagate the shadow bits through some of the
/// arithmetic instruction (including MOV), store the shadow bits on every memory
/// write, report a bug on some other instructions (e.g. JMP) if the
/// associated shadow is poisoned.
///
/// But there are differences too. The first and the major one:
/// compiler instrumentation instead of binary instrumentation. This
/// gives us much better register allocation, possible compiler
/// optimizations and a fast start-up. But this brings the major issue
/// as well: msan needs to see all program events, including system
/// calls and reads/writes in system libraries, so we either need to
/// compile *everything* with msan or use a binary translation
/// component (e.g. DynamoRIO) to instrument pre-built libraries.
/// Another difference from Memcheck is that we use 8 shadow bits per
/// byte of application memory and use a direct shadow mapping. This
/// greatly simplifies the instrumentation code and avoids races on
/// shadow updates (Memcheck is single-threaded so races are not a
/// concern there. Memcheck uses 2 shadow bits per byte with a slow
/// path storage that uses 8 bits per byte).
///
/// The default value of shadow is 0, which means "clean" (not poisoned).
///
/// Every module initializer should call __msan_init to ensure that the
/// shadow memory is ready. On error, __msan_warning is called. Since
/// parameters and return values may be passed via registers, we have a
/// specialized thread-local shadow for return values
/// (__msan_retval_tls) and parameters (__msan_param_tls).
///
///                           Origin tracking.
///
/// MemorySanitizer can track origins (allocation points) of all uninitialized
/// values. This behavior is controlled with a flag (msan-track-origins) and is
/// disabled by default.
///
/// Origins are 4-byte values created and interpreted by the runtime library.
/// They are stored in a second shadow mapping, one 4-byte value for 4 bytes
/// of application memory. Propagation of origins is basically a bunch of
/// "select" instructions that pick the origin of a dirty argument, if an
/// instruction has one.
///
/// Every 4 aligned, consecutive bytes of application memory have one origin
/// value associated with them. If these bytes contain uninitialized data
/// coming from 2 different allocations, the last store wins. Because of this,
/// MemorySanitizer reports can show unrelated origins, but this is unlikely in
/// practice.
///
/// Origins are meaningless for fully initialized values, so MemorySanitizer
/// avoids storing origin to memory when a fully initialized value is stored.
/// This way it avoids needless overwriting origin of the 4-byte region on
/// a short (i.e. 1 byte) clean store, and it is also good for performance.
///
///                            Atomic handling.
///
/// Ideally, every atomic store of application value should update the
/// corresponding shadow location in an atomic way. Unfortunately, atomic store
/// of two disjoint locations can not be done without severe slowdown.
///
/// Therefore, we implement an approximation that may err on the safe side.
/// In this implementation, every atomically accessed location in the program
/// may only change from (partially) uninitialized to fully initialized, but
/// not the other way around. We load the shadow _after_ the application load,
/// and we store the shadow _before_ the app store. Also, we always store clean
/// shadow (if the application store is atomic). This way, if the store-load
/// pair constitutes a happens-before arc, shadow store and load are correctly
/// ordered such that the load will get either the value that was stored, or
/// some later value (which is always clean).
///
/// This does not work very well with Compare-And-Swap (CAS) and
/// Read-Modify-Write (RMW) operations. To follow the above logic, CAS and RMW
/// must store the new shadow before the app operation, and load the shadow
/// after the app operation. Computers don't work this way. Current
/// implementation ignores the load aspect of CAS/RMW, always returning a clean
/// value. It implements the store part as a simple atomic store by storing a
/// clean shadow.
///
///                      Instrumenting inline assembly.
///
/// For inline assembly code LLVM has little idea about which memory locations
/// become initialized depending on the arguments. It can be possible to figure
/// out which arguments are meant to point to inputs and outputs, but the
/// actual semantics can be only visible at runtime. In the Linux kernel it's
/// also possible that the arguments only indicate the offset for a base taken
/// from a segment register, so it's dangerous to treat any asm() arguments as
/// pointers. We take a conservative approach generating calls to
///   __msan_instrument_asm_store(ptr, size)
/// , which defer the memory unpoisoning to the runtime library.
/// The latter can perform more complex address checks to figure out whether
/// it's safe to touch the shadow memory.
/// Like with atomic operations, we call __msan_instrument_asm_store() before
/// the assembly call, so that changes to the shadow memory will be seen by
/// other threads together with main memory initialization.
///
///                  KernelMemorySanitizer (KMSAN) implementation.
///
/// The major differences between KMSAN and MSan instrumentation are:
///  - KMSAN always tracks the origins and implies msan-keep-going=true;
///  - KMSAN allocates shadow and origin memory for each page separately, so
///    there are no explicit accesses to shadow and origin in the
///    instrumentation.
///    Shadow and origin values for a particular X-byte memory location
///    (X=1,2,4,8) are accessed through pointers obtained via the
///      __msan_metadata_ptr_for_load_X(ptr)
///      __msan_metadata_ptr_for_store_X(ptr)
///    functions. The corresponding functions check that the X-byte accesses
///    are possible and returns the pointers to shadow and origin memory.
///    Arbitrary sized accesses are handled with:
///      __msan_metadata_ptr_for_load_n(ptr, size)
///      __msan_metadata_ptr_for_store_n(ptr, size);
///    Note that the sanitizer code has to deal with how shadow/origin pairs
///    returned by the these functions are represented in different ABIs. In
///    the X86_64 ABI they are returned in RDX:RAX, in PowerPC64 they are
///    returned in r3 and r4, and in the SystemZ ABI they are written to memory
///    pointed to by a hidden parameter.
///  - TLS variables are stored in a single per-task struct. A call to a
///    function __msan_get_context_state() returning a pointer to that struct
///    is inserted into every instrumented function before the entry block;
///  - __msan_warning() takes a 32-bit origin parameter;
///  - local variables are poisoned with __msan_poison_alloca() upon function
///    entry and unpoisoned with __msan_unpoison_alloca() before leaving the
///    function;
///  - the pass doesn't declare any global variables or add global constructors
///    to the translation unit.
///
/// Also, KMSAN currently ignores uninitialized memory passed into inline asm
/// calls, making sure we're on the safe side wrt. possible false positives.
///
///  KernelMemorySanitizer only supports X86_64, SystemZ and PowerPC64 at the
///  moment.
///
//
// FIXME: This sanitizer does not yet handle scalable vectors
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/MemorySanitizer.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/AttributeMask.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAArch64.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>

using namespace llvm;

#define DEBUG_TYPE "msan"

DEBUG_COUNTER(DebugInsertCheck, "msan-insert-check",
              "Controls which checks to insert");

DEBUG_COUNTER(DebugInstrumentInstruction, "msan-instrument-instruction",
              "Controls which instruction to instrument");

static const unsigned kOriginSize = 4;
static const Align kMinOriginAlignment = Align(4);
static const Align kShadowTLSAlignment = Align(8);

// These constants must be kept in sync with the ones in msan.h.
static const unsigned kParamTLSSize = 800;
static const unsigned kRetvalTLSSize = 800;

// Accesses sizes are powers of two: 1, 2, 4, 8.
static const size_t kNumberOfAccessSizes = 4;

/// Track origins of uninitialized values.
///
/// Adds a section to MemorySanitizer report that points to the allocation
/// (stack or heap) the uninitialized bits came from originally.
static cl::opt<int> ClTrackOrigins(
    "msan-track-origins",
    cl::desc("Track origins (allocation sites) of poisoned memory"), cl::Hidden,
    cl::init(0));

static cl::opt<bool> ClKeepGoing("msan-keep-going",
                                 cl::desc("keep going after reporting a UMR"),
                                 cl::Hidden, cl::init(false));

static cl::opt<bool>
    ClPoisonStack("msan-poison-stack",
                  cl::desc("poison uninitialized stack variables"), cl::Hidden,
                  cl::init(true));

static cl::opt<bool> ClPoisonStackWithCall(
    "msan-poison-stack-with-call",
    cl::desc("poison uninitialized stack variables with a call"), cl::Hidden,
    cl::init(false));

static cl::opt<int> ClPoisonStackPattern(
    "msan-poison-stack-pattern",
    cl::desc("poison uninitialized stack variables with the given pattern"),
    cl::Hidden, cl::init(0xff));

static cl::opt<bool>
    ClPrintStackNames("msan-print-stack-names",
                      cl::desc("Print name of local stack variable"),
                      cl::Hidden, cl::init(true));

static cl::opt<bool> ClPoisonUndef("msan-poison-undef",
                                   cl::desc("poison undef temps"), cl::Hidden,
                                   cl::init(true));

static cl::opt<bool>
    ClHandleICmp("msan-handle-icmp",
                 cl::desc("propagate shadow through ICmpEQ and ICmpNE"),
                 cl::Hidden, cl::init(true));

static cl::opt<bool>
    ClHandleICmpExact("msan-handle-icmp-exact",
                      cl::desc("exact handling of relational integer ICmp"),
                      cl::Hidden, cl::init(false));

static cl::opt<bool> ClHandleLifetimeIntrinsics(
    "msan-handle-lifetime-intrinsics",
    cl::desc(
        "when possible, poison scoped variables at the beginning of the scope "
        "(slower, but more precise)"),
    cl::Hidden, cl::init(true));

// When compiling the Linux kernel, we sometimes see false positives related to
// MSan being unable to understand that inline assembly calls may initialize
// local variables.
// This flag makes the compiler conservatively unpoison every memory location
// passed into an assembly call. Note that this may cause false positives.
// Because it's impossible to figure out the array sizes, we can only unpoison
// the first sizeof(type) bytes for each type* pointer.
static cl::opt<bool> ClHandleAsmConservative(
    "msan-handle-asm-conservative",
    cl::desc("conservative handling of inline assembly"), cl::Hidden,
    cl::init(true));

// This flag controls whether we check the shadow of the address
// operand of load or store. Such bugs are very rare, since load from
// a garbage address typically results in SEGV, but still happen
// (e.g. only lower bits of address are garbage, or the access happens
// early at program startup where malloc-ed memory is more likely to
// be zeroed. As of 2012-08-28 this flag adds 20% slowdown.
static cl::opt<bool> ClCheckAccessAddress(
    "msan-check-access-address",
    cl::desc("report accesses through a pointer which has poisoned shadow"),
    cl::Hidden, cl::init(true));

static cl::opt<bool> ClEagerChecks(
    "msan-eager-checks",
    cl::desc("check arguments and return values at function call boundaries"),
    cl::Hidden, cl::init(false));

static cl::opt<bool> ClDumpStrictInstructions(
    "msan-dump-strict-instructions",
    cl::desc("print out instructions with default strict semantics"),
    cl::Hidden, cl::init(false));

static cl::opt<int> ClInstrumentationWithCallThreshold(
    "msan-instrumentation-with-call-threshold",
    cl::desc(
        "If the function being instrumented requires more than "
        "this number of checks and origin stores, use callbacks instead of "
        "inline checks (-1 means never use callbacks)."),
    cl::Hidden, cl::init(3500));

static cl::opt<bool>
    ClEnableKmsan("msan-kernel",
                  cl::desc("Enable KernelMemorySanitizer instrumentation"),
                  cl::Hidden, cl::init(false));

static cl::opt<bool>
    ClDisableChecks("msan-disable-checks",
                    cl::desc("Apply no_sanitize to the whole file"), cl::Hidden,
                    cl::init(false));

static cl::opt<bool>
    ClCheckConstantShadow("msan-check-constant-shadow",
                          cl::desc("Insert checks for constant shadow values"),
                          cl::Hidden, cl::init(true));

// This is off by default because of a bug in gold:
// https://sourceware.org/bugzilla/show_bug.cgi?id=19002
static cl::opt<bool>
    ClWithComdat("msan-with-comdat",
                 cl::desc("Place MSan constructors in comdat sections"),
                 cl::Hidden, cl::init(false));

// These options allow to specify custom memory map parameters
// See MemoryMapParams for details.
static cl::opt<uint64_t> ClAndMask("msan-and-mask",
                                   cl::desc("Define custom MSan AndMask"),
                                   cl::Hidden, cl::init(0));

static cl::opt<uint64_t> ClXorMask("msan-xor-mask",
                                   cl::desc("Define custom MSan XorMask"),
                                   cl::Hidden, cl::init(0));

static cl::opt<uint64_t> ClShadowBase("msan-shadow-base",
                                      cl::desc("Define custom MSan ShadowBase"),
                                      cl::Hidden, cl::init(0));

static cl::opt<uint64_t> ClOriginBase("msan-origin-base",
                                      cl::desc("Define custom MSan OriginBase"),
                                      cl::Hidden, cl::init(0));

static cl::opt<int>
    ClDisambiguateWarning("msan-disambiguate-warning-threshold",
                          cl::desc("Define threshold for number of checks per "
                                   "debug location to force origin update."),
                          cl::Hidden, cl::init(3));

const char kMsanModuleCtorName[] = "msan.module_ctor";
const char kMsanInitName[] = "__msan_init";

namespace {

// Memory map parameters used in application-to-shadow address calculation.
// Offset = (Addr & ~AndMask) ^ XorMask
// Shadow = ShadowBase + Offset
// Origin = OriginBase + Offset
struct MemoryMapParams {
  uint64_t AndMask;
  uint64_t XorMask;
  uint64_t ShadowBase;
  uint64_t OriginBase;
};

struct PlatformMemoryMapParams {
  const MemoryMapParams *bits32;
  const MemoryMapParams *bits64;
};

} // end anonymous namespace

// i386 Linux
static const MemoryMapParams Linux_I386_MemoryMapParams = {
    0x000080000000, // AndMask
    0,              // XorMask (not used)
    0,              // ShadowBase (not used)
    0x000040000000, // OriginBase
};

// x86_64 Linux
static const MemoryMapParams Linux_X86_64_MemoryMapParams = {
    0,              // AndMask (not used)
    0x500000000000, // XorMask
    0,              // ShadowBase (not used)
    0x100000000000, // OriginBase
};

// mips64 Linux
static const MemoryMapParams Linux_MIPS64_MemoryMapParams = {
    0,              // AndMask (not used)
    0x008000000000, // XorMask
    0,              // ShadowBase (not used)
    0x002000000000, // OriginBase
};

// ppc64 Linux
static const MemoryMapParams Linux_PowerPC64_MemoryMapParams = {
    0xE00000000000, // AndMask
    0x100000000000, // XorMask
    0x080000000000, // ShadowBase
    0x1C0000000000, // OriginBase
};

// s390x Linux
static const MemoryMapParams Linux_S390X_MemoryMapParams = {
    0xC00000000000, // AndMask
    0,              // XorMask (not used)
    0x080000000000, // ShadowBase
    0x1C0000000000, // OriginBase
};

// aarch64 Linux
static const MemoryMapParams Linux_AArch64_MemoryMapParams = {
    0,               // AndMask (not used)
    0x0B00000000000, // XorMask
    0,               // ShadowBase (not used)
    0x0200000000000, // OriginBase
};

// loongarch64 Linux
static const MemoryMapParams Linux_LoongArch64_MemoryMapParams = {
    0,              // AndMask (not used)
    0x500000000000, // XorMask
    0,              // ShadowBase (not used)
    0x100000000000, // OriginBase
};

// aarch64 FreeBSD
static const MemoryMapParams FreeBSD_AArch64_MemoryMapParams = {
    0x1800000000000, // AndMask
    0x0400000000000, // XorMask
    0x0200000000000, // ShadowBase
    0x0700000000000, // OriginBase
};

// i386 FreeBSD
static const MemoryMapParams FreeBSD_I386_MemoryMapParams = {
    0x000180000000, // AndMask
    0x000040000000, // XorMask
    0x000020000000, // ShadowBase
    0x000700000000, // OriginBase
};

// x86_64 FreeBSD
static const MemoryMapParams FreeBSD_X86_64_MemoryMapParams = {
    0xc00000000000, // AndMask
    0x200000000000, // XorMask
    0x100000000000, // ShadowBase
    0x380000000000, // OriginBase
};

// x86_64 NetBSD
static const MemoryMapParams NetBSD_X86_64_MemoryMapParams = {
    0,              // AndMask
    0x500000000000, // XorMask
    0,              // ShadowBase
    0x100000000000, // OriginBase
};

static const PlatformMemoryMapParams Linux_X86_MemoryMapParams = {
    &Linux_I386_MemoryMapParams,
    &Linux_X86_64_MemoryMapParams,
};

static const PlatformMemoryMapParams Linux_MIPS_MemoryMapParams = {
    nullptr,
    &Linux_MIPS64_MemoryMapParams,
};

static const PlatformMemoryMapParams Linux_PowerPC_MemoryMapParams = {
    nullptr,
    &Linux_PowerPC64_MemoryMapParams,
};

static const PlatformMemoryMapParams Linux_S390_MemoryMapParams = {
    nullptr,
    &Linux_S390X_MemoryMapParams,
};

static const PlatformMemoryMapParams Linux_ARM_MemoryMapParams = {
    nullptr,
    &Linux_AArch64_MemoryMapParams,
};

static const PlatformMemoryMapParams Linux_LoongArch_MemoryMapParams = {
    nullptr,
    &Linux_LoongArch64_MemoryMapParams,
};

static const PlatformMemoryMapParams FreeBSD_ARM_MemoryMapParams = {
    nullptr,
    &FreeBSD_AArch64_MemoryMapParams,
};

static const PlatformMemoryMapParams FreeBSD_X86_MemoryMapParams = {
    &FreeBSD_I386_MemoryMapParams,
    &FreeBSD_X86_64_MemoryMapParams,
};

static const PlatformMemoryMapParams NetBSD_X86_MemoryMapParams = {
    nullptr,
    &NetBSD_X86_64_MemoryMapParams,
};

namespace {

/// Instrument functions of a module to detect uninitialized reads.
///
/// Instantiating MemorySanitizer inserts the msan runtime library API function
/// declarations into the module if they don't exist already. Instantiating
/// ensures the __msan_init function is in the list of global constructors for
/// the module.
class MemorySanitizer {
public:
  MemorySanitizer(Module &M, MemorySanitizerOptions Options)
      : CompileKernel(Options.Kernel), TrackOrigins(Options.TrackOrigins),
        Recover(Options.Recover), EagerChecks(Options.EagerChecks) {
    initializeModule(M);
  }

  // MSan cannot be moved or copied because of MapParams.
  MemorySanitizer(MemorySanitizer &&) = delete;
  MemorySanitizer &operator=(MemorySanitizer &&) = delete;
  MemorySanitizer(const MemorySanitizer &) = delete;
  MemorySanitizer &operator=(const MemorySanitizer &) = delete;

  bool sanitizeFunction(Function &F, TargetLibraryInfo &TLI);

private:
  friend struct MemorySanitizerVisitor;
  friend struct VarArgHelperBase;
  friend struct VarArgAMD64Helper;
  friend struct VarArgMIPS64Helper;
  friend struct VarArgAArch64Helper;
  friend struct VarArgPowerPC64Helper;
  friend struct VarArgSystemZHelper;

  void initializeModule(Module &M);
  void initializeCallbacks(Module &M, const TargetLibraryInfo &TLI);
  void createKernelApi(Module &M, const TargetLibraryInfo &TLI);
  void createUserspaceApi(Module &M, const TargetLibraryInfo &TLI);

  template <typename... ArgsTy>
  FunctionCallee getOrInsertMsanMetadataFunction(Module &M, StringRef Name,
                                                 ArgsTy... Args);

  /// True if we're compiling the Linux kernel.
  bool CompileKernel;
  /// Track origins (allocation points) of uninitialized values.
  int TrackOrigins;
  bool Recover;
  bool EagerChecks;

  Triple TargetTriple;
  LLVMContext *C;
  Type *IntptrTy;  ///< Integer type with the size of a ptr in default AS.
  Type *OriginTy;
  PointerType *PtrTy; ///< Integer type with the size of a ptr in default AS.

  // XxxTLS variables represent the per-thread state in MSan and per-task state
  // in KMSAN.
  // For the userspace these point to thread-local globals. In the kernel land
  // they point to the members of a per-task struct obtained via a call to
  // __msan_get_context_state().

  /// Thread-local shadow storage for function parameters.
  Value *ParamTLS;

  /// Thread-local origin storage for function parameters.
  Value *ParamOriginTLS;

  /// Thread-local shadow storage for function return value.
  Value *RetvalTLS;

  /// Thread-local origin storage for function return value.
  Value *RetvalOriginTLS;

  /// Thread-local shadow storage for in-register va_arg function.
  Value *VAArgTLS;

  /// Thread-local shadow storage for in-register va_arg function.
  Value *VAArgOriginTLS;

  /// Thread-local shadow storage for va_arg overflow area.
  Value *VAArgOverflowSizeTLS;

  /// Are the instrumentation callbacks set up?
  bool CallbacksInitialized = false;

  /// The run-time callback to print a warning.
  FunctionCallee WarningFn;

  // These arrays are indexed by log2(AccessSize).
  FunctionCallee MaybeWarningFn[kNumberOfAccessSizes];
  FunctionCallee MaybeStoreOriginFn[kNumberOfAccessSizes];

  /// Run-time helper that generates a new origin value for a stack
  /// allocation.
  FunctionCallee MsanSetAllocaOriginWithDescriptionFn;
  // No description version
  FunctionCallee MsanSetAllocaOriginNoDescriptionFn;

  /// Run-time helper that poisons stack on function entry.
  FunctionCallee MsanPoisonStackFn;

  /// Run-time helper that records a store (or any event) of an
  /// uninitialized value and returns an updated origin id encoding this info.
  FunctionCallee MsanChainOriginFn;

  /// Run-time helper that paints an origin over a region.
  FunctionCallee MsanSetOriginFn;

  /// MSan runtime replacements for memmove, memcpy and memset.
  FunctionCallee MemmoveFn, MemcpyFn, MemsetFn;

  /// KMSAN callback for task-local function argument shadow.
  StructType *MsanContextStateTy;
  FunctionCallee MsanGetContextStateFn;

  /// Functions for poisoning/unpoisoning local variables
  FunctionCallee MsanPoisonAllocaFn, MsanUnpoisonAllocaFn;

  /// Pair of shadow/origin pointers.
  Type *MsanMetadata;

  /// Each of the MsanMetadataPtrXxx functions returns a MsanMetadata.
  FunctionCallee MsanMetadataPtrForLoadN, MsanMetadataPtrForStoreN;
  FunctionCallee MsanMetadataPtrForLoad_1_8[4];
  FunctionCallee MsanMetadataPtrForStore_1_8[4];
  FunctionCallee MsanInstrumentAsmStoreFn;

  /// Storage for return values of the MsanMetadataPtrXxx functions.
  Value *MsanMetadataAlloca;

  /// Helper to choose between different MsanMetadataPtrXxx().
  FunctionCallee getKmsanShadowOriginAccessFn(bool isStore, int size);

  /// Memory map parameters used in application-to-shadow calculation.
  const MemoryMapParams *MapParams;

  /// Custom memory map parameters used when -msan-shadow-base or
  // -msan-origin-base is provided.
  MemoryMapParams CustomMapParams;

  MDNode *ColdCallWeights;

  /// Branch weights for origin store.
  MDNode *OriginStoreWeights;
};

void insertModuleCtor(Module &M) {
  getOrCreateSanitizerCtorAndInitFunctions(
      M, kMsanModuleCtorName, kMsanInitName,
      /*InitArgTypes=*/{},
      /*InitArgs=*/{},
      // This callback is invoked when the functions are created the first
      // time. Hook them into the global ctors list in that case:
      [&](Function *Ctor, FunctionCallee) {
        if (!ClWithComdat) {
          appendToGlobalCtors(M, Ctor, 0);
          return;
        }
        Comdat *MsanCtorComdat = M.getOrInsertComdat(kMsanModuleCtorName);
        Ctor->setComdat(MsanCtorComdat);
        appendToGlobalCtors(M, Ctor, 0, Ctor);
      });
}

template <class T> T getOptOrDefault(const cl::opt<T> &Opt, T Default) {
  return (Opt.getNumOccurrences() > 0) ? Opt : Default;
}

} // end anonymous namespace

MemorySanitizerOptions::MemorySanitizerOptions(int TO, bool R, bool K,
                                               bool EagerChecks)
    : Kernel(getOptOrDefault(ClEnableKmsan, K)),
      TrackOrigins(getOptOrDefault(ClTrackOrigins, Kernel ? 2 : TO)),
      Recover(getOptOrDefault(ClKeepGoing, Kernel || R)),
      EagerChecks(getOptOrDefault(ClEagerChecks, EagerChecks)) {}

PreservedAnalyses MemorySanitizerPass::run(Module &M,
                                           ModuleAnalysisManager &AM) {
  bool Modified = false;
  if (!Options.Kernel) {
    insertModuleCtor(M);
    Modified = true;
  }

  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  for (Function &F : M) {
    if (F.empty())
      continue;
    MemorySanitizer Msan(*F.getParent(), Options);
    Modified |=
        Msan.sanitizeFunction(F, FAM.getResult<TargetLibraryAnalysis>(F));
  }

  if (!Modified)
    return PreservedAnalyses::all();

  PreservedAnalyses PA = PreservedAnalyses::none();
  // GlobalsAA is considered stateless and does not get invalidated unless
  // explicitly invalidated; PreservedAnalyses::none() is not enough. Sanitizers
  // make changes that require GlobalsAA to be invalidated.
  PA.abandon<GlobalsAA>();
  return PA;
}

void MemorySanitizerPass::printPipeline(
    raw_ostream &OS, function_ref<StringRef(StringRef)> MapClassName2PassName) {
  static_cast<PassInfoMixin<MemorySanitizerPass> *>(this)->printPipeline(
      OS, MapClassName2PassName);
  OS << '<';
  if (Options.Recover)
    OS << "recover;";
  if (Options.Kernel)
    OS << "kernel;";
  if (Options.EagerChecks)
    OS << "eager-checks;";
  OS << "track-origins=" << Options.TrackOrigins;
  OS << '>';
}

/// Create a non-const global initialized with the given string.
///
/// Creates a writable global for Str so that we can pass it to the
/// run-time lib. Runtime uses first 4 bytes of the string to store the
/// frame ID, so the string needs to be mutable.
static GlobalVariable *createPrivateConstGlobalForString(Module &M,
                                                         StringRef Str) {
  Constant *StrConst = ConstantDataArray::getString(M.getContext(), Str);
  return new GlobalVariable(M, StrConst->getType(), /*isConstant=*/true,
                            GlobalValue::PrivateLinkage, StrConst, "");
}

template <typename... ArgsTy>
FunctionCallee
MemorySanitizer::getOrInsertMsanMetadataFunction(Module &M, StringRef Name,
                                                 ArgsTy... Args) {
  if (TargetTriple.getArch() == Triple::systemz) {
    // SystemZ ABI: shadow/origin pair is returned via a hidden parameter.
    return M.getOrInsertFunction(Name, Type::getVoidTy(*C),
                                 PointerType::get(MsanMetadata, 0),
                                 std::forward<ArgsTy>(Args)...);
  }

  return M.getOrInsertFunction(Name, MsanMetadata,
                               std::forward<ArgsTy>(Args)...);
}

/// Create KMSAN API callbacks.
void MemorySanitizer::createKernelApi(Module &M, const TargetLibraryInfo &TLI) {
  IRBuilder<> IRB(*C);

  // These will be initialized in insertKmsanPrologue().
  RetvalTLS = nullptr;
  RetvalOriginTLS = nullptr;
  ParamTLS = nullptr;
  ParamOriginTLS = nullptr;
  VAArgTLS = nullptr;
  VAArgOriginTLS = nullptr;
  VAArgOverflowSizeTLS = nullptr;

  WarningFn = M.getOrInsertFunction("__msan_warning",
                                    TLI.getAttrList(C, {0}, /*Signed=*/false),
                                    IRB.getVoidTy(), IRB.getInt32Ty());

  // Requests the per-task context state (kmsan_context_state*) from the
  // runtime library.
  MsanContextStateTy = StructType::get(
      ArrayType::get(IRB.getInt64Ty(), kParamTLSSize / 8),
      ArrayType::get(IRB.getInt64Ty(), kRetvalTLSSize / 8),
      ArrayType::get(IRB.getInt64Ty(), kParamTLSSize / 8),
      ArrayType::get(IRB.getInt64Ty(), kParamTLSSize / 8), /* va_arg_origin */
      IRB.getInt64Ty(), ArrayType::get(OriginTy, kParamTLSSize / 4), OriginTy,
      OriginTy);
  MsanGetContextStateFn = M.getOrInsertFunction(
      "__msan_get_context_state", PointerType::get(MsanContextStateTy, 0));

  MsanMetadata = StructType::get(PointerType::get(IRB.getInt8Ty(), 0),
                                 PointerType::get(IRB.getInt32Ty(), 0));

  for (int ind = 0, size = 1; ind < 4; ind++, size <<= 1) {
    std::string name_load =
        "__msan_metadata_ptr_for_load_" + std::to_string(size);
    std::string name_store =
        "__msan_metadata_ptr_for_store_" + std::to_string(size);
    MsanMetadataPtrForLoad_1_8[ind] = getOrInsertMsanMetadataFunction(
        M, name_load, PointerType::get(IRB.getInt8Ty(), 0));
    MsanMetadataPtrForStore_1_8[ind] = getOrInsertMsanMetadataFunction(
        M, name_store, PointerType::get(IRB.getInt8Ty(), 0));
  }

  MsanMetadataPtrForLoadN = getOrInsertMsanMetadataFunction(
      M, "__msan_metadata_ptr_for_load_n", PointerType::get(IRB.getInt8Ty(), 0),
      IRB.getInt64Ty());
  MsanMetadataPtrForStoreN = getOrInsertMsanMetadataFunction(
      M, "__msan_metadata_ptr_for_store_n",
      PointerType::get(IRB.getInt8Ty(), 0), IRB.getInt64Ty());

  // Functions for poisoning and unpoisoning memory.
  MsanPoisonAllocaFn = M.getOrInsertFunction(
      "__msan_poison_alloca", IRB.getVoidTy(), PtrTy, IntptrTy, PtrTy);
  MsanUnpoisonAllocaFn = M.getOrInsertFunction(
      "__msan_unpoison_alloca", IRB.getVoidTy(), PtrTy, IntptrTy);
}

static Constant *getOrInsertGlobal(Module &M, StringRef Name, Type *Ty) {
  return M.getOrInsertGlobal(Name, Ty, [&] {
    return new GlobalVariable(M, Ty, false, GlobalVariable::ExternalLinkage,
                              nullptr, Name, nullptr,
                              GlobalVariable::InitialExecTLSModel);
  });
}

/// Insert declarations for userspace-specific functions and globals.
void MemorySanitizer::createUserspaceApi(Module &M, const TargetLibraryInfo &TLI) {
  IRBuilder<> IRB(*C);

  // Create the callback.
  // FIXME: this function should have "Cold" calling conv,
  // which is not yet implemented.
  if (TrackOrigins) {
    StringRef WarningFnName = Recover ? "__msan_warning_with_origin"
                                      : "__msan_warning_with_origin_noreturn";
    WarningFn = M.getOrInsertFunction(WarningFnName,
                                      TLI.getAttrList(C, {0}, /*Signed=*/false),
                                      IRB.getVoidTy(), IRB.getInt32Ty());
  } else {
    StringRef WarningFnName =
        Recover ? "__msan_warning" : "__msan_warning_noreturn";
    WarningFn = M.getOrInsertFunction(WarningFnName, IRB.getVoidTy());
  }

  // Create the global TLS variables.
  RetvalTLS =
      getOrInsertGlobal(M, "__msan_retval_tls",
                        ArrayType::get(IRB.getInt64Ty(), kRetvalTLSSize / 8));

  RetvalOriginTLS = getOrInsertGlobal(M, "__msan_retval_origin_tls", OriginTy);

  ParamTLS =
      getOrInsertGlobal(M, "__msan_param_tls",
                        ArrayType::get(IRB.getInt64Ty(), kParamTLSSize / 8));

  ParamOriginTLS =
      getOrInsertGlobal(M, "__msan_param_origin_tls",
                        ArrayType::get(OriginTy, kParamTLSSize / 4));

  VAArgTLS =
      getOrInsertGlobal(M, "__msan_va_arg_tls",
                        ArrayType::get(IRB.getInt64Ty(), kParamTLSSize / 8));

  VAArgOriginTLS =
      getOrInsertGlobal(M, "__msan_va_arg_origin_tls",
                        ArrayType::get(OriginTy, kParamTLSSize / 4));

  VAArgOverflowSizeTLS =
      getOrInsertGlobal(M, "__msan_va_arg_overflow_size_tls", IRB.getInt64Ty());

  for (size_t AccessSizeIndex = 0; AccessSizeIndex < kNumberOfAccessSizes;
       AccessSizeIndex++) {
    unsigned AccessSize = 1 << AccessSizeIndex;
    std::string FunctionName = "__msan_maybe_warning_" + itostr(AccessSize);
    MaybeWarningFn[AccessSizeIndex] = M.getOrInsertFunction(
        FunctionName, TLI.getAttrList(C, {0, 1}, /*Signed=*/false),
        IRB.getVoidTy(), IRB.getIntNTy(AccessSize * 8), IRB.getInt32Ty());

    FunctionName = "__msan_maybe_store_origin_" + itostr(AccessSize);
    MaybeStoreOriginFn[AccessSizeIndex] = M.getOrInsertFunction(
        FunctionName, TLI.getAttrList(C, {0, 2}, /*Signed=*/false),
        IRB.getVoidTy(), IRB.getIntNTy(AccessSize * 8), PtrTy,
        IRB.getInt32Ty());
  }

  MsanSetAllocaOriginWithDescriptionFn =
      M.getOrInsertFunction("__msan_set_alloca_origin_with_descr",
                            IRB.getVoidTy(), PtrTy, IntptrTy, PtrTy, PtrTy);
  MsanSetAllocaOriginNoDescriptionFn =
      M.getOrInsertFunction("__msan_set_alloca_origin_no_descr",
                            IRB.getVoidTy(), PtrTy, IntptrTy, PtrTy);
  MsanPoisonStackFn = M.getOrInsertFunction("__msan_poison_stack",
                                            IRB.getVoidTy(), PtrTy, IntptrTy);
}

/// Insert extern declaration of runtime-provided functions and globals.
void MemorySanitizer::initializeCallbacks(Module &M, const TargetLibraryInfo &TLI) {
  // Only do this once.
  if (CallbacksInitialized)
    return;

  IRBuilder<> IRB(*C);
  // Initialize callbacks that are common for kernel and userspace
  // instrumentation.
  MsanChainOriginFn = M.getOrInsertFunction(
      "__msan_chain_origin",
      TLI.getAttrList(C, {0}, /*Signed=*/false, /*Ret=*/true), IRB.getInt32Ty(),
      IRB.getInt32Ty());
  MsanSetOriginFn = M.getOrInsertFunction(
      "__msan_set_origin", TLI.getAttrList(C, {2}, /*Signed=*/false),
      IRB.getVoidTy(), PtrTy, IntptrTy, IRB.getInt32Ty());
  MemmoveFn =
      M.getOrInsertFunction("__msan_memmove", PtrTy, PtrTy, PtrTy, IntptrTy);
  MemcpyFn =
      M.getOrInsertFunction("__msan_memcpy", PtrTy, PtrTy, PtrTy, IntptrTy);
  MemsetFn = M.getOrInsertFunction("__msan_memset",
                                   TLI.getAttrList(C, {1}, /*Signed=*/true),
                                   PtrTy, PtrTy, IRB.getInt32Ty(), IntptrTy);

  MsanInstrumentAsmStoreFn =
      M.getOrInsertFunction("__msan_instrument_asm_store", IRB.getVoidTy(),
                            PointerType::get(IRB.getInt8Ty(), 0), IntptrTy);

  if (CompileKernel) {
    createKernelApi(M, TLI);
  } else {
    createUserspaceApi(M, TLI);
  }
  CallbacksInitialized = true;
}

FunctionCallee MemorySanitizer::getKmsanShadowOriginAccessFn(bool isStore,
                                                             int size) {
  FunctionCallee *Fns =
      isStore ? MsanMetadataPtrForStore_1_8 : MsanMetadataPtrForLoad_1_8;
  switch (size) {
  case 1:
    return Fns[0];
  case 2:
    return Fns[1];
  case 4:
    return Fns[2];
  case 8:
    return Fns[3];
  default:
    return nullptr;
  }
}

/// Module-level initialization.
///
/// inserts a call to __msan_init to the module's constructor list.
void MemorySanitizer::initializeModule(Module &M) {
  auto &DL = M.getDataLayout();

  TargetTriple = Triple(M.getTargetTriple());

  bool ShadowPassed = ClShadowBase.getNumOccurrences() > 0;
  bool OriginPassed = ClOriginBase.getNumOccurrences() > 0;
  // Check the overrides first
  if (ShadowPassed || OriginPassed) {
    CustomMapParams.AndMask = ClAndMask;
    CustomMapParams.XorMask = ClXorMask;
    CustomMapParams.ShadowBase = ClShadowBase;
    CustomMapParams.OriginBase = ClOriginBase;
    MapParams = &CustomMapParams;
  } else {
    switch (TargetTriple.getOS()) {
    case Triple::FreeBSD:
      switch (TargetTriple.getArch()) {
      case Triple::aarch64:
        MapParams = FreeBSD_ARM_MemoryMapParams.bits64;
        break;
      case Triple::x86_64:
        MapParams = FreeBSD_X86_MemoryMapParams.bits64;
        break;
      case Triple::x86:
        MapParams = FreeBSD_X86_MemoryMapParams.bits32;
        break;
      default:
        report_fatal_error("unsupported architecture");
      }
      break;
    case Triple::NetBSD:
      switch (TargetTriple.getArch()) {
      case Triple::x86_64:
        MapParams = NetBSD_X86_MemoryMapParams.bits64;
        break;
      default:
        report_fatal_error("unsupported architecture");
      }
      break;
    case Triple::Linux:
      switch (TargetTriple.getArch()) {
      case Triple::x86_64:
        MapParams = Linux_X86_MemoryMapParams.bits64;
        break;
      case Triple::x86:
        MapParams = Linux_X86_MemoryMapParams.bits32;
        break;
      case Triple::mips64:
      case Triple::mips64el:
        MapParams = Linux_MIPS_MemoryMapParams.bits64;
        break;
      case Triple::ppc64:
      case Triple::ppc64le:
        MapParams = Linux_PowerPC_MemoryMapParams.bits64;
        break;
      case Triple::systemz:
        MapParams = Linux_S390_MemoryMapParams.bits64;
        break;
      case Triple::aarch64:
      case Triple::aarch64_be:
        MapParams = Linux_ARM_MemoryMapParams.bits64;
        break;
      case Triple::loongarch64:
        MapParams = Linux_LoongArch_MemoryMapParams.bits64;
        break;
      default:
        report_fatal_error("unsupported architecture");
      }
      break;
    default:
      report_fatal_error("unsupported operating system");
    }
  }

  C = &(M.getContext());
  IRBuilder<> IRB(*C);
  IntptrTy = IRB.getIntPtrTy(DL);
  OriginTy = IRB.getInt32Ty();
  PtrTy = IRB.getPtrTy();

  ColdCallWeights = MDBuilder(*C).createUnlikelyBranchWeights();
  OriginStoreWeights = MDBuilder(*C).createUnlikelyBranchWeights();

  if (!CompileKernel) {
    if (TrackOrigins)
      M.getOrInsertGlobal("__msan_track_origins", IRB.getInt32Ty(), [&] {
        return new GlobalVariable(
            M, IRB.getInt32Ty(), true, GlobalValue::WeakODRLinkage,
            IRB.getInt32(TrackOrigins), "__msan_track_origins");
      });

    if (Recover)
      M.getOrInsertGlobal("__msan_keep_going", IRB.getInt32Ty(), [&] {
        return new GlobalVariable(M, IRB.getInt32Ty(), true,
                                  GlobalValue::WeakODRLinkage,
                                  IRB.getInt32(Recover), "__msan_keep_going");
      });
  }
}

namespace {

/// A helper class that handles instrumentation of VarArg
/// functions on a particular platform.
///
/// Implementations are expected to insert the instrumentation
/// necessary to propagate argument shadow through VarArg function
/// calls. Visit* methods are called during an InstVisitor pass over
/// the function, and should avoid creating new basic blocks. A new
/// instance of this class is created for each instrumented function.
struct VarArgHelper {
  virtual ~VarArgHelper() = default;

  /// Visit a CallBase.
  virtual void visitCallBase(CallBase &CB, IRBuilder<> &IRB) = 0;

  /// Visit a va_start call.
  virtual void visitVAStartInst(VAStartInst &I) = 0;

  /// Visit a va_copy call.
  virtual void visitVACopyInst(VACopyInst &I) = 0;

  /// Finalize function instrumentation.
  ///
  /// This method is called after visiting all interesting (see above)
  /// instructions in a function.
  virtual void finalizeInstrumentation() = 0;
};

struct MemorySanitizerVisitor;

} // end anonymous namespace

static VarArgHelper *CreateVarArgHelper(Function &Func, MemorySanitizer &Msan,
                                        MemorySanitizerVisitor &Visitor);

static unsigned TypeSizeToSizeIndex(TypeSize TS) {
  if (TS.isScalable())
    // Scalable types unconditionally take slowpaths.
    return kNumberOfAccessSizes;
  unsigned TypeSizeFixed = TS.getFixedValue();
  if (TypeSizeFixed <= 8)
    return 0;
  return Log2_32_Ceil((TypeSizeFixed + 7) / 8);
}

namespace {

/// Helper class to attach debug information of the given instruction onto new
/// instructions inserted after.
class NextNodeIRBuilder : public IRBuilder<> {
public:
  explicit NextNodeIRBuilder(Instruction *IP) : IRBuilder<>(IP->getNextNode()) {
    SetCurrentDebugLocation(IP->getDebugLoc());
  }
};

/// This class does all the work for a given function. Store and Load
/// instructions store and load corresponding shadow and origin
/// values. Most instructions propagate shadow from arguments to their
/// return values. Certain instructions (most importantly, BranchInst)
/// test their argument shadow and print reports (with a runtime call) if it's
/// non-zero.
struct MemorySanitizerVisitor : public InstVisitor<MemorySanitizerVisitor> {
  Function &F;
  MemorySanitizer &MS;
  SmallVector<PHINode *, 16> ShadowPHINodes, OriginPHINodes;
  ValueMap<Value *, Value *> ShadowMap, OriginMap;
  std::unique_ptr<VarArgHelper> VAHelper;
  const TargetLibraryInfo *TLI;
  Instruction *FnPrologueEnd;
  SmallVector<Instruction *, 16> Instructions;

  // The following flags disable parts of MSan instrumentation based on
  // exclusion list contents and command-line options.
  bool InsertChecks;
  bool PropagateShadow;
  bool PoisonStack;
  bool PoisonUndef;

  struct ShadowOriginAndInsertPoint {
    Value *Shadow;
    Value *Origin;
    Instruction *OrigIns;

    ShadowOriginAndInsertPoint(Value *S, Value *O, Instruction *I)
        : Shadow(S), Origin(O), OrigIns(I) {}
  };
  SmallVector<ShadowOriginAndInsertPoint, 16> InstrumentationList;
  DenseMap<const DILocation *, int> LazyWarningDebugLocationCount;
  bool InstrumentLifetimeStart = ClHandleLifetimeIntrinsics;
  SmallSetVector<AllocaInst *, 16> AllocaSet;
  SmallVector<std::pair<IntrinsicInst *, AllocaInst *>, 16> LifetimeStartList;
  SmallVector<StoreInst *, 16> StoreList;
  int64_t SplittableBlocksCount = 0;

  MemorySanitizerVisitor(Function &F, MemorySanitizer &MS,
                         const TargetLibraryInfo &TLI)
      : F(F), MS(MS), VAHelper(CreateVarArgHelper(F, MS, *this)), TLI(&TLI) {
    bool SanitizeFunction =
        F.hasFnAttribute(Attribute::SanitizeMemory) && !ClDisableChecks;
    InsertChecks = SanitizeFunction;
    PropagateShadow = SanitizeFunction;
    PoisonStack = SanitizeFunction && ClPoisonStack;
    PoisonUndef = SanitizeFunction && ClPoisonUndef;

    // In the presence of unreachable blocks, we may see Phi nodes with
    // incoming nodes from such blocks. Since InstVisitor skips unreachable
    // blocks, such nodes will not have any shadow value associated with them.
    // It's easier to remove unreachable blocks than deal with missing shadow.
    removeUnreachableBlocks(F);

    MS.initializeCallbacks(*F.getParent(), TLI);
    FnPrologueEnd = IRBuilder<>(F.getEntryBlock().getFirstNonPHI())
                        .CreateIntrinsic(Intrinsic::donothing, {}, {});

    if (MS.CompileKernel) {
      IRBuilder<> IRB(FnPrologueEnd);
      insertKmsanPrologue(IRB);
    }

    LLVM_DEBUG(if (!InsertChecks) dbgs()
               << "MemorySanitizer is not inserting checks into '"
               << F.getName() << "'\n");
  }

  bool instrumentWithCalls(Value *V) {
    // Constants likely will be eliminated by follow-up passes.
    if (isa<Constant>(V))
      return false;

    ++SplittableBlocksCount;
    return ClInstrumentationWithCallThreshold >= 0 &&
           SplittableBlocksCount > ClInstrumentationWithCallThreshold;
  }

  bool isInPrologue(Instruction &I) {
    return I.getParent() == FnPrologueEnd->getParent() &&
           (&I == FnPrologueEnd || I.comesBefore(FnPrologueEnd));
  }

  // Creates a new origin and records the stack trace. In general we can call
  // this function for any origin manipulation we like. However it will cost
  // runtime resources. So use this wisely only if it can provide additional
  // information helpful to a user.
  Value *updateOrigin(Value *V, IRBuilder<> &IRB) {
    if (MS.TrackOrigins <= 1)
      return V;
    return IRB.CreateCall(MS.MsanChainOriginFn, V);
  }

  Value *originToIntptr(IRBuilder<> &IRB, Value *Origin) {
    const DataLayout &DL = F.getDataLayout();
    unsigned IntptrSize = DL.getTypeStoreSize(MS.IntptrTy);
    if (IntptrSize == kOriginSize)
      return Origin;
    assert(IntptrSize == kOriginSize * 2);
    Origin = IRB.CreateIntCast(Origin, MS.IntptrTy, /* isSigned */ false);
    return IRB.CreateOr(Origin, IRB.CreateShl(Origin, kOriginSize * 8));
  }

  /// Fill memory range with the given origin value.
  void paintOrigin(IRBuilder<> &IRB, Value *Origin, Value *OriginPtr,
                   TypeSize TS, Align Alignment) {
    const DataLayout &DL = F.getDataLayout();
    const Align IntptrAlignment = DL.getABITypeAlign(MS.IntptrTy);
    unsigned IntptrSize = DL.getTypeStoreSize(MS.IntptrTy);
    assert(IntptrAlignment >= kMinOriginAlignment);
    assert(IntptrSize >= kOriginSize);

    // Note: The loop based formation works for fixed length vectors too,
    // however we prefer to unroll and specialize alignment below.
    if (TS.isScalable()) {
      Value *Size = IRB.CreateTypeSize(MS.IntptrTy, TS);
      Value *RoundUp =
          IRB.CreateAdd(Size, ConstantInt::get(MS.IntptrTy, kOriginSize - 1));
      Value *End =
          IRB.CreateUDiv(RoundUp, ConstantInt::get(MS.IntptrTy, kOriginSize));
      auto [InsertPt, Index] =
        SplitBlockAndInsertSimpleForLoop(End, &*IRB.GetInsertPoint());
      IRB.SetInsertPoint(InsertPt);

      Value *GEP = IRB.CreateGEP(MS.OriginTy, OriginPtr, Index);
      IRB.CreateAlignedStore(Origin, GEP, kMinOriginAlignment);
      return;
    }

    unsigned Size = TS.getFixedValue();

    unsigned Ofs = 0;
    Align CurrentAlignment = Alignment;
    if (Alignment >= IntptrAlignment && IntptrSize > kOriginSize) {
      Value *IntptrOrigin = originToIntptr(IRB, Origin);
      Value *IntptrOriginPtr =
          IRB.CreatePointerCast(OriginPtr, PointerType::get(MS.IntptrTy, 0));
      for (unsigned i = 0; i < Size / IntptrSize; ++i) {
        Value *Ptr = i ? IRB.CreateConstGEP1_32(MS.IntptrTy, IntptrOriginPtr, i)
                       : IntptrOriginPtr;
        IRB.CreateAlignedStore(IntptrOrigin, Ptr, CurrentAlignment);
        Ofs += IntptrSize / kOriginSize;
        CurrentAlignment = IntptrAlignment;
      }
    }

    for (unsigned i = Ofs; i < (Size + kOriginSize - 1) / kOriginSize; ++i) {
      Value *GEP =
          i ? IRB.CreateConstGEP1_32(MS.OriginTy, OriginPtr, i) : OriginPtr;
      IRB.CreateAlignedStore(Origin, GEP, CurrentAlignment);
      CurrentAlignment = kMinOriginAlignment;
    }
  }

  void storeOrigin(IRBuilder<> &IRB, Value *Addr, Value *Shadow, Value *Origin,
                   Value *OriginPtr, Align Alignment) {
    const DataLayout &DL = F.getDataLayout();
    const Align OriginAlignment = std::max(kMinOriginAlignment, Alignment);
    TypeSize StoreSize = DL.getTypeStoreSize(Shadow->getType());
    // ZExt cannot convert between vector and scalar
    Value *ConvertedShadow = convertShadowToScalar(Shadow, IRB);
    if (auto *ConstantShadow = dyn_cast<Constant>(ConvertedShadow)) {
      if (!ClCheckConstantShadow || ConstantShadow->isZeroValue()) {
        // Origin is not needed: value is initialized or const shadow is
        // ignored.
        return;
      }
      if (llvm::isKnownNonZero(ConvertedShadow, DL)) {
        // Copy origin as the value is definitely uninitialized.
        paintOrigin(IRB, updateOrigin(Origin, IRB), OriginPtr, StoreSize,
                    OriginAlignment);
        return;
      }
      // Fallback to runtime check, which still can be optimized out later.
    }

    TypeSize TypeSizeInBits = DL.getTypeSizeInBits(ConvertedShadow->getType());
    unsigned SizeIndex = TypeSizeToSizeIndex(TypeSizeInBits);
    if (instrumentWithCalls(ConvertedShadow) &&
        SizeIndex < kNumberOfAccessSizes && !MS.CompileKernel) {
      FunctionCallee Fn = MS.MaybeStoreOriginFn[SizeIndex];
      Value *ConvertedShadow2 =
          IRB.CreateZExt(ConvertedShadow, IRB.getIntNTy(8 * (1 << SizeIndex)));
      CallBase *CB = IRB.CreateCall(Fn, {ConvertedShadow2, Addr, Origin});
      CB->addParamAttr(0, Attribute::ZExt);
      CB->addParamAttr(2, Attribute::ZExt);
    } else {
      Value *Cmp = convertToBool(ConvertedShadow, IRB, "_mscmp");
      Instruction *CheckTerm = SplitBlockAndInsertIfThen(
          Cmp, &*IRB.GetInsertPoint(), false, MS.OriginStoreWeights);
      IRBuilder<> IRBNew(CheckTerm);
      paintOrigin(IRBNew, updateOrigin(Origin, IRBNew), OriginPtr, StoreSize,
                  OriginAlignment);
    }
  }

  void materializeStores() {
    for (StoreInst *SI : StoreList) {
      IRBuilder<> IRB(SI);
      Value *Val = SI->getValueOperand();
      Value *Addr = SI->getPointerOperand();
      Value *Shadow = SI->isAtomic() ? getCleanShadow(Val) : getShadow(Val);
      Value *ShadowPtr, *OriginPtr;
      Type *ShadowTy = Shadow->getType();
      const Align Alignment = SI->getAlign();
      const Align OriginAlignment = std::max(kMinOriginAlignment, Alignment);
      std::tie(ShadowPtr, OriginPtr) =
          getShadowOriginPtr(Addr, IRB, ShadowTy, Alignment, /*isStore*/ true);

      StoreInst *NewSI = IRB.CreateAlignedStore(Shadow, ShadowPtr, Alignment);
      LLVM_DEBUG(dbgs() << "  STORE: " << *NewSI << "\n");
      (void)NewSI;

      if (SI->isAtomic())
        SI->setOrdering(addReleaseOrdering(SI->getOrdering()));

      if (MS.TrackOrigins && !SI->isAtomic())
        storeOrigin(IRB, Addr, Shadow, getOrigin(Val), OriginPtr,
                    OriginAlignment);
    }
  }

  // Returns true if Debug Location corresponds to multiple warnings.
  bool shouldDisambiguateWarningLocation(const DebugLoc &DebugLoc) {
    if (MS.TrackOrigins < 2)
      return false;

    if (LazyWarningDebugLocationCount.empty())
      for (const auto &I : InstrumentationList)
        ++LazyWarningDebugLocationCount[I.OrigIns->getDebugLoc()];

    return LazyWarningDebugLocationCount[DebugLoc] >= ClDisambiguateWarning;
  }

  /// Helper function to insert a warning at IRB's current insert point.
  void insertWarningFn(IRBuilder<> &IRB, Value *Origin) {
    if (!Origin)
      Origin = (Value *)IRB.getInt32(0);
    assert(Origin->getType()->isIntegerTy());

    if (shouldDisambiguateWarningLocation(IRB.getCurrentDebugLocation())) {
      // Try to create additional origin with debug info of the last origin
      // instruction. It may provide additional information to the user.
      if (Instruction *OI = dyn_cast_or_null<Instruction>(Origin)) {
        assert(MS.TrackOrigins);
        auto NewDebugLoc = OI->getDebugLoc();
        // Origin update with missing or the same debug location provides no
        // additional value.
        if (NewDebugLoc && NewDebugLoc != IRB.getCurrentDebugLocation()) {
          // Insert update just before the check, so we call runtime only just
          // before the report.
          IRBuilder<> IRBOrigin(&*IRB.GetInsertPoint());
          IRBOrigin.SetCurrentDebugLocation(NewDebugLoc);
          Origin = updateOrigin(Origin, IRBOrigin);
        }
      }
    }

    if (MS.CompileKernel || MS.TrackOrigins)
      IRB.CreateCall(MS.WarningFn, Origin)->setCannotMerge();
    else
      IRB.CreateCall(MS.WarningFn)->setCannotMerge();
    // FIXME: Insert UnreachableInst if !MS.Recover?
    // This may invalidate some of the following checks and needs to be done
    // at the very end.
  }

  void materializeOneCheck(IRBuilder<> &IRB, Value *ConvertedShadow,
                           Value *Origin) {
    const DataLayout &DL = F.getDataLayout();
    TypeSize TypeSizeInBits = DL.getTypeSizeInBits(ConvertedShadow->getType());
    unsigned SizeIndex = TypeSizeToSizeIndex(TypeSizeInBits);
    if (instrumentWithCalls(ConvertedShadow) &&
        SizeIndex < kNumberOfAccessSizes && !MS.CompileKernel) {
      FunctionCallee Fn = MS.MaybeWarningFn[SizeIndex];
      // ZExt cannot convert between vector and scalar
      ConvertedShadow = convertShadowToScalar(ConvertedShadow, IRB);
      Value *ConvertedShadow2 =
          IRB.CreateZExt(ConvertedShadow, IRB.getIntNTy(8 * (1 << SizeIndex)));
      CallBase *CB = IRB.CreateCall(
          Fn, {ConvertedShadow2,
               MS.TrackOrigins && Origin ? Origin : (Value *)IRB.getInt32(0)});
      CB->addParamAttr(0, Attribute::ZExt);
      CB->addParamAttr(1, Attribute::ZExt);
    } else {
      Value *Cmp = convertToBool(ConvertedShadow, IRB, "_mscmp");
      Instruction *CheckTerm = SplitBlockAndInsertIfThen(
          Cmp, &*IRB.GetInsertPoint(),
          /* Unreachable */ !MS.Recover, MS.ColdCallWeights);

      IRB.SetInsertPoint(CheckTerm);
      insertWarningFn(IRB, Origin);
      LLVM_DEBUG(dbgs() << "  CHECK: " << *Cmp << "\n");
    }
  }

  void materializeInstructionChecks(
      ArrayRef<ShadowOriginAndInsertPoint> InstructionChecks) {
    const DataLayout &DL = F.getDataLayout();
    // Disable combining in some cases. TrackOrigins checks each shadow to pick
    // correct origin.
    bool Combine = !MS.TrackOrigins;
    Instruction *Instruction = InstructionChecks.front().OrigIns;
    Value *Shadow = nullptr;
    for (const auto &ShadowData : InstructionChecks) {
      assert(ShadowData.OrigIns == Instruction);
      IRBuilder<> IRB(Instruction);

      Value *ConvertedShadow = ShadowData.Shadow;

      if (auto *ConstantShadow = dyn_cast<Constant>(ConvertedShadow)) {
        if (!ClCheckConstantShadow || ConstantShadow->isZeroValue()) {
          // Skip, value is initialized or const shadow is ignored.
          continue;
        }
        if (llvm::isKnownNonZero(ConvertedShadow, DL)) {
          // Report as the value is definitely uninitialized.
          insertWarningFn(IRB, ShadowData.Origin);
          if (!MS.Recover)
            return; // Always fail and stop here, not need to check the rest.
          // Skip entire instruction,
          continue;
        }
        // Fallback to runtime check, which still can be optimized out later.
      }

      if (!Combine) {
        materializeOneCheck(IRB, ConvertedShadow, ShadowData.Origin);
        continue;
      }

      if (!Shadow) {
        Shadow = ConvertedShadow;
        continue;
      }

      Shadow = convertToBool(Shadow, IRB, "_mscmp");
      ConvertedShadow = convertToBool(ConvertedShadow, IRB, "_mscmp");
      Shadow = IRB.CreateOr(Shadow, ConvertedShadow, "_msor");
    }

    if (Shadow) {
      assert(Combine);
      IRBuilder<> IRB(Instruction);
      materializeOneCheck(IRB, Shadow, nullptr);
    }
  }

  void materializeChecks() {
#ifndef NDEBUG
    // For assert below.
    SmallPtrSet<Instruction *, 16> Done;
#endif

    for (auto I = InstrumentationList.begin();
         I != InstrumentationList.end();) {
      auto OrigIns = I->OrigIns;
      // Checks are grouped by the original instruction. We call all
      // `insertShadowCheck` for an instruction at once.
      assert(Done.insert(OrigIns).second);
      auto J = std::find_if(I + 1, InstrumentationList.end(),
                            [OrigIns](const ShadowOriginAndInsertPoint &R) {
                              return OrigIns != R.OrigIns;
                            });
      // Process all checks of instruction at once.
      materializeInstructionChecks(ArrayRef<ShadowOriginAndInsertPoint>(I, J));
      I = J;
    }

    LLVM_DEBUG(dbgs() << "DONE:\n" << F);
  }

  // Returns the last instruction in the new prologue
  void insertKmsanPrologue(IRBuilder<> &IRB) {
    Value *ContextState = IRB.CreateCall(MS.MsanGetContextStateFn, {});
    Constant *Zero = IRB.getInt32(0);
    MS.ParamTLS = IRB.CreateGEP(MS.MsanContextStateTy, ContextState,
                                {Zero, IRB.getInt32(0)}, "param_shadow");
    MS.RetvalTLS = IRB.CreateGEP(MS.MsanContextStateTy, ContextState,
                                 {Zero, IRB.getInt32(1)}, "retval_shadow");
    MS.VAArgTLS = IRB.CreateGEP(MS.MsanContextStateTy, ContextState,
                                {Zero, IRB.getInt32(2)}, "va_arg_shadow");
    MS.VAArgOriginTLS = IRB.CreateGEP(MS.MsanContextStateTy, ContextState,
                                      {Zero, IRB.getInt32(3)}, "va_arg_origin");
    MS.VAArgOverflowSizeTLS =
        IRB.CreateGEP(MS.MsanContextStateTy, ContextState,
                      {Zero, IRB.getInt32(4)}, "va_arg_overflow_size");
    MS.ParamOriginTLS = IRB.CreateGEP(MS.MsanContextStateTy, ContextState,
                                      {Zero, IRB.getInt32(5)}, "param_origin");
    MS.RetvalOriginTLS =
        IRB.CreateGEP(MS.MsanContextStateTy, ContextState,
                      {Zero, IRB.getInt32(6)}, "retval_origin");
    if (MS.TargetTriple.getArch() == Triple::systemz)
      MS.MsanMetadataAlloca = IRB.CreateAlloca(MS.MsanMetadata, 0u);
  }

  /// Add MemorySanitizer instrumentation to a function.
  bool runOnFunction() {
    // Iterate all BBs in depth-first order and create shadow instructions
    // for all instructions (where applicable).
    // For PHI nodes we create dummy shadow PHIs which will be finalized later.
    for (BasicBlock *BB : depth_first(FnPrologueEnd->getParent()))
      visit(*BB);

    // `visit` above only collects instructions. Process them after iterating
    // CFG to avoid requirement on CFG transformations.
    for (Instruction *I : Instructions)
      InstVisitor<MemorySanitizerVisitor>::visit(*I);

    // Finalize PHI nodes.
    for (PHINode *PN : ShadowPHINodes) {
      PHINode *PNS = cast<PHINode>(getShadow(PN));
      PHINode *PNO = MS.TrackOrigins ? cast<PHINode>(getOrigin(PN)) : nullptr;
      size_t NumValues = PN->getNumIncomingValues();
      for (size_t v = 0; v < NumValues; v++) {
        PNS->addIncoming(getShadow(PN, v), PN->getIncomingBlock(v));
        if (PNO)
          PNO->addIncoming(getOrigin(PN, v), PN->getIncomingBlock(v));
      }
    }

    VAHelper->finalizeInstrumentation();

    // Poison llvm.lifetime.start intrinsics, if we haven't fallen back to
    // instrumenting only allocas.
    if (InstrumentLifetimeStart) {
      for (auto Item : LifetimeStartList) {
        instrumentAlloca(*Item.second, Item.first);
        AllocaSet.remove(Item.second);
      }
    }
    // Poison the allocas for which we didn't instrument the corresponding
    // lifetime intrinsics.
    for (AllocaInst *AI : AllocaSet)
      instrumentAlloca(*AI);

    // Insert shadow value checks.
    materializeChecks();

    // Delayed instrumentation of StoreInst.
    // This may not add new address checks.
    materializeStores();

    return true;
  }

  /// Compute the shadow type that corresponds to a given Value.
  Type *getShadowTy(Value *V) { return getShadowTy(V->getType()); }

  /// Compute the shadow type that corresponds to a given Type.
  Type *getShadowTy(Type *OrigTy) {
    if (!OrigTy->isSized()) {
      return nullptr;
    }
    // For integer type, shadow is the same as the original type.
    // This may return weird-sized types like i1.
    if (IntegerType *IT = dyn_cast<IntegerType>(OrigTy))
      return IT;
    const DataLayout &DL = F.getDataLayout();
    if (VectorType *VT = dyn_cast<VectorType>(OrigTy)) {
      uint32_t EltSize = DL.getTypeSizeInBits(VT->getElementType());
      return VectorType::get(IntegerType::get(*MS.C, EltSize),
                             VT->getElementCount());
    }
    if (ArrayType *AT = dyn_cast<ArrayType>(OrigTy)) {
      return ArrayType::get(getShadowTy(AT->getElementType()),
                            AT->getNumElements());
    }
    if (StructType *ST = dyn_cast<StructType>(OrigTy)) {
      SmallVector<Type *, 4> Elements;
      for (unsigned i = 0, n = ST->getNumElements(); i < n; i++)
        Elements.push_back(getShadowTy(ST->getElementType(i)));
      StructType *Res = StructType::get(*MS.C, Elements, ST->isPacked());
      LLVM_DEBUG(dbgs() << "getShadowTy: " << *ST << " ===> " << *Res << "\n");
      return Res;
    }
    uint32_t TypeSize = DL.getTypeSizeInBits(OrigTy);
    return IntegerType::get(*MS.C, TypeSize);
  }

  /// Extract combined shadow of struct elements as a bool
  Value *collapseStructShadow(StructType *Struct, Value *Shadow,
                              IRBuilder<> &IRB) {
    Value *FalseVal = IRB.getIntN(/* width */ 1, /* value */ 0);
    Value *Aggregator = FalseVal;

    for (unsigned Idx = 0; Idx < Struct->getNumElements(); Idx++) {
      // Combine by ORing together each element's bool shadow
      Value *ShadowItem = IRB.CreateExtractValue(Shadow, Idx);
      Value *ShadowBool = convertToBool(ShadowItem, IRB);

      if (Aggregator != FalseVal)
        Aggregator = IRB.CreateOr(Aggregator, ShadowBool);
      else
        Aggregator = ShadowBool;
    }

    return Aggregator;
  }

  // Extract combined shadow of array elements
  Value *collapseArrayShadow(ArrayType *Array, Value *Shadow,
                             IRBuilder<> &IRB) {
    if (!Array->getNumElements())
      return IRB.getIntN(/* width */ 1, /* value */ 0);

    Value *FirstItem = IRB.CreateExtractValue(Shadow, 0);
    Value *Aggregator = convertShadowToScalar(FirstItem, IRB);

    for (unsigned Idx = 1; Idx < Array->getNumElements(); Idx++) {
      Value *ShadowItem = IRB.CreateExtractValue(Shadow, Idx);
      Value *ShadowInner = convertShadowToScalar(ShadowItem, IRB);
      Aggregator = IRB.CreateOr(Aggregator, ShadowInner);
    }
    return Aggregator;
  }

  /// Convert a shadow value to it's flattened variant. The resulting
  /// shadow may not necessarily have the same bit width as the input
  /// value, but it will always be comparable to zero.
  Value *convertShadowToScalar(Value *V, IRBuilder<> &IRB) {
    if (StructType *Struct = dyn_cast<StructType>(V->getType()))
      return collapseStructShadow(Struct, V, IRB);
    if (ArrayType *Array = dyn_cast<ArrayType>(V->getType()))
      return collapseArrayShadow(Array, V, IRB);
    if (isa<VectorType>(V->getType())) {
      if (isa<ScalableVectorType>(V->getType()))
        return convertShadowToScalar(IRB.CreateOrReduce(V), IRB);
      unsigned BitWidth =
        V->getType()->getPrimitiveSizeInBits().getFixedValue();
      return IRB.CreateBitCast(V, IntegerType::get(*MS.C, BitWidth));
    }
    return V;
  }

  // Convert a scalar value to an i1 by comparing with 0
  Value *convertToBool(Value *V, IRBuilder<> &IRB, const Twine &name = "") {
    Type *VTy = V->getType();
    if (!VTy->isIntegerTy())
      return convertToBool(convertShadowToScalar(V, IRB), IRB, name);
    if (VTy->getIntegerBitWidth() == 1)
      // Just converting a bool to a bool, so do nothing.
      return V;
    return IRB.CreateICmpNE(V, ConstantInt::get(VTy, 0), name);
  }

  Type *ptrToIntPtrType(Type *PtrTy) const {
    if (VectorType *VectTy = dyn_cast<VectorType>(PtrTy)) {
      return VectorType::get(ptrToIntPtrType(VectTy->getElementType()),
                             VectTy->getElementCount());
    }
    assert(PtrTy->isIntOrPtrTy());
    return MS.IntptrTy;
  }

  Type *getPtrToShadowPtrType(Type *IntPtrTy, Type *ShadowTy) const {
    if (VectorType *VectTy = dyn_cast<VectorType>(IntPtrTy)) {
      return VectorType::get(
          getPtrToShadowPtrType(VectTy->getElementType(), ShadowTy),
          VectTy->getElementCount());
    }
    assert(IntPtrTy == MS.IntptrTy);
    return PointerType::get(*MS.C, 0);
  }

  Constant *constToIntPtr(Type *IntPtrTy, uint64_t C) const {
    if (VectorType *VectTy = dyn_cast<VectorType>(IntPtrTy)) {
      return ConstantVector::getSplat(
          VectTy->getElementCount(), constToIntPtr(VectTy->getElementType(), C));
    }
    assert(IntPtrTy == MS.IntptrTy);
    return ConstantInt::get(MS.IntptrTy, C);
  }

  /// Compute the integer shadow offset that corresponds to a given
  /// application address.
  ///
  /// Offset = (Addr & ~AndMask) ^ XorMask
  /// Addr can be a ptr or <N x ptr>. In both cases ShadowTy the shadow type of
  /// a single pointee.
  /// Returns <shadow_ptr, origin_ptr> or <<N x shadow_ptr>, <N x origin_ptr>>.
  Value *getShadowPtrOffset(Value *Addr, IRBuilder<> &IRB) {
    Type *IntptrTy = ptrToIntPtrType(Addr->getType());
    Value *OffsetLong = IRB.CreatePointerCast(Addr, IntptrTy);

    if (uint64_t AndMask = MS.MapParams->AndMask)
      OffsetLong = IRB.CreateAnd(OffsetLong, constToIntPtr(IntptrTy, ~AndMask));

    if (uint64_t XorMask = MS.MapParams->XorMask)
      OffsetLong = IRB.CreateXor(OffsetLong, constToIntPtr(IntptrTy, XorMask));
    return OffsetLong;
  }

  /// Compute the shadow and origin addresses corresponding to a given
  /// application address.
  ///
  /// Shadow = ShadowBase + Offset
  /// Origin = (OriginBase + Offset) & ~3ULL
  /// Addr can be a ptr or <N x ptr>. In both cases ShadowTy the shadow type of
  /// a single pointee.
  /// Returns <shadow_ptr, origin_ptr> or <<N x shadow_ptr>, <N x origin_ptr>>.
  std::pair<Value *, Value *>
  getShadowOriginPtrUserspace(Value *Addr, IRBuilder<> &IRB, Type *ShadowTy,
                              MaybeAlign Alignment) {
    VectorType *VectTy = dyn_cast<VectorType>(Addr->getType());
    if (!VectTy) {
      assert(Addr->getType()->isPointerTy());
    } else {
      assert(VectTy->getElementType()->isPointerTy());
    }
    Type *IntptrTy = ptrToIntPtrType(Addr->getType());
    Value *ShadowOffset = getShadowPtrOffset(Addr, IRB);
    Value *ShadowLong = ShadowOffset;
    if (uint64_t ShadowBase = MS.MapParams->ShadowBase) {
      ShadowLong =
          IRB.CreateAdd(ShadowLong, constToIntPtr(IntptrTy, ShadowBase));
    }
    Value *ShadowPtr = IRB.CreateIntToPtr(
        ShadowLong, getPtrToShadowPtrType(IntptrTy, ShadowTy));

    Value *OriginPtr = nullptr;
    if (MS.TrackOrigins) {
      Value *OriginLong = ShadowOffset;
      uint64_t OriginBase = MS.MapParams->OriginBase;
      if (OriginBase != 0)
        OriginLong =
            IRB.CreateAdd(OriginLong, constToIntPtr(IntptrTy, OriginBase));
      if (!Alignment || *Alignment < kMinOriginAlignment) {
        uint64_t Mask = kMinOriginAlignment.value() - 1;
        OriginLong = IRB.CreateAnd(OriginLong, constToIntPtr(IntptrTy, ~Mask));
      }
      OriginPtr = IRB.CreateIntToPtr(
          OriginLong, getPtrToShadowPtrType(IntptrTy, MS.OriginTy));
    }
    return std::make_pair(ShadowPtr, OriginPtr);
  }

  template <typename... ArgsTy>
  Value *createMetadataCall(IRBuilder<> &IRB, FunctionCallee Callee,
                            ArgsTy... Args) {
    if (MS.TargetTriple.getArch() == Triple::systemz) {
      IRB.CreateCall(Callee,
                     {MS.MsanMetadataAlloca, std::forward<ArgsTy>(Args)...});
      return IRB.CreateLoad(MS.MsanMetadata, MS.MsanMetadataAlloca);
    }

    return IRB.CreateCall(Callee, {std::forward<ArgsTy>(Args)...});
  }

  std::pair<Value *, Value *> getShadowOriginPtrKernelNoVec(Value *Addr,
                                                            IRBuilder<> &IRB,
                                                            Type *ShadowTy,
                                                            bool isStore) {
    Value *ShadowOriginPtrs;
    const DataLayout &DL = F.getDataLayout();
    TypeSize Size = DL.getTypeStoreSize(ShadowTy);

    FunctionCallee Getter = MS.getKmsanShadowOriginAccessFn(isStore, Size);
    Value *AddrCast =
        IRB.CreatePointerCast(Addr, PointerType::get(IRB.getInt8Ty(), 0));
    if (Getter) {
      ShadowOriginPtrs = createMetadataCall(IRB, Getter, AddrCast);
    } else {
      Value *SizeVal = ConstantInt::get(MS.IntptrTy, Size);
      ShadowOriginPtrs = createMetadataCall(
          IRB,
          isStore ? MS.MsanMetadataPtrForStoreN : MS.MsanMetadataPtrForLoadN,
          AddrCast, SizeVal);
    }
    Value *ShadowPtr = IRB.CreateExtractValue(ShadowOriginPtrs, 0);
    ShadowPtr = IRB.CreatePointerCast(ShadowPtr, PointerType::get(ShadowTy, 0));
    Value *OriginPtr = IRB.CreateExtractValue(ShadowOriginPtrs, 1);

    return std::make_pair(ShadowPtr, OriginPtr);
  }

  /// Addr can be a ptr or <N x ptr>. In both cases ShadowTy the shadow type of
  /// a single pointee.
  /// Returns <shadow_ptr, origin_ptr> or <<N x shadow_ptr>, <N x origin_ptr>>.
  std::pair<Value *, Value *> getShadowOriginPtrKernel(Value *Addr,
                                                       IRBuilder<> &IRB,
                                                       Type *ShadowTy,
                                                       bool isStore) {
    VectorType *VectTy = dyn_cast<VectorType>(Addr->getType());
    if (!VectTy) {
      assert(Addr->getType()->isPointerTy());
      return getShadowOriginPtrKernelNoVec(Addr, IRB, ShadowTy, isStore);
    }

    // TODO: Support callbacs with vectors of addresses.
    unsigned NumElements = cast<FixedVectorType>(VectTy)->getNumElements();
    Value *ShadowPtrs = ConstantInt::getNullValue(
        FixedVectorType::get(IRB.getPtrTy(), NumElements));
    Value *OriginPtrs = nullptr;
    if (MS.TrackOrigins)
      OriginPtrs = ConstantInt::getNullValue(
          FixedVectorType::get(IRB.getPtrTy(), NumElements));
    for (unsigned i = 0; i < NumElements; ++i) {
      Value *OneAddr =
          IRB.CreateExtractElement(Addr, ConstantInt::get(IRB.getInt32Ty(), i));
      auto [ShadowPtr, OriginPtr] =
          getShadowOriginPtrKernelNoVec(OneAddr, IRB, ShadowTy, isStore);

      ShadowPtrs = IRB.CreateInsertElement(
          ShadowPtrs, ShadowPtr, ConstantInt::get(IRB.getInt32Ty(), i));
      if (MS.TrackOrigins)
        OriginPtrs = IRB.CreateInsertElement(
            OriginPtrs, OriginPtr, ConstantInt::get(IRB.getInt32Ty(), i));
    }
    return {ShadowPtrs, OriginPtrs};
  }

  std::pair<Value *, Value *> getShadowOriginPtr(Value *Addr, IRBuilder<> &IRB,
                                                 Type *ShadowTy,
                                                 MaybeAlign Alignment,
                                                 bool isStore) {
    if (MS.CompileKernel)
      return getShadowOriginPtrKernel(Addr, IRB, ShadowTy, isStore);
    return getShadowOriginPtrUserspace(Addr, IRB, ShadowTy, Alignment);
  }

  /// Compute the shadow address for a given function argument.
  ///
  /// Shadow = ParamTLS+ArgOffset.
  Value *getShadowPtrForArgument(IRBuilder<> &IRB, int ArgOffset) {
    Value *Base = IRB.CreatePointerCast(MS.ParamTLS, MS.IntptrTy);
    if (ArgOffset)
      Base = IRB.CreateAdd(Base, ConstantInt::get(MS.IntptrTy, ArgOffset));
    return IRB.CreateIntToPtr(Base, IRB.getPtrTy(0), "_msarg");
  }

  /// Compute the origin address for a given function argument.
  Value *getOriginPtrForArgument(IRBuilder<> &IRB, int ArgOffset) {
    if (!MS.TrackOrigins)
      return nullptr;
    Value *Base = IRB.CreatePointerCast(MS.ParamOriginTLS, MS.IntptrTy);
    if (ArgOffset)
      Base = IRB.CreateAdd(Base, ConstantInt::get(MS.IntptrTy, ArgOffset));
    return IRB.CreateIntToPtr(Base, IRB.getPtrTy(0), "_msarg_o");
  }

  /// Compute the shadow address for a retval.
  Value *getShadowPtrForRetval(IRBuilder<> &IRB) {
    return IRB.CreatePointerCast(MS.RetvalTLS, IRB.getPtrTy(0), "_msret");
  }

  /// Compute the origin address for a retval.
  Value *getOriginPtrForRetval() {
    // We keep a single origin for the entire retval. Might be too optimistic.
    return MS.RetvalOriginTLS;
  }

  /// Set SV to be the shadow value for V.
  void setShadow(Value *V, Value *SV) {
    assert(!ShadowMap.count(V) && "Values may only have one shadow");
    ShadowMap[V] = PropagateShadow ? SV : getCleanShadow(V);
  }

  /// Set Origin to be the origin value for V.
  void setOrigin(Value *V, Value *Origin) {
    if (!MS.TrackOrigins)
      return;
    assert(!OriginMap.count(V) && "Values may only have one origin");
    LLVM_DEBUG(dbgs() << "ORIGIN: " << *V << "  ==> " << *Origin << "\n");
    OriginMap[V] = Origin;
  }

  Constant *getCleanShadow(Type *OrigTy) {
    Type *ShadowTy = getShadowTy(OrigTy);
    if (!ShadowTy)
      return nullptr;
    return Constant::getNullValue(ShadowTy);
  }

  /// Create a clean shadow value for a given value.
  ///
  /// Clean shadow (all zeroes) means all bits of the value are defined
  /// (initialized).
  Constant *getCleanShadow(Value *V) { return getCleanShadow(V->getType()); }

  /// Create a dirty shadow of a given shadow type.
  Constant *getPoisonedShadow(Type *ShadowTy) {
    assert(ShadowTy);
    if (isa<IntegerType>(ShadowTy) || isa<VectorType>(ShadowTy))
      return Constant::getAllOnesValue(ShadowTy);
    if (ArrayType *AT = dyn_cast<ArrayType>(ShadowTy)) {
      SmallVector<Constant *, 4> Vals(AT->getNumElements(),
                                      getPoisonedShadow(AT->getElementType()));
      return ConstantArray::get(AT, Vals);
    }
    if (StructType *ST = dyn_cast<StructType>(ShadowTy)) {
      SmallVector<Constant *, 4> Vals;
      for (unsigned i = 0, n = ST->getNumElements(); i < n; i++)
        Vals.push_back(getPoisonedShadow(ST->getElementType(i)));
      return ConstantStruct::get(ST, Vals);
    }
    llvm_unreachable("Unexpected shadow type");
  }

  /// Create a dirty shadow for a given value.
  Constant *getPoisonedShadow(Value *V) {
    Type *ShadowTy = getShadowTy(V);
    if (!ShadowTy)
      return nullptr;
    return getPoisonedShadow(ShadowTy);
  }

  /// Create a clean (zero) origin.
  Value *getCleanOrigin() { return Constant::getNullValue(MS.OriginTy); }

  /// Get the shadow value for a given Value.
  ///
  /// This function either returns the value set earlier with setShadow,
  /// or extracts if from ParamTLS (for function arguments).
  Value *getShadow(Value *V) {
    if (Instruction *I = dyn_cast<Instruction>(V)) {
      if (!PropagateShadow || I->getMetadata(LLVMContext::MD_nosanitize))
        return getCleanShadow(V);
      // For instructions the shadow is already stored in the map.
      Value *Shadow = ShadowMap[V];
      if (!Shadow) {
        LLVM_DEBUG(dbgs() << "No shadow: " << *V << "\n" << *(I->getParent()));
        (void)I;
        assert(Shadow && "No shadow for a value");
      }
      return Shadow;
    }
    if (UndefValue *U = dyn_cast<UndefValue>(V)) {
      Value *AllOnes = (PropagateShadow && PoisonUndef) ? getPoisonedShadow(V)
                                                        : getCleanShadow(V);
      LLVM_DEBUG(dbgs() << "Undef: " << *U << " ==> " << *AllOnes << "\n");
      (void)U;
      return AllOnes;
    }
    if (Argument *A = dyn_cast<Argument>(V)) {
      // For arguments we compute the shadow on demand and store it in the map.
      Value *&ShadowPtr = ShadowMap[V];
      if (ShadowPtr)
        return ShadowPtr;
      Function *F = A->getParent();
      IRBuilder<> EntryIRB(FnPrologueEnd);
      unsigned ArgOffset = 0;
      const DataLayout &DL = F->getDataLayout();
      for (auto &FArg : F->args()) {
        if (!FArg.getType()->isSized() || FArg.getType()->isScalableTy()) {
          LLVM_DEBUG(dbgs() << (FArg.getType()->isScalableTy()
                                    ? "vscale not fully supported\n"
                                    : "Arg is not sized\n"));
          if (A == &FArg) {
            ShadowPtr = getCleanShadow(V);
            setOrigin(A, getCleanOrigin());
            break;
          }
          continue;
        }

        unsigned Size = FArg.hasByValAttr()
                            ? DL.getTypeAllocSize(FArg.getParamByValType())
                            : DL.getTypeAllocSize(FArg.getType());

        if (A == &FArg) {
          bool Overflow = ArgOffset + Size > kParamTLSSize;
          if (FArg.hasByValAttr()) {
            // ByVal pointer itself has clean shadow. We copy the actual
            // argument shadow to the underlying memory.
            // Figure out maximal valid memcpy alignment.
            const Align ArgAlign = DL.getValueOrABITypeAlignment(
                FArg.getParamAlign(), FArg.getParamByValType());
            Value *CpShadowPtr, *CpOriginPtr;
            std::tie(CpShadowPtr, CpOriginPtr) =
                getShadowOriginPtr(V, EntryIRB, EntryIRB.getInt8Ty(), ArgAlign,
                                   /*isStore*/ true);
            if (!PropagateShadow || Overflow) {
              // ParamTLS overflow.
              EntryIRB.CreateMemSet(
                  CpShadowPtr, Constant::getNullValue(EntryIRB.getInt8Ty()),
                  Size, ArgAlign);
            } else {
              Value *Base = getShadowPtrForArgument(EntryIRB, ArgOffset);
              const Align CopyAlign = std::min(ArgAlign, kShadowTLSAlignment);
              Value *Cpy = EntryIRB.CreateMemCpy(CpShadowPtr, CopyAlign, Base,
                                                 CopyAlign, Size);
              LLVM_DEBUG(dbgs() << "  ByValCpy: " << *Cpy << "\n");
              (void)Cpy;

              if (MS.TrackOrigins) {
                Value *OriginPtr =
                    getOriginPtrForArgument(EntryIRB, ArgOffset);
                // FIXME: OriginSize should be:
                // alignTo(V % kMinOriginAlignment + Size, kMinOriginAlignment)
                unsigned OriginSize = alignTo(Size, kMinOriginAlignment);
                EntryIRB.CreateMemCpy(
                    CpOriginPtr,
                    /* by getShadowOriginPtr */ kMinOriginAlignment, OriginPtr,
                    /* by origin_tls[ArgOffset] */ kMinOriginAlignment,
                    OriginSize);
              }
            }
          }

          if (!PropagateShadow || Overflow || FArg.hasByValAttr() ||
              (MS.EagerChecks && FArg.hasAttribute(Attribute::NoUndef))) {
            ShadowPtr = getCleanShadow(V);
            setOrigin(A, getCleanOrigin());
          } else {
            // Shadow over TLS
            Value *Base = getShadowPtrForArgument(EntryIRB, ArgOffset);
            ShadowPtr = EntryIRB.CreateAlignedLoad(getShadowTy(&FArg), Base,
                                                   kShadowTLSAlignment);
            if (MS.TrackOrigins) {
              Value *OriginPtr =
                  getOriginPtrForArgument(EntryIRB, ArgOffset);
              setOrigin(A, EntryIRB.CreateLoad(MS.OriginTy, OriginPtr));
            }
          }
          LLVM_DEBUG(dbgs()
                     << "  ARG:    " << FArg << " ==> " << *ShadowPtr << "\n");
          break;
        }

        ArgOffset += alignTo(Size, kShadowTLSAlignment);
      }
      assert(ShadowPtr && "Could not find shadow for an argument");
      return ShadowPtr;
    }
    // For everything else the shadow is zero.
    return getCleanShadow(V);
  }

  /// Get the shadow for i-th argument of the instruction I.
  Value *getShadow(Instruction *I, int i) {
    return getShadow(I->getOperand(i));
  }

  /// Get the origin for a value.
  Value *getOrigin(Value *V) {
    if (!MS.TrackOrigins)
      return nullptr;
    if (!PropagateShadow || isa<Constant>(V) || isa<InlineAsm>(V))
      return getCleanOrigin();
    assert((isa<Instruction>(V) || isa<Argument>(V)) &&
           "Unexpected value type in getOrigin()");
    if (Instruction *I = dyn_cast<Instruction>(V)) {
      if (I->getMetadata(LLVMContext::MD_nosanitize))
        return getCleanOrigin();
    }
    Value *Origin = OriginMap[V];
    assert(Origin && "Missing origin");
    return Origin;
  }

  /// Get the origin for i-th argument of the instruction I.
  Value *getOrigin(Instruction *I, int i) {
    return getOrigin(I->getOperand(i));
  }

  /// Remember the place where a shadow check should be inserted.
  ///
  /// This location will be later instrumented with a check that will print a
  /// UMR warning in runtime if the shadow value is not 0.
  void insertShadowCheck(Value *Shadow, Value *Origin, Instruction *OrigIns) {
    assert(Shadow);
    if (!InsertChecks)
      return;

    if (!DebugCounter::shouldExecute(DebugInsertCheck)) {
      LLVM_DEBUG(dbgs() << "Skipping check of " << *Shadow << " before "
                        << *OrigIns << "\n");
      return;
    }
#ifndef NDEBUG
    Type *ShadowTy = Shadow->getType();
    assert((isa<IntegerType>(ShadowTy) || isa<VectorType>(ShadowTy) ||
            isa<StructType>(ShadowTy) || isa<ArrayType>(ShadowTy)) &&
           "Can only insert checks for integer, vector, and aggregate shadow "
           "types");
#endif
    InstrumentationList.push_back(
        ShadowOriginAndInsertPoint(Shadow, Origin, OrigIns));
  }

  /// Remember the place where a shadow check should be inserted.
  ///
  /// This location will be later instrumented with a check that will print a
  /// UMR warning in runtime if the value is not fully defined.
  void insertShadowCheck(Value *Val, Instruction *OrigIns) {
    assert(Val);
    Value *Shadow, *Origin;
    if (ClCheckConstantShadow) {
      Shadow = getShadow(Val);
      if (!Shadow)
        return;
      Origin = getOrigin(Val);
    } else {
      Shadow = dyn_cast_or_null<Instruction>(getShadow(Val));
      if (!Shadow)
        return;
      Origin = dyn_cast_or_null<Instruction>(getOrigin(Val));
    }
    insertShadowCheck(Shadow, Origin, OrigIns);
  }

  AtomicOrdering addReleaseOrdering(AtomicOrdering a) {
    switch (a) {
    case AtomicOrdering::NotAtomic:
      return AtomicOrdering::NotAtomic;
    case AtomicOrdering::Unordered:
    case AtomicOrdering::Monotonic:
    case AtomicOrdering::Release:
      return AtomicOrdering::Release;
    case AtomicOrdering::Acquire:
    case AtomicOrdering::AcquireRelease:
      return AtomicOrdering::AcquireRelease;
    case AtomicOrdering::SequentiallyConsistent:
      return AtomicOrdering::SequentiallyConsistent;
    }
    llvm_unreachable("Unknown ordering");
  }

  Value *makeAddReleaseOrderingTable(IRBuilder<> &IRB) {
    constexpr int NumOrderings = (int)AtomicOrderingCABI::seq_cst + 1;
    uint32_t OrderingTable[NumOrderings] = {};

    OrderingTable[(int)AtomicOrderingCABI::relaxed] =
        OrderingTable[(int)AtomicOrderingCABI::release] =
            (int)AtomicOrderingCABI::release;
    OrderingTable[(int)AtomicOrderingCABI::consume] =
        OrderingTable[(int)AtomicOrderingCABI::acquire] =
            OrderingTable[(int)AtomicOrderingCABI::acq_rel] =
                (int)AtomicOrderingCABI::acq_rel;
    OrderingTable[(int)AtomicOrderingCABI::seq_cst] =
        (int)AtomicOrderingCABI::seq_cst;

    return ConstantDataVector::get(IRB.getContext(), OrderingTable);
  }

  AtomicOrdering addAcquireOrdering(AtomicOrdering a) {
    switch (a) {
    case AtomicOrdering::NotAtomic:
      return AtomicOrdering::NotAtomic;
    case AtomicOrdering::Unordered:
    case AtomicOrdering::Monotonic:
    case AtomicOrdering::Acquire:
      return AtomicOrdering::Acquire;
    case AtomicOrdering::Release:
    case AtomicOrdering::AcquireRelease:
      return AtomicOrdering::AcquireRelease;
    case AtomicOrdering::SequentiallyConsistent:
      return AtomicOrdering::SequentiallyConsistent;
    }
    llvm_unreachable("Unknown ordering");
  }

  Value *makeAddAcquireOrderingTable(IRBuilder<> &IRB) {
    constexpr int NumOrderings = (int)AtomicOrderingCABI::seq_cst + 1;
    uint32_t OrderingTable[NumOrderings] = {};

    OrderingTable[(int)AtomicOrderingCABI::relaxed] =
        OrderingTable[(int)AtomicOrderingCABI::acquire] =
            OrderingTable[(int)AtomicOrderingCABI::consume] =
                (int)AtomicOrderingCABI::acquire;
    OrderingTable[(int)AtomicOrderingCABI::release] =
        OrderingTable[(int)AtomicOrderingCABI::acq_rel] =
            (int)AtomicOrderingCABI::acq_rel;
    OrderingTable[(int)AtomicOrderingCABI::seq_cst] =
        (int)AtomicOrderingCABI::seq_cst;

    return ConstantDataVector::get(IRB.getContext(), OrderingTable);
  }

  // ------------------- Visitors.
  using InstVisitor<MemorySanitizerVisitor>::visit;
  void visit(Instruction &I) {
    if (I.getMetadata(LLVMContext::MD_nosanitize))
      return;
    // Don't want to visit if we're in the prologue
    if (isInPrologue(I))
      return;
    if (!DebugCounter::shouldExecute(DebugInstrumentInstruction)) {
      LLVM_DEBUG(dbgs() << "Skipping instruction: " << I << "\n");
      // We still need to set the shadow and origin to clean values.
      setShadow(&I, getCleanShadow(&I));
      setOrigin(&I, getCleanOrigin());
      return;
    }

    Instructions.push_back(&I);
  }

  /// Instrument LoadInst
  ///
  /// Loads the corresponding shadow and (optionally) origin.
  /// Optionally, checks that the load address is fully defined.
  void visitLoadInst(LoadInst &I) {
    assert(I.getType()->isSized() && "Load type must have size");
    assert(!I.getMetadata(LLVMContext::MD_nosanitize));
    NextNodeIRBuilder IRB(&I);
    Type *ShadowTy = getShadowTy(&I);
    Value *Addr = I.getPointerOperand();
    Value *ShadowPtr = nullptr, *OriginPtr = nullptr;
    const Align Alignment = I.getAlign();
    if (PropagateShadow) {
      std::tie(ShadowPtr, OriginPtr) =
          getShadowOriginPtr(Addr, IRB, ShadowTy, Alignment, /*isStore*/ false);
      setShadow(&I,
                IRB.CreateAlignedLoad(ShadowTy, ShadowPtr, Alignment, "_msld"));
    } else {
      setShadow(&I, getCleanShadow(&I));
    }

    if (ClCheckAccessAddress)
      insertShadowCheck(I.getPointerOperand(), &I);

    if (I.isAtomic())
      I.setOrdering(addAcquireOrdering(I.getOrdering()));

    if (MS.TrackOrigins) {
      if (PropagateShadow) {
        const Align OriginAlignment = std::max(kMinOriginAlignment, Alignment);
        setOrigin(
            &I, IRB.CreateAlignedLoad(MS.OriginTy, OriginPtr, OriginAlignment));
      } else {
        setOrigin(&I, getCleanOrigin());
      }
    }
  }

  /// Instrument StoreInst
  ///
  /// Stores the corresponding shadow and (optionally) origin.
  /// Optionally, checks that the store address is fully defined.
  void visitStoreInst(StoreInst &I) {
    StoreList.push_back(&I);
    if (ClCheckAccessAddress)
      insertShadowCheck(I.getPointerOperand(), &I);
  }

  void handleCASOrRMW(Instruction &I) {
    assert(isa<AtomicRMWInst>(I) || isa<AtomicCmpXchgInst>(I));

    IRBuilder<> IRB(&I);
    Value *Addr = I.getOperand(0);
    Value *Val = I.getOperand(1);
    Value *ShadowPtr = getShadowOriginPtr(Addr, IRB, getShadowTy(Val), Align(1),
                                          /*isStore*/ true)
                           .first;

    if (ClCheckAccessAddress)
      insertShadowCheck(Addr, &I);

    // Only test the conditional argument of cmpxchg instruction.
    // The other argument can potentially be uninitialized, but we can not
    // detect this situation reliably without possible false positives.
    if (isa<AtomicCmpXchgInst>(I))
      insertShadowCheck(Val, &I);

    IRB.CreateStore(getCleanShadow(Val), ShadowPtr);

    setShadow(&I, getCleanShadow(&I));
    setOrigin(&I, getCleanOrigin());
  }

  void visitAtomicRMWInst(AtomicRMWInst &I) {
    handleCASOrRMW(I);
    I.setOrdering(addReleaseOrdering(I.getOrdering()));
  }

  void visitAtomicCmpXchgInst(AtomicCmpXchgInst &I) {
    handleCASOrRMW(I);
    I.setSuccessOrdering(addReleaseOrdering(I.getSuccessOrdering()));
  }

  // Vector manipulation.
  void visitExtractElementInst(ExtractElementInst &I) {
    insertShadowCheck(I.getOperand(1), &I);
    IRBuilder<> IRB(&I);
    setShadow(&I, IRB.CreateExtractElement(getShadow(&I, 0), I.getOperand(1),
                                           "_msprop"));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void visitInsertElementInst(InsertElementInst &I) {
    insertShadowCheck(I.getOperand(2), &I);
    IRBuilder<> IRB(&I);
    auto *Shadow0 = getShadow(&I, 0);
    auto *Shadow1 = getShadow(&I, 1);
    setShadow(&I, IRB.CreateInsertElement(Shadow0, Shadow1, I.getOperand(2),
                                          "_msprop"));
    setOriginForNaryOp(I);
  }

  void visitShuffleVectorInst(ShuffleVectorInst &I) {
    IRBuilder<> IRB(&I);
    auto *Shadow0 = getShadow(&I, 0);
    auto *Shadow1 = getShadow(&I, 1);
    setShadow(&I, IRB.CreateShuffleVector(Shadow0, Shadow1, I.getShuffleMask(),
                                          "_msprop"));
    setOriginForNaryOp(I);
  }

  // Casts.
  void visitSExtInst(SExtInst &I) {
    IRBuilder<> IRB(&I);
    setShadow(&I, IRB.CreateSExt(getShadow(&I, 0), I.getType(), "_msprop"));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void visitZExtInst(ZExtInst &I) {
    IRBuilder<> IRB(&I);
    setShadow(&I, IRB.CreateZExt(getShadow(&I, 0), I.getType(), "_msprop"));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void visitTruncInst(TruncInst &I) {
    IRBuilder<> IRB(&I);
    setShadow(&I, IRB.CreateTrunc(getShadow(&I, 0), I.getType(), "_msprop"));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void visitBitCastInst(BitCastInst &I) {
    // Special case: if this is the bitcast (there is exactly 1 allowed) between
    // a musttail call and a ret, don't instrument. New instructions are not
    // allowed after a musttail call.
    if (auto *CI = dyn_cast<CallInst>(I.getOperand(0)))
      if (CI->isMustTailCall())
        return;
    IRBuilder<> IRB(&I);
    setShadow(&I, IRB.CreateBitCast(getShadow(&I, 0), getShadowTy(&I)));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void visitPtrToIntInst(PtrToIntInst &I) {
    IRBuilder<> IRB(&I);
    setShadow(&I, IRB.CreateIntCast(getShadow(&I, 0), getShadowTy(&I), false,
                                    "_msprop_ptrtoint"));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void visitIntToPtrInst(IntToPtrInst &I) {
    IRBuilder<> IRB(&I);
    setShadow(&I, IRB.CreateIntCast(getShadow(&I, 0), getShadowTy(&I), false,
                                    "_msprop_inttoptr"));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void visitFPToSIInst(CastInst &I) { handleShadowOr(I); }
  void visitFPToUIInst(CastInst &I) { handleShadowOr(I); }
  void visitSIToFPInst(CastInst &I) { handleShadowOr(I); }
  void visitUIToFPInst(CastInst &I) { handleShadowOr(I); }
  void visitFPExtInst(CastInst &I) { handleShadowOr(I); }
  void visitFPTruncInst(CastInst &I) { handleShadowOr(I); }

  /// Propagate shadow for bitwise AND.
  ///
  /// This code is exact, i.e. if, for example, a bit in the left argument
  /// is defined and 0, then neither the value not definedness of the
  /// corresponding bit in B don't affect the resulting shadow.
  void visitAnd(BinaryOperator &I) {
    IRBuilder<> IRB(&I);
    //  "And" of 0 and a poisoned value results in unpoisoned value.
    //  1&1 => 1;     0&1 => 0;     p&1 => p;
    //  1&0 => 0;     0&0 => 0;     p&0 => 0;
    //  1&p => p;     0&p => 0;     p&p => p;
    //  S = (S1 & S2) | (V1 & S2) | (S1 & V2)
    Value *S1 = getShadow(&I, 0);
    Value *S2 = getShadow(&I, 1);
    Value *V1 = I.getOperand(0);
    Value *V2 = I.getOperand(1);
    if (V1->getType() != S1->getType()) {
      V1 = IRB.CreateIntCast(V1, S1->getType(), false);
      V2 = IRB.CreateIntCast(V2, S2->getType(), false);
    }
    Value *S1S2 = IRB.CreateAnd(S1, S2);
    Value *V1S2 = IRB.CreateAnd(V1, S2);
    Value *S1V2 = IRB.CreateAnd(S1, V2);
    setShadow(&I, IRB.CreateOr({S1S2, V1S2, S1V2}));
    setOriginForNaryOp(I);
  }

  void visitOr(BinaryOperator &I) {
    IRBuilder<> IRB(&I);
    //  "Or" of 1 and a poisoned value results in unpoisoned value.
    //  1|1 => 1;     0|1 => 1;     p|1 => 1;
    //  1|0 => 1;     0|0 => 0;     p|0 => p;
    //  1|p => 1;     0|p => p;     p|p => p;
    //  S = (S1 & S2) | (~V1 & S2) | (S1 & ~V2)
    Value *S1 = getShadow(&I, 0);
    Value *S2 = getShadow(&I, 1);
    Value *V1 = IRB.CreateNot(I.getOperand(0));
    Value *V2 = IRB.CreateNot(I.getOperand(1));
    if (V1->getType() != S1->getType()) {
      V1 = IRB.CreateIntCast(V1, S1->getType(), false);
      V2 = IRB.CreateIntCast(V2, S2->getType(), false);
    }
    Value *S1S2 = IRB.CreateAnd(S1, S2);
    Value *V1S2 = IRB.CreateAnd(V1, S2);
    Value *S1V2 = IRB.CreateAnd(S1, V2);
    setShadow(&I, IRB.CreateOr({S1S2, V1S2, S1V2}));
    setOriginForNaryOp(I);
  }

  /// Default propagation of shadow and/or origin.
  ///
  /// This class implements the general case of shadow propagation, used in all
  /// cases where we don't know and/or don't care about what the operation
  /// actually does. It converts all input shadow values to a common type
  /// (extending or truncating as necessary), and bitwise OR's them.
  ///
  /// This is much cheaper than inserting checks (i.e. requiring inputs to be
  /// fully initialized), and less prone to false positives.
  ///
  /// This class also implements the general case of origin propagation. For a
  /// Nary operation, result origin is set to the origin of an argument that is
  /// not entirely initialized. If there is more than one such arguments, the
  /// rightmost of them is picked. It does not matter which one is picked if all
  /// arguments are initialized.
  template <bool CombineShadow> class Combiner {
    Value *Shadow = nullptr;
    Value *Origin = nullptr;
    IRBuilder<> &IRB;
    MemorySanitizerVisitor *MSV;

  public:
    Combiner(MemorySanitizerVisitor *MSV, IRBuilder<> &IRB)
        : IRB(IRB), MSV(MSV) {}

    /// Add a pair of shadow and origin values to the mix.
    Combiner &Add(Value *OpShadow, Value *OpOrigin) {
      if (CombineShadow) {
        assert(OpShadow);
        if (!Shadow)
          Shadow = OpShadow;
        else {
          OpShadow = MSV->CreateShadowCast(IRB, OpShadow, Shadow->getType());
          Shadow = IRB.CreateOr(Shadow, OpShadow, "_msprop");
        }
      }

      if (MSV->MS.TrackOrigins) {
        assert(OpOrigin);
        if (!Origin) {
          Origin = OpOrigin;
        } else {
          Constant *ConstOrigin = dyn_cast<Constant>(OpOrigin);
          // No point in adding something that might result in 0 origin value.
          if (!ConstOrigin || !ConstOrigin->isNullValue()) {
            Value *Cond = MSV->convertToBool(OpShadow, IRB);
            Origin = IRB.CreateSelect(Cond, OpOrigin, Origin);
          }
        }
      }
      return *this;
    }

    /// Add an application value to the mix.
    Combiner &Add(Value *V) {
      Value *OpShadow = MSV->getShadow(V);
      Value *OpOrigin = MSV->MS.TrackOrigins ? MSV->getOrigin(V) : nullptr;
      return Add(OpShadow, OpOrigin);
    }

    /// Set the current combined values as the given instruction's shadow
    /// and origin.
    void Done(Instruction *I) {
      if (CombineShadow) {
        assert(Shadow);
        Shadow = MSV->CreateShadowCast(IRB, Shadow, MSV->getShadowTy(I));
        MSV->setShadow(I, Shadow);
      }
      if (MSV->MS.TrackOrigins) {
        assert(Origin);
        MSV->setOrigin(I, Origin);
      }
    }

    /// Store the current combined value at the specified origin
    /// location.
    void DoneAndStoreOrigin(TypeSize TS, Value *OriginPtr) {
      if (MSV->MS.TrackOrigins) {
        assert(Origin);
        MSV->paintOrigin(IRB, Origin, OriginPtr, TS, kMinOriginAlignment);
      }
    }
  };

  using ShadowAndOriginCombiner = Combiner<true>;
  using OriginCombiner = Combiner<false>;

  /// Propagate origin for arbitrary operation.
  void setOriginForNaryOp(Instruction &I) {
    if (!MS.TrackOrigins)
      return;
    IRBuilder<> IRB(&I);
    OriginCombiner OC(this, IRB);
    for (Use &Op : I.operands())
      OC.Add(Op.get());
    OC.Done(&I);
  }

  size_t VectorOrPrimitiveTypeSizeInBits(Type *Ty) {
    assert(!(Ty->isVectorTy() && Ty->getScalarType()->isPointerTy()) &&
           "Vector of pointers is not a valid shadow type");
    return Ty->isVectorTy() ? cast<FixedVectorType>(Ty)->getNumElements() *
                                  Ty->getScalarSizeInBits()
                            : Ty->getPrimitiveSizeInBits();
  }

  /// Cast between two shadow types, extending or truncating as
  /// necessary.
  Value *CreateShadowCast(IRBuilder<> &IRB, Value *V, Type *dstTy,
                          bool Signed = false) {
    Type *srcTy = V->getType();
    if (srcTy == dstTy)
      return V;
    size_t srcSizeInBits = VectorOrPrimitiveTypeSizeInBits(srcTy);
    size_t dstSizeInBits = VectorOrPrimitiveTypeSizeInBits(dstTy);
    if (srcSizeInBits > 1 && dstSizeInBits == 1)
      return IRB.CreateICmpNE(V, getCleanShadow(V));

    if (dstTy->isIntegerTy() && srcTy->isIntegerTy())
      return IRB.CreateIntCast(V, dstTy, Signed);
    if (dstTy->isVectorTy() && srcTy->isVectorTy() &&
        cast<VectorType>(dstTy)->getElementCount() ==
            cast<VectorType>(srcTy)->getElementCount())
      return IRB.CreateIntCast(V, dstTy, Signed);
    Value *V1 = IRB.CreateBitCast(V, Type::getIntNTy(*MS.C, srcSizeInBits));
    Value *V2 =
        IRB.CreateIntCast(V1, Type::getIntNTy(*MS.C, dstSizeInBits), Signed);
    return IRB.CreateBitCast(V2, dstTy);
    // TODO: handle struct types.
  }

  /// Cast an application value to the type of its own shadow.
  Value *CreateAppToShadowCast(IRBuilder<> &IRB, Value *V) {
    Type *ShadowTy = getShadowTy(V);
    if (V->getType() == ShadowTy)
      return V;
    if (V->getType()->isPtrOrPtrVectorTy())
      return IRB.CreatePtrToInt(V, ShadowTy);
    else
      return IRB.CreateBitCast(V, ShadowTy);
  }

  /// Propagate shadow for arbitrary operation.
  void handleShadowOr(Instruction &I) {
    IRBuilder<> IRB(&I);
    ShadowAndOriginCombiner SC(this, IRB);
    for (Use &Op : I.operands())
      SC.Add(Op.get());
    SC.Done(&I);
  }

  void visitFNeg(UnaryOperator &I) { handleShadowOr(I); }

  // Handle multiplication by constant.
  //
  // Handle a special case of multiplication by constant that may have one or
  // more zeros in the lower bits. This makes corresponding number of lower bits
  // of the result zero as well. We model it by shifting the other operand
  // shadow left by the required number of bits. Effectively, we transform
  // (X * (A * 2**B)) to ((X << B) * A) and instrument (X << B) as (Sx << B).
  // We use multiplication by 2**N instead of shift to cover the case of
  // multiplication by 0, which may occur in some elements of a vector operand.
  void handleMulByConstant(BinaryOperator &I, Constant *ConstArg,
                           Value *OtherArg) {
    Constant *ShadowMul;
    Type *Ty = ConstArg->getType();
    if (auto *VTy = dyn_cast<VectorType>(Ty)) {
      unsigned NumElements = cast<FixedVectorType>(VTy)->getNumElements();
      Type *EltTy = VTy->getElementType();
      SmallVector<Constant *, 16> Elements;
      for (unsigned Idx = 0; Idx < NumElements; ++Idx) {
        if (ConstantInt *Elt =
                dyn_cast<ConstantInt>(ConstArg->getAggregateElement(Idx))) {
          const APInt &V = Elt->getValue();
          APInt V2 = APInt(V.getBitWidth(), 1) << V.countr_zero();
          Elements.push_back(ConstantInt::get(EltTy, V2));
        } else {
          Elements.push_back(ConstantInt::get(EltTy, 1));
        }
      }
      ShadowMul = ConstantVector::get(Elements);
    } else {
      if (ConstantInt *Elt = dyn_cast<ConstantInt>(ConstArg)) {
        const APInt &V = Elt->getValue();
        APInt V2 = APInt(V.getBitWidth(), 1) << V.countr_zero();
        ShadowMul = ConstantInt::get(Ty, V2);
      } else {
        ShadowMul = ConstantInt::get(Ty, 1);
      }
    }

    IRBuilder<> IRB(&I);
    setShadow(&I,
              IRB.CreateMul(getShadow(OtherArg), ShadowMul, "msprop_mul_cst"));
    setOrigin(&I, getOrigin(OtherArg));
  }

  void visitMul(BinaryOperator &I) {
    Constant *constOp0 = dyn_cast<Constant>(I.getOperand(0));
    Constant *constOp1 = dyn_cast<Constant>(I.getOperand(1));
    if (constOp0 && !constOp1)
      handleMulByConstant(I, constOp0, I.getOperand(1));
    else if (constOp1 && !constOp0)
      handleMulByConstant(I, constOp1, I.getOperand(0));
    else
      handleShadowOr(I);
  }

  void visitFAdd(BinaryOperator &I) { handleShadowOr(I); }
  void visitFSub(BinaryOperator &I) { handleShadowOr(I); }
  void visitFMul(BinaryOperator &I) { handleShadowOr(I); }
  void visitAdd(BinaryOperator &I) { handleShadowOr(I); }
  void visitSub(BinaryOperator &I) { handleShadowOr(I); }
  void visitXor(BinaryOperator &I) { handleShadowOr(I); }

  void handleIntegerDiv(Instruction &I) {
    IRBuilder<> IRB(&I);
    // Strict on the second argument.
    insertShadowCheck(I.getOperand(1), &I);
    setShadow(&I, getShadow(&I, 0));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void visitUDiv(BinaryOperator &I) { handleIntegerDiv(I); }
  void visitSDiv(BinaryOperator &I) { handleIntegerDiv(I); }
  void visitURem(BinaryOperator &I) { handleIntegerDiv(I); }
  void visitSRem(BinaryOperator &I) { handleIntegerDiv(I); }

  // Floating point division is side-effect free. We can not require that the
  // divisor is fully initialized and must propagate shadow. See PR37523.
  void visitFDiv(BinaryOperator &I) { handleShadowOr(I); }
  void visitFRem(BinaryOperator &I) { handleShadowOr(I); }

  /// Instrument == and != comparisons.
  ///
  /// Sometimes the comparison result is known even if some of the bits of the
  /// arguments are not.
  void handleEqualityComparison(ICmpInst &I) {
    IRBuilder<> IRB(&I);
    Value *A = I.getOperand(0);
    Value *B = I.getOperand(1);
    Value *Sa = getShadow(A);
    Value *Sb = getShadow(B);

    // Get rid of pointers and vectors of pointers.
    // For ints (and vectors of ints), types of A and Sa match,
    // and this is a no-op.
    A = IRB.CreatePointerCast(A, Sa->getType());
    B = IRB.CreatePointerCast(B, Sb->getType());

    // A == B  <==>  (C = A^B) == 0
    // A != B  <==>  (C = A^B) != 0
    // Sc = Sa | Sb
    Value *C = IRB.CreateXor(A, B);
    Value *Sc = IRB.CreateOr(Sa, Sb);
    // Now dealing with i = (C == 0) comparison (or C != 0, does not matter now)
    // Result is defined if one of the following is true
    // * there is a defined 1 bit in C
    // * C is fully defined
    // Si = !(C & ~Sc) && Sc
    Value *Zero = Constant::getNullValue(Sc->getType());
    Value *MinusOne = Constant::getAllOnesValue(Sc->getType());
    Value *LHS = IRB.CreateICmpNE(Sc, Zero);
    Value *RHS =
        IRB.CreateICmpEQ(IRB.CreateAnd(IRB.CreateXor(Sc, MinusOne), C), Zero);
    Value *Si = IRB.CreateAnd(LHS, RHS);
    Si->setName("_msprop_icmp");
    setShadow(&I, Si);
    setOriginForNaryOp(I);
  }

  /// Build the lowest possible value of V, taking into account V's
  ///        uninitialized bits.
  Value *getLowestPossibleValue(IRBuilder<> &IRB, Value *A, Value *Sa,
                                bool isSigned) {
    if (isSigned) {
      // Split shadow into sign bit and other bits.
      Value *SaOtherBits = IRB.CreateLShr(IRB.CreateShl(Sa, 1), 1);
      Value *SaSignBit = IRB.CreateXor(Sa, SaOtherBits);
      // Maximise the undefined shadow bit, minimize other undefined bits.
      return IRB.CreateOr(IRB.CreateAnd(A, IRB.CreateNot(SaOtherBits)),
                          SaSignBit);
    } else {
      // Minimize undefined bits.
      return IRB.CreateAnd(A, IRB.CreateNot(Sa));
    }
  }

  /// Build the highest possible value of V, taking into account V's
  ///        uninitialized bits.
  Value *getHighestPossibleValue(IRBuilder<> &IRB, Value *A, Value *Sa,
                                 bool isSigned) {
    if (isSigned) {
      // Split shadow into sign bit and other bits.
      Value *SaOtherBits = IRB.CreateLShr(IRB.CreateShl(Sa, 1), 1);
      Value *SaSignBit = IRB.CreateXor(Sa, SaOtherBits);
      // Minimise the undefined shadow bit, maximise other undefined bits.
      return IRB.CreateOr(IRB.CreateAnd(A, IRB.CreateNot(SaSignBit)),
                          SaOtherBits);
    } else {
      // Maximize undefined bits.
      return IRB.CreateOr(A, Sa);
    }
  }

  /// Instrument relational comparisons.
  ///
  /// This function does exact shadow propagation for all relational
  /// comparisons of integers, pointers and vectors of those.
  /// FIXME: output seems suboptimal when one of the operands is a constant
  void handleRelationalComparisonExact(ICmpInst &I) {
    IRBuilder<> IRB(&I);
    Value *A = I.getOperand(0);
    Value *B = I.getOperand(1);
    Value *Sa = getShadow(A);
    Value *Sb = getShadow(B);

    // Get rid of pointers and vectors of pointers.
    // For ints (and vectors of ints), types of A and Sa match,
    // and this is a no-op.
    A = IRB.CreatePointerCast(A, Sa->getType());
    B = IRB.CreatePointerCast(B, Sb->getType());

    // Let [a0, a1] be the interval of possible values of A, taking into account
    // its undefined bits. Let [b0, b1] be the interval of possible values of B.
    // Then (A cmp B) is defined iff (a0 cmp b1) == (a1 cmp b0).
    bool IsSigned = I.isSigned();
    Value *S1 = IRB.CreateICmp(I.getPredicate(),
                               getLowestPossibleValue(IRB, A, Sa, IsSigned),
                               getHighestPossibleValue(IRB, B, Sb, IsSigned));
    Value *S2 = IRB.CreateICmp(I.getPredicate(),
                               getHighestPossibleValue(IRB, A, Sa, IsSigned),
                               getLowestPossibleValue(IRB, B, Sb, IsSigned));
    Value *Si = IRB.CreateXor(S1, S2);
    setShadow(&I, Si);
    setOriginForNaryOp(I);
  }

  /// Instrument signed relational comparisons.
  ///
  /// Handle sign bit tests: x<0, x>=0, x<=-1, x>-1 by propagating the highest
  /// bit of the shadow. Everything else is delegated to handleShadowOr().
  void handleSignedRelationalComparison(ICmpInst &I) {
    Constant *constOp;
    Value *op = nullptr;
    CmpInst::Predicate pre;
    if ((constOp = dyn_cast<Constant>(I.getOperand(1)))) {
      op = I.getOperand(0);
      pre = I.getPredicate();
    } else if ((constOp = dyn_cast<Constant>(I.getOperand(0)))) {
      op = I.getOperand(1);
      pre = I.getSwappedPredicate();
    } else {
      handleShadowOr(I);
      return;
    }

    if ((constOp->isNullValue() &&
         (pre == CmpInst::ICMP_SLT || pre == CmpInst::ICMP_SGE)) ||
        (constOp->isAllOnesValue() &&
         (pre == CmpInst::ICMP_SGT || pre == CmpInst::ICMP_SLE))) {
      IRBuilder<> IRB(&I);
      Value *Shadow = IRB.CreateICmpSLT(getShadow(op), getCleanShadow(op),
                                        "_msprop_icmp_s");
      setShadow(&I, Shadow);
      setOrigin(&I, getOrigin(op));
    } else {
      handleShadowOr(I);
    }
  }

  void visitICmpInst(ICmpInst &I) {
    if (!ClHandleICmp) {
      handleShadowOr(I);
      return;
    }
    if (I.isEquality()) {
      handleEqualityComparison(I);
      return;
    }

    assert(I.isRelational());
    if (ClHandleICmpExact) {
      handleRelationalComparisonExact(I);
      return;
    }
    if (I.isSigned()) {
      handleSignedRelationalComparison(I);
      return;
    }

    assert(I.isUnsigned());
    if ((isa<Constant>(I.getOperand(0)) || isa<Constant>(I.getOperand(1)))) {
      handleRelationalComparisonExact(I);
      return;
    }

    handleShadowOr(I);
  }

  void visitFCmpInst(FCmpInst &I) { handleShadowOr(I); }

  void handleShift(BinaryOperator &I) {
    IRBuilder<> IRB(&I);
    // If any of the S2 bits are poisoned, the whole thing is poisoned.
    // Otherwise perform the same shift on S1.
    Value *S1 = getShadow(&I, 0);
    Value *S2 = getShadow(&I, 1);
    Value *S2Conv =
        IRB.CreateSExt(IRB.CreateICmpNE(S2, getCleanShadow(S2)), S2->getType());
    Value *V2 = I.getOperand(1);
    Value *Shift = IRB.CreateBinOp(I.getOpcode(), S1, V2);
    setShadow(&I, IRB.CreateOr(Shift, S2Conv));
    setOriginForNaryOp(I);
  }

  void visitShl(BinaryOperator &I) { handleShift(I); }
  void visitAShr(BinaryOperator &I) { handleShift(I); }
  void visitLShr(BinaryOperator &I) { handleShift(I); }

  void handleFunnelShift(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    // If any of the S2 bits are poisoned, the whole thing is poisoned.
    // Otherwise perform the same shift on S0 and S1.
    Value *S0 = getShadow(&I, 0);
    Value *S1 = getShadow(&I, 1);
    Value *S2 = getShadow(&I, 2);
    Value *S2Conv =
        IRB.CreateSExt(IRB.CreateICmpNE(S2, getCleanShadow(S2)), S2->getType());
    Value *V2 = I.getOperand(2);
    Function *Intrin = Intrinsic::getDeclaration(
        I.getModule(), I.getIntrinsicID(), S2Conv->getType());
    Value *Shift = IRB.CreateCall(Intrin, {S0, S1, V2});
    setShadow(&I, IRB.CreateOr(Shift, S2Conv));
    setOriginForNaryOp(I);
  }

  /// Instrument llvm.memmove
  ///
  /// At this point we don't know if llvm.memmove will be inlined or not.
  /// If we don't instrument it and it gets inlined,
  /// our interceptor will not kick in and we will lose the memmove.
  /// If we instrument the call here, but it does not get inlined,
  /// we will memove the shadow twice: which is bad in case
  /// of overlapping regions. So, we simply lower the intrinsic to a call.
  ///
  /// Similar situation exists for memcpy and memset.
  void visitMemMoveInst(MemMoveInst &I) {
    getShadow(I.getArgOperand(1)); // Ensure shadow initialized
    IRBuilder<> IRB(&I);
    IRB.CreateCall(MS.MemmoveFn,
                   {I.getArgOperand(0), I.getArgOperand(1),
                    IRB.CreateIntCast(I.getArgOperand(2), MS.IntptrTy, false)});
    I.eraseFromParent();
  }

  /// Instrument memcpy
  ///
  /// Similar to memmove: avoid copying shadow twice. This is somewhat
  /// unfortunate as it may slowdown small constant memcpys.
  /// FIXME: consider doing manual inline for small constant sizes and proper
  /// alignment.
  ///
  /// Note: This also handles memcpy.inline, which promises no calls to external
  /// functions as an optimization. However, with instrumentation enabled this
  /// is difficult to promise; additionally, we know that the MSan runtime
  /// exists and provides __msan_memcpy(). Therefore, we assume that with
  /// instrumentation it's safe to turn memcpy.inline into a call to
  /// __msan_memcpy(). Should this be wrong, such as when implementing memcpy()
  /// itself, instrumentation should be disabled with the no_sanitize attribute.
  void visitMemCpyInst(MemCpyInst &I) {
    getShadow(I.getArgOperand(1)); // Ensure shadow initialized
    IRBuilder<> IRB(&I);
    IRB.CreateCall(MS.MemcpyFn,
                   {I.getArgOperand(0), I.getArgOperand(1),
                    IRB.CreateIntCast(I.getArgOperand(2), MS.IntptrTy, false)});
    I.eraseFromParent();
  }

  // Same as memcpy.
  void visitMemSetInst(MemSetInst &I) {
    IRBuilder<> IRB(&I);
    IRB.CreateCall(
        MS.MemsetFn,
        {I.getArgOperand(0),
         IRB.CreateIntCast(I.getArgOperand(1), IRB.getInt32Ty(), false),
         IRB.CreateIntCast(I.getArgOperand(2), MS.IntptrTy, false)});
    I.eraseFromParent();
  }

  void visitVAStartInst(VAStartInst &I) { VAHelper->visitVAStartInst(I); }

  void visitVACopyInst(VACopyInst &I) { VAHelper->visitVACopyInst(I); }

  /// Handle vector store-like intrinsics.
  ///
  /// Instrument intrinsics that look like a simple SIMD store: writes memory,
  /// has 1 pointer argument and 1 vector argument, returns void.
  bool handleVectorStoreIntrinsic(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *Addr = I.getArgOperand(0);
    Value *Shadow = getShadow(&I, 1);
    Value *ShadowPtr, *OriginPtr;

    // We don't know the pointer alignment (could be unaligned SSE store!).
    // Have to assume to worst case.
    std::tie(ShadowPtr, OriginPtr) = getShadowOriginPtr(
        Addr, IRB, Shadow->getType(), Align(1), /*isStore*/ true);
    IRB.CreateAlignedStore(Shadow, ShadowPtr, Align(1));

    if (ClCheckAccessAddress)
      insertShadowCheck(Addr, &I);

    // FIXME: factor out common code from materializeStores
    if (MS.TrackOrigins)
      IRB.CreateStore(getOrigin(&I, 1), OriginPtr);
    return true;
  }

  /// Handle vector load-like intrinsics.
  ///
  /// Instrument intrinsics that look like a simple SIMD load: reads memory,
  /// has 1 pointer argument, returns a vector.
  bool handleVectorLoadIntrinsic(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *Addr = I.getArgOperand(0);

    Type *ShadowTy = getShadowTy(&I);
    Value *ShadowPtr = nullptr, *OriginPtr = nullptr;
    if (PropagateShadow) {
      // We don't know the pointer alignment (could be unaligned SSE load!).
      // Have to assume to worst case.
      const Align Alignment = Align(1);
      std::tie(ShadowPtr, OriginPtr) =
          getShadowOriginPtr(Addr, IRB, ShadowTy, Alignment, /*isStore*/ false);
      setShadow(&I,
                IRB.CreateAlignedLoad(ShadowTy, ShadowPtr, Alignment, "_msld"));
    } else {
      setShadow(&I, getCleanShadow(&I));
    }

    if (ClCheckAccessAddress)
      insertShadowCheck(Addr, &I);

    if (MS.TrackOrigins) {
      if (PropagateShadow)
        setOrigin(&I, IRB.CreateLoad(MS.OriginTy, OriginPtr));
      else
        setOrigin(&I, getCleanOrigin());
    }
    return true;
  }

  /// Handle (SIMD arithmetic)-like intrinsics.
  ///
  /// Instrument intrinsics with any number of arguments of the same type,
  /// equal to the return type. The type should be simple (no aggregates or
  /// pointers; vectors are fine).
  /// Caller guarantees that this intrinsic does not access memory.
  bool maybeHandleSimpleNomemIntrinsic(IntrinsicInst &I) {
    Type *RetTy = I.getType();
    if (!(RetTy->isIntOrIntVectorTy() || RetTy->isFPOrFPVectorTy() ||
          RetTy->isX86_MMXTy()))
      return false;

    unsigned NumArgOperands = I.arg_size();
    for (unsigned i = 0; i < NumArgOperands; ++i) {
      Type *Ty = I.getArgOperand(i)->getType();
      if (Ty != RetTy)
        return false;
    }

    IRBuilder<> IRB(&I);
    ShadowAndOriginCombiner SC(this, IRB);
    for (unsigned i = 0; i < NumArgOperands; ++i)
      SC.Add(I.getArgOperand(i));
    SC.Done(&I);

    return true;
  }

  /// Heuristically instrument unknown intrinsics.
  ///
  /// The main purpose of this code is to do something reasonable with all
  /// random intrinsics we might encounter, most importantly - SIMD intrinsics.
  /// We recognize several classes of intrinsics by their argument types and
  /// ModRefBehaviour and apply special instrumentation when we are reasonably
  /// sure that we know what the intrinsic does.
  ///
  /// We special-case intrinsics where this approach fails. See llvm.bswap
  /// handling as an example of that.
  bool handleUnknownIntrinsic(IntrinsicInst &I) {
    unsigned NumArgOperands = I.arg_size();
    if (NumArgOperands == 0)
      return false;

    if (NumArgOperands == 2 && I.getArgOperand(0)->getType()->isPointerTy() &&
        I.getArgOperand(1)->getType()->isVectorTy() &&
        I.getType()->isVoidTy() && !I.onlyReadsMemory()) {
      // This looks like a vector store.
      return handleVectorStoreIntrinsic(I);
    }

    if (NumArgOperands == 1 && I.getArgOperand(0)->getType()->isPointerTy() &&
        I.getType()->isVectorTy() && I.onlyReadsMemory()) {
      // This looks like a vector load.
      return handleVectorLoadIntrinsic(I);
    }

    if (I.doesNotAccessMemory())
      if (maybeHandleSimpleNomemIntrinsic(I))
        return true;

    // FIXME: detect and handle SSE maskstore/maskload
    return false;
  }

  void handleInvariantGroup(IntrinsicInst &I) {
    setShadow(&I, getShadow(&I, 0));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void handleLifetimeStart(IntrinsicInst &I) {
    if (!PoisonStack)
      return;
    AllocaInst *AI = llvm::findAllocaForValue(I.getArgOperand(1));
    if (!AI)
      InstrumentLifetimeStart = false;
    LifetimeStartList.push_back(std::make_pair(&I, AI));
  }

  void handleBswap(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *Op = I.getArgOperand(0);
    Type *OpType = Op->getType();
    Function *BswapFunc = Intrinsic::getDeclaration(
        F.getParent(), Intrinsic::bswap, ArrayRef(&OpType, 1));
    setShadow(&I, IRB.CreateCall(BswapFunc, getShadow(Op)));
    setOrigin(&I, getOrigin(Op));
  }

  void handleCountZeroes(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *Src = I.getArgOperand(0);

    // Set the Output shadow based on input Shadow
    Value *BoolShadow = IRB.CreateIsNotNull(getShadow(Src), "_mscz_bs");

    // If zero poison is requested, mix in with the shadow
    Constant *IsZeroPoison = cast<Constant>(I.getOperand(1));
    if (!IsZeroPoison->isZeroValue()) {
      Value *BoolZeroPoison = IRB.CreateIsNull(Src, "_mscz_bzp");
      BoolShadow = IRB.CreateOr(BoolShadow, BoolZeroPoison, "_mscz_bs");
    }

    Value *OutputShadow =
        IRB.CreateSExt(BoolShadow, getShadowTy(Src), "_mscz_os");

    setShadow(&I, OutputShadow);
    setOriginForNaryOp(I);
  }

  // Instrument vector convert intrinsic.
  //
  // This function instruments intrinsics like cvtsi2ss:
  // %Out = int_xxx_cvtyyy(%ConvertOp)
  // or
  // %Out = int_xxx_cvtyyy(%CopyOp, %ConvertOp)
  // Intrinsic converts \p NumUsedElements elements of \p ConvertOp to the same
  // number \p Out elements, and (if has 2 arguments) copies the rest of the
  // elements from \p CopyOp.
  // In most cases conversion involves floating-point value which may trigger a
  // hardware exception when not fully initialized. For this reason we require
  // \p ConvertOp[0:NumUsedElements] to be fully initialized and trap otherwise.
  // We copy the shadow of \p CopyOp[NumUsedElements:] to \p
  // Out[NumUsedElements:]. This means that intrinsics without \p CopyOp always
  // return a fully initialized value.
  void handleVectorConvertIntrinsic(IntrinsicInst &I, int NumUsedElements,
                                    bool HasRoundingMode = false) {
    IRBuilder<> IRB(&I);
    Value *CopyOp, *ConvertOp;

    assert((!HasRoundingMode ||
            isa<ConstantInt>(I.getArgOperand(I.arg_size() - 1))) &&
           "Invalid rounding mode");

    switch (I.arg_size() - HasRoundingMode) {
    case 2:
      CopyOp = I.getArgOperand(0);
      ConvertOp = I.getArgOperand(1);
      break;
    case 1:
      ConvertOp = I.getArgOperand(0);
      CopyOp = nullptr;
      break;
    default:
      llvm_unreachable("Cvt intrinsic with unsupported number of arguments.");
    }

    // The first *NumUsedElements* elements of ConvertOp are converted to the
    // same number of output elements. The rest of the output is copied from
    // CopyOp, or (if not available) filled with zeroes.
    // Combine shadow for elements of ConvertOp that are used in this operation,
    // and insert a check.
    // FIXME: consider propagating shadow of ConvertOp, at least in the case of
    // int->any conversion.
    Value *ConvertShadow = getShadow(ConvertOp);
    Value *AggShadow = nullptr;
    if (ConvertOp->getType()->isVectorTy()) {
      AggShadow = IRB.CreateExtractElement(
          ConvertShadow, ConstantInt::get(IRB.getInt32Ty(), 0));
      for (int i = 1; i < NumUsedElements; ++i) {
        Value *MoreShadow = IRB.CreateExtractElement(
            ConvertShadow, ConstantInt::get(IRB.getInt32Ty(), i));
        AggShadow = IRB.CreateOr(AggShadow, MoreShadow);
      }
    } else {
      AggShadow = ConvertShadow;
    }
    assert(AggShadow->getType()->isIntegerTy());
    insertShadowCheck(AggShadow, getOrigin(ConvertOp), &I);

    // Build result shadow by zero-filling parts of CopyOp shadow that come from
    // ConvertOp.
    if (CopyOp) {
      assert(CopyOp->getType() == I.getType());
      assert(CopyOp->getType()->isVectorTy());
      Value *ResultShadow = getShadow(CopyOp);
      Type *EltTy = cast<VectorType>(ResultShadow->getType())->getElementType();
      for (int i = 0; i < NumUsedElements; ++i) {
        ResultShadow = IRB.CreateInsertElement(
            ResultShadow, ConstantInt::getNullValue(EltTy),
            ConstantInt::get(IRB.getInt32Ty(), i));
      }
      setShadow(&I, ResultShadow);
      setOrigin(&I, getOrigin(CopyOp));
    } else {
      setShadow(&I, getCleanShadow(&I));
      setOrigin(&I, getCleanOrigin());
    }
  }

  // Given a scalar or vector, extract lower 64 bits (or less), and return all
  // zeroes if it is zero, and all ones otherwise.
  Value *Lower64ShadowExtend(IRBuilder<> &IRB, Value *S, Type *T) {
    if (S->getType()->isVectorTy())
      S = CreateShadowCast(IRB, S, IRB.getInt64Ty(), /* Signed */ true);
    assert(S->getType()->getPrimitiveSizeInBits() <= 64);
    Value *S2 = IRB.CreateICmpNE(S, getCleanShadow(S));
    return CreateShadowCast(IRB, S2, T, /* Signed */ true);
  }

  // Given a vector, extract its first element, and return all
  // zeroes if it is zero, and all ones otherwise.
  Value *LowerElementShadowExtend(IRBuilder<> &IRB, Value *S, Type *T) {
    Value *S1 = IRB.CreateExtractElement(S, (uint64_t)0);
    Value *S2 = IRB.CreateICmpNE(S1, getCleanShadow(S1));
    return CreateShadowCast(IRB, S2, T, /* Signed */ true);
  }

  Value *VariableShadowExtend(IRBuilder<> &IRB, Value *S) {
    Type *T = S->getType();
    assert(T->isVectorTy());
    Value *S2 = IRB.CreateICmpNE(S, getCleanShadow(S));
    return IRB.CreateSExt(S2, T);
  }

  // Instrument vector shift intrinsic.
  //
  // This function instruments intrinsics like int_x86_avx2_psll_w.
  // Intrinsic shifts %In by %ShiftSize bits.
  // %ShiftSize may be a vector. In that case the lower 64 bits determine shift
  // size, and the rest is ignored. Behavior is defined even if shift size is
  // greater than register (or field) width.
  void handleVectorShiftIntrinsic(IntrinsicInst &I, bool Variable) {
    assert(I.arg_size() == 2);
    IRBuilder<> IRB(&I);
    // If any of the S2 bits are poisoned, the whole thing is poisoned.
    // Otherwise perform the same shift on S1.
    Value *S1 = getShadow(&I, 0);
    Value *S2 = getShadow(&I, 1);
    Value *S2Conv = Variable ? VariableShadowExtend(IRB, S2)
                             : Lower64ShadowExtend(IRB, S2, getShadowTy(&I));
    Value *V1 = I.getOperand(0);
    Value *V2 = I.getOperand(1);
    Value *Shift = IRB.CreateCall(I.getFunctionType(), I.getCalledOperand(),
                                  {IRB.CreateBitCast(S1, V1->getType()), V2});
    Shift = IRB.CreateBitCast(Shift, getShadowTy(&I));
    setShadow(&I, IRB.CreateOr(Shift, S2Conv));
    setOriginForNaryOp(I);
  }

  // Get an X86_MMX-sized vector type.
  Type *getMMXVectorTy(unsigned EltSizeInBits) {
    const unsigned X86_MMXSizeInBits = 64;
    assert(EltSizeInBits != 0 && (X86_MMXSizeInBits % EltSizeInBits) == 0 &&
           "Illegal MMX vector element size");
    return FixedVectorType::get(IntegerType::get(*MS.C, EltSizeInBits),
                                X86_MMXSizeInBits / EltSizeInBits);
  }

  // Returns a signed counterpart for an (un)signed-saturate-and-pack
  // intrinsic.
  Intrinsic::ID getSignedPackIntrinsic(Intrinsic::ID id) {
    switch (id) {
    case Intrinsic::x86_sse2_packsswb_128:
    case Intrinsic::x86_sse2_packuswb_128:
      return Intrinsic::x86_sse2_packsswb_128;

    case Intrinsic::x86_sse2_packssdw_128:
    case Intrinsic::x86_sse41_packusdw:
      return Intrinsic::x86_sse2_packssdw_128;

    case Intrinsic::x86_avx2_packsswb:
    case Intrinsic::x86_avx2_packuswb:
      return Intrinsic::x86_avx2_packsswb;

    case Intrinsic::x86_avx2_packssdw:
    case Intrinsic::x86_avx2_packusdw:
      return Intrinsic::x86_avx2_packssdw;

    case Intrinsic::x86_mmx_packsswb:
    case Intrinsic::x86_mmx_packuswb:
      return Intrinsic::x86_mmx_packsswb;

    case Intrinsic::x86_mmx_packssdw:
      return Intrinsic::x86_mmx_packssdw;
    default:
      llvm_unreachable("unexpected intrinsic id");
    }
  }

  // Instrument vector pack intrinsic.
  //
  // This function instruments intrinsics like x86_mmx_packsswb, that
  // packs elements of 2 input vectors into half as many bits with saturation.
  // Shadow is propagated with the signed variant of the same intrinsic applied
  // to sext(Sa != zeroinitializer), sext(Sb != zeroinitializer).
  // EltSizeInBits is used only for x86mmx arguments.
  void handleVectorPackIntrinsic(IntrinsicInst &I, unsigned EltSizeInBits = 0) {
    assert(I.arg_size() == 2);
    bool isX86_MMX = I.getOperand(0)->getType()->isX86_MMXTy();
    IRBuilder<> IRB(&I);
    Value *S1 = getShadow(&I, 0);
    Value *S2 = getShadow(&I, 1);
    assert(isX86_MMX || S1->getType()->isVectorTy());

    // SExt and ICmpNE below must apply to individual elements of input vectors.
    // In case of x86mmx arguments, cast them to appropriate vector types and
    // back.
    Type *T = isX86_MMX ? getMMXVectorTy(EltSizeInBits) : S1->getType();
    if (isX86_MMX) {
      S1 = IRB.CreateBitCast(S1, T);
      S2 = IRB.CreateBitCast(S2, T);
    }
    Value *S1_ext =
        IRB.CreateSExt(IRB.CreateICmpNE(S1, Constant::getNullValue(T)), T);
    Value *S2_ext =
        IRB.CreateSExt(IRB.CreateICmpNE(S2, Constant::getNullValue(T)), T);
    if (isX86_MMX) {
      Type *X86_MMXTy = Type::getX86_MMXTy(*MS.C);
      S1_ext = IRB.CreateBitCast(S1_ext, X86_MMXTy);
      S2_ext = IRB.CreateBitCast(S2_ext, X86_MMXTy);
    }

    Function *ShadowFn = Intrinsic::getDeclaration(
        F.getParent(), getSignedPackIntrinsic(I.getIntrinsicID()));

    Value *S =
        IRB.CreateCall(ShadowFn, {S1_ext, S2_ext}, "_msprop_vector_pack");
    if (isX86_MMX)
      S = IRB.CreateBitCast(S, getShadowTy(&I));
    setShadow(&I, S);
    setOriginForNaryOp(I);
  }

  // Convert `Mask` into `<n x i1>`.
  Constant *createDppMask(unsigned Width, unsigned Mask) {
    SmallVector<Constant *, 4> R(Width);
    for (auto &M : R) {
      M = ConstantInt::getBool(F.getContext(), Mask & 1);
      Mask >>= 1;
    }
    return ConstantVector::get(R);
  }

  // Calculate output shadow as array of booleans `<n x i1>`, assuming if any
  // arg is poisoned, entire dot product is poisoned.
  Value *findDppPoisonedOutput(IRBuilder<> &IRB, Value *S, unsigned SrcMask,
                               unsigned DstMask) {
    const unsigned Width =
        cast<FixedVectorType>(S->getType())->getNumElements();

    S = IRB.CreateSelect(createDppMask(Width, SrcMask), S,
                         Constant::getNullValue(S->getType()));
    Value *SElem = IRB.CreateOrReduce(S);
    Value *IsClean = IRB.CreateIsNull(SElem, "_msdpp");
    Value *DstMaskV = createDppMask(Width, DstMask);

    return IRB.CreateSelect(
        IsClean, Constant::getNullValue(DstMaskV->getType()), DstMaskV);
  }

  // See `Intel Intrinsics Guide` for `_dp_p*` instructions.
  //
  // 2 and 4 element versions produce single scalar of dot product, and then
  // puts it into elements of output vector, selected by 4 lowest bits of the
  // mask. Top 4 bits of the mask control which elements of input to use for dot
  // product.
  //
  // 8 element version mask still has only 4 bit for input, and 4 bit for output
  // mask. According to the spec it just operates as 4 element version on first
  // 4 elements of inputs and output, and then on last 4 elements of inputs and
  // output.
  void handleDppIntrinsic(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);

    Value *S0 = getShadow(&I, 0);
    Value *S1 = getShadow(&I, 1);
    Value *S = IRB.CreateOr(S0, S1);

    const unsigned Width =
        cast<FixedVectorType>(S->getType())->getNumElements();
    assert(Width == 2 || Width == 4 || Width == 8);

    const unsigned Mask = cast<ConstantInt>(I.getArgOperand(2))->getZExtValue();
    const unsigned SrcMask = Mask >> 4;
    const unsigned DstMask = Mask & 0xf;

    // Calculate shadow as `<n x i1>`.
    Value *SI1 = findDppPoisonedOutput(IRB, S, SrcMask, DstMask);
    if (Width == 8) {
      // First 4 elements of shadow are already calculated. `makeDppShadow`
      // operats on 32 bit masks, so we can just shift masks, and repeat.
      SI1 = IRB.CreateOr(
          SI1, findDppPoisonedOutput(IRB, S, SrcMask << 4, DstMask << 4));
    }
    // Extend to real size of shadow, poisoning either all or none bits of an
    // element.
    S = IRB.CreateSExt(SI1, S->getType(), "_msdpp");

    setShadow(&I, S);
    setOriginForNaryOp(I);
  }

  Value *convertBlendvToSelectMask(IRBuilder<> &IRB, Value *C) {
    C = CreateAppToShadowCast(IRB, C);
    FixedVectorType *FVT = cast<FixedVectorType>(C->getType());
    unsigned ElSize = FVT->getElementType()->getPrimitiveSizeInBits();
    C = IRB.CreateAShr(C, ElSize - 1);
    FVT = FixedVectorType::get(IRB.getInt1Ty(), FVT->getNumElements());
    return IRB.CreateTrunc(C, FVT);
  }

  // `blendv(f, t, c)` is effectively `select(c[top_bit], t, f)`.
  void handleBlendvIntrinsic(IntrinsicInst &I) {
    Value *C = I.getOperand(2);
    Value *T = I.getOperand(1);
    Value *F = I.getOperand(0);

    Value *Sc = getShadow(&I, 2);
    Value *Oc = MS.TrackOrigins ? getOrigin(C) : nullptr;

    {
      IRBuilder<> IRB(&I);
      // Extract top bit from condition and its shadow.
      C = convertBlendvToSelectMask(IRB, C);
      Sc = convertBlendvToSelectMask(IRB, Sc);

      setShadow(C, Sc);
      setOrigin(C, Oc);
    }

    handleSelectLikeInst(I, C, T, F);
  }

  // Instrument sum-of-absolute-differences intrinsic.
  void handleVectorSadIntrinsic(IntrinsicInst &I) {
    const unsigned SignificantBitsPerResultElement = 16;
    bool isX86_MMX = I.getOperand(0)->getType()->isX86_MMXTy();
    Type *ResTy = isX86_MMX ? IntegerType::get(*MS.C, 64) : I.getType();
    unsigned ZeroBitsPerResultElement =
        ResTy->getScalarSizeInBits() - SignificantBitsPerResultElement;

    IRBuilder<> IRB(&I);
    auto *Shadow0 = getShadow(&I, 0);
    auto *Shadow1 = getShadow(&I, 1);
    Value *S = IRB.CreateOr(Shadow0, Shadow1);
    S = IRB.CreateBitCast(S, ResTy);
    S = IRB.CreateSExt(IRB.CreateICmpNE(S, Constant::getNullValue(ResTy)),
                       ResTy);
    S = IRB.CreateLShr(S, ZeroBitsPerResultElement);
    S = IRB.CreateBitCast(S, getShadowTy(&I));
    setShadow(&I, S);
    setOriginForNaryOp(I);
  }

  // Instrument multiply-add intrinsic.
  void handleVectorPmaddIntrinsic(IntrinsicInst &I,
                                  unsigned EltSizeInBits = 0) {
    bool isX86_MMX = I.getOperand(0)->getType()->isX86_MMXTy();
    Type *ResTy = isX86_MMX ? getMMXVectorTy(EltSizeInBits * 2) : I.getType();
    IRBuilder<> IRB(&I);
    auto *Shadow0 = getShadow(&I, 0);
    auto *Shadow1 = getShadow(&I, 1);
    Value *S = IRB.CreateOr(Shadow0, Shadow1);
    S = IRB.CreateBitCast(S, ResTy);
    S = IRB.CreateSExt(IRB.CreateICmpNE(S, Constant::getNullValue(ResTy)),
                       ResTy);
    S = IRB.CreateBitCast(S, getShadowTy(&I));
    setShadow(&I, S);
    setOriginForNaryOp(I);
  }

  // Instrument compare-packed intrinsic.
  // Basically, an or followed by sext(icmp ne 0) to end up with all-zeros or
  // all-ones shadow.
  void handleVectorComparePackedIntrinsic(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Type *ResTy = getShadowTy(&I);
    auto *Shadow0 = getShadow(&I, 0);
    auto *Shadow1 = getShadow(&I, 1);
    Value *S0 = IRB.CreateOr(Shadow0, Shadow1);
    Value *S = IRB.CreateSExt(
        IRB.CreateICmpNE(S0, Constant::getNullValue(ResTy)), ResTy);
    setShadow(&I, S);
    setOriginForNaryOp(I);
  }

  // Instrument compare-scalar intrinsic.
  // This handles both cmp* intrinsics which return the result in the first
  // element of a vector, and comi* which return the result as i32.
  void handleVectorCompareScalarIntrinsic(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    auto *Shadow0 = getShadow(&I, 0);
    auto *Shadow1 = getShadow(&I, 1);
    Value *S0 = IRB.CreateOr(Shadow0, Shadow1);
    Value *S = LowerElementShadowExtend(IRB, S0, getShadowTy(&I));
    setShadow(&I, S);
    setOriginForNaryOp(I);
  }

  // Instrument generic vector reduction intrinsics
  // by ORing together all their fields.
  void handleVectorReduceIntrinsic(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *S = IRB.CreateOrReduce(getShadow(&I, 0));
    setShadow(&I, S);
    setOrigin(&I, getOrigin(&I, 0));
  }

  // Instrument vector.reduce.or intrinsic.
  // Valid (non-poisoned) set bits in the operand pull low the
  // corresponding shadow bits.
  void handleVectorReduceOrIntrinsic(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *OperandShadow = getShadow(&I, 0);
    Value *OperandUnsetBits = IRB.CreateNot(I.getOperand(0));
    Value *OperandUnsetOrPoison = IRB.CreateOr(OperandUnsetBits, OperandShadow);
    // Bit N is clean if any field's bit N is 1 and unpoison
    Value *OutShadowMask = IRB.CreateAndReduce(OperandUnsetOrPoison);
    // Otherwise, it is clean if every field's bit N is unpoison
    Value *OrShadow = IRB.CreateOrReduce(OperandShadow);
    Value *S = IRB.CreateAnd(OutShadowMask, OrShadow);

    setShadow(&I, S);
    setOrigin(&I, getOrigin(&I, 0));
  }

  // Instrument vector.reduce.and intrinsic.
  // Valid (non-poisoned) unset bits in the operand pull down the
  // corresponding shadow bits.
  void handleVectorReduceAndIntrinsic(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *OperandShadow = getShadow(&I, 0);
    Value *OperandSetOrPoison = IRB.CreateOr(I.getOperand(0), OperandShadow);
    // Bit N is clean if any field's bit N is 0 and unpoison
    Value *OutShadowMask = IRB.CreateAndReduce(OperandSetOrPoison);
    // Otherwise, it is clean if every field's bit N is unpoison
    Value *OrShadow = IRB.CreateOrReduce(OperandShadow);
    Value *S = IRB.CreateAnd(OutShadowMask, OrShadow);

    setShadow(&I, S);
    setOrigin(&I, getOrigin(&I, 0));
  }

  void handleStmxcsr(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *Addr = I.getArgOperand(0);
    Type *Ty = IRB.getInt32Ty();
    Value *ShadowPtr =
        getShadowOriginPtr(Addr, IRB, Ty, Align(1), /*isStore*/ true).first;

    IRB.CreateStore(getCleanShadow(Ty), ShadowPtr);

    if (ClCheckAccessAddress)
      insertShadowCheck(Addr, &I);
  }

  void handleLdmxcsr(IntrinsicInst &I) {
    if (!InsertChecks)
      return;

    IRBuilder<> IRB(&I);
    Value *Addr = I.getArgOperand(0);
    Type *Ty = IRB.getInt32Ty();
    const Align Alignment = Align(1);
    Value *ShadowPtr, *OriginPtr;
    std::tie(ShadowPtr, OriginPtr) =
        getShadowOriginPtr(Addr, IRB, Ty, Alignment, /*isStore*/ false);

    if (ClCheckAccessAddress)
      insertShadowCheck(Addr, &I);

    Value *Shadow = IRB.CreateAlignedLoad(Ty, ShadowPtr, Alignment, "_ldmxcsr");
    Value *Origin = MS.TrackOrigins ? IRB.CreateLoad(MS.OriginTy, OriginPtr)
                                    : getCleanOrigin();
    insertShadowCheck(Shadow, Origin, &I);
  }

  void handleMaskedExpandLoad(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *Ptr = I.getArgOperand(0);
    Value *Mask = I.getArgOperand(1);
    Value *PassThru = I.getArgOperand(2);

    if (ClCheckAccessAddress) {
      insertShadowCheck(Ptr, &I);
      insertShadowCheck(Mask, &I);
    }

    if (!PropagateShadow) {
      setShadow(&I, getCleanShadow(&I));
      setOrigin(&I, getCleanOrigin());
      return;
    }

    Type *ShadowTy = getShadowTy(&I);
    Type *ElementShadowTy = cast<VectorType>(ShadowTy)->getElementType();
    auto [ShadowPtr, OriginPtr] =
        getShadowOriginPtr(Ptr, IRB, ElementShadowTy, {}, /*isStore*/ false);

    Value *Shadow = IRB.CreateMaskedExpandLoad(
        ShadowTy, ShadowPtr, Mask, getShadow(PassThru), "_msmaskedexpload");

    setShadow(&I, Shadow);

    // TODO: Store origins.
    setOrigin(&I, getCleanOrigin());
  }

  void handleMaskedCompressStore(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *Values = I.getArgOperand(0);
    Value *Ptr = I.getArgOperand(1);
    Value *Mask = I.getArgOperand(2);

    if (ClCheckAccessAddress) {
      insertShadowCheck(Ptr, &I);
      insertShadowCheck(Mask, &I);
    }

    Value *Shadow = getShadow(Values);
    Type *ElementShadowTy =
        getShadowTy(cast<VectorType>(Values->getType())->getElementType());
    auto [ShadowPtr, OriginPtrs] =
        getShadowOriginPtr(Ptr, IRB, ElementShadowTy, {}, /*isStore*/ true);

    IRB.CreateMaskedCompressStore(Shadow, ShadowPtr, Mask);

    // TODO: Store origins.
  }

  void handleMaskedGather(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *Ptrs = I.getArgOperand(0);
    const Align Alignment(
        cast<ConstantInt>(I.getArgOperand(1))->getZExtValue());
    Value *Mask = I.getArgOperand(2);
    Value *PassThru = I.getArgOperand(3);

    Type *PtrsShadowTy = getShadowTy(Ptrs);
    if (ClCheckAccessAddress) {
      insertShadowCheck(Mask, &I);
      Value *MaskedPtrShadow = IRB.CreateSelect(
          Mask, getShadow(Ptrs), Constant::getNullValue((PtrsShadowTy)),
          "_msmaskedptrs");
      insertShadowCheck(MaskedPtrShadow, getOrigin(Ptrs), &I);
    }

    if (!PropagateShadow) {
      setShadow(&I, getCleanShadow(&I));
      setOrigin(&I, getCleanOrigin());
      return;
    }

    Type *ShadowTy = getShadowTy(&I);
    Type *ElementShadowTy = cast<VectorType>(ShadowTy)->getElementType();
    auto [ShadowPtrs, OriginPtrs] = getShadowOriginPtr(
        Ptrs, IRB, ElementShadowTy, Alignment, /*isStore*/ false);

    Value *Shadow =
        IRB.CreateMaskedGather(ShadowTy, ShadowPtrs, Alignment, Mask,
                               getShadow(PassThru), "_msmaskedgather");

    setShadow(&I, Shadow);

    // TODO: Store origins.
    setOrigin(&I, getCleanOrigin());
  }

  void handleMaskedScatter(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *Values = I.getArgOperand(0);
    Value *Ptrs = I.getArgOperand(1);
    const Align Alignment(
        cast<ConstantInt>(I.getArgOperand(2))->getZExtValue());
    Value *Mask = I.getArgOperand(3);

    Type *PtrsShadowTy = getShadowTy(Ptrs);
    if (ClCheckAccessAddress) {
      insertShadowCheck(Mask, &I);
      Value *MaskedPtrShadow = IRB.CreateSelect(
          Mask, getShadow(Ptrs), Constant::getNullValue((PtrsShadowTy)),
          "_msmaskedptrs");
      insertShadowCheck(MaskedPtrShadow, getOrigin(Ptrs), &I);
    }

    Value *Shadow = getShadow(Values);
    Type *ElementShadowTy =
        getShadowTy(cast<VectorType>(Values->getType())->getElementType());
    auto [ShadowPtrs, OriginPtrs] = getShadowOriginPtr(
        Ptrs, IRB, ElementShadowTy, Alignment, /*isStore*/ true);

    IRB.CreateMaskedScatter(Shadow, ShadowPtrs, Alignment, Mask);

    // TODO: Store origin.
  }

  void handleMaskedStore(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *V = I.getArgOperand(0);
    Value *Ptr = I.getArgOperand(1);
    const Align Alignment(
        cast<ConstantInt>(I.getArgOperand(2))->getZExtValue());
    Value *Mask = I.getArgOperand(3);
    Value *Shadow = getShadow(V);

    if (ClCheckAccessAddress) {
      insertShadowCheck(Ptr, &I);
      insertShadowCheck(Mask, &I);
    }

    Value *ShadowPtr;
    Value *OriginPtr;
    std::tie(ShadowPtr, OriginPtr) = getShadowOriginPtr(
        Ptr, IRB, Shadow->getType(), Alignment, /*isStore*/ true);

    IRB.CreateMaskedStore(Shadow, ShadowPtr, Alignment, Mask);

    if (!MS.TrackOrigins)
      return;

    auto &DL = F.getDataLayout();
    paintOrigin(IRB, getOrigin(V), OriginPtr,
                DL.getTypeStoreSize(Shadow->getType()),
                std::max(Alignment, kMinOriginAlignment));
  }

  void handleMaskedLoad(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *Ptr = I.getArgOperand(0);
    const Align Alignment(
        cast<ConstantInt>(I.getArgOperand(1))->getZExtValue());
    Value *Mask = I.getArgOperand(2);
    Value *PassThru = I.getArgOperand(3);

    if (ClCheckAccessAddress) {
      insertShadowCheck(Ptr, &I);
      insertShadowCheck(Mask, &I);
    }

    if (!PropagateShadow) {
      setShadow(&I, getCleanShadow(&I));
      setOrigin(&I, getCleanOrigin());
      return;
    }

    Type *ShadowTy = getShadowTy(&I);
    Value *ShadowPtr, *OriginPtr;
    std::tie(ShadowPtr, OriginPtr) =
        getShadowOriginPtr(Ptr, IRB, ShadowTy, Alignment, /*isStore*/ false);
    setShadow(&I, IRB.CreateMaskedLoad(ShadowTy, ShadowPtr, Alignment, Mask,
                                       getShadow(PassThru), "_msmaskedld"));

    if (!MS.TrackOrigins)
      return;

    // Choose between PassThru's and the loaded value's origins.
    Value *MaskedPassThruShadow = IRB.CreateAnd(
        getShadow(PassThru), IRB.CreateSExt(IRB.CreateNeg(Mask), ShadowTy));

    Value *NotNull = convertToBool(MaskedPassThruShadow, IRB, "_mscmp");

    Value *PtrOrigin = IRB.CreateLoad(MS.OriginTy, OriginPtr);
    Value *Origin = IRB.CreateSelect(NotNull, getOrigin(PassThru), PtrOrigin);

    setOrigin(&I, Origin);
  }

  // Instrument BMI / BMI2 intrinsics.
  // All of these intrinsics are Z = I(X, Y)
  // where the types of all operands and the result match, and are either i32 or
  // i64. The following instrumentation happens to work for all of them:
  //   Sz = I(Sx, Y) | (sext (Sy != 0))
  void handleBmiIntrinsic(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Type *ShadowTy = getShadowTy(&I);

    // If any bit of the mask operand is poisoned, then the whole thing is.
    Value *SMask = getShadow(&I, 1);
    SMask = IRB.CreateSExt(IRB.CreateICmpNE(SMask, getCleanShadow(ShadowTy)),
                           ShadowTy);
    // Apply the same intrinsic to the shadow of the first operand.
    Value *S = IRB.CreateCall(I.getCalledFunction(),
                              {getShadow(&I, 0), I.getOperand(1)});
    S = IRB.CreateOr(SMask, S);
    setShadow(&I, S);
    setOriginForNaryOp(I);
  }

  static SmallVector<int, 8> getPclmulMask(unsigned Width, bool OddElements) {
    SmallVector<int, 8> Mask;
    for (unsigned X = OddElements ? 1 : 0; X < Width; X += 2) {
      Mask.append(2, X);
    }
    return Mask;
  }

  // Instrument pclmul intrinsics.
  // These intrinsics operate either on odd or on even elements of the input
  // vectors, depending on the constant in the 3rd argument, ignoring the rest.
  // Replace the unused elements with copies of the used ones, ex:
  //   (0, 1, 2, 3) -> (0, 0, 2, 2) (even case)
  // or
  //   (0, 1, 2, 3) -> (1, 1, 3, 3) (odd case)
  // and then apply the usual shadow combining logic.
  void handlePclmulIntrinsic(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    unsigned Width =
        cast<FixedVectorType>(I.getArgOperand(0)->getType())->getNumElements();
    assert(isa<ConstantInt>(I.getArgOperand(2)) &&
           "pclmul 3rd operand must be a constant");
    unsigned Imm = cast<ConstantInt>(I.getArgOperand(2))->getZExtValue();
    Value *Shuf0 = IRB.CreateShuffleVector(getShadow(&I, 0),
                                           getPclmulMask(Width, Imm & 0x01));
    Value *Shuf1 = IRB.CreateShuffleVector(getShadow(&I, 1),
                                           getPclmulMask(Width, Imm & 0x10));
    ShadowAndOriginCombiner SOC(this, IRB);
    SOC.Add(Shuf0, getOrigin(&I, 0));
    SOC.Add(Shuf1, getOrigin(&I, 1));
    SOC.Done(&I);
  }

  // Instrument _mm_*_sd|ss intrinsics
  void handleUnarySdSsIntrinsic(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    unsigned Width =
        cast<FixedVectorType>(I.getArgOperand(0)->getType())->getNumElements();
    Value *First = getShadow(&I, 0);
    Value *Second = getShadow(&I, 1);
    // First element of second operand, remaining elements of first operand
    SmallVector<int, 16> Mask;
    Mask.push_back(Width);
    for (unsigned i = 1; i < Width; i++)
      Mask.push_back(i);
    Value *Shadow = IRB.CreateShuffleVector(First, Second, Mask);

    setShadow(&I, Shadow);
    setOriginForNaryOp(I);
  }

  void handleVtestIntrinsic(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *Shadow0 = getShadow(&I, 0);
    Value *Shadow1 = getShadow(&I, 1);
    Value *Or = IRB.CreateOr(Shadow0, Shadow1);
    Value *NZ = IRB.CreateICmpNE(Or, Constant::getNullValue(Or->getType()));
    Value *Scalar = convertShadowToScalar(NZ, IRB);
    Value *Shadow = IRB.CreateZExt(Scalar, getShadowTy(&I));

    setShadow(&I, Shadow);
    setOriginForNaryOp(I);
  }

  void handleBinarySdSsIntrinsic(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    unsigned Width =
        cast<FixedVectorType>(I.getArgOperand(0)->getType())->getNumElements();
    Value *First = getShadow(&I, 0);
    Value *Second = getShadow(&I, 1);
    Value *OrShadow = IRB.CreateOr(First, Second);
    // First element of both OR'd together, remaining elements of first operand
    SmallVector<int, 16> Mask;
    Mask.push_back(Width);
    for (unsigned i = 1; i < Width; i++)
      Mask.push_back(i);
    Value *Shadow = IRB.CreateShuffleVector(First, OrShadow, Mask);

    setShadow(&I, Shadow);
    setOriginForNaryOp(I);
  }

  // Instrument abs intrinsic.
  // handleUnknownIntrinsic can't handle it because of the last
  // is_int_min_poison argument which does not match the result type.
  void handleAbsIntrinsic(IntrinsicInst &I) {
    assert(I.getType()->isIntOrIntVectorTy());
    assert(I.getArgOperand(0)->getType() == I.getType());

    // FIXME: Handle is_int_min_poison.
    IRBuilder<> IRB(&I);
    setShadow(&I, getShadow(&I, 0));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void handleIsFpClass(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *Shadow = getShadow(&I, 0);
    setShadow(&I, IRB.CreateICmpNE(Shadow, getCleanShadow(Shadow)));
    setOrigin(&I, getOrigin(&I, 0));
  }

  void handleArithmeticWithOverflow(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *Shadow0 = getShadow(&I, 0);
    Value *Shadow1 = getShadow(&I, 1);
    Value *ShadowElt0 = IRB.CreateOr(Shadow0, Shadow1);
    Value *ShadowElt1 =
        IRB.CreateICmpNE(ShadowElt0, getCleanShadow(ShadowElt0));

    Value *Shadow = PoisonValue::get(getShadowTy(&I));
    Shadow = IRB.CreateInsertValue(Shadow, ShadowElt0, 0);
    Shadow = IRB.CreateInsertValue(Shadow, ShadowElt1, 1);

    setShadow(&I, Shadow);
    setOriginForNaryOp(I);
  }

  /// Handle Arm NEON vector store intrinsics (vst{2,3,4}).
  ///
  /// Arm NEON vector store intrinsics have the output address (pointer) as the
  /// last argument, with the initial arguments being the inputs. They return
  /// void.
  void handleNEONVectorStoreIntrinsic(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);

    // Don't use getNumOperands() because it includes the callee
    int numArgOperands = I.arg_size();
    assert(numArgOperands >= 1);

    // The last arg operand is the output
    Value *Addr = I.getArgOperand(numArgOperands - 1);
    assert(Addr->getType()->isPointerTy());

    if (ClCheckAccessAddress)
      insertShadowCheck(Addr, &I);

    // Every arg operand, other than the last one, is an input vector
    IntrinsicInst *ShadowI = cast<IntrinsicInst>(I.clone());
    for (int i = 0; i < numArgOperands - 1; i++) {
      assert(isa<FixedVectorType>(I.getArgOperand(i)->getType()));
      ShadowI->setArgOperand(i, getShadow(&I, i));
    }

    // MSan's GetShadowTy assumes the LHS is the type we want the shadow for
    // e.g., for:
    //     [[TMP5:%.*]] = bitcast <16 x i8> [[TMP2]] to i128
    // we know the type of the output (and its shadow) is <16 x i8>.
    //
    // Arm NEON VST is unusual because the last argument is the output address:
    //     define void @st2_16b(<16 x i8> %A, <16 x i8> %B, ptr %P) {
    //         call void @llvm.aarch64.neon.st2.v16i8.p0
    //                   (<16 x i8> [[A]], <16 x i8> [[B]], ptr [[P]])
    // and we have no type information about P's operand. We must manually
    // compute the type (<16 x i8> x 2).
    FixedVectorType *OutputVectorTy = FixedVectorType::get(
        cast<FixedVectorType>(I.getArgOperand(0)->getType())->getElementType(),
        cast<FixedVectorType>(I.getArgOperand(0)->getType())->getNumElements() *
            (numArgOperands - 1));
    Type *ShadowTy = getShadowTy(OutputVectorTy);
    Value *ShadowPtr, *OriginPtr;
    // AArch64 NEON does not need alignment (unless OS requires it)
    std::tie(ShadowPtr, OriginPtr) =
        getShadowOriginPtr(Addr, IRB, ShadowTy, Align(1), /*isStore*/ true);
    ShadowI->setArgOperand(numArgOperands - 1, ShadowPtr);
    ShadowI->insertAfter(&I);

    if (MS.TrackOrigins) {
      // TODO: if we modelled the vst* instruction more precisely, we could
      // more accurately track the origins (e.g., if both inputs are
      // uninitialized for vst2, we currently blame the second input, even
      // though part of the output depends only on the first input).
      OriginCombiner OC(this, IRB);
      for (int i = 0; i < numArgOperands - 1; i++)
        OC.Add(I.getArgOperand(i));

      const DataLayout &DL = F.getDataLayout();
      OC.DoneAndStoreOrigin(DL.getTypeStoreSize(OutputVectorTy), OriginPtr);
    }
  }

  void visitIntrinsicInst(IntrinsicInst &I) {
    switch (I.getIntrinsicID()) {
    case Intrinsic::uadd_with_overflow:
    case Intrinsic::sadd_with_overflow:
    case Intrinsic::usub_with_overflow:
    case Intrinsic::ssub_with_overflow:
    case Intrinsic::umul_with_overflow:
    case Intrinsic::smul_with_overflow:
      handleArithmeticWithOverflow(I);
      break;
    case Intrinsic::abs:
      handleAbsIntrinsic(I);
      break;
    case Intrinsic::is_fpclass:
      handleIsFpClass(I);
      break;
    case Intrinsic::lifetime_start:
      handleLifetimeStart(I);
      break;
    case Intrinsic::launder_invariant_group:
    case Intrinsic::strip_invariant_group:
      handleInvariantGroup(I);
      break;
    case Intrinsic::bswap:
      handleBswap(I);
      break;
    case Intrinsic::ctlz:
    case Intrinsic::cttz:
      handleCountZeroes(I);
      break;
    case Intrinsic::masked_compressstore:
      handleMaskedCompressStore(I);
      break;
    case Intrinsic::masked_expandload:
      handleMaskedExpandLoad(I);
      break;
    case Intrinsic::masked_gather:
      handleMaskedGather(I);
      break;
    case Intrinsic::masked_scatter:
      handleMaskedScatter(I);
      break;
    case Intrinsic::masked_store:
      handleMaskedStore(I);
      break;
    case Intrinsic::masked_load:
      handleMaskedLoad(I);
      break;
    case Intrinsic::vector_reduce_and:
      handleVectorReduceAndIntrinsic(I);
      break;
    case Intrinsic::vector_reduce_or:
      handleVectorReduceOrIntrinsic(I);
      break;
    case Intrinsic::vector_reduce_add:
    case Intrinsic::vector_reduce_xor:
    case Intrinsic::vector_reduce_mul:
      handleVectorReduceIntrinsic(I);
      break;
    case Intrinsic::x86_sse_stmxcsr:
      handleStmxcsr(I);
      break;
    case Intrinsic::x86_sse_ldmxcsr:
      handleLdmxcsr(I);
      break;
    case Intrinsic::x86_avx512_vcvtsd2usi64:
    case Intrinsic::x86_avx512_vcvtsd2usi32:
    case Intrinsic::x86_avx512_vcvtss2usi64:
    case Intrinsic::x86_avx512_vcvtss2usi32:
    case Intrinsic::x86_avx512_cvttss2usi64:
    case Intrinsic::x86_avx512_cvttss2usi:
    case Intrinsic::x86_avx512_cvttsd2usi64:
    case Intrinsic::x86_avx512_cvttsd2usi:
    case Intrinsic::x86_avx512_cvtusi2ss:
    case Intrinsic::x86_avx512_cvtusi642sd:
    case Intrinsic::x86_avx512_cvtusi642ss:
      handleVectorConvertIntrinsic(I, 1, true);
      break;
    case Intrinsic::x86_sse2_cvtsd2si64:
    case Intrinsic::x86_sse2_cvtsd2si:
    case Intrinsic::x86_sse2_cvtsd2ss:
    case Intrinsic::x86_sse2_cvttsd2si64:
    case Intrinsic::x86_sse2_cvttsd2si:
    case Intrinsic::x86_sse_cvtss2si64:
    case Intrinsic::x86_sse_cvtss2si:
    case Intrinsic::x86_sse_cvttss2si64:
    case Intrinsic::x86_sse_cvttss2si:
      handleVectorConvertIntrinsic(I, 1);
      break;
    case Intrinsic::x86_sse_cvtps2pi:
    case Intrinsic::x86_sse_cvttps2pi:
      handleVectorConvertIntrinsic(I, 2);
      break;

    case Intrinsic::x86_avx512_psll_w_512:
    case Intrinsic::x86_avx512_psll_d_512:
    case Intrinsic::x86_avx512_psll_q_512:
    case Intrinsic::x86_avx512_pslli_w_512:
    case Intrinsic::x86_avx512_pslli_d_512:
    case Intrinsic::x86_avx512_pslli_q_512:
    case Intrinsic::x86_avx512_psrl_w_512:
    case Intrinsic::x86_avx512_psrl_d_512:
    case Intrinsic::x86_avx512_psrl_q_512:
    case Intrinsic::x86_avx512_psra_w_512:
    case Intrinsic::x86_avx512_psra_d_512:
    case Intrinsic::x86_avx512_psra_q_512:
    case Intrinsic::x86_avx512_psrli_w_512:
    case Intrinsic::x86_avx512_psrli_d_512:
    case Intrinsic::x86_avx512_psrli_q_512:
    case Intrinsic::x86_avx512_psrai_w_512:
    case Intrinsic::x86_avx512_psrai_d_512:
    case Intrinsic::x86_avx512_psrai_q_512:
    case Intrinsic::x86_avx512_psra_q_256:
    case Intrinsic::x86_avx512_psra_q_128:
    case Intrinsic::x86_avx512_psrai_q_256:
    case Intrinsic::x86_avx512_psrai_q_128:
    case Intrinsic::x86_avx2_psll_w:
    case Intrinsic::x86_avx2_psll_d:
    case Intrinsic::x86_avx2_psll_q:
    case Intrinsic::x86_avx2_pslli_w:
    case Intrinsic::x86_avx2_pslli_d:
    case Intrinsic::x86_avx2_pslli_q:
    case Intrinsic::x86_avx2_psrl_w:
    case Intrinsic::x86_avx2_psrl_d:
    case Intrinsic::x86_avx2_psrl_q:
    case Intrinsic::x86_avx2_psra_w:
    case Intrinsic::x86_avx2_psra_d:
    case Intrinsic::x86_avx2_psrli_w:
    case Intrinsic::x86_avx2_psrli_d:
    case Intrinsic::x86_avx2_psrli_q:
    case Intrinsic::x86_avx2_psrai_w:
    case Intrinsic::x86_avx2_psrai_d:
    case Intrinsic::x86_sse2_psll_w:
    case Intrinsic::x86_sse2_psll_d:
    case Intrinsic::x86_sse2_psll_q:
    case Intrinsic::x86_sse2_pslli_w:
    case Intrinsic::x86_sse2_pslli_d:
    case Intrinsic::x86_sse2_pslli_q:
    case Intrinsic::x86_sse2_psrl_w:
    case Intrinsic::x86_sse2_psrl_d:
    case Intrinsic::x86_sse2_psrl_q:
    case Intrinsic::x86_sse2_psra_w:
    case Intrinsic::x86_sse2_psra_d:
    case Intrinsic::x86_sse2_psrli_w:
    case Intrinsic::x86_sse2_psrli_d:
    case Intrinsic::x86_sse2_psrli_q:
    case Intrinsic::x86_sse2_psrai_w:
    case Intrinsic::x86_sse2_psrai_d:
    case Intrinsic::x86_mmx_psll_w:
    case Intrinsic::x86_mmx_psll_d:
    case Intrinsic::x86_mmx_psll_q:
    case Intrinsic::x86_mmx_pslli_w:
    case Intrinsic::x86_mmx_pslli_d:
    case Intrinsic::x86_mmx_pslli_q:
    case Intrinsic::x86_mmx_psrl_w:
    case Intrinsic::x86_mmx_psrl_d:
    case Intrinsic::x86_mmx_psrl_q:
    case Intrinsic::x86_mmx_psra_w:
    case Intrinsic::x86_mmx_psra_d:
    case Intrinsic::x86_mmx_psrli_w:
    case Intrinsic::x86_mmx_psrli_d:
    case Intrinsic::x86_mmx_psrli_q:
    case Intrinsic::x86_mmx_psrai_w:
    case Intrinsic::x86_mmx_psrai_d:
      handleVectorShiftIntrinsic(I, /* Variable */ false);
      break;
    case Intrinsic::x86_avx2_psllv_d:
    case Intrinsic::x86_avx2_psllv_d_256:
    case Intrinsic::x86_avx512_psllv_d_512:
    case Intrinsic::x86_avx2_psllv_q:
    case Intrinsic::x86_avx2_psllv_q_256:
    case Intrinsic::x86_avx512_psllv_q_512:
    case Intrinsic::x86_avx2_psrlv_d:
    case Intrinsic::x86_avx2_psrlv_d_256:
    case Intrinsic::x86_avx512_psrlv_d_512:
    case Intrinsic::x86_avx2_psrlv_q:
    case Intrinsic::x86_avx2_psrlv_q_256:
    case Intrinsic::x86_avx512_psrlv_q_512:
    case Intrinsic::x86_avx2_psrav_d:
    case Intrinsic::x86_avx2_psrav_d_256:
    case Intrinsic::x86_avx512_psrav_d_512:
    case Intrinsic::x86_avx512_psrav_q_128:
    case Intrinsic::x86_avx512_psrav_q_256:
    case Intrinsic::x86_avx512_psrav_q_512:
      handleVectorShiftIntrinsic(I, /* Variable */ true);
      break;

    case Intrinsic::x86_sse2_packsswb_128:
    case Intrinsic::x86_sse2_packssdw_128:
    case Intrinsic::x86_sse2_packuswb_128:
    case Intrinsic::x86_sse41_packusdw:
    case Intrinsic::x86_avx2_packsswb:
    case Intrinsic::x86_avx2_packssdw:
    case Intrinsic::x86_avx2_packuswb:
    case Intrinsic::x86_avx2_packusdw:
      handleVectorPackIntrinsic(I);
      break;

    case Intrinsic::x86_sse41_pblendvb:
    case Intrinsic::x86_sse41_blendvpd:
    case Intrinsic::x86_sse41_blendvps:
    case Intrinsic::x86_avx_blendv_pd_256:
    case Intrinsic::x86_avx_blendv_ps_256:
    case Intrinsic::x86_avx2_pblendvb:
      handleBlendvIntrinsic(I);
      break;

    case Intrinsic::x86_avx_dp_ps_256:
    case Intrinsic::x86_sse41_dppd:
    case Intrinsic::x86_sse41_dpps:
      handleDppIntrinsic(I);
      break;

    case Intrinsic::x86_mmx_packsswb:
    case Intrinsic::x86_mmx_packuswb:
      handleVectorPackIntrinsic(I, 16);
      break;

    case Intrinsic::x86_mmx_packssdw:
      handleVectorPackIntrinsic(I, 32);
      break;

    case Intrinsic::x86_mmx_psad_bw:
    case Intrinsic::x86_sse2_psad_bw:
    case Intrinsic::x86_avx2_psad_bw:
      handleVectorSadIntrinsic(I);
      break;

    case Intrinsic::x86_sse2_pmadd_wd:
    case Intrinsic::x86_avx2_pmadd_wd:
    case Intrinsic::x86_ssse3_pmadd_ub_sw_128:
    case Intrinsic::x86_avx2_pmadd_ub_sw:
      handleVectorPmaddIntrinsic(I);
      break;

    case Intrinsic::x86_ssse3_pmadd_ub_sw:
      handleVectorPmaddIntrinsic(I, 8);
      break;

    case Intrinsic::x86_mmx_pmadd_wd:
      handleVectorPmaddIntrinsic(I, 16);
      break;

    case Intrinsic::x86_sse_cmp_ss:
    case Intrinsic::x86_sse2_cmp_sd:
    case Intrinsic::x86_sse_comieq_ss:
    case Intrinsic::x86_sse_comilt_ss:
    case Intrinsic::x86_sse_comile_ss:
    case Intrinsic::x86_sse_comigt_ss:
    case Intrinsic::x86_sse_comige_ss:
    case Intrinsic::x86_sse_comineq_ss:
    case Intrinsic::x86_sse_ucomieq_ss:
    case Intrinsic::x86_sse_ucomilt_ss:
    case Intrinsic::x86_sse_ucomile_ss:
    case Intrinsic::x86_sse_ucomigt_ss:
    case Intrinsic::x86_sse_ucomige_ss:
    case Intrinsic::x86_sse_ucomineq_ss:
    case Intrinsic::x86_sse2_comieq_sd:
    case Intrinsic::x86_sse2_comilt_sd:
    case Intrinsic::x86_sse2_comile_sd:
    case Intrinsic::x86_sse2_comigt_sd:
    case Intrinsic::x86_sse2_comige_sd:
    case Intrinsic::x86_sse2_comineq_sd:
    case Intrinsic::x86_sse2_ucomieq_sd:
    case Intrinsic::x86_sse2_ucomilt_sd:
    case Intrinsic::x86_sse2_ucomile_sd:
    case Intrinsic::x86_sse2_ucomigt_sd:
    case Intrinsic::x86_sse2_ucomige_sd:
    case Intrinsic::x86_sse2_ucomineq_sd:
      handleVectorCompareScalarIntrinsic(I);
      break;

    case Intrinsic::x86_avx_cmp_pd_256:
    case Intrinsic::x86_avx_cmp_ps_256:
    case Intrinsic::x86_sse2_cmp_pd:
    case Intrinsic::x86_sse_cmp_ps:
      handleVectorComparePackedIntrinsic(I);
      break;

    case Intrinsic::x86_bmi_bextr_32:
    case Intrinsic::x86_bmi_bextr_64:
    case Intrinsic::x86_bmi_bzhi_32:
    case Intrinsic::x86_bmi_bzhi_64:
    case Intrinsic::x86_bmi_pdep_32:
    case Intrinsic::x86_bmi_pdep_64:
    case Intrinsic::x86_bmi_pext_32:
    case Intrinsic::x86_bmi_pext_64:
      handleBmiIntrinsic(I);
      break;

    case Intrinsic::x86_pclmulqdq:
    case Intrinsic::x86_pclmulqdq_256:
    case Intrinsic::x86_pclmulqdq_512:
      handlePclmulIntrinsic(I);
      break;

    case Intrinsic::x86_sse41_round_sd:
    case Intrinsic::x86_sse41_round_ss:
      handleUnarySdSsIntrinsic(I);
      break;
    case Intrinsic::x86_sse2_max_sd:
    case Intrinsic::x86_sse_max_ss:
    case Intrinsic::x86_sse2_min_sd:
    case Intrinsic::x86_sse_min_ss:
      handleBinarySdSsIntrinsic(I);
      break;

    case Intrinsic::x86_avx_vtestc_pd:
    case Intrinsic::x86_avx_vtestc_pd_256:
    case Intrinsic::x86_avx_vtestc_ps:
    case Intrinsic::x86_avx_vtestc_ps_256:
    case Intrinsic::x86_avx_vtestnzc_pd:
    case Intrinsic::x86_avx_vtestnzc_pd_256:
    case Intrinsic::x86_avx_vtestnzc_ps:
    case Intrinsic::x86_avx_vtestnzc_ps_256:
    case Intrinsic::x86_avx_vtestz_pd:
    case Intrinsic::x86_avx_vtestz_pd_256:
    case Intrinsic::x86_avx_vtestz_ps:
    case Intrinsic::x86_avx_vtestz_ps_256:
    case Intrinsic::x86_avx_ptestc_256:
    case Intrinsic::x86_avx_ptestnzc_256:
    case Intrinsic::x86_avx_ptestz_256:
    case Intrinsic::x86_sse41_ptestc:
    case Intrinsic::x86_sse41_ptestnzc:
    case Intrinsic::x86_sse41_ptestz:
      handleVtestIntrinsic(I);
      break;

    case Intrinsic::fshl:
    case Intrinsic::fshr:
      handleFunnelShift(I);
      break;

    case Intrinsic::is_constant:
      // The result of llvm.is.constant() is always defined.
      setShadow(&I, getCleanShadow(&I));
      setOrigin(&I, getCleanOrigin());
      break;

    case Intrinsic::aarch64_neon_st2:
    case Intrinsic::aarch64_neon_st3:
    case Intrinsic::aarch64_neon_st4: {
      handleNEONVectorStoreIntrinsic(I);
      break;
    }

    default:
      if (!handleUnknownIntrinsic(I))
        visitInstruction(I);
      break;
    }
  }

  void visitLibAtomicLoad(CallBase &CB) {
    // Since we use getNextNode here, we can't have CB terminate the BB.
    assert(isa<CallInst>(CB));

    IRBuilder<> IRB(&CB);
    Value *Size = CB.getArgOperand(0);
    Value *SrcPtr = CB.getArgOperand(1);
    Value *DstPtr = CB.getArgOperand(2);
    Value *Ordering = CB.getArgOperand(3);
    // Convert the call to have at least Acquire ordering to make sure
    // the shadow operations aren't reordered before it.
    Value *NewOrdering =
        IRB.CreateExtractElement(makeAddAcquireOrderingTable(IRB), Ordering);
    CB.setArgOperand(3, NewOrdering);

    NextNodeIRBuilder NextIRB(&CB);
    Value *SrcShadowPtr, *SrcOriginPtr;
    std::tie(SrcShadowPtr, SrcOriginPtr) =
        getShadowOriginPtr(SrcPtr, NextIRB, NextIRB.getInt8Ty(), Align(1),
                           /*isStore*/ false);
    Value *DstShadowPtr =
        getShadowOriginPtr(DstPtr, NextIRB, NextIRB.getInt8Ty(), Align(1),
                           /*isStore*/ true)
            .first;

    NextIRB.CreateMemCpy(DstShadowPtr, Align(1), SrcShadowPtr, Align(1), Size);
    if (MS.TrackOrigins) {
      Value *SrcOrigin = NextIRB.CreateAlignedLoad(MS.OriginTy, SrcOriginPtr,
                                                   kMinOriginAlignment);
      Value *NewOrigin = updateOrigin(SrcOrigin, NextIRB);
      NextIRB.CreateCall(MS.MsanSetOriginFn, {DstPtr, Size, NewOrigin});
    }
  }

  void visitLibAtomicStore(CallBase &CB) {
    IRBuilder<> IRB(&CB);
    Value *Size = CB.getArgOperand(0);
    Value *DstPtr = CB.getArgOperand(2);
    Value *Ordering = CB.getArgOperand(3);
    // Convert the call to have at least Release ordering to make sure
    // the shadow operations aren't reordered after it.
    Value *NewOrdering =
        IRB.CreateExtractElement(makeAddReleaseOrderingTable(IRB), Ordering);
    CB.setArgOperand(3, NewOrdering);

    Value *DstShadowPtr =
        getShadowOriginPtr(DstPtr, IRB, IRB.getInt8Ty(), Align(1),
                           /*isStore*/ true)
            .first;

    // Atomic store always paints clean shadow/origin. See file header.
    IRB.CreateMemSet(DstShadowPtr, getCleanShadow(IRB.getInt8Ty()), Size,
                     Align(1));
  }

  void visitCallBase(CallBase &CB) {
    assert(!CB.getMetadata(LLVMContext::MD_nosanitize));
    if (CB.isInlineAsm()) {
      // For inline asm (either a call to asm function, or callbr instruction),
      // do the usual thing: check argument shadow and mark all outputs as
      // clean. Note that any side effects of the inline asm that are not
      // immediately visible in its constraints are not handled.
      if (ClHandleAsmConservative)
        visitAsmInstruction(CB);
      else
        visitInstruction(CB);
      return;
    }
    LibFunc LF;
    if (TLI->getLibFunc(CB, LF)) {
      // libatomic.a functions need to have special handling because there isn't
      // a good way to intercept them or compile the library with
      // instrumentation.
      switch (LF) {
      case LibFunc_atomic_load:
        if (!isa<CallInst>(CB)) {
          llvm::errs() << "MSAN -- cannot instrument invoke of libatomic load."
                          "Ignoring!\n";
          break;
        }
        visitLibAtomicLoad(CB);
        return;
      case LibFunc_atomic_store:
        visitLibAtomicStore(CB);
        return;
      default:
        break;
      }
    }

    if (auto *Call = dyn_cast<CallInst>(&CB)) {
      assert(!isa<IntrinsicInst>(Call) && "intrinsics are handled elsewhere");

      // We are going to insert code that relies on the fact that the callee
      // will become a non-readonly function after it is instrumented by us. To
      // prevent this code from being optimized out, mark that function
      // non-readonly in advance.
      // TODO: We can likely do better than dropping memory() completely here.
      AttributeMask B;
      B.addAttribute(Attribute::Memory).addAttribute(Attribute::Speculatable);

      Call->removeFnAttrs(B);
      if (Function *Func = Call->getCalledFunction()) {
        Func->removeFnAttrs(B);
      }

      maybeMarkSanitizerLibraryCallNoBuiltin(Call, TLI);
    }
    IRBuilder<> IRB(&CB);
    bool MayCheckCall = MS.EagerChecks;
    if (Function *Func = CB.getCalledFunction()) {
      // __sanitizer_unaligned_{load,store} functions may be called by users
      // and always expects shadows in the TLS. So don't check them.
      MayCheckCall &= !Func->getName().starts_with("__sanitizer_unaligned_");
    }

    unsigned ArgOffset = 0;
    LLVM_DEBUG(dbgs() << "  CallSite: " << CB << "\n");
    for (const auto &[i, A] : llvm::enumerate(CB.args())) {
      if (!A->getType()->isSized()) {
        LLVM_DEBUG(dbgs() << "Arg " << i << " is not sized: " << CB << "\n");
        continue;
      }

      if (A->getType()->isScalableTy()) {
        LLVM_DEBUG(dbgs() << "Arg  " << i << " is vscale: " << CB << "\n");
        // Handle as noundef, but don't reserve tls slots.
        insertShadowCheck(A, &CB);
        continue;
      }

      unsigned Size = 0;
      const DataLayout &DL = F.getDataLayout();

      bool ByVal = CB.paramHasAttr(i, Attribute::ByVal);
      bool NoUndef = CB.paramHasAttr(i, Attribute::NoUndef);
      bool EagerCheck = MayCheckCall && !ByVal && NoUndef;

      if (EagerCheck) {
        insertShadowCheck(A, &CB);
        Size = DL.getTypeAllocSize(A->getType());
      } else {
        Value *Store = nullptr;
        // Compute the Shadow for arg even if it is ByVal, because
        // in that case getShadow() will copy the actual arg shadow to
        // __msan_param_tls.
        Value *ArgShadow = getShadow(A);
        Value *ArgShadowBase = getShadowPtrForArgument(IRB, ArgOffset);
        LLVM_DEBUG(dbgs() << "  Arg#" << i << ": " << *A
                          << " Shadow: " << *ArgShadow << "\n");
        if (ByVal) {
          // ByVal requires some special handling as it's too big for a single
          // load
          assert(A->getType()->isPointerTy() &&
                 "ByVal argument is not a pointer!");
          Size = DL.getTypeAllocSize(CB.getParamByValType(i));
          if (ArgOffset + Size > kParamTLSSize)
            break;
          const MaybeAlign ParamAlignment(CB.getParamAlign(i));
          MaybeAlign Alignment = std::nullopt;
          if (ParamAlignment)
            Alignment = std::min(*ParamAlignment, kShadowTLSAlignment);
          Value *AShadowPtr, *AOriginPtr;
          std::tie(AShadowPtr, AOriginPtr) =
              getShadowOriginPtr(A, IRB, IRB.getInt8Ty(), Alignment,
                                 /*isStore*/ false);
          if (!PropagateShadow) {
            Store = IRB.CreateMemSet(ArgShadowBase,
                                     Constant::getNullValue(IRB.getInt8Ty()),
                                     Size, Alignment);
          } else {
            Store = IRB.CreateMemCpy(ArgShadowBase, Alignment, AShadowPtr,
                                     Alignment, Size);
            if (MS.TrackOrigins) {
              Value *ArgOriginBase = getOriginPtrForArgument(IRB, ArgOffset);
              // FIXME: OriginSize should be:
              // alignTo(A % kMinOriginAlignment + Size, kMinOriginAlignment)
              unsigned OriginSize = alignTo(Size, kMinOriginAlignment);
              IRB.CreateMemCpy(
                  ArgOriginBase,
                  /* by origin_tls[ArgOffset] */ kMinOriginAlignment,
                  AOriginPtr,
                  /* by getShadowOriginPtr */ kMinOriginAlignment, OriginSize);
            }
          }
        } else {
          // Any other parameters mean we need bit-grained tracking of uninit
          // data
          Size = DL.getTypeAllocSize(A->getType());
          if (ArgOffset + Size > kParamTLSSize)
            break;
          Store = IRB.CreateAlignedStore(ArgShadow, ArgShadowBase,
                                         kShadowTLSAlignment);
          Constant *Cst = dyn_cast<Constant>(ArgShadow);
          if (MS.TrackOrigins && !(Cst && Cst->isNullValue())) {
            IRB.CreateStore(getOrigin(A),
                            getOriginPtrForArgument(IRB, ArgOffset));
          }
        }
        (void)Store;
        assert(Store != nullptr);
        LLVM_DEBUG(dbgs() << "  Param:" << *Store << "\n");
      }
      assert(Size != 0);
      ArgOffset += alignTo(Size, kShadowTLSAlignment);
    }
    LLVM_DEBUG(dbgs() << "  done with call args\n");

    FunctionType *FT = CB.getFunctionType();
    if (FT->isVarArg()) {
      VAHelper->visitCallBase(CB, IRB);
    }

    // Now, get the shadow for the RetVal.
    if (!CB.getType()->isSized())
      return;
    // Don't emit the epilogue for musttail call returns.
    if (isa<CallInst>(CB) && cast<CallInst>(CB).isMustTailCall())
      return;

    if (MayCheckCall && CB.hasRetAttr(Attribute::NoUndef)) {
      setShadow(&CB, getCleanShadow(&CB));
      setOrigin(&CB, getCleanOrigin());
      return;
    }

    IRBuilder<> IRBBefore(&CB);
    // Until we have full dynamic coverage, make sure the retval shadow is 0.
    Value *Base = getShadowPtrForRetval(IRBBefore);
    IRBBefore.CreateAlignedStore(getCleanShadow(&CB), Base,
                                 kShadowTLSAlignment);
    BasicBlock::iterator NextInsn;
    if (isa<CallInst>(CB)) {
      NextInsn = ++CB.getIterator();
      assert(NextInsn != CB.getParent()->end());
    } else {
      BasicBlock *NormalDest = cast<InvokeInst>(CB).getNormalDest();
      if (!NormalDest->getSinglePredecessor()) {
        // FIXME: this case is tricky, so we are just conservative here.
        // Perhaps we need to split the edge between this BB and NormalDest,
        // but a naive attempt to use SplitEdge leads to a crash.
        setShadow(&CB, getCleanShadow(&CB));
        setOrigin(&CB, getCleanOrigin());
        return;
      }
      // FIXME: NextInsn is likely in a basic block that has not been visited
      // yet. Anything inserted there will be instrumented by MSan later!
      NextInsn = NormalDest->getFirstInsertionPt();
      assert(NextInsn != NormalDest->end() &&
             "Could not find insertion point for retval shadow load");
    }
    IRBuilder<> IRBAfter(&*NextInsn);
    Value *RetvalShadow = IRBAfter.CreateAlignedLoad(
        getShadowTy(&CB), getShadowPtrForRetval(IRBAfter),
        kShadowTLSAlignment, "_msret");
    setShadow(&CB, RetvalShadow);
    if (MS.TrackOrigins)
      setOrigin(&CB, IRBAfter.CreateLoad(MS.OriginTy,
                                         getOriginPtrForRetval()));
  }

  bool isAMustTailRetVal(Value *RetVal) {
    if (auto *I = dyn_cast<BitCastInst>(RetVal)) {
      RetVal = I->getOperand(0);
    }
    if (auto *I = dyn_cast<CallInst>(RetVal)) {
      return I->isMustTailCall();
    }
    return false;
  }

  void visitReturnInst(ReturnInst &I) {
    IRBuilder<> IRB(&I);
    Value *RetVal = I.getReturnValue();
    if (!RetVal)
      return;
    // Don't emit the epilogue for musttail call returns.
    if (isAMustTailRetVal(RetVal))
      return;
    Value *ShadowPtr = getShadowPtrForRetval(IRB);
    bool HasNoUndef = F.hasRetAttribute(Attribute::NoUndef);
    bool StoreShadow = !(MS.EagerChecks && HasNoUndef);
    // FIXME: Consider using SpecialCaseList to specify a list of functions that
    // must always return fully initialized values. For now, we hardcode "main".
    bool EagerCheck = (MS.EagerChecks && HasNoUndef) || (F.getName() == "main");

    Value *Shadow = getShadow(RetVal);
    bool StoreOrigin = true;
    if (EagerCheck) {
      insertShadowCheck(RetVal, &I);
      Shadow = getCleanShadow(RetVal);
      StoreOrigin = false;
    }

    // The caller may still expect information passed over TLS if we pass our
    // check
    if (StoreShadow) {
      IRB.CreateAlignedStore(Shadow, ShadowPtr, kShadowTLSAlignment);
      if (MS.TrackOrigins && StoreOrigin)
        IRB.CreateStore(getOrigin(RetVal), getOriginPtrForRetval());
    }
  }

  void visitPHINode(PHINode &I) {
    IRBuilder<> IRB(&I);
    if (!PropagateShadow) {
      setShadow(&I, getCleanShadow(&I));
      setOrigin(&I, getCleanOrigin());
      return;
    }

    ShadowPHINodes.push_back(&I);
    setShadow(&I, IRB.CreatePHI(getShadowTy(&I), I.getNumIncomingValues(),
                                "_msphi_s"));
    if (MS.TrackOrigins)
      setOrigin(
          &I, IRB.CreatePHI(MS.OriginTy, I.getNumIncomingValues(), "_msphi_o"));
  }

  Value *getLocalVarIdptr(AllocaInst &I) {
    ConstantInt *IntConst =
        ConstantInt::get(Type::getInt32Ty((*F.getParent()).getContext()), 0);
    return new GlobalVariable(*F.getParent(), IntConst->getType(),
                              /*isConstant=*/false, GlobalValue::PrivateLinkage,
                              IntConst);
  }

  Value *getLocalVarDescription(AllocaInst &I) {
    return createPrivateConstGlobalForString(*F.getParent(), I.getName());
  }

  void poisonAllocaUserspace(AllocaInst &I, IRBuilder<> &IRB, Value *Len) {
    if (PoisonStack && ClPoisonStackWithCall) {
      IRB.CreateCall(MS.MsanPoisonStackFn, {&I, Len});
    } else {
      Value *ShadowBase, *OriginBase;
      std::tie(ShadowBase, OriginBase) = getShadowOriginPtr(
          &I, IRB, IRB.getInt8Ty(), Align(1), /*isStore*/ true);

      Value *PoisonValue = IRB.getInt8(PoisonStack ? ClPoisonStackPattern : 0);
      IRB.CreateMemSet(ShadowBase, PoisonValue, Len, I.getAlign());
    }

    if (PoisonStack && MS.TrackOrigins) {
      Value *Idptr = getLocalVarIdptr(I);
      if (ClPrintStackNames) {
        Value *Descr = getLocalVarDescription(I);
        IRB.CreateCall(MS.MsanSetAllocaOriginWithDescriptionFn,
                       {&I, Len, Idptr, Descr});
      } else {
        IRB.CreateCall(MS.MsanSetAllocaOriginNoDescriptionFn, {&I, Len, Idptr});
      }
    }
  }

  void poisonAllocaKmsan(AllocaInst &I, IRBuilder<> &IRB, Value *Len) {
    Value *Descr = getLocalVarDescription(I);
    if (PoisonStack) {
      IRB.CreateCall(MS.MsanPoisonAllocaFn, {&I, Len, Descr});
    } else {
      IRB.CreateCall(MS.MsanUnpoisonAllocaFn, {&I, Len});
    }
  }

  void instrumentAlloca(AllocaInst &I, Instruction *InsPoint = nullptr) {
    if (!InsPoint)
      InsPoint = &I;
    NextNodeIRBuilder IRB(InsPoint);
    const DataLayout &DL = F.getDataLayout();
    TypeSize TS = DL.getTypeAllocSize(I.getAllocatedType());
    Value *Len = IRB.CreateTypeSize(MS.IntptrTy, TS);
    if (I.isArrayAllocation())
      Len = IRB.CreateMul(Len,
                          IRB.CreateZExtOrTrunc(I.getArraySize(), MS.IntptrTy));

    if (MS.CompileKernel)
      poisonAllocaKmsan(I, IRB, Len);
    else
      poisonAllocaUserspace(I, IRB, Len);
  }

  void visitAllocaInst(AllocaInst &I) {
    setShadow(&I, getCleanShadow(&I));
    setOrigin(&I, getCleanOrigin());
    // We'll get to this alloca later unless it's poisoned at the corresponding
    // llvm.lifetime.start.
    AllocaSet.insert(&I);
  }

  void visitSelectInst(SelectInst &I) {
    // a = select b, c, d
    Value *B = I.getCondition();
    Value *C = I.getTrueValue();
    Value *D = I.getFalseValue();

    handleSelectLikeInst(I, B, C, D);
  }

  void handleSelectLikeInst(Instruction &I, Value *B, Value *C, Value *D) {
    IRBuilder<> IRB(&I);

    Value *Sb = getShadow(B);
    Value *Sc = getShadow(C);
    Value *Sd = getShadow(D);

    Value *Ob = MS.TrackOrigins ? getOrigin(B) : nullptr;
    Value *Oc = MS.TrackOrigins ? getOrigin(C) : nullptr;
    Value *Od = MS.TrackOrigins ? getOrigin(D) : nullptr;

    // Result shadow if condition shadow is 0.
    Value *Sa0 = IRB.CreateSelect(B, Sc, Sd);
    Value *Sa1;
    if (I.getType()->isAggregateType()) {
      // To avoid "sign extending" i1 to an arbitrary aggregate type, we just do
      // an extra "select". This results in much more compact IR.
      // Sa = select Sb, poisoned, (select b, Sc, Sd)
      Sa1 = getPoisonedShadow(getShadowTy(I.getType()));
    } else {
      // Sa = select Sb, [ (c^d) | Sc | Sd ], [ b ? Sc : Sd ]
      // If Sb (condition is poisoned), look for bits in c and d that are equal
      // and both unpoisoned.
      // If !Sb (condition is unpoisoned), simply pick one of Sc and Sd.

      // Cast arguments to shadow-compatible type.
      C = CreateAppToShadowCast(IRB, C);
      D = CreateAppToShadowCast(IRB, D);

      // Result shadow if condition shadow is 1.
      Sa1 = IRB.CreateOr({IRB.CreateXor(C, D), Sc, Sd});
    }
    Value *Sa = IRB.CreateSelect(Sb, Sa1, Sa0, "_msprop_select");
    setShadow(&I, Sa);
    if (MS.TrackOrigins) {
      // Origins are always i32, so any vector conditions must be flattened.
      // FIXME: consider tracking vector origins for app vectors?
      if (B->getType()->isVectorTy()) {
        B = convertToBool(B, IRB);
        Sb = convertToBool(Sb, IRB);
      }
      // a = select b, c, d
      // Oa = Sb ? Ob : (b ? Oc : Od)
      setOrigin(&I, IRB.CreateSelect(Sb, Ob, IRB.CreateSelect(B, Oc, Od)));
    }
  }

  void visitLandingPadInst(LandingPadInst &I) {
    // Do nothing.
    // See https://github.com/google/sanitizers/issues/504
    setShadow(&I, getCleanShadow(&I));
    setOrigin(&I, getCleanOrigin());
  }

  void visitCatchSwitchInst(CatchSwitchInst &I) {
    setShadow(&I, getCleanShadow(&I));
    setOrigin(&I, getCleanOrigin());
  }

  void visitFuncletPadInst(FuncletPadInst &I) {
    setShadow(&I, getCleanShadow(&I));
    setOrigin(&I, getCleanOrigin());
  }

  void visitGetElementPtrInst(GetElementPtrInst &I) { handleShadowOr(I); }

  void visitExtractValueInst(ExtractValueInst &I) {
    IRBuilder<> IRB(&I);
    Value *Agg = I.getAggregateOperand();
    LLVM_DEBUG(dbgs() << "ExtractValue:  " << I << "\n");
    Value *AggShadow = getShadow(Agg);
    LLVM_DEBUG(dbgs() << "   AggShadow:  " << *AggShadow << "\n");
    Value *ResShadow = IRB.CreateExtractValue(AggShadow, I.getIndices());
    LLVM_DEBUG(dbgs() << "   ResShadow:  " << *ResShadow << "\n");
    setShadow(&I, ResShadow);
    setOriginForNaryOp(I);
  }

  void visitInsertValueInst(InsertValueInst &I) {
    IRBuilder<> IRB(&I);
    LLVM_DEBUG(dbgs() << "InsertValue:  " << I << "\n");
    Value *AggShadow = getShadow(I.getAggregateOperand());
    Value *InsShadow = getShadow(I.getInsertedValueOperand());
    LLVM_DEBUG(dbgs() << "   AggShadow:  " << *AggShadow << "\n");
    LLVM_DEBUG(dbgs() << "   InsShadow:  " << *InsShadow << "\n");
    Value *Res = IRB.CreateInsertValue(AggShadow, InsShadow, I.getIndices());
    LLVM_DEBUG(dbgs() << "   Res:        " << *Res << "\n");
    setShadow(&I, Res);
    setOriginForNaryOp(I);
  }

  void dumpInst(Instruction &I) {
    if (CallInst *CI = dyn_cast<CallInst>(&I)) {
      errs() << "ZZZ call " << CI->getCalledFunction()->getName() << "\n";
    } else {
      errs() << "ZZZ " << I.getOpcodeName() << "\n";
    }
    errs() << "QQQ " << I << "\n";
  }

  void visitResumeInst(ResumeInst &I) {
    LLVM_DEBUG(dbgs() << "Resume: " << I << "\n");
    // Nothing to do here.
  }

  void visitCleanupReturnInst(CleanupReturnInst &CRI) {
    LLVM_DEBUG(dbgs() << "CleanupReturn: " << CRI << "\n");
    // Nothing to do here.
  }

  void visitCatchReturnInst(CatchReturnInst &CRI) {
    LLVM_DEBUG(dbgs() << "CatchReturn: " << CRI << "\n");
    // Nothing to do here.
  }

  void instrumentAsmArgument(Value *Operand, Type *ElemTy, Instruction &I,
                             IRBuilder<> &IRB, const DataLayout &DL,
                             bool isOutput) {
    // For each assembly argument, we check its value for being initialized.
    // If the argument is a pointer, we assume it points to a single element
    // of the corresponding type (or to a 8-byte word, if the type is unsized).
    // Each such pointer is instrumented with a call to the runtime library.
    Type *OpType = Operand->getType();
    // Check the operand value itself.
    insertShadowCheck(Operand, &I);
    if (!OpType->isPointerTy() || !isOutput) {
      assert(!isOutput);
      return;
    }
    if (!ElemTy->isSized())
      return;
    auto Size = DL.getTypeStoreSize(ElemTy);
    Value *SizeVal = IRB.CreateTypeSize(MS.IntptrTy, Size);
    if (MS.CompileKernel) {
      IRB.CreateCall(MS.MsanInstrumentAsmStoreFn, {Operand, SizeVal});
    } else {
      // ElemTy, derived from elementtype(), does not encode the alignment of
      // the pointer. Conservatively assume that the shadow memory is unaligned.
      // When Size is large, avoid StoreInst as it would expand to many
      // instructions.
      auto [ShadowPtr, _] =
          getShadowOriginPtrUserspace(Operand, IRB, IRB.getInt8Ty(), Align(1));
      if (Size <= 32)
        IRB.CreateAlignedStore(getCleanShadow(ElemTy), ShadowPtr, Align(1));
      else
        IRB.CreateMemSet(ShadowPtr, ConstantInt::getNullValue(IRB.getInt8Ty()),
                         SizeVal, Align(1));
    }
  }

  /// Get the number of output arguments returned by pointers.
  int getNumOutputArgs(InlineAsm *IA, CallBase *CB) {
    int NumRetOutputs = 0;
    int NumOutputs = 0;
    Type *RetTy = cast<Value>(CB)->getType();
    if (!RetTy->isVoidTy()) {
      // Register outputs are returned via the CallInst return value.
      auto *ST = dyn_cast<StructType>(RetTy);
      if (ST)
        NumRetOutputs = ST->getNumElements();
      else
        NumRetOutputs = 1;
    }
    InlineAsm::ConstraintInfoVector Constraints = IA->ParseConstraints();
    for (const InlineAsm::ConstraintInfo &Info : Constraints) {
      switch (Info.Type) {
      case InlineAsm::isOutput:
        NumOutputs++;
        break;
      default:
        break;
      }
    }
    return NumOutputs - NumRetOutputs;
  }

  void visitAsmInstruction(Instruction &I) {
    // Conservative inline assembly handling: check for poisoned shadow of
    // asm() arguments, then unpoison the result and all the memory locations
    // pointed to by those arguments.
    // An inline asm() statement in C++ contains lists of input and output
    // arguments used by the assembly code. These are mapped to operands of the
    // CallInst as follows:
    //  - nR register outputs ("=r) are returned by value in a single structure
    //  (SSA value of the CallInst);
    //  - nO other outputs ("=m" and others) are returned by pointer as first
    // nO operands of the CallInst;
    //  - nI inputs ("r", "m" and others) are passed to CallInst as the
    // remaining nI operands.
    // The total number of asm() arguments in the source is nR+nO+nI, and the
    // corresponding CallInst has nO+nI+1 operands (the last operand is the
    // function to be called).
    const DataLayout &DL = F.getDataLayout();
    CallBase *CB = cast<CallBase>(&I);
    IRBuilder<> IRB(&I);
    InlineAsm *IA = cast<InlineAsm>(CB->getCalledOperand());
    int OutputArgs = getNumOutputArgs(IA, CB);
    // The last operand of a CallInst is the function itself.
    int NumOperands = CB->getNumOperands() - 1;

    // Check input arguments. Doing so before unpoisoning output arguments, so
    // that we won't overwrite uninit values before checking them.
    for (int i = OutputArgs; i < NumOperands; i++) {
      Value *Operand = CB->getOperand(i);
      instrumentAsmArgument(Operand, CB->getParamElementType(i), I, IRB, DL,
                            /*isOutput*/ false);
    }
    // Unpoison output arguments. This must happen before the actual InlineAsm
    // call, so that the shadow for memory published in the asm() statement
    // remains valid.
    for (int i = 0; i < OutputArgs; i++) {
      Value *Operand = CB->getOperand(i);
      instrumentAsmArgument(Operand, CB->getParamElementType(i), I, IRB, DL,
                            /*isOutput*/ true);
    }

    setShadow(&I, getCleanShadow(&I));
    setOrigin(&I, getCleanOrigin());
  }

  void visitFreezeInst(FreezeInst &I) {
    // Freeze always returns a fully defined value.
    setShadow(&I, getCleanShadow(&I));
    setOrigin(&I, getCleanOrigin());
  }

  void visitInstruction(Instruction &I) {
    // Everything else: stop propagating and check for poisoned shadow.
    if (ClDumpStrictInstructions)
      dumpInst(I);
    LLVM_DEBUG(dbgs() << "DEFAULT: " << I << "\n");
    for (size_t i = 0, n = I.getNumOperands(); i < n; i++) {
      Value *Operand = I.getOperand(i);
      if (Operand->getType()->isSized())
        insertShadowCheck(Operand, &I);
    }
    setShadow(&I, getCleanShadow(&I));
    setOrigin(&I, getCleanOrigin());
  }
};

struct VarArgHelperBase : public VarArgHelper {
  Function &F;
  MemorySanitizer &MS;
  MemorySanitizerVisitor &MSV;
  SmallVector<CallInst *, 16> VAStartInstrumentationList;
  const unsigned VAListTagSize;

  VarArgHelperBase(Function &F, MemorySanitizer &MS,
                   MemorySanitizerVisitor &MSV, unsigned VAListTagSize)
      : F(F), MS(MS), MSV(MSV), VAListTagSize(VAListTagSize) {}

  Value *getShadowAddrForVAArgument(IRBuilder<> &IRB, unsigned ArgOffset) {
    Value *Base = IRB.CreatePointerCast(MS.VAArgTLS, MS.IntptrTy);
    return IRB.CreateAdd(Base, ConstantInt::get(MS.IntptrTy, ArgOffset));
  }

  /// Compute the shadow address for a given va_arg.
  Value *getShadowPtrForVAArgument(Type *Ty, IRBuilder<> &IRB,
                                   unsigned ArgOffset) {
    Value *Base = IRB.CreatePointerCast(MS.VAArgTLS, MS.IntptrTy);
    Base = IRB.CreateAdd(Base, ConstantInt::get(MS.IntptrTy, ArgOffset));
    return IRB.CreateIntToPtr(Base, PointerType::get(MSV.getShadowTy(Ty), 0),
                              "_msarg_va_s");
  }

  /// Compute the shadow address for a given va_arg.
  Value *getShadowPtrForVAArgument(Type *Ty, IRBuilder<> &IRB,
                                   unsigned ArgOffset, unsigned ArgSize) {
    // Make sure we don't overflow __msan_va_arg_tls.
    if (ArgOffset + ArgSize > kParamTLSSize)
      return nullptr;
    return getShadowPtrForVAArgument(Ty, IRB, ArgOffset);
  }

  /// Compute the origin address for a given va_arg.
  Value *getOriginPtrForVAArgument(IRBuilder<> &IRB, int ArgOffset) {
    Value *Base = IRB.CreatePointerCast(MS.VAArgOriginTLS, MS.IntptrTy);
    // getOriginPtrForVAArgument() is always called after
    // getShadowPtrForVAArgument(), so __msan_va_arg_origin_tls can never
    // overflow.
    Base = IRB.CreateAdd(Base, ConstantInt::get(MS.IntptrTy, ArgOffset));
    return IRB.CreateIntToPtr(Base, PointerType::get(MS.OriginTy, 0),
                              "_msarg_va_o");
  }

  void CleanUnusedTLS(IRBuilder<> &IRB, Value *ShadowBase,
                      unsigned BaseOffset) {
    // The tails of __msan_va_arg_tls is not large enough to fit full
    // value shadow, but it will be copied to backup anyway. Make it
    // clean.
    if (BaseOffset >= kParamTLSSize)
      return;
    Value *TailSize =
        ConstantInt::getSigned(IRB.getInt32Ty(), kParamTLSSize - BaseOffset);
    IRB.CreateMemSet(ShadowBase, ConstantInt::getNullValue(IRB.getInt8Ty()),
                     TailSize, Align(8));
  }

  void unpoisonVAListTagForInst(IntrinsicInst &I) {
    IRBuilder<> IRB(&I);
    Value *VAListTag = I.getArgOperand(0);
    const Align Alignment = Align(8);
    auto [ShadowPtr, OriginPtr] = MSV.getShadowOriginPtr(
        VAListTag, IRB, IRB.getInt8Ty(), Alignment, /*isStore*/ true);
    // Unpoison the whole __va_list_tag.
    IRB.CreateMemSet(ShadowPtr, Constant::getNullValue(IRB.getInt8Ty()),
                     VAListTagSize, Alignment, false);
  }

  void visitVAStartInst(VAStartInst &I) override {
    if (F.getCallingConv() == CallingConv::Win64)
      return;
    VAStartInstrumentationList.push_back(&I);
    unpoisonVAListTagForInst(I);
  }

  void visitVACopyInst(VACopyInst &I) override {
    if (F.getCallingConv() == CallingConv::Win64)
      return;
    unpoisonVAListTagForInst(I);
  }
};

/// AMD64-specific implementation of VarArgHelper.
struct VarArgAMD64Helper : public VarArgHelperBase {
  // An unfortunate workaround for asymmetric lowering of va_arg stuff.
  // See a comment in visitCallBase for more details.
  static const unsigned AMD64GpEndOffset = 48; // AMD64 ABI Draft 0.99.6 p3.5.7
  static const unsigned AMD64FpEndOffsetSSE = 176;
  // If SSE is disabled, fp_offset in va_list is zero.
  static const unsigned AMD64FpEndOffsetNoSSE = AMD64GpEndOffset;

  unsigned AMD64FpEndOffset;
  AllocaInst *VAArgTLSCopy = nullptr;
  AllocaInst *VAArgTLSOriginCopy = nullptr;
  Value *VAArgOverflowSize = nullptr;

  enum ArgKind { AK_GeneralPurpose, AK_FloatingPoint, AK_Memory };

  VarArgAMD64Helper(Function &F, MemorySanitizer &MS,
                    MemorySanitizerVisitor &MSV)
      : VarArgHelperBase(F, MS, MSV, /*VAListTagSize=*/24) {
    AMD64FpEndOffset = AMD64FpEndOffsetSSE;
    for (const auto &Attr : F.getAttributes().getFnAttrs()) {
      if (Attr.isStringAttribute() &&
          (Attr.getKindAsString() == "target-features")) {
        if (Attr.getValueAsString().contains("-sse"))
          AMD64FpEndOffset = AMD64FpEndOffsetNoSSE;
        break;
      }
    }
  }

  ArgKind classifyArgument(Value *arg) {
    // A very rough approximation of X86_64 argument classification rules.
    Type *T = arg->getType();
    if (T->isX86_FP80Ty())
      return AK_Memory;
    if (T->isFPOrFPVectorTy() || T->isX86_MMXTy())
      return AK_FloatingPoint;
    if (T->isIntegerTy() && T->getPrimitiveSizeInBits() <= 64)
      return AK_GeneralPurpose;
    if (T->isPointerTy())
      return AK_GeneralPurpose;
    return AK_Memory;
  }

  // For VarArg functions, store the argument shadow in an ABI-specific format
  // that corresponds to va_list layout.
  // We do this because Clang lowers va_arg in the frontend, and this pass
  // only sees the low level code that deals with va_list internals.
  // A much easier alternative (provided that Clang emits va_arg instructions)
  // would have been to associate each live instance of va_list with a copy of
  // MSanParamTLS, and extract shadow on va_arg() call in the argument list
  // order.
  void visitCallBase(CallBase &CB, IRBuilder<> &IRB) override {
    unsigned GpOffset = 0;
    unsigned FpOffset = AMD64GpEndOffset;
    unsigned OverflowOffset = AMD64FpEndOffset;
    const DataLayout &DL = F.getDataLayout();

    for (const auto &[ArgNo, A] : llvm::enumerate(CB.args())) {
      bool IsFixed = ArgNo < CB.getFunctionType()->getNumParams();
      bool IsByVal = CB.paramHasAttr(ArgNo, Attribute::ByVal);
      if (IsByVal) {
        // ByVal arguments always go to the overflow area.
        // Fixed arguments passed through the overflow area will be stepped
        // over by va_start, so don't count them towards the offset.
        if (IsFixed)
          continue;
        assert(A->getType()->isPointerTy());
        Type *RealTy = CB.getParamByValType(ArgNo);
        uint64_t ArgSize = DL.getTypeAllocSize(RealTy);
        uint64_t AlignedSize = alignTo(ArgSize, 8);
        unsigned BaseOffset = OverflowOffset;
        Value *ShadowBase =
            getShadowPtrForVAArgument(RealTy, IRB, OverflowOffset);
        Value *OriginBase = nullptr;
        if (MS.TrackOrigins)
          OriginBase = getOriginPtrForVAArgument(IRB, OverflowOffset);
        OverflowOffset += AlignedSize;

        if (OverflowOffset > kParamTLSSize) {
          CleanUnusedTLS(IRB, ShadowBase, BaseOffset);
          continue; // We have no space to copy shadow there.
        }

        Value *ShadowPtr, *OriginPtr;
        std::tie(ShadowPtr, OriginPtr) =
            MSV.getShadowOriginPtr(A, IRB, IRB.getInt8Ty(), kShadowTLSAlignment,
                                   /*isStore*/ false);
        IRB.CreateMemCpy(ShadowBase, kShadowTLSAlignment, ShadowPtr,
                         kShadowTLSAlignment, ArgSize);
        if (MS.TrackOrigins)
          IRB.CreateMemCpy(OriginBase, kShadowTLSAlignment, OriginPtr,
                           kShadowTLSAlignment, ArgSize);
      } else {
        ArgKind AK = classifyArgument(A);
        if (AK == AK_GeneralPurpose && GpOffset >= AMD64GpEndOffset)
          AK = AK_Memory;
        if (AK == AK_FloatingPoint && FpOffset >= AMD64FpEndOffset)
          AK = AK_Memory;
        Value *ShadowBase, *OriginBase = nullptr;
        switch (AK) {
        case AK_GeneralPurpose:
          ShadowBase = getShadowPtrForVAArgument(A->getType(), IRB, GpOffset);
          if (MS.TrackOrigins)
            OriginBase = getOriginPtrForVAArgument(IRB, GpOffset);
          GpOffset += 8;
          assert(GpOffset <= kParamTLSSize);
          break;
        case AK_FloatingPoint:
          ShadowBase = getShadowPtrForVAArgument(A->getType(), IRB, FpOffset);
          if (MS.TrackOrigins)
            OriginBase = getOriginPtrForVAArgument(IRB, FpOffset);
          FpOffset += 16;
          assert(FpOffset <= kParamTLSSize);
          break;
        case AK_Memory:
          if (IsFixed)
            continue;
          uint64_t ArgSize = DL.getTypeAllocSize(A->getType());
          uint64_t AlignedSize = alignTo(ArgSize, 8);
          unsigned BaseOffset = OverflowOffset;
          ShadowBase =
              getShadowPtrForVAArgument(A->getType(), IRB, OverflowOffset);
          if (MS.TrackOrigins) {
            OriginBase = getOriginPtrForVAArgument(IRB, OverflowOffset);
          }
          OverflowOffset += AlignedSize;
          if (OverflowOffset > kParamTLSSize) {
            // We have no space to copy shadow there.
            CleanUnusedTLS(IRB, ShadowBase, BaseOffset);
            continue;
          }
        }
        // Take fixed arguments into account for GpOffset and FpOffset,
        // but don't actually store shadows for them.
        // TODO(glider): don't call get*PtrForVAArgument() for them.
        if (IsFixed)
          continue;
        Value *Shadow = MSV.getShadow(A);
        IRB.CreateAlignedStore(Shadow, ShadowBase, kShadowTLSAlignment);
        if (MS.TrackOrigins) {
          Value *Origin = MSV.getOrigin(A);
          TypeSize StoreSize = DL.getTypeStoreSize(Shadow->getType());
          MSV.paintOrigin(IRB, Origin, OriginBase, StoreSize,
                          std::max(kShadowTLSAlignment, kMinOriginAlignment));
        }
      }
    }
    Constant *OverflowSize =
        ConstantInt::get(IRB.getInt64Ty(), OverflowOffset - AMD64FpEndOffset);
    IRB.CreateStore(OverflowSize, MS.VAArgOverflowSizeTLS);
  }

  void finalizeInstrumentation() override {
    assert(!VAArgOverflowSize && !VAArgTLSCopy &&
           "finalizeInstrumentation called twice");
    if (!VAStartInstrumentationList.empty()) {
      // If there is a va_start in this function, make a backup copy of
      // va_arg_tls somewhere in the function entry block.
      IRBuilder<> IRB(MSV.FnPrologueEnd);
      VAArgOverflowSize =
          IRB.CreateLoad(IRB.getInt64Ty(), MS.VAArgOverflowSizeTLS);
      Value *CopySize = IRB.CreateAdd(
          ConstantInt::get(MS.IntptrTy, AMD64FpEndOffset), VAArgOverflowSize);
      VAArgTLSCopy = IRB.CreateAlloca(Type::getInt8Ty(*MS.C), CopySize);
      VAArgTLSCopy->setAlignment(kShadowTLSAlignment);
      IRB.CreateMemSet(VAArgTLSCopy, Constant::getNullValue(IRB.getInt8Ty()),
                       CopySize, kShadowTLSAlignment, false);

      Value *SrcSize = IRB.CreateBinaryIntrinsic(
          Intrinsic::umin, CopySize,
          ConstantInt::get(MS.IntptrTy, kParamTLSSize));
      IRB.CreateMemCpy(VAArgTLSCopy, kShadowTLSAlignment, MS.VAArgTLS,
                       kShadowTLSAlignment, SrcSize);
      if (MS.TrackOrigins) {
        VAArgTLSOriginCopy = IRB.CreateAlloca(Type::getInt8Ty(*MS.C), CopySize);
        VAArgTLSOriginCopy->setAlignment(kShadowTLSAlignment);
        IRB.CreateMemCpy(VAArgTLSOriginCopy, kShadowTLSAlignment,
                         MS.VAArgOriginTLS, kShadowTLSAlignment, SrcSize);
      }
    }

    // Instrument va_start.
    // Copy va_list shadow from the backup copy of the TLS contents.
    for (CallInst *OrigInst : VAStartInstrumentationList) {
      NextNodeIRBuilder IRB(OrigInst);
      Value *VAListTag = OrigInst->getArgOperand(0);

      Type *RegSaveAreaPtrTy = PointerType::getUnqual(*MS.C); // i64*
      Value *RegSaveAreaPtrPtr = IRB.CreateIntToPtr(
          IRB.CreateAdd(IRB.CreatePtrToInt(VAListTag, MS.IntptrTy),
                        ConstantInt::get(MS.IntptrTy, 16)),
          PointerType::get(RegSaveAreaPtrTy, 0));
      Value *RegSaveAreaPtr =
          IRB.CreateLoad(RegSaveAreaPtrTy, RegSaveAreaPtrPtr);
      Value *RegSaveAreaShadowPtr, *RegSaveAreaOriginPtr;
      const Align Alignment = Align(16);
      std::tie(RegSaveAreaShadowPtr, RegSaveAreaOriginPtr) =
          MSV.getShadowOriginPtr(RegSaveAreaPtr, IRB, IRB.getInt8Ty(),
                                 Alignment, /*isStore*/ true);
      IRB.CreateMemCpy(RegSaveAreaShadowPtr, Alignment, VAArgTLSCopy, Alignment,
                       AMD64FpEndOffset);
      if (MS.TrackOrigins)
        IRB.CreateMemCpy(RegSaveAreaOriginPtr, Alignment, VAArgTLSOriginCopy,
                         Alignment, AMD64FpEndOffset);
      Type *OverflowArgAreaPtrTy = PointerType::getUnqual(*MS.C); // i64*
      Value *OverflowArgAreaPtrPtr = IRB.CreateIntToPtr(
          IRB.CreateAdd(IRB.CreatePtrToInt(VAListTag, MS.IntptrTy),
                        ConstantInt::get(MS.IntptrTy, 8)),
          PointerType::get(OverflowArgAreaPtrTy, 0));
      Value *OverflowArgAreaPtr =
          IRB.CreateLoad(OverflowArgAreaPtrTy, OverflowArgAreaPtrPtr);
      Value *OverflowArgAreaShadowPtr, *OverflowArgAreaOriginPtr;
      std::tie(OverflowArgAreaShadowPtr, OverflowArgAreaOriginPtr) =
          MSV.getShadowOriginPtr(OverflowArgAreaPtr, IRB, IRB.getInt8Ty(),
                                 Alignment, /*isStore*/ true);
      Value *SrcPtr = IRB.CreateConstGEP1_32(IRB.getInt8Ty(), VAArgTLSCopy,
                                             AMD64FpEndOffset);
      IRB.CreateMemCpy(OverflowArgAreaShadowPtr, Alignment, SrcPtr, Alignment,
                       VAArgOverflowSize);
      if (MS.TrackOrigins) {
        SrcPtr = IRB.CreateConstGEP1_32(IRB.getInt8Ty(), VAArgTLSOriginCopy,
                                        AMD64FpEndOffset);
        IRB.CreateMemCpy(OverflowArgAreaOriginPtr, Alignment, SrcPtr, Alignment,
                         VAArgOverflowSize);
      }
    }
  }
};

/// MIPS64-specific implementation of VarArgHelper.
/// NOTE: This is also used for LoongArch64.
struct VarArgMIPS64Helper : public VarArgHelperBase {
  AllocaInst *VAArgTLSCopy = nullptr;
  Value *VAArgSize = nullptr;

  VarArgMIPS64Helper(Function &F, MemorySanitizer &MS,
                     MemorySanitizerVisitor &MSV)
      : VarArgHelperBase(F, MS, MSV, /*VAListTagSize=*/8) {}

  void visitCallBase(CallBase &CB, IRBuilder<> &IRB) override {
    unsigned VAArgOffset = 0;
    const DataLayout &DL = F.getDataLayout();
    for (Value *A :
         llvm::drop_begin(CB.args(), CB.getFunctionType()->getNumParams())) {
      Triple TargetTriple(F.getParent()->getTargetTriple());
      Value *Base;
      uint64_t ArgSize = DL.getTypeAllocSize(A->getType());
      if (TargetTriple.getArch() == Triple::mips64) {
        // Adjusting the shadow for argument with size < 8 to match the
        // placement of bits in big endian system
        if (ArgSize < 8)
          VAArgOffset += (8 - ArgSize);
      }
      Base = getShadowPtrForVAArgument(A->getType(), IRB, VAArgOffset, ArgSize);
      VAArgOffset += ArgSize;
      VAArgOffset = alignTo(VAArgOffset, 8);
      if (!Base)
        continue;
      IRB.CreateAlignedStore(MSV.getShadow(A), Base, kShadowTLSAlignment);
    }

    Constant *TotalVAArgSize = ConstantInt::get(IRB.getInt64Ty(), VAArgOffset);
    // Here using VAArgOverflowSizeTLS as VAArgSizeTLS to avoid creation of
    // a new class member i.e. it is the total size of all VarArgs.
    IRB.CreateStore(TotalVAArgSize, MS.VAArgOverflowSizeTLS);
  }

  void finalizeInstrumentation() override {
    assert(!VAArgSize && !VAArgTLSCopy &&
           "finalizeInstrumentation called twice");
    IRBuilder<> IRB(MSV.FnPrologueEnd);
    VAArgSize = IRB.CreateLoad(IRB.getInt64Ty(), MS.VAArgOverflowSizeTLS);
    Value *CopySize =
        IRB.CreateAdd(ConstantInt::get(MS.IntptrTy, 0), VAArgSize);

    if (!VAStartInstrumentationList.empty()) {
      // If there is a va_start in this function, make a backup copy of
      // va_arg_tls somewhere in the function entry block.
      VAArgTLSCopy = IRB.CreateAlloca(Type::getInt8Ty(*MS.C), CopySize);
      VAArgTLSCopy->setAlignment(kShadowTLSAlignment);
      IRB.CreateMemSet(VAArgTLSCopy, Constant::getNullValue(IRB.getInt8Ty()),
                       CopySize, kShadowTLSAlignment, false);

      Value *SrcSize = IRB.CreateBinaryIntrinsic(
          Intrinsic::umin, CopySize,
          ConstantInt::get(MS.IntptrTy, kParamTLSSize));
      IRB.CreateMemCpy(VAArgTLSCopy, kShadowTLSAlignment, MS.VAArgTLS,
                       kShadowTLSAlignment, SrcSize);
    }

    // Instrument va_start.
    // Copy va_list shadow from the backup copy of the TLS contents.
    for (CallInst *OrigInst : VAStartInstrumentationList) {
      NextNodeIRBuilder IRB(OrigInst);
      Value *VAListTag = OrigInst->getArgOperand(0);
      Type *RegSaveAreaPtrTy = PointerType::getUnqual(*MS.C); // i64*
      Value *RegSaveAreaPtrPtr =
          IRB.CreateIntToPtr(IRB.CreatePtrToInt(VAListTag, MS.IntptrTy),
                             PointerType::get(RegSaveAreaPtrTy, 0));
      Value *RegSaveAreaPtr =
          IRB.CreateLoad(RegSaveAreaPtrTy, RegSaveAreaPtrPtr);
      Value *RegSaveAreaShadowPtr, *RegSaveAreaOriginPtr;
      const Align Alignment = Align(8);
      std::tie(RegSaveAreaShadowPtr, RegSaveAreaOriginPtr) =
          MSV.getShadowOriginPtr(RegSaveAreaPtr, IRB, IRB.getInt8Ty(),
                                 Alignment, /*isStore*/ true);
      IRB.CreateMemCpy(RegSaveAreaShadowPtr, Alignment, VAArgTLSCopy, Alignment,
                       CopySize);
    }
  }
};

/// AArch64-specific implementation of VarArgHelper.
struct VarArgAArch64Helper : public VarArgHelperBase {
  static const unsigned kAArch64GrArgSize = 64;
  static const unsigned kAArch64VrArgSize = 128;

  static const unsigned AArch64GrBegOffset = 0;
  static const unsigned AArch64GrEndOffset = kAArch64GrArgSize;
  // Make VR space aligned to 16 bytes.
  static const unsigned AArch64VrBegOffset = AArch64GrEndOffset;
  static const unsigned AArch64VrEndOffset =
      AArch64VrBegOffset + kAArch64VrArgSize;
  static const unsigned AArch64VAEndOffset = AArch64VrEndOffset;

  AllocaInst *VAArgTLSCopy = nullptr;
  Value *VAArgOverflowSize = nullptr;

  enum ArgKind { AK_GeneralPurpose, AK_FloatingPoint, AK_Memory };

  VarArgAArch64Helper(Function &F, MemorySanitizer &MS,
                      MemorySanitizerVisitor &MSV)
      : VarArgHelperBase(F, MS, MSV, /*VAListTagSize=*/32) {}

  // A very rough approximation of aarch64 argument classification rules.
  std::pair<ArgKind, uint64_t> classifyArgument(Type *T) {
    if (T->isIntOrPtrTy() && T->getPrimitiveSizeInBits() <= 64)
      return {AK_GeneralPurpose, 1};
    if (T->isFloatingPointTy() && T->getPrimitiveSizeInBits() <= 128)
      return {AK_FloatingPoint, 1};

    if (T->isArrayTy()) {
      auto R = classifyArgument(T->getArrayElementType());
      R.second *= T->getScalarType()->getArrayNumElements();
      return R;
    }

    if (const FixedVectorType *FV = dyn_cast<FixedVectorType>(T)) {
      auto R = classifyArgument(FV->getScalarType());
      R.second *= FV->getNumElements();
      return R;
    }

    LLVM_DEBUG(errs() << "Unknown vararg type: " << *T << "\n");
    return {AK_Memory, 0};
  }

  // The instrumentation stores the argument shadow in a non ABI-specific
  // format because it does not know which argument is named (since Clang,
  // like x86_64 case, lowers the va_args in the frontend and this pass only
  // sees the low level code that deals with va_list internals).
  // The first seven GR registers are saved in the first 56 bytes of the
  // va_arg tls arra, followed by the first 8 FP/SIMD registers, and then
  // the remaining arguments.
  // Using constant offset within the va_arg TLS array allows fast copy
  // in the finalize instrumentation.
  void visitCallBase(CallBase &CB, IRBuilder<> &IRB) override {
    unsigned GrOffset = AArch64GrBegOffset;
    unsigned VrOffset = AArch64VrBegOffset;
    unsigned OverflowOffset = AArch64VAEndOffset;

    const DataLayout &DL = F.getDataLayout();
    for (const auto &[ArgNo, A] : llvm::enumerate(CB.args())) {
      bool IsFixed = ArgNo < CB.getFunctionType()->getNumParams();
      auto [AK, RegNum] = classifyArgument(A->getType());
      if (AK == AK_GeneralPurpose &&
          (GrOffset + RegNum * 8) > AArch64GrEndOffset)
        AK = AK_Memory;
      if (AK == AK_FloatingPoint &&
          (VrOffset + RegNum * 16) > AArch64VrEndOffset)
        AK = AK_Memory;
      Value *Base;
      switch (AK) {
      case AK_GeneralPurpose:
        Base = getShadowPtrForVAArgument(A->getType(), IRB, GrOffset);
        GrOffset += 8 * RegNum;
        break;
      case AK_FloatingPoint:
        Base = getShadowPtrForVAArgument(A->getType(), IRB, VrOffset);
        VrOffset += 16 * RegNum;
        break;
      case AK_Memory:
        // Don't count fixed arguments in the overflow area - va_start will
        // skip right over them.
        if (IsFixed)
          continue;
        uint64_t ArgSize = DL.getTypeAllocSize(A->getType());
        uint64_t AlignedSize = alignTo(ArgSize, 8);
        unsigned BaseOffset = OverflowOffset;
        Base = getShadowPtrForVAArgument(A->getType(), IRB, BaseOffset);
        OverflowOffset += AlignedSize;
        if (OverflowOffset > kParamTLSSize) {
          // We have no space to copy shadow there.
          CleanUnusedTLS(IRB, Base, BaseOffset);
          continue;
        }
        break;
      }
      // Count Gp/Vr fixed arguments to their respective offsets, but don't
      // bother to actually store a shadow.
      if (IsFixed)
        continue;
      IRB.CreateAlignedStore(MSV.getShadow(A), Base, kShadowTLSAlignment);
    }
    Constant *OverflowSize =
        ConstantInt::get(IRB.getInt64Ty(), OverflowOffset - AArch64VAEndOffset);
    IRB.CreateStore(OverflowSize, MS.VAArgOverflowSizeTLS);
  }

  // Retrieve a va_list field of 'void*' size.
  Value *getVAField64(IRBuilder<> &IRB, Value *VAListTag, int offset) {
    Value *SaveAreaPtrPtr = IRB.CreateIntToPtr(
        IRB.CreateAdd(IRB.CreatePtrToInt(VAListTag, MS.IntptrTy),
                      ConstantInt::get(MS.IntptrTy, offset)),
        PointerType::get(*MS.C, 0));
    return IRB.CreateLoad(Type::getInt64Ty(*MS.C), SaveAreaPtrPtr);
  }

  // Retrieve a va_list field of 'int' size.
  Value *getVAField32(IRBuilder<> &IRB, Value *VAListTag, int offset) {
    Value *SaveAreaPtr = IRB.CreateIntToPtr(
        IRB.CreateAdd(IRB.CreatePtrToInt(VAListTag, MS.IntptrTy),
                      ConstantInt::get(MS.IntptrTy, offset)),
        PointerType::get(*MS.C, 0));
    Value *SaveArea32 = IRB.CreateLoad(IRB.getInt32Ty(), SaveAreaPtr);
    return IRB.CreateSExt(SaveArea32, MS.IntptrTy);
  }

  void finalizeInstrumentation() override {
    assert(!VAArgOverflowSize && !VAArgTLSCopy &&
           "finalizeInstrumentation called twice");
    if (!VAStartInstrumentationList.empty()) {
      // If there is a va_start in this function, make a backup copy of
      // va_arg_tls somewhere in the function entry block.
      IRBuilder<> IRB(MSV.FnPrologueEnd);
      VAArgOverflowSize =
          IRB.CreateLoad(IRB.getInt64Ty(), MS.VAArgOverflowSizeTLS);
      Value *CopySize = IRB.CreateAdd(
          ConstantInt::get(MS.IntptrTy, AArch64VAEndOffset), VAArgOverflowSize);
      VAArgTLSCopy = IRB.CreateAlloca(Type::getInt8Ty(*MS.C), CopySize);
      VAArgTLSCopy->setAlignment(kShadowTLSAlignment);
      IRB.CreateMemSet(VAArgTLSCopy, Constant::getNullValue(IRB.getInt8Ty()),
                       CopySize, kShadowTLSAlignment, false);

      Value *SrcSize = IRB.CreateBinaryIntrinsic(
          Intrinsic::umin, CopySize,
          ConstantInt::get(MS.IntptrTy, kParamTLSSize));
      IRB.CreateMemCpy(VAArgTLSCopy, kShadowTLSAlignment, MS.VAArgTLS,
                       kShadowTLSAlignment, SrcSize);
    }

    Value *GrArgSize = ConstantInt::get(MS.IntptrTy, kAArch64GrArgSize);
    Value *VrArgSize = ConstantInt::get(MS.IntptrTy, kAArch64VrArgSize);

    // Instrument va_start, copy va_list shadow from the backup copy of
    // the TLS contents.
    for (CallInst *OrigInst : VAStartInstrumentationList) {
      NextNodeIRBuilder IRB(OrigInst);

      Value *VAListTag = OrigInst->getArgOperand(0);

      // The variadic ABI for AArch64 creates two areas to save the incoming
      // argument registers (one for 64-bit general register xn-x7 and another
      // for 128-bit FP/SIMD vn-v7).
      // We need then to propagate the shadow arguments on both regions
      // 'va::__gr_top + va::__gr_offs' and 'va::__vr_top + va::__vr_offs'.
      // The remaining arguments are saved on shadow for 'va::stack'.
      // One caveat is it requires only to propagate the non-named arguments,
      // however on the call site instrumentation 'all' the arguments are
      // saved. So to copy the shadow values from the va_arg TLS array
      // we need to adjust the offset for both GR and VR fields based on
      // the __{gr,vr}_offs value (since they are stores based on incoming
      // named arguments).
      Type *RegSaveAreaPtrTy = IRB.getPtrTy();

      // Read the stack pointer from the va_list.
      Value *StackSaveAreaPtr =
          IRB.CreateIntToPtr(getVAField64(IRB, VAListTag, 0), RegSaveAreaPtrTy);

      // Read both the __gr_top and __gr_off and add them up.
      Value *GrTopSaveAreaPtr = getVAField64(IRB, VAListTag, 8);
      Value *GrOffSaveArea = getVAField32(IRB, VAListTag, 24);

      Value *GrRegSaveAreaPtr = IRB.CreateIntToPtr(
          IRB.CreateAdd(GrTopSaveAreaPtr, GrOffSaveArea), RegSaveAreaPtrTy);

      // Read both the __vr_top and __vr_off and add them up.
      Value *VrTopSaveAreaPtr = getVAField64(IRB, VAListTag, 16);
      Value *VrOffSaveArea = getVAField32(IRB, VAListTag, 28);

      Value *VrRegSaveAreaPtr = IRB.CreateIntToPtr(
          IRB.CreateAdd(VrTopSaveAreaPtr, VrOffSaveArea), RegSaveAreaPtrTy);

      // It does not know how many named arguments is being used and, on the
      // callsite all the arguments were saved.  Since __gr_off is defined as
      // '0 - ((8 - named_gr) * 8)', the idea is to just propagate the variadic
      // argument by ignoring the bytes of shadow from named arguments.
      Value *GrRegSaveAreaShadowPtrOff =
          IRB.CreateAdd(GrArgSize, GrOffSaveArea);

      Value *GrRegSaveAreaShadowPtr =
          MSV.getShadowOriginPtr(GrRegSaveAreaPtr, IRB, IRB.getInt8Ty(),
                                 Align(8), /*isStore*/ true)
              .first;

      Value *GrSrcPtr =
          IRB.CreateInBoundsPtrAdd(VAArgTLSCopy, GrRegSaveAreaShadowPtrOff);
      Value *GrCopySize = IRB.CreateSub(GrArgSize, GrRegSaveAreaShadowPtrOff);

      IRB.CreateMemCpy(GrRegSaveAreaShadowPtr, Align(8), GrSrcPtr, Align(8),
                       GrCopySize);

      // Again, but for FP/SIMD values.
      Value *VrRegSaveAreaShadowPtrOff =
          IRB.CreateAdd(VrArgSize, VrOffSaveArea);

      Value *VrRegSaveAreaShadowPtr =
          MSV.getShadowOriginPtr(VrRegSaveAreaPtr, IRB, IRB.getInt8Ty(),
                                 Align(8), /*isStore*/ true)
              .first;

      Value *VrSrcPtr = IRB.CreateInBoundsPtrAdd(
          IRB.CreateInBoundsPtrAdd(VAArgTLSCopy,
                                   IRB.getInt32(AArch64VrBegOffset)),
          VrRegSaveAreaShadowPtrOff);
      Value *VrCopySize = IRB.CreateSub(VrArgSize, VrRegSaveAreaShadowPtrOff);

      IRB.CreateMemCpy(VrRegSaveAreaShadowPtr, Align(8), VrSrcPtr, Align(8),
                       VrCopySize);

      // And finally for remaining arguments.
      Value *StackSaveAreaShadowPtr =
          MSV.getShadowOriginPtr(StackSaveAreaPtr, IRB, IRB.getInt8Ty(),
                                 Align(16), /*isStore*/ true)
              .first;

      Value *StackSrcPtr = IRB.CreateInBoundsPtrAdd(
          VAArgTLSCopy, IRB.getInt32(AArch64VAEndOffset));

      IRB.CreateMemCpy(StackSaveAreaShadowPtr, Align(16), StackSrcPtr,
                       Align(16), VAArgOverflowSize);
    }
  }
};

/// PowerPC64-specific implementation of VarArgHelper.
struct VarArgPowerPC64Helper : public VarArgHelperBase {
  AllocaInst *VAArgTLSCopy = nullptr;
  Value *VAArgSize = nullptr;

  VarArgPowerPC64Helper(Function &F, MemorySanitizer &MS,
                        MemorySanitizerVisitor &MSV)
      : VarArgHelperBase(F, MS, MSV, /*VAListTagSize=*/8) {}

  void visitCallBase(CallBase &CB, IRBuilder<> &IRB) override {
    // For PowerPC, we need to deal with alignment of stack arguments -
    // they are mostly aligned to 8 bytes, but vectors and i128 arrays
    // are aligned to 16 bytes, byvals can be aligned to 8 or 16 bytes,
    // For that reason, we compute current offset from stack pointer (which is
    // always properly aligned), and offset for the first vararg, then subtract
    // them.
    unsigned VAArgBase;
    Triple TargetTriple(F.getParent()->getTargetTriple());
    // Parameter save area starts at 48 bytes from frame pointer for ABIv1,
    // and 32 bytes for ABIv2.  This is usually determined by target
    // endianness, but in theory could be overridden by function attribute.
    if (TargetTriple.getArch() == Triple::ppc64)
      VAArgBase = 48;
    else
      VAArgBase = 32;
    unsigned VAArgOffset = VAArgBase;
    const DataLayout &DL = F.getDataLayout();
    for (const auto &[ArgNo, A] : llvm::enumerate(CB.args())) {
      bool IsFixed = ArgNo < CB.getFunctionType()->getNumParams();
      bool IsByVal = CB.paramHasAttr(ArgNo, Attribute::ByVal);
      if (IsByVal) {
        assert(A->getType()->isPointerTy());
        Type *RealTy = CB.getParamByValType(ArgNo);
        uint64_t ArgSize = DL.getTypeAllocSize(RealTy);
        Align ArgAlign = CB.getParamAlign(ArgNo).value_or(Align(8));
        if (ArgAlign < 8)
          ArgAlign = Align(8);
        VAArgOffset = alignTo(VAArgOffset, ArgAlign);
        if (!IsFixed) {
          Value *Base = getShadowPtrForVAArgument(
              RealTy, IRB, VAArgOffset - VAArgBase, ArgSize);
          if (Base) {
            Value *AShadowPtr, *AOriginPtr;
            std::tie(AShadowPtr, AOriginPtr) =
                MSV.getShadowOriginPtr(A, IRB, IRB.getInt8Ty(),
                                       kShadowTLSAlignment, /*isStore*/ false);

            IRB.CreateMemCpy(Base, kShadowTLSAlignment, AShadowPtr,
                             kShadowTLSAlignment, ArgSize);
          }
        }
        VAArgOffset += alignTo(ArgSize, Align(8));
      } else {
        Value *Base;
        uint64_t ArgSize = DL.getTypeAllocSize(A->getType());
        Align ArgAlign = Align(8);
        if (A->getType()->isArrayTy()) {
          // Arrays are aligned to element size, except for long double
          // arrays, which are aligned to 8 bytes.
          Type *ElementTy = A->getType()->getArrayElementType();
          if (!ElementTy->isPPC_FP128Ty())
            ArgAlign = Align(DL.getTypeAllocSize(ElementTy));
        } else if (A->getType()->isVectorTy()) {
          // Vectors are naturally aligned.
          ArgAlign = Align(ArgSize);
        }
        if (ArgAlign < 8)
          ArgAlign = Align(8);
        VAArgOffset = alignTo(VAArgOffset, ArgAlign);
        if (DL.isBigEndian()) {
          // Adjusting the shadow for argument with size < 8 to match the
          // placement of bits in big endian system
          if (ArgSize < 8)
            VAArgOffset += (8 - ArgSize);
        }
        if (!IsFixed) {
          Base = getShadowPtrForVAArgument(A->getType(), IRB,
                                           VAArgOffset - VAArgBase, ArgSize);
          if (Base)
            IRB.CreateAlignedStore(MSV.getShadow(A), Base, kShadowTLSAlignment);
        }
        VAArgOffset += ArgSize;
        VAArgOffset = alignTo(VAArgOffset, Align(8));
      }
      if (IsFixed)
        VAArgBase = VAArgOffset;
    }

    Constant *TotalVAArgSize =
        ConstantInt::get(IRB.getInt64Ty(), VAArgOffset - VAArgBase);
    // Here using VAArgOverflowSizeTLS as VAArgSizeTLS to avoid creation of
    // a new class member i.e. it is the total size of all VarArgs.
    IRB.CreateStore(TotalVAArgSize, MS.VAArgOverflowSizeTLS);
  }

  void finalizeInstrumentation() override {
    assert(!VAArgSize && !VAArgTLSCopy &&
           "finalizeInstrumentation called twice");
    IRBuilder<> IRB(MSV.FnPrologueEnd);
    VAArgSize = IRB.CreateLoad(IRB.getInt64Ty(), MS.VAArgOverflowSizeTLS);
    Value *CopySize =
        IRB.CreateAdd(ConstantInt::get(MS.IntptrTy, 0), VAArgSize);

    if (!VAStartInstrumentationList.empty()) {
      // If there is a va_start in this function, make a backup copy of
      // va_arg_tls somewhere in the function entry block.

      VAArgTLSCopy = IRB.CreateAlloca(Type::getInt8Ty(*MS.C), CopySize);
      VAArgTLSCopy->setAlignment(kShadowTLSAlignment);
      IRB.CreateMemSet(VAArgTLSCopy, Constant::getNullValue(IRB.getInt8Ty()),
                       CopySize, kShadowTLSAlignment, false);

      Value *SrcSize = IRB.CreateBinaryIntrinsic(
          Intrinsic::umin, CopySize,
          ConstantInt::get(MS.IntptrTy, kParamTLSSize));
      IRB.CreateMemCpy(VAArgTLSCopy, kShadowTLSAlignment, MS.VAArgTLS,
                       kShadowTLSAlignment, SrcSize);
    }

    // Instrument va_start.
    // Copy va_list shadow from the backup copy of the TLS contents.
    for (CallInst *OrigInst : VAStartInstrumentationList) {
      NextNodeIRBuilder IRB(OrigInst);
      Value *VAListTag = OrigInst->getArgOperand(0);
      Type *RegSaveAreaPtrTy = PointerType::getUnqual(*MS.C); // i64*
      Value *RegSaveAreaPtrPtr =
          IRB.CreateIntToPtr(IRB.CreatePtrToInt(VAListTag, MS.IntptrTy),
                             PointerType::get(RegSaveAreaPtrTy, 0));
      Value *RegSaveAreaPtr =
          IRB.CreateLoad(RegSaveAreaPtrTy, RegSaveAreaPtrPtr);
      Value *RegSaveAreaShadowPtr, *RegSaveAreaOriginPtr;
      const Align Alignment = Align(8);
      std::tie(RegSaveAreaShadowPtr, RegSaveAreaOriginPtr) =
          MSV.getShadowOriginPtr(RegSaveAreaPtr, IRB, IRB.getInt8Ty(),
                                 Alignment, /*isStore*/ true);
      IRB.CreateMemCpy(RegSaveAreaShadowPtr, Alignment, VAArgTLSCopy, Alignment,
                       CopySize);
    }
  }
};

/// SystemZ-specific implementation of VarArgHelper.
struct VarArgSystemZHelper : public VarArgHelperBase {
  static const unsigned SystemZGpOffset = 16;
  static const unsigned SystemZGpEndOffset = 56;
  static const unsigned SystemZFpOffset = 128;
  static const unsigned SystemZFpEndOffset = 160;
  static const unsigned SystemZMaxVrArgs = 8;
  static const unsigned SystemZRegSaveAreaSize = 160;
  static const unsigned SystemZOverflowOffset = 160;
  static const unsigned SystemZVAListTagSize = 32;
  static const unsigned SystemZOverflowArgAreaPtrOffset = 16;
  static const unsigned SystemZRegSaveAreaPtrOffset = 24;

  bool IsSoftFloatABI;
  AllocaInst *VAArgTLSCopy = nullptr;
  AllocaInst *VAArgTLSOriginCopy = nullptr;
  Value *VAArgOverflowSize = nullptr;

  enum class ArgKind {
    GeneralPurpose,
    FloatingPoint,
    Vector,
    Memory,
    Indirect,
  };

  enum class ShadowExtension { None, Zero, Sign };

  VarArgSystemZHelper(Function &F, MemorySanitizer &MS,
                      MemorySanitizerVisitor &MSV)
      : VarArgHelperBase(F, MS, MSV, SystemZVAListTagSize),
        IsSoftFloatABI(F.getFnAttribute("use-soft-float").getValueAsBool()) {}

  ArgKind classifyArgument(Type *T) {
    // T is a SystemZABIInfo::classifyArgumentType() output, and there are
    // only a few possibilities of what it can be. In particular, enums, single
    // element structs and large types have already been taken care of.

    // Some i128 and fp128 arguments are converted to pointers only in the
    // back end.
    if (T->isIntegerTy(128) || T->isFP128Ty())
      return ArgKind::Indirect;
    if (T->isFloatingPointTy())
      return IsSoftFloatABI ? ArgKind::GeneralPurpose : ArgKind::FloatingPoint;
    if (T->isIntegerTy() || T->isPointerTy())
      return ArgKind::GeneralPurpose;
    if (T->isVectorTy())
      return ArgKind::Vector;
    return ArgKind::Memory;
  }

  ShadowExtension getShadowExtension(const CallBase &CB, unsigned ArgNo) {
    // ABI says: "One of the simple integer types no more than 64 bits wide.
    // ... If such an argument is shorter than 64 bits, replace it by a full
    // 64-bit integer representing the same number, using sign or zero
    // extension". Shadow for an integer argument has the same type as the
    // argument itself, so it can be sign or zero extended as well.
    bool ZExt = CB.paramHasAttr(ArgNo, Attribute::ZExt);
    bool SExt = CB.paramHasAttr(ArgNo, Attribute::SExt);
    if (ZExt) {
      assert(!SExt);
      return ShadowExtension::Zero;
    }
    if (SExt) {
      assert(!ZExt);
      return ShadowExtension::Sign;
    }
    return ShadowExtension::None;
  }

  void visitCallBase(CallBase &CB, IRBuilder<> &IRB) override {
    unsigned GpOffset = SystemZGpOffset;
    unsigned FpOffset = SystemZFpOffset;
    unsigned VrIndex = 0;
    unsigned OverflowOffset = SystemZOverflowOffset;
    const DataLayout &DL = F.getDataLayout();
    for (const auto &[ArgNo, A] : llvm::enumerate(CB.args())) {
      bool IsFixed = ArgNo < CB.getFunctionType()->getNumParams();
      // SystemZABIInfo does not produce ByVal parameters.
      assert(!CB.paramHasAttr(ArgNo, Attribute::ByVal));
      Type *T = A->getType();
      ArgKind AK = classifyArgument(T);
      if (AK == ArgKind::Indirect) {
        T = PointerType::get(T, 0);
        AK = ArgKind::GeneralPurpose;
      }
      if (AK == ArgKind::GeneralPurpose && GpOffset >= SystemZGpEndOffset)
        AK = ArgKind::Memory;
      if (AK == ArgKind::FloatingPoint && FpOffset >= SystemZFpEndOffset)
        AK = ArgKind::Memory;
      if (AK == ArgKind::Vector && (VrIndex >= SystemZMaxVrArgs || !IsFixed))
        AK = ArgKind::Memory;
      Value *ShadowBase = nullptr;
      Value *OriginBase = nullptr;
      ShadowExtension SE = ShadowExtension::None;
      switch (AK) {
      case ArgKind::GeneralPurpose: {
        // Always keep track of GpOffset, but store shadow only for varargs.
        uint64_t ArgSize = 8;
        if (GpOffset + ArgSize <= kParamTLSSize) {
          if (!IsFixed) {
            SE = getShadowExtension(CB, ArgNo);
            uint64_t GapSize = 0;
            if (SE == ShadowExtension::None) {
              uint64_t ArgAllocSize = DL.getTypeAllocSize(T);
              assert(ArgAllocSize <= ArgSize);
              GapSize = ArgSize - ArgAllocSize;
            }
            ShadowBase = getShadowAddrForVAArgument(IRB, GpOffset + GapSize);
            if (MS.TrackOrigins)
              OriginBase = getOriginPtrForVAArgument(IRB, GpOffset + GapSize);
          }
          GpOffset += ArgSize;
        } else {
          GpOffset = kParamTLSSize;
        }
        break;
      }
      case ArgKind::FloatingPoint: {
        // Always keep track of FpOffset, but store shadow only for varargs.
        uint64_t ArgSize = 8;
        if (FpOffset + ArgSize <= kParamTLSSize) {
          if (!IsFixed) {
            // PoP says: "A short floating-point datum requires only the
            // left-most 32 bit positions of a floating-point register".
            // Therefore, in contrast to AK_GeneralPurpose and AK_Memory,
            // don't extend shadow and don't mind the gap.
            ShadowBase = getShadowAddrForVAArgument(IRB, FpOffset);
            if (MS.TrackOrigins)
              OriginBase = getOriginPtrForVAArgument(IRB, FpOffset);
          }
          FpOffset += ArgSize;
        } else {
          FpOffset = kParamTLSSize;
        }
        break;
      }
      case ArgKind::Vector: {
        // Keep track of VrIndex. No need to store shadow, since vector varargs
        // go through AK_Memory.
        assert(IsFixed);
        VrIndex++;
        break;
      }
      case ArgKind::Memory: {
        // Keep track of OverflowOffset and store shadow only for varargs.
        // Ignore fixed args, since we need to copy only the vararg portion of
        // the overflow area shadow.
        if (!IsFixed) {
          uint64_t ArgAllocSize = DL.getTypeAllocSize(T);
          uint64_t ArgSize = alignTo(ArgAllocSize, 8);
          if (OverflowOffset + ArgSize <= kParamTLSSize) {
            SE = getShadowExtension(CB, ArgNo);
            uint64_t GapSize =
                SE == ShadowExtension::None ? ArgSize - ArgAllocSize : 0;
            ShadowBase =
                getShadowAddrForVAArgument(IRB, OverflowOffset + GapSize);
            if (MS.TrackOrigins)
              OriginBase =
                  getOriginPtrForVAArgument(IRB, OverflowOffset + GapSize);
            OverflowOffset += ArgSize;
          } else {
            OverflowOffset = kParamTLSSize;
          }
        }
        break;
      }
      case ArgKind::Indirect:
        llvm_unreachable("Indirect must be converted to GeneralPurpose");
      }
      if (ShadowBase == nullptr)
        continue;
      Value *Shadow = MSV.getShadow(A);
      if (SE != ShadowExtension::None)
        Shadow = MSV.CreateShadowCast(IRB, Shadow, IRB.getInt64Ty(),
                                      /*Signed*/ SE == ShadowExtension::Sign);
      ShadowBase = IRB.CreateIntToPtr(
          ShadowBase, PointerType::get(Shadow->getType(), 0), "_msarg_va_s");
      IRB.CreateStore(Shadow, ShadowBase);
      if (MS.TrackOrigins) {
        Value *Origin = MSV.getOrigin(A);
        TypeSize StoreSize = DL.getTypeStoreSize(Shadow->getType());
        MSV.paintOrigin(IRB, Origin, OriginBase, StoreSize,
                        kMinOriginAlignment);
      }
    }
    Constant *OverflowSize = ConstantInt::get(
        IRB.getInt64Ty(), OverflowOffset - SystemZOverflowOffset);
    IRB.CreateStore(OverflowSize, MS.VAArgOverflowSizeTLS);
  }

  void copyRegSaveArea(IRBuilder<> &IRB, Value *VAListTag) {
    Type *RegSaveAreaPtrTy = PointerType::getUnqual(*MS.C); // i64*
    Value *RegSaveAreaPtrPtr = IRB.CreateIntToPtr(
        IRB.CreateAdd(
            IRB.CreatePtrToInt(VAListTag, MS.IntptrTy),
            ConstantInt::get(MS.IntptrTy, SystemZRegSaveAreaPtrOffset)),
        PointerType::get(RegSaveAreaPtrTy, 0));
    Value *RegSaveAreaPtr = IRB.CreateLoad(RegSaveAreaPtrTy, RegSaveAreaPtrPtr);
    Value *RegSaveAreaShadowPtr, *RegSaveAreaOriginPtr;
    const Align Alignment = Align(8);
    std::tie(RegSaveAreaShadowPtr, RegSaveAreaOriginPtr) =
        MSV.getShadowOriginPtr(RegSaveAreaPtr, IRB, IRB.getInt8Ty(), Alignment,
                               /*isStore*/ true);
    // TODO(iii): copy only fragments filled by visitCallBase()
    // TODO(iii): support packed-stack && !use-soft-float
    // For use-soft-float functions, it is enough to copy just the GPRs.
    unsigned RegSaveAreaSize =
        IsSoftFloatABI ? SystemZGpEndOffset : SystemZRegSaveAreaSize;
    IRB.CreateMemCpy(RegSaveAreaShadowPtr, Alignment, VAArgTLSCopy, Alignment,
                     RegSaveAreaSize);
    if (MS.TrackOrigins)
      IRB.CreateMemCpy(RegSaveAreaOriginPtr, Alignment, VAArgTLSOriginCopy,
                       Alignment, RegSaveAreaSize);
  }

  // FIXME: This implementation limits OverflowOffset to kParamTLSSize, so we
  // don't know real overflow size and can't clear shadow beyond kParamTLSSize.
  void copyOverflowArea(IRBuilder<> &IRB, Value *VAListTag) {
    Type *OverflowArgAreaPtrTy = PointerType::getUnqual(*MS.C); // i64*
    Value *OverflowArgAreaPtrPtr = IRB.CreateIntToPtr(
        IRB.CreateAdd(
            IRB.CreatePtrToInt(VAListTag, MS.IntptrTy),
            ConstantInt::get(MS.IntptrTy, SystemZOverflowArgAreaPtrOffset)),
        PointerType::get(OverflowArgAreaPtrTy, 0));
    Value *OverflowArgAreaPtr =
        IRB.CreateLoad(OverflowArgAreaPtrTy, OverflowArgAreaPtrPtr);
    Value *OverflowArgAreaShadowPtr, *OverflowArgAreaOriginPtr;
    const Align Alignment = Align(8);
    std::tie(OverflowArgAreaShadowPtr, OverflowArgAreaOriginPtr) =
        MSV.getShadowOriginPtr(OverflowArgAreaPtr, IRB, IRB.getInt8Ty(),
                               Alignment, /*isStore*/ true);
    Value *SrcPtr = IRB.CreateConstGEP1_32(IRB.getInt8Ty(), VAArgTLSCopy,
                                           SystemZOverflowOffset);
    IRB.CreateMemCpy(OverflowArgAreaShadowPtr, Alignment, SrcPtr, Alignment,
                     VAArgOverflowSize);
    if (MS.TrackOrigins) {
      SrcPtr = IRB.CreateConstGEP1_32(IRB.getInt8Ty(), VAArgTLSOriginCopy,
                                      SystemZOverflowOffset);
      IRB.CreateMemCpy(OverflowArgAreaOriginPtr, Alignment, SrcPtr, Alignment,
                       VAArgOverflowSize);
    }
  }

  void finalizeInstrumentation() override {
    assert(!VAArgOverflowSize && !VAArgTLSCopy &&
           "finalizeInstrumentation called twice");
    if (!VAStartInstrumentationList.empty()) {
      // If there is a va_start in this function, make a backup copy of
      // va_arg_tls somewhere in the function entry block.
      IRBuilder<> IRB(MSV.FnPrologueEnd);
      VAArgOverflowSize =
          IRB.CreateLoad(IRB.getInt64Ty(), MS.VAArgOverflowSizeTLS);
      Value *CopySize =
          IRB.CreateAdd(ConstantInt::get(MS.IntptrTy, SystemZOverflowOffset),
                        VAArgOverflowSize);
      VAArgTLSCopy = IRB.CreateAlloca(Type::getInt8Ty(*MS.C), CopySize);
      VAArgTLSCopy->setAlignment(kShadowTLSAlignment);
      IRB.CreateMemSet(VAArgTLSCopy, Constant::getNullValue(IRB.getInt8Ty()),
                       CopySize, kShadowTLSAlignment, false);

      Value *SrcSize = IRB.CreateBinaryIntrinsic(
          Intrinsic::umin, CopySize,
          ConstantInt::get(MS.IntptrTy, kParamTLSSize));
      IRB.CreateMemCpy(VAArgTLSCopy, kShadowTLSAlignment, MS.VAArgTLS,
                       kShadowTLSAlignment, SrcSize);
      if (MS.TrackOrigins) {
        VAArgTLSOriginCopy = IRB.CreateAlloca(Type::getInt8Ty(*MS.C), CopySize);
        VAArgTLSOriginCopy->setAlignment(kShadowTLSAlignment);
        IRB.CreateMemCpy(VAArgTLSOriginCopy, kShadowTLSAlignment,
                         MS.VAArgOriginTLS, kShadowTLSAlignment, SrcSize);
      }
    }

    // Instrument va_start.
    // Copy va_list shadow from the backup copy of the TLS contents.
    for (CallInst *OrigInst : VAStartInstrumentationList) {
      NextNodeIRBuilder IRB(OrigInst);
      Value *VAListTag = OrigInst->getArgOperand(0);
      copyRegSaveArea(IRB, VAListTag);
      copyOverflowArea(IRB, VAListTag);
    }
  }
};

// Loongarch64 is not a MIPS, but the current vargs calling convention matches
// the MIPS.
using VarArgLoongArch64Helper = VarArgMIPS64Helper;

/// A no-op implementation of VarArgHelper.
struct VarArgNoOpHelper : public VarArgHelper {
  VarArgNoOpHelper(Function &F, MemorySanitizer &MS,
                   MemorySanitizerVisitor &MSV) {}

  void visitCallBase(CallBase &CB, IRBuilder<> &IRB) override {}

  void visitVAStartInst(VAStartInst &I) override {}

  void visitVACopyInst(VACopyInst &I) override {}

  void finalizeInstrumentation() override {}
};

} // end anonymous namespace

static VarArgHelper *CreateVarArgHelper(Function &Func, MemorySanitizer &Msan,
                                        MemorySanitizerVisitor &Visitor) {
  // VarArg handling is only implemented on AMD64. False positives are possible
  // on other platforms.
  Triple TargetTriple(Func.getParent()->getTargetTriple());
  if (TargetTriple.getArch() == Triple::x86_64)
    return new VarArgAMD64Helper(Func, Msan, Visitor);
  else if (TargetTriple.isMIPS64())
    return new VarArgMIPS64Helper(Func, Msan, Visitor);
  else if (TargetTriple.getArch() == Triple::aarch64)
    return new VarArgAArch64Helper(Func, Msan, Visitor);
  else if (TargetTriple.getArch() == Triple::ppc64 ||
           TargetTriple.getArch() == Triple::ppc64le)
    return new VarArgPowerPC64Helper(Func, Msan, Visitor);
  else if (TargetTriple.getArch() == Triple::systemz)
    return new VarArgSystemZHelper(Func, Msan, Visitor);
  else if (TargetTriple.isLoongArch64())
    return new VarArgLoongArch64Helper(Func, Msan, Visitor);
  else
    return new VarArgNoOpHelper(Func, Msan, Visitor);
}

bool MemorySanitizer::sanitizeFunction(Function &F, TargetLibraryInfo &TLI) {
  if (!CompileKernel && F.getName() == kMsanModuleCtorName)
    return false;

  if (F.hasFnAttribute(Attribute::DisableSanitizerInstrumentation))
    return false;

  MemorySanitizerVisitor Visitor(F, *this, TLI);

  // Clear out memory attributes.
  AttributeMask B;
  B.addAttribute(Attribute::Memory).addAttribute(Attribute::Speculatable);
  F.removeFnAttrs(B);

  return Visitor.runOnFunction();
}
