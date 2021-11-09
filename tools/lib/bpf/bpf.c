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
	int retries = 5;
	int fd;

	do {
		fd = sys_bpf(BPF_PROG_LOAD, attr, size);
	} while (fd < 0 && errno == EAGAIN && retries-- > 0);

	return fd;
}

int libbpf__bpf_create_map_xattr(const struct bpf_create_map_params *create_attr)
{
	union bpf_attr attr;
	int fd;

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
	if (attr.map_type == BPF_MAP_TYPE_STRUCT_OPS)
		attr.btf_vmlinux_value_type_id =
			create_attr->btf_vmlinux_value_type_id;
	else
		attr.inner_map_fd = create_attr->inner_map_fd;
	attr.map_extra = create_attr->map_extra;

	fd = sys_bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
	return libbpf_err_errno(fd);
}

int bpf_create_map_xattr(const struct bpf_create_map_attr *create_attr)
{
	struct bpf_create_map_params p = {};

	p.map_type = create_attr->map_type;
	p.key_size = create_attr->key_size;
	p.value_size = create_attr->value_size;
	p.max_entries = create_attr->max_entries;
	p.map_flags = create_attr->map_flags;
	p.name = create_attr->name;
	p.numa_node = create_attr->numa_node;
	p.btf_fd = create_attr->btf_fd;
	p.btf_key_type_id = create_attr->btf_key_type_id;
	p.btf_value_type_id = create_attr->btf_value_type_id;
	p.map_ifindex = create_attr->map_ifindex;
	if (p.map_type == BPF_MAP_TYPE_STRUCT_OPS)
		p.btf_vmlinux_value_type_id =
			create_attr->btf_vmlinux_value_type_id;
	else
		p.inner_map_fd = create_attr->inner_map_fd;

	return libbpf__bpf_create_map_xattr(&p);
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
	int fd;

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

	fd = sys_bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
	return libbpf_err_errno(fd);
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
	__u64 info_len = (__u64)actual_rec_size * cnt;
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

int libbpf__bpf_prog_load(const struct bpf_prog_load_params *load_attr)
{
	void *finfo = NULL, *linfo = NULL;
	union bpf_attr attr;
	int fd;

	if (!load_attr->log_buf != !load_attr->log_buf_sz)
		return libbpf_err(-EINVAL);

	if (load_attr->log_level > (4 | 2 | 1) || (load_attr->log_level && !load_attr->log_buf))
		return libbpf_err(-EINVAL);

	memset(&attr, 0, sizeof(attr));
	attr.prog_type = load_attr->prog_type;
	attr.expected_attach_type = load_attr->expected_attach_type;

	if (load_attr->attach_prog_fd)
		attr.attach_prog_fd = load_attr->attach_prog_fd;
	else
		attr.attach_btf_obj_fd = load_attr->attach_btf_obj_fd;
	attr.attach_btf_id = load_attr->attach_btf_id;

	attr.prog_ifindex = load_attr->prog_ifindex;
	attr.kern_version = load_attr->kern_version;

	attr.insn_cnt = (__u32)load_attr->insn_cnt;
	attr.insns = ptr_to_u64(load_attr->insns);
	attr.license = ptr_to_u64(load_attr->license);

	attr.log_level = load_attr->log_level;
	if (attr.log_level) {
		attr.log_buf = ptr_to_u64(load_attr->log_buf);
		attr.log_size = load_attr->log_buf_sz;
	}

	attr.prog_btf_fd = load_attr->prog_btf_fd;
	attr.prog_flags = load_attr->prog_flags;

	attr.func_info_rec_size = load_attr->func_info_rec_size;
	attr.func_info_cnt = load_attr->func_info_cnt;
	attr.func_info = ptr_to_u64(load_attr->func_info);

	attr.line_info_rec_size = load_attr->line_info_rec_size;
	attr.line_info_cnt = load_attr->line_info_cnt;
	attr.line_info = ptr_to_u64(load_attr->line_info);
	attr.fd_array = ptr_to_u64(load_attr->fd_array);

	if (load_attr->name)
		memcpy(attr.prog_name, load_attr->name,
		       min(strlen(load_attr->name), (size_t)BPF_OBJ_NAME_LEN - 1));

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
			if (!finfo) {
				errno = E2BIG;
				goto done;
			}

			attr.func_info = ptr_to_u64(finfo);
			attr.func_info_rec_size = load_attr->func_info_rec_size;
		} else if (!linfo && attr.line_info_cnt &&
			   attr.line_info_rec_size <
			   load_attr->line_info_rec_size) {
			linfo = alloc_zero_tailing_info(load_attr->line_info,
							load_attr->line_info_cnt,
							load_attr->line_info_rec_size,
							attr.line_info_rec_size);
			if (!linfo) {
				errno = E2BIG;
				goto done;
			}

			attr.line_info = ptr_to_u64(linfo);
			attr.line_info_rec_size = load_attr->line_info_rec_size;
		} else {
			break;
		}

		fd = sys_bpf_prog_load(&attr, sizeof(attr));
		if (fd >= 0)
			goto done;
	}

	if (load_attr->log_level || !load_attr->log_buf)
		goto done;

	/* Try again with log */
	attr.log_buf = ptr_to_u64(load_attr->log_buf);
	attr.log_size = load_attr->log_buf_sz;
	attr.log_level = 1;
	load_attr->log_buf[0] = 0;

	fd = sys_bpf_prog_load(&attr, sizeof(attr));
done:
	/* free() doesn't affect errno, so we don't need to restore it */
	free(finfo);
	free(linfo);
	return libbpf_err_errno(fd);
}

int bpf_load_program_xattr(const struct bpf_load_program_attr *load_attr,
			   char *log_buf, size_t log_buf_sz)
{
	struct bpf_prog_load_params p = {};

	if (!load_attr || !log_buf != !log_buf_sz)
		return libbpf_err(-EINVAL);

	p.prog_type = load_attr->prog_type;
	p.expected_attach_type = load_attr->expected_attach_type;
	switch (p.prog_type) {
	case BPF_PROG_TYPE_STRUCT_OPS:
	case BPF_PROG_TYPE_LSM:
		p.attach_btf_id = load_attr->attach_btf_id;
		break;
	case BPF_PROG_TYPE_TRACING:
	case BPF_PROG_TYPE_EXT:
		p.attach_btf_id = load_attr->attach_btf_id;
		p.attach_prog_fd = load_attr->attach_prog_fd;
		break;
	default:
		p.prog_ifindex = load_attr->prog_ifindex;
		p.kern_version = load_attr->kern_version;
	}
	p.insn_cnt = load_attr->insns_cnt;
	p.insns = load_attr->insns;
	p.license = load_attr->license;
	p.log_level = load_attr->log_level;
	p.log_buf = log_buf;
	p.log_buf_sz = log_buf_sz;
	p.prog_btf_fd = load_attr->prog_btf_fd;
	p.func_info_rec_size = load_attr->func_info_rec_size;
	p.func_info_cnt = load_attr->func_info_cnt;
	p.func_info = load_attr->func_info;
	p.line_info_rec_size = load_attr->line_info_rec_size;
	p.line_info_cnt = load_attr->line_info_cnt;
	p.line_info = load_attr->line_info;
	p.name = load_attr->name;
	p.prog_flags = load_attr->prog_flags;

	return libbpf__bpf_prog_load(&p);
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
	int fd;

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

	fd = sys_bpf_prog_load(&attr, sizeof(attr));
	return libbpf_err_errno(fd);
}

int bpf_map_update_elem(int fd, const void *key, const void *value,
			__u64 flags)
{
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, sizeof(attr));
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.value = ptr_to_u64(value);
	attr.flags = flags;

	ret = sys_bpf(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
	return libbpf_err_errno(ret);
}

int bpf_map_lookup_elem(int fd, const void *key, void *value)
{
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, sizeof(attr));
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.value = ptr_to_u64(value);

	ret = sys_bpf(BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
	return libbpf_err_errno(ret);
}

int bpf_map_lookup_elem_flags(int fd, const void *key, void *value, __u64 flags)
{
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, sizeof(attr));
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.value = ptr_to_u64(value);
	attr.flags = flags;

	ret = sys_bpf(BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
	return libbpf_err_errno(ret);
}

int bpf_map_lookup_and_delete_elem(int fd, const void *key, void *value)
{
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, sizeof(attr));
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.value = ptr_to_u64(value);

	ret = sys_bpf(BPF_MAP_LOOKUP_AND_DELETE_ELEM, &attr, sizeof(attr));
	return libbpf_err_errno(ret);
}

int bpf_map_lookup_and_delete_elem_flags(int fd, const void *key, void *value, __u64 flags)
{
	union bpf_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.value = ptr_to_u64(value);
	attr.flags = flags;

	return sys_bpf(BPF_MAP_LOOKUP_AND_DELETE_ELEM, &attr, sizeof(attr));
}

int bpf_map_delete_elem(int fd, const void *key)
{
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, sizeof(attr));
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);

	ret = sys_bpf(BPF_MAP_DELETE_ELEM, &attr, sizeof(attr));
	return libbpf_err_errno(ret);
}

int bpf_map_get_next_key(int fd, const void *key, void *next_key)
{
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, sizeof(attr));
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.next_key = ptr_to_u64(next_key);

	ret = sys_bpf(BPF_MAP_GET_NEXT_KEY, &attr, sizeof(attr));
	return libbpf_err_errno(ret);
}

int bpf_map_freeze(int fd)
{
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, sizeof(attr));
	attr.map_fd = fd;

	ret = sys_bpf(BPF_MAP_FREEZE, &attr, sizeof(attr));
	return libbpf_err_errno(ret);
}

static int bpf_map_batch_common(int cmd, int fd, void  *in_batch,
				void *out_batch, void *keys, void *values,
				__u32 *count,
				const struct bpf_map_batch_opts *opts)
{
	union bpf_attr attr;
	int ret;

	if (!OPTS_VALID(opts, bpf_map_batch_opts))
		return libbpf_err(-EINVAL);

	memset(&attr, 0, sizeof(attr));
	attr.batch.map_fd = fd;
	attr.batch.in_batch = ptr_to_u64(in_batch);
	attr.batch.out_batch = ptr_to_u64(out_batch);
	attr.batch.keys = ptr_to_u64(keys);
	attr.batch.values = ptr_to_u64(values);
	attr.batch.count = *count;
	attr.batch.elem_flags  = OPTS_GET(opts, elem_flags, 0);
	attr.batch.flags = OPTS_GET(opts, flags, 0);

	ret = sys_bpf(cmd, &attr, sizeof(attr));
	*count = attr.batch.count;

	return libbpf_err_errno(ret);
}

int bpf_map_delete_batch(int fd, void *keys, __u32 *count,
			 const struct bpf_map_batch_opts *opts)
{
	return bpf_map_batch_common(BPF_MAP_DELETE_BATCH, fd, NULL,
				    NULL, keys, NULL, count, opts);
}

int bpf_map_lookup_batch(int fd, void *in_batch, void *out_batch, void *keys,
			 void *values, __u32 *count,
			 const struct bpf_map_batch_opts *opts)
{
	return bpf_map_batch_common(BPF_MAP_LOOKUP_BATCH, fd, in_batch,
				    out_batch, keys, values, count, opts);
}

int bpf_map_lookup_and_delete_batch(int fd, void *in_batch, void *out_batch,
				    void *keys, void *values, __u32 *count,
				    const struct bpf_map_batch_opts *opts)
{
	return bpf_map_batch_common(BPF_MAP_LOOKUP_AND_DELETE_BATCH,
				    fd, in_batch, out_batch, keys, values,
				    count, opts);
}

int bpf_map_update_batch(int fd, void *keys, void *values, __u32 *count,
			 const struct bpf_map_batch_opts *opts)
{
	return bpf_map_batch_common(BPF_MAP_UPDATE_BATCH, fd, NULL, NULL,
				    keys, values, count, opts);
}

int bpf_obj_pin(int fd, const char *pathname)
{
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, sizeof(attr));
	attr.pathname = ptr_to_u64((void *)pathname);
	attr.bpf_fd = fd;

	ret = sys_bpf(BPF_OBJ_PIN, &attr, sizeof(attr));
	return libbpf_err_errno(ret);
}

int bpf_obj_get(const char *pathname)
{
	union bpf_attr attr;
	int fd;

	memset(&attr, 0, sizeof(attr));
	attr.pathname = ptr_to_u64((void *)pathname);

	fd = sys_bpf(BPF_OBJ_GET, &attr, sizeof(attr));
	return libbpf_err_errno(fd);
}

int bpf_prog_attach(int prog_fd, int target_fd, enum bpf_attach_type type,
		    unsigned int flags)
{
	DECLARE_LIBBPF_OPTS(bpf_prog_attach_opts, opts,
		.flags = flags,
	);

	return bpf_prog_attach_xattr(prog_fd, target_fd, type, &opts);
}

int bpf_prog_attach_xattr(int prog_fd, int target_fd,
			  enum bpf_attach_type type,
			  const struct bpf_prog_attach_opts *opts)
{
	union bpf_attr attr;
	int ret;

	if (!OPTS_VALID(opts, bpf_prog_attach_opts))
		return libbpf_err(-EINVAL);

	memset(&attr, 0, sizeof(attr));
	attr.target_fd	   = target_fd;
	attr.attach_bpf_fd = prog_fd;
	attr.attach_type   = type;
	attr.attach_flags  = OPTS_GET(opts, flags, 0);
	attr.replace_bpf_fd = OPTS_GET(opts, replace_prog_fd, 0);

	ret = sys_bpf(BPF_PROG_ATTACH, &attr, sizeof(attr));
	return libbpf_err_errno(ret);
}

int bpf_prog_detach(int target_fd, enum bpf_attach_type type)
{
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, sizeof(attr));
	attr.target_fd	 = target_fd;
	attr.attach_type = type;

	ret = sys_bpf(BPF_PROG_DETACH, &attr, sizeof(attr));
	return libbpf_err_errno(ret);
}

int bpf_prog_detach2(int prog_fd, int target_fd, enum bpf_attach_type type)
{
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, sizeof(attr));
	attr.target_fd	 = target_fd;
	attr.attach_bpf_fd = prog_fd;
	attr.attach_type = type;

	ret = sys_bpf(BPF_PROG_DETACH, &attr, sizeof(attr));
	return libbpf_err_errno(ret);
}

int bpf_link_create(int prog_fd, int target_fd,
		    enum bpf_attach_type attach_type,
		    const struct bpf_link_create_opts *opts)
{
	__u32 target_btf_id, iter_info_len;
	union bpf_attr attr;
	int fd;

	if (!OPTS_VALID(opts, bpf_link_create_opts))
		return libbpf_err(-EINVAL);

	iter_info_len = OPTS_GET(opts, iter_info_len, 0);
	target_btf_id = OPTS_GET(opts, target_btf_id, 0);

	/* validate we don't have unexpected combinations of non-zero fields */
	if (iter_info_len || target_btf_id) {
		if (iter_info_len && target_btf_id)
			return libbpf_err(-EINVAL);
		if (!OPTS_ZEROED(opts, target_btf_id))
			return libbpf_err(-EINVAL);
	}

	memset(&attr, 0, sizeof(attr));
	attr.link_create.prog_fd = prog_fd;
	attr.link_create.target_fd = target_fd;
	attr.link_create.attach_type = attach_type;
	attr.link_create.flags = OPTS_GET(opts, flags, 0);

	if (target_btf_id) {
		attr.link_create.target_btf_id = target_btf_id;
		goto proceed;
	}

	switch (attach_type) {
	case BPF_TRACE_ITER:
		attr.link_create.iter_info = ptr_to_u64(OPTS_GET(opts, iter_info, (void *)0));
		attr.link_create.iter_info_len = iter_info_len;
		break;
	case BPF_PERF_EVENT:
		attr.link_create.perf_event.bpf_cookie = OPTS_GET(opts, perf_event.bpf_cookie, 0);
		if (!OPTS_ZEROED(opts, perf_event))
			return libbpf_err(-EINVAL);
		break;
	default:
		if (!OPTS_ZEROED(opts, flags))
			return libbpf_err(-EINVAL);
		break;
	}
proceed:
	fd = sys_bpf(BPF_LINK_CREATE, &attr, sizeof(attr));
	return libbpf_err_errno(fd);
}

int bpf_link_detach(int link_fd)
{
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, sizeof(attr));
	attr.link_detach.link_fd = link_fd;

	ret = sys_bpf(BPF_LINK_DETACH, &attr, sizeof(attr));
	return libbpf_err_errno(ret);
}

int bpf_link_update(int link_fd, int new_prog_fd,
		    const struct bpf_link_update_opts *opts)
{
	union bpf_attr attr;
	int ret;

	if (!OPTS_VALID(opts, bpf_link_update_opts))
		return libbpf_err(-EINVAL);

	memset(&attr, 0, sizeof(attr));
	attr.link_update.link_fd = link_fd;
	attr.link_update.new_prog_fd = new_prog_fd;
	attr.link_update.flags = OPTS_GET(opts, flags, 0);
	attr.link_update.old_prog_fd = OPTS_GET(opts, old_prog_fd, 0);

	ret = sys_bpf(BPF_LINK_UPDATE, &attr, sizeof(attr));
	return libbpf_err_errno(ret);
}

int bpf_iter_create(int link_fd)
{
	union bpf_attr attr;
	int fd;

	memset(&attr, 0, sizeof(attr));
	attr.iter_create.link_fd = link_fd;

	fd = sys_bpf(BPF_ITER_CREATE, &attr, sizeof(attr));
	return libbpf_err_errno(fd);
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

	return libbpf_err_errno(ret);
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

	return libbpf_err_errno(ret);
}

int bpf_prog_test_run_xattr(struct bpf_prog_test_run_attr *test_attr)
{
	union bpf_attr attr;
	int ret;

	if (!test_attr->data_out && test_attr->data_size_out > 0)
		return libbpf_err(-EINVAL);

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

	return libbpf_err_errno(ret);
}

int bpf_prog_test_run_opts(int prog_fd, struct bpf_test_run_opts *opts)
{
	union bpf_attr attr;
	int ret;

	if (!OPTS_VALID(opts, bpf_test_run_opts))
		return libbpf_err(-EINVAL);

	memset(&attr, 0, sizeof(attr));
	attr.test.prog_fd = prog_fd;
	attr.test.cpu = OPTS_GET(opts, cpu, 0);
	attr.test.flags = OPTS_GET(opts, flags, 0);
	attr.test.repeat = OPTS_GET(opts, repeat, 0);
	attr.test.duration = OPTS_GET(opts, duration, 0);
	attr.test.ctx_size_in = OPTS_GET(opts, ctx_size_in, 0);
	attr.test.ctx_size_out = OPTS_GET(opts, ctx_size_out, 0);
	attr.test.data_size_in = OPTS_GET(opts, data_size_in, 0);
	attr.test.data_size_out = OPTS_GET(opts, data_size_out, 0);
	attr.test.ctx_in = ptr_to_u64(OPTS_GET(opts, ctx_in, NULL));
	attr.test.ctx_out = ptr_to_u64(OPTS_GET(opts, ctx_out, NULL));
	attr.test.data_in = ptr_to_u64(OPTS_GET(opts, data_in, NULL));
	attr.test.data_out = ptr_to_u64(OPTS_GET(opts, data_out, NULL));

	ret = sys_bpf(BPF_PROG_TEST_RUN, &attr, sizeof(attr));

	OPTS_SET(opts, data_size_out, attr.test.data_size_out);
	OPTS_SET(opts, ctx_size_out, attr.test.ctx_size_out);
	OPTS_SET(opts, duration, attr.test.duration);
	OPTS_SET(opts, retval, attr.test.retval);

	return libbpf_err_errno(ret);
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

	return libbpf_err_errno(err);
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

int bpf_link_get_next_id(__u32 start_id, __u32 *next_id)
{
	return bpf_obj_get_next_id(start_id, next_id, BPF_LINK_GET_NEXT_ID);
}

int bpf_prog_get_fd_by_id(__u32 id)
{
	union bpf_attr attr;
	int fd;

	memset(&attr, 0, sizeof(attr));
	attr.prog_id = id;

	fd = sys_bpf(BPF_PROG_GET_FD_BY_ID, &attr, sizeof(attr));
	return libbpf_err_errno(fd);
}

int bpf_map_get_fd_by_id(__u32 id)
{
	union bpf_attr attr;
	int fd;

	memset(&attr, 0, sizeof(attr));
	attr.map_id = id;

	fd = sys_bpf(BPF_MAP_GET_FD_BY_ID, &attr, sizeof(attr));
	return libbpf_err_errno(fd);
}

int bpf_btf_get_fd_by_id(__u32 id)
{
	union bpf_attr attr;
	int fd;

	memset(&attr, 0, sizeof(attr));
	attr.btf_id = id;

	fd = sys_bpf(BPF_BTF_GET_FD_BY_ID, &attr, sizeof(attr));
	return libbpf_err_errno(fd);
}

int bpf_link_get_fd_by_id(__u32 id)
{
	union bpf_attr attr;
	int fd;

	memset(&attr, 0, sizeof(attr));
	attr.link_id = id;

	fd = sys_bpf(BPF_LINK_GET_FD_BY_ID, &attr, sizeof(attr));
	return libbpf_err_errno(fd);
}

int bpf_obj_get_info_by_fd(int bpf_fd, void *info, __u32 *info_len)
{
	union bpf_attr attr;
	int err;

	memset(&attr, 0, sizeof(attr));
	attr.info.bpf_fd = bpf_fd;
	attr.info.info_len = *info_len;
	attr.info.info = ptr_to_u64(info);

	err = sys_bpf(BPF_OBJ_GET_INFO_BY_FD, &attr, sizeof(attr));

	if (!err)
		*info_len = attr.info.info_len;

	return libbpf_err_errno(err);
}

int bpf_raw_tracepoint_open(const char *name, int prog_fd)
{
	union bpf_attr attr;
	int fd;

	memset(&attr, 0, sizeof(attr));
	attr.raw_tracepoint.name = ptr_to_u64(name);
	attr.raw_tracepoint.prog_fd = prog_fd;

	fd = sys_bpf(BPF_RAW_TRACEPOINT_OPEN, &attr, sizeof(attr));
	return libbpf_err_errno(fd);
}

int bpf_load_btf(const void *btf, __u32 btf_size, char *log_buf, __u32 log_buf_size,
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

	if (fd < 0 && !do_log && log_buf && log_buf_size) {
		do_log = true;
		goto retry;
	}

	return libbpf_err_errno(fd);
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

	return libbpf_err_errno(err);
}

int bpf_enable_stats(enum bpf_stats_type type)
{
	union bpf_attr attr;
	int fd;

	memset(&attr, 0, sizeof(attr));
	attr.enable_stats.type = type;

	fd = sys_bpf(BPF_ENABLE_STATS, &attr, sizeof(attr));
	return libbpf_err_errno(fd);
}

int bpf_prog_bind_map(int prog_fd, int map_fd,
		      const struct bpf_prog_bind_opts *opts)
{
	union bpf_attr attr;
	int ret;

	if (!OPTS_VALID(opts, bpf_prog_bind_opts))
		return libbpf_err(-EINVAL);

	memset(&attr, 0, sizeof(attr));
	attr.prog_bind_map.prog_fd = prog_fd;
	attr.prog_bind_map.map_fd = map_fd;
	attr.prog_bind_map.flags = OPTS_GET(opts, flags, 0);

	ret = sys_bpf(BPF_PROG_BIND_MAP, &attr, sizeof(attr));
	return libbpf_err_errno(ret);
}
