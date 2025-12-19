// SPDX-License-Identifier: GPL-2.0
/*
 * Init code for a livepatch kernel module
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/livepatch.h>

extern struct klp_object_ext __start_klp_objects[];
extern struct klp_object_ext __stop_klp_objects[];

static struct klp_patch *patch;

static int __init livepatch_mod_init(void)
{
	struct klp_object *objs;
	unsigned int nr_objs;
	int ret;

	nr_objs = __stop_klp_objects - __start_klp_objects;

	if (!nr_objs) {
		pr_err("nothing to patch!\n");
		ret = -EINVAL;
		goto err;
	}

	patch = kzalloc(sizeof(*patch), GFP_KERNEL);
	if (!patch) {
		ret = -ENOMEM;
		goto err;
	}

	objs = kzalloc(sizeof(struct klp_object) * (nr_objs + 1),  GFP_KERNEL);
	if (!objs) {
		ret = -ENOMEM;
		goto err_free_patch;
	}

	for (int i = 0; i < nr_objs; i++) {
		struct klp_object_ext *obj_ext = __start_klp_objects + i;
		struct klp_func_ext *funcs_ext = obj_ext->funcs;
		unsigned int nr_funcs = obj_ext->nr_funcs;
		struct klp_func *funcs = objs[i].funcs;
		struct klp_object *obj = objs + i;

		funcs = kzalloc(sizeof(struct klp_func) * (nr_funcs + 1), GFP_KERNEL);
		if (!funcs) {
			ret = -ENOMEM;
			for (int j = 0; j < i; j++)
				kfree(objs[i].funcs);
			goto err_free_objs;
		}

		for (int j = 0; j < nr_funcs; j++) {
			funcs[j].old_name   = funcs_ext[j].old_name;
			funcs[j].new_func   = funcs_ext[j].new_func;
			funcs[j].old_sympos = funcs_ext[j].sympos;
		}

		obj->name = obj_ext->name;
		obj->funcs = funcs;

		memcpy(&obj->callbacks, &obj_ext->callbacks, sizeof(struct klp_callbacks));
	}

	patch->mod = THIS_MODULE;
	patch->objs = objs;

	/* TODO patch->states */

#ifdef KLP_NO_REPLACE
	patch->replace = false;
#else
	patch->replace = true;
#endif

	return klp_enable_patch(patch);

err_free_objs:
	kfree(objs);
err_free_patch:
	kfree(patch);
err:
	return ret;
}

static void __exit livepatch_mod_exit(void)
{
	unsigned int nr_objs;

	nr_objs = __stop_klp_objects - __start_klp_objects;

	for (int i = 0; i < nr_objs; i++)
		kfree(patch->objs[i].funcs);

	kfree(patch->objs);
	kfree(patch);
}

module_init(livepatch_mod_init);
module_exit(livepatch_mod_exit);
MODULE_LICENSE("GPL");
MODULE_INFO(livepatch, "Y");
MODULE_DESCRIPTION("Livepatch module");
