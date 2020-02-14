/*
 * Copyright (C) 2016 Red Hat
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 */

#ifndef DRM_PRINT_H_
#define DRM_PRINT_H_

#include <linux/compiler.h>
#include <linux/printk.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/debugfs.h>

#include <drm/drm.h>

/* Do *not* use outside of drm_print.[ch]! */
extern unsigned int __drm_debug;

/**
 * DOC: print
 *
 * A simple wrapper for dev_printk(), seq_printf(), etc.  Allows same
 * debug code to be used for both debugfs and printk logging.
 *
 * For example::
 *
 *     void log_some_info(struct drm_printer *p)
 *     {
 *             drm_printf(p, "foo=%d\n", foo);
 *             drm_printf(p, "bar=%d\n", bar);
 *     }
 *
 *     #ifdef CONFIG_DEBUG_FS
 *     void debugfs_show(struct seq_file *f)
 *     {
 *             struct drm_printer p = drm_seq_file_printer(f);
 *             log_some_info(&p);
 *     }
 *     #endif
 *
 *     void some_other_function(...)
 *     {
 *             struct drm_printer p = drm_info_printer(drm->dev);
 *             log_some_info(&p);
 *     }
 */

/**
 * struct drm_printer - drm output "stream"
 *
 * Do not use struct members directly.  Use drm_printer_seq_file(),
 * drm_printer_info(), etc to initialize.  And drm_printf() for output.
 */
struct drm_printer {
	/* private: */
	void (*printfn)(struct drm_printer *p, struct va_format *vaf);
	void (*puts)(struct drm_printer *p, const char *str);
	void *arg;
	const char *prefix;
};

void __drm_printfn_coredump(struct drm_printer *p, struct va_format *vaf);
void __drm_puts_coredump(struct drm_printer *p, const char *str);
void __drm_printfn_seq_file(struct drm_printer *p, struct va_format *vaf);
void __drm_puts_seq_file(struct drm_printer *p, const char *str);
void __drm_printfn_info(struct drm_printer *p, struct va_format *vaf);
void __drm_printfn_debug(struct drm_printer *p, struct va_format *vaf);
void __drm_printfn_err(struct drm_printer *p, struct va_format *vaf);

__printf(2, 3)
void drm_printf(struct drm_printer *p, const char *f, ...);
void drm_puts(struct drm_printer *p, const char *str);
void drm_print_regset32(struct drm_printer *p, struct debugfs_regset32 *regset);
void drm_print_bits(struct drm_printer *p, unsigned long value,
		    const char * const bits[], unsigned int nbits);

__printf(2, 0)
/**
 * drm_vprintf - print to a &drm_printer stream
 * @p: the &drm_printer
 * @fmt: format string
 * @va: the va_list
 */
static inline void
drm_vprintf(struct drm_printer *p, const char *fmt, va_list *va)
{
	struct va_format vaf = { .fmt = fmt, .va = va };

	p->printfn(p, &vaf);
}

/**
 * drm_printf_indent - Print to a &drm_printer stream with indentation
 * @printer: DRM printer
 * @indent: Tab indentation level (max 5)
 * @fmt: Format string
 */
#define drm_printf_indent(printer, indent, fmt, ...) \
	drm_printf((printer), "%.*s" fmt, (indent), "\t\t\t\t\tX", ##__VA_ARGS__)

/**
 * struct drm_print_iterator - local struct used with drm_printer_coredump
 * @data: Pointer to the devcoredump output buffer
 * @start: The offset within the buffer to start writing
 * @remain: The number of bytes to write for this iteration
 */
struct drm_print_iterator {
	void *data;
	ssize_t start;
	ssize_t remain;
	/* private: */
	ssize_t offset;
};

/**
 * drm_coredump_printer - construct a &drm_printer that can output to a buffer
 * from the read function for devcoredump
 * @iter: A pointer to a struct drm_print_iterator for the read instance
 *
 * This wrapper extends drm_printf() to work with a dev_coredumpm() callback
 * function. The passed in drm_print_iterator struct contains the buffer
 * pointer, size and offset as passed in from devcoredump.
 *
 * For example::
 *
 *	void coredump_read(char *buffer, loff_t offset, size_t count,
 *		void *data, size_t datalen)
 *	{
 *		struct drm_print_iterator iter;
 *		struct drm_printer p;
 *
 *		iter.data = buffer;
 *		iter.start = offset;
 *		iter.remain = count;
 *
 *		p = drm_coredump_printer(&iter);
 *
 *		drm_printf(p, "foo=%d\n", foo);
 *	}
 *
 *	void makecoredump(...)
 *	{
 *		...
 *		dev_coredumpm(dev, THIS_MODULE, data, 0, GFP_KERNEL,
 *			coredump_read, ...)
 *	}
 *
 * RETURNS:
 * The &drm_printer object
 */
static inline struct drm_printer
drm_coredump_printer(struct drm_print_iterator *iter)
{
	struct drm_printer p = {
		.printfn = __drm_printfn_coredump,
		.puts = __drm_puts_coredump,
		.arg = iter,
	};

	/* Set the internal offset of the iterator to zero */
	iter->offset = 0;

	return p;
}

/**
 * drm_seq_file_printer - construct a &drm_printer that outputs to &seq_file
 * @f:  the &struct seq_file to output to
 *
 * RETURNS:
 * The &drm_printer object
 */
static inline struct drm_printer drm_seq_file_printer(struct seq_file *f)
{
	struct drm_printer p = {
		.printfn = __drm_printfn_seq_file,
		.puts = __drm_puts_seq_file,
		.arg = f,
	};
	return p;
}

/**
 * drm_info_printer - construct a &drm_printer that outputs to dev_printk()
 * @dev: the &struct device pointer
 *
 * RETURNS:
 * The &drm_printer object
 */
static inline struct drm_printer drm_info_printer(struct device *dev)
{
	struct drm_printer p = {
		.printfn = __drm_printfn_info,
		.arg = dev,
	};
	return p;
}

/**
 * drm_debug_printer - construct a &drm_printer that outputs to pr_debug()
 * @prefix: debug output prefix
 *
 * RETURNS:
 * The &drm_printer object
 */
static inline struct drm_printer drm_debug_printer(const char *prefix)
{
	struct drm_printer p = {
		.printfn = __drm_printfn_debug,
		.prefix = prefix
	};
	return p;
}

/**
 * drm_err_printer - construct a &drm_printer that outputs to pr_err()
 * @prefix: debug output prefix
 *
 * RETURNS:
 * The &drm_printer object
 */
static inline struct drm_printer drm_err_printer(const char *prefix)
{
	struct drm_printer p = {
		.printfn = __drm_printfn_err,
		.prefix = prefix
	};
	return p;
}

/**
 * enum drm_debug_category - The DRM debug categories
 *
 * Each of the DRM debug logging macros use a specific category, and the logging
 * is filtered by the drm.debug module parameter. This enum specifies the values
 * for the interface.
 *
 * Each DRM_DEBUG_<CATEGORY> macro logs to DRM_UT_<CATEGORY> category, except
 * DRM_DEBUG() logs to DRM_UT_CORE.
 *
 * Enabling verbose debug messages is done through the drm.debug parameter, each
 * category being enabled by a bit:
 *
 *  - drm.debug=0x1 will enable CORE messages
 *  - drm.debug=0x2 will enable DRIVER messages
 *  - drm.debug=0x3 will enable CORE and DRIVER messages
 *  - ...
 *  - drm.debug=0x1ff will enable all messages
 *
 * An interesting feature is that it's possible to enable verbose logging at
 * run-time by echoing the debug value in its sysfs node::
 *
 *   # echo 0xf > /sys/module/drm/parameters/debug
 *
 */
enum drm_debug_category {
	/**
	 * @DRM_UT_CORE: Used in the generic drm code: drm_ioctl.c, drm_mm.c,
	 * drm_memory.c, ...
	 */
	DRM_UT_CORE		= 0x01,
	/**
	 * @DRM_UT_DRIVER: Used in the vendor specific part of the driver: i915,
	 * radeon, ... macro.
	 */
	DRM_UT_DRIVER		= 0x02,
	/**
	 * @DRM_UT_KMS: Used in the modesetting code.
	 */
	DRM_UT_KMS		= 0x04,
	/**
	 * @DRM_UT_PRIME: Used in the prime code.
	 */
	DRM_UT_PRIME		= 0x08,
	/**
	 * @DRM_UT_ATOMIC: Used in the atomic code.
	 */
	DRM_UT_ATOMIC		= 0x10,
	/**
	 * @DRM_UT_VBL: Used for verbose debug message in the vblank code.
	 */
	DRM_UT_VBL		= 0x20,
	/**
	 * @DRM_UT_STATE: Used for verbose atomic state debugging.
	 */
	DRM_UT_STATE		= 0x40,
	/**
	 * @DRM_UT_LEASE: Used in the lease code.
	 */
	DRM_UT_LEASE		= 0x80,
	/**
	 * @DRM_UT_DP: Used in the DP code.
	 */
	DRM_UT_DP		= 0x100,
};

static inline bool drm_debug_enabled(enum drm_debug_category category)
{
	return unlikely(__drm_debug & category);
}

/*
 * struct device based logging
 *
 * Prefer drm_device based logging over device or prink based logging.
 */

__printf(3, 4)
void drm_dev_printk(const struct device *dev, const char *level,
		    const char *format, ...);
__printf(3, 4)
void drm_dev_dbg(const struct device *dev, enum drm_debug_category category,
		 const char *format, ...);

/**
 * Error output.
 *
 * @dev: device pointer
 * @fmt: printf() like format string.
 */
#define DRM_DEV_ERROR(dev, fmt, ...)					\
	drm_dev_printk(dev, KERN_ERR, "*ERROR* " fmt, ##__VA_ARGS__)

/**
 * Rate limited error output.  Like DRM_ERROR() but won't flood the log.
 *
 * @dev: device pointer
 * @fmt: printf() like format string.
 */
#define DRM_DEV_ERROR_RATELIMITED(dev, fmt, ...)			\
({									\
	static DEFINE_RATELIMIT_STATE(_rs,				\
				      DEFAULT_RATELIMIT_INTERVAL,	\
				      DEFAULT_RATELIMIT_BURST);		\
									\
	if (__ratelimit(&_rs))						\
		DRM_DEV_ERROR(dev, fmt, ##__VA_ARGS__);			\
})

#define DRM_DEV_INFO(dev, fmt, ...)				\
	drm_dev_printk(dev, KERN_INFO, fmt, ##__VA_ARGS__)

#define DRM_DEV_INFO_ONCE(dev, fmt, ...)				\
({									\
	static bool __print_once __read_mostly;				\
	if (!__print_once) {						\
		__print_once = true;					\
		DRM_DEV_INFO(dev, fmt, ##__VA_ARGS__);			\
	}								\
})

/**
 * Debug output.
 *
 * @dev: device pointer
 * @fmt: printf() like format string.
 */
#define DRM_DEV_DEBUG(dev, fmt, ...)					\
	drm_dev_dbg(dev, DRM_UT_CORE, fmt, ##__VA_ARGS__)
#define DRM_DEV_DEBUG_DRIVER(dev, fmt, ...)				\
	drm_dev_dbg(dev, DRM_UT_DRIVER,	fmt, ##__VA_ARGS__)
#define DRM_DEV_DEBUG_KMS(dev, fmt, ...)				\
	drm_dev_dbg(dev, DRM_UT_KMS, fmt, ##__VA_ARGS__)

/*
 * struct drm_device based logging
 *
 * Prefer drm_device based logging over device or prink based logging.
 */

/* Helper for struct drm_device based logging. */
#define __drm_printk(drm, level, type, fmt, ...)			\
	dev_##level##type((drm)->dev, "[drm] " fmt, ##__VA_ARGS__)


#define drm_info(drm, fmt, ...)					\
	__drm_printk((drm), info,, fmt, ##__VA_ARGS__)

#define drm_notice(drm, fmt, ...)				\
	__drm_printk((drm), notice,, fmt, ##__VA_ARGS__)

#define drm_warn(drm, fmt, ...)					\
	__drm_printk((drm), warn,, fmt, ##__VA_ARGS__)

#define drm_err(drm, fmt, ...)					\
	__drm_printk((drm), err,, "*ERROR* " fmt, ##__VA_ARGS__)


#define drm_info_once(drm, fmt, ...)				\
	__drm_printk((drm), info, _once, fmt, ##__VA_ARGS__)

#define drm_notice_once(drm, fmt, ...)				\
	__drm_printk((drm), notice, _once, fmt, ##__VA_ARGS__)

#define drm_warn_once(drm, fmt, ...)				\
	__drm_printk((drm), warn, _once, fmt, ##__VA_ARGS__)

#define drm_err_once(drm, fmt, ...)				\
	__drm_printk((drm), err, _once, "*ERROR* " fmt, ##__VA_ARGS__)


#define drm_err_ratelimited(drm, fmt, ...)				\
	__drm_printk((drm), err, _ratelimited, "*ERROR* " fmt, ##__VA_ARGS__)


#define drm_dbg_core(drm, fmt, ...)					\
	drm_dev_dbg((drm)->dev, DRM_UT_CORE, fmt, ##__VA_ARGS__)
#define drm_dbg(drm, fmt, ...)						\
	drm_dev_dbg((drm)->dev, DRM_UT_DRIVER, fmt, ##__VA_ARGS__)
#define drm_dbg_kms(drm, fmt, ...)					\
	drm_dev_dbg((drm)->dev, DRM_UT_KMS, fmt, ##__VA_ARGS__)
#define drm_dbg_prime(drm, fmt, ...)					\
	drm_dev_dbg((drm)->dev, DRM_UT_PRIME, fmt, ##__VA_ARGS__)
#define drm_dbg_atomic(drm, fmt, ...)					\
	drm_dev_dbg((drm)->dev, DRM_UT_ATOMIC, fmt, ##__VA_ARGS__)
#define drm_dbg_vbl(drm, fmt, ...)					\
	drm_dev_dbg((drm)->dev, DRM_UT_VBL, fmt, ##__VA_ARGS__)
#define drm_dbg_state(drm, fmt, ...)					\
	drm_dev_dbg((drm)->dev, DRM_UT_STATE, fmt, ##__VA_ARGS__)
#define drm_dbg_lease(drm, fmt, ...)					\
	drm_dev_dbg((drm)->dev, DRM_UT_LEASE, fmt, ##__VA_ARGS__)
#define drm_dbg_dp(drm, fmt, ...)					\
	drm_dev_dbg((drm)->dev, DRM_UT_DP, fmt, ##__VA_ARGS__)


/*
 * printk based logging
 *
 * Prefer drm_device based logging over device or prink based logging.
 */

__printf(2, 3)
void __drm_dbg(enum drm_debug_category category, const char *format, ...);
__printf(1, 2)
void __drm_err(const char *format, ...);

/* Macros to make printk easier */

#define _DRM_PRINTK(once, level, fmt, ...)				\
	printk##once(KERN_##level "[" DRM_NAME "] " fmt, ##__VA_ARGS__)

#define DRM_INFO(fmt, ...)						\
	_DRM_PRINTK(, INFO, fmt, ##__VA_ARGS__)
#define DRM_NOTE(fmt, ...)						\
	_DRM_PRINTK(, NOTICE, fmt, ##__VA_ARGS__)
#define DRM_WARN(fmt, ...)						\
	_DRM_PRINTK(, WARNING, fmt, ##__VA_ARGS__)

#define DRM_INFO_ONCE(fmt, ...)						\
	_DRM_PRINTK(_once, INFO, fmt, ##__VA_ARGS__)
#define DRM_NOTE_ONCE(fmt, ...)						\
	_DRM_PRINTK(_once, NOTICE, fmt, ##__VA_ARGS__)
#define DRM_WARN_ONCE(fmt, ...)						\
	_DRM_PRINTK(_once, WARNING, fmt, ##__VA_ARGS__)

#define DRM_ERROR(fmt, ...)						\
	__drm_err(fmt, ##__VA_ARGS__)

#define DRM_ERROR_RATELIMITED(fmt, ...)					\
	DRM_DEV_ERROR_RATELIMITED(NULL, fmt, ##__VA_ARGS__)

#define DRM_DEBUG(fmt, ...)						\
	__drm_dbg(DRM_UT_CORE, fmt, ##__VA_ARGS__)

#define DRM_DEBUG_DRIVER(fmt, ...)					\
	__drm_dbg(DRM_UT_DRIVER, fmt, ##__VA_ARGS__)

#define DRM_DEBUG_KMS(fmt, ...)						\
	__drm_dbg(DRM_UT_KMS, fmt, ##__VA_ARGS__)

#define DRM_DEBUG_PRIME(fmt, ...)					\
	__drm_dbg(DRM_UT_PRIME, fmt, ##__VA_ARGS__)

#define DRM_DEBUG_ATOMIC(fmt, ...)					\
	__drm_dbg(DRM_UT_ATOMIC, fmt, ##__VA_ARGS__)

#define DRM_DEBUG_VBL(fmt, ...)						\
	__drm_dbg(DRM_UT_VBL, fmt, ##__VA_ARGS__)

#define DRM_DEBUG_LEASE(fmt, ...)					\
	__drm_dbg(DRM_UT_LEASE, fmt, ##__VA_ARGS__)

#define DRM_DEBUG_DP(fmt, ...)						\
	__drm_dbg(DRM_UT_DP, fmt, ## __VA_ARGS__)


#define DRM_DEBUG_KMS_RATELIMITED(fmt, ...)				\
({									\
	static DEFINE_RATELIMIT_STATE(_rs,				\
				      DEFAULT_RATELIMIT_INTERVAL,       \
				      DEFAULT_RATELIMIT_BURST);         \
	if (__ratelimit(&_rs))						\
		drm_dev_dbg(NULL, DRM_UT_KMS, fmt, ##__VA_ARGS__);	\
})

/*
 * struct drm_device based WARNs
 *
 * drm_WARN*() acts like WARN*(), but with the key difference of
 * using device specific information so that we know from which device
 * warning is originating from.
 *
 * Prefer drm_device based drm_WARN* over regular WARN*
 */

/* Helper for struct drm_device based WARNs */
#define drm_WARN(drm, condition, format, arg...)			\
	WARN(condition, "%s %s: " format,				\
			dev_driver_string((drm)->dev),			\
			dev_name((drm)->dev), ## arg)

#define drm_WARN_ONCE(drm, condition, format, arg...)			\
	WARN_ONCE(condition, "%s %s: " format,				\
			dev_driver_string((drm)->dev),			\
			dev_name((drm)->dev), ## arg)

#define drm_WARN_ON(drm, x)						\
	drm_WARN((drm), (x), "%s",					\
		 "drm_WARN_ON(" __stringify(x) ")")

#define drm_WARN_ON_ONCE(drm, x)					\
	drm_WARN_ONCE((drm), (x), "%s",					\
		      "drm_WARN_ON_ONCE(" __stringify(x) ")")

#endif /* DRM_PRINT_H_ */
