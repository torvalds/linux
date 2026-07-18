// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#pragma once

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>

static inline int libarena_run_prog(int prog_fd)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	int ret;

	ret = bpf_prog_test_run_opts(prog_fd, &opts);
	if (ret)
		return ret;

	return opts.retval;
}

static inline bool libarena_is_test_prog(const char *name)
{
	return strstr(name, "test_") == name;
}

static inline bool libarena_is_asan_test_prog(const char *name)
{
	return strstr(name, "asan_test") == name;
}

static inline bool libarena_is_parallel_test_prog(const char *name)
{
	return strstr(name, "parallel_test") == name;
}


static inline int libarena_run_prog_args(int prog_fd, void *args, size_t argsize)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	int ret;

	opts.ctx_in = args;
	opts.ctx_size_in = argsize;

	ret = bpf_prog_test_run_opts(prog_fd, &opts);

	return ret ?: opts.retval;
}

static inline int libarena_get_arena_base(int arena_get_info_fd,
					  void **arena_base)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct arena_get_info_args args = { .arena_base = NULL };
	int ret;

	opts.ctx_in = &args;
	opts.ctx_size_in = sizeof(args);

	ret = bpf_prog_test_run_opts(arena_get_info_fd, &opts);
	if (ret)
		return ret;
	if (opts.retval)
		return opts.retval;

	*arena_base = args.arena_base;
	return 0;
}

static inline int libarena_get_globals_pages(int arena_get_globals_fd,
					     size_t arena_all_pages,
					     u64 *globals_pages)
{
	size_t pgsize = sysconf(_SC_PAGESIZE);
	void *arena_base;
	ssize_t i;
	u8 *vec;
	int ret;

	ret = libarena_get_arena_base(arena_get_globals_fd, &arena_base);
	if (ret)
		return ret;

	if (!arena_base)
		return -EINVAL;

	vec = calloc(arena_all_pages, sizeof(*vec));
	if (!vec)
		return -ENOMEM;

	if (mincore(arena_base, arena_all_pages * pgsize, vec) < 0) {
		ret = -errno;
		free(vec);
		return ret;
	}

	*globals_pages = 0;
	for (i = arena_all_pages - 1; i >= 0; i--) {
		if (!(vec[i] & 0x1))
			break;
		*globals_pages += 1;
	}

	free(vec);
	return 0;
}

static inline int libarena_asan_init(int arena_asan_init_fd,
				     int asan_init_fd,
				     size_t arena_all_pages)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct asan_init_args args;
	u64 globals_pages;
	int ret;

	ret = libarena_get_globals_pages(arena_asan_init_fd,
					 arena_all_pages, &globals_pages);
	if (ret)
		return ret;

	args = (struct asan_init_args){
		.arena_all_pages = arena_all_pages,
		.arena_globals_pages = globals_pages,
	};

	opts.ctx_in = &args;
	opts.ctx_size_in = sizeof(args);

	ret = bpf_prog_test_run_opts(asan_init_fd, &opts);
	if (ret)
		return ret;
	return opts.retval;
}
