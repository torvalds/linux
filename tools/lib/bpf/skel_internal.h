/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
/* Copyright (c) 2021 Facebook */
#ifndef __SKEL_INTERNAL_H
#define __SKEL_INTERNAL_H

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>

/* This file is a base header for auto-generated *.lskel.h files.
 * Its contents will change and may become part of auto-generation in the future.
 *
 * The layout of bpf_[map|prog]_desc and bpf_loader_ctx is feature dependent
 * and will change from one version of libbpf to another and features
 * requested during loader program generation.
 */
struct bpf_map_desc {
	union {
		/* input for the loader prog */
		struct {
			__aligned_u64 initial_value;
			__u32 max_entries;
		};
		/* output of the loader prog */
		struct {
			int map_fd;
		};
	};
};
struct bpf_prog_desc {
	int prog_fd;
};

struct bpf_loader_ctx {
	size_t sz;
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

static inline int skel_sys_bpf(enum bpf_cmd cmd, union bpf_attr *attr,
			  unsigned int size)
{
	return syscall(__NR_bpf, cmd, attr, size);
}

static inline int skel_closenz(int fd)
{
	if (fd > 0)
		return close(fd);
	return -EINVAL;
}

static inline int bpf_load_and_run(struct bpf_load_and_run_opts *opts)
{
	int map_fd = -1, prog_fd = -1, key = 0, err;
	union bpf_attr attr;

	map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "__loader.map", 4, opts->data_sz, 1, NULL);
	if (map_fd < 0) {
		opts->errstr = "failed to create loader map";
		err = -errno;
		goto out;
	}

	err = bpf_map_update_elem(map_fd, &key, opts->data, 0);
	if (err < 0) {
		opts->errstr = "failed to update loader map";
		err = -errno;
		goto out;
	}

	memset(&attr, 0, sizeof(attr));
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
	prog_fd = skel_sys_bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
	if (prog_fd < 0) {
		opts->errstr = "failed to load loader prog";
		err = -errno;
		goto out;
	}

	memset(&attr, 0, sizeof(attr));
	attr.test.prog_fd = prog_fd;
	attr.test.ctx_in = (long) opts->ctx;
	attr.test.ctx_size_in = opts->ctx->sz;
	err = skel_sys_bpf(BPF_PROG_RUN, &attr, sizeof(attr));
	if (err < 0 || (int)attr.test.retval < 0) {
		opts->errstr = "failed to execute loader prog";
		if (err < 0) {
			err = -errno;
		} else {
			err = (int)attr.test.retval;
			errno = -err;
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
