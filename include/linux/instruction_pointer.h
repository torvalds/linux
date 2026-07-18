/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_INSTRUCTION_POINTER_H
#define _LINUX_INSTRUCTION_POINTER_H

#include <asm/linkage.h>

#define _RET_IP_		(unsigned long)__builtin_return_address(0)

#ifndef _THIS_IP_
#define _THIS_IP_  ({ __label__ __here; __here: (unsigned long)&&__here; })
/*
 * The current generic definition of _THIS_IP_ is considered broken by GCC [1]
 * and Clang [2]. In particular, the address of a label is only expected to be
 * used with a computed goto.
 *
 *   [1] https://gcc.gnu.org/bugzilla/show_bug.cgi?id=120071
 *   [2] https://github.com/llvm/llvm-project/issues/138272
 *
 * Mark it as broken, so that appropriate fallback options can be implemented
 * for architectures that do not define their own _THIS_IP_.
 */
#define HAS_BROKEN_THIS_IP
#endif

/*
 * _CODE_LOCATION_ provides a unique identifier for the current code location.
 * When _THIS_IP_ is broken (generic version), we fall back to a static marker
 * which guarantees uniqueness and resolves to a constant address at link time,
 * avoiding runtime overhead and compiler optimizations breaking it.
 */
#ifdef HAS_BROKEN_THIS_IP
#define _CODE_LOCATION_ ({ static const char __here; (unsigned long)&__here; })
#else
#define _CODE_LOCATION_ _THIS_IP_
#endif

#endif /* _LINUX_INSTRUCTION_POINTER_H */
