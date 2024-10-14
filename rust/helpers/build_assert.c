// SPDX-License-Identifier: GPL-2.0

#include <linux/build_bug.h>

/*
 * `bindgen` binds the C `size_t` type as the Rust `usize` type, so we can
 * use it in contexts where Rust expects a `usize` like slice (array) indices.
 * `usize` is defined to be the same as C's `uintptr_t` type (can hold any
 * pointer) but not necessarily the same as `size_t` (can hold the size of any
 * single object). Most modern platforms use the same concrete integer type for
 * both of them, but in case we find ourselves on a platform where
 * that's not true, fail early instead of risking ABI or
 * integer-overflow issues.
 *
 * If your platform fails this assertion, it means that you are in
 * danger of integer-overflow bugs (even if you attempt to add
 * `--no-size_t-is-usize`). It may be easiest to change the kernel ABI on
 * your platform such that `size_t` matches `uintptr_t` (i.e., to increase
 * `size_t`, because `uintptr_t` has to be at least as big as `size_t`).
 */
static_assert(
	sizeof(size_t) == sizeof(uintptr_t) &&
	__alignof__(size_t) == __alignof__(uintptr_t),
	"Rust code expects C `size_t` to match Rust `usize`"
);
