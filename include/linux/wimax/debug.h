/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Linux WiMAX
 * Collection of tools to manage debug operations.
 *
 * Copyright (C) 2005-2007 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * Don't #include this file directly, read on!
 *
 * EXECUTING DEBUGGING ACTIONS OR NOT
 *
 * The main thing this framework provides is decission power to take a
 * debug action (like printing a message) if the current debug level
 * allows it.
 *
 * The decission power is at two levels: at compile-time (what does
 * not make it is compiled out) and at run-time. The run-time
 * selection is done per-submodule (as they are declared by the user
 * of the framework).
 *
 * A call to d_test(L) (L being the target debug level) returns true
 * if the action should be taken because the current debug levels
 * allow it (both compile and run time).
 *
 * It follows that a call to d_test() that can be determined to be
 * always false at compile time will get the code depending on it
 * compiled out by optimization.
 *
 * DEBUG LEVELS
 *
 * It is up to the caller to define how much a debugging level is.
 *
 * Convention sets 0 as "no debug" (so an action marked as debug level 0
 * will always be taken). The increasing debug levels are used for
 * increased verbosity.
 *
 * USAGE
 *
 * Group the code in modules and submodules inside each module [which
 * in most cases maps to Linux modules and .c files that compose
 * those].
 *
 * For each module, there is:
 *
 *  - a MODULENAME (single word, legal C identifier)
 *
 *  - a debug-levels.h header file that declares the list of
 *    submodules and that is included by all .c files that use
 *    the debugging tools. The file name can be anything.
 *
 *  - some (optional) .c code to manipulate the runtime debug levels
 *    through debugfs.
 *
 * The debug-levels.h file would look like:
 *
 *     #ifndef __debug_levels__h__
 *     #define __debug_levels__h__
 *
 *     #define D_MODULENAME modulename
 *     #define D_MASTER 10
 *
 *     #include <linux/wimax/debug.h>
 *
 *     enum d_module {
 *             D_SUBMODULE_DECLARE(submodule_1),
 *             D_SUBMODULE_DECLARE(submodule_2),
 *             ...
 *             D_SUBMODULE_DECLARE(submodule_N)
 *     };
 *
 *     #endif
 *
 * D_MASTER is the maximum compile-time debug level; any debug actions
 * above this will be out. D_MODULENAME is the module name (legal C
 * identifier), which has to be unique for each module (to avoid
 * namespace collisions during linkage). Note those #defines need to
 * be done before #including debug.h
 *
 * We declare N different submodules whose debug level can be
 * independently controlled during runtime.
 *
 * In a .c file of the module (and only in one of them), define the
 * following code:
 *
 *     struct d_level D_LEVEL[] = {
 *             D_SUBMODULE_DEFINE(submodule_1),
 *             D_SUBMODULE_DEFINE(submodule_2),
 *             ...
 *             D_SUBMODULE_DEFINE(submodule_N),
 *     };
 *     size_t D_LEVEL_SIZE = ARRAY_SIZE(D_LEVEL);
 *
 * Externs for d_level_MODULENAME and d_level_size_MODULENAME are used
 * and declared in this file using the D_LEVEL and D_LEVEL_SIZE macros
 * #defined also in this file.
 *
 * To manipulate from user space the levels, create a debugfs dentry
 * and then register each submodule with:
 *
 *     result = d_level_register_debugfs("PREFIX_", submodule_X, parent);
 *     if (result < 0)
 *            goto error;
 *
 * Where PREFIX_ is a name of your chosing. This will create debugfs
 * file with a single numeric value that can be use to tweak it. To
 * remove the entires, just use debugfs_remove_recursive() on 'parent'.
 *
 * NOTE: remember that even if this will show attached to some
 *     particular instance of a device, the settings are *global*.
 *
 * On each submodule (for example, .c files), the debug infrastructure
 * should be included like this:
 *
 *     #define D_SUBMODULE submodule_x     // matches one in debug-levels.h
 *     #include "debug-levels.h"
 *
 * after #including all your include files.
 *
 * Now you can use the d_*() macros below [d_test(), d_fnstart(),
 * d_fnend(), d_printf(), d_dump()].
 *
 * If their debug level is greater than D_MASTER, they will be
 * compiled out.
 *
 * If their debug level is lower or equal than D_MASTER but greater
 * than the current debug level of their submodule, they'll be
 * ignored.
 *
 * Otherwise, the action will be performed.
 */
#ifndef __debug__h__
#define __debug__h__

#include <linux/types.h>
#include <linux/slab.h>

struct device;

/* Backend stuff */

/*
 * Debug backend: generate a message header from a 'struct device'
 *
 * @head: buffer where to place the header
 * @head_size: length of @head
 * @dev: pointer to device used to generate a header from. If NULL,
 *     an empty ("") header is generated.
 */
static inline
void __d_head(char *head, size_t head_size,
	      struct device *dev)
{
	if (dev == NULL)
		head[0] = 0;
	else if ((unsigned long)dev < 4096) {
		printk(KERN_ERR "E: Corrupt dev %p\n", dev);
		WARN_ON(1);
	} else
		snprintf(head, head_size, "%s %s: ",
			 dev_driver_string(dev), dev_name(dev));
}


/*
 * Debug backend: log some message if debugging is enabled
 *
 * @l: intended debug level
 * @tag: tag to prefix the message with
 * @dev: 'struct device' associated to this message
 * @f: printf-like format and arguments
 *
 * Note this is optimized out if it doesn't pass the compile-time
 * check; however, it is *always* compiled. This is useful to make
 * sure the printf-like formats and variables are always checked and
 * they don't get bit rot if you have all the debugging disabled.
 */
#define _d_printf(l, tag, dev, f, a...)					\
do {									\
	char head[64];							\
	if (!d_test(l))							\
		break;							\
	__d_head(head, sizeof(head), dev);				\
	printk(KERN_ERR "%s%s%s: " f, head, __func__, tag, ##a);	\
} while (0)


/*
 * CPP sintatic sugar to generate A_B like symbol names when one of
 * the arguments is a a preprocessor #define.
 */
#define __D_PASTE__(varname, modulename) varname##_##modulename
#define __D_PASTE(varname, modulename) (__D_PASTE__(varname, modulename))
#define _D_SUBMODULE_INDEX(_name) (D_SUBMODULE_DECLARE(_name))


/*
 * Store a submodule's runtime debug level and name
 */
struct d_level {
	u8 level;
	const char *name;
};


/*
 * List of available submodules and their debug levels
 *
 * We call them d_level_MODULENAME and d_level_size_MODULENAME; the
 * macros D_LEVEL and D_LEVEL_SIZE contain the name already for
 * convenience.
 *
 * This array and the size are defined on some .c file that is part of
 * the current module.
 */
#define D_LEVEL __D_PASTE(d_level, D_MODULENAME)
#define D_LEVEL_SIZE __D_PASTE(d_level_size, D_MODULENAME)

extern struct d_level D_LEVEL[];
extern size_t D_LEVEL_SIZE;


/*
 * Frontend stuff
 *
 *
 * Stuff you need to declare prior to using the actual "debug" actions
 * (defined below).
 */

#ifndef D_MODULENAME
#error D_MODULENAME is not defined in your debug-levels.h file
/**
 * D_MODULE - Name of the current module
 *
 * #define in your module's debug-levels.h, making sure it is
 * unique. This has to be a legal C identifier.
 */
#define D_MODULENAME undefined_modulename
#endif


#ifndef D_MASTER
#warning D_MASTER not defined, but debug.h included! [see docs]
/**
 * D_MASTER - Compile time maximum debug level
 *
 * #define in your debug-levels.h file to the maximum debug level the
 * runtime code will be allowed to have. This allows you to provide a
 * main knob.
 *
 * Anything above that level will be optimized out of the compile.
 *
 * Defaults to zero (no debug code compiled in).
 *
 * Maximum one definition per module (at the debug-levels.h file).
 */
#define D_MASTER 0
#endif

#ifndef D_SUBMODULE
#error D_SUBMODULE not defined, but debug.h included! [see docs]
/**
 * D_SUBMODULE - Name of the current submodule
 *
 * #define in your submodule .c file before #including debug-levels.h
 * to the name of the current submodule as previously declared and
 * defined with D_SUBMODULE_DECLARE() (in your module's
 * debug-levels.h) and D_SUBMODULE_DEFINE().
 *
 * This is used to provide runtime-control over the debug levels.
 *
 * Maximum one per .c file! Can be shared among different .c files
 * (meaning they belong to the same submodule categorization).
 */
#define D_SUBMODULE undefined_module
#endif


/**
 * D_SUBMODULE_DECLARE - Declare a submodule for runtime debug level control
 *
 * @_name: name of the submodule, restricted to the chars that make up a
 *     valid C identifier ([a-zA-Z0-9_]).
 *
 * Declare in the module's debug-levels.h header file as:
 *
 * enum d_module {
 *         D_SUBMODULE_DECLARE(submodule_1),
 *         D_SUBMODULE_DECLARE(submodule_2),
 *         D_SUBMODULE_DECLARE(submodule_3),
 * };
 *
 * Some corresponding .c file needs to have a matching
 * D_SUBMODULE_DEFINE().
 */
#define D_SUBMODULE_DECLARE(_name) __D_SUBMODULE_##_name


/**
 * D_SUBMODULE_DEFINE - Define a submodule for runtime debug level control
 *
 * @_name: name of the submodule, restricted to the chars that make up a
 *     valid C identifier ([a-zA-Z0-9_]).
 *
 * Use once per module (in some .c file) as:
 *
 * static
 * struct d_level d_level_SUBMODULENAME[] = {
 *         D_SUBMODULE_DEFINE(submodule_1),
 *         D_SUBMODULE_DEFINE(submodule_2),
 *         D_SUBMODULE_DEFINE(submodule_3),
 * };
 * size_t d_level_size_SUBDMODULENAME = ARRAY_SIZE(d_level_SUBDMODULENAME);
 *
 * Matching D_SUBMODULE_DECLARE()s have to be present in a
 * debug-levels.h header file.
 */
#define D_SUBMODULE_DEFINE(_name)		\
[__D_SUBMODULE_##_name] = {			\
	.level = 0,				\
	.name = #_name				\
}



/* The actual "debug" operations */


/**
 * d_test - Returns true if debugging should be enabled
 *
 * @l: intended debug level (unsigned)
 *
 * If the master debug switch is enabled and the current settings are
 * higher or equal to the requested level, then debugging
 * output/actions should be enabled.
 *
 * NOTE:
 *
 * This needs to be coded so that it can be evaluated in compile
 * time; this is why the ugly BUG_ON() is placed in there, so the
 * D_MASTER evaluation compiles all out if it is compile-time false.
 */
#define d_test(l)							\
({									\
	unsigned __l = l;	/* type enforcer */			\
	(D_MASTER) >= __l						\
	&& ({								\
		BUG_ON(_D_SUBMODULE_INDEX(D_SUBMODULE) >= D_LEVEL_SIZE);\
		D_LEVEL[_D_SUBMODULE_INDEX(D_SUBMODULE)].level >= __l;	\
	});								\
})


/**
 * d_fnstart - log message at function start if debugging enabled
 *
 * @l: intended debug level
 * @_dev: 'struct device' pointer, NULL if none (for context)
 * @f: printf-like format and arguments
 */
#define d_fnstart(l, _dev, f, a...) _d_printf(l, " FNSTART", _dev, f, ## a)


/**
 * d_fnend - log message at function end if debugging enabled
 *
 * @l: intended debug level
 * @_dev: 'struct device' pointer, NULL if none (for context)
 * @f: printf-like format and arguments
 */
#define d_fnend(l, _dev, f, a...) _d_printf(l, " FNEND", _dev, f, ## a)


/**
 * d_printf - log message if debugging enabled
 *
 * @l: intended debug level
 * @_dev: 'struct device' pointer, NULL if none (for context)
 * @f: printf-like format and arguments
 */
#define d_printf(l, _dev, f, a...) _d_printf(l, "", _dev, f, ## a)


/**
 * d_dump - log buffer hex dump if debugging enabled
 *
 * @l: intended debug level
 * @_dev: 'struct device' pointer, NULL if none (for context)
 * @f: printf-like format and arguments
 */
#define d_dump(l, dev, ptr, size)			\
do {							\
	char head[64];					\
	if (!d_test(l))					\
		break;					\
	__d_head(head, sizeof(head), dev);		\
	print_hex_dump(KERN_ERR, head, 0, 16, 1,	\
		       ((void *) ptr), (size), 0);	\
} while (0)


/**
 * Export a submodule's debug level over debugfs as PREFIXSUBMODULE
 *
 * @prefix: string to prefix the name with
 * @submodule: name of submodule (not a string, just the name)
 * @dentry: debugfs parent dentry
 *
 * Returns: 0 if ok, < 0 errno on error.
 *
 * For removing, just use debugfs_remove_recursive() on the parent.
 */
#define d_level_register_debugfs(prefix, name, parent)			\
({									\
	int rc;								\
	struct dentry *fd;						\
	struct dentry *verify_parent_type = parent;			\
	fd = debugfs_create_u8(						\
		prefix #name, 0600, verify_parent_type,			\
		&(D_LEVEL[__D_SUBMODULE_ ## name].level));		\
	rc = PTR_ERR(fd);						\
	if (IS_ERR(fd) && rc != -ENODEV)				\
		printk(KERN_ERR "%s: Can't create debugfs entry %s: "	\
		       "%d\n", __func__, prefix #name, rc);		\
	else								\
		rc = 0;							\
	rc;								\
})


static inline
void d_submodule_set(struct d_level *d_level, size_t d_level_size,
		     const char *submodule, u8 level, const char *tag)
{
	struct d_level *itr, *top;
	int index = -1;

	for (itr = d_level, top = itr + d_level_size; itr < top; itr++) {
		index++;
		if (itr->name == NULL) {
			printk(KERN_ERR "%s: itr->name NULL?? (%p, #%d)\n",
			       tag, itr, index);
			continue;
		}
		if (!strcmp(itr->name, submodule)) {
			itr->level = level;
			return;
		}
	}
	printk(KERN_ERR "%s: unknown submodule %s\n", tag, submodule);
}


/**
 * d_parse_params - Parse a string with debug parameters from the
 * command line
 *
 * @d_level: level structure (D_LEVEL)
 * @d_level_size: number of items in the level structure
 *     (D_LEVEL_SIZE).
 * @_params: string with the parameters; this is a space (not tab!)
 *     separated list of NAME:VALUE, where value is the debug level
 *     and NAME is the name of the submodule.
 * @tag: string for error messages (example: MODULE.ARGNAME).
 */
static inline
void d_parse_params(struct d_level *d_level, size_t d_level_size,
		    const char *_params, const char *tag)
{
	char submodule[130], *params, *params_orig, *token, *colon;
	unsigned level, tokens;

	if (_params == NULL)
		return;
	params_orig = kstrdup(_params, GFP_KERNEL);
	params = params_orig;
	while (1) {
		token = strsep(&params, " ");
		if (token == NULL)
			break;
		if (*token == '\0')	/* eat joint spaces */
			continue;
		/* kernel's sscanf %s eats until whitespace, so we
		 * replace : by \n so it doesn't get eaten later by
		 * strsep */
		colon = strchr(token, ':');
		if (colon != NULL)
			*colon = '\n';
		tokens = sscanf(token, "%s\n%u", submodule, &level);
		if (colon != NULL)
			*colon = ':';	/* set back, for error messages */
		if (tokens == 2)
			d_submodule_set(d_level, d_level_size,
					submodule, level, tag);
		else
			printk(KERN_ERR "%s: can't parse '%s' as a "
			       "SUBMODULE:LEVEL (%d tokens)\n",
			       tag, token, tokens);
	}
	kfree(params_orig);
}

#endif /* #ifndef __debug__h__ */
