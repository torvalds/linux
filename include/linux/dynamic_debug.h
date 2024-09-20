/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DYNAMIC_DEBUG_H
#define _DYNAMIC_DEBUG_H

#if defined(CONFIG_JUMP_LABEL)
#include <linux/jump_label.h>
#endif

#include <linux/build_bug.h>

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
	unsigned int lineno:18;
#define CLS_BITS 6
	unsigned int class_id:CLS_BITS;
#define _DPRINTK_CLASS_DFLT		((1 << CLS_BITS) - 1)
	/*
	 * The flags field controls the behaviour at the callsite.
	 * The bits here are changed dynamically when the user
	 * writes commands to <debugfs>/dynamic_debug/control
	 */
#define _DPRINTK_FLAGS_NONE	0
#define _DPRINTK_FLAGS_PRINT	(1<<0) /* printk() a message using the format */
#define _DPRINTK_FLAGS_INCL_MODNAME	(1<<1)
#define _DPRINTK_FLAGS_INCL_FUNCNAME	(1<<2)
#define _DPRINTK_FLAGS_INCL_LINENO	(1<<3)
#define _DPRINTK_FLAGS_INCL_TID		(1<<4)
#define _DPRINTK_FLAGS_INCL_SOURCENAME	(1<<5)

#define _DPRINTK_FLAGS_INCL_ANY		\
	(_DPRINTK_FLAGS_INCL_MODNAME | _DPRINTK_FLAGS_INCL_FUNCNAME |\
	 _DPRINTK_FLAGS_INCL_LINENO  | _DPRINTK_FLAGS_INCL_TID |\
	 _DPRINTK_FLAGS_INCL_SOURCENAME)

#if defined DEBUG
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

enum class_map_type {
	DD_CLASS_TYPE_DISJOINT_BITS,
	/**
	 * DD_CLASS_TYPE_DISJOINT_BITS: classes are independent, one per bit.
	 * expecting hex input. Built for drm.debug, basis for other types.
	 */
	DD_CLASS_TYPE_LEVEL_NUM,
	/**
	 * DD_CLASS_TYPE_LEVEL_NUM: input is numeric level, 0-N.
	 * N turns on just bits N-1 .. 0, so N=0 turns all bits off.
	 */
	DD_CLASS_TYPE_DISJOINT_NAMES,
	/**
	 * DD_CLASS_TYPE_DISJOINT_NAMES: input is a CSV of [+-]CLASS_NAMES,
	 * classes are independent, like _DISJOINT_BITS.
	 */
	DD_CLASS_TYPE_LEVEL_NAMES,
	/**
	 * DD_CLASS_TYPE_LEVEL_NAMES: input is a CSV of [+-]CLASS_NAMES,
	 * intended for names like: INFO,DEBUG,TRACE, with a module prefix
	 * avoid EMERG,ALERT,CRIT,ERR,WARNING: they're not debug
	 */
};

struct ddebug_class_map {
	struct list_head link;
	struct module *mod;
	const char *mod_name;	/* needed for builtins */
	const char **class_names;
	const int length;
	const int base;		/* index of 1st .class_id, allows split/shared space */
	enum class_map_type map_type;
};

/**
 * DECLARE_DYNDBG_CLASSMAP - declare classnames known by a module
 * @_var:   a struct ddebug_class_map, passed to module_param_cb
 * @_type:  enum class_map_type, chooses bits/verbose, numeric/symbolic
 * @_base:  offset of 1st class-name. splits .class_id space
 * @classes: class-names used to control class'd prdbgs
 */
#define DECLARE_DYNDBG_CLASSMAP(_var, _maptype, _base, ...)		\
	static const char *_var##_classnames[] = { __VA_ARGS__ };	\
	static struct ddebug_class_map __aligned(8) __used		\
		__section("__dyndbg_classes") _var = {			\
		.mod = THIS_MODULE,					\
		.mod_name = KBUILD_MODNAME,				\
		.base = _base,						\
		.map_type = _maptype,					\
		.length = NUM_TYPE_ARGS(char*, __VA_ARGS__),		\
		.class_names = _var##_classnames,			\
	}
#define NUM_TYPE_ARGS(eltype, ...)				\
        (sizeof((eltype[]){__VA_ARGS__}) / sizeof(eltype))

/* encapsulate linker provided built-in (or module) dyndbg data */
struct _ddebug_info {
	struct _ddebug *descs;
	struct ddebug_class_map *classes;
	unsigned int num_descs;
	unsigned int num_classes;
};

struct ddebug_class_param {
	union {
		unsigned long *bits;
		unsigned int *lvl;
	};
	char flags[8];
	const struct ddebug_class_map *map;
};

/*
 * pr_debug() and friends are globally enabled or modules have selectively
 * enabled them.
 */
#if defined(CONFIG_DYNAMIC_DEBUG) || \
	(defined(CONFIG_DYNAMIC_DEBUG_CORE) && defined(DYNAMIC_DEBUG_MODULE))

extern __printf(2, 3)
void __dynamic_pr_debug(struct _ddebug *descriptor, const char *fmt, ...);

struct device;

extern __printf(3, 4)
void __dynamic_dev_dbg(struct _ddebug *descriptor, const struct device *dev,
		       const char *fmt, ...);

struct net_device;

extern __printf(3, 4)
void __dynamic_netdev_dbg(struct _ddebug *descriptor,
			  const struct net_device *dev,
			  const char *fmt, ...);

struct ib_device;

extern __printf(3, 4)
void __dynamic_ibdev_dbg(struct _ddebug *descriptor,
			 const struct ib_device *ibdev,
			 const char *fmt, ...);

#define DEFINE_DYNAMIC_DEBUG_METADATA_CLS(name, cls, fmt)	\
	static struct _ddebug  __aligned(8)			\
	__section("__dyndbg") name = {				\
		.modname = KBUILD_MODNAME,			\
		.function = __func__,				\
		.filename = __FILE__,				\
		.format = (fmt),				\
		.lineno = __LINE__,				\
		.flags = _DPRINTK_FLAGS_DEFAULT,		\
		.class_id = cls,				\
		_DPRINTK_KEY_INIT				\
	};							\
	BUILD_BUG_ON_MSG(cls > _DPRINTK_CLASS_DFLT,		\
			 "classid value overflow")

#define DEFINE_DYNAMIC_DEBUG_METADATA(name, fmt)		\
	DEFINE_DYNAMIC_DEBUG_METADATA_CLS(name, _DPRINTK_CLASS_DFLT, fmt)

#ifdef CONFIG_JUMP_LABEL

#ifdef DEBUG

#define _DPRINTK_KEY_INIT .key.dd_key_true = (STATIC_KEY_TRUE_INIT)

#define DYNAMIC_DEBUG_BRANCH(descriptor) \
	static_branch_likely(&descriptor.key.dd_key_true)
#else
#define _DPRINTK_KEY_INIT .key.dd_key_false = (STATIC_KEY_FALSE_INIT)

#define DYNAMIC_DEBUG_BRANCH(descriptor) \
	static_branch_unlikely(&descriptor.key.dd_key_false)
#endif

#else /* !CONFIG_JUMP_LABEL */

#define _DPRINTK_KEY_INIT

#ifdef DEBUG
#define DYNAMIC_DEBUG_BRANCH(descriptor) \
	likely(descriptor.flags & _DPRINTK_FLAGS_PRINT)
#else
#define DYNAMIC_DEBUG_BRANCH(descriptor) \
	unlikely(descriptor.flags & _DPRINTK_FLAGS_PRINT)
#endif

#endif /* CONFIG_JUMP_LABEL */

/*
 * Factory macros: ($prefix)dynamic_func_call($suffix)
 *
 * Lower layer (with __ prefix) gets the callsite metadata, and wraps
 * the func inside a debug-branch/static-key construct.  Upper layer
 * (with _ prefix) does the UNIQUE_ID once, so that lower can ref the
 * name/label multiple times, and tie the elements together.
 * Multiple flavors:
 * (|_cls):	adds in _DPRINT_CLASS_DFLT as needed
 * (|_no_desc):	former gets callsite descriptor as 1st arg (for prdbgs)
 */
#define __dynamic_func_call_cls(id, cls, fmt, func, ...) do {	\
	DEFINE_DYNAMIC_DEBUG_METADATA_CLS(id, cls, fmt);	\
	if (DYNAMIC_DEBUG_BRANCH(id))				\
		func(&id, ##__VA_ARGS__);			\
} while (0)
#define __dynamic_func_call(id, fmt, func, ...)				\
	__dynamic_func_call_cls(id, _DPRINTK_CLASS_DFLT, fmt,		\
				func, ##__VA_ARGS__)

#define __dynamic_func_call_cls_no_desc(id, cls, fmt, func, ...) do {	\
	DEFINE_DYNAMIC_DEBUG_METADATA_CLS(id, cls, fmt);		\
	if (DYNAMIC_DEBUG_BRANCH(id))					\
		func(__VA_ARGS__);					\
} while (0)
#define __dynamic_func_call_no_desc(id, fmt, func, ...)			\
	__dynamic_func_call_cls_no_desc(id, _DPRINTK_CLASS_DFLT,	\
					fmt, func, ##__VA_ARGS__)

/*
 * "Factory macro" for generating a call to func, guarded by a
 * DYNAMIC_DEBUG_BRANCH. The dynamic debug descriptor will be
 * initialized using the fmt argument. The function will be called with
 * the address of the descriptor as first argument, followed by all
 * the varargs. Note that fmt is repeated in invocations of this
 * macro.
 */
#define _dynamic_func_call_cls(cls, fmt, func, ...)			\
	__dynamic_func_call_cls(__UNIQUE_ID(ddebug), cls, fmt, func, ##__VA_ARGS__)
#define _dynamic_func_call(fmt, func, ...)				\
	_dynamic_func_call_cls(_DPRINTK_CLASS_DFLT, fmt, func, ##__VA_ARGS__)

/*
 * A variant that does the same, except that the descriptor is not
 * passed as the first argument to the function; it is only called
 * with precisely the macro's varargs.
 */
#define _dynamic_func_call_cls_no_desc(cls, fmt, func, ...)		\
	__dynamic_func_call_cls_no_desc(__UNIQUE_ID(ddebug), cls, fmt,	\
					func, ##__VA_ARGS__)
#define _dynamic_func_call_no_desc(fmt, func, ...)			\
	_dynamic_func_call_cls_no_desc(_DPRINTK_CLASS_DFLT, fmt,	\
				       func, ##__VA_ARGS__)

#define dynamic_pr_debug_cls(cls, fmt, ...)				\
	_dynamic_func_call_cls(cls, fmt, __dynamic_pr_debug,		\
			   pr_fmt(fmt), ##__VA_ARGS__)

#define dynamic_pr_debug(fmt, ...)				\
	_dynamic_func_call(fmt, __dynamic_pr_debug,		\
			   pr_fmt(fmt), ##__VA_ARGS__)

#define dynamic_dev_dbg(dev, fmt, ...)				\
	_dynamic_func_call(fmt, __dynamic_dev_dbg, 		\
			   dev, fmt, ##__VA_ARGS__)

#define dynamic_netdev_dbg(dev, fmt, ...)			\
	_dynamic_func_call(fmt, __dynamic_netdev_dbg,		\
			   dev, fmt, ##__VA_ARGS__)

#define dynamic_ibdev_dbg(dev, fmt, ...)			\
	_dynamic_func_call(fmt, __dynamic_ibdev_dbg,		\
			   dev, fmt, ##__VA_ARGS__)

#define dynamic_hex_dump(prefix_str, prefix_type, rowsize,		\
			 groupsize, buf, len, ascii)			\
	_dynamic_func_call_no_desc(__builtin_constant_p(prefix_str) ? prefix_str : "hexdump", \
				   print_hex_dump,			\
				   KERN_DEBUG, prefix_str, prefix_type,	\
				   rowsize, groupsize, buf, len, ascii)

/* for test only, generally expect drm.debug style macro wrappers */
#define __pr_debug_cls(cls, fmt, ...) do {			\
	BUILD_BUG_ON_MSG(!__builtin_constant_p(cls),		\
			 "expecting constant class int/enum");	\
	dynamic_pr_debug_cls(cls, fmt, ##__VA_ARGS__);		\
	} while (0)

#else /* !(CONFIG_DYNAMIC_DEBUG || (CONFIG_DYNAMIC_DEBUG_CORE && DYNAMIC_DEBUG_MODULE)) */

#include <linux/string.h>
#include <linux/errno.h>
#include <linux/printk.h>

#define DEFINE_DYNAMIC_DEBUG_METADATA(name, fmt)
#define DYNAMIC_DEBUG_BRANCH(descriptor) false

#define dynamic_pr_debug(fmt, ...)					\
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#define dynamic_dev_dbg(dev, fmt, ...)					\
	dev_no_printk(KERN_DEBUG, dev, fmt, ##__VA_ARGS__)
#define dynamic_hex_dump(prefix_str, prefix_type, rowsize,		\
			 groupsize, buf, len, ascii)			\
	do { if (0)							\
		print_hex_dump(KERN_DEBUG, prefix_str, prefix_type,	\
				rowsize, groupsize, buf, len, ascii);	\
	} while (0)

#endif /* CONFIG_DYNAMIC_DEBUG || (CONFIG_DYNAMIC_DEBUG_CORE && DYNAMIC_DEBUG_MODULE) */


#ifdef CONFIG_DYNAMIC_DEBUG_CORE

extern int ddebug_dyndbg_module_param_cb(char *param, char *val,
					const char *modname);
struct kernel_param;
int param_set_dyndbg_classes(const char *instr, const struct kernel_param *kp);
int param_get_dyndbg_classes(char *buffer, const struct kernel_param *kp);

#else

static inline int ddebug_dyndbg_module_param_cb(char *param, char *val,
						const char *modname)
{
	if (!strcmp(param, "dyndbg")) {
		/* avoid pr_warn(), which wants pr_fmt() fully defined */
		printk(KERN_WARNING "dyndbg param is supported only in "
			"CONFIG_DYNAMIC_DEBUG builds\n");
		return 0; /* allow and ignore */
	}
	return -EINVAL;
}

struct kernel_param;
static inline int param_set_dyndbg_classes(const char *instr, const struct kernel_param *kp)
{ return 0; }
static inline int param_get_dyndbg_classes(char *buffer, const struct kernel_param *kp)
{ return 0; }

#endif


extern const struct kernel_param_ops param_ops_dyndbg_classes;

#endif /* _DYNAMIC_DEBUG_H */
