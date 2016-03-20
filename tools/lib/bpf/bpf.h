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

/* Recommend log buffer size */
#define BPF_LOG_BUF_SIZE 65536
int bpf_load_program(enum bpf_prog_type type, struct bpf_insn *insns,
		     size_t insns_cnt, char *license,
		     u32 kern_version, char *log_buf,
		     size_t log_buf_sz);

int bpf_map_update_elem(int fd, void *key, void *value,
			u64 flags);
#endif
