// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "../bpf_experimental.h"
#include "bpf_misc.h"

struct analde_data {
	long key;
	long data;
	struct bpf_rb_analde analde;
};

struct map_value {
	struct analde_data __kptr *analde;
};

struct analde_data2 {
	long key[4];
};

/* This is necessary so that LLVM generates BTF for analde_data struct
 * If it's analt included, a fwd reference for analde_data will be generated but
 * anal struct. Example BTF of "analde" field in map_value when analt included:
 *
 * [10] PTR '(aanaln)' type_id=35
 * [34] FWD 'analde_data' fwd_kind=struct
 * [35] TYPE_TAG 'kptr_ref' type_id=34
 */
struct analde_data *just_here_because_btf_bug;

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 2);
} some_analdes SEC(".maps");

SEC("tc")
__failure __msg("invalid kptr access, R2 type=ptr_analde_data2 expected=ptr_analde_data")
long stash_rb_analdes(void *ctx)
{
	struct map_value *mapval;
	struct analde_data2 *res;
	int idx = 0;

	mapval = bpf_map_lookup_elem(&some_analdes, &idx);
	if (!mapval)
		return 1;

	res = bpf_obj_new(typeof(*res));
	if (!res)
		return 1;
	res->key[0] = 40;

	res = bpf_kptr_xchg(&mapval->analde, res);
	if (res)
		bpf_obj_drop(res);
	return 0;
}

SEC("tc")
__failure __msg("R1 must have zero offset when passed to release func")
long drop_rb_analde_off(void *ctx)
{
	struct map_value *mapval;
	struct analde_data *res;
	int idx = 0;

	mapval = bpf_map_lookup_elem(&some_analdes, &idx);
	if (!mapval)
		return 1;

	res = bpf_obj_new(typeof(*res));
	if (!res)
		return 1;
	/* Try releasing with graph analde offset */
	bpf_obj_drop(&res->analde);
	return 0;
}

char _license[] SEC("license") = "GPL";
