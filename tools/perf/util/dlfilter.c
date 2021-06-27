// SPDX-License-Identifier: GPL-2.0
/*
 * dlfilter.c: Interface to perf script --dlfilter shared object
 * Copyright (c) 2021, Intel Corporation.
 */
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <subcmd/exec-cmd.h>
#include <linux/zalloc.h>
#include <linux/build_bug.h>

#include "debug.h"
#include "event.h"
#include "evsel.h"
#include "dso.h"
#include "map.h"
#include "thread.h"
#include "trace-event.h"
#include "symbol.h"
#include "srcline.h"
#include "dlfilter.h"
#include "perf_dlfilter.h"

static void al_to_d_al(struct addr_location *al, struct perf_dlfilter_al *d_al)
{
	struct symbol *sym = al->sym;

	d_al->size = sizeof(*d_al);
	if (al->map) {
		struct dso *dso = al->map->dso;

		if (symbol_conf.show_kernel_path && dso->long_name)
			d_al->dso = dso->long_name;
		else
			d_al->dso = dso->name;
		d_al->is_64_bit = dso->is_64_bit;
		d_al->buildid_size = dso->bid.size;
		d_al->buildid = dso->bid.data;
	} else {
		d_al->dso = NULL;
		d_al->is_64_bit = 0;
		d_al->buildid_size = 0;
		d_al->buildid = NULL;
	}
	if (sym) {
		d_al->sym = sym->name;
		d_al->sym_start = sym->start;
		d_al->sym_end = sym->end;
		if (al->addr < sym->end)
			d_al->symoff = al->addr - sym->start;
		else
			d_al->symoff = al->addr - al->map->start - sym->start;
		d_al->sym_binding = sym->binding;
	} else {
		d_al->sym = NULL;
		d_al->sym_start = 0;
		d_al->sym_end = 0;
		d_al->symoff = 0;
		d_al->sym_binding = 0;
	}
	d_al->addr = al->addr;
	d_al->comm = NULL;
	d_al->filtered = 0;
}

static struct addr_location *get_al(struct dlfilter *d)
{
	struct addr_location *al = d->al;

	if (!al->thread && machine__resolve(d->machine, al, d->sample) < 0)
		return NULL;
	return al;
}

static struct thread *get_thread(struct dlfilter *d)
{
	struct addr_location *al = get_al(d);

	return al ? al->thread : NULL;
}

static const struct perf_dlfilter_al *dlfilter__resolve_ip(void *ctx)
{
	struct dlfilter *d = (struct dlfilter *)ctx;
	struct perf_dlfilter_al *d_al = d->d_ip_al;
	struct addr_location *al;

	if (!d->ctx_valid)
		return NULL;

	/* 'size' is also used to indicate already initialized */
	if (d_al->size)
		return d_al;

	al = get_al(d);
	if (!al)
		return NULL;

	al_to_d_al(al, d_al);

	d_al->is_kernel_ip = machine__kernel_ip(d->machine, d->sample->ip);
	d_al->comm = al->thread ? thread__comm_str(al->thread) : ":-1";
	d_al->filtered = al->filtered;

	return d_al;
}

static const struct perf_dlfilter_al *dlfilter__resolve_addr(void *ctx)
{
	struct dlfilter *d = (struct dlfilter *)ctx;
	struct perf_dlfilter_al *d_addr_al = d->d_addr_al;
	struct addr_location *addr_al = d->addr_al;

	if (!d->ctx_valid || !d->d_sample->addr_correlates_sym)
		return NULL;

	/* 'size' is also used to indicate already initialized */
	if (d_addr_al->size)
		return d_addr_al;

	if (!addr_al->thread) {
		struct thread *thread = get_thread(d);

		if (!thread)
			return NULL;
		thread__resolve(thread, addr_al, d->sample);
	}

	al_to_d_al(addr_al, d_addr_al);

	d_addr_al->is_kernel_ip = machine__kernel_ip(d->machine, d->sample->addr);

	return d_addr_al;
}

static char **dlfilter__args(void *ctx, int *dlargc)
{
	struct dlfilter *d = (struct dlfilter *)ctx;

	if (dlargc)
		*dlargc = 0;
	else
		return NULL;

	if (!d->ctx_valid && !d->in_start && !d->in_stop)
		return NULL;

	*dlargc = d->dlargc;
	return d->dlargv;
}

static __s32 dlfilter__resolve_address(void *ctx, __u64 address, struct perf_dlfilter_al *d_al_p)
{
	struct dlfilter *d = (struct dlfilter *)ctx;
	struct perf_dlfilter_al d_al;
	struct addr_location al;
	struct thread *thread;
	__u32 sz;

	if (!d->ctx_valid || !d_al_p)
		return -1;

	thread = get_thread(d);
	if (!thread)
		return -1;

	thread__find_symbol_fb(thread, d->sample->cpumode, address, &al);

	al_to_d_al(&al, &d_al);

	d_al.is_kernel_ip = machine__kernel_ip(d->machine, address);

	sz = d_al_p->size;
	memcpy(d_al_p, &d_al, min((size_t)sz, sizeof(d_al)));
	d_al_p->size = sz;

	return 0;
}

static const __u8 *dlfilter__insn(void *ctx, __u32 *len)
{
	struct dlfilter *d = (struct dlfilter *)ctx;

	if (!len)
		return NULL;

	*len = 0;

	if (!d->ctx_valid)
		return NULL;

	if (d->sample->ip && !d->sample->insn_len) {
		struct addr_location *al = d->al;

		if (!al->thread && machine__resolve(d->machine, al, d->sample) < 0)
			return NULL;

		if (al->thread->maps && al->thread->maps->machine)
			script_fetch_insn(d->sample, al->thread, al->thread->maps->machine);
	}

	if (!d->sample->insn_len)
		return NULL;

	*len = d->sample->insn_len;

	return (__u8 *)d->sample->insn;
}

static const char *dlfilter__srcline(void *ctx, __u32 *line_no)
{
	struct dlfilter *d = (struct dlfilter *)ctx;
	struct addr_location *al;
	unsigned int line = 0;
	char *srcfile = NULL;
	struct map *map;
	u64 addr;

	if (!d->ctx_valid || !line_no)
		return NULL;

	al = get_al(d);
	if (!al)
		return NULL;

	map = al->map;
	addr = al->addr;

	if (map && map->dso)
		srcfile = get_srcline_split(map->dso, map__rip_2objdump(map, addr), &line);

	*line_no = line;
	return srcfile;
}

static struct perf_event_attr *dlfilter__attr(void *ctx)
{
	struct dlfilter *d = (struct dlfilter *)ctx;

	if (!d->ctx_valid)
		return NULL;

	return &d->evsel->core.attr;
}

static __s32 dlfilter__object_code(void *ctx, __u64 ip, void *buf, __u32 len)
{
	struct dlfilter *d = (struct dlfilter *)ctx;
	struct addr_location *al;
	struct addr_location a;
	struct map *map;
	u64 offset;

	if (!d->ctx_valid)
		return -1;

	al = get_al(d);
	if (!al)
		return -1;

	map = al->map;

	if (map && ip >= map->start && ip < map->end &&
	    machine__kernel_ip(d->machine, ip) == machine__kernel_ip(d->machine, d->sample->ip))
		goto have_map;

	thread__find_map_fb(al->thread, d->sample->cpumode, ip, &a);
	if (!a.map)
		return -1;

	map = a.map;
have_map:
	offset = map->map_ip(map, ip);
	if (ip + len >= map->end)
		len = map->end - ip;
	return dso__data_read_offset(map->dso, d->machine, offset, buf, len);
}

static const struct perf_dlfilter_fns perf_dlfilter_fns = {
	.resolve_ip      = dlfilter__resolve_ip,
	.resolve_addr    = dlfilter__resolve_addr,
	.args            = dlfilter__args,
	.resolve_address = dlfilter__resolve_address,
	.insn            = dlfilter__insn,
	.srcline         = dlfilter__srcline,
	.attr            = dlfilter__attr,
	.object_code     = dlfilter__object_code,
};

static char *find_dlfilter(const char *file)
{
	char path[PATH_MAX];
	char *exec_path;

	if (strchr(file, '/'))
		goto out;

	if (!access(file, R_OK)) {
		/*
		 * Prepend "./" so that dlopen will find the file in the
		 * current directory.
		 */
		snprintf(path, sizeof(path), "./%s", file);
		file = path;
		goto out;
	}

	exec_path = get_argv_exec_path();
	if (!exec_path)
		goto out;
	snprintf(path, sizeof(path), "%s/dlfilters/%s", exec_path, file);
	free(exec_path);
	if (!access(path, R_OK))
		file = path;
out:
	return strdup(file);
}

#define CHECK_FLAG(x) BUILD_BUG_ON((u64)PERF_DLFILTER_FLAG_ ## x != (u64)PERF_IP_FLAG_ ## x)

static int dlfilter__init(struct dlfilter *d, const char *file, int dlargc, char **dlargv)
{
	CHECK_FLAG(BRANCH);
	CHECK_FLAG(CALL);
	CHECK_FLAG(RETURN);
	CHECK_FLAG(CONDITIONAL);
	CHECK_FLAG(SYSCALLRET);
	CHECK_FLAG(ASYNC);
	CHECK_FLAG(INTERRUPT);
	CHECK_FLAG(TX_ABORT);
	CHECK_FLAG(TRACE_BEGIN);
	CHECK_FLAG(TRACE_END);
	CHECK_FLAG(IN_TX);
	CHECK_FLAG(VMENTRY);
	CHECK_FLAG(VMEXIT);

	memset(d, 0, sizeof(*d));
	d->file = find_dlfilter(file);
	if (!d->file)
		return -1;
	d->dlargc = dlargc;
	d->dlargv = dlargv;
	return 0;
}

static void dlfilter__exit(struct dlfilter *d)
{
	zfree(&d->file);
}

static int dlfilter__open(struct dlfilter *d)
{
	d->handle = dlopen(d->file, RTLD_NOW);
	if (!d->handle) {
		pr_err("dlopen failed for: '%s'\n", d->file);
		return -1;
	}
	d->start = dlsym(d->handle, "start");
	d->filter_event = dlsym(d->handle, "filter_event");
	d->filter_event_early = dlsym(d->handle, "filter_event_early");
	d->stop = dlsym(d->handle, "stop");
	d->fns = dlsym(d->handle, "perf_dlfilter_fns");
	if (d->fns)
		memcpy(d->fns, &perf_dlfilter_fns, sizeof(struct perf_dlfilter_fns));
	return 0;
}

static int dlfilter__close(struct dlfilter *d)
{
	return dlclose(d->handle);
}

struct dlfilter *dlfilter__new(const char *file, int dlargc, char **dlargv)
{
	struct dlfilter *d = malloc(sizeof(*d));

	if (!d)
		return NULL;

	if (dlfilter__init(d, file, dlargc, dlargv))
		goto err_free;

	if (dlfilter__open(d))
		goto err_exit;

	return d;

err_exit:
	dlfilter__exit(d);
err_free:
	free(d);
	return NULL;
}

static void dlfilter__free(struct dlfilter *d)
{
	if (d) {
		dlfilter__exit(d);
		free(d);
	}
}

int dlfilter__start(struct dlfilter *d, struct perf_session *session)
{
	if (d) {
		d->session = session;
		if (d->start) {
			int ret;

			d->in_start = true;
			ret = d->start(&d->data, d);
			d->in_start = false;
			return ret;
		}
	}
	return 0;
}

static int dlfilter__stop(struct dlfilter *d)
{
	if (d && d->stop) {
		int ret;

		d->in_stop = true;
		ret = d->stop(d->data, d);
		d->in_stop = false;
		return ret;
	}
	return 0;
}

void dlfilter__cleanup(struct dlfilter *d)
{
	if (d) {
		dlfilter__stop(d);
		dlfilter__close(d);
		dlfilter__free(d);
	}
}

#define ASSIGN(x) d_sample.x = sample->x

int dlfilter__do_filter_event(struct dlfilter *d,
			      union perf_event *event,
			      struct perf_sample *sample,
			      struct evsel *evsel,
			      struct machine *machine,
			      struct addr_location *al,
			      struct addr_location *addr_al,
			      bool early)
{
	struct perf_dlfilter_sample d_sample;
	struct perf_dlfilter_al d_ip_al;
	struct perf_dlfilter_al d_addr_al;
	int ret;

	d->event       = event;
	d->sample      = sample;
	d->evsel       = evsel;
	d->machine     = machine;
	d->al          = al;
	d->addr_al     = addr_al;
	d->d_sample    = &d_sample;
	d->d_ip_al     = &d_ip_al;
	d->d_addr_al   = &d_addr_al;

	d_sample.size  = sizeof(d_sample);
	d_ip_al.size   = 0; /* To indicate d_ip_al is not initialized */
	d_addr_al.size = 0; /* To indicate d_addr_al is not initialized */

	ASSIGN(ip);
	ASSIGN(pid);
	ASSIGN(tid);
	ASSIGN(time);
	ASSIGN(addr);
	ASSIGN(id);
	ASSIGN(stream_id);
	ASSIGN(period);
	ASSIGN(weight);
	ASSIGN(ins_lat);
	ASSIGN(p_stage_cyc);
	ASSIGN(transaction);
	ASSIGN(insn_cnt);
	ASSIGN(cyc_cnt);
	ASSIGN(cpu);
	ASSIGN(flags);
	ASSIGN(data_src);
	ASSIGN(phys_addr);
	ASSIGN(data_page_size);
	ASSIGN(code_page_size);
	ASSIGN(cgroup);
	ASSIGN(cpumode);
	ASSIGN(misc);
	ASSIGN(raw_size);
	ASSIGN(raw_data);

	if (sample->branch_stack) {
		d_sample.brstack_nr = sample->branch_stack->nr;
		d_sample.brstack = (struct perf_branch_entry *)perf_sample__branch_entries(sample);
	} else {
		d_sample.brstack_nr = 0;
		d_sample.brstack = NULL;
	}

	if (sample->callchain) {
		d_sample.raw_callchain_nr = sample->callchain->nr;
		d_sample.raw_callchain = (__u64 *)sample->callchain->ips;
	} else {
		d_sample.raw_callchain_nr = 0;
		d_sample.raw_callchain = NULL;
	}

	d_sample.addr_correlates_sym =
		(evsel->core.attr.sample_type & PERF_SAMPLE_ADDR) &&
		sample_addr_correlates_sym(&evsel->core.attr);

	d_sample.event = evsel__name(evsel);

	d->ctx_valid = true;

	if (early)
		ret = d->filter_event_early(d->data, &d_sample, d);
	else
		ret = d->filter_event(d->data, &d_sample, d);

	d->ctx_valid = false;

	return ret;
}

static bool get_filter_desc(const char *dirname, const char *name,
			    char **desc, char **long_desc)
{
	char path[PATH_MAX];
	void *handle;
	const char *(*desc_fn)(const char **long_description);

	snprintf(path, sizeof(path), "%s/%s", dirname, name);
	handle = dlopen(path, RTLD_NOW);
	if (!handle || !(dlsym(handle, "filter_event") || dlsym(handle, "filter_event_early")))
		return false;
	desc_fn = dlsym(handle, "filter_description");
	if (desc_fn) {
		const char *dsc;
		const char *long_dsc;

		dsc = desc_fn(&long_dsc);
		if (dsc)
			*desc = strdup(dsc);
		if (long_dsc)
			*long_desc = strdup(long_dsc);
	}
	dlclose(handle);
	return true;
}

static void list_filters(const char *dirname)
{
	struct dirent *entry;
	DIR *dir;

	dir = opendir(dirname);
	if (!dir)
		return;

	while ((entry = readdir(dir)) != NULL)
	{
		size_t n = strlen(entry->d_name);
		char *long_desc = NULL;
		char *desc = NULL;

		if (entry->d_type == DT_DIR || n < 4 ||
		    strcmp(".so", entry->d_name + n - 3))
			continue;
		if (!get_filter_desc(dirname, entry->d_name, &desc, &long_desc))
			continue;
		printf("  %-36s %s\n", entry->d_name, desc ? desc : "");
		if (verbose) {
			char *p = long_desc;
			char *line;

			while ((line = strsep(&p, "\n")) != NULL)
				printf("%39s%s\n", "", line);
		}
		free(long_desc);
		free(desc);
	}

	closedir(dir);
}

int list_available_dlfilters(const struct option *opt __maybe_unused,
			     const char *s __maybe_unused,
			     int unset __maybe_unused)
{
	char path[PATH_MAX];
	char *exec_path;

	printf("List of available dlfilters:\n");

	list_filters(".");

	exec_path = get_argv_exec_path();
	if (!exec_path)
		goto out;
	snprintf(path, sizeof(path), "%s/dlfilters", exec_path);

	list_filters(path);

	free(exec_path);
out:
	exit(0);
}
