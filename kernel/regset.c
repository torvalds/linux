// SPDX-License-Identifier: GPL-2.0-only
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/regset.h>

static int __regset_get(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int size,
			void **data)
{
	void *p = *data, *to_free = NULL;
	int res;

	if (!regset->get)
		return -EOPNOTSUPP;
	if (size > regset->n * regset->size)
		size = regset->n * regset->size;
	if (!p) {
		to_free = p = kzalloc(size, GFP_KERNEL);
		if (!p)
			return -ENOMEM;
	}
	res = regset->get(target, regset, 0, size, p, NULL);
	if (unlikely(res < 0)) {
		kfree(to_free);
		return res;
	}
	*data = p;
	if (regset->get_size) { // arm64-only kludge, will go away
		unsigned max_size = regset->get_size(target, regset);
		if (size > max_size)
			size = max_size;
	}
	return size;
}

int regset_get(struct task_struct *target,
	       const struct user_regset *regset,
	       unsigned int size,
	       void *data)
{
	return __regset_get(target, regset, size, &data);
}
EXPORT_SYMBOL(regset_get);

int regset_get_alloc(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int size,
		     void **data)
{
	*data = NULL;
	return __regset_get(target, regset, size, data);
}
EXPORT_SYMBOL(regset_get_alloc);
