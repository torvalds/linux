#include "sanitizer_common/sanitizer_atomic.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#ifdef KERNEL_USE
extern "C" void ubsan_message(const char *msg);
static void message(const char *msg) { ubsan_message(msg); }
#else
static void message(const char *msg) {
  (void)write(2, msg, strlen(msg));
}
#endif

static const int kMaxCallerPcs = 20;
static __sanitizer::atomic_uintptr_t caller_pcs[kMaxCallerPcs];
// Number of elements in caller_pcs. A special value of kMaxCallerPcs + 1 means
// that "too many errors" has already been reported.
static __sanitizer::atomic_uint32_t caller_pcs_sz;

__attribute__((noinline)) static bool report_this_error(uintptr_t caller) {
  if (caller == 0)
    return false;
  while (true) {
    unsigned sz = __sanitizer::atomic_load_relaxed(&caller_pcs_sz);
    if (sz > kMaxCallerPcs) return false;  // early exit
    // when sz==kMaxCallerPcs print "too many errors", but only when cmpxchg
    // succeeds in order to not print it multiple times.
    if (sz > 0 && sz < kMaxCallerPcs) {
      uintptr_t p;
      for (unsigned i = 0; i < sz; ++i) {
        p = __sanitizer::atomic_load_relaxed(&caller_pcs[i]);
        if (p == 0) break;  // Concurrent update.
        if (p == caller) return false;
      }
      if (p == 0) continue;  // FIXME: yield?
    }

    if (!__sanitizer::atomic_compare_exchange_strong(
            &caller_pcs_sz, &sz, sz + 1, __sanitizer::memory_order_seq_cst))
      continue;  // Concurrent update! Try again from the start.

    if (sz == kMaxCallerPcs) {
      message("ubsan: too many errors\n");
      return false;
    }
    __sanitizer::atomic_store_relaxed(&caller_pcs[sz], caller);
    return true;
  }
}

__attribute__((noinline)) static void decorate_msg(char *buf,
                                                   uintptr_t caller) {
  // print the address by nibbles
  for (unsigned shift = sizeof(uintptr_t) * 8; shift;) {
    shift -= 4;
    unsigned nibble = (caller >> shift) & 0xf;
    *(buf++) = nibble < 10 ? nibble + '0' : nibble - 10 + 'a';
  }
  // finish the message
  buf[0] = '\n';
  buf[1] = '\0';
}

#if defined(__ANDROID__)
extern "C" __attribute__((weak)) void android_set_abort_message(const char *);
static void abort_with_message(const char *msg) {
  if (&android_set_abort_message) android_set_abort_message(msg);
  abort();
}
#else
static void abort_with_message(const char *) { abort(); }
#endif

#if SANITIZER_DEBUG
namespace __sanitizer {
// The DCHECK macro needs this symbol to be defined.
void NORETURN CheckFailed(const char *file, int, const char *cond, u64, u64) {
  message("Sanitizer CHECK failed: ");
  message(file);
  message(":?? : "); // FIXME: Show line number.
  message(cond);
  abort();
}
} // namespace __sanitizer
#endif

#define INTERFACE extern "C" __attribute__((visibility("default")))

// How many chars we need to reserve to print an address.
constexpr unsigned kAddrBuf = SANITIZER_WORDSIZE / 4;
#define MSG_TMPL(msg) "ubsan: " msg " by 0x"
#define MSG_TMPL_END(buf, msg) (buf + sizeof(MSG_TMPL(msg)) - 1)
// Reserve an additional byte for '\n'.
#define MSG_BUF_LEN(msg) (sizeof(MSG_TMPL(msg)) + kAddrBuf + 1)

#define HANDLER_RECOVER(name, msg)                               \
  INTERFACE void __ubsan_handle_##name##_minimal() {             \
    uintptr_t caller = GET_CALLER_PC();                  \
    if (!report_this_error(caller)) return;                      \
    char msg_buf[MSG_BUF_LEN(msg)] = MSG_TMPL(msg);              \
    decorate_msg(MSG_TMPL_END(msg_buf, msg), caller);            \
    message(msg_buf);                                            \
  }

#define HANDLER_NORECOVER(name, msg)                             \
  INTERFACE void __ubsan_handle_##name##_minimal_abort() {       \
    char msg_buf[MSG_BUF_LEN(msg)] = MSG_TMPL(msg);              \
    decorate_msg(MSG_TMPL_END(msg_buf, msg), GET_CALLER_PC());   \
    message(msg_buf);                                            \
    abort_with_message(msg_buf);                                 \
  }

#define HANDLER(name, msg)                                       \
  HANDLER_RECOVER(name, msg)                                     \
  HANDLER_NORECOVER(name, msg)

HANDLER(type_mismatch, "type-mismatch")
HANDLER(alignment_assumption, "alignment-assumption")
HANDLER(add_overflow, "add-overflow")
HANDLER(sub_overflow, "sub-overflow")
HANDLER(mul_overflow, "mul-overflow")
HANDLER(negate_overflow, "negate-overflow")
HANDLER(divrem_overflow, "divrem-overflow")
HANDLER(shift_out_of_bounds, "shift-out-of-bounds")
HANDLER(out_of_bounds, "out-of-bounds")
HANDLER_RECOVER(builtin_unreachable, "builtin-unreachable")
HANDLER_RECOVER(missing_return, "missing-return")
HANDLER(vla_bound_not_positive, "vla-bound-not-positive")
HANDLER(float_cast_overflow, "float-cast-overflow")
HANDLER(load_invalid_value, "load-invalid-value")
HANDLER(invalid_builtin, "invalid-builtin")
HANDLER(invalid_objc_cast, "invalid-objc-cast")
HANDLER(function_type_mismatch, "function-type-mismatch")
HANDLER(implicit_conversion, "implicit-conversion")
HANDLER(nonnull_arg, "nonnull-arg")
HANDLER(nonnull_return, "nonnull-return")
HANDLER(nullability_arg, "nullability-arg")
HANDLER(nullability_return, "nullability-return")
HANDLER(pointer_overflow, "pointer-overflow")
HANDLER(cfi_check_fail, "cfi-check-fail")
