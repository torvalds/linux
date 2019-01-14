/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
/* Copyright (c) 2018 Facebook */

#ifndef __LIBBPF_BTF_H
#define __LIBBPF_BTF_H

#include <linux/types.h>

#ifndef LIBBPF_API
#define LIBBPF_API __attribute__((visibility("default")))
#endif

#define BTF_ELF_SEC ".BTF"

struct btf;
struct btf_type;

typedef int (*btf_print_fn_t)(const char *, ...)
	__attribute__((format(printf, 1, 2)));

LIBBPF_API void btf__free(struct btf *btf);
LIBBPF_API struct btf *btf__new(__u8 *data, __u32 size, btf_print_fn_t err_log);
LIBBPF_API __s32 btf__find_by_name(const struct btf *btf,
				   const char *type_name);
LIBBPF_API const struct btf_type *btf__type_by_id(const struct btf *btf,
						  __u32 id);
LIBBPF_API __s64 btf__resolve_size(const struct btf *btf, __u32 type_id);
LIBBPF_API int btf__resolve_type(const struct btf *btf, __u32 type_id);
LIBBPF_API int btf__fd(const struct btf *btf);
LIBBPF_API const char *btf__name_by_offset(const struct btf *btf, __u32 offset);

#endif /* __LIBBPF_BTF_H */
