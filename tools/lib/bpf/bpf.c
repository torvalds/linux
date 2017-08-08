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
#include <memory.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <linux/bpf.h>
#include "bpf.h"

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

int bpf_create_map(enum bpf_map_type map_type, int key_size,
		   int value_size, int max_entries, __u32 map_flags)
{
	union bpf_attr attr;

	memset(&attr, '\0', sizeof(attr));

	attr.map_type = map_type;
	attr.key_size = key_size;
	attr.value_size = value_size;
	attr.max_entries = max_entries;
	attr.map_flags = map_flags;

	return sys_bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}

int bpf_create_map_in_map(enum bpf_map_type map_type, int key_size,
			  int inner_map_fd, int max_entries, __u32 map_flags)
{
	union bpf_attr attr;

	memset(&attr, '\0', sizeof(attr));

	attr.map_type = map_type;
	attr.key_size = key_size;
	attr.value_size = 4;
	attr.inner_map_fd = inner_map_fd;
	attr.max_entries = max_entries;
	attr.map_flags = map_flags;

	return sys_bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}

int bpf_load_program(enum bpf_prog_type type, const struct bpf_insn *insns,
		     size_t insns_cnt, const char *license,
		     __u32 kern_version, char *log_buf, size_t log_buf_sz)
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

int bpf_verify_program(enum bpf_prog_type type, const struct bpf_insn *insns,
		       size_t insns_cnt, int strict_alignment,
		       const char *license, __u32 kern_version,
		       char *log_buf, size_t log_buf_sz, int log_level)
{
	union bpf_attr attr;

	bzero(&attr, sizeof(attr));
	attr.prog_type = type;
	attr.insn_cnt = (__u32)insns_cnt;
	attr.insns = ptr_to_u64(insns);
	attr.license = ptr_to_u64(license);
	attr.log_buf = ptr_to_u64(log_buf);
	attr.log_size = log_buf_sz;
	attr.log_level = log_level;
	log_buf[0] = 0;
	attr.kern_version = kern_version;
	attr.prog_flags = strict_alignment ? BPF_F_STRICT_ALIGNMENT : 0;

	return sys_bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
}

int bpf_map_update_elem(int fd, const void *key, const void *value,
			__u64 flags)
{
	union bpf_attr attr;

	bzero(&attr, sizeof(attr));
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.value = ptr_to_u64(value);
	attr.flags = flags;

	return sys_bpf(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

int bpf_map_lookup_elem(int fd, const void *key, void *value)
{
	union bpf_attr attr;

	bzero(&attr, sizeof(attr));
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.value = ptr_to_u64(value);

	return sys_bpf(BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
}

int bpf_map_delete_elem(int fd, const void *key)
{
	union bpf_attr attr;

	bzero(&attr, sizeof(attr));
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);

	return sys_bpf(BPF_MAP_DELETE_ELEM, &attr, sizeof(attr));
}

int bpf_map_get_next_key(int fd, const void *key, void *next_key)
{
	union bpf_attr attr;

	bzero(&attr, sizeof(attr));
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.next_key = ptr_to_u64(next_key);

	return sys_bpf(BPF_MAP_GET_NEXT_KEY, &attr, sizeof(attr));
}

int bpf_obj_pin(int fd, const char *pathname)
{
	union bpf_attr attr;

	bzero(&attr, sizeof(attr));
	attr.pathname = ptr_to_u64((void *)pathname);
	attr.bpf_fd = fd;

	return sys_bpf(BPF_OBJ_PIN, &attr, sizeof(attr));
}

int bpf_obj_get(const char *pathname)
{
	union bpf_attr attr;

	bzero(&attr, sizeof(attr));
	attr.pathname = ptr_to_u64((void *)pathname);

	return sys_bpf(BPF_OBJ_GET, &attr, sizeof(attr));
}

int bpf_prog_attach(int prog_fd, int target_fd, enum bpf_attach_type type,
		    unsigned int flags)
{
	union bpf_attr attr;

	bzero(&attr, sizeof(attr));
	attr.target_fd	   = target_fd;
	attr.attach_bpf_fd = prog_fd;
	attr.attach_type   = type;
	attr.attach_flags  = flags;

	return sys_bpf(BPF_PROG_ATTACH, &attr, sizeof(attr));
}

int bpf_prog_detach(int target_fd, enum bpf_attach_type type)
{
	union bpf_attr attr;

	bzero(&attr, sizeof(attr));
	attr.target_fd	 = target_fd;
	attr.attach_type = type;

	return sys_bpf(BPF_PROG_DETACH, &attr, sizeof(attr));
}

int bpf_prog_test_run(int prog_fd, int repeat, void *data, __u32 size,
		      void *data_out, __u32 *size_out, __u32 *retval,
		      __u32 *duration)
{
	union bpf_attr attr;
	int ret;

	bzero(&attr, sizeof(attr));
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

int bpf_prog_get_next_id(__u32 start_id, __u32 *next_id)
{
	union bpf_attr attr;
	int err;

	bzero(&attr, sizeof(attr));
	attr.start_id = start_id;

	err = sys_bpf(BPF_PROG_GET_NEXT_ID, &attr, sizeof(attr));
	if (!err)
		*next_id = attr.next_id;

	return err;
}

int bpf_map_get_next_id(__u32 start_id, __u32 *next_id)
{
	union bpf_attr attr;
	int err;

	bzero(&attr, sizeof(attr));
	attr.start_id = start_id;

	err = sys_bpf(BPF_MAP_GET_NEXT_ID, &attr, sizeof(attr));
	if (!err)
		*next_id = attr.next_id;

	return err;
}

int bpf_prog_get_fd_by_id(__u32 id)
{
	union bpf_attr attr;

	bzero(&attr, sizeof(attr));
	attr.prog_id = id;

	return sys_bpf(BPF_PROG_GET_FD_BY_ID, &attr, sizeof(attr));
}

int bpf_map_get_fd_by_id(__u32 id)
{
	union bpf_attr attr;

	bzero(&attr, sizeof(attr));
	attr.map_id = id;

	return sys_bpf(BPF_MAP_GET_FD_BY_ID, &attr, sizeof(attr));
}

int bpf_obj_get_info_by_fd(int prog_fd, void *info, __u32 *info_len)
{
	union bpf_attr attr;
	int err;

	bzero(&attr, sizeof(attr));
	attr.info.bpf_fd = prog_fd;
	attr.info.info_len = *info_len;
	attr.info.info = ptr_to_u64(info);

	err = sys_bpf(BPF_OBJ_GET_INFO_BY_FD, &attr, sizeof(attr));
	if (!err)
		*info_len = attr.info.info_len;

	return err;
}
