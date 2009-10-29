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
#include <linux/ctype.h>

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
		if (isspace(args[i]) && !in_quote)
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
	while (isspace(*next))
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
	while (isspace(*args))
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
		tmptype l;						\
		int ret;						\
									\
		if (!val) return -EINVAL;				\
		ret = strtolfn(val, 0, &l);				\
		if (ret == -EINVAL || ((type)l != l))			\
			return -EINVAL;					\
		*((type *)kp->arg) = l;					\
		return 0;						\
	}								\
	int param_get_##name(char *buffer, struct kernel_param *kp)	\
	{								\
		return sprintf(buffer, format, *((type *)kp->arg));	\
	}

STANDARD_PARAM_DEF(byte, unsigned char, "%c", unsigned long, strict_strtoul);
STANDARD_PARAM_DEF(short, short, "%hi", long, strict_strtol);
STANDARD_PARAM_DEF(ushort, unsigned short, "%hu", unsigned long, strict_strtoul);
STANDARD_PARAM_DEF(int, int, "%i", long, strict_strtol);
STANDARD_PARAM_DEF(uint, unsigned int, "%u", unsigned long, strict_strtoul);
STANDARD_PARAM_DEF(long, long, "%li", long, strict_strtol);
STANDARD_PARAM_DEF(ulong, unsigned long, "%lu", unsigned long, strict_strtoul);

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

	/* This is a hack.  We can't need to strdup in early boot, and we
	 * don't need to; this mangled commandline is preserved. */
	if (slab_is_available()) {
		*(char **)kp->arg = kstrdup(val, GFP_KERNEL);
		if (!kp->arg)
			return -ENOMEM;
	} else
		*(const char **)kp->arg = val;

	return 0;
}

int param_get_charp(char *buffer, struct kernel_param *kp)
{
	return sprintf(buffer, "%s", *((char **)kp->arg));
}

/* Actually could be a bool or an int, for historical reasons. */
int param_set_bool(const char *val, struct kernel_param *kp)
{
	bool v;

	/* No equals means "set"... */
	if (!val) val = "1";

	/* One of =[yYnN01] */
	switch (val[0]) {
	case 'y': case 'Y': case '1':
		v = true;
		break;
	case 'n': case 'N': case '0':
		v = false;
		break;
	default:
		return -EINVAL;
	}

	if (kp->flags & KPARAM_ISBOOL)
		*(bool *)kp->arg = v;
	else
		*(int *)kp->arg = v;
	return 0;
}

int param_get_bool(char *buffer, struct kernel_param *kp)
{
	bool val;
	if (kp->flags & KPARAM_ISBOOL)
		val = *(bool *)kp->arg;
	else
		val = *(int *)kp->arg;

	/* Y and N chosen as being relatively non-coder friendly */
	return sprintf(buffer, "%c", val ? 'Y' : 'N');
}

/* This one must be bool. */
int param_set_invbool(const char *val, struct kernel_param *kp)
{
	int ret;
	bool boolval;
	struct kernel_param dummy;

	dummy.arg = &boolval;
	dummy.flags = KPARAM_ISBOOL;
	ret = param_set_bool(val, &dummy);
	if (ret == 0)
		*(bool *)kp->arg = !boolval;
	return ret;
}

int param_get_invbool(char *buffer, struct kernel_param *kp)
{
	return sprintf(buffer, "%c", (*(bool *)kp->arg) ? 'N' : 'Y');
}

/* We break the rule and mangle the string. */
static int param_array(const char *name,
		       const char *val,
		       unsigned int min, unsigned int max,
		       void *elem, int elemsize,
		       int (*set)(const char *, struct kernel_param *kp),
		       unsigned int *num)
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
	const struct kparam_array *arr = kp->arr;
	unsigned int temp_num;

	return param_array(kp->name, val, 1, arr->max, arr->elem,
			   arr->elemsize, arr->set, arr->num ?: &temp_num);
}

int param_array_get(char *buffer, struct kernel_param *kp)
{
	int i, off, ret;
	const struct kparam_array *arr = kp->arr;
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
	const struct kparam_string *kps = kp->str;

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
	const struct kparam_string *kps = kp->str;
	return strlcpy(buffer, kps->string, kps->maxlen);
}

/* sysfs output in /sys/modules/XYZ/parameters/ */
#define to_module_attr(n) container_of(n, struct module_attribute, attr);
#define to_module_kobject(n) container_of(n, struct module_kobject, kobj);

extern struct kernel_param __start___param[], __stop___param[];

struct param_attribute
{
	struct module_attribute mattr;
	struct kernel_param *param;
};

struct module_param_attrs
{
	unsigned int num;
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
 * add_sysfs_param - add a parameter to sysfs
 * @mk: struct module_kobject
 * @kparam: the actual parameter definition to add to sysfs
 * @name: name of parameter
 *
 * Create a kobject if for a (per-module) parameter if mp NULL, and
 * create file in sysfs.  Returns an error on out of memory.  Always cleans up
 * if there's an error.
 */
static __modinit int add_sysfs_param(struct module_kobject *mk,
				     struct kernel_param *kp,
				     const char *name)
{
	struct module_param_attrs *new;
	struct attribute **attrs;
	int err, num;

	/* We don't bother calling this with invisible parameters. */
	BUG_ON(!kp->perm);

	if (!mk->mp) {
		num = 0;
		attrs = NULL;
	} else {
		num = mk->mp->num;
		attrs = mk->mp->grp.attrs;
	}

	/* Enlarge. */
	new = krealloc(mk->mp,
		       sizeof(*mk->mp) + sizeof(mk->mp->attrs[0]) * (num+1),
		       GFP_KERNEL);
	if (!new) {
		kfree(mk->mp);
		err = -ENOMEM;
		goto fail;
	}
	attrs = krealloc(attrs, sizeof(new->grp.attrs[0])*(num+2), GFP_KERNEL);
	if (!attrs) {
		err = -ENOMEM;
		goto fail_free_new;
	}

	/* Sysfs wants everything zeroed. */
	memset(new, 0, sizeof(*new));
	memset(&new->attrs[num], 0, sizeof(new->attrs[num]));
	memset(&attrs[num], 0, sizeof(attrs[num]));
	new->grp.name = "parameters";
	new->grp.attrs = attrs;

	/* Tack new one on the end. */
	new->attrs[num].param = kp;
	new->attrs[num].mattr.show = param_attr_show;
	new->attrs[num].mattr.store = param_attr_store;
	new->attrs[num].mattr.attr.name = (char *)name;
	new->attrs[num].mattr.attr.mode = kp->perm;
	new->num = num+1;

	/* Fix up all the pointers, since krealloc can move us */
	for (num = 0; num < new->num; num++)
		new->grp.attrs[num] = &new->attrs[num].mattr.attr;
	new->grp.attrs[num] = NULL;

	mk->mp = new;
	return 0;

fail_free_new:
	kfree(new);
fail:
	mk->mp = NULL;
	return err;
}

#ifdef CONFIG_MODULES
static void free_module_param_attrs(struct module_kobject *mk)
{
	kfree(mk->mp->grp.attrs);
	kfree(mk->mp);
	mk->mp = NULL;
}

/*
 * module_param_sysfs_setup - setup sysfs support for one module
 * @mod: module
 * @kparam: module parameters (array)
 * @num_params: number of module parameters
 *
 * Adds sysfs entries for module parameters under
 * /sys/module/[mod->name]/parameters/
 */
int module_param_sysfs_setup(struct module *mod,
			     struct kernel_param *kparam,
			     unsigned int num_params)
{
	int i, err;
	bool params = false;

	for (i = 0; i < num_params; i++) {
		if (kparam[i].perm == 0)
			continue;
		err = add_sysfs_param(&mod->mkobj, &kparam[i], kparam[i].name);
		if (err)
			return err;
		params = true;
	}

	if (!params)
		return 0;

	/* Create the param group. */
	err = sysfs_create_group(&mod->mkobj.kobj, &mod->mkobj.mp->grp);
	if (err)
		free_module_param_attrs(&mod->mkobj);
	return err;
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
	if (mod->mkobj.mp) {
		sysfs_remove_group(&mod->mkobj.kobj, &mod->mkobj.mp->grp);
		/* We are positive that no one is using any param
		 * attrs at this point.  Deallocate immediately. */
		free_module_param_attrs(&mod->mkobj);
	}
}
#endif

void destroy_params(const struct kernel_param *params, unsigned num)
{
	/* FIXME: This should free kmalloced charp parameters.  It doesn't. */
}

static void __init kernel_add_sysfs_param(const char *name,
					  struct kernel_param *kparam,
					  unsigned int name_skip)
{
	struct module_kobject *mk;
	struct kobject *kobj;
	int err;

	kobj = kset_find_obj(module_kset, name);
	if (kobj) {
		/* We already have one.  Remove params so we can add more. */
		mk = to_module_kobject(kobj);
		/* We need to remove it before adding parameters. */
		sysfs_remove_group(&mk->kobj, &mk->mp->grp);
	} else {
		mk = kzalloc(sizeof(struct module_kobject), GFP_KERNEL);
		BUG_ON(!mk);

		mk->mod = THIS_MODULE;
		mk->kobj.kset = module_kset;
		err = kobject_init_and_add(&mk->kobj, &module_ktype, NULL,
					   "%s", name);
		if (err) {
			kobject_put(&mk->kobj);
			printk(KERN_ERR "Module '%s' failed add to sysfs, "
			       "error number %d\n", name, err);
			printk(KERN_ERR	"The system will be unstable now.\n");
			return;
		}
		/* So that exit path is even. */
		kobject_get(&mk->kobj);
	}

	/* These should not fail at boot. */
	err = add_sysfs_param(mk, kparam, kparam->name + name_skip);
	BUG_ON(err);
	err = sysfs_create_group(&mk->kobj, &mk->mp->grp);
	BUG_ON(err);
	kobject_uevent(&mk->kobj, KOBJ_ADD);
	kobject_put(&mk->kobj);
}

/*
 * param_sysfs_builtin - add contents in /sys/parameters for built-in modules
 *
 * Add module_parameters to sysfs for "modules" built into the kernel.
 *
 * The "module" name (KBUILD_MODNAME) is stored before a dot, the
 * "parameter" name is stored behind a dot in kernel_param->name. So,
 * extract the "module" name for all built-in kernel_param-eters,
 * and for all who have the same, call kernel_add_sysfs_param.
 */
static void __init param_sysfs_builtin(void)
{
	struct kernel_param *kp;
	unsigned int name_len;
	char modname[MODULE_NAME_LEN];

	for (kp = __start___param; kp < __stop___param; kp++) {
		char *dot;

		if (kp->perm == 0)
			continue;

		dot = strchr(kp->name, '.');
		if (!dot) {
			/* This happens for core_param() */
			strcpy(modname, "kernel");
			name_len = 0;
		} else {
			name_len = dot - kp->name + 1;
			strlcpy(modname, kp->name, name_len);
		}
		kernel_add_sysfs_param(modname, kp, name_len);
	}
}


/* module-related sysfs stuff */

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

struct kset *module_kset;
int module_sysfs_initialized;

struct kobj_type module_ktype = {
	.sysfs_ops =	&module_sysfs_ops,
};

/*
 * param_sysfs_init - wrapper for built-in params support
 */
static int __init param_sysfs_init(void)
{
	module_kset = kset_create_and_add("module", &module_uevent_ops, NULL);
	if (!module_kset) {
		printk(KERN_WARNING "%s (%d): error creating kset\n",
			__FILE__, __LINE__);
		return -ENOMEM;
	}
	module_sysfs_initialized = 1;

	param_sysfs_builtin();

	return 0;
}
subsys_initcall(param_sysfs_init);

#endif /* CONFIG_SYSFS */

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
