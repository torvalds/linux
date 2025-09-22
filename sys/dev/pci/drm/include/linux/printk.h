/* Public domain. */

#ifndef _LINUX_PRINTK_H
#define _LINUX_PRINTK_H

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/stdarg.h>

#include <linux/init.h>

#define KERN_CRIT	"\0012"
#define KERN_ERR	"\0013"
#define KERN_WARNING	"\0014"
#define KERN_NOTICE	"\0015"
#define KERN_INFO	"\0016"
#define KERN_DEBUG	"\0017"

#define KERN_CONT	"\001c"

#define HW_ERR		"HW_ERR: "

#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif

#define printk_once(fmt, arg...) ({		\
	static int __warned;			\
	if (!__warned) {			\
		printk(fmt, ## arg);		\
		__warned = 1;			\
	}					\
})

#define printk_ratelimit()	1

int printk(const char *fmt, ...);

#define pr_warn(fmt, arg...)	printk(KERN_WARNING pr_fmt(fmt), ## arg)
#define pr_warn_ratelimited(fmt, arg...)	printk(KERN_WARNING pr_fmt(fmt), ## arg)
#define pr_warn_once(fmt, arg...)	printk_once(KERN_WARNING pr_fmt(fmt), ## arg)
#define pr_notice(fmt, arg...)	printk(KERN_NOTICE pr_fmt(fmt), ## arg)
#define pr_crit(fmt, arg...)	printk(KERN_CRIT pr_fmt(fmt), ## arg)
#define pr_err(fmt, arg...)	printk(KERN_ERR pr_fmt(fmt), ## arg)
#define pr_err_once(fmt, arg...)	printk_once(KERN_ERR pr_fmt(fmt), ## arg)
#define pr_cont(fmt, arg...)	printk(KERN_CONT pr_fmt(fmt), ## arg)

#ifdef DRMDEBUG
#define pr_info(fmt, arg...)	printk(KERN_INFO pr_fmt(fmt), ## arg)
#define pr_info_ratelimited(fmt, arg...)	printk(KERN_INFO pr_fmt(fmt), ## arg)
#define pr_info_once(fmt, arg...)	printk_once(KERN_INFO pr_fmt(fmt), ## arg)
#define pr_debug(fmt, arg...)	printk(KERN_DEBUG pr_fmt(fmt), ## arg)
#else
#define pr_info(fmt, arg...)	do { } while(0)
#define pr_info_ratelimited(fmt, arg...)	do { } while(0)
#define pr_info_once(fmt, arg...)	do { } while(0)
#define pr_debug(fmt, arg...)	do { } while(0)
#endif

enum {
	DUMP_PREFIX_NONE,
	DUMP_PREFIX_ADDRESS,
	DUMP_PREFIX_OFFSET
};

void print_hex_dump(const char *, const char *, int, int, int,
	 const void *, size_t, bool);

struct va_format {
	const char *fmt;
	va_list *va;
};

static inline int
_in_dbg_master(void)
{
#ifdef DDB
	return (db_active);
#endif
	return (0);
}

#define oops_in_progress _in_dbg_master()

#endif
