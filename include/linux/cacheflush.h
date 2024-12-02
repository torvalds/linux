/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CACHEFLUSH_H
#define _LINUX_CACHEFLUSH_H

#include <asm/cacheflush.h>

struct folio;

#if ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE
#ifndef ARCH_IMPLEMENTS_FLUSH_DCACHE_FOLIO
void flush_dcache_folio(struct folio *folio);
#endif
#else
static inline void flush_dcache_folio(struct folio *folio)
{
}
#define ARCH_IMPLEMENTS_FLUSH_DCACHE_FOLIO 0
#endif /* ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE */

#endif /* _LINUX_CACHEFLUSH_H */
