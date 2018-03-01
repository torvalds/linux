/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_COMPILER_TYPES_H
#error "Please don't include <linux/compiler-clang.h> directly, include <linux/compiler.h> instead."
#endif

/* Some compiler specific definitions are overwritten here
 * for Clang compiler
 */

#ifdef uninitialized_var
#undef uninitialized_var
#define uninitialized_var(x) x = *(&(x))
#endif

/* same as gcc, this was present in clang-2.6 so we can assume it works
 * with any version that can compile the kernel
 */
#define __UNIQUE_ID(prefix) __PASTE(__PASTE(__UNIQUE_ID_, prefix), __COUNTER__)

#define randomized_struct_fields_start	struct {
#define randomized_struct_fields_end	};

/* all clang versions usable with the kernel support KASAN ABI version 5 */
#define KASAN_ABI_VERSION 5

/* emulate gcc's __SANITIZE_ADDRESS__ flag */
#if __has_feature(address_sanitizer)
#define __SANITIZE_ADDRESS__
#endif
