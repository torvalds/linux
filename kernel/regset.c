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

	if (!regset->regset_get)
		return -EOPNOTSUPP;
	if (size > regset->n * regset->size)
		size = regset->n * regset->size;
	if (!p) {
		to_free = p = kvzalloc(size, GFP_KERNEL);
		if (!p)
			return -ENOMEM;
	}
	res = regset->regset_get(target, regset,
			   (struct membuf){.p = p, .left = size});
	if (res < 0) {
		kvfree(to_free);
		return res;
	}
	*data = p;
	return size - res;
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

/**
 * copy_regset_to_user - fetch a thread's user_regset data into user memory
 * @target:	thread to be examined
 * @view:	&struct user_regset_view describing user thread machine state
 * @setno:	index in @view->regsets
 * @offset:	offset into the regset data, in bytes
 * @size:	amount of data to copy, in bytes
 * @data:	user-mode pointer to copy into
 */
int copy_regset_to_user(struct task_struct *target,
			const struct user_regset_view *view,
			unsigned int setno,
			unsigned int offset, unsigned int size,
			void __user *data)
{
	const struct user_regset *regset = &view->regsets[setno];
	void *buf;
	int ret;

	ret = regset_get_alloc(target, regset, size, &buf);
	if (ret > 0)
		ret = copy_to_user(data, buf, ret) ? -EFAULT : 0;
	kvfree(buf);
	return ret;
}
