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
#include <linux/filter.h>
#include <linux/kernel.h>
#include <limits.h>
#include <sys/resource.h>
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
# elif defined(__mips__) && defined(_ABIO32)
#  define __NR_bpf 4355
# elif defined(__mips__) && defined(_ABIN32)
#  define __NR_bpf 6319
# elif defined(__mips__) && defined(_ABI64)
#  define __NR_bpf 5315
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

static inline int sys_bpf_fd(enum bpf_cmd cmd, union bpf_attr *attr,
			     unsigned int size)
{
	int fd;

	fd = sys_bpf(cmd, attr, size);
	return ensure_good_fd(fd);
}

int sys_bpf_prog_load(union bpf_attr *attr, unsigned int size, int attempts)
{
	int fd;

	do {
		fd = sys_bpf_fd(BPF_PROG_LOAD, attr, size);
	} while (fd < 0 && errno == EAGAIN && --attempts > 0);

	return fd;
}

/* Probe whether kernel switched from memlock-based (RLIMIT_MEMLOCK) to
 * memcg-based memory accounting for BPF maps and progs. This was done in [0].
 * We use the support for bpf_ktime_get_coarse_ns() helper, which was added in
 * the same 5.11 Linux release ([1]), to detect memcg-based accounting for BPF.
 *
 *   [0] https://lore.kernel.org/bpf/20201201215900.3569844-1-guro@fb.com/
 *   [1] d05512618056 ("bpf: Add bpf_ktime_get_coarse_ns helper")
 */
int probe_memcg_account(void)
{
	const size_t attr_sz = offsetofend(union bpf_attr, attach_btf_obj_fd);
	struct bpf_insn insns[] = {
		BPF_EMIT_CALL(BPF_FUNC_ktime_get_coarse_ns),
		BPF_EXIT_INSN(),
	};
	size_t insn_cnt = ARRAY_SIZE(insns);
	union bpf_attr attr;
	int prog_fd;

	/* attempt loading freplace trying to use custom BTF */
	memset(&attr, 0, attr_sz);
	attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
	attr.insns = ptr_to_u64(insns);
	attr.insn_cnt = insn_cnt;
	attr.license = ptr_to_u64("GPL");

	prog_fd = sys_bpf_fd(BPF_PROG_LOAD, &attr, attr_sz);
	if (prog_fd >= 0) {
		close(prog_fd);
		return 1;
	}
	return 0;
}

static bool memlock_bumped;
static rlim_t memlock_rlim = RLIM_INFINITY;

int libbpf_set_memlock_rlim(size_t memlock_bytes)
{
	if (memlock_bumped)
		return libbpf_err(-EBUSY);

	memlock_rlim = memlock_bytes;
	return 0;
}

int bump_rlimit_memlock(void)
{
	struct rlimit rlim;

	/* if kernel supports memcg-based accounting, skip bumping RLIMIT_MEMLOCK */
	if (memlock_bumped || kernel_supports(NULL, FEAT_MEMCG_ACCOUNT))
		return 0;

	memlock_bumped = true;

	/* zero memlock_rlim_max disables auto-bumping RLIMIT_MEMLOCK */
	if (memlock_rlim == 0)
		return 0;

	rlim.rlim_cur = rlim.rlim_max = memlock_rlim;
	if (setrlimit(RLIMIT_MEMLOCK, &rlim))
		return -errno;

	return 0;
}

int bpf_map_create(enum bpf_map_type map_type,
		   const char *map_name,
		   __u32 key_size,
		   __u32 value_size,
		   __u32 max_entries,
		   const struct bpf_map_create_opts *opts)
{
	const size_t attr_sz = offsetofend(union bpf_attr, map_extra);
	union bpf_attr attr;
	int fd;

	bump_rlimit_memlock();

	memset(&attr, 0, attr_sz);

	if (!OPTS_VALID(opts, bpf_map_create_opts))
		return libbpf_err(-EINVAL);

	attr.map_type = map_type;
	if (map_name && kernel_supports(NULL, FEAT_PROG_NAME))
		libbpf_strlcpy(attr.map_name, map_name, sizeof(attr.map_name));
	attr.key_size = key_size;
	attr.value_size = value_size;
	attr.max_entries = max_entries;

	attr.btf_fd = OPTS_GET(opts, btf_fd, 0);
	attr.btf_key_type_id = OPTS_GET(opts, btf_key_type_id, 0);
	attr.btf_value_type_id = OPTS_GET(opts, btf_value_type_id, 0);
	attr.btf_vmlinux_value_type_id = OPTS_GET(opts, btf_vmlinux_value_type_id, 0);

	attr.inner_map_fd = OPTS_GET(opts, inner_map_fd, 0);
	attr.map_flags = OPTS_GET(opts, map_flags, 0);
	attr.map_extra = OPTS_GET(opts, map_extra, 0);
	attr.numa_node = OPTS_GET(opts, numa_node, 0);
	attr.map_ifindex = OPTS_GET(opts, map_ifindex, 0);

	fd = sys_bpf_fd(BPF_MAP_CREATE, &attr, attr_sz);
	return libbpf_err_errno(fd);
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

int bpf_prog_load(enum bpf_prog_type prog_type,
		  const char *prog_name, const char *license,
		  const struct bpf_insn *insns, size_t insn_cnt,
		  struct bpf_prog_load_opts *opts)
{
	const size_t attr_sz = offsetofend(union bpf_attr, log_true_size);
	void *finfo = NULL, *linfo = NULL;
	const char *func_info, *line_info;
	__u32 log_size, log_level, attach_prog_fd, attach_btf_obj_fd;
	__u32 func_info_rec_size, line_info_rec_size;
	int fd, attempts;
	union bpf_attr attr;
	char *log_buf;

	bump_rlimit_memlock();

	if (!OPTS_VALID(opts, bpf_prog_load_opts))
		return libbpf_err(-EINVAL);

	attempts = OPTS_GET(opts, attempts, 0);
	if (attempts < 0)
		return libbpf_err(-EINVAL);
	if (attempts == 0)
		attempts = PROG_LOAD_ATTEMPTS;

	memset(&attr, 0, attr_sz);

	attr.prog_type = prog_type;
	attr.expected_attach_type = OPTS_GET(opts, expected_attach_type, 0);

	attr.prog_btf_fd = OPTS_GET(opts, prog_btf_fd, 0);
	attr.prog_flags = OPTS_GET(opts, prog_flags, 0);
	attr.prog_ifindex = OPTS_GET(opts, prog_ifindex, 0);
	attr.kern_version = OPTS_GET(opts, kern_version, 0);

	if (prog_name && kernel_supports(NULL, FEAT_PROG_NAME))
		libbpf_strlcpy(attr.prog_name, prog_name, sizeof(attr.prog_name));
	attr.license = ptr_to_u64(license);

	if (insn_cnt > UINT_MAX)
		return libbpf_err(-E2BIG);

	attr.insns = ptr_to_u64(insns);
	attr.insn_cnt = (__u32)insn_cnt;

	attach_prog_fd = OPTS_GET(opts, attach_prog_fd, 0);
	attach_btf_obj_fd = OPTS_GET(opts, attach_btf_obj_fd, 0);

	if (attach_prog_fd && attach_btf_obj_fd)
		return libbpf_err(-EINVAL);

	attr.attach_btf_id = OPTS_GET(opts, attach_btf_id, 0);
	if (attach_prog_fd)
		attr.attach_prog_fd = attach_prog_fd;
	else
		attr.attach_btf_obj_fd = attach_btf_obj_fd;

	log_buf = OPTS_GET(opts, log_buf, NULL);
	log_size = OPTS_GET(opts, log_size, 0);
	log_level = OPTS_GET(opts, log_level, 0);

	if (!!log_buf != !!log_size)
		return libbpf_err(-EINVAL);

	func_info_rec_size = OPTS_GET(opts, func_info_rec_size, 0);
	func_info = OPTS_GET(opts, func_info, NULL);
	attr.func_info_rec_size = func_info_rec_size;
	attr.func_info = ptr_to_u64(func_info);
	attr.func_info_cnt = OPTS_GET(opts, func_info_cnt, 0);

	line_info_rec_size = OPTS_GET(opts, line_info_rec_size, 0);
	line_info = OPTS_GET(opts, line_info, NULL);
	attr.line_info_rec_size = line_info_rec_size;
	attr.line_info = ptr_to_u64(line_info);
	attr.line_info_cnt = OPTS_GET(opts, line_info_cnt, 0);

	attr.fd_array = ptr_to_u64(OPTS_GET(opts, fd_array, NULL));

	if (log_level) {
		attr.log_buf = ptr_to_u64(log_buf);
		attr.log_size = log_size;
		attr.log_level = log_level;
	}

	fd = sys_bpf_prog_load(&attr, attr_sz, attempts);
	OPTS_SET(opts, log_true_size, attr.log_true_size);
	if (fd >= 0)
		return fd;

	/* After bpf_prog_load, the kernel may modify certain attributes
	 * to give user space a hint how to deal with loading failure.
	 * Check to see whether we can make some changes and load again.
	 */
	while (errno == E2BIG && (!finfo || !linfo)) {
		if (!finfo && attr.func_info_cnt &&
		    attr.func_info_rec_size < func_info_rec_size) {
			/* try with corrected func info records */
			finfo = alloc_zero_tailing_info(func_info,
							attr.func_info_cnt,
							func_info_rec_size,
							attr.func_info_rec_size);
			if (!finfo) {
				errno = E2BIG;
				goto done;
			}

			attr.func_info = ptr_to_u64(finfo);
			attr.func_info_rec_size = func_info_rec_size;
		} else if (!linfo && attr.line_info_cnt &&
			   attr.line_info_rec_size < line_info_rec_size) {
			linfo = alloc_zero_tailing_info(line_info,
							attr.line_info_cnt,
							line_info_rec_size,
							attr.line_info_rec_size);
			if (!linfo) {
				errno = E2BIG;
				goto done;
			}

			attr.line_info = ptr_to_u64(linfo);
			attr.line_info_rec_size = line_info_rec_size;
		} else {
			break;
		}

		fd = sys_bpf_prog_load(&attr, attr_sz, attempts);
		OPTS_SET(opts, log_true_size, attr.log_true_size);
		if (fd >= 0)
			goto done;
	}

	if (log_level == 0 && log_buf) {
		/* log_level == 0 with non-NULL log_buf requires retrying on error
		 * with log_level == 1 and log_buf/log_buf_size set, to get details of
		 * failure
		 */
		attr.log_buf = ptr_to_u64(log_buf);
		attr.log_size = log_size;
		attr.log_level = 1;

		fd = sys_bpf_prog_load(&attr, attr_sz, attempts);
		OPTS_SET(opts, log_true_size, attr.log_true_size);
	}
done:
	/* free() doesn't affect errno, so we don't need to restore it */
	free(finfo);
	free(linfo);
	return libbpf_err_errno(fd);
}

int bpf_map_update_elem(int fd, const void *key, const void *value,
			__u64 flags)
{
	const size_t attr_sz = offsetofend(union bpf_attr, flags);
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, attr_sz);
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.value = ptr_to_u64(value);
	attr.flags = flags;

	ret = sys_bpf(BPF_MAP_UPDATE_ELEM, &attr, attr_sz);
	return libbpf_err_errno(ret);
}

int bpf_map_lookup_elem(int fd, const void *key, void *value)
{
	const size_t attr_sz = offsetofend(union bpf_attr, flags);
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, attr_sz);
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.value = ptr_to_u64(value);

	ret = sys_bpf(BPF_MAP_LOOKUP_ELEM, &attr, attr_sz);
	return libbpf_err_errno(ret);
}

int bpf_map_lookup_elem_flags(int fd, const void *key, void *value, __u64 flags)
{
	const size_t attr_sz = offsetofend(union bpf_attr, flags);
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, attr_sz);
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.value = ptr_to_u64(value);
	attr.flags = flags;

	ret = sys_bpf(BPF_MAP_LOOKUP_ELEM, &attr, attr_sz);
	return libbpf_err_errno(ret);
}

int bpf_map_lookup_and_delete_elem(int fd, const void *key, void *value)
{
	const size_t attr_sz = offsetofend(union bpf_attr, flags);
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, attr_sz);
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.value = ptr_to_u64(value);

	ret = sys_bpf(BPF_MAP_LOOKUP_AND_DELETE_ELEM, &attr, attr_sz);
	return libbpf_err_errno(ret);
}

int bpf_map_lookup_and_delete_elem_flags(int fd, const void *key, void *value, __u64 flags)
{
	const size_t attr_sz = offsetofend(union bpf_attr, flags);
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, attr_sz);
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.value = ptr_to_u64(value);
	attr.flags = flags;

	ret = sys_bpf(BPF_MAP_LOOKUP_AND_DELETE_ELEM, &attr, attr_sz);
	return libbpf_err_errno(ret);
}

int bpf_map_delete_elem(int fd, const void *key)
{
	const size_t attr_sz = offsetofend(union bpf_attr, flags);
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, attr_sz);
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);

	ret = sys_bpf(BPF_MAP_DELETE_ELEM, &attr, attr_sz);
	return libbpf_err_errno(ret);
}

int bpf_map_delete_elem_flags(int fd, const void *key, __u64 flags)
{
	const size_t attr_sz = offsetofend(union bpf_attr, flags);
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, attr_sz);
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.flags = flags;

	ret = sys_bpf(BPF_MAP_DELETE_ELEM, &attr, attr_sz);
	return libbpf_err_errno(ret);
}

int bpf_map_get_next_key(int fd, const void *key, void *next_key)
{
	const size_t attr_sz = offsetofend(union bpf_attr, next_key);
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, attr_sz);
	attr.map_fd = fd;
	attr.key = ptr_to_u64(key);
	attr.next_key = ptr_to_u64(next_key);

	ret = sys_bpf(BPF_MAP_GET_NEXT_KEY, &attr, attr_sz);
	return libbpf_err_errno(ret);
}

int bpf_map_freeze(int fd)
{
	const size_t attr_sz = offsetofend(union bpf_attr, map_fd);
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, attr_sz);
	attr.map_fd = fd;

	ret = sys_bpf(BPF_MAP_FREEZE, &attr, attr_sz);
	return libbpf_err_errno(ret);
}

static int bpf_map_batch_common(int cmd, int fd, void  *in_batch,
				void *out_batch, void *keys, void *values,
				__u32 *count,
				const struct bpf_map_batch_opts *opts)
{
	const size_t attr_sz = offsetofend(union bpf_attr, batch);
	union bpf_attr attr;
	int ret;

	if (!OPTS_VALID(opts, bpf_map_batch_opts))
		return libbpf_err(-EINVAL);

	memset(&attr, 0, attr_sz);
	attr.batch.map_fd = fd;
	attr.batch.in_batch = ptr_to_u64(in_batch);
	attr.batch.out_batch = ptr_to_u64(out_batch);
	attr.batch.keys = ptr_to_u64(keys);
	attr.batch.values = ptr_to_u64(values);
	attr.batch.count = *count;
	attr.batch.elem_flags  = OPTS_GET(opts, elem_flags, 0);
	attr.batch.flags = OPTS_GET(opts, flags, 0);

	ret = sys_bpf(cmd, &attr, attr_sz);
	*count = attr.batch.count;

	return libbpf_err_errno(ret);
}

int bpf_map_delete_batch(int fd, const void *keys, __u32 *count,
			 const struct bpf_map_batch_opts *opts)
{
	return bpf_map_batch_common(BPF_MAP_DELETE_BATCH, fd, NULL,
				    NULL, (void *)keys, NULL, count, opts);
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

int bpf_map_update_batch(int fd, const void *keys, const void *values, __u32 *count,
			 const struct bpf_map_batch_opts *opts)
{
	return bpf_map_batch_common(BPF_MAP_UPDATE_BATCH, fd, NULL, NULL,
				    (void *)keys, (void *)values, count, opts);
}

int bpf_obj_pin_opts(int fd, const char *pathname, const struct bpf_obj_pin_opts *opts)
{
	const size_t attr_sz = offsetofend(union bpf_attr, path_fd);
	union bpf_attr attr;
	int ret;

	if (!OPTS_VALID(opts, bpf_obj_pin_opts))
		return libbpf_err(-EINVAL);

	memset(&attr, 0, attr_sz);
	attr.path_fd = OPTS_GET(opts, path_fd, 0);
	attr.pathname = ptr_to_u64((void *)pathname);
	attr.file_flags = OPTS_GET(opts, file_flags, 0);
	attr.bpf_fd = fd;

	ret = sys_bpf(BPF_OBJ_PIN, &attr, attr_sz);
	return libbpf_err_errno(ret);
}

int bpf_obj_pin(int fd, const char *pathname)
{
	return bpf_obj_pin_opts(fd, pathname, NULL);
}

int bpf_obj_get(const char *pathname)
{
	return bpf_obj_get_opts(pathname, NULL);
}

int bpf_obj_get_opts(const char *pathname, const struct bpf_obj_get_opts *opts)
{
	const size_t attr_sz = offsetofend(union bpf_attr, path_fd);
	union bpf_attr attr;
	int fd;

	if (!OPTS_VALID(opts, bpf_obj_get_opts))
		return libbpf_err(-EINVAL);

	memset(&attr, 0, attr_sz);
	attr.path_fd = OPTS_GET(opts, path_fd, 0);
	attr.pathname = ptr_to_u64((void *)pathname);
	attr.file_flags = OPTS_GET(opts, file_flags, 0);

	fd = sys_bpf_fd(BPF_OBJ_GET, &attr, attr_sz);
	return libbpf_err_errno(fd);
}

int bpf_prog_attach(int prog_fd, int target_fd, enum bpf_attach_type type,
		    unsigned int flags)
{
	DECLARE_LIBBPF_OPTS(bpf_prog_attach_opts, opts,
		.flags = flags,
	);

	return bpf_prog_attach_opts(prog_fd, target_fd, type, &opts);
}

int bpf_prog_attach_opts(int prog_fd, int target_fd,
			  enum bpf_attach_type type,
			  const struct bpf_prog_attach_opts *opts)
{
	const size_t attr_sz = offsetofend(union bpf_attr, replace_bpf_fd);
	union bpf_attr attr;
	int ret;

	if (!OPTS_VALID(opts, bpf_prog_attach_opts))
		return libbpf_err(-EINVAL);

	memset(&attr, 0, attr_sz);
	attr.target_fd	   = target_fd;
	attr.attach_bpf_fd = prog_fd;
	attr.attach_type   = type;
	attr.attach_flags  = OPTS_GET(opts, flags, 0);
	attr.replace_bpf_fd = OPTS_GET(opts, replace_prog_fd, 0);

	ret = sys_bpf(BPF_PROG_ATTACH, &attr, attr_sz);
	return libbpf_err_errno(ret);
}

int bpf_prog_detach(int target_fd, enum bpf_attach_type type)
{
	const size_t attr_sz = offsetofend(union bpf_attr, replace_bpf_fd);
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, attr_sz);
	attr.target_fd	 = target_fd;
	attr.attach_type = type;

	ret = sys_bpf(BPF_PROG_DETACH, &attr, attr_sz);
	return libbpf_err_errno(ret);
}

int bpf_prog_detach2(int prog_fd, int target_fd, enum bpf_attach_type type)
{
	const size_t attr_sz = offsetofend(union bpf_attr, replace_bpf_fd);
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, attr_sz);
	attr.target_fd	 = target_fd;
	attr.attach_bpf_fd = prog_fd;
	attr.attach_type = type;

	ret = sys_bpf(BPF_PROG_DETACH, &attr, attr_sz);
	return libbpf_err_errno(ret);
}

int bpf_link_create(int prog_fd, int target_fd,
		    enum bpf_attach_type attach_type,
		    const struct bpf_link_create_opts *opts)
{
	const size_t attr_sz = offsetofend(union bpf_attr, link_create);
	__u32 target_btf_id, iter_info_len;
	union bpf_attr attr;
	int fd, err;

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

	memset(&attr, 0, attr_sz);
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
	case BPF_TRACE_KPROBE_MULTI:
		attr.link_create.kprobe_multi.flags = OPTS_GET(opts, kprobe_multi.flags, 0);
		attr.link_create.kprobe_multi.cnt = OPTS_GET(opts, kprobe_multi.cnt, 0);
		attr.link_create.kprobe_multi.syms = ptr_to_u64(OPTS_GET(opts, kprobe_multi.syms, 0));
		attr.link_create.kprobe_multi.addrs = ptr_to_u64(OPTS_GET(opts, kprobe_multi.addrs, 0));
		attr.link_create.kprobe_multi.cookies = ptr_to_u64(OPTS_GET(opts, kprobe_multi.cookies, 0));
		if (!OPTS_ZEROED(opts, kprobe_multi))
			return libbpf_err(-EINVAL);
		break;
	case BPF_TRACE_FENTRY:
	case BPF_TRACE_FEXIT:
	case BPF_MODIFY_RETURN:
	case BPF_LSM_MAC:
		attr.link_create.tracing.cookie = OPTS_GET(opts, tracing.cookie, 0);
		if (!OPTS_ZEROED(opts, tracing))
			return libbpf_err(-EINVAL);
		break;
	case BPF_NETFILTER:
		attr.link_create.netfilter.pf = OPTS_GET(opts, netfilter.pf, 0);
		attr.link_create.netfilter.hooknum = OPTS_GET(opts, netfilter.hooknum, 0);
		attr.link_create.netfilter.priority = OPTS_GET(opts, netfilter.priority, 0);
		attr.link_create.netfilter.flags = OPTS_GET(opts, netfilter.flags, 0);
		if (!OPTS_ZEROED(opts, netfilter))
			return libbpf_err(-EINVAL);
		break;
	default:
		if (!OPTS_ZEROED(opts, flags))
			return libbpf_err(-EINVAL);
		break;
	}
proceed:
	fd = sys_bpf_fd(BPF_LINK_CREATE, &attr, attr_sz);
	if (fd >= 0)
		return fd;
	/* we'll get EINVAL if LINK_CREATE doesn't support attaching fentry
	 * and other similar programs
	 */
	err = -errno;
	if (err != -EINVAL)
		return libbpf_err(err);

	/* if user used features not supported by
	 * BPF_RAW_TRACEPOINT_OPEN command, then just give up immediately
	 */
	if (attr.link_create.target_fd || attr.link_create.target_btf_id)
		return libbpf_err(err);
	if (!OPTS_ZEROED(opts, sz))
		return libbpf_err(err);

	/* otherwise, for few select kinds of programs that can be
	 * attached using BPF_RAW_TRACEPOINT_OPEN command, try that as
	 * a fallback for older kernels
	 */
	switch (attach_type) {
	case BPF_TRACE_RAW_TP:
	case BPF_LSM_MAC:
	case BPF_TRACE_FENTRY:
	case BPF_TRACE_FEXIT:
	case BPF_MODIFY_RETURN:
		return bpf_raw_tracepoint_open(NULL, prog_fd);
	default:
		return libbpf_err(err);
	}
}

int bpf_link_detach(int link_fd)
{
	const size_t attr_sz = offsetofend(union bpf_attr, link_detach);
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, attr_sz);
	attr.link_detach.link_fd = link_fd;

	ret = sys_bpf(BPF_LINK_DETACH, &attr, attr_sz);
	return libbpf_err_errno(ret);
}

int bpf_link_update(int link_fd, int new_prog_fd,
		    const struct bpf_link_update_opts *opts)
{
	const size_t attr_sz = offsetofend(union bpf_attr, link_update);
	union bpf_attr attr;
	int ret;

	if (!OPTS_VALID(opts, bpf_link_update_opts))
		return libbpf_err(-EINVAL);

	if (OPTS_GET(opts, old_prog_fd, 0) && OPTS_GET(opts, old_map_fd, 0))
		return libbpf_err(-EINVAL);

	memset(&attr, 0, attr_sz);
	attr.link_update.link_fd = link_fd;
	attr.link_update.new_prog_fd = new_prog_fd;
	attr.link_update.flags = OPTS_GET(opts, flags, 0);
	if (OPTS_GET(opts, old_prog_fd, 0))
		attr.link_update.old_prog_fd = OPTS_GET(opts, old_prog_fd, 0);
	else if (OPTS_GET(opts, old_map_fd, 0))
		attr.link_update.old_map_fd = OPTS_GET(opts, old_map_fd, 0);

	ret = sys_bpf(BPF_LINK_UPDATE, &attr, attr_sz);
	return libbpf_err_errno(ret);
}

int bpf_iter_create(int link_fd)
{
	const size_t attr_sz = offsetofend(union bpf_attr, iter_create);
	union bpf_attr attr;
	int fd;

	memset(&attr, 0, attr_sz);
	attr.iter_create.link_fd = link_fd;

	fd = sys_bpf_fd(BPF_ITER_CREATE, &attr, attr_sz);
	return libbpf_err_errno(fd);
}

int bpf_prog_query_opts(int target_fd,
			enum bpf_attach_type type,
			struct bpf_prog_query_opts *opts)
{
	const size_t attr_sz = offsetofend(union bpf_attr, query);
	union bpf_attr attr;
	int ret;

	if (!OPTS_VALID(opts, bpf_prog_query_opts))
		return libbpf_err(-EINVAL);

	memset(&attr, 0, attr_sz);

	attr.query.target_fd	= target_fd;
	attr.query.attach_type	= type;
	attr.query.query_flags	= OPTS_GET(opts, query_flags, 0);
	attr.query.prog_cnt	= OPTS_GET(opts, prog_cnt, 0);
	attr.query.prog_ids	= ptr_to_u64(OPTS_GET(opts, prog_ids, NULL));
	attr.query.prog_attach_flags = ptr_to_u64(OPTS_GET(opts, prog_attach_flags, NULL));

	ret = sys_bpf(BPF_PROG_QUERY, &attr, attr_sz);

	OPTS_SET(opts, attach_flags, attr.query.attach_flags);
	OPTS_SET(opts, prog_cnt, attr.query.prog_cnt);

	return libbpf_err_errno(ret);
}

int bpf_prog_query(int target_fd, enum bpf_attach_type type, __u32 query_flags,
		   __u32 *attach_flags, __u32 *prog_ids, __u32 *prog_cnt)
{
	LIBBPF_OPTS(bpf_prog_query_opts, opts);
	int ret;

	opts.query_flags = query_flags;
	opts.prog_ids = prog_ids;
	opts.prog_cnt = *prog_cnt;

	ret = bpf_prog_query_opts(target_fd, type, &opts);

	if (attach_flags)
		*attach_flags = opts.attach_flags;
	*prog_cnt = opts.prog_cnt;

	return libbpf_err_errno(ret);
}

int bpf_prog_test_run_opts(int prog_fd, struct bpf_test_run_opts *opts)
{
	const size_t attr_sz = offsetofend(union bpf_attr, test);
	union bpf_attr attr;
	int ret;

	if (!OPTS_VALID(opts, bpf_test_run_opts))
		return libbpf_err(-EINVAL);

	memset(&attr, 0, attr_sz);
	attr.test.prog_fd = prog_fd;
	attr.test.batch_size = OPTS_GET(opts, batch_size, 0);
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

	ret = sys_bpf(BPF_PROG_TEST_RUN, &attr, attr_sz);

	OPTS_SET(opts, data_size_out, attr.test.data_size_out);
	OPTS_SET(opts, ctx_size_out, attr.test.ctx_size_out);
	OPTS_SET(opts, duration, attr.test.duration);
	OPTS_SET(opts, retval, attr.test.retval);

	return libbpf_err_errno(ret);
}

static int bpf_obj_get_next_id(__u32 start_id, __u32 *next_id, int cmd)
{
	const size_t attr_sz = offsetofend(union bpf_attr, open_flags);
	union bpf_attr attr;
	int err;

	memset(&attr, 0, attr_sz);
	attr.start_id = start_id;

	err = sys_bpf(cmd, &attr, attr_sz);
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

int bpf_prog_get_fd_by_id_opts(__u32 id,
			       const struct bpf_get_fd_by_id_opts *opts)
{
	const size_t attr_sz = offsetofend(union bpf_attr, open_flags);
	union bpf_attr attr;
	int fd;

	if (!OPTS_VALID(opts, bpf_get_fd_by_id_opts))
		return libbpf_err(-EINVAL);

	memset(&attr, 0, attr_sz);
	attr.prog_id = id;
	attr.open_flags = OPTS_GET(opts, open_flags, 0);

	fd = sys_bpf_fd(BPF_PROG_GET_FD_BY_ID, &attr, attr_sz);
	return libbpf_err_errno(fd);
}

int bpf_prog_get_fd_by_id(__u32 id)
{
	return bpf_prog_get_fd_by_id_opts(id, NULL);
}

int bpf_map_get_fd_by_id_opts(__u32 id,
			      const struct bpf_get_fd_by_id_opts *opts)
{
	const size_t attr_sz = offsetofend(union bpf_attr, open_flags);
	union bpf_attr attr;
	int fd;

	if (!OPTS_VALID(opts, bpf_get_fd_by_id_opts))
		return libbpf_err(-EINVAL);

	memset(&attr, 0, attr_sz);
	attr.map_id = id;
	attr.open_flags = OPTS_GET(opts, open_flags, 0);

	fd = sys_bpf_fd(BPF_MAP_GET_FD_BY_ID, &attr, attr_sz);
	return libbpf_err_errno(fd);
}

int bpf_map_get_fd_by_id(__u32 id)
{
	return bpf_map_get_fd_by_id_opts(id, NULL);
}

int bpf_btf_get_fd_by_id_opts(__u32 id,
			      const struct bpf_get_fd_by_id_opts *opts)
{
	const size_t attr_sz = offsetofend(union bpf_attr, open_flags);
	union bpf_attr attr;
	int fd;

	if (!OPTS_VALID(opts, bpf_get_fd_by_id_opts))
		return libbpf_err(-EINVAL);

	memset(&attr, 0, attr_sz);
	attr.btf_id = id;
	attr.open_flags = OPTS_GET(opts, open_flags, 0);

	fd = sys_bpf_fd(BPF_BTF_GET_FD_BY_ID, &attr, attr_sz);
	return libbpf_err_errno(fd);
}

int bpf_btf_get_fd_by_id(__u32 id)
{
	return bpf_btf_get_fd_by_id_opts(id, NULL);
}

int bpf_link_get_fd_by_id_opts(__u32 id,
			       const struct bpf_get_fd_by_id_opts *opts)
{
	const size_t attr_sz = offsetofend(union bpf_attr, open_flags);
	union bpf_attr attr;
	int fd;

	if (!OPTS_VALID(opts, bpf_get_fd_by_id_opts))
		return libbpf_err(-EINVAL);

	memset(&attr, 0, attr_sz);
	attr.link_id = id;
	attr.open_flags = OPTS_GET(opts, open_flags, 0);

	fd = sys_bpf_fd(BPF_LINK_GET_FD_BY_ID, &attr, attr_sz);
	return libbpf_err_errno(fd);
}

int bpf_link_get_fd_by_id(__u32 id)
{
	return bpf_link_get_fd_by_id_opts(id, NULL);
}

int bpf_obj_get_info_by_fd(int bpf_fd, void *info, __u32 *info_len)
{
	const size_t attr_sz = offsetofend(union bpf_attr, info);
	union bpf_attr attr;
	int err;

	memset(&attr, 0, attr_sz);
	attr.info.bpf_fd = bpf_fd;
	attr.info.info_len = *info_len;
	attr.info.info = ptr_to_u64(info);

	err = sys_bpf(BPF_OBJ_GET_INFO_BY_FD, &attr, attr_sz);
	if (!err)
		*info_len = attr.info.info_len;
	return libbpf_err_errno(err);
}

int bpf_prog_get_info_by_fd(int prog_fd, struct bpf_prog_info *info, __u32 *info_len)
{
	return bpf_obj_get_info_by_fd(prog_fd, info, info_len);
}

int bpf_map_get_info_by_fd(int map_fd, struct bpf_map_info *info, __u32 *info_len)
{
	return bpf_obj_get_info_by_fd(map_fd, info, info_len);
}

int bpf_btf_get_info_by_fd(int btf_fd, struct bpf_btf_info *info, __u32 *info_len)
{
	return bpf_obj_get_info_by_fd(btf_fd, info, info_len);
}

int bpf_link_get_info_by_fd(int link_fd, struct bpf_link_info *info, __u32 *info_len)
{
	return bpf_obj_get_info_by_fd(link_fd, info, info_len);
}

int bpf_raw_tracepoint_open(const char *name, int prog_fd)
{
	const size_t attr_sz = offsetofend(union bpf_attr, raw_tracepoint);
	union bpf_attr attr;
	int fd;

	memset(&attr, 0, attr_sz);
	attr.raw_tracepoint.name = ptr_to_u64(name);
	attr.raw_tracepoint.prog_fd = prog_fd;

	fd = sys_bpf_fd(BPF_RAW_TRACEPOINT_OPEN, &attr, attr_sz);
	return libbpf_err_errno(fd);
}

int bpf_btf_load(const void *btf_data, size_t btf_size, struct bpf_btf_load_opts *opts)
{
	const size_t attr_sz = offsetofend(union bpf_attr, btf_log_true_size);
	union bpf_attr attr;
	char *log_buf;
	size_t log_size;
	__u32 log_level;
	int fd;

	bump_rlimit_memlock();

	memset(&attr, 0, attr_sz);

	if (!OPTS_VALID(opts, bpf_btf_load_opts))
		return libbpf_err(-EINVAL);

	log_buf = OPTS_GET(opts, log_buf, NULL);
	log_size = OPTS_GET(opts, log_size, 0);
	log_level = OPTS_GET(opts, log_level, 0);

	if (log_size > UINT_MAX)
		return libbpf_err(-EINVAL);
	if (log_size && !log_buf)
		return libbpf_err(-EINVAL);

	attr.btf = ptr_to_u64(btf_data);
	attr.btf_size = btf_size;
	/* log_level == 0 and log_buf != NULL means "try loading without
	 * log_buf, but retry with log_buf and log_level=1 on error", which is
	 * consistent across low-level and high-level BTF and program loading
	 * APIs within libbpf and provides a sensible behavior in practice
	 */
	if (log_level) {
		attr.btf_log_buf = ptr_to_u64(log_buf);
		attr.btf_log_size = (__u32)log_size;
		attr.btf_log_level = log_level;
	}

	fd = sys_bpf_fd(BPF_BTF_LOAD, &attr, attr_sz);
	if (fd < 0 && log_buf && log_level == 0) {
		attr.btf_log_buf = ptr_to_u64(log_buf);
		attr.btf_log_size = (__u32)log_size;
		attr.btf_log_level = 1;
		fd = sys_bpf_fd(BPF_BTF_LOAD, &attr, attr_sz);
	}

	OPTS_SET(opts, log_true_size, attr.btf_log_true_size);
	return libbpf_err_errno(fd);
}

int bpf_task_fd_query(int pid, int fd, __u32 flags, char *buf, __u32 *buf_len,
		      __u32 *prog_id, __u32 *fd_type, __u64 *probe_offset,
		      __u64 *probe_addr)
{
	const size_t attr_sz = offsetofend(union bpf_attr, task_fd_query);
	union bpf_attr attr;
	int err;

	memset(&attr, 0, attr_sz);
	attr.task_fd_query.pid = pid;
	attr.task_fd_query.fd = fd;
	attr.task_fd_query.flags = flags;
	attr.task_fd_query.buf = ptr_to_u64(buf);
	attr.task_fd_query.buf_len = *buf_len;

	err = sys_bpf(BPF_TASK_FD_QUERY, &attr, attr_sz);

	*buf_len = attr.task_fd_query.buf_len;
	*prog_id = attr.task_fd_query.prog_id;
	*fd_type = attr.task_fd_query.fd_type;
	*probe_offset = attr.task_fd_query.probe_offset;
	*probe_addr = attr.task_fd_query.probe_addr;

	return libbpf_err_errno(err);
}

int bpf_enable_stats(enum bpf_stats_type type)
{
	const size_t attr_sz = offsetofend(union bpf_attr, enable_stats);
	union bpf_attr attr;
	int fd;

	memset(&attr, 0, attr_sz);
	attr.enable_stats.type = type;

	fd = sys_bpf_fd(BPF_ENABLE_STATS, &attr, attr_sz);
	return libbpf_err_errno(fd);
}

int bpf_prog_bind_map(int prog_fd, int map_fd,
		      const struct bpf_prog_bind_opts *opts)
{
	const size_t attr_sz = offsetofend(union bpf_attr, prog_bind_map);
	union bpf_attr attr;
	int ret;

	if (!OPTS_VALID(opts, bpf_prog_bind_opts))
		return libbpf_err(-EINVAL);

	memset(&attr, 0, attr_sz);
	attr.prog_bind_map.prog_fd = prog_fd;
	attr.prog_bind_map.map_fd = map_fd;
	attr.prog_bind_map.flags = OPTS_GET(opts, flags, 0);

	ret = sys_bpf(BPF_PROG_BIND_MAP, &attr, attr_sz);
	return libbpf_err_errno(ret);
}
