/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#ifndef _ANDROID_DEBUG_SYMBOLS_H
#define _ANDROID_DEBUG_SYMBOLS_H

enum android_debug_symbol {
	ADS_SDATA = 0,
	ADS_BSS_END,
	ADS_PER_CPU_START,
	ADS_PER_CPU_END,
	ADS_START_RO_AFTER_INIT,
	ADS_END_RO_AFTER_INIT,
	ADS_LINUX_BANNER,
#ifdef CONFIG_CMA
	ADS_TOTAL_CMA,
#endif
	ADS_SLAB_CACHES,
	ADS_SLAB_MUTEX,
	ADS_MIN_LOW_PFN,
	ADS_MAX_PFN,
#ifdef CONFIG_PAGE_OWNER
	ADS_PAGE_OWNER_ENABLED,
#endif
#ifdef CONFIG_SLUB_DEBUG
	ADS_SLUB_DEBUG,
#endif
	ADS_END
};

enum android_debug_per_cpu_symbol {
	ADS_IRQ_STACK_PTR = 0,
	ADS_DEBUG_PER_CPU_END
};

#ifdef CONFIG_ANDROID_DEBUG_SYMBOLS

void *android_debug_symbol(enum android_debug_symbol symbol);
void *android_debug_per_cpu_symbol(enum android_debug_per_cpu_symbol symbol);

#else /* !CONFIG_ANDROID_DEBUG_SYMBOLS */

static inline void *android_debug_symbol(enum android_debug_symbol symbol)
{
	return NULL;
}
static inline void *android_debug_per_cpu_symbol(enum android_debug_per_cpu_symbol symbol)
{
	return NULL;
}
#endif /* CONFIG_ANDROID_DEBUG_SYMBOLS */

#endif /* _ANDROID_DEBUG_SYMBOLS_H */
