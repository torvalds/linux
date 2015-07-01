/*
 * common eBPF ELF operations.
 *
 * Copyright (C) 2013-2015 Alexei Starovoitov <ast@kernel.org>
 * Copyright (C) 2015 Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015 Huawei Inc.
 */
#ifndef __BPF_BPF_H
#define __BPF_BPF_H

#include <linux/bpf.h>

int bpf_create_map(enum bpf_map_type map_type, int key_size, int value_size,
		   int max_entries);

#endif
