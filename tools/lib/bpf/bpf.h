/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */

/*
 * common eBPF ELF operations.
 *
 * Copyright (C) 2013-2015 Alexei Starovoitov <ast@kernel.org>
 * Copyright (C) 2015 Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015 Huawei Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License (not later!)
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not,  see <http://www.gnu.org/licenses>
 */
#ifndef __LIBBPF_BPF_H
#define __LIBBPF_BPF_H

#include <linux/bpf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LIBBPF_API
#define LIBBPF_API __attribute__((visibility("default")))
#endif

struct bpf_create_map_attr {
	const char *name;
	enum bpf_map_type map_type;
	__u32 map_flags;
	__u32 key_size;
	__u32 value_size;
	__u32 max_entries;
	__u32 numa_node;
	__u32 btf_fd;
	__u32 btf_key_type_id;
	__u32 btf_value_type_id;
	__u32 map_ifindex;
	__u32 inner_map_fd;
};

LIBBPF_API int
bpf_create_map_xattr(const struct bpf_create_map_attr *create_attr);
LIBBPF_API int bpf_create_map_node(enum bpf_map_type map_type, const char *name,
				   int key_size, int value_size,
				   int max_entries, __u32 map_flags, int node);
LIBBPF_API int bpf_create_map_name(enum bpf_map_type map_type, const char *name,
				   int key_size, int value_size,
				   int max_entries, __u32 map_flags);
LIBBPF_API int bpf_create_map(enum bpf_map_type map_type, int key_size,
			      int value_size, int max_entries, __u32 map_flags);
LIBBPF_API int bpf_create_map_in_map_node(enum bpf_map_type map_type,
					  const char *name, int key_size,
					  int inner_map_fd, int max_entries,
					  __u32 map_flags, int node);
LIBBPF_API int bpf_create_map_in_map(enum bpf_map_type map_type,
				     const char *name, int key_size,
				     int inner_map_fd, int max_entries,
				     __u32 map_flags);

struct bpf_load_program_attr {
	enum bpf_prog_type prog_type;
	enum bpf_attach_type expected_attach_type;
	const char *name;
	const struct bpf_insn *insns;
	size_t insns_cnt;
	const char *license;
	__u32 kern_version;
	__u32 prog_ifindex;
	__u32 prog_btf_fd;
	__u32 func_info_rec_size;
	const void *func_info;
	__u32 func_info_cnt;
	__u32 line_info_rec_size;
	const void *line_info;
	__u32 line_info_cnt;
	__u32 log_level;
	__u32 prog_flags;
};

/* Flags to direct loading requirements */
#define MAPS_RELAX_COMPAT	0x01

/* Recommend log buffer size */
#define BPF_LOG_BUF_SIZE (UINT32_MAX >> 8) /* verifier maximum in kernels <= 5.1 */
LIBBPF_API int
bpf_load_program_xattr(const struct bpf_load_program_attr *load_attr,
		       char *log_buf, size_t log_buf_sz);
LIBBPF_API int bpf_load_program(enum bpf_prog_type type,
				const struct bpf_insn *insns, size_t insns_cnt,
				const char *license, __u32 kern_version,
				char *log_buf, size_t log_buf_sz);
LIBBPF_API int bpf_verify_program(enum bpf_prog_type type,
				  const struct bpf_insn *insns,
				  size_t insns_cnt, __u32 prog_flags,
				  const char *license, __u32 kern_version,
				  char *log_buf, size_t log_buf_sz,
				  int log_level);

LIBBPF_API int bpf_map_update_elem(int fd, const void *key, const void *value,
				   __u64 flags);

LIBBPF_API int bpf_map_lookup_elem(int fd, const void *key, void *value);
LIBBPF_API int bpf_map_lookup_elem_flags(int fd, const void *key, void *value,
					 __u64 flags);
LIBBPF_API int bpf_map_lookup_and_delete_elem(int fd, const void *key,
					      void *value);
LIBBPF_API int bpf_map_delete_elem(int fd, const void *key);
LIBBPF_API int bpf_map_get_next_key(int fd, const void *key, void *next_key);
LIBBPF_API int bpf_map_freeze(int fd);
LIBBPF_API int bpf_obj_pin(int fd, const char *pathname);
LIBBPF_API int bpf_obj_get(const char *pathname);
LIBBPF_API int bpf_prog_attach(int prog_fd, int attachable_fd,
			       enum bpf_attach_type type, unsigned int flags);
LIBBPF_API int bpf_prog_detach(int attachable_fd, enum bpf_attach_type type);
LIBBPF_API int bpf_prog_detach2(int prog_fd, int attachable_fd,
				enum bpf_attach_type type);

struct bpf_prog_test_run_attr {
	int prog_fd;
	int repeat;
	const void *data_in;
	__u32 data_size_in;
	void *data_out;      /* optional */
	__u32 data_size_out; /* in: max length of data_out
			      * out: length of data_out */
	__u32 retval;        /* out: return code of the BPF program */
	__u32 duration;      /* out: average per repetition in ns */
	const void *ctx_in; /* optional */
	__u32 ctx_size_in;
	void *ctx_out;      /* optional */
	__u32 ctx_size_out; /* in: max length of ctx_out
			     * out: length of cxt_out */
};

LIBBPF_API int bpf_prog_test_run_xattr(struct bpf_prog_test_run_attr *test_attr);

/*
 * bpf_prog_test_run does not check that data_out is large enough. Consider
 * using bpf_prog_test_run_xattr instead.
 */
LIBBPF_API int bpf_prog_test_run(int prog_fd, int repeat, void *data,
				 __u32 size, void *data_out, __u32 *size_out,
				 __u32 *retval, __u32 *duration);
LIBBPF_API int bpf_prog_get_next_id(__u32 start_id, __u32 *next_id);
LIBBPF_API int bpf_map_get_next_id(__u32 start_id, __u32 *next_id);
LIBBPF_API int bpf_btf_get_next_id(__u32 start_id, __u32 *next_id);
LIBBPF_API int bpf_prog_get_fd_by_id(__u32 id);
LIBBPF_API int bpf_map_get_fd_by_id(__u32 id);
LIBBPF_API int bpf_btf_get_fd_by_id(__u32 id);
LIBBPF_API int bpf_obj_get_info_by_fd(int prog_fd, void *info, __u32 *info_len);
LIBBPF_API int bpf_prog_query(int target_fd, enum bpf_attach_type type,
			      __u32 query_flags, __u32 *attach_flags,
			      __u32 *prog_ids, __u32 *prog_cnt);
LIBBPF_API int bpf_raw_tracepoint_open(const char *name, int prog_fd);
LIBBPF_API int bpf_load_btf(void *btf, __u32 btf_size, char *log_buf,
			    __u32 log_buf_size, bool do_log);
LIBBPF_API int bpf_task_fd_query(int pid, int fd, __u32 flags, char *buf,
				 __u32 *buf_len, __u32 *prog_id, __u32 *fd_type,
				 __u64 *probe_offset, __u64 *probe_addr);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __LIBBPF_BPF_H */
