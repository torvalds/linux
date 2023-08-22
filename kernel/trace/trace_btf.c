// SPDX-License-Identifier: GPL-2.0
#include <linux/btf.h>
#include <linux/kernel.h>

#include "trace_btf.h"

/*
 * Find a function proto type by name, and return the btf_type with its btf
 * in *@btf_p. Return NULL if not found.
 * Note that caller has to call btf_put(*@btf_p) after using the btf_type.
 */
const struct btf_type *btf_find_func_proto(const char *func_name, struct btf **btf_p)
{
	const struct btf_type *t;
	s32 id;

	id = bpf_find_btf_id(func_name, BTF_KIND_FUNC, btf_p);
	if (id < 0)
		return NULL;

	/* Get BTF_KIND_FUNC type */
	t = btf_type_by_id(*btf_p, id);
	if (!t || !btf_type_is_func(t))
		goto err;

	/* The type of BTF_KIND_FUNC is BTF_KIND_FUNC_PROTO */
	t = btf_type_by_id(*btf_p, t->type);
	if (!t || !btf_type_is_func_proto(t))
		goto err;

	return t;
err:
	btf_put(*btf_p);
	return NULL;
}

/*
 * Get function parameter with the number of parameters.
 * This can return NULL if the function has no parameters.
 * It can return -EINVAL if the @func_proto is not a function proto type.
 */
const struct btf_param *btf_get_func_param(const struct btf_type *func_proto, s32 *nr)
{
	if (!btf_type_is_func_proto(func_proto))
		return ERR_PTR(-EINVAL);

	*nr = btf_type_vlen(func_proto);
	if (*nr > 0)
		return (const struct btf_param *)(func_proto + 1);
	else
		return NULL;
}

