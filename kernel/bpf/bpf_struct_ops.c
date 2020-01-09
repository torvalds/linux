// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2019 Facebook */

#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/btf.h>
#include <linux/filter.h>
#include <linux/slab.h>
#include <linux/numa.h>
#include <linux/seq_file.h>
#include <linux/refcount.h>

#define BPF_STRUCT_OPS_TYPE(_name)				\
extern struct bpf_struct_ops bpf_##_name;
#include "bpf_struct_ops_types.h"
#undef BPF_STRUCT_OPS_TYPE

enum {
#define BPF_STRUCT_OPS_TYPE(_name) BPF_STRUCT_OPS_TYPE_##_name,
#include "bpf_struct_ops_types.h"
#undef BPF_STRUCT_OPS_TYPE
	__NR_BPF_STRUCT_OPS_TYPE,
};

static struct bpf_struct_ops * const bpf_struct_ops[] = {
#define BPF_STRUCT_OPS_TYPE(_name)				\
	[BPF_STRUCT_OPS_TYPE_##_name] = &bpf_##_name,
#include "bpf_struct_ops_types.h"
#undef BPF_STRUCT_OPS_TYPE
};

const struct bpf_verifier_ops bpf_struct_ops_verifier_ops = {
};

const struct bpf_prog_ops bpf_struct_ops_prog_ops = {
};

void bpf_struct_ops_init(struct btf *btf)
{
	const struct btf_member *member;
	struct bpf_struct_ops *st_ops;
	struct bpf_verifier_log log = {};
	const struct btf_type *t;
	const char *mname;
	s32 type_id;
	u32 i, j;

	for (i = 0; i < ARRAY_SIZE(bpf_struct_ops); i++) {
		st_ops = bpf_struct_ops[i];

		type_id = btf_find_by_name_kind(btf, st_ops->name,
						BTF_KIND_STRUCT);
		if (type_id < 0) {
			pr_warn("Cannot find struct %s in btf_vmlinux\n",
				st_ops->name);
			continue;
		}
		t = btf_type_by_id(btf, type_id);
		if (btf_type_vlen(t) > BPF_STRUCT_OPS_MAX_NR_MEMBERS) {
			pr_warn("Cannot support #%u members in struct %s\n",
				btf_type_vlen(t), st_ops->name);
			continue;
		}

		for_each_member(j, t, member) {
			const struct btf_type *func_proto;

			mname = btf_name_by_offset(btf, member->name_off);
			if (!*mname) {
				pr_warn("anon member in struct %s is not supported\n",
					st_ops->name);
				break;
			}

			if (btf_member_bitfield_size(t, member)) {
				pr_warn("bit field member %s in struct %s is not supported\n",
					mname, st_ops->name);
				break;
			}

			func_proto = btf_type_resolve_func_ptr(btf,
							       member->type,
							       NULL);
			if (func_proto &&
			    btf_distill_func_proto(&log, btf,
						   func_proto, mname,
						   &st_ops->func_models[j])) {
				pr_warn("Error in parsing func ptr %s in struct %s\n",
					mname, st_ops->name);
				break;
			}
		}

		if (j == btf_type_vlen(t)) {
			if (st_ops->init(btf)) {
				pr_warn("Error in init bpf_struct_ops %s\n",
					st_ops->name);
			} else {
				st_ops->type_id = type_id;
				st_ops->type = t;
			}
		}
	}
}

extern struct btf *btf_vmlinux;

const struct bpf_struct_ops *bpf_struct_ops_find(u32 type_id)
{
	unsigned int i;

	if (!type_id || !btf_vmlinux)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(bpf_struct_ops); i++) {
		if (bpf_struct_ops[i]->type_id == type_id)
			return bpf_struct_ops[i];
	}

	return NULL;
}
