/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015, Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015, Huawei Inc.
 */
#ifndef __BPF_LOADER_H
#define __BPF_LOADER_H

#include <linux/compiler.h>
#include <linux/err.h>
#include <bpf/libbpf.h>

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
	BPF_LOADER_ERRNO__OBJCONF_OPT,	/* Invalid object config option */
	BPF_LOADER_ERRNO__OBJCONF_CONF,	/* Config value not set (lost '=')) */
	BPF_LOADER_ERRNO__OBJCONF_MAP_OPT,	/* Invalid object map config option */
	BPF_LOADER_ERRNO__OBJCONF_MAP_NOTEXIST,	/* Target map not exist */
	BPF_LOADER_ERRNO__OBJCONF_MAP_VALUE,	/* Incorrect value type for map */
	BPF_LOADER_ERRNO__OBJCONF_MAP_TYPE,	/* Incorrect map type */
	BPF_LOADER_ERRNO__OBJCONF_MAP_KEYSIZE,	/* Incorrect map key size */
	BPF_LOADER_ERRNO__OBJCONF_MAP_VALUESIZE,/* Incorrect map value size */
	BPF_LOADER_ERRNO__OBJCONF_MAP_NOEVT,	/* Event not found for map setting */
	BPF_LOADER_ERRNO__OBJCONF_MAP_MAPSIZE,	/* Invalid map size for event setting */
	BPF_LOADER_ERRNO__OBJCONF_MAP_EVTDIM,	/* Event dimension too large */
	BPF_LOADER_ERRNO__OBJCONF_MAP_EVTINH,	/* Doesn't support inherit event */
	BPF_LOADER_ERRNO__OBJCONF_MAP_EVTTYPE,	/* Wrong event type for map */
	BPF_LOADER_ERRNO__OBJCONF_MAP_IDX2BIG,	/* Index too large */
	__BPF_LOADER_ERRNO__END,
};

struct perf_evsel;
struct perf_evlist;
struct bpf_object;
struct parse_events_term;
#define PERF_BPF_PROBE_GROUP "perf_bpf_probe"

typedef int (*bpf_prog_iter_callback_t)(const char *group, const char *event,
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
int bpf__foreach_event(struct bpf_object *obj,
		       bpf_prog_iter_callback_t func, void *arg);

int bpf__config_obj(struct bpf_object *obj, struct parse_events_term *term,
		    struct perf_evlist *evlist, int *error_pos);
int bpf__strerror_config_obj(struct bpf_object *obj,
			     struct parse_events_term *term,
			     struct perf_evlist *evlist,
			     int *error_pos, int err, char *buf,
			     size_t size);
int bpf__apply_obj_config(void);
int bpf__strerror_apply_obj_config(int err, char *buf, size_t size);

int bpf__setup_stdout(struct perf_evlist *evlist);
struct perf_evsel *bpf__setup_output_event(struct perf_evlist *evlist, const char *name);
int bpf__strerror_setup_output_event(struct perf_evlist *evlist, int err, char *buf, size_t size);
#else
#include <errno.h>
#include <string.h>
#include "debug.h"

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
bpf__foreach_event(struct bpf_object *obj __maybe_unused,
		   bpf_prog_iter_callback_t func __maybe_unused,
		   void *arg __maybe_unused)
{
	return 0;
}

static inline int
bpf__config_obj(struct bpf_object *obj __maybe_unused,
		struct parse_events_term *term __maybe_unused,
		struct perf_evlist *evlist __maybe_unused,
		int *error_pos __maybe_unused)
{
	return 0;
}

static inline int
bpf__apply_obj_config(void)
{
	return 0;
}

static inline int
bpf__setup_stdout(struct perf_evlist *evlist __maybe_unused)
{
	return 0;
}

static inline struct perf_evsel *
bpf__setup_output_event(struct perf_evlist *evlist __maybe_unused, const char *name __maybe_unused)
{
	return NULL;
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

static inline int
bpf__strerror_config_obj(struct bpf_object *obj __maybe_unused,
			 struct parse_events_term *term __maybe_unused,
			 struct perf_evlist *evlist __maybe_unused,
			 int *error_pos __maybe_unused,
			 int err __maybe_unused,
			 char *buf, size_t size)
{
	return __bpf_strerror(buf, size);
}

static inline int
bpf__strerror_apply_obj_config(int err __maybe_unused,
			       char *buf, size_t size)
{
	return __bpf_strerror(buf, size);
}

static inline int
bpf__strerror_setup_output_event(struct perf_evlist *evlist __maybe_unused,
				 int err __maybe_unused, char *buf, size_t size)
{
	return __bpf_strerror(buf, size);
}

#endif

static inline int bpf__strerror_setup_stdout(struct perf_evlist *evlist, int err, char *buf, size_t size)
{
	return bpf__strerror_setup_output_event(evlist, err, buf, size);
}
#endif
