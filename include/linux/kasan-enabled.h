/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KASAN_ENABLED_H
#define _LINUX_KASAN_ENABLED_H

#include <linux/static_key.h>

#if defined(CONFIG_ARCH_DEFER_KASAN) || defined(CONFIG_KASAN_HW_TAGS)
/*
 * Global runtime flag for KASAN modes that need runtime control.
 * Used by ARCH_DEFER_KASAN architectures and HW_TAGS mode.
 */
DECLARE_STATIC_KEY_FALSE(kasan_flag_enabled);

/*
 * Runtime control for shadow memory initialization or HW_TAGS mode.
 * Uses static key for architectures that need deferred KASAN or HW_TAGS.
 */
static __always_inline bool kasan_enabled(void)
{
	return static_branch_likely(&kasan_flag_enabled);
}

static inline void kasan_enable(void)
{
	static_branch_enable(&kasan_flag_enabled);
}
#else
/* For architectures that can enable KASAN early, use compile-time check. */
static __always_inline bool kasan_enabled(void)
{
	return IS_ENABLED(CONFIG_KASAN);
}

static inline void kasan_enable(void) {}
#endif /* CONFIG_ARCH_DEFER_KASAN || CONFIG_KASAN_HW_TAGS */

#ifdef CONFIG_KASAN_HW_TAGS
static inline bool kasan_hw_tags_enabled(void)
{
	return kasan_enabled();
}
#else
static inline bool kasan_hw_tags_enabled(void)
{
	return false;
}
#endif /* CONFIG_KASAN_HW_TAGS */

#endif /* LINUX_KASAN_ENABLED_H */
