// SPDX-License-Identifier: Apache-2.0 OR MIT

// The contents of this file come from the Rust rustc-demangle library, hosted
// in the <https://github.com/rust-lang/rustc-demangle> repository, licensed
// under "Apache-2.0 OR MIT". For copyright details, see
// <https://github.com/rust-lang/rustc-demangle/blob/main/README.md>.
// Please note that the file should be kept as close as possible to upstream.

#ifndef _H_DEMANGLE_V0_H
#define _H_DEMANGLE_V0_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#if defined(__GNUC__) || defined(__clang__)
#define DEMANGLE_NODISCARD __attribute__((warn_unused_result))
#else
#define DEMANGLE_NODISCARD
#endif

typedef enum {
    OverflowOk,
    OverflowOverflow
} overflow_status;

enum demangle_style {
    DemangleStyleUnknown = 0,
    DemangleStyleLegacy,
    DemangleStyleV0,
};

// Not using a union here to make the struct easier to copy-paste if needed.
struct demangle {
    enum demangle_style style;
    // points to the "mangled" part of the name,
    // not including `ZN` or `R` prefixes.
    const char *mangled;
    size_t mangled_len;
    // In DemangleStyleLegacy, is the number of path elements
    size_t elements;
    // while it's called "original", it will not contain `.llvm.9D1C9369@@16` suffixes
    // that are to be ignored.
    const char *original;
    size_t original_len;
    // Contains the part after the mangled name that is to be outputted,
    // which can be `.exit.i.i` suffixes LLVM sometimes adds.
    const char *suffix;
    size_t suffix_len;
};

// if the length of the output buffer is less than `output_len-OVERFLOW_MARGIN`,
// the demangler will return `OverflowOverflow` even if there is no overflow.
#define OVERFLOW_MARGIN 4

/// Demangle a C string that refers to a Rust symbol and put the demangle intermediate result in `res`.
/// Beware that `res` contains references into `s`. If `s` is modified (or free'd) before calling
/// `rust_demangle_display_demangle` behavior is undefined.
///
/// Use `rust_demangle_display_demangle` to convert it to an actual string.
void rust_demangle_demangle(const char *s, struct demangle *res);

/// Write the string in a `struct demangle` into a buffer.
///
/// Return `OverflowOk` if the output buffer was sufficiently big, `OverflowOverflow` if it wasn't.
/// This function is `O(n)` in the length of the input + *output* [$], but the demangled output of demangling a symbol can
/// be exponentially[$$] large, therefore it is recommended to have a sane bound (`rust-demangle`
/// uses 1,000,000 bytes) on `len`.
///
/// `alternate`, if true, uses the less verbose alternate formatting (Rust `{:#}`) is used, which does not show
/// symbol hashes and types of constant ints.
///
/// [$] It's `O(n * MAX_DEPTH)`, but `MAX_DEPTH` is a constant 300 and therefore it's `O(n)`
/// [$$] Technically, bounded by `O(n^MAX_DEPTH)`, but this is practically exponential.
DEMANGLE_NODISCARD overflow_status rust_demangle_display_demangle(struct demangle const *res, char *out, size_t len, bool alternate);

/// Returns true if `res` refers to a known valid Rust demangling style, false if it's an unknown style.
bool rust_demangle_is_known(struct demangle *res);

#undef DEMANGLE_NODISCARD

#ifdef __cplusplus
}
#endif

#endif
