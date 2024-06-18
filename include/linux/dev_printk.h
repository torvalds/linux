// SPDX-License-Identifier: GPL-2.0
/*
 * dev_printk.h - printk messages helpers for devices
 *
 * Copyright (c) 2001-2003 Patrick Mochel <mochel@osdl.org>
 * Copyright (c) 2004-2009 Greg Kroah-Hartman <gregkh@suse.de>
 * Copyright (c) 2008-2009 Novell Inc.
 *
 */

#ifndef _DEVICE_PRINTK_H_
#define _DEVICE_PRINTK_H_

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/ratelimit.h>

#ifndef dev_fmt
#define dev_fmt(fmt) fmt
#endif

struct device;

#define PRINTK_INFO_SUBSYSTEM_LEN	16
#define PRINTK_INFO_DEVICE_LEN		48

struct dev_printk_info {
	char subsystem[PRINTK_INFO_SUBSYSTEM_LEN];
	char device[PRINTK_INFO_DEVICE_LEN];
};

#ifdef CONFIG_PRINTK

__printf(3, 0) __cold
int dev_vprintk_emit(int level, const struct device *dev,
		     const char *fmt, va_list args);
__printf(3, 4) __cold
int dev_printk_emit(int level, const struct device *dev, const char *fmt, ...);

__printf(3, 4) __cold
void _dev_printk(const char *level, const struct device *dev,
		 const char *fmt, ...);
__printf(2, 3) __cold
void _dev_emerg(const struct device *dev, const char *fmt, ...);
__printf(2, 3) __cold
void _dev_alert(const struct device *dev, const char *fmt, ...);
__printf(2, 3) __cold
void _dev_crit(const struct device *dev, const char *fmt, ...);
__printf(2, 3) __cold
void _dev_err(const struct device *dev, const char *fmt, ...);
__printf(2, 3) __cold
void _dev_warn(const struct device *dev, const char *fmt, ...);
__printf(2, 3) __cold
void _dev_notice(const struct device *dev, const char *fmt, ...);
__printf(2, 3) __cold
void _dev_info(const struct device *dev, const char *fmt, ...);

#else

static inline __printf(3, 0)
int dev_vprintk_emit(int level, const struct device *dev,
		     const char *fmt, va_list args)
{ return 0; }
static inline __printf(3, 4)
int dev_printk_emit(int level, const struct device *dev, const char *fmt, ...)
{ return 0; }

static inline void __dev_printk(const char *level, const struct device *dev,
				struct va_format *vaf)
{}
static inline __printf(3, 4)
void _dev_printk(const char *level, const struct device *dev,
		 const char *fmt, ...)
{}

static inline __printf(2, 3)
void _dev_emerg(const struct device *dev, const char *fmt, ...)
{}
static inline __printf(2, 3)
void _dev_crit(const struct device *dev, const char *fmt, ...)
{}
static inline __printf(2, 3)
void _dev_alert(const struct device *dev, const char *fmt, ...)
{}
static inline __printf(2, 3)
void _dev_err(const struct device *dev, const char *fmt, ...)
{}
static inline __printf(2, 3)
void _dev_warn(const struct device *dev, const char *fmt, ...)
{}
static inline __printf(2, 3)
void _dev_notice(const struct device *dev, const char *fmt, ...)
{}
static inline __printf(2, 3)
void _dev_info(const struct device *dev, const char *fmt, ...)
{}

#endif

/*
 * Need to take variadic arguments even though we don't use them, as dev_fmt()
 * may only just have been expanded and may result in multiple arguments.
 */
#define dev_printk_index_emit(level, fmt, ...) \
	printk_index_subsys_emit("%s %s: ", level, fmt)

#define dev_printk_index_wrap(_p_func, level, dev, fmt, ...)		\
	({								\
		dev_printk_index_emit(level, fmt);			\
		_p_func(dev, fmt, ##__VA_ARGS__);			\
	})

/*
 * Some callsites directly call dev_printk rather than going through the
 * dev_<level> infrastructure, so we need to emit here as well as inside those
 * level-specific macros. Only one index entry will be produced, either way,
 * since dev_printk's `fmt` isn't known at compile time if going through the
 * dev_<level> macros.
 *
 * dev_fmt() isn't called for dev_printk when used directly, as it's used by
 * the dev_<level> macros internally which already have dev_fmt() processed.
 *
 * We also can't use dev_printk_index_wrap directly, because we have a separate
 * level to process.
 */
#define dev_printk(level, dev, fmt, ...)				\
	({								\
		dev_printk_index_emit(level, fmt);			\
		_dev_printk(level, dev, fmt, ##__VA_ARGS__);		\
	})

/*
 * Dummy dev_printk for disabled debugging statements to use whilst maintaining
 * gcc's format checking.
 */
#define dev_no_printk(level, dev, fmt, ...)				\
	({								\
		if (0)							\
			_dev_printk(level, dev, fmt, ##__VA_ARGS__);	\
	})

/*
 * #defines for all the dev_<level> macros to prefix with whatever
 * possible use of #define dev_fmt(fmt) ...
 */

#define dev_emerg(dev, fmt, ...) \
	dev_printk_index_wrap(_dev_emerg, KERN_EMERG, dev, dev_fmt(fmt), ##__VA_ARGS__)
#define dev_crit(dev, fmt, ...) \
	dev_printk_index_wrap(_dev_crit, KERN_CRIT, dev, dev_fmt(fmt), ##__VA_ARGS__)
#define dev_alert(dev, fmt, ...) \
	dev_printk_index_wrap(_dev_alert, KERN_ALERT, dev, dev_fmt(fmt), ##__VA_ARGS__)
#define dev_err(dev, fmt, ...) \
	dev_printk_index_wrap(_dev_err, KERN_ERR, dev, dev_fmt(fmt), ##__VA_ARGS__)
#define dev_warn(dev, fmt, ...) \
	dev_printk_index_wrap(_dev_warn, KERN_WARNING, dev, dev_fmt(fmt), ##__VA_ARGS__)
#define dev_notice(dev, fmt, ...) \
	dev_printk_index_wrap(_dev_notice, KERN_NOTICE, dev, dev_fmt(fmt), ##__VA_ARGS__)
#define dev_info(dev, fmt, ...) \
	dev_printk_index_wrap(_dev_info, KERN_INFO, dev, dev_fmt(fmt), ##__VA_ARGS__)

#if defined(CONFIG_DYNAMIC_DEBUG) || \
	(defined(CONFIG_DYNAMIC_DEBUG_CORE) && defined(DYNAMIC_DEBUG_MODULE))
#define dev_dbg(dev, fmt, ...)						\
	dynamic_dev_dbg(dev, dev_fmt(fmt), ##__VA_ARGS__)
#elif defined(DEBUG)
#define dev_dbg(dev, fmt, ...)						\
	dev_printk(KERN_DEBUG, dev, dev_fmt(fmt), ##__VA_ARGS__)
#else
#define dev_dbg(dev, fmt, ...)						\
	dev_no_printk(KERN_DEBUG, dev, dev_fmt(fmt), ##__VA_ARGS__)
#endif

#ifdef CONFIG_PRINTK
#define dev_level_once(dev_level, dev, fmt, ...)			\
do {									\
	static bool __print_once __read_mostly;				\
									\
	if (!__print_once) {						\
		__print_once = true;					\
		dev_level(dev, fmt, ##__VA_ARGS__);			\
	}								\
} while (0)
#else
#define dev_level_once(dev_level, dev, fmt, ...)			\
do {									\
	if (0)								\
		dev_level(dev, fmt, ##__VA_ARGS__);			\
} while (0)
#endif

#define dev_emerg_once(dev, fmt, ...)					\
	dev_level_once(dev_emerg, dev, fmt, ##__VA_ARGS__)
#define dev_alert_once(dev, fmt, ...)					\
	dev_level_once(dev_alert, dev, fmt, ##__VA_ARGS__)
#define dev_crit_once(dev, fmt, ...)					\
	dev_level_once(dev_crit, dev, fmt, ##__VA_ARGS__)
#define dev_err_once(dev, fmt, ...)					\
	dev_level_once(dev_err, dev, fmt, ##__VA_ARGS__)
#define dev_warn_once(dev, fmt, ...)					\
	dev_level_once(dev_warn, dev, fmt, ##__VA_ARGS__)
#define dev_notice_once(dev, fmt, ...)					\
	dev_level_once(dev_notice, dev, fmt, ##__VA_ARGS__)
#define dev_info_once(dev, fmt, ...)					\
	dev_level_once(dev_info, dev, fmt, ##__VA_ARGS__)
#define dev_dbg_once(dev, fmt, ...)					\
	dev_level_once(dev_dbg, dev, fmt, ##__VA_ARGS__)

#define dev_level_ratelimited(dev_level, dev, fmt, ...)			\
do {									\
	static DEFINE_RATELIMIT_STATE(_rs,				\
				      DEFAULT_RATELIMIT_INTERVAL,	\
				      DEFAULT_RATELIMIT_BURST);		\
	if (__ratelimit(&_rs))						\
		dev_level(dev, fmt, ##__VA_ARGS__);			\
} while (0)

#define dev_emerg_ratelimited(dev, fmt, ...)				\
	dev_level_ratelimited(dev_emerg, dev, fmt, ##__VA_ARGS__)
#define dev_alert_ratelimited(dev, fmt, ...)				\
	dev_level_ratelimited(dev_alert, dev, fmt, ##__VA_ARGS__)
#define dev_crit_ratelimited(dev, fmt, ...)				\
	dev_level_ratelimited(dev_crit, dev, fmt, ##__VA_ARGS__)
#define dev_err_ratelimited(dev, fmt, ...)				\
	dev_level_ratelimited(dev_err, dev, fmt, ##__VA_ARGS__)
#define dev_warn_ratelimited(dev, fmt, ...)				\
	dev_level_ratelimited(dev_warn, dev, fmt, ##__VA_ARGS__)
#define dev_notice_ratelimited(dev, fmt, ...)				\
	dev_level_ratelimited(dev_notice, dev, fmt, ##__VA_ARGS__)
#define dev_info_ratelimited(dev, fmt, ...)				\
	dev_level_ratelimited(dev_info, dev, fmt, ##__VA_ARGS__)
#if defined(CONFIG_DYNAMIC_DEBUG) || \
	(defined(CONFIG_DYNAMIC_DEBUG_CORE) && defined(DYNAMIC_DEBUG_MODULE))
/* descriptor check is first to prevent flooding with "callbacks suppressed" */
#define dev_dbg_ratelimited(dev, fmt, ...)				\
do {									\
	static DEFINE_RATELIMIT_STATE(_rs,				\
				      DEFAULT_RATELIMIT_INTERVAL,	\
				      DEFAULT_RATELIMIT_BURST);		\
	DEFINE_DYNAMIC_DEBUG_METADATA(descriptor, fmt);			\
	if (DYNAMIC_DEBUG_BRANCH(descriptor) &&				\
	    __ratelimit(&_rs))						\
		__dynamic_dev_dbg(&descriptor, dev, dev_fmt(fmt),	\
				  ##__VA_ARGS__);			\
} while (0)
#elif defined(DEBUG)
#define dev_dbg_ratelimited(dev, fmt, ...)				\
do {									\
	static DEFINE_RATELIMIT_STATE(_rs,				\
				      DEFAULT_RATELIMIT_INTERVAL,	\
				      DEFAULT_RATELIMIT_BURST);		\
	if (__ratelimit(&_rs))						\
		dev_printk(KERN_DEBUG, dev, dev_fmt(fmt), ##__VA_ARGS__); \
} while (0)
#else
#define dev_dbg_ratelimited(dev, fmt, ...)				\
	dev_no_printk(KERN_DEBUG, dev, dev_fmt(fmt), ##__VA_ARGS__)
#endif

#ifdef VERBOSE_DEBUG
#define dev_vdbg	dev_dbg
#else
#define dev_vdbg(dev, fmt, ...)						\
	dev_no_printk(KERN_DEBUG, dev, dev_fmt(fmt), ##__VA_ARGS__)
#endif

/*
 * dev_WARN*() acts like dev_printk(), but with the key difference of
 * using WARN/WARN_ONCE to include file/line information and a backtrace.
 */
#define dev_WARN(dev, format, arg...) \
	WARN(1, "%s %s: " format, dev_driver_string(dev), dev_name(dev), ## arg)

#define dev_WARN_ONCE(dev, condition, format, arg...) \
	WARN_ONCE(condition, "%s %s: " format, \
			dev_driver_string(dev), dev_name(dev), ## arg)

__printf(3, 4) int dev_err_probe(const struct device *dev, int err, const char *fmt, ...);

#endif /* _DEVICE_PRINTK_H_ */
