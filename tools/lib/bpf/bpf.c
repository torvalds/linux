/*
 * common eBPF ELF operations.
 *
 * Copyright (C) 2013-2015 Alexei Starovoitov <ast@kernel.org>
 * Copyright (C) 2015 Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015 Huawei Inc.
 */

#include <stdlib.h>
#include <memory.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <linux/bpf.h>
#include "bpf.h"

/*
 * When building perf, unistd.h is override. Define __NR_bpf is
 * required to be defined.
 */
#ifndef __NR_bpf
# if defined(__i386__)
#  define __NR_bpf 357
# elif defined(__x86_64__)
#  define __NR_bpf 321
# elif defined(__aarch64__)
#  define __NR_bpf 280
# else
#  error __NR_bpf not defined. libbpf does not support your arch.
# endif
#endif

static int sys_bpf(enum bpf_cmd cmd, union bpf_attr *attr,
		   unsigned int size)
{
	return syscall(__NR_bpf, cmd, attr, size);
}

int bpf_create_map(enum bpf_map_type map_type, int key_size,
		   int value_size, int max_entries)
{
	union bpf_attr attr;

	memset(&attr, '\0', sizeof(attr));

	attr.map_type = map_type;
	attr.key_size = key_size;
	attr.value_size = value_size;
	attr.max_entries = max_entries;

	return sys_bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}
