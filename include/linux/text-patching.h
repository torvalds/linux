/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TEXT_PATCHING_H
#define _LINUX_TEXT_PATCHING_H

#include <asm/text-patching.h>

#ifndef text_poke_copy
static inline void *text_poke_copy(void *dst, const void *src, size_t len)
{
	return memcpy(dst, src, len);
}
#define text_poke_copy text_poke_copy
#endif

#endif /* _LINUX_TEXT_PATCHING_H */
