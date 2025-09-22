//===-- nsan.cc -----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// NumericalStabilitySanitizer runtime.
//
// This implements:
//  - The public nsan interface (include/sanitizer/nsan_interface.h).
//  - The private nsan interface (./nsan.h).
//  - The internal instrumentation interface. These are function emitted by the
//    instrumentation pass:
//        * __nsan_get_shadow_ptr_for_{float,double,longdouble}_load
//          These return the shadow memory pointer for loading the shadow value,
//          after checking that the types are consistent. If the types are not
//          consistent, returns nullptr.
//        * __nsan_get_shadow_ptr_for_{float,double,longdouble}_store
//          Sets the shadow types appropriately and returns the shadow memory
//          pointer for storing the shadow value.
//        * __nsan_internal_check_{float,double,long double}_{f,d,l} checks the
//          accuracy of a value against its shadow and emits a warning depending
//          on the runtime configuration. The middle part indicates the type of
//          the application value, the suffix (f,d,l) indicates the type of the
//          shadow, and depends on the instrumentation configuration.
//        * __nsan_fcmp_fail_* emits a warning for an fcmp instruction whose
//          corresponding shadow fcmp result differs.
//
//===----------------------------------------------------------------------===//

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_report_decorator.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "sanitizer_common/sanitizer_symbolizer.h"

#include "nsan/nsan.h"
#include "nsan/nsan_flags.h"
#include "nsan/nsan_stats.h"
#include "nsan/nsan_suppressions.h"

using namespace __sanitizer;
using namespace __nsan;

constexpr int kMaxVectorWidth = 8;

// When copying application memory, we also copy its shadow and shadow type.
// FIXME: We could provide fixed-size versions that would nicely
// vectorize for known sizes.
extern "C" SANITIZER_INTERFACE_ATTRIBUTE void
__nsan_copy_values(const u8 *daddr, const u8 *saddr, uptr size) {
  internal_memmove((void *)GetShadowTypeAddrFor(daddr),
                   GetShadowTypeAddrFor(saddr), size);
  internal_memmove((void *)GetShadowAddrFor(daddr), GetShadowAddrFor(saddr),
                   size * kShadowScale);
}

// FIXME: We could provide fixed-size versions that would nicely
// vectorize for known sizes.
extern "C" SANITIZER_INTERFACE_ATTRIBUTE void
__nsan_set_value_unknown(const u8 *addr, uptr size) {
  internal_memset((void *)GetShadowTypeAddrFor(addr), 0, size);
}


const char *FTInfo<float>::kCppTypeName = "float";
const char *FTInfo<double>::kCppTypeName = "double";
const char *FTInfo<long double>::kCppTypeName = "long double";
const char *FTInfo<__float128>::kCppTypeName = "__float128";

const char FTInfo<float>::kTypePattern[sizeof(float)];
const char FTInfo<double>::kTypePattern[sizeof(double)];
const char FTInfo<long double>::kTypePattern[sizeof(long double)];

// Helper for __nsan_dump_shadow_mem: Reads the value at address `ptr`,
// identified by its type id.
template <typename ShadowFT>
static __float128 ReadShadowInternal(const u8 *ptr) {
  ShadowFT Shadow;
  __builtin_memcpy(&Shadow, ptr, sizeof(Shadow));
  return Shadow;
}

static __float128 ReadShadow(const u8 *ptr, const char ShadowTypeId) {
  switch (ShadowTypeId) {
  case 'd':
    return ReadShadowInternal<double>(ptr);
  case 'l':
    return ReadShadowInternal<long double>(ptr);
  case 'q':
    return ReadShadowInternal<__float128>(ptr);
  default:
    return 0.0;
  }
}

namespace {
class Decorator : public __sanitizer::SanitizerCommonDecorator {
public:
  Decorator() : SanitizerCommonDecorator() {}
  const char *Warning() { return Red(); }
  const char *Name() { return Green(); }
  const char *End() { return Default(); }
};

// Workaround for the fact that Printf() does not support floats.
struct PrintBuffer {
  char Buffer[64];
};
template <typename FT> struct FTPrinter {};

template <> struct FTPrinter<double> {
  static PrintBuffer dec(double value) {
    PrintBuffer result;
    snprintf(result.Buffer, sizeof(result.Buffer) - 1, "%.20f", value);
    return result;
  }
  static PrintBuffer hex(double value) {
    PrintBuffer result;
    snprintf(result.Buffer, sizeof(result.Buffer) - 1, "%.20a", value);
    return result;
  }
};

template <> struct FTPrinter<float> : FTPrinter<double> {};

template <> struct FTPrinter<long double> {
  static PrintBuffer dec(long double value) {
    PrintBuffer result;
    snprintf(result.Buffer, sizeof(result.Buffer) - 1, "%.20Lf", value);
    return result;
  }
  static PrintBuffer hex(long double value) {
    PrintBuffer result;
    snprintf(result.Buffer, sizeof(result.Buffer) - 1, "%.20La", value);
    return result;
  }
};

// FIXME: print with full precision.
template <> struct FTPrinter<__float128> : FTPrinter<long double> {};

// This is a template so that there are no implicit conversions.
template <typename FT> inline FT ftAbs(FT v);

template <> inline long double ftAbs(long double v) { return fabsl(v); }
template <> inline double ftAbs(double v) { return fabs(v); }

// We don't care about nans.
// std::abs(__float128) code is suboptimal and generates a function call to
// __getf2().
template <typename FT> inline FT ftAbs(FT v) { return v >= FT{0} ? v : -v; }

template <typename FT1, typename FT2, bool Enable> struct LargestFTImpl {
  using type = FT2;
};

template <typename FT1, typename FT2> struct LargestFTImpl<FT1, FT2, true> {
  using type = FT1;
};

template <typename FT1, typename FT2>
using LargestFT =
    typename LargestFTImpl<FT1, FT2, (sizeof(FT1) > sizeof(FT2))>::type;

template <typename T> T max(T a, T b) { return a < b ? b : a; }

} // end anonymous namespace

void __sanitizer::BufferedStackTrace::UnwindImpl(uptr pc, uptr bp,
                                                 void *context,
                                                 bool request_fast,
                                                 u32 max_depth) {
  using namespace __nsan;
  return Unwind(max_depth, pc, bp, context, 0, 0, false);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void __nsan_print_accumulated_stats() {
  if (nsan_stats)
    nsan_stats->Print();
}

static void NsanAtexit() {
  Printf("Numerical Sanitizer exit stats:\n");
  __nsan_print_accumulated_stats();
  nsan_stats = nullptr;
}

// The next three functions return a pointer for storing a shadow value for `n`
// values, after setting the shadow types. We return the pointer instead of
// storing ourselves because it avoids having to rely on the calling convention
// around long double being the same for nsan and the target application.
// We have to have 3 versions because we need to know which type we are storing
// since we are setting the type shadow memory.
template <typename FT> static u8 *getShadowPtrForStore(u8 *store_addr, uptr n) {
  unsigned char *shadow_type = GetShadowTypeAddrFor(store_addr);
  for (uptr i = 0; i < n; ++i) {
    __builtin_memcpy(shadow_type + i * sizeof(FT), FTInfo<FT>::kTypePattern,
                     sizeof(FTInfo<FT>::kTypePattern));
  }
  return GetShadowAddrFor(store_addr);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE u8 *
__nsan_get_shadow_ptr_for_float_store(u8 *store_addr, uptr n) {
  return getShadowPtrForStore<float>(store_addr, n);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE u8 *
__nsan_get_shadow_ptr_for_double_store(u8 *store_addr, uptr n) {
  return getShadowPtrForStore<double>(store_addr, n);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE u8 *
__nsan_get_shadow_ptr_for_longdouble_store(u8 *store_addr, uptr n) {
  return getShadowPtrForStore<long double>(store_addr, n);
}

template <typename FT> static bool IsValidShadowType(const u8 *shadow_type) {
  return __builtin_memcmp(shadow_type, FTInfo<FT>::kTypePattern, sizeof(FT)) ==
         0;
}

template <int kSize, typename T> static bool IsZero(const T *ptr) {
  constexpr const char kZeros[kSize] = {}; // Zero initialized.
  return __builtin_memcmp(ptr, kZeros, kSize) == 0;
}

template <typename FT> static bool IsUnknownShadowType(const u8 *shadow_type) {
  return IsZero<sizeof(FTInfo<FT>::kTypePattern)>(shadow_type);
}

// The three folowing functions check that the address stores a complete
// shadow value of the given type and return a pointer for loading.
// They return nullptr if the type of the value is unknown or incomplete.
template <typename FT>
static const u8 *getShadowPtrForLoad(const u8 *load_addr, uptr n) {
  const u8 *const shadow_type = GetShadowTypeAddrFor(load_addr);
  for (uptr i = 0; i < n; ++i) {
    if (!IsValidShadowType<FT>(shadow_type + i * sizeof(FT))) {
      // If loadtracking stats are enabled, log loads with invalid types
      // (tampered with through type punning).
      if (flags().enable_loadtracking_stats) {
        if (IsUnknownShadowType<FT>(shadow_type + i * sizeof(FT))) {
          // Warn only if the value is non-zero. Zero is special because
          // applications typically initialize large buffers to zero in an
          // untyped way.
          if (!IsZero<sizeof(FT)>(load_addr)) {
            GET_CALLER_PC_BP;
            nsan_stats->AddUnknownLoadTrackingEvent(pc, bp);
          }
        } else {
          GET_CALLER_PC_BP;
          nsan_stats->AddInvalidLoadTrackingEvent(pc, bp);
        }
      }
      return nullptr;
    }
  }
  return GetShadowAddrFor(load_addr);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE const u8 *
__nsan_get_shadow_ptr_for_float_load(const u8 *load_addr, uptr n) {
  return getShadowPtrForLoad<float>(load_addr, n);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE const u8 *
__nsan_get_shadow_ptr_for_double_load(const u8 *load_addr, uptr n) {
  return getShadowPtrForLoad<double>(load_addr, n);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE const u8 *
__nsan_get_shadow_ptr_for_longdouble_load(const u8 *load_addr, uptr n) {
  return getShadowPtrForLoad<long double>(load_addr, n);
}

// Returns the raw shadow pointer. The returned pointer should be considered
// opaque.
extern "C" SANITIZER_INTERFACE_ATTRIBUTE u8 *
__nsan_internal_get_raw_shadow_ptr(const u8 *addr) {
  return GetShadowAddrFor(const_cast<u8 *>(addr));
}

// Returns the raw shadow type pointer. The returned pointer should be
// considered opaque.
extern "C" SANITIZER_INTERFACE_ATTRIBUTE u8 *
__nsan_internal_get_raw_shadow_type_ptr(const u8 *addr) {
  return reinterpret_cast<u8 *>(GetShadowTypeAddrFor(const_cast<u8 *>(addr)));
}

static ValueType getValueType(u8 c) { return static_cast<ValueType>(c & 0x3); }

static int getValuePos(u8 c) { return c >> kValueSizeSizeBits; }

// Checks the consistency of the value types at the given type pointer.
// If the value is inconsistent, returns ValueType::kUnknown. Else, return the
// consistent type.
template <typename FT>
static bool checkValueConsistency(const u8 *shadow_type) {
  const int pos = getValuePos(*shadow_type);
  // Check that all bytes from the start of the value are ordered.
  for (uptr i = 0; i < sizeof(FT); ++i) {
    const u8 T = *(shadow_type - pos + i);
    if (!(getValueType(T) == FTInfo<FT>::kValueType && getValuePos(T) == i))
      return false;
  }
  return true;
}

// The instrumentation automatically appends `shadow_value_type_ids`, see
// maybeAddSuffixForNsanInterface.
extern "C" SANITIZER_INTERFACE_ATTRIBUTE void
__nsan_dump_shadow_mem(const u8 *addr, size_t size_bytes, size_t bytes_per_line,
                       size_t shadow_value_type_ids) {
  const u8 *const shadow_type = GetShadowTypeAddrFor(addr);
  const u8 *const shadow = GetShadowAddrFor(addr);

  constexpr int kMaxNumDecodedValues = 16;
  __float128 decoded_values[kMaxNumDecodedValues];
  int num_decoded_values = 0;
  if (bytes_per_line > 4 * kMaxNumDecodedValues)
    bytes_per_line = 4 * kMaxNumDecodedValues;

  // We keep track of the current type and position as we go.
  ValueType LastValueTy = kUnknownValueType;
  int LastPos = -1;
  size_t Offset = 0;
  for (size_t R = 0; R < (size_bytes + bytes_per_line - 1) / bytes_per_line;
       ++R) {
    printf("%p:    ", (void *)(addr + R * bytes_per_line));
    for (size_t C = 0; C < bytes_per_line && Offset < size_bytes; ++C) {
      const ValueType ValueTy = getValueType(shadow_type[Offset]);
      const int pos = getValuePos(shadow_type[Offset]);
      if (ValueTy == LastValueTy && pos == LastPos + 1) {
        ++LastPos;
      } else {
        LastValueTy = ValueTy;
        LastPos = pos == 0 ? 0 : -1;
      }

      switch (ValueTy) {
      case kUnknownValueType:
        printf("__ ");
        break;
      case kFloatValueType:
        printf("f%x ", pos);
        if (LastPos == sizeof(float) - 1) {
          decoded_values[num_decoded_values] =
              ReadShadow(shadow + kShadowScale * (Offset + 1 - sizeof(float)),
                         static_cast<char>(shadow_value_type_ids & 0xff));
          ++num_decoded_values;
        }
        break;
      case kDoubleValueType:
        printf("d%x ", pos);
        if (LastPos == sizeof(double) - 1) {
          decoded_values[num_decoded_values] = ReadShadow(
              shadow + kShadowScale * (Offset + 1 - sizeof(double)),
              static_cast<char>((shadow_value_type_ids >> 8) & 0xff));
          ++num_decoded_values;
        }
        break;
      case kFp80ValueType:
        printf("l%x ", pos);
        if (LastPos == sizeof(long double) - 1) {
          decoded_values[num_decoded_values] = ReadShadow(
              shadow + kShadowScale * (Offset + 1 - sizeof(long double)),
              static_cast<char>((shadow_value_type_ids >> 16) & 0xff));
          ++num_decoded_values;
        }
        break;
      }
      ++Offset;
    }
    for (int i = 0; i < num_decoded_values; ++i) {
      printf("  (%s)", FTPrinter<__float128>::dec(decoded_values[i]).Buffer);
    }
    num_decoded_values = 0;
    printf("\n");
  }
}

alignas(16) SANITIZER_INTERFACE_ATTRIBUTE
    thread_local uptr __nsan_shadow_ret_tag = 0;

alignas(16) SANITIZER_INTERFACE_ATTRIBUTE
    thread_local char __nsan_shadow_ret_ptr[kMaxVectorWidth *
                                            sizeof(__float128)];

alignas(16) SANITIZER_INTERFACE_ATTRIBUTE
    thread_local uptr __nsan_shadow_args_tag = 0;

// Maximum number of args. This should be enough for anyone (tm). An alternate
// scheme is to have the generated code create an alloca and make
// __nsan_shadow_args_ptr point ot the alloca.
constexpr const int kMaxNumArgs = 128;
alignas(16) SANITIZER_INTERFACE_ATTRIBUTE
    thread_local char __nsan_shadow_args_ptr[kMaxVectorWidth * kMaxNumArgs *
                                             sizeof(__float128)];

enum ContinuationType { // Keep in sync with instrumentation pass.
  kContinueWithShadow = 0,
  kResumeFromValue = 1,
};

// Checks the consistency between application and shadow value. Returns true
// when the instrumented code should resume computations from the original value
// rather than the shadow value. This prevents one error to propagate to all
// subsequent operations. This behaviour is tunable with flags.
template <typename FT, typename ShadowFT>
int32_t checkFT(const FT value, ShadowFT Shadow, CheckTypeT CheckType,
                uptr CheckArg) {
  // We do all comparisons in the InternalFT domain, which is the largest FT
  // type.
  using InternalFT = LargestFT<FT, ShadowFT>;
  const InternalFT check_value = value;
  const InternalFT check_shadow = Shadow;

  // See this article for an interesting discussion of how to compare floats:
  // https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition/
  static constexpr const FT Eps = FTInfo<FT>::kEpsilon;

  const InternalFT abs_err = ftAbs(check_value - check_shadow);

  if (flags().enable_check_stats) {
    GET_CALLER_PC_BP;
    // We are re-computing `largest` here because this is a cold branch, and we
    // want to avoid having to move the computation of `largest` before the
    // absolute value check when this branch is not taken.
    const InternalFT largest = max(ftAbs(check_value), ftAbs(check_shadow));
    nsan_stats->AddCheck(CheckType, pc, bp, abs_err / largest);
  }

  // Note: writing the comparison that way ensures that when `abs_err` is Nan
  // (value and shadow are inf or -inf), we pass the test.
  if (!(abs_err >= flags().cached_absolute_error_threshold))
    return kContinueWithShadow;

  const InternalFT largest = max(ftAbs(check_value), ftAbs(check_shadow));
  if (abs_err * (1ull << flags().log2_max_relative_error) <= largest)
    return kContinueWithShadow; // No problem here.

  if (!flags().disable_warnings) {
    GET_CALLER_PC_BP;
    BufferedStackTrace stack;
    stack.Unwind(pc, bp, nullptr, false);
    if (GetSuppressionForStack(&stack, CheckKind::Consistency)) {
      // FIXME: optionally print.
      return flags().resume_after_suppression ? kResumeFromValue
                                              : kContinueWithShadow;
    }

    Decorator D;
    Printf("%s", D.Warning());
    // Printf does not support float formatting.
    char RelErrBuf[64] = "inf";
    if (largest > Eps) {
      snprintf(RelErrBuf, sizeof(RelErrBuf) - 1, "%.20Lf%% (2^%.0Lf epsilons)",
               static_cast<long double>(100.0 * abs_err / largest),
               log2l(static_cast<long double>(abs_err / largest / Eps)));
    }
    char ulp_err_buf[128] = "";
    const double shadow_ulp_diff = GetULPDiff(check_value, check_shadow);
    if (shadow_ulp_diff != kMaxULPDiff) {
      // This is the ULP diff in the internal domain. The user actually cares
      // about that in the original domain.
      const double ulp_diff =
          shadow_ulp_diff / (u64{1} << (FTInfo<InternalFT>::kMantissaBits -
                                        FTInfo<FT>::kMantissaBits));
      snprintf(ulp_err_buf, sizeof(ulp_err_buf) - 1,
               "(%.0f ULPs == %.1f digits == %.1f bits)", ulp_diff,
               log10(ulp_diff), log2(ulp_diff));
    }
    Printf("WARNING: NumericalStabilitySanitizer: inconsistent shadow results");
    switch (CheckType) {
    case CheckTypeT::kUnknown:
    case CheckTypeT::kFcmp:
    case CheckTypeT::kMaxCheckType:
      break;
    case CheckTypeT::kRet:
      Printf(" while checking return value");
      break;
    case CheckTypeT::kArg:
      Printf(" while checking call argument #%d", static_cast<int>(CheckArg));
      break;
    case CheckTypeT::kLoad:
      Printf(
          " while checking load from address 0x%lx. This is due to incorrect "
          "shadow memory tracking, typically due to uninstrumented code "
          "writing to memory.",
          CheckArg);
      break;
    case CheckTypeT::kStore:
      Printf(" while checking store to address 0x%lx", CheckArg);
      break;
    case CheckTypeT::kInsert:
      Printf(" while checking vector insert");
      break;
    case CheckTypeT::kUser:
      Printf(" in user-initiated check");
      break;
    }
    using ValuePrinter = FTPrinter<FT>;
    using ShadowPrinter = FTPrinter<ShadowFT>;
    Printf("%s", D.Default());

    Printf("\n"
           "%-12s precision  (native): dec: %s  hex: %s\n"
           "%-12s precision  (shadow): dec: %s  hex: %s\n"
           "shadow truncated to %-12s: dec: %s  hex: %s\n"
           "Relative error: %s\n"
           "Absolute error: %s\n"
           "%s\n",
           FTInfo<FT>::kCppTypeName, ValuePrinter::dec(value).Buffer,
           ValuePrinter::hex(value).Buffer, FTInfo<ShadowFT>::kCppTypeName,
           ShadowPrinter::dec(Shadow).Buffer, ShadowPrinter::hex(Shadow).Buffer,
           FTInfo<FT>::kCppTypeName, ValuePrinter::dec(Shadow).Buffer,
           ValuePrinter::hex(Shadow).Buffer, RelErrBuf,
           ValuePrinter::hex(abs_err).Buffer, ulp_err_buf);
    stack.Print();
  }

  if (flags().enable_warning_stats) {
    GET_CALLER_PC_BP;
    nsan_stats->AddWarning(CheckType, pc, bp, abs_err / largest);
  }

  if (flags().halt_on_error) {
    if (common_flags()->abort_on_error)
      Printf("ABORTING\n");
    else
      Printf("Exiting\n");
    Die();
  }
  return flags().resume_after_warning ? kResumeFromValue : kContinueWithShadow;
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE int32_t __nsan_internal_check_float_d(
    float value, double shadow, int32_t check_type, uptr check_arg) {
  return checkFT(value, shadow, static_cast<CheckTypeT>(check_type), check_arg);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE int32_t __nsan_internal_check_double_l(
    double value, long double shadow, int32_t check_type, uptr check_arg) {
  return checkFT(value, shadow, static_cast<CheckTypeT>(check_type), check_arg);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE int32_t __nsan_internal_check_double_q(
    double value, __float128 shadow, int32_t check_type, uptr check_arg) {
  return checkFT(value, shadow, static_cast<CheckTypeT>(check_type), check_arg);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE int32_t
__nsan_internal_check_longdouble_q(long double value, __float128 shadow,
                                   int32_t check_type, uptr check_arg) {
  return checkFT(value, shadow, static_cast<CheckTypeT>(check_type), check_arg);
}

static const char *GetTruthValueName(bool v) { return v ? "true" : "false"; }

// This uses the same values as CmpInst::Predicate.
static const char *GetPredicateName(int v) {
  switch (v) {
  case 0:
    return "(false)";
  case 1:
    return "==";
  case 2:
    return ">";
  case 3:
    return ">=";
  case 4:
    return "<";
  case 5:
    return "<=";
  case 6:
    return "!=";
  case 7:
    return "(ordered)";
  case 8:
    return "(unordered)";
  case 9:
    return "==";
  case 10:
    return ">";
  case 11:
    return ">=";
  case 12:
    return "<";
  case 13:
    return "<=";
  case 14:
    return "!=";
  case 15:
    return "(true)";
  }
  return "??";
}

template <typename FT, typename ShadowFT>
void fCmpFailFT(const FT Lhs, const FT Rhs, ShadowFT LhsShadow,
                ShadowFT RhsShadow, int Predicate, bool result,
                bool ShadowResult) {
  if (result == ShadowResult) {
    // When a vector comparison fails, we fail each element of the comparison
    // to simplify instrumented code. Skip elements where the shadow comparison
    // gave the same result as the original one.
    return;
  }

  GET_CALLER_PC_BP;
  BufferedStackTrace stack;
  stack.Unwind(pc, bp, nullptr, false);

  if (GetSuppressionForStack(&stack, CheckKind::Fcmp)) {
    // FIXME: optionally print.
    return;
  }

  if (flags().enable_warning_stats)
    nsan_stats->AddWarning(CheckTypeT::kFcmp, pc, bp, 0.0);

  if (flags().disable_warnings)
    return;

  // FIXME: ideally we would print the shadow value as FP128. Right now because
  // we truncate to long double we can sometimes see stuff like:
  // shadow <value> == <value> (false)
  using ValuePrinter = FTPrinter<FT>;
  using ShadowPrinter = FTPrinter<ShadowFT>;
  Decorator D;
  const char *const PredicateName = GetPredicateName(Predicate);
  Printf("%s", D.Warning());
  Printf("WARNING: NumericalStabilitySanitizer: floating-point comparison "
         "results depend on precision\n");
  Printf("%s", D.Default());
  Printf("%-12s precision dec (native): %s %s %s (%s)\n"
         "%-12s precision dec (shadow): %s %s %s (%s)\n"
         "%-12s precision hex (native): %s %s %s (%s)\n"
         "%-12s precision hex (shadow): %s %s %s (%s)\n"
         "%s",
         // Native, decimal.
         FTInfo<FT>::kCppTypeName, ValuePrinter::dec(Lhs).Buffer, PredicateName,
         ValuePrinter::dec(Rhs).Buffer, GetTruthValueName(result),
         // Shadow, decimal
         FTInfo<ShadowFT>::kCppTypeName, ShadowPrinter::dec(LhsShadow).Buffer,
         PredicateName, ShadowPrinter::dec(RhsShadow).Buffer,
         GetTruthValueName(ShadowResult),
         // Native, hex.
         FTInfo<FT>::kCppTypeName, ValuePrinter::hex(Lhs).Buffer, PredicateName,
         ValuePrinter::hex(Rhs).Buffer, GetTruthValueName(result),
         // Shadow, hex
         FTInfo<ShadowFT>::kCppTypeName, ShadowPrinter::hex(LhsShadow).Buffer,
         PredicateName, ShadowPrinter::hex(RhsShadow).Buffer,
         GetTruthValueName(ShadowResult), D.End());
  stack.Print();
  if (flags().halt_on_error) {
    Printf("Exiting\n");
    Die();
  }
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void
__nsan_fcmp_fail_float_d(float lhs, float rhs, double lhs_shadow,
                         double rhs_shadow, int predicate, bool result,
                         bool shadow_result) {
  fCmpFailFT(lhs, rhs, lhs_shadow, rhs_shadow, predicate, result,
             shadow_result);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void
__nsan_fcmp_fail_double_q(double lhs, double rhs, __float128 lhs_shadow,
                          __float128 rhs_shadow, int predicate, bool result,
                          bool shadow_result) {
  fCmpFailFT(lhs, rhs, lhs_shadow, rhs_shadow, predicate, result,
             shadow_result);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void
__nsan_fcmp_fail_double_l(double lhs, double rhs, long double lhs_shadow,
                          long double rhs_shadow, int predicate, bool result,
                          bool shadow_result) {
  fCmpFailFT(lhs, rhs, lhs_shadow, rhs_shadow, predicate, result,
             shadow_result);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void
__nsan_fcmp_fail_longdouble_q(long double lhs, long double rhs,
                              __float128 lhs_shadow, __float128 rhs_shadow,
                              int predicate, bool result, bool shadow_result) {
  fCmpFailFT(lhs, rhs, lhs_shadow, rhs_shadow, predicate, result,
             shadow_result);
}

template <typename FT> void checkFTFromShadowStack(const FT value) {
  // Get the shadow 2FT value from the shadow stack. Note that
  // __nsan_check_{float,double,long double} is a function like any other, so
  // the instrumentation will have placed the shadow value on the shadow stack.
  using ShadowFT = typename FTInfo<FT>::shadow_type;
  ShadowFT Shadow;
  __builtin_memcpy(&Shadow, __nsan_shadow_args_ptr, sizeof(ShadowFT));
  checkFT(value, Shadow, CheckTypeT::kUser, 0);
}

// FIXME: Add suffixes and let the instrumentation pass automatically add
// suffixes.
extern "C" SANITIZER_INTERFACE_ATTRIBUTE void __nsan_check_float(float value) {
  assert(__nsan_shadow_args_tag == (uptr)&__nsan_check_float &&
         "__nsan_check_float called from non-instrumented function");
  checkFTFromShadowStack(value);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void
__nsan_check_double(double value) {
  assert(__nsan_shadow_args_tag == (uptr)&__nsan_check_double &&
         "__nsan_check_double called from non-instrumented function");
  checkFTFromShadowStack(value);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void
__nsan_check_longdouble(long double value) {
  assert(__nsan_shadow_args_tag == (uptr)&__nsan_check_longdouble &&
         "__nsan_check_longdouble called from non-instrumented function");
  checkFTFromShadowStack(value);
}

template <typename FT> static void dumpFTFromShadowStack(const FT value) {
  // Get the shadow 2FT value from the shadow stack. Note that
  // __nsan_dump_{float,double,long double} is a function like any other, so
  // the instrumentation will have placed the shadow value on the shadow stack.
  using ShadowFT = typename FTInfo<FT>::shadow_type;
  ShadowFT shadow;
  __builtin_memcpy(&shadow, __nsan_shadow_args_ptr, sizeof(ShadowFT));
  using ValuePrinter = FTPrinter<FT>;
  using ShadowPrinter = FTPrinter<typename FTInfo<FT>::shadow_type>;
  printf("value  dec:%s hex:%s\n"
         "shadow dec:%s hex:%s\n",
         ValuePrinter::dec(value).Buffer, ValuePrinter::hex(value).Buffer,
         ShadowPrinter::dec(shadow).Buffer, ShadowPrinter::hex(shadow).Buffer);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void __nsan_dump_float(float value) {
  assert(__nsan_shadow_args_tag == (uptr)&__nsan_dump_float &&
         "__nsan_dump_float called from non-instrumented function");
  dumpFTFromShadowStack(value);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void __nsan_dump_double(double value) {
  assert(__nsan_shadow_args_tag == (uptr)&__nsan_dump_double &&
         "__nsan_dump_double called from non-instrumented function");
  dumpFTFromShadowStack(value);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void
__nsan_dump_longdouble(long double value) {
  assert(__nsan_shadow_args_tag == (uptr)&__nsan_dump_longdouble &&
         "__nsan_dump_longdouble called from non-instrumented function");
  dumpFTFromShadowStack(value);
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void __nsan_dump_shadow_ret() {
  printf("ret tag: %lx\n", __nsan_shadow_ret_tag);
  double v;
  __builtin_memcpy(&v, __nsan_shadow_ret_ptr, sizeof(double));
  printf("double value: %f\n", v);
  // FIXME: float128 value.
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void __nsan_dump_shadow_args() {
  printf("args tag: %lx\n", __nsan_shadow_args_tag);
}

bool __nsan::nsan_initialized;
bool __nsan::nsan_init_is_running;

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void __nsan_init() {
  CHECK(!nsan_init_is_running);
  if (nsan_initialized)
    return;
  nsan_init_is_running = true;

  InitializeFlags();
  InitializeSuppressions();
  InitializePlatformEarly();

  DisableCoreDumperIfNecessary();

  if (!MmapFixedNoReserve(TypesAddr(), UnusedAddr() - TypesAddr()))
    Die();

  InitializeInterceptors();

  InitializeStats();
  if (flags().print_stats_on_exit)
    Atexit(NsanAtexit);

  nsan_init_is_running = false;
  nsan_initialized = true;
}
