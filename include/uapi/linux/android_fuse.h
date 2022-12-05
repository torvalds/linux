/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause WITH Linux-syscall-note */
/* Copyright (c) 2022 Google LLC */

#ifndef _LINUX_ANDROID_FUSE_H
#define _LINUX_ANDROID_FUSE_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#define FUSE_ACTION_KEEP	0
#define FUSE_ACTION_REMOVE	1
#define FUSE_ACTION_REPLACE	2

struct fuse_entry_bpf_out {
	uint64_t	backing_action;
	uint64_t	backing_fd;
	uint64_t	bpf_action;
	uint64_t	bpf_fd;
};

struct fuse_entry_bpf {
	struct fuse_entry_bpf_out out;
	struct file *backing_file;
	struct file *bpf_file;
};

struct fuse_read_out {
	uint64_t	offset;
	uint32_t	again;
	uint32_t	padding;
};

struct fuse_in_postfilter_header {
	uint32_t	len;
	uint32_t	opcode;
	uint64_t	unique;
	uint64_t	nodeid;
	uint32_t	uid;
	uint32_t	gid;
	uint32_t	pid;
	uint32_t	error_in;
};

/*
 * Fuse BPF Args
 *
 * Used to communicate with bpf programs to allow checking or altering certain values.
 * The end_offset allows the bpf verifier to check boundaries statically. This reflects
 * the ends of the buffer. size shows the length that was actually used.
 *
 */

/** One input argument of a request */
struct fuse_bpf_in_arg {
	uint32_t size;
	const void *value;
	const void *end_offset;
};

/** One output argument of a request */
struct fuse_bpf_arg {
	uint32_t size;
	void *value;
	void *end_offset;
};

#define FUSE_MAX_IN_ARGS 5
#define FUSE_MAX_OUT_ARGS 3

#define FUSE_BPF_FORCE (1 << 0)
#define FUSE_BPF_OUT_ARGVAR (1 << 6)

struct fuse_bpf_args {
	uint64_t nodeid;
	uint32_t opcode;
	uint32_t error_in;
	uint32_t in_numargs;
	uint32_t out_numargs;
	uint32_t flags;
	struct fuse_bpf_in_arg in_args[FUSE_MAX_IN_ARGS];
	struct fuse_bpf_arg out_args[FUSE_MAX_OUT_ARGS];
};

#define FUSE_BPF_USER_FILTER	1
#define FUSE_BPF_BACKING	2
#define FUSE_BPF_POST_FILTER	4

#define FUSE_OPCODE_FILTER	0x0ffff
#define FUSE_PREFILTER		0x10000
#define FUSE_POSTFILTER		0x20000

struct bpf_prog *fuse_get_bpf_prog(struct file *file);

#endif  /* _LINUX_ANDROID_FUSE_H */
