/* SPDX-License-Identifier: GPL-2.0 */

#ifdef HAVE_ARCH_X86_64_SUPPORT

#define MEMSET_FN(fn, name, desc)		\
	void *fn(void *, int, size_t);

#include "mem-memset-x86-64-asm-def.h"

#undef MEMSET_FN

#endif

