/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Facebook */
#ifndef __BTF_HELPERS_H
#define __BTF_HELPERS_H

#include <stdio.h>
#include <bpf/btf.h>

int fprintf_btf_type_raw(FILE *out, const struct btf *btf, __u32 id);
const char *btf_type_raw_dump(const struct btf *btf, int type_id);

#endif
