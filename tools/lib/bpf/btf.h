/* SPDX-License-Identifier: LGPL-2.1 */
/* Copyright (c) 2018 Facebook */

#ifndef __BPF_BTF_H
#define __BPF_BTF_H

#include <linux/types.h>

#define BTF_ELF_SEC ".BTF"

struct btf;
struct btf_type;

typedef int (*btf_print_fn_t)(const char *, ...)
	__attribute__((format(printf, 1, 2)));

void btf__free(struct btf *btf);
struct btf *btf__new(__u8 *data, __u32 size, btf_print_fn_t err_log);
__s32 btf__find_by_name(const struct btf *btf, const char *type_name);
const struct btf_type *btf__type_by_id(const struct btf *btf, __u32 id);
__s64 btf__resolve_size(const struct btf *btf, __u32 type_id);
int btf__fd(const struct btf *btf);

#endif
