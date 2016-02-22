/*
 * Copyright (C) 2015, Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015, Huawei Inc.
 */
#ifndef __BPF_LOADER_H
#define __BPF_LOADER_H

#include <linux/compiler.h>
#include <linux/err.h>
#include <string.h>
#include <bpf/libbpf.h>
#include "probe-event.h"
#include "debug.h"

enum bpf_loader_errno {
	__BPF_LOADER_ERRNO__START = __LIBBPF_ERRNO__START - 100,
	/* Invalid config string */
	BPF_LOADER_ERRNO__CONFIG = __BPF_LOADER_ERRNO__START,
	BPF_LOADER_ERRNO__GROUP,	/* Invalid group name */
	BPF_LOADER_ERRNO__EVENTNAME,	/* Event name is missing */
	BPF_LOADER_ERRNO__INTERNAL,	/* BPF loader internal error */
	BPF_LOADER_ERRNO__COMPILE,	/* Error when compiling BPF scriptlet */
	BPF_LOADER_ERRNO__PROGCONF_TERM,/* Invalid program config term in config string */
	BPF_LOADER_ERRNO__PROLOGUE,	/* Failed to generate prologue */
	BPF_LOADER_ERRNO__PROLOGUE2BIG,	/* Prologue too big for program */
	BPF_LOADER_ERRNO__PROLOGUEOOB,	/* Offset out of bound for prologue */
	__BPF_LOADER_ERRNO__END,
};

struct bpf_object;
#define PERF_BPF_PROBE_GROUP "perf_bpf_probe"

typedef int (*bpf_prog_iter_callback_t)(struct probe_trace_event *tev,
					int fd, void *arg);

#ifdef HAVE_LIBBPF_SUPPORT
struct bpf_object *bpf__prepare_load(const char *filename, bool source);
int bpf__strerror_prepare_load(const char *filename, bool source,
			       int err, char *buf, size_t size);

struct bpf_object *bpf__prepare_load_buffer(void *obj_buf, size_t obj_buf_sz,
					    const char *name);

void bpf__clear(void);

int bpf__probe(struct bpf_object *obj);
int bpf__unprobe(struct bpf_object *obj);
int bpf__strerror_probe(struct bpf_object *obj, int err,
			char *buf, size_t size);

int bpf__load(struct bpf_object *obj);
int bpf__strerror_load(struct bpf_object *obj, int err,
		       char *buf, size_t size);
int bpf__foreach_tev(struct bpf_object *obj,
		     bpf_prog_iter_callback_t func, void *arg);
#else
static inline struct bpf_object *
bpf__prepare_load(const char *filename __maybe_unused,
		  bool source __maybe_unused)
{
	pr_debug("ERROR: eBPF object loading is disabled during compiling.\n");
	return ERR_PTR(-ENOTSUP);
}

static inline struct bpf_object *
bpf__prepare_load_buffer(void *obj_buf __maybe_unused,
					   size_t obj_buf_sz __maybe_unused)
{
	return ERR_PTR(-ENOTSUP);
}

static inline void bpf__clear(void) { }

static inline int bpf__probe(struct bpf_object *obj __maybe_unused) { return 0;}
static inline int bpf__unprobe(struct bpf_object *obj __maybe_unused) { return 0;}
static inline int bpf__load(struct bpf_object *obj __maybe_unused) { return 0; }

static inline int
bpf__foreach_tev(struct bpf_object *obj __maybe_unused,
		 bpf_prog_iter_callback_t func __maybe_unused,
		 void *arg __maybe_unused)
{
	return 0;
}

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

static inline
int bpf__strerror_prepare_load(const char *filename __maybe_unused,
			       bool source __maybe_unused,
			       int err __maybe_unused,
			       char *buf, size_t size)
{
	return __bpf_strerror(buf, size);
}

static inline int
bpf__strerror_probe(struct bpf_object *obj __maybe_unused,
		    int err __maybe_unused,
		    char *buf, size_t size)
{
	return __bpf_strerror(buf, size);
}

static inline int bpf__strerror_load(struct bpf_object *obj __maybe_unused,
				     int err __maybe_unused,
				     char *buf, size_t size)
{
	return __bpf_strerror(buf, size);
}
#endif
#endif
