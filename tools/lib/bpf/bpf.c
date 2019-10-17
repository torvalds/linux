// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

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

#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <errno.h>
#include <linux/bpf.h>
#include "bpf.h"
#include "libbpf.h"
#include "libbpf_internal.h"

/*
 * When building perf, unistd.h is overridden. __NR_bpf is
 * required to be defined explicitly.
 */
#ifndef __NR_bpf
# if defined(__i386__)
#  define __NR_bpf 357
# elif defined(__x86_64__)
#  define __NR_bpf 321
# elif defined(__aarch64__)
#  define __NR_bpf 280
# elif defined(__sparc__)
#  define __NR_bpf 349
# elif defined(__s390__)
#  define __NR_bpf 351
# elif defined(__arc__)
#  define __NR_bpf 280
# else
#  error __NR_bpf not defined. libbpf does not support your arch.
# endif
#endif

static inline __u64 ptr_to_u64(const void *ptr)
{
	return (__u64) (unsigned long) ptr;
}

static inline int sys_bpf(enum bpf_cmd cmd, union bpf_attr *attr,
			  unsigned int size)
{
	return syscall(__NR_bpf, cmd, attr, size);
}

static inline int sys_bpf_prog_load(union bpf_attr *attr, unsigned int size)
{
	int fd;

	do {
		fd = sys_bpf(BPF_PROG_LOAD, attr, size);
	} while (fd < 0 && errno == EAGAIN);

	return fd;
}

int bpf_create_map_xattr(const struct bpf_create_map_attr *create_attr)
{
	union bpf_attr attr;

	memset(&attr, '\0', sizeof(attr));

	attr.map_type = create_attr->map_type;
	attr.key_size = create_attr->key_size;
	attr.value_size = create_attr->value_size;
	attr.max_entries = create_attr->max_entries;
	attr.map_flags = create_attr->map_flags;
	if (create_attr->name)
		memcpy(attr.map_name, create_attr->name,
		       min(strlen(create_attr->name), BPF_OBJ_NAME_LEN - 1));
	attr.numa_node = create_attr->numa_node;
	attr.btf_fd = create_attr->btf_fd;
	attr.btf_key_type_id = create_attr->btf_key_type_id;
	attr.btf_value_type_id = create_attr->btf_value_type_id;
	attr.map_ifindex = create_attr->map_ifindex;
	attr.inner_map_fd = create_attr->inner_map_fd;

	return sys_bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}

int bpf_create_map_node(enum bpf_map_type map_type, const char *name,
			int key_size, int value_size, int max_entries,
			__u32 map_flags, int node)
{
	struct bpf_create_map_attr map_attr = {};

	map_attr.name = name;
	map_attr.map_type = map_type;
	map_attr.map_flags = map_flags;
	map_attr.key_size = key_size;
	map_attr.value_size = value_size;
	map_attr.max_entries = max_entries;
	if (node >= 0) {
		map_attr.numa_node = node;
		map_attr.map_flags |= BPF_F_NUMA_NODE;
	}

	return bpf_create_map_xattr(&map_attr);
}

int bpf_create_map(enum bpf_map_type map_type, int key_size,
		   int value_size, int max_entries, __u32 map_flags)
{
	struct bpf_create_map_attr map_attr = {};

	map_attr.map_type = map_type;
	map_attr.map_flags = map_flags;
	map_attr.key_size = key_size;
	map_attr.value_size = value_size;
	map_attr.max_entries = max_entries;

	return bpf_create_map_xattr(&map_attr);
}

int bpf_create_map_name(enum bpf_map_type map_type, const char *name,
			int key_size, int value_size, int max_entries,
			__u32 map_flags)
{
	struct bpf_create_map_attr map_attr = {};

	map_attr.name = name;
	map_attr.map_type = map_type;
	map_attr.map_flags = map_flags;
	map_attr.key_size = key_size;
	map_attr.value_size = value_size;
	map_attr.max_entries = max_entries;

	return bpf_create_map_xattr(&map_attr);
}

int bpf_create_map_in_map_node(enum bpf_map_type map_type, const char *name,
			       int key_size, int inner_map_fd, int max_entries,
			       __u32 map_flags, int node)
{
	union bpf_attr attr;

	memset(&attr, '\0', sizeof(attr));

	attr.map_type = map_type;
	attr.key_size = key_size;
	attr.value_size = 4;
	attr.inner_map_fd = inner_map_fd;
	attr.max_entries = max_entries;
	attr.map_flags = map_flags;
	if (name)
		memcpy(attr.map_name, name,
		       min(strlen(name), BPF_OBJ_NAME_LEN - 1));

	if (node >= 0) {
		attr.map_flags |= BPF_F_NUMA_NODE;
		attr.numa_node = node;
	}

	return sys_bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}

int bpf_create_map_in_map(enum bpf_map_type map_type, const char *name,
			  int key_size, int inner_map_fd, int max_entries,
			  __u32 map_flags)
{
	return bpf_create_map_in_map_node(map_type, name, key_size,
					  inner_map_fd, max_entries, map_flags,
					  -1);
}

static void *
alloc_zero_tailing_info(const void *orecord, __u32 cnt,
			__u32 actual_rec_size, __u32 expected_rec_size)
{
	__u64 info_len = actual_rec_size * cnt;
	void *info, *nrecord;
	int i;

	info = malloc(info_len);
	if (!info)
		return NULL;

	/* zero out bytes kernel does not understand */
	nrecord = info;
	for (i = 0; i < cnt; i++) {
		memcpy(nrecord, orecord, expected_rec_size);
		memset(nrecord + expected_rec_size, 0,
		       actual_rec_size - expected_rec_size);
		orecord += actual_rec_size;
		nrecord += actual_rec_size;
	}

	return info;
}

int bpf_load_program_xattr(const struct bpf_load_program_attr *load_attr,
			   char *log_buf, size_t log_buf_sz)
{
	void *finfo = NULL, *linfo = NULL;
	union bpf_attr attr;
	__u32 log_level;
	int fd;

	if (!load_attr || !log_buf != !log_buf_sz)
		return -EINVAL;

	log_level = load_attr->log_level;
	if (log_level > (4 | 2 | 1) || (log_level && !log_buf))
		return -EINVAL;

	memset(&attr, 0, sizeof(attr));
	attr.prog_type = load_attr->prog_type;
	attr.expected_attach_type = load_attr->expected_attach_type;
	if (attr.prog_type == BPF_PROG_TYPE_RAW_TRACEPOINT)
		/* expected_attach_type is ignored for tracing progs */
		attr.attach_btf_id = attr.expected_attach_type;
	attr.insn_cnt = (__u32)load_attr->insns_cnt;
	attr.insns = ptr_to_u64(load_attr->insns);
	attr.license = ptr_to_u64(load_attr->license);

	attr.log_level = log_level;
	if (log_level) {
		attr.log_buf = ptr_to_u64(log_buf);
		attr.log_size = log_buf_sz;
	} else {
		attr.log_buf = ptr_to_u64(NULL);
		attr.log_size = 0;
	}

	attr.kern_version = load_attr->kern_version;
	attr.prog_ifindex = load_attr->prog_ifindex;
	attr.prog_btf_fd = load_attr->prog_btf_fd;
	attr.func_info_rec_size = load_attr->func_info_rec_size;
	attr.func_info_cnt = load_attr->func_info_cnt;
	attr.func_info = ptr_to_u64(load_attr->func_info);
	attr.line_info_rec_size = load_attr->line_info_rec_size;
	attr.line_info_cnt = load_attr->line_info_cnt;
	attr.line_info = ptr_to_u64(load_attr->line_info);
	if (load_attr->name)
		memcpy(attr.prog_name, load_attr->name,
		       min(strlen(load_attr->name), BPF_OBJ_NAME_LEN - 1));
	attr.prog_flags = load_attr->prog_flags;

	fd = sys_bpf_prog_load(&attr, sizeof(attr));
	if (fd >= 0)
		return fd;

	/* After bpf_prog_load, the kernel may modify certain attributes
	 * to give user space a hint how to deal with loading failure.
	 * Check to see whether we can make some changes and load again.
	 */
	while (errno == E2BIG && (!finfo || !linfo)) {
		if (!finfo && attr.func_info_cnt &&
		    attr.func_info_rec_size < load_attr->func_info_rec_size) {
			/* try with corrected func info records */
			finfo = alloc_zero_tailing_info(load_attr->func_info,
							load_attr->func_info_cnt,
							load_attr->func_info_rec_size,
							attr.func_info_rec_size);
			if (!finfo)
				goto done;

			attr.func_info = ptr_to_u64(finfo);
			attr.func_info_rec_size = load_attr->func_info_rec_size;
		} else if (!linfo && attr.line_info_cnt &&
			   attr.line_info_rec_size <
			   load_attr->line_info_rec_size) {
			linfo = alloc_zero_tailing_info(load_attr->line_info,
							load_attr->line_info_cnt,
							load_attr->line_info_rec_size,
							attr.line_info_rec_size);
			if (!linfo)
				goto done;

			attr.line_info = ptr_to_u64(linfo);
			attr.line_info_rec_size = load_attr->line_info_rec_size;
		} else {
			break;
		}

		fd = sys_bpf_prog_load(&attr, sizeof(attr));

		if (fd >= 0)
			goto done;
	}

	if (log_level || !log_buf)
		goto done;

	/* Try again with log */
	attr.log_buf = ptr_to_u64(log_buf);
	attr.log_size = log_buf_sz;
	attr.log_level = 1;
	log_buf[0] = 0;
	fd = sys_bpf_prog_load(&attr, sizeof(attr));
done:
	free(finfo);
	free(linfo);
	return fd;
}

int bpf_load_program(enum bpf_prog_type type, const struct bpf_insn *insns,
		     size_t insns_cnt, const char *license,
		     __u32 kern_version, char *log_buf,
		     size_t log_buf_sz)
{
	struct bpf_load_program_attr load_attr;

	memset(&load_attr, 0, sizeof(struct bpf_load_program_attr));
	load_attr.prog_type = type;
	load_attr.expected_attach_type = 0;
	load_attr.name = NULL;
	load_attr.insns = insns;
	load_attr.insns_cnt = insns_cnt;
	load_attr.license = license;
	load_attr.kern_version = kern_version;

	return bpf_load_program_xattr(&load_attr, log_buf, log_buf_sz);
}

int bpf_verify_program(enum bpf_prog_type type, const struct bpf_insn *insns,
		       size_t insns_cnt, __u32 prog_flags, const char *license,
		       __u32 kern_version, char *log_buf, size_t log_buf_sz,
		       int log_level)
{
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.prog_type = type;
	attr.insn_cnt = (__u32)insns_cnt;
	attr.insns = ptr_to_u64(insns);
	attr.license = ptr_to_u64(license);
	attr.log_buf = ptr_to_u64(log_buf);
	attr.log_size = log_buf_sz;
	attr.log_level = log_level;
	log_buf[0] = 0;
	attr.kern_version = kern_version;
	attr.prog_flags = prog_flags;

	return sys_bpf_prog_load(&attr, sizeof(attr));
}

int bpf_map_update_elem(int fd, const void *key, const void *value,
			__u64 flags)
{
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.value = ptr_to_u64(value);
	attr.flags = flags;

	return sys_bpf(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

int bpf_map_lookup_elem(int fd, const void *key, void *value)
{
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.value = ptr_to_u64(value);

	return sys_bpf(BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
}

int bpf_map_lookup_elem_flags(int fd, const void *key, void *value, __u64 flags)
{
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.value = ptr_to_u64(value);
	attr.flags = flags;

	return sys_bpf(BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
}

int bpf_map_lookup_and_delete_elem(int fd, const void *key, void *value)
{
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.value = ptr_to_u64(value);

	return sys_bpf(BPF_MAP_LOOKUP_AND_DELETE_ELEM, &attr, sizeof(attr));
}

int bpf_map_delete_elem(int fd, const void *key)
{
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);

	return sys_bpf(BPF_MAP_DELETE_ELEM, &attr, sizeof(attr));
}

int bpf_map_get_next_key(int fd, const void *key, void *next_key)
{
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.next_key = ptr_to_u64(next_key);

	return sys_bpf(BPF_MAP_GET_NEXT_KEY, &attr, sizeof(attr));
}

int bpf_map_freeze(int fd)
{
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.map_fd = fd;

	return sys_bpf(BPF_MAP_FREEZE, &attr, sizeof(attr));
}

int bpf_obj_pin(int fd, const char *pathname)
{
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.pathname = ptr_to_u64((void *)pathname);
	attr.bpf_fd = fd;

	return sys_bpf(BPF_OBJ_PIN, &attr, sizeof(attr));
}

int bpf_obj_get(const char *pathname)
{
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.pathname = ptr_to_u64((void *)pathname);

	return sys_bpf(BPF_OBJ_GET, &attr, sizeof(attr));
}

int bpf_prog_attach(int prog_fd, int target_fd, enum bpf_attach_type type,
		    unsigned int flags)
{
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.target_fd	   = target_fd;
	attr.attach_bpf_fd = prog_fd;
	attr.attach_type   = type;
	attr.attach_flags  = flags;

	return sys_bpf(BPF_PROG_ATTACH, &attr, sizeof(attr));
}

int bpf_prog_detach(int target_fd, enum bpf_attach_type type)
{
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.target_fd	 = target_fd;
	attr.attach_type = type;

	return sys_bpf(BPF_PROG_DETACH, &attr, sizeof(attr));
}

int bpf_prog_detach2(int prog_fd, int target_fd, enum bpf_attach_type type)
{
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.target_fd	 = target_fd;
	attr.attach_bpf_fd = prog_fd;
	attr.attach_type = type;

	return sys_bpf(BPF_PROG_DETACH, &attr, sizeof(attr));
}

int bpf_prog_query(int target_fd, enum bpf_attach_type type, __u32 query_flags,
		   __u32 *attach_flags, __u32 *prog_ids, __u32 *prog_cnt)
{
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, sizeof(attr));
	attr.query.target_fd	= target_fd;
	attr.query.attach_type	= type;
	attr.query.query_flags	= query_flags;
	attr.query.prog_cnt	= *prog_cnt;
	attr.query.prog_ids	= ptr_to_u64(prog_ids);

	ret = sys_bpf(BPF_PROG_QUERY, &attr, sizeof(attr));
	if (attach_flags)
		*attach_flags = attr.query.attach_flags;
	*prog_cnt = attr.query.prog_cnt;
	return ret;
}

int bpf_prog_test_run(int prog_fd, int repeat, void *data, __u32 size,
		      void *data_out, __u32 *size_out, __u32 *retval,
		      __u32 *duration)
{
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, sizeof(attr));
	attr.test.prog_fd = prog_fd;
	attr.test.data_in = ptr_to_u64(data);
	attr.test.data_out = ptr_to_u64(data_out);
	attr.test.data_size_in = size;
	attr.test.repeat = repeat;

	ret = sys_bpf(BPF_PROG_TEST_RUN, &attr, sizeof(attr));
	if (size_out)
		*size_out = attr.test.data_size_out;
	if (retval)
		*retval = attr.test.retval;
	if (duration)
		*duration = attr.test.duration;
	return ret;
}

int bpf_prog_test_run_xattr(struct bpf_prog_test_run_attr *test_attr)
{
	union bpf_attr attr;
	int ret;

	if (!test_attr->data_out && test_attr->data_size_out > 0)
		return -EINVAL;

	memset(&attr, 0, sizeof(attr));
	attr.test.prog_fd = test_attr->prog_fd;
	attr.test.data_in = ptr_to_u64(test_attr->data_in);
	attr.test.data_out = ptr_to_u64(test_attr->data_out);
	attr.test.data_size_in = test_attr->data_size_in;
	attr.test.data_size_out = test_attr->data_size_out;
	attr.test.ctx_in = ptr_to_u64(test_attr->ctx_in);
	attr.test.ctx_out = ptr_to_u64(test_attr->ctx_out);
	attr.test.ctx_size_in = test_attr->ctx_size_in;
	attr.test.ctx_size_out = test_attr->ctx_size_out;
	attr.test.repeat = test_attr->repeat;

	ret = sys_bpf(BPF_PROG_TEST_RUN, &attr, sizeof(attr));
	test_attr->data_size_out = attr.test.data_size_out;
	test_attr->ctx_size_out = attr.test.ctx_size_out;
	test_attr->retval = attr.test.retval;
	test_attr->duration = attr.test.duration;
	return ret;
}

static int bpf_obj_get_next_id(__u32 start_id, __u32 *next_id, int cmd)
{
	union bpf_attr attr;
	int err;

	memset(&attr, 0, sizeof(attr));
	attr.start_id = start_id;

	err = sys_bpf(cmd, &attr, sizeof(attr));
	if (!err)
		*next_id = attr.next_id;

	return err;
}

int bpf_prog_get_next_id(__u32 start_id, __u32 *next_id)
{
	return bpf_obj_get_next_id(start_id, next_id, BPF_PROG_GET_NEXT_ID);
}

int bpf_map_get_next_id(__u32 start_id, __u32 *next_id)
{
	return bpf_obj_get_next_id(start_id, next_id, BPF_MAP_GET_NEXT_ID);
}

int bpf_btf_get_next_id(__u32 start_id, __u32 *next_id)
{
	return bpf_obj_get_next_id(start_id, next_id, BPF_BTF_GET_NEXT_ID);
}

int bpf_prog_get_fd_by_id(__u32 id)
{
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.prog_id = id;

	return sys_bpf(BPF_PROG_GET_FD_BY_ID, &attr, sizeof(attr));
}

int bpf_map_get_fd_by_id(__u32 id)
{
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.map_id = id;

	return sys_bpf(BPF_MAP_GET_FD_BY_ID, &attr, sizeof(attr));
}

int bpf_btf_get_fd_by_id(__u32 id)
{
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.btf_id = id;

	return sys_bpf(BPF_BTF_GET_FD_BY_ID, &attr, sizeof(attr));
}

int bpf_obj_get_info_by_fd(int prog_fd, void *info, __u32 *info_len)
{
	union bpf_attr attr;
	int err;

	memset(&attr, 0, sizeof(attr));
	attr.info.bpf_fd = prog_fd;
	attr.info.info_len = *info_len;
	attr.info.info = ptr_to_u64(info);

	err = sys_bpf(BPF_OBJ_GET_INFO_BY_FD, &attr, sizeof(attr));
	if (!err)
		*info_len = attr.info.info_len;

	return err;
}

int bpf_raw_tracepoint_open(const char *name, int prog_fd)
{
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.raw_tracepoint.name = ptr_to_u64(name);
	attr.raw_tracepoint.prog_fd = prog_fd;

	return sys_bpf(BPF_RAW_TRACEPOINT_OPEN, &attr, sizeof(attr));
}

int bpf_load_btf(void *btf, __u32 btf_size, char *log_buf, __u32 log_buf_size,
		 bool do_log)
{
	union bpf_attr attr = {};
	int fd;

	attr.btf = ptr_to_u64(btf);
	attr.btf_size = btf_size;

retry:
	if (do_log && log_buf && log_buf_size) {
		attr.btf_log_level = 1;
		attr.btf_log_size = log_buf_size;
		attr.btf_log_buf = ptr_to_u64(log_buf);
	}

	fd = sys_bpf(BPF_BTF_LOAD, &attr, sizeof(attr));
	if (fd == -1 && !do_log && log_buf && log_buf_size) {
		do_log = true;
		goto retry;
	}

	return fd;
}

int bpf_task_fd_query(int pid, int fd, __u32 flags, char *buf, __u32 *buf_len,
		      __u32 *prog_id, __u32 *fd_type, __u64 *probe_offset,
		      __u64 *probe_addr)
{
	union bpf_attr attr = {};
	int err;

	attr.task_fd_query.pid = pid;
	attr.task_fd_query.fd = fd;
	attr.task_fd_query.flags = flags;
	attr.task_fd_query.buf = ptr_to_u64(buf);
	attr.task_fd_query.buf_len = *buf_len;

	err = sys_bpf(BPF_TASK_FD_QUERY, &attr, sizeof(attr));
	*buf_len = attr.task_fd_query.buf_len;
	*prog_id = attr.task_fd_query.prog_id;
	*fd_type = attr.task_fd_query.fd_type;
	*probe_offset = attr.task_fd_query.probe_offset;
	*probe_addr = attr.task_fd_query.probe_addr;

	return err;
}
