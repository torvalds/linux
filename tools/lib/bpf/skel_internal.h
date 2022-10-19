/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
/* Copyright (c) 2021 Facebook */
#ifndef __SKEL_INTERNAL_H
#define __SKEL_INTERNAL_H

#ifdef __KERNEL__
#include <linux/fdtable.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/bpf.h>
#else
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <stdlib.h>
#include "bpf.h"
#endif

#ifndef __NR_bpf
# if defined(__mips__) && defined(_ABIO32)
#  define __NR_bpf 4355
# elif defined(__mips__) && defined(_ABIN32)
#  define __NR_bpf 6319
# elif defined(__mips__) && defined(_ABI64)
#  define __NR_bpf 5315
# endif
#endif

/* This file is a base header for auto-generated *.lskel.h files.
 * Its contents will change and may become part of auto-generation in the future.
 *
 * The layout of bpf_[map|prog]_desc and bpf_loader_ctx is feature dependent
 * and will change from one version of libbpf to another and features
 * requested during loader program generation.
 */
struct bpf_map_desc {
	/* output of the loader prog */
	int map_fd;
	/* input for the loader prog */
	__u32 max_entries;
	__aligned_u64 initial_value;
};
struct bpf_prog_desc {
	int prog_fd;
};

enum {
	BPF_SKEL_KERNEL = (1ULL << 0),
};

struct bpf_loader_ctx {
	__u32 sz;
	__u32 flags;
	__u32 log_level;
	__u32 log_size;
	__u64 log_buf;
};

struct bpf_load_and_run_opts {
	struct bpf_loader_ctx *ctx;
	const void *data;
	const void *insns;
	__u32 data_sz;
	__u32 insns_sz;
	const char *errstr;
};

long kern_sys_bpf(__u32 cmd, void *attr, __u32 attr_size);

static inline int skel_sys_bpf(enum bpf_cmd cmd, union bpf_attr *attr,
			  unsigned int size)
{
#ifdef __KERNEL__
	return kern_sys_bpf(cmd, attr, size);
#else
	return syscall(__NR_bpf, cmd, attr, size);
#endif
}

#ifdef __KERNEL__
static inline int close(int fd)
{
	return close_fd(fd);
}

static inline void *skel_alloc(size_t size)
{
	struct bpf_loader_ctx *ctx = kzalloc(size, GFP_KERNEL);

	if (!ctx)
		return NULL;
	ctx->flags |= BPF_SKEL_KERNEL;
	return ctx;
}

static inline void skel_free(const void *p)
{
	kfree(p);
}

/* skel->bss/rodata maps are populated the following way:
 *
 * For kernel use:
 * skel_prep_map_data() allocates kernel memory that kernel module can directly access.
 * Generated lskel stores the pointer in skel->rodata and in skel->maps.rodata.initial_value.
 * The loader program will perform probe_read_kernel() from maps.rodata.initial_value.
 * skel_finalize_map_data() sets skel->rodata to point to actual value in a bpf map and
 * does maps.rodata.initial_value = ~0ULL to signal skel_free_map_data() that kvfree
 * is not nessary.
 *
 * For user space:
 * skel_prep_map_data() mmaps anon memory into skel->rodata that can be accessed directly.
 * Generated lskel stores the pointer in skel->rodata and in skel->maps.rodata.initial_value.
 * The loader program will perform copy_from_user() from maps.rodata.initial_value.
 * skel_finalize_map_data() remaps bpf array map value from the kernel memory into
 * skel->rodata address.
 *
 * The "bpftool gen skeleton -L" command generates lskel.h that is suitable for
 * both kernel and user space. The generated loader program does
 * either bpf_probe_read_kernel() or bpf_copy_from_user() from initial_value
 * depending on bpf_loader_ctx->flags.
 */
static inline void skel_free_map_data(void *p, __u64 addr, size_t sz)
{
	if (addr != ~0ULL)
		kvfree(p);
	/* When addr == ~0ULL the 'p' points to
	 * ((struct bpf_array *)map)->value. See skel_finalize_map_data.
	 */
}

static inline void *skel_prep_map_data(const void *val, size_t mmap_sz, size_t val_sz)
{
	void *addr;

	addr = kvmalloc(val_sz, GFP_KERNEL);
	if (!addr)
		return NULL;
	memcpy(addr, val, val_sz);
	return addr;
}

static inline void *skel_finalize_map_data(__u64 *init_val, size_t mmap_sz, int flags, int fd)
{
	struct bpf_map *map;
	void *addr = NULL;

	kvfree((void *) (long) *init_val);
	*init_val = ~0ULL;

	/* At this point bpf_load_and_run() finished without error and
	 * 'fd' is a valid bpf map FD. All sanity checks below should succeed.
	 */
	map = bpf_map_get(fd);
	if (IS_ERR(map))
		return NULL;
	if (map->map_type != BPF_MAP_TYPE_ARRAY)
		goto out;
	addr = ((struct bpf_array *)map)->value;
	/* the addr stays valid, since FD is not closed */
out:
	bpf_map_put(map);
	return addr;
}

#else

static inline void *skel_alloc(size_t size)
{
	return calloc(1, size);
}

static inline void skel_free(void *p)
{
	free(p);
}

static inline void skel_free_map_data(void *p, __u64 addr, size_t sz)
{
	munmap(p, sz);
}

static inline void *skel_prep_map_data(const void *val, size_t mmap_sz, size_t val_sz)
{
	void *addr;

	addr = mmap(NULL, mmap_sz, PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (addr == (void *) -1)
		return NULL;
	memcpy(addr, val, val_sz);
	return addr;
}

static inline void *skel_finalize_map_data(__u64 *init_val, size_t mmap_sz, int flags, int fd)
{
	void *addr;

	addr = mmap((void *) (long) *init_val, mmap_sz, flags, MAP_SHARED | MAP_FIXED, fd, 0);
	if (addr == (void *) -1)
		return NULL;
	return addr;
}
#endif

static inline int skel_closenz(int fd)
{
	if (fd > 0)
		return close(fd);
	return -EINVAL;
}

#ifndef offsetofend
#define offsetofend(TYPE, MEMBER) \
	(offsetof(TYPE, MEMBER)	+ sizeof((((TYPE *)0)->MEMBER)))
#endif

static inline int skel_map_create(enum bpf_map_type map_type,
				  const char *map_name,
				  __u32 key_size,
				  __u32 value_size,
				  __u32 max_entries)
{
	const size_t attr_sz = offsetofend(union bpf_attr, map_extra);
	union bpf_attr attr;

	memset(&attr, 0, attr_sz);

	attr.map_type = map_type;
	strncpy(attr.map_name, map_name, sizeof(attr.map_name));
	attr.key_size = key_size;
	attr.value_size = value_size;
	attr.max_entries = max_entries;

	return skel_sys_bpf(BPF_MAP_CREATE, &attr, attr_sz);
}

static inline int skel_map_update_elem(int fd, const void *key,
				       const void *value, __u64 flags)
{
	const size_t attr_sz = offsetofend(union bpf_attr, flags);
	union bpf_attr attr;

	memset(&attr, 0, attr_sz);
	attr.map_fd = fd;
	attr.key = (long) key;
	attr.value = (long) value;
	attr.flags = flags;

	return skel_sys_bpf(BPF_MAP_UPDATE_ELEM, &attr, attr_sz);
}

static inline int skel_map_delete_elem(int fd, const void *key)
{
	const size_t attr_sz = offsetofend(union bpf_attr, flags);
	union bpf_attr attr;

	memset(&attr, 0, attr_sz);
	attr.map_fd = fd;
	attr.key = (long)key;

	return skel_sys_bpf(BPF_MAP_DELETE_ELEM, &attr, attr_sz);
}

static inline int skel_map_get_fd_by_id(__u32 id)
{
	const size_t attr_sz = offsetofend(union bpf_attr, flags);
	union bpf_attr attr;

	memset(&attr, 0, attr_sz);
	attr.map_id = id;

	return skel_sys_bpf(BPF_MAP_GET_FD_BY_ID, &attr, attr_sz);
}

static inline int skel_raw_tracepoint_open(const char *name, int prog_fd)
{
	const size_t attr_sz = offsetofend(union bpf_attr, raw_tracepoint.prog_fd);
	union bpf_attr attr;

	memset(&attr, 0, attr_sz);
	attr.raw_tracepoint.name = (long) name;
	attr.raw_tracepoint.prog_fd = prog_fd;

	return skel_sys_bpf(BPF_RAW_TRACEPOINT_OPEN, &attr, attr_sz);
}

static inline int skel_link_create(int prog_fd, int target_fd,
				   enum bpf_attach_type attach_type)
{
	const size_t attr_sz = offsetofend(union bpf_attr, link_create.iter_info_len);
	union bpf_attr attr;

	memset(&attr, 0, attr_sz);
	attr.link_create.prog_fd = prog_fd;
	attr.link_create.target_fd = target_fd;
	attr.link_create.attach_type = attach_type;

	return skel_sys_bpf(BPF_LINK_CREATE, &attr, attr_sz);
}

#ifdef __KERNEL__
#define set_err
#else
#define set_err err = -errno
#endif

static inline int bpf_load_and_run(struct bpf_load_and_run_opts *opts)
{
	const size_t prog_load_attr_sz = offsetofend(union bpf_attr, fd_array);
	const size_t test_run_attr_sz = offsetofend(union bpf_attr, test);
	int map_fd = -1, prog_fd = -1, key = 0, err;
	union bpf_attr attr;

	err = map_fd = skel_map_create(BPF_MAP_TYPE_ARRAY, "__loader.map", 4, opts->data_sz, 1);
	if (map_fd < 0) {
		opts->errstr = "failed to create loader map";
		set_err;
		goto out;
	}

	err = skel_map_update_elem(map_fd, &key, opts->data, 0);
	if (err < 0) {
		opts->errstr = "failed to update loader map";
		set_err;
		goto out;
	}

	memset(&attr, 0, prog_load_attr_sz);
	attr.prog_type = BPF_PROG_TYPE_SYSCALL;
	attr.insns = (long) opts->insns;
	attr.insn_cnt = opts->insns_sz / sizeof(struct bpf_insn);
	attr.license = (long) "Dual BSD/GPL";
	memcpy(attr.prog_name, "__loader.prog", sizeof("__loader.prog"));
	attr.fd_array = (long) &map_fd;
	attr.log_level = opts->ctx->log_level;
	attr.log_size = opts->ctx->log_size;
	attr.log_buf = opts->ctx->log_buf;
	attr.prog_flags = BPF_F_SLEEPABLE;
	err = prog_fd = skel_sys_bpf(BPF_PROG_LOAD, &attr, prog_load_attr_sz);
	if (prog_fd < 0) {
		opts->errstr = "failed to load loader prog";
		set_err;
		goto out;
	}

	memset(&attr, 0, test_run_attr_sz);
	attr.test.prog_fd = prog_fd;
	attr.test.ctx_in = (long) opts->ctx;
	attr.test.ctx_size_in = opts->ctx->sz;
	err = skel_sys_bpf(BPF_PROG_RUN, &attr, test_run_attr_sz);
	if (err < 0 || (int)attr.test.retval < 0) {
		opts->errstr = "failed to execute loader prog";
		if (err < 0) {
			set_err;
		} else {
			err = (int)attr.test.retval;
#ifndef __KERNEL__
			errno = -err;
#endif
		}
		goto out;
	}
	err = 0;
out:
	if (map_fd >= 0)
		close(map_fd);
	if (prog_fd >= 0)
		close(prog_fd);
	return err;
}

#endif
