#ifndef _LINUX_MODULE_PARAMS_H
#define _LINUX_MODULE_PARAMS_H
/* (C) Copyright 2001, 2002 Rusty Russell IBM Corporation */
#include <linux/init.h>
#include <linux/stringify.h>
#include <linux/kernel.h>

/* You can override this manually, but generally this should match the
   module name. */
#ifdef MODULE
#define MODULE_PARAM_PREFIX /* empty */
#else
#define MODULE_PARAM_PREFIX KBUILD_MODNAME "."
#endif

/* Chosen so that structs with an unsigned long line up. */
#define MAX_PARAM_PREFIX_LEN (64 - sizeof(unsigned long))

#ifdef MODULE
#define __MODULE_INFO(tag, name, info)					  \
static const char __UNIQUE_ID(name)[]					  \
  __used __attribute__((section(".modinfo"), unused, aligned(1)))	  \
  = __stringify(tag) "=" info
#else  /* !MODULE */
/* This struct is here for syntactic coherency, it is not used */
#define __MODULE_INFO(tag, name, info)					  \
  struct __UNIQUE_ID(name) {}
#endif
#define __MODULE_PARM_TYPE(name, _type)					  \
  __MODULE_INFO(parmtype, name##type, #name ":" _type)

/* One for each parameter, describing how to use it.  Some files do
   multiple of these per line, so can't just use MODULE_INFO. */
#define MODULE_PARM_DESC(_parm, desc) \
	__MODULE_INFO(parm, _parm, #_parm ":" desc)

struct kernel_param;

/*
 * Flags available for kernel_param_ops
 *
 * NOARG - the parameter allows for no argument (foo instead of foo=1)
 */
enum {
	KERNEL_PARAM_OPS_FL_NOARG = (1 << 0)
};

struct kernel_param_ops {
	/* How the ops should behave */
	unsigned int flags;
	/* Returns 0, or -errno.  arg is in kp->arg. */
	int (*set)(const char *val, const struct kernel_param *kp);
	/* Returns length written or -errno.  Buffer is 4k (ie. be short!) */
	int (*get)(char *buffer, const struct kernel_param *kp);
	/* Optional function to free kp->arg when module unloaded. */
	void (*free)(void *arg);
};

/*
 * Flags available for kernel_param
 *
 * UNSAFE - the parameter is dangerous and setting it will taint the kernel
 */
enum {
	KERNEL_PARAM_FL_UNSAFE = (1 << 0)
};

struct kernel_param {
	const char *name;
	const struct kernel_param_ops *ops;
	u16 perm;
	s8 level;
	u8 flags;
	union {
		void *arg;
		const struct kparam_string *str;
		const struct kparam_array *arr;
	};
};

extern const struct kernel_param __start___param[], __stop___param[];

/* Special one for strings we want to copy into */
struct kparam_string {
	unsigned int maxlen;
	char *string;
};

/* Special one for arrays */
struct kparam_array
{
	unsigned int max;
	unsigned int elemsize;
	unsigned int *num;
	const struct kernel_param_ops *ops;
	void *elem;
};

/**
 * module_param - typesafe helper for a module/cmdline parameter
 * @value: the variable to alter, and exposed parameter name.
 * @type: the type of the parameter
 * @perm: visibility in sysfs.
 *
 * @value becomes the module parameter, or (prefixed by KBUILD_MODNAME and a
 * ".") the kernel commandline parameter.  Note that - is changed to _, so
 * the user can use "foo-bar=1" even for variable "foo_bar".
 *
 * @perm is 0 if the the variable is not to appear in sysfs, or 0444
 * for world-readable, 0644 for root-writable, etc.  Note that if it
 * is writable, you may need to use kparam_block_sysfs_write() around
 * accesses (esp. charp, which can be kfreed when it changes).
 *
 * The @type is simply pasted to refer to a param_ops_##type and a
 * param_check_##type: for convenience many standard types are provided but
 * you can create your own by defining those variables.
 *
 * Standard types are:
 *	byte, short, ushort, int, uint, long, ulong
 *	charp: a character pointer
 *	bool: a bool, values 0/1, y/n, Y/N.
 *	invbool: the above, only sense-reversed (N = true).
 */
#define module_param(name, type, perm)				\
	module_param_named(name, name, type, perm)

/**
 * module_param_unsafe - same as module_param but taints kernel
 */
#define module_param_unsafe(name, type, perm)			\
	module_param_named_unsafe(name, name, type, perm)

/**
 * module_param_named - typesafe helper for a renamed module/cmdline parameter
 * @name: a valid C identifier which is the parameter name.
 * @value: the actual lvalue to alter.
 * @type: the type of the parameter
 * @perm: visibility in sysfs.
 *
 * Usually it's a good idea to have variable names and user-exposed names the
 * same, but that's harder if the variable must be non-static or is inside a
 * structure.  This allows exposure under a different name.
 */
#define module_param_named(name, value, type, perm)			   \
	param_check_##type(name, &(value));				   \
	module_param_cb(name, &param_ops_##type, &value, perm);		   \
	__MODULE_PARM_TYPE(name, #type)

/**
 * module_param_named_unsafe - same as module_param_named but taints kernel
 */
#define module_param_named_unsafe(name, value, type, perm)		\
	param_check_##type(name, &(value));				\
	module_param_cb_unsafe(name, &param_ops_##type, &value, perm);	\
	__MODULE_PARM_TYPE(name, #type)

/**
 * module_param_cb - general callback for a module/cmdline parameter
 * @name: a valid C identifier which is the parameter name.
 * @ops: the set & get operations for this parameter.
 * @perm: visibility in sysfs.
 *
 * The ops can have NULL set or get functions.
 */
#define module_param_cb(name, ops, arg, perm)				      \
	__module_param_call(MODULE_PARAM_PREFIX, name, ops, arg, perm, -1, 0)

#define module_param_cb_unsafe(name, ops, arg, perm)			      \
	__module_param_call(MODULE_PARAM_PREFIX, name, ops, arg, perm, -1,    \
			    KERNEL_PARAM_FL_UNSAFE)

/**
 * <level>_param_cb - general callback for a module/cmdline parameter
 *                    to be evaluated before certain initcall level
 * @name: a valid C identifier which is the parameter name.
 * @ops: the set & get operations for this parameter.
 * @perm: visibility in sysfs.
 *
 * The ops can have NULL set or get functions.
 */
#define __level_param_cb(name, ops, arg, perm, level)			\
	__module_param_call(MODULE_PARAM_PREFIX, name, ops, arg, perm, level, 0)

#define core_param_cb(name, ops, arg, perm)		\
	__level_param_cb(name, ops, arg, perm, 1)

#define postcore_param_cb(name, ops, arg, perm)		\
	__level_param_cb(name, ops, arg, perm, 2)

#define arch_param_cb(name, ops, arg, perm)		\
	__level_param_cb(name, ops, arg, perm, 3)

#define subsys_param_cb(name, ops, arg, perm)		\
	__level_param_cb(name, ops, arg, perm, 4)

#define fs_param_cb(name, ops, arg, perm)		\
	__level_param_cb(name, ops, arg, perm, 5)

#define device_param_cb(name, ops, arg, perm)		\
	__level_param_cb(name, ops, arg, perm, 6)

#define late_param_cb(name, ops, arg, perm)		\
	__level_param_cb(name, ops, arg, perm, 7)

/* On alpha, ia64 and ppc64 relocations to global data cannot go into
   read-only sections (which is part of respective UNIX ABI on these
   platforms). So 'const' makes no sense and even causes compile failures
   with some compilers. */
#if defined(CONFIG_ALPHA) || defined(CONFIG_IA64) || defined(CONFIG_PPC64)
#define __moduleparam_const
#else
#define __moduleparam_const const
#endif

/* This is the fundamental function for registering boot/module
   parameters. */
#define __module_param_call(prefix, name, ops, arg, perm, level, flags)	\
	/* Default value instead of permissions? */			\
	static const char __param_str_##name[] = prefix #name; \
	static struct kernel_param __moduleparam_const __param_##name	\
	__used								\
    __attribute__ ((unused,__section__ ("__param"),aligned(sizeof(void *)))) \
	= { __param_str_##name, ops, VERIFY_OCTAL_PERMISSIONS(perm),	\
	    level, flags, { arg } }

/* Obsolete - use module_param_cb() */
#define module_param_call(name, set, get, arg, perm)			\
	static struct kernel_param_ops __param_ops_##name =		\
		{ .flags = 0, (void *)set, (void *)get };		\
	__module_param_call(MODULE_PARAM_PREFIX,			\
			    name, &__param_ops_##name, arg,		\
			    (perm) + sizeof(__check_old_set_param(set))*0, -1, 0)

/* We don't get oldget: it's often a new-style param_get_uint, etc. */
static inline int
__check_old_set_param(int (*oldset)(const char *, struct kernel_param *))
{
	return 0;
}

/**
 * kparam_block_sysfs_write - make sure a parameter isn't written via sysfs.
 * @name: the name of the parameter
 *
 * There's no point blocking write on a paramter that isn't writable via sysfs!
 */
#define kparam_block_sysfs_write(name)			\
	do {						\
		BUG_ON(!(__param_##name.perm & 0222));	\
		__kernel_param_lock();			\
	} while (0)

/**
 * kparam_unblock_sysfs_write - allows sysfs to write to a parameter again.
 * @name: the name of the parameter
 */
#define kparam_unblock_sysfs_write(name)		\
	do {						\
		BUG_ON(!(__param_##name.perm & 0222));	\
		__kernel_param_unlock();		\
	} while (0)

/**
 * kparam_block_sysfs_read - make sure a parameter isn't read via sysfs.
 * @name: the name of the parameter
 *
 * This also blocks sysfs writes.
 */
#define kparam_block_sysfs_read(name)			\
	do {						\
		BUG_ON(!(__param_##name.perm & 0444));	\
		__kernel_param_lock();			\
	} while (0)

/**
 * kparam_unblock_sysfs_read - allows sysfs to read a parameter again.
 * @name: the name of the parameter
 */
#define kparam_unblock_sysfs_read(name)			\
	do {						\
		BUG_ON(!(__param_##name.perm & 0444));	\
		__kernel_param_unlock();		\
	} while (0)

#ifdef CONFIG_SYSFS
extern void __kernel_param_lock(void);
extern void __kernel_param_unlock(void);
#else
static inline void __kernel_param_lock(void)
{
}
static inline void __kernel_param_unlock(void)
{
}
#endif

#ifndef MODULE
/**
 * core_param - define a historical core kernel parameter.
 * @name: the name of the cmdline and sysfs parameter (often the same as var)
 * @var: the variable
 * @type: the type of the parameter
 * @perm: visibility in sysfs
 *
 * core_param is just like module_param(), but cannot be modular and
 * doesn't add a prefix (such as "printk.").  This is for compatibility
 * with __setup(), and it makes sense as truly core parameters aren't
 * tied to the particular file they're in.
 */
#define core_param(name, var, type, perm)				\
	param_check_##type(name, &(var));				\
	__module_param_call("", name, &param_ops_##type, &var, perm, -1, 0)
#endif /* !MODULE */

/**
 * module_param_string - a char array parameter
 * @name: the name of the parameter
 * @string: the string variable
 * @len: the maximum length of the string, incl. terminator
 * @perm: visibility in sysfs.
 *
 * This actually copies the string when it's set (unlike type charp).
 * @len is usually just sizeof(string).
 */
#define module_param_string(name, string, len, perm)			\
	static const struct kparam_string __param_string_##name		\
		= { len, string };					\
	__module_param_call(MODULE_PARAM_PREFIX, name,			\
			    &param_ops_string,				\
			    .str = &__param_string_##name, perm, -1, 0);\
	__MODULE_PARM_TYPE(name, "string")

/**
 * parameq - checks if two parameter names match
 * @name1: parameter name 1
 * @name2: parameter name 2
 *
 * Returns true if the two parameter names are equal.
 * Dashes (-) are considered equal to underscores (_).
 */
extern bool parameq(const char *name1, const char *name2);

/**
 * parameqn - checks if two parameter names match
 * @name1: parameter name 1
 * @name2: parameter name 2
 * @n: the length to compare
 *
 * Similar to parameq(), except it compares @n characters.
 */
extern bool parameqn(const char *name1, const char *name2, size_t n);

/* Called on module insert or kernel boot */
extern char *parse_args(const char *name,
		      char *args,
		      const struct kernel_param *params,
		      unsigned num,
		      s16 level_min,
		      s16 level_max,
		      int (*unknown)(char *param, char *val,
			      const char *doing));

/* Called by module remove. */
#ifdef CONFIG_SYSFS
extern void destroy_params(const struct kernel_param *params, unsigned num);
#else
static inline void destroy_params(const struct kernel_param *params,
				  unsigned num)
{
}
#endif /* !CONFIG_SYSFS */

/* All the helper functions */
/* The macros to do compile-time type checking stolen from Jakub
   Jelinek, who IIRC came up with this idea for the 2.4 module init code. */
#define __param_check(name, p, type) \
	static inline type __always_unused *__check_##name(void) { return(p); }

extern struct kernel_param_ops param_ops_byte;
extern int param_set_byte(const char *val, const struct kernel_param *kp);
extern int param_get_byte(char *buffer, const struct kernel_param *kp);
#define param_check_byte(name, p) __param_check(name, p, unsigned char)

extern struct kernel_param_ops param_ops_short;
extern int param_set_short(const char *val, const struct kernel_param *kp);
extern int param_get_short(char *buffer, const struct kernel_param *kp);
#define param_check_short(name, p) __param_check(name, p, short)

extern struct kernel_param_ops param_ops_ushort;
extern int param_set_ushort(const char *val, const struct kernel_param *kp);
extern int param_get_ushort(char *buffer, const struct kernel_param *kp);
#define param_check_ushort(name, p) __param_check(name, p, unsigned short)

extern struct kernel_param_ops param_ops_int;
extern int param_set_int(const char *val, const struct kernel_param *kp);
extern int param_get_int(char *buffer, const struct kernel_param *kp);
#define param_check_int(name, p) __param_check(name, p, int)

extern struct kernel_param_ops param_ops_uint;
extern int param_set_uint(const char *val, const struct kernel_param *kp);
extern int param_get_uint(char *buffer, const struct kernel_param *kp);
#define param_check_uint(name, p) __param_check(name, p, unsigned int)

extern struct kernel_param_ops param_ops_long;
extern int param_set_long(const char *val, const struct kernel_param *kp);
extern int param_get_long(char *buffer, const struct kernel_param *kp);
#define param_check_long(name, p) __param_check(name, p, long)

extern struct kernel_param_ops param_ops_ulong;
extern int param_set_ulong(const char *val, const struct kernel_param *kp);
extern int param_get_ulong(char *buffer, const struct kernel_param *kp);
#define param_check_ulong(name, p) __param_check(name, p, unsigned long)

extern struct kernel_param_ops param_ops_ullong;
extern int param_set_ullong(const char *val, const struct kernel_param *kp);
extern int param_get_ullong(char *buffer, const struct kernel_param *kp);
#define param_check_ullong(name, p) __param_check(name, p, unsigned long long)

extern struct kernel_param_ops param_ops_charp;
extern int param_set_charp(const char *val, const struct kernel_param *kp);
extern int param_get_charp(char *buffer, const struct kernel_param *kp);
#define param_check_charp(name, p) __param_check(name, p, char *)

/* We used to allow int as well as bool.  We're taking that away! */
extern struct kernel_param_ops param_ops_bool;
extern int param_set_bool(const char *val, const struct kernel_param *kp);
extern int param_get_bool(char *buffer, const struct kernel_param *kp);
#define param_check_bool(name, p) __param_check(name, p, bool)

extern struct kernel_param_ops param_ops_invbool;
extern int param_set_invbool(const char *val, const struct kernel_param *kp);
extern int param_get_invbool(char *buffer, const struct kernel_param *kp);
#define param_check_invbool(name, p) __param_check(name, p, bool)

/* An int, which can only be set like a bool (though it shows as an int). */
extern struct kernel_param_ops param_ops_bint;
extern int param_set_bint(const char *val, const struct kernel_param *kp);
#define param_get_bint param_get_int
#define param_check_bint param_check_int

/**
 * module_param_array - a parameter which is an array of some type
 * @name: the name of the array variable
 * @type: the type, as per module_param()
 * @nump: optional pointer filled in with the number written
 * @perm: visibility in sysfs
 *
 * Input and output are as comma-separated values.  Commas inside values
 * don't work properly (eg. an array of charp).
 *
 * ARRAY_SIZE(@name) is used to determine the number of elements in the
 * array, so the definition must be visible.
 */
#define module_param_array(name, type, nump, perm)		\
	module_param_array_named(name, name, type, nump, perm)

/**
 * module_param_array_named - renamed parameter which is an array of some type
 * @name: a valid C identifier which is the parameter name
 * @array: the name of the array variable
 * @type: the type, as per module_param()
 * @nump: optional pointer filled in with the number written
 * @perm: visibility in sysfs
 *
 * This exposes a different name than the actual variable name.  See
 * module_param_named() for why this might be necessary.
 */
#define module_param_array_named(name, array, type, nump, perm)		\
	param_check_##type(name, &(array)[0]);				\
	static const struct kparam_array __param_arr_##name		\
	= { .max = ARRAY_SIZE(array), .num = nump,                      \
	    .ops = &param_ops_##type,					\
	    .elemsize = sizeof(array[0]), .elem = array };		\
	__module_param_call(MODULE_PARAM_PREFIX, name,			\
			    &param_array_ops,				\
			    .arr = &__param_arr_##name,			\
			    perm, -1, 0);				\
	__MODULE_PARM_TYPE(name, "array of " #type)

extern struct kernel_param_ops param_array_ops;

extern struct kernel_param_ops param_ops_string;
extern int param_set_copystring(const char *val, const struct kernel_param *);
extern int param_get_string(char *buffer, const struct kernel_param *kp);

/* for exporting parameters in /sys/module/.../parameters */

struct module;

#if defined(CONFIG_SYSFS) && defined(CONFIG_MODULES)
extern int module_param_sysfs_setup(struct module *mod,
				    const struct kernel_param *kparam,
				    unsigned int num_params);

extern void module_param_sysfs_remove(struct module *mod);
#else
static inline int module_param_sysfs_setup(struct module *mod,
			     const struct kernel_param *kparam,
			     unsigned int num_params)
{
	return 0;
}

static inline void module_param_sysfs_remove(struct module *mod)
{ }
#endif

#endif /* _LINUX_MODULE_PARAMS_H */
