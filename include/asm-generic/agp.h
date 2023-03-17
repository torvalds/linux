/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_AGP_H
#define _ASM_GENERIC_AGP_H

#include <asm/io.h>

#define map_page_into_agp(page) do {} while (0)
#define unmap_page_from_agp(page) do {} while (0)
#define flush_agp_cache() mb()

#endif	/* _ASM_GENERIC_AGP_H */
