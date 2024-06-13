// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
#ifndef __PERF_BPF_MAP_H
#define __PERF_BPF_MAP_H 1

#include <stdio.h>
#include <linux/compiler.h>
struct bpf_map;

#ifdef HAVE_LIBBPF_SUPPORT

int bpf_map__fprintf(struct bpf_map *map, FILE *fp);

#else

static inline int bpf_map__fprintf(struct bpf_map *map __maybe_unused, FILE *fp __maybe_unused)
{
	return 0;
}

#endif // HAVE_LIBBPF_SUPPORT

#endif // __PERF_BPF_MAP_H
