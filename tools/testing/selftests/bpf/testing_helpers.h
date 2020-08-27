/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
/* Copyright (C) 2020 Facebook, Inc. */
#include <stdbool.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

int parse_num_list(const char *s, bool **set, int *set_len);
__u32 link_info_prog_id(const struct bpf_link *link, struct bpf_link_info *info);
