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
#define ___module_cat(a,b) __mod_ ## a ## b
#define __module_cat(a,b) ___module_cat(a,b)
#define __MODULE_INFO(tag, name, info)					  \
static const char __module_cat(name,__LINE__)[]				  \
  __used								  \
  __attribute__((section(".modinfo"),unused)) = __stringify(tag) "=" info
#else  /* !MODULE */
#define __MODULE_INFO(tag, name, info)
#endif
#define __MODULE_PARM_TYPE(name, _type)					  \
  __MODULE_INFO(parmtype, name##type, #name ":" _type)

struct kernel_param;

/* Returns 0, or -errno.  arg is in kp->arg. */
typedef int (*param_set_fn)(const char *val, struct kernel_param *kp);
/* Returns length written or -errno.  Buffer is 4k (ie. be short!) */
typedef int (*param_get_fn)(char *buffer, struct kernel_param *kp);

struct kernel_param {
	const char *name;
	unsigned int perm;
	param_set_fn set;
	param_get_fn get;
	union {
		void *arg;
		const struct kparam_string *str;
		const struct kparam_array *arr;
	};
};

/* Special one for strings we want to copy into */
struct kparam_string {
	unsigned int maxlen;
	char *string;
};

/* Special one for arrays */
struct kparam_array
{
	unsigned int max;
	unsigned int *num;
	param_set_fn set;
	param_get_fn get;
	unsigned int elemsize;
	void *elem;
};

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
   parameters.  perm sets the visibility in sysfs: 000 means it's
   not there, read bits mean it's readable, write bits mean it's
   writable. */
#define __module_param_call(prefix, name, set, get, arg, perm)		\
	/* Default value instead of permissions? */			\
	static int __param_perm_check_##name __attribute__((unused)) =	\
	BUILD_BUG_ON_ZERO((perm) < 0 || (perm) > 0777 || ((perm) & 2))	\
	+ BUILD_BUG_ON_ZERO(sizeof(""prefix) > MAX_PARAM_PREFIX_LEN);	\
	static const char __param_str_##name[] = prefix #name;		\
	static struct kernel_param __moduleparam_const __param_##name	\
	__used								\
    __attribute__ ((unused,__section__ ("__param"),aligned(sizeof(void *)))) \
	= { __param_str_##name, perm, set, get, { arg } }

#define module_param_call(name, set, get, arg, perm)			      \
	__module_param_call(MODULE_PARAM_PREFIX, name, set, get, arg, perm)

/* Helper functions: type is byte, short, ushort, int, uint, long,
   ulong, charp, bool or invbool, or XXX if you define param_get_XXX,
   param_set_XXX and param_check_XXX. */
#define module_param_named(name, value, type, perm)			   \
	param_check_##type(name, &(value));				   \
	module_param_call(name, param_set_##type, param_get_##type, &value, perm); \
	__MODULE_PARM_TYPE(name, #type)

#define module_param(name, type, perm)				\
	module_param_named(name, name, type, perm)

#ifndef MODULE
/**
 * core_param - define a historical core kernel parameter.
 * @name: the name of the cmdline and sysfs parameter (often the same as var)
 * @var: the variable
 * @type: the type (for param_set_##type and param_get_##type)
 * @perm: visibility in sysfs
 *
 * core_param is just like module_param(), but cannot be modular and
 * doesn't add a prefix (such as "printk.").  This is for compatibility
 * with __setup(), and it makes sense as truly core parameters aren't
 * tied to the particular file they're in.
 */
#define core_param(name, var, type, perm)				\
	param_check_##type(name, &(var));				\
	__module_param_call("", name, param_set_##type, param_get_##type, \
			    &var, perm)
#endif /* !MODULE */

/* Actually copy string: maxlen param is usually sizeof(string). */
#define module_param_string(name, string, len, perm)			\
	static const struct kparam_string __param_string_##name		\
		= { len, string };					\
	module_param_call(name, param_set_copystring, param_get_string,	\
			  .str = &__param_string_##name, perm);		\
	__MODULE_PARM_TYPE(name, "string")

/* Called on module insert or kernel boot */
extern int parse_args(const char *name,
		      char *args,
		      struct kernel_param *params,
		      unsigned num,
		      int (*unknown)(char *param, char *val));

/* All the helper functions */
/* The macros to do compile-time type checking stolen from Jakub
   Jelinek, who IIRC came up with this idea for the 2.4 module init code. */
#define __param_check(name, p, type) \
	static inline type *__check_##name(void) { return(p); }

extern int param_set_byte(const char *val, struct kernel_param *kp);
extern int param_get_byte(char *buffer, struct kernel_param *kp);
#define param_check_byte(name, p) __param_check(name, p, unsigned char)

extern int param_set_short(const char *val, struct kernel_param *kp);
extern int param_get_short(char *buffer, struct kernel_param *kp);
#define param_check_short(name, p) __param_check(name, p, short)

extern int param_set_ushort(const char *val, struct kernel_param *kp);
extern int param_get_ushort(char *buffer, struct kernel_param *kp);
#define param_check_ushort(name, p) __param_check(name, p, unsigned short)

extern int param_set_int(const char *val, struct kernel_param *kp);
extern int param_get_int(char *buffer, struct kernel_param *kp);
#define param_check_int(name, p) __param_check(name, p, int)

extern int param_set_uint(const char *val, struct kernel_param *kp);
extern int param_get_uint(char *buffer, struct kernel_param *kp);
#define param_check_uint(name, p) __param_check(name, p, unsigned int)

extern int param_set_long(const char *val, struct kernel_param *kp);
extern int param_get_long(char *buffer, struct kernel_param *kp);
#define param_check_long(name, p) __param_check(name, p, long)

extern int param_set_ulong(const char *val, struct kernel_param *kp);
extern int param_get_ulong(char *buffer, struct kernel_param *kp);
#define param_check_ulong(name, p) __param_check(name, p, unsigned long)

extern int param_set_charp(const char *val, struct kernel_param *kp);
extern int param_get_charp(char *buffer, struct kernel_param *kp);
#define param_check_charp(name, p) __param_check(name, p, char *)

extern int param_set_bool(const char *val, struct kernel_param *kp);
extern int param_get_bool(char *buffer, struct kernel_param *kp);
#define param_check_bool(name, p) __param_check(name, p, int)

extern int param_set_invbool(const char *val, struct kernel_param *kp);
extern int param_get_invbool(char *buffer, struct kernel_param *kp);
#define param_check_invbool(name, p) __param_check(name, p, int)

/* Comma-separated array: *nump is set to number they actually specified. */
#define module_param_array_named(name, array, type, nump, perm)		\
	static const struct kparam_array __param_arr_##name		\
	= { ARRAY_SIZE(array), nump, param_set_##type, param_get_##type,\
	    sizeof(array[0]), array };					\
	module_param_call(name, param_array_set, param_array_get, 	\
			  .arr = &__param_arr_##name, perm);		\
	__MODULE_PARM_TYPE(name, "array of " #type)

#define module_param_array(name, type, nump, perm)		\
	module_param_array_named(name, name, type, nump, perm)

extern int param_array_set(const char *val, struct kernel_param *kp);
extern int param_array_get(char *buffer, struct kernel_param *kp);

extern int param_set_copystring(const char *val, struct kernel_param *kp);
extern int param_get_string(char *buffer, struct kernel_param *kp);

/* for exporting parameters in /sys/parameters */

struct module;

#if defined(CONFIG_SYSFS) && defined(CONFIG_MODULES)
extern int module_param_sysfs_setup(struct module *mod,
				    struct kernel_param *kparam,
				    unsigned int num_params);

extern void module_param_sysfs_remove(struct module *mod);
#else
static inline int module_param_sysfs_setup(struct module *mod,
			     struct kernel_param *kparam,
			     unsigned int num_params)
{
	return 0;
}

static inline void module_param_sysfs_remove(struct module *mod)
{ }
#endif

#endif /* _LINUX_MODULE_PARAMS_H */
