// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Module sysfs support
 *
 * Copyright (C) 2008 Rusty Russell
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/kallsyms.h>
#include <linux/mutex.h>
#include "internal.h"

/*
 * /sys/module/foo/sections stuff
 * J. Corbet <corbet@lwn.net>
 */
#ifdef CONFIG_KALLSYMS
struct module_sect_attr {
	struct bin_attribute battr;
	unsigned long address;
};

struct module_sect_attrs {
	struct attribute_group grp;
	unsigned int nsections;
	struct module_sect_attr attrs[];
};

#define MODULE_SECT_READ_SIZE (3 /* "0x", "\n" */ + (BITS_PER_LONG / 4))
static ssize_t module_sect_read(struct file *file, struct kobject *kobj,
				struct bin_attribute *battr,
				char *buf, loff_t pos, size_t count)
{
	struct module_sect_attr *sattr =
		container_of(battr, struct module_sect_attr, battr);
	char bounce[MODULE_SECT_READ_SIZE + 1];
	size_t wrote;

	if (pos != 0)
		return -EINVAL;

	/*
	 * Since we're a binary read handler, we must account for the
	 * trailing NUL byte that sprintf will write: if "buf" is
	 * too small to hold the NUL, or the NUL is exactly the last
	 * byte, the read will look like it got truncated by one byte.
	 * Since there is no way to ask sprintf nicely to not write
	 * the NUL, we have to use a bounce buffer.
	 */
	wrote = scnprintf(bounce, sizeof(bounce), "0x%px\n",
			  kallsyms_show_value(file->f_cred)
				? (void *)sattr->address : NULL);
	count = min(count, wrote);
	memcpy(buf, bounce, count);

	return count;
}

static void free_sect_attrs(struct module_sect_attrs *sect_attrs)
{
	unsigned int section;

	for (section = 0; section < sect_attrs->nsections; section++)
		kfree(sect_attrs->attrs[section].battr.attr.name);
	kfree(sect_attrs);
}

static void add_sect_attrs(struct module *mod, const struct load_info *info)
{
	unsigned int nloaded = 0, i, size[2];
	struct module_sect_attrs *sect_attrs;
	struct module_sect_attr *sattr;
	struct bin_attribute **gattr;

	/* Count loaded sections and allocate structures */
	for (i = 0; i < info->hdr->e_shnum; i++)
		if (!sect_empty(&info->sechdrs[i]))
			nloaded++;
	size[0] = ALIGN(struct_size(sect_attrs, attrs, nloaded),
			sizeof(sect_attrs->grp.bin_attrs[0]));
	size[1] = (nloaded + 1) * sizeof(sect_attrs->grp.bin_attrs[0]);
	sect_attrs = kzalloc(size[0] + size[1], GFP_KERNEL);
	if (!sect_attrs)
		return;

	/* Setup section attributes. */
	sect_attrs->grp.name = "sections";
	sect_attrs->grp.bin_attrs = (void *)sect_attrs + size[0];

	sect_attrs->nsections = 0;
	sattr = &sect_attrs->attrs[0];
	gattr = &sect_attrs->grp.bin_attrs[0];
	for (i = 0; i < info->hdr->e_shnum; i++) {
		Elf_Shdr *sec = &info->sechdrs[i];

		if (sect_empty(sec))
			continue;
		sysfs_bin_attr_init(&sattr->battr);
		sattr->address = sec->sh_addr;
		sattr->battr.attr.name =
			kstrdup(info->secstrings + sec->sh_name, GFP_KERNEL);
		if (!sattr->battr.attr.name)
			goto out;
		sect_attrs->nsections++;
		sattr->battr.read = module_sect_read;
		sattr->battr.size = MODULE_SECT_READ_SIZE;
		sattr->battr.attr.mode = 0400;
		*(gattr++) = &(sattr++)->battr;
	}
	*gattr = NULL;

	if (sysfs_create_group(&mod->mkobj.kobj, &sect_attrs->grp))
		goto out;

	mod->sect_attrs = sect_attrs;
	return;
out:
	free_sect_attrs(sect_attrs);
}

static void remove_sect_attrs(struct module *mod)
{
	if (mod->sect_attrs) {
		sysfs_remove_group(&mod->mkobj.kobj,
				   &mod->sect_attrs->grp);
		/*
		 * We are positive that no one is using any sect attrs
		 * at this point.  Deallocate immediately.
		 */
		free_sect_attrs(mod->sect_attrs);
		mod->sect_attrs = NULL;
	}
}

/*
 * /sys/module/foo/notes/.section.name gives contents of SHT_NOTE sections.
 */

struct module_notes_attrs {
	struct kobject *dir;
	unsigned int notes;
	struct bin_attribute attrs[];
};

static ssize_t module_notes_read(struct file *filp, struct kobject *kobj,
				 struct bin_attribute *bin_attr,
				 char *buf, loff_t pos, size_t count)
{
	/*
	 * The caller checked the pos and count against our size.
	 */
	memcpy(buf, bin_attr->private + pos, count);
	return count;
}

static void free_notes_attrs(struct module_notes_attrs *notes_attrs,
			     unsigned int i)
{
	if (notes_attrs->dir) {
		while (i-- > 0)
			sysfs_remove_bin_file(notes_attrs->dir,
					      &notes_attrs->attrs[i]);
		kobject_put(notes_attrs->dir);
	}
	kfree(notes_attrs);
}

static void add_notes_attrs(struct module *mod, const struct load_info *info)
{
	unsigned int notes, loaded, i;
	struct module_notes_attrs *notes_attrs;
	struct bin_attribute *nattr;

	/* failed to create section attributes, so can't create notes */
	if (!mod->sect_attrs)
		return;

	/* Count notes sections and allocate structures.  */
	notes = 0;
	for (i = 0; i < info->hdr->e_shnum; i++)
		if (!sect_empty(&info->sechdrs[i]) &&
		    info->sechdrs[i].sh_type == SHT_NOTE)
			++notes;

	if (notes == 0)
		return;

	notes_attrs = kzalloc(struct_size(notes_attrs, attrs, notes),
			      GFP_KERNEL);
	if (!notes_attrs)
		return;

	notes_attrs->notes = notes;
	nattr = &notes_attrs->attrs[0];
	for (loaded = i = 0; i < info->hdr->e_shnum; ++i) {
		if (sect_empty(&info->sechdrs[i]))
			continue;
		if (info->sechdrs[i].sh_type == SHT_NOTE) {
			sysfs_bin_attr_init(nattr);
			nattr->attr.name = mod->sect_attrs->attrs[loaded].battr.attr.name;
			nattr->attr.mode = 0444;
			nattr->size = info->sechdrs[i].sh_size;
			nattr->private = (void *)info->sechdrs[i].sh_addr;
			nattr->read = module_notes_read;
			++nattr;
		}
		++loaded;
	}

	notes_attrs->dir = kobject_create_and_add("notes", &mod->mkobj.kobj);
	if (!notes_attrs->dir)
		goto out;

	for (i = 0; i < notes; ++i)
		if (sysfs_create_bin_file(notes_attrs->dir,
					  &notes_attrs->attrs[i]))
			goto out;

	mod->notes_attrs = notes_attrs;
	return;

out:
	free_notes_attrs(notes_attrs, i);
}

static void remove_notes_attrs(struct module *mod)
{
	if (mod->notes_attrs)
		free_notes_attrs(mod->notes_attrs, mod->notes_attrs->notes);
}

#else /* !CONFIG_KALLSYMS */
static inline void add_sect_attrs(struct module *mod, const struct load_info *info) { }
static inline void remove_sect_attrs(struct module *mod) { }
static inline void add_notes_attrs(struct module *mod, const struct load_info *info) { }
static inline void remove_notes_attrs(struct module *mod) { }
#endif /* CONFIG_KALLSYMS */

static void del_usage_links(struct module *mod)
{
#ifdef CONFIG_MODULE_UNLOAD
	struct module_use *use;

	mutex_lock(&module_mutex);
	list_for_each_entry(use, &mod->target_list, target_list)
		sysfs_remove_link(use->target->holders_dir, mod->name);
	mutex_unlock(&module_mutex);
#endif
}

static int add_usage_links(struct module *mod)
{
	int ret = 0;
#ifdef CONFIG_MODULE_UNLOAD
	struct module_use *use;

	mutex_lock(&module_mutex);
	list_for_each_entry(use, &mod->target_list, target_list) {
		ret = sysfs_create_link(use->target->holders_dir,
					&mod->mkobj.kobj, mod->name);
		if (ret)
			break;
	}
	mutex_unlock(&module_mutex);
	if (ret)
		del_usage_links(mod);
#endif
	return ret;
}

static void module_remove_modinfo_attrs(struct module *mod, int end)
{
	struct module_attribute *attr;
	int i;

	for (i = 0; (attr = &mod->modinfo_attrs[i]); i++) {
		if (end >= 0 && i > end)
			break;
		/* pick a field to test for end of list */
		if (!attr->attr.name)
			break;
		sysfs_remove_file(&mod->mkobj.kobj, &attr->attr);
		if (attr->free)
			attr->free(mod);
	}
	kfree(mod->modinfo_attrs);
}

static int module_add_modinfo_attrs(struct module *mod)
{
	struct module_attribute *attr;
	struct module_attribute *temp_attr;
	int error = 0;
	int i;

	mod->modinfo_attrs = kzalloc((sizeof(struct module_attribute) *
					(modinfo_attrs_count + 1)),
					GFP_KERNEL);
	if (!mod->modinfo_attrs)
		return -ENOMEM;

	temp_attr = mod->modinfo_attrs;
	for (i = 0; (attr = modinfo_attrs[i]); i++) {
		if (!attr->test || attr->test(mod)) {
			memcpy(temp_attr, attr, sizeof(*temp_attr));
			sysfs_attr_init(&temp_attr->attr);
			error = sysfs_create_file(&mod->mkobj.kobj,
						  &temp_attr->attr);
			if (error)
				goto error_out;
			++temp_attr;
		}
	}

	return 0;

error_out:
	if (i > 0)
		module_remove_modinfo_attrs(mod, --i);
	else
		kfree(mod->modinfo_attrs);
	return error;
}

static void mod_kobject_put(struct module *mod)
{
	DECLARE_COMPLETION_ONSTACK(c);

	mod->mkobj.kobj_completion = &c;
	kobject_put(&mod->mkobj.kobj);
	wait_for_completion(&c);
}

static int mod_sysfs_init(struct module *mod)
{
	int err;
	struct kobject *kobj;

	if (!module_kset) {
		pr_err("%s: module sysfs not initialized\n", mod->name);
		err = -EINVAL;
		goto out;
	}

	kobj = kset_find_obj(module_kset, mod->name);
	if (kobj) {
		pr_err("%s: module is already loaded\n", mod->name);
		kobject_put(kobj);
		err = -EINVAL;
		goto out;
	}

	mod->mkobj.mod = mod;

	memset(&mod->mkobj.kobj, 0, sizeof(mod->mkobj.kobj));
	mod->mkobj.kobj.kset = module_kset;
	err = kobject_init_and_add(&mod->mkobj.kobj, &module_ktype, NULL,
				   "%s", mod->name);
	if (err)
		mod_kobject_put(mod);

out:
	return err;
}

int mod_sysfs_setup(struct module *mod,
		    const struct load_info *info,
			   struct kernel_param *kparam,
			   unsigned int num_params)
{
	int err;

	err = mod_sysfs_init(mod);
	if (err)
		goto out;

	mod->holders_dir = kobject_create_and_add("holders", &mod->mkobj.kobj);
	if (!mod->holders_dir) {
		err = -ENOMEM;
		goto out_unreg;
	}

	err = module_param_sysfs_setup(mod, kparam, num_params);
	if (err)
		goto out_unreg_holders;

	err = module_add_modinfo_attrs(mod);
	if (err)
		goto out_unreg_param;

	err = add_usage_links(mod);
	if (err)
		goto out_unreg_modinfo_attrs;

	add_sect_attrs(mod, info);
	add_notes_attrs(mod, info);

	return 0;

out_unreg_modinfo_attrs:
	module_remove_modinfo_attrs(mod, -1);
out_unreg_param:
	module_param_sysfs_remove(mod);
out_unreg_holders:
	kobject_put(mod->holders_dir);
out_unreg:
	mod_kobject_put(mod);
out:
	return err;
}

static void mod_sysfs_fini(struct module *mod)
{
	remove_notes_attrs(mod);
	remove_sect_attrs(mod);
	mod_kobject_put(mod);
}

void mod_sysfs_teardown(struct module *mod)
{
	del_usage_links(mod);
	module_remove_modinfo_attrs(mod, -1);
	module_param_sysfs_remove(mod);
	kobject_put(mod->mkobj.drivers_dir);
	kobject_put(mod->holders_dir);
	mod_sysfs_fini(mod);
}

void init_param_lock(struct module *mod)
{
	mutex_init(&mod->param_lock);
}
