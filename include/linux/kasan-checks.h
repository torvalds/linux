/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KASAN_CHECKS_H
#define _LINUX_KASAN_CHECKS_H

#include <linux/types.h>

/*
 * __kasan_check_*: Always available when KASAN is enabled. This may be used
 * even in compilation units that selectively disable KASAN, but must use KASAN
 * to validate access to an address.   Never use these in header files!
 */
#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)
bool __kasan_check_read(const volatile void *p, unsigned int size);
bool __kasan_check_write(const volatile void *p, unsigned int size);
#else
static inline bool __kasan_check_read(const volatile void *p, unsigned int size)
{
	return true;
}
static inline bool __kasan_check_write(const volatile void *p, unsigned int size)
{
	return true;
}
#endif

/*
 * kasan_check_*: Only available when the particular compilation unit has KASAN
 * instrumentation enabled. May be used in header files.
 */
#ifdef __SANITIZE_ADDRESS__
#define kasan_check_read __kasan_check_read
#define kasan_check_write __kasan_check_write
#else
static inline bool kasan_check_read(const volatile void *p, unsigned int size)
{
	return true;
}
static inline bool kasan_check_write(const volatile void *p, unsigned int size)
{
	return true;
}
#endif

#endif
