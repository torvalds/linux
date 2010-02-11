#ifndef _DYNAMIC_DEBUG_H
#define _DYNAMIC_DEBUG_H

/* dynamic_printk_enabled, and dynamic_printk_enabled2 are bitmasks in which
 * bit n is set to 1 if any modname hashes into the bucket n, 0 otherwise. They
 * use independent hash functions, to reduce the chance of false positives.
 */
extern long long dynamic_debug_enabled;
extern long long dynamic_debug_enabled2;

/*
 * An instance of this structure is created in a special
 * ELF section at every dynamic debug callsite.  At runtime,
 * the special section is treated as an array of these.
 */
struct _ddebug {
	/*
	 * These fields are used to drive the user interface
	 * for selecting and displaying debug callsites.
	 */
	const char *modname;
	const char *function;
	const char *filename;
	const char *format;
	char primary_hash;
	char secondary_hash;
	unsigned int lineno:24;
	/*
 	 * The flags field controls the behaviour at the callsite.
 	 * The bits here are changed dynamically when the user
 	 * writes commands to <debugfs>/dynamic_debug/ddebug
	 */
#define _DPRINTK_FLAGS_PRINT   (1<<0)  /* printk() a message using the format */
#define _DPRINTK_FLAGS_DEFAULT 0
	unsigned int flags:8;
} __attribute__((aligned(8)));


int ddebug_add_module(struct _ddebug *tab, unsigned int n,
				const char *modname);

#if defined(CONFIG_DYNAMIC_DEBUG)
extern int ddebug_remove_module(char *mod_name);

#define __dynamic_dbg_enabled(dd)  ({	     \
	int __ret = 0;							     \
	if (unlikely((dynamic_debug_enabled & (1LL << DEBUG_HASH)) &&	     \
			(dynamic_debug_enabled2 & (1LL << DEBUG_HASH2))))   \
				if (unlikely(dd.flags))			     \
					__ret = 1;			     \
	__ret; })

#define dynamic_pr_debug(fmt, ...) do {					\
	static struct _ddebug descriptor				\
	__used								\
	__attribute__((section("__verbose"), aligned(8))) =		\
	{ KBUILD_MODNAME, __func__, __FILE__, fmt, DEBUG_HASH,	\
		DEBUG_HASH2, __LINE__, _DPRINTK_FLAGS_DEFAULT };	\
	if (__dynamic_dbg_enabled(descriptor))				\
		printk(KERN_DEBUG pr_fmt(fmt),	##__VA_ARGS__);		\
	} while (0)


#define dynamic_dev_dbg(dev, fmt, ...) do {				\
	static struct _ddebug descriptor				\
	__used								\
	__attribute__((section("__verbose"), aligned(8))) =		\
	{ KBUILD_MODNAME, __func__, __FILE__, fmt, DEBUG_HASH,	\
		DEBUG_HASH2, __LINE__, _DPRINTK_FLAGS_DEFAULT };	\
	if (__dynamic_dbg_enabled(descriptor))				\
		dev_printk(KERN_DEBUG, dev, fmt, ##__VA_ARGS__);	\
	} while (0)

#else

static inline int ddebug_remove_module(char *mod)
{
	return 0;
}

#define dynamic_pr_debug(fmt, ...)					\
	do { if (0) printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__); } while (0)
#define dynamic_dev_dbg(dev, format, ...)				\
	do { if (0) dev_printk(KERN_DEBUG, dev, fmt, ##__VA_ARGS__); } while (0)
#endif

#endif
