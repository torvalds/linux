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

#ifdef HAVE_LIBBPF_SUPPORT
struct bpf_object *bpf__prepare_load(const char *filename);

void bpf__clear(void);
#else
static inline struct bpf_object *
bpf__prepare_load(const char *filename __maybe_unused)
{
	pr_debug("ERROR: eBPF object loading is disabled during compiling.\n");
	return ERR_PTR(-ENOTSUP);
}

static inline void bpf__clear(void) { }
#endif
#endif
