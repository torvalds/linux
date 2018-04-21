/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Facebook */

#ifndef __BPF_BTF_H
#define __BPF_BTF_H

#include <stdint.h>

#define BTF_ELF_SEC ".BTF"

struct btf;

typedef int (*btf_print_fn_t)(const char *, ...)
	__attribute__((format(printf, 1, 2)));

void btf__free(struct btf *btf);
struct btf *btf__new(uint8_t *data, uint32_t size, btf_print_fn_t err_log);
int32_t btf__find_by_name(const struct btf *btf, const char *type_name);
int64_t btf__resolve_size(const struct btf *btf, uint32_t type_id);
int btf__fd(const struct btf *btf);

#endif
