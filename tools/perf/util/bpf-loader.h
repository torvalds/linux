/*
 * Copyright (C) 2015, Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015, Huawei Inc.
 */
#ifndef __BPF_LOADER_H
#define __BPF_LOADER_H

#include <linux/compiler.h>
#include <linux/err.h>
#include <string.h>
#include "debug.h"

struct bpf_object;
#define PERF_BPF_PROBE_GROUP "perf_bpf_probe"

#ifdef HAVE_LIBBPF_SUPPORT
struct bpf_object *bpf__prepare_load(const char *filename);

void bpf__clear(void);

int bpf__probe(struct bpf_object *obj);
int bpf__unprobe(struct bpf_object *obj);
int bpf__strerror_probe(struct bpf_object *obj, int err,
			char *buf, size_t size);

#else
static inline struct bpf_object *
bpf__prepare_load(const char *filename __maybe_unused)
{
	pr_debug("ERROR: eBPF object loading is disabled during compiling.\n");
	return ERR_PTR(-ENOTSUP);
}

static inline void bpf__clear(void) { }

static inline int bpf__probe(struct bpf_object *obj __maybe_unused) { return 0;}
static inline int bpf__unprobe(struct bpf_object *obj __maybe_unused) { return 0;}

static inline int
__bpf_strerror(char *buf, size_t size)
{
	if (!size)
		return 0;
	strncpy(buf,
		"ERROR: eBPF object loading is disabled during compiling.\n",
		size);
	buf[size - 1] = '\0';
	return 0;
}

static inline int
bpf__strerror_probe(struct bpf_object *obj __maybe_unused,
		    int err __maybe_unused,
		    char *buf, size_t size)
{
	return __bpf_strerror(buf, size);
}
#endif
#endif
