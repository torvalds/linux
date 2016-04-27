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
 * When building perf, unistd.h is overrided. __NR_bpf is
 * required to be defined explicitly.
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

static __u64 ptr_to_u64(void *ptr)
{
	return (__u64) (unsigned long) ptr;
}

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

int bpf_load_program(enum bpf_prog_type type, struct bpf_insn *insns,
		     size_t insns_cnt, char *license,
		     u32 kern_version, char *log_buf, size_t log_buf_sz)
{
	int fd;
	union bpf_attr attr;

	bzero(&attr, sizeof(attr));
	attr.prog_type = type;
	attr.insn_cnt = (__u32)insns_cnt;
	attr.insns = ptr_to_u64(insns);
	attr.license = ptr_to_u64(license);
	attr.log_buf = ptr_to_u64(NULL);
	attr.log_size = 0;
	attr.log_level = 0;
	attr.kern_version = kern_version;

	fd = sys_bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
	if (fd >= 0 || !log_buf || !log_buf_sz)
		return fd;

	/* Try again with log */
	attr.log_buf = ptr_to_u64(log_buf);
	attr.log_size = log_buf_sz;
	attr.log_level = 1;
	log_buf[0] = 0;
	return sys_bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
}

int bpf_map_update_elem(int fd, void *key, void *value,
			u64 flags)
{
	union bpf_attr attr;

	bzero(&attr, sizeof(attr));
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.value = ptr_to_u64(value);
	attr.flags = flags;

	return sys_bpf(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}
