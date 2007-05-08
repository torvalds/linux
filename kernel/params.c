/* Helpers for initial module or kernel cmdline parsing
   Copyright (C) 2001 Rusty Russell.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt, a...)
#endif

static inline char dash2underscore(char c)
{
	if (c == '-')
		return '_';
	return c;
}

static inline int parameq(const char *input, const char *paramname)
{
	unsigned int i;
	for (i = 0; dash2underscore(input[i]) == paramname[i]; i++)
		if (input[i] == '\0')
			return 1;
	return 0;
}

static int parse_one(char *param,
		     char *val,
		     struct kernel_param *params, 
		     unsigned num_params,
		     int (*handle_unknown)(char *param, char *val))
{
	unsigned int i;

	/* Find parameter */
	for (i = 0; i < num_params; i++) {
		if (parameq(param, params[i].name)) {
			DEBUGP("They are equal!  Calling %p\n",
			       params[i].set);
			return params[i].set(val, &params[i]);
		}
	}

	if (handle_unknown) {
		DEBUGP("Unknown argument: calling %p\n", handle_unknown);
		return handle_unknown(param, val);
	}

	DEBUGP("Unknown argument `%s'\n", param);
	return -ENOENT;
}

/* You can use " around spaces, but can't escape ". */
/* Hyphens and underscores equivalent in parameter names. */
static char *next_arg(char *args, char **param, char **val)
{
	unsigned int i, equals = 0;
	int in_quote = 0, quoted = 0;
	char *next;

	if (*args == '"') {
		args++;
		in_quote = 1;
		quoted = 1;
	}

	for (i = 0; args[i]; i++) {
		if (args[i] == ' ' && !in_quote)
			break;
		if (equals == 0) {
			if (args[i] == '=')
				equals = i;
		}
		if (args[i] == '"')
			in_quote = !in_quote;
	}

	*param = args;
	if (!equals)
		*val = NULL;
	else {
		args[equals] = '\0';
		*val = args + equals + 1;

		/* Don't include quotes in value. */
		if (**val == '"') {
			(*val)++;
			if (args[i-1] == '"')
				args[i-1] = '\0';
		}
		if (quoted && args[i-1] == '"')
			args[i-1] = '\0';
	}

	if (args[i]) {
		args[i] = '\0';
		next = args + i + 1;
	} else
		next = args + i;

	/* Chew up trailing spaces. */
	while (*next == ' ')
		next++;
	return next;
}

/* Args looks like "foo=bar,bar2 baz=fuz wiz". */
int parse_args(const char *name,
	       char *args,
	       struct kernel_param *params,
	       unsigned num,
	       int (*unknown)(char *param, char *val))
{
	char *param, *val;

	DEBUGP("Parsing ARGS: %s\n", args);

	/* Chew leading spaces */
	while (*args == ' ')
		args++;

	while (*args) {
		int ret;
		int irq_was_disabled;

		args = next_arg(args, &param, &val);
		irq_was_disabled = irqs_disabled();
		ret = parse_one(param, val, params, num, unknown);
		if (irq_was_disabled && !irqs_disabled()) {
			printk(KERN_WARNING "parse_args(): option '%s' enabled "
					"irq's!\n", param);
		}
		switch (ret) {
		case -ENOENT:
			printk(KERN_ERR "%s: Unknown parameter `%s'\n",
			       name, param);
			return ret;
		case -ENOSPC:
			printk(KERN_ERR
			       "%s: `%s' too large for parameter `%s'\n",
			       name, val ?: "", param);
			return ret;
		case 0:
			break;
		default:
			printk(KERN_ERR
			       "%s: `%s' invalid for parameter `%s'\n",
			       name, val ?: "", param);
			return ret;
		}
	}

	/* All parsed OK. */
	return 0;
}

/* Lazy bastard, eh? */
#define STANDARD_PARAM_DEF(name, type, format, tmptype, strtolfn)      	\
	int param_set_##name(const char *val, struct kernel_param *kp)	\
	{								\
		char *endp;						\
		tmptype l;						\
									\
		if (!val) return -EINVAL;				\
		l = strtolfn(val, &endp, 0);				\
		if (endp == val || ((type)l != l))			\
			return -EINVAL;					\
		*((type *)kp->arg) = l;					\
		return 0;						\
	}								\
	int param_get_##name(char *buffer, struct kernel_param *kp)	\
	{								\
		return sprintf(buffer, format, *((type *)kp->arg));	\
	}

STANDARD_PARAM_DEF(byte, unsigned char, "%c", unsigned long, simple_strtoul);
STANDARD_PARAM_DEF(short, short, "%hi", long, simple_strtol);
STANDARD_PARAM_DEF(ushort, unsigned short, "%hu", unsigned long, simple_strtoul);
STANDARD_PARAM_DEF(int, int, "%i", long, simple_strtol);
STANDARD_PARAM_DEF(uint, unsigned int, "%u", unsigned long, simple_strtoul);
STANDARD_PARAM_DEF(long, long, "%li", long, simple_strtol);
STANDARD_PARAM_DEF(ulong, unsigned long, "%lu", unsigned long, simple_strtoul);

int param_set_charp(const char *val, struct kernel_param *kp)
{
	if (!val) {
		printk(KERN_ERR "%s: string parameter expected\n",
		       kp->name);
		return -EINVAL;
	}

	if (strlen(val) > 1024) {
		printk(KERN_ERR "%s: string parameter too long\n",
		       kp->name);
		return -ENOSPC;
	}

	*(char **)kp->arg = (char *)val;
	return 0;
}

int param_get_charp(char *buffer, struct kernel_param *kp)
{
	return sprintf(buffer, "%s", *((char **)kp->arg));
}

int param_set_bool(const char *val, struct kernel_param *kp)
{
	/* No equals means "set"... */
	if (!val) val = "1";

	/* One of =[yYnN01] */
	switch (val[0]) {
	case 'y': case 'Y': case '1':
		*(int *)kp->arg = 1;
		return 0;
	case 'n': case 'N': case '0':
		*(int *)kp->arg = 0;
		return 0;
	}
	return -EINVAL;
}

int param_get_bool(char *buffer, struct kernel_param *kp)
{
	/* Y and N chosen as being relatively non-coder friendly */
	return sprintf(buffer, "%c", (*(int *)kp->arg) ? 'Y' : 'N');
}

int param_set_invbool(const char *val, struct kernel_param *kp)
{
	int boolval, ret;
	struct kernel_param dummy = { .arg = &boolval };

	ret = param_set_bool(val, &dummy);
	if (ret == 0)
		*(int *)kp->arg = !boolval;
	return ret;
}

int param_get_invbool(char *buffer, struct kernel_param *kp)
{
	int val;
	struct kernel_param dummy = { .arg = &val };

	val = !*(int *)kp->arg;
	return param_get_bool(buffer, &dummy);
}

/* We break the rule and mangle the string. */
static int param_array(const char *name,
		       const char *val,
		       unsigned int min, unsigned int max,
		       void *elem, int elemsize,
		       int (*set)(const char *, struct kernel_param *kp),
		       int *num)
{
	int ret;
	struct kernel_param kp;
	char save;

	/* Get the name right for errors. */
	kp.name = name;
	kp.arg = elem;

	/* No equals sign? */
	if (!val) {
		printk(KERN_ERR "%s: expects arguments\n", name);
		return -EINVAL;
	}

	*num = 0;
	/* We expect a comma-separated list of values. */
	do {
		int len;

		if (*num == max) {
			printk(KERN_ERR "%s: can only take %i arguments\n",
			       name, max);
			return -EINVAL;
		}
		len = strcspn(val, ",");

		/* nul-terminate and parse */
		save = val[len];
		((char *)val)[len] = '\0';
		ret = set(val, &kp);

		if (ret != 0)
			return ret;
		kp.arg += elemsize;
		val += len+1;
		(*num)++;
	} while (save == ',');

	if (*num < min) {
		printk(KERN_ERR "%s: needs at least %i arguments\n",
		       name, min);
		return -EINVAL;
	}
	return 0;
}

int param_array_set(const char *val, struct kernel_param *kp)
{
	struct kparam_array *arr = kp->arg;
	unsigned int temp_num;

	return param_array(kp->name, val, 1, arr->max, arr->elem,
			   arr->elemsize, arr->set, arr->num ?: &temp_num);
}

int param_array_get(char *buffer, struct kernel_param *kp)
{
	int i, off, ret;
	struct kparam_array *arr = kp->arg;
	struct kernel_param p;

	p = *kp;
	for (i = off = 0; i < (arr->num ? *arr->num : arr->max); i++) {
		if (i)
			buffer[off++] = ',';
		p.arg = arr->elem + arr->elemsize * i;
		ret = arr->get(buffer + off, &p);
		if (ret < 0)
			return ret;
		off += ret;
	}
	buffer[off] = '\0';
	return off;
}

int param_set_copystring(const char *val, struct kernel_param *kp)
{
	struct kparam_string *kps = kp->arg;

	if (!val) {
		printk(KERN_ERR "%s: missing param set value\n", kp->name);
		return -EINVAL;
	}
	if (strlen(val)+1 > kps->maxlen) {
		printk(KERN_ERR "%s: string doesn't fit in %u chars.\n",
		       kp->name, kps->maxlen-1);
		return -ENOSPC;
	}
	strcpy(kps->string, val);
	return 0;
}

int param_get_string(char *buffer, struct kernel_param *kp)
{
	struct kparam_string *kps = kp->arg;
	return strlcpy(buffer, kps->string, kps->maxlen);
}

/* sysfs output in /sys/modules/XYZ/parameters/ */

extern struct kernel_param __start___param[], __stop___param[];

#define MAX_KBUILD_MODNAME KOBJ_NAME_LEN

struct param_attribute
{
	struct module_attribute mattr;
	struct kernel_param *param;
};

struct module_param_attrs
{
	struct attribute_group grp;
	struct param_attribute attrs[0];
};

#ifdef CONFIG_SYSFS
#define to_param_attr(n) container_of(n, struct param_attribute, mattr);

static ssize_t param_attr_show(struct module_attribute *mattr,
			       struct module *mod, char *buf)
{
	int count;
	struct param_attribute *attribute = to_param_attr(mattr);

	if (!attribute->param->get)
		return -EPERM;

	count = attribute->param->get(buf, attribute->param);
	if (count > 0) {
		strcat(buf, "\n");
		++count;
	}
	return count;
}

/* sysfs always hands a nul-terminated string in buf.  We rely on that. */
static ssize_t param_attr_store(struct module_attribute *mattr,
				struct module *owner,
				const char *buf, size_t len)
{
 	int err;
	struct param_attribute *attribute = to_param_attr(mattr);

	if (!attribute->param->set)
		return -EPERM;

	err = attribute->param->set(buf, attribute->param);
	if (!err)
		return len;
	return err;
}
#endif

#ifdef CONFIG_MODULES
#define __modinit
#else
#define __modinit __init
#endif

#ifdef CONFIG_SYSFS
/*
 * param_sysfs_setup - setup sysfs support for one module or KBUILD_MODNAME
 * @mk: struct module_kobject (contains parent kobject)
 * @kparam: array of struct kernel_param, the actual parameter definitions
 * @num_params: number of entries in array
 * @name_skip: offset where the parameter name start in kparam[].name. Needed for built-in "modules"
 *
 * Create a kobject for a (per-module) group of parameters, and create files
 * in sysfs. A pointer to the param_kobject is returned on success,
 * NULL if there's no parameter to export, or other ERR_PTR(err).
 */
static __modinit struct module_param_attrs *
param_sysfs_setup(struct module_kobject *mk,
		  struct kernel_param *kparam,
		  unsigned int num_params,
		  unsigned int name_skip)
{
	struct module_param_attrs *mp;
	unsigned int valid_attrs = 0;
	unsigned int i, size[2];
	struct param_attribute *pattr;
	struct attribute **gattr;
	int err;

	for (i=0; i<num_params; i++) {
		if (kparam[i].perm)
			valid_attrs++;
	}

	if (!valid_attrs)
		return NULL;

	size[0] = ALIGN(sizeof(*mp) +
			valid_attrs * sizeof(mp->attrs[0]),
			sizeof(mp->grp.attrs[0]));
	size[1] = (valid_attrs + 1) * sizeof(mp->grp.attrs[0]);

	mp = kmalloc(size[0] + size[1], GFP_KERNEL);
	if (!mp)
		return ERR_PTR(-ENOMEM);

	mp->grp.name = "parameters";
	mp->grp.attrs = (void *)mp + size[0];

	pattr = &mp->attrs[0];
	gattr = &mp->grp.attrs[0];
	for (i = 0; i < num_params; i++) {
		struct kernel_param *kp = &kparam[i];
		if (kp->perm) {
			pattr->param = kp;
			pattr->mattr.show = param_attr_show;
			pattr->mattr.store = param_attr_store;
			pattr->mattr.attr.name = (char *)&kp->name[name_skip];
			pattr->mattr.attr.owner = mk->mod;
			pattr->mattr.attr.mode = kp->perm;
			*(gattr++) = &(pattr++)->mattr.attr;
		}
	}
	*gattr = NULL;

	if ((err = sysfs_create_group(&mk->kobj, &mp->grp))) {
		kfree(mp);
		return ERR_PTR(err);
	}
	return mp;
}

#ifdef CONFIG_MODULES
/*
 * module_param_sysfs_setup - setup sysfs support for one module
 * @mod: module
 * @kparam: module parameters (array)
 * @num_params: number of module parameters
 *
 * Adds sysfs entries for module parameters, and creates a link from
 * /sys/module/[mod->name]/parameters to /sys/parameters/[mod->name]/
 */
int module_param_sysfs_setup(struct module *mod,
			     struct kernel_param *kparam,
			     unsigned int num_params)
{
	struct module_param_attrs *mp;

	mp = param_sysfs_setup(&mod->mkobj, kparam, num_params, 0);
	if (IS_ERR(mp))
		return PTR_ERR(mp);

	mod->param_attrs = mp;
	return 0;
}

/*
 * module_param_sysfs_remove - remove sysfs support for one module
 * @mod: module
 *
 * Remove sysfs entries for module parameters and the corresponding
 * kobject.
 */
void module_param_sysfs_remove(struct module *mod)
{
	if (mod->param_attrs) {
		sysfs_remove_group(&mod->mkobj.kobj,
				   &mod->param_attrs->grp);
		/* We are positive that no one is using any param
		 * attrs at this point.  Deallocate immediately. */
		kfree(mod->param_attrs);
		mod->param_attrs = NULL;
	}
}
#endif

/*
 * kernel_param_sysfs_setup - wrapper for built-in params support
 */
static void __init kernel_param_sysfs_setup(const char *name,
					    struct kernel_param *kparam,
					    unsigned int num_params,
					    unsigned int name_skip)
{
	struct module_kobject *mk;
	int ret;

	mk = kzalloc(sizeof(struct module_kobject), GFP_KERNEL);
	BUG_ON(!mk);

	mk->mod = THIS_MODULE;
	kobj_set_kset_s(mk, module_subsys);
	kobject_set_name(&mk->kobj, name);
	kobject_init(&mk->kobj);
	ret = kobject_add(&mk->kobj);
	BUG_ON(ret < 0);
	param_sysfs_setup(mk, kparam, num_params, name_skip);
	kobject_uevent(&mk->kobj, KOBJ_ADD);
}

/*
 * param_sysfs_builtin - add contents in /sys/parameters for built-in modules
 *
 * Add module_parameters to sysfs for "modules" built into the kernel.
 *
 * The "module" name (KBUILD_MODNAME) is stored before a dot, the
 * "parameter" name is stored behind a dot in kernel_param->name. So,
 * extract the "module" name for all built-in kernel_param-eters,
 * and for all who have the same, call kernel_param_sysfs_setup.
 */
static void __init param_sysfs_builtin(void)
{
	struct kernel_param *kp, *kp_begin = NULL;
	unsigned int i, name_len, count = 0;
	char modname[MAX_KBUILD_MODNAME + 1] = "";

	for (i=0; i < __stop___param - __start___param; i++) {
		char *dot;

		kp = &__start___param[i];

		/* We do not handle args without periods. */
		dot = memchr(kp->name, '.', MAX_KBUILD_MODNAME);
		if (!dot) {
			DEBUGP("couldn't find period in %s\n", kp->name);
			continue;
		}
		name_len = dot - kp->name;

 		/* new kbuild_modname? */
		if (strlen(modname) != name_len
		    || strncmp(modname, kp->name, name_len) != 0) {
			/* add a new kobject for previous kernel_params. */
			if (count)
				kernel_param_sysfs_setup(modname,
							 kp_begin,
							 count,
							 strlen(modname)+1);

			strncpy(modname, kp->name, name_len);
			modname[name_len] = '\0';
			count = 0;
			kp_begin = kp;
		}
		count++;
	}

	/* last kernel_params need to be registered as well */
	if (count)
		kernel_param_sysfs_setup(modname, kp_begin, count,
					 strlen(modname)+1);
}


/* module-related sysfs stuff */

#define to_module_attr(n) container_of(n, struct module_attribute, attr);
#define to_module_kobject(n) container_of(n, struct module_kobject, kobj);

static ssize_t module_attr_show(struct kobject *kobj,
				struct attribute *attr,
				char *buf)
{
	struct module_attribute *attribute;
	struct module_kobject *mk;
	int ret;

	attribute = to_module_attr(attr);
	mk = to_module_kobject(kobj);

	if (!attribute->show)
		return -EIO;

	ret = attribute->show(attribute, mk->mod, buf);

	return ret;
}

static ssize_t module_attr_store(struct kobject *kobj,
				struct attribute *attr,
				const char *buf, size_t len)
{
	struct module_attribute *attribute;
	struct module_kobject *mk;
	int ret;

	attribute = to_module_attr(attr);
	mk = to_module_kobject(kobj);

	if (!attribute->store)
		return -EIO;

	ret = attribute->store(attribute, mk->mod, buf, len);

	return ret;
}

static struct sysfs_ops module_sysfs_ops = {
	.show = module_attr_show,
	.store = module_attr_store,
};

static struct kobj_type module_ktype;

static int uevent_filter(struct kset *kset, struct kobject *kobj)
{
	struct kobj_type *ktype = get_ktype(kobj);

	if (ktype == &module_ktype)
		return 1;
	return 0;
}

static struct kset_uevent_ops module_uevent_ops = {
	.filter = uevent_filter,
};

decl_subsys(module, &module_ktype, &module_uevent_ops);
int module_sysfs_initialized;

static struct kobj_type module_ktype = {
	.sysfs_ops =	&module_sysfs_ops,
};

/*
 * param_sysfs_init - wrapper for built-in params support
 */
static int __init param_sysfs_init(void)
{
	int ret;

	ret = subsystem_register(&module_subsys);
	if (ret < 0) {
		printk(KERN_WARNING "%s (%d): subsystem_register error: %d\n",
			__FILE__, __LINE__, ret);
		return ret;
	}
	module_sysfs_initialized = 1;

	param_sysfs_builtin();

	return 0;
}
subsys_initcall(param_sysfs_init);

#else
#if 0
static struct sysfs_ops module_sysfs_ops = {
	.show = NULL,
	.store = NULL,
};
#endif
#endif

EXPORT_SYMBOL(param_set_byte);
EXPORT_SYMBOL(param_get_byte);
EXPORT_SYMBOL(param_set_short);
EXPORT_SYMBOL(param_get_short);
EXPORT_SYMBOL(param_set_ushort);
EXPORT_SYMBOL(param_get_ushort);
EXPORT_SYMBOL(param_set_int);
EXPORT_SYMBOL(param_get_int);
EXPORT_SYMBOL(param_set_uint);
EXPORT_SYMBOL(param_get_uint);
EXPORT_SYMBOL(param_set_long);
EXPORT_SYMBOL(param_get_long);
EXPORT_SYMBOL(param_set_ulong);
EXPORT_SYMBOL(param_get_ulong);
EXPORT_SYMBOL(param_set_charp);
EXPORT_SYMBOL(param_get_charp);
EXPORT_SYMBOL(param_set_bool);
EXPORT_SYMBOL(param_get_bool);
EXPORT_SYMBOL(param_set_invbool);
EXPORT_SYMBOL(param_get_invbool);
EXPORT_SYMBOL(param_array_set);
EXPORT_SYMBOL(param_array_get);
EXPORT_SYMBOL(param_set_copystring);
EXPORT_SYMBOL(param_get_string);
