/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Google LLC
 */

#ifndef TEST_FUSE__BPF__H
#define TEST_FUSE__BPF__H

#define __EXPORTED_HEADERS__
#define __KERNEL__

#ifdef __ANDROID__
#include <stdint.h>
#endif

#include <uapi/linux/types.h>
#include <uapi/linux/bpf.h>
#include <uapi/linux/android_fuse.h>
#include <uapi/linux/fuse.h>
#include <uapi/linux/errno.h>

#define SEC(NAME) __section(NAME)

struct fuse_bpf_map {
	int map_type;
	size_t key_size;
	size_t value_size;
	int max_entries;
};

static void *(*bpf_map_lookup_elem)(struct fuse_bpf_map *map, void *key)
	= (void *) 1;

static void *(*bpf_map_update_elem)(struct fuse_bpf_map *map, void *key,
				    void *value, int flags)
	= (void *) 2;

static long (*bpf_trace_printk)(const char *fmt, __u32 fmt_size, ...)
	= (void *) 6;

static long (*bpf_get_current_pid_tgid)()
	= (void *) 14;

static long (*bpf_get_current_uid_gid)()
	= (void *) 15;

#define bpf_printk(fmt, ...)					\
	({			                                \
		char ____fmt[] = fmt;                           \
		bpf_trace_printk(____fmt, sizeof(____fmt),      \
					##__VA_ARGS__);		\
	})

SEC("dummy") inline int strcmp(const char *a, const char *b)
{
	int i;

	for (i = 0; i < __builtin_strlen(b) + 1; ++i)
		if (a[i] != b[i])
			return -1;

	return 0;
}

#endif
