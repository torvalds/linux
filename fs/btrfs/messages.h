/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_MESSAGES_H
#define BTRFS_MESSAGES_H

#include <linux/types.h>
#include <linux/printk.h>
#include <linux/bug.h>

struct btrfs_fs_info;

/*
 * We want to be able to override this in btrfs-progs.
 */
#ifdef __KERNEL__

static inline __printf(2, 3) __cold
void btrfs_no_printk(const struct btrfs_fs_info *fs_info, const char *fmt, ...)
{
}

#endif

#ifdef CONFIG_PRINTK

__printf(3, 4) __cold
void _btrfs_printk(const struct btrfs_fs_info *fs_info, unsigned int level, const char *fmt, ...);

#else

#define btrfs_printk_in_rcu(fs_info, level, fmt, args...)		\
	btrfs_no_printk(fs_info, fmt, ##args)

#define btrfs_printk_in_rcu(fs_info, level, fmt, args...)		\
	btrfs_no_printk(fs_info, fmt, ##args)

#define btrfs_printk_rl_in_rcu(fs_info, level, fmt, args...)		\
	btrfs_no_printk(fs_info, fmt, ##args)

#endif

/*
 * Print a message with filesystem info, enclosed in RCU protection.
 */
#define btrfs_crit(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, LOGLEVEL_CRIT, fmt, ##args)
#define btrfs_err(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, LOGLEVEL_ERR, fmt, ##args)
#define btrfs_warn(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, LOGLEVEL_WARNING, fmt, ##args)
#define btrfs_info(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, LOGLEVEL_INFO, fmt, ##args)

/*
 * Wrappers that use a ratelimited printk
 */
#define btrfs_crit_rl(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, LOGLEVEL_CRIT, fmt, ##args)
#define btrfs_err_rl(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, LOGLEVEL_ERR, fmt, ##args)
#define btrfs_warn_rl(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, LOGLEVEL_WARNING, fmt, ##args)
#define btrfs_info_rl(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, LOGLEVEL_INFO, fmt, ##args)

#if defined(CONFIG_DYNAMIC_DEBUG)
#define btrfs_debug(fs_info, fmt, args...)				\
	_dynamic_func_call_no_desc(fmt, btrfs_printk_in_rcu,		\
				   fs_info, LOGLEVEL_DEBUG, fmt, ##args)
#define btrfs_debug_rl(fs_info, fmt, args...)				\
	_dynamic_func_call_no_desc(fmt, btrfs_printk_rl_in_rcu,		\
				   fs_info, LOGLEVEL_DEBUG, fmt, ##args)
#elif defined(DEBUG)
#define btrfs_debug(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, LOGLEVEL_DEBUG, fmt, ##args)
#define btrfs_debug_rl(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, LOGLEVEl_DEBUG, fmt, ##args)
#else
/* When printk() is no_printk(), expand to no-op. */
#define btrfs_debug(fs_info, fmt, args...)	do { (void)(fs_info); } while(0)
#define btrfs_debug_rl(fs_info, fmt, args...)	do { (void)(fs_info); } while(0)
#endif

#ifdef CONFIG_PRINTK

#define btrfs_printk_in_rcu(fs_info, level, fmt, args...)	\
do {								\
	rcu_read_lock();					\
	_btrfs_printk(fs_info, level, fmt, ##args);		\
	rcu_read_unlock();					\
} while (0)

#define btrfs_printk_rl_in_rcu(fs_info, level, fmt, args...)	\
do {								\
	static DEFINE_RATELIMIT_STATE(_rs,			\
		DEFAULT_RATELIMIT_INTERVAL,			\
		DEFAULT_RATELIMIT_BURST);			\
								\
	rcu_read_lock();					\
	if (__ratelimit(&_rs))					\
		_btrfs_printk(fs_info, level, fmt, ##args);	\
	rcu_read_unlock();					\
} while (0)

#endif

#ifdef CONFIG_BTRFS_ASSERT

__printf(1, 2)
static inline void verify_assert_printk_format(const char *fmt, ...) {
	/* Stub to verify the assertion format string. */
}

/* Take the first token if any. */
#define __FIRST_ARG(_, ...) _
/*
 * Skip the first token and return the rest, if it's empty the comma is dropped.
 * As ##__VA_ARGS__ cannot be at the beginning of the macro the __VA_OPT__ is needed
 * and supported since GCC 8 and Clang 12.
 */
#define __REST_ARGS(_, ... ) __VA_OPT__(,) __VA_ARGS__

/*
 * Assertion with optional printk() format.
 *
 * Accepted syntax:
 * ASSERT(condition);
 * ASSERT(condition, "string");
 * ASSERT(condition, "variable=%d", variable);
 *
 * How it works:
 * - if there's no format string, ""[0] evaluates at compile time to 0 and the
 *   true branch is executed
 * - any non-empty format string with the "" prefix evaluates to != 0 at
 *   compile time and the false branch is executed
 * - stringified condition is printed as %s so we don't accidentally mix format
 *   strings (the % operator)
 * - there can be only one printk() call, so the format strings and arguments are
 *   spliced together:
 *   DEFAULT_FMT [USER_FMT], DEFAULT_ARGS [, USER_ARGS]
 * - comma between DEFAULT_ARGS and USER_ARGS is handled by preprocessor
 *   (requires __VA_OPT__ support)
 * - otherwise we could use __VA_OPT(,) __VA_ARGS__ for the 2nd+ argument of args,
 */
#define ASSERT(cond, args...)							\
do {										\
	verify_assert_printk_format("check the format string" args);		\
	if (!likely(cond)) {							\
		if (("" __FIRST_ARG(args) [0]) == 0) {				\
			pr_err("assertion failed: %s :: %ld, in %s:%d\n",	\
				#cond, (long)(cond), __FILE__, __LINE__);	\
		} else {							\
			pr_err("assertion failed: %s :: %ld, in %s:%d (" __FIRST_ARG(args) ")\n", \
				#cond, (long)(cond), __FILE__, __LINE__ __REST_ARGS(args)); \
		}								\
		BUG();								\
	}									\
} while(0)

#else
/* Compile check the @cond expression but don't generate any code. */
#define ASSERT(cond, args...)			BUILD_BUG_ON_INVALID(cond)
#endif

#ifdef CONFIG_BTRFS_DEBUG
/* Verbose warning only under debug build. */
#define DEBUG_WARN(args...)			WARN(1, KERN_ERR args)
#else
#define DEBUG_WARN(...)				do {} while(0)
#endif

__printf(5, 6)
__cold
void __btrfs_handle_fs_error(struct btrfs_fs_info *fs_info, const char *function,
		     unsigned int line, int error, const char *fmt, ...);

const char * __attribute_const__ btrfs_decode_error(int error);

#define btrfs_handle_fs_error(fs_info, error, fmt, args...)		\
	__btrfs_handle_fs_error((fs_info), __func__, __LINE__,		\
				(error), fmt, ##args)

__printf(5, 6)
__cold
void __btrfs_panic(const struct btrfs_fs_info *fs_info, const char *function,
		   unsigned int line, int error, const char *fmt, ...);
/*
 * If BTRFS_MOUNT_PANIC_ON_FATAL_ERROR is in mount_opt, __btrfs_panic
 * will panic().  Otherwise we BUG() here.
 */
#define btrfs_panic(fs_info, error, fmt, args...)			\
do {									\
	__btrfs_panic(fs_info, __func__, __LINE__, error, fmt, ##args);	\
	BUG();								\
} while (0)

#if BITS_PER_LONG == 32
#define BTRFS_32BIT_MAX_FILE_SIZE (((u64)ULONG_MAX + 1) << PAGE_SHIFT)
/*
 * The warning threshold is 5/8th of the MAX_LFS_FILESIZE that limits the logical
 * addresses of extents.
 *
 * For 4K page size it's about 10T, for 64K it's 160T.
 */
#define BTRFS_32BIT_EARLY_WARN_THRESHOLD (BTRFS_32BIT_MAX_FILE_SIZE * 5 / 8)
void btrfs_warn_32bit_limit(struct btrfs_fs_info *fs_info);
void btrfs_err_32bit_limit(struct btrfs_fs_info *fs_info);
#endif

#endif
