/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_BPF_COMPILER_TYPES_H
#define __LINUX_BPF_COMPILER_TYPES_H

#include <linux/compiler_types.h>

/*
 * Qualifiers for the BPF verifier rather than address spaces: __arena marks a
 * pointer into a BPF arena, __nullable a pointer that may be NULL. They reach
 * BPF programs as BTF type tags on the members of kernel structs shared with
 * them; to kernel code and to sparse the pointers are ordinary.
 */
#ifdef __CHECKER__
# define __arena
# define __nullable
#else
# define __arena	BTF_TYPE_TAG(arena)
# define __nullable	BTF_TYPE_TAG(nullable)
#endif

#endif /* __LINUX_BPF_COMPILER_TYPES_H */
