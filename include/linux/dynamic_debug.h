/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DYNAMIC_DE_H
#define _DYNAMIC_DE_H

#if defined(CONFIG_JUMP_LABEL)
#include <linux/jump_label.h>
#endif

/*
 * An instance of this structure is created in a special
 * ELF section at every dynamic de callsite.  At runtime,
 * the special section is treated as an array of these.
 */
struct _dde {
	/*
	 * These fields are used to drive the user interface
	 * for selecting and displaying de callsites.
	 */
	const char *modname;
	const char *function;
	const char *filename;
	const char *format;
	unsigned int lineno:18;
	/*
	 * The flags field controls the behaviour at the callsite.
	 * The bits here are changed dynamically when the user
	 * writes commands to <defs>/dynamic_de/control
	 */
#define _DPRINTK_FLAGS_NONE	0
#define _DPRINTK_FLAGS_PRINT	(1<<0) /* printk() a message using the format */
#define _DPRINTK_FLAGS_INCL_MODNAME	(1<<1)
#define _DPRINTK_FLAGS_INCL_FUNCNAME	(1<<2)
#define _DPRINTK_FLAGS_INCL_LINENO	(1<<3)
#define _DPRINTK_FLAGS_INCL_TID		(1<<4)
#if defined DE
#define _DPRINTK_FLAGS_DEFAULT _DPRINTK_FLAGS_PRINT
#else
#define _DPRINTK_FLAGS_DEFAULT 0
#endif
	unsigned int flags:8;
#ifdef CONFIG_JUMP_LABEL
	union {
		struct static_key_true dd_key_true;
		struct static_key_false dd_key_false;
	} key;
#endif
} __attribute__((aligned(8)));



#if defined(CONFIG_DYNAMIC_DE)
int dde_add_module(struct _dde *tab, unsigned int n,
				const char *modname);
extern int dde_remove_module(const char *mod_name);
extern __printf(2, 3)
void __dynamic_pr_de(struct _dde *descriptor, const char *fmt, ...);

extern int dde_dyndbg_module_param_cb(char *param, char *val,
					const char *modname);

struct device;

extern __printf(3, 4)
void __dynamic_dev_dbg(struct _dde *descriptor, const struct device *dev,
		       const char *fmt, ...);

struct net_device;

extern __printf(3, 4)
void __dynamic_netdev_dbg(struct _dde *descriptor,
			  const struct net_device *dev,
			  const char *fmt, ...);

#define DEFINE_DYNAMIC_DE_METADATA(name, fmt)		\
	static struct _dde  __aligned(8)			\
	__attribute__((section("__verbose"))) name = {		\
		.modname = KBUILD_MODNAME,			\
		.function = __func__,				\
		.filename = __FILE__,				\
		.format = (fmt),				\
		.lineno = __LINE__,				\
		.flags = _DPRINTK_FLAGS_DEFAULT,		\
		_DPRINTK_KEY_INIT				\
	}

#ifdef CONFIG_JUMP_LABEL

#ifdef DE

#define _DPRINTK_KEY_INIT .key.dd_key_true = (STATIC_KEY_TRUE_INIT)

#define DYNAMIC_DE_BRANCH(descriptor) \
	static_branch_likely(&descriptor.key.dd_key_true)
#else
#define _DPRINTK_KEY_INIT .key.dd_key_false = (STATIC_KEY_FALSE_INIT)

#define DYNAMIC_DE_BRANCH(descriptor) \
	static_branch_unlikely(&descriptor.key.dd_key_false)
#endif

#else /* !HAVE_JUMP_LABEL */

#define _DPRINTK_KEY_INIT

#ifdef DE
#define DYNAMIC_DE_BRANCH(descriptor) \
	likely(descriptor.flags & _DPRINTK_FLAGS_PRINT)
#else
#define DYNAMIC_DE_BRANCH(descriptor) \
	unlikely(descriptor.flags & _DPRINTK_FLAGS_PRINT)
#endif

#endif

#define __dynamic_func_call(id, fmt, func, ...) do {	\
	DEFINE_DYNAMIC_DE_METADATA(id, fmt);		\
	if (DYNAMIC_DE_BRANCH(id))			\
		func(&id, ##__VA_ARGS__);		\
} while (0)

#define __dynamic_func_call_no_desc(id, fmt, func, ...) do {	\
	DEFINE_DYNAMIC_DE_METADATA(id, fmt);			\
	if (DYNAMIC_DE_BRANCH(id))				\
		func(__VA_ARGS__);				\
} while (0)

/*
 * "Factory macro" for generating a call to func, guarded by a
 * DYNAMIC_DE_BRANCH. The dynamic de decriptor will be
 * initialized using the fmt argument. The function will be called with
 * the address of the descriptor as first argument, followed by all
 * the varargs. Note that fmt is repeated in invocations of this
 * macro.
 */
#define _dynamic_func_call(fmt, func, ...)				\
	__dynamic_func_call(__UNIQUE_ID(dde), fmt, func, ##__VA_ARGS__)
/*
 * A variant that does the same, except that the descriptor is not
 * passed as the first argument to the function; it is only called
 * with precisely the macro's varargs.
 */
#define _dynamic_func_call_no_desc(fmt, func, ...)	\
	__dynamic_func_call_no_desc(__UNIQUE_ID(dde), fmt, func, ##__VA_ARGS__)

#define dynamic_pr_de(fmt, ...)				\
	_dynamic_func_call(fmt,	__dynamic_pr_de,		\
			   pr_fmt(fmt), ##__VA_ARGS__)

#define dynamic_dev_dbg(dev, fmt, ...)				\
	_dynamic_func_call(fmt,__dynamic_dev_dbg, 		\
			   dev, fmt, ##__VA_ARGS__)

#define dynamic_netdev_dbg(dev, fmt, ...)			\
	_dynamic_func_call(fmt, __dynamic_netdev_dbg,		\
			   dev, fmt, ##__VA_ARGS__)

#define dynamic_hex_dump(prefix_str, prefix_type, rowsize,		\
			 groupsize, buf, len, ascii)			\
	_dynamic_func_call_no_desc(__builtin_constant_p(prefix_str) ? prefix_str : "hexdump", \
				   print_hex_dump,			\
				   KERN_DE, prefix_str, prefix_type,	\
				   rowsize, groupsize, buf, len, ascii)

#else

#include <linux/string.h>
#include <linux/errno.h>

static inline int dde_add_module(struct _dde *tab, unsigned int n,
				    const char *modname)
{
	return 0;
}

static inline int dde_remove_module(const char *mod)
{
	return 0;
}

static inline int dde_dyndbg_module_param_cb(char *param, char *val,
						const char *modname)
{
	if (strstr(param, "dyndbg")) {
		/* avoid pr_warn(), which wants pr_fmt() fully defined */
		printk(KERN_WARNING "dyndbg param is supported only in "
			"CONFIG_DYNAMIC_DE builds\n");
		return 0; /* allow and ignore */
	}
	return -EINVAL;
}

#define dynamic_pr_de(fmt, ...)					\
	do { if (0) printk(KERN_DE pr_fmt(fmt), ##__VA_ARGS__); } while (0)
#define dynamic_dev_dbg(dev, fmt, ...)					\
	do { if (0) dev_printk(KERN_DE, dev, fmt, ##__VA_ARGS__); } while (0)
#endif

#endif
