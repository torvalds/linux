/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/btf.h>

const struct btf_type *btf_find_func_proto(const char *func_name,
					   struct btf **btf_p);
const struct btf_param *btf_get_func_param(const struct btf_type *func_proto,
					   s32 *nr);
const struct btf_member *btf_find_struct_member(struct btf *btf,
						const struct btf_type *type,
						const char *member_name,
						u32 *anon_offset);
