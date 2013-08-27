/*
 * build-id.c
 *
 * build-id support
 *
 * Copyright (C) 2009, 2010 Red Hat Inc.
 * Copyright (C) 2009, 2010 Arnaldo Carvalho de Melo <acme@redhat.com>
 */
#include "util.h"
#include <stdio.h>
#include "build-id.h"
#include "event.h"
#include "symbol.h"
#include <linux/kernel.h>
#include "debug.h"
#include "session.h"
#include "tool.h"

int build_id__mark_dso_hit(struct perf_tool *tool __maybe_unused,
			   union perf_event *event,
			   struct perf_sample *sample __maybe_unused,
			   struct perf_evsel *evsel __maybe_unused,
			   struct machine *machine)
{
	struct addr_location al;
	u8 cpumode = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;
	struct thread *thread = machine__findnew_thread(machine, event->ip.pid,
							event->ip.pid);

	if (thread == NULL) {
		pr_err("problem processing %d event, skipping it.\n",
			event->header.type);
		return -1;
	}

	thread__find_addr_map(thread, machine, cpumode, MAP__FUNCTION,
			      event->ip.ip, &al);

	if (al.map != NULL)
		al.map->dso->hit = 1;

	return 0;
}

static int perf_event__exit_del_thread(struct perf_tool *tool __maybe_unused,
				       union perf_event *event,
				       struct perf_sample *sample
				       __maybe_unused,
				       struct machine *machine)
{
	struct thread *thread = machine__findnew_thread(machine,
							event->fork.pid,
							event->fork.tid);

	dump_printf("(%d:%d):(%d:%d)\n", event->fork.pid, event->fork.tid,
		    event->fork.ppid, event->fork.ptid);

	if (thread) {
		rb_erase(&thread->rb_node, &machine->threads);
		machine->last_match = NULL;
		thread__delete(thread);
	}

	return 0;
}

struct perf_tool build_id__mark_dso_hit_ops = {
	.sample	= build_id__mark_dso_hit,
	.mmap	= perf_event__process_mmap,
	.fork	= perf_event__process_fork,
	.exit	= perf_event__exit_del_thread,
	.attr		 = perf_event__process_attr,
	.build_id	 = perf_event__process_build_id,
};

int build_id__sprintf(const u8 *build_id, int len, char *bf)
{
	char *bid = bf;
	const u8 *raw = build_id;
	int i;

	for (i = 0; i < len; ++i) {
		sprintf(bid, "%02x", *raw);
		++raw;
		bid += 2;
	}

	return raw - build_id;
}

char *dso__build_id_filename(struct dso *self, char *bf, size_t size)
{
	char build_id_hex[BUILD_ID_SIZE * 2 + 1];

	if (!self->has_build_id)
		return NULL;

	build_id__sprintf(self->build_id, sizeof(self->build_id), build_id_hex);
	if (bf == NULL) {
		if (asprintf(&bf, "%s/.build-id/%.2s/%s", buildid_dir,
			     build_id_hex, build_id_hex + 2) < 0)
			return NULL;
	} else
		snprintf(bf, size, "%s/.build-id/%.2s/%s", buildid_dir,
			 build_id_hex, build_id_hex + 2);
	return bf;
}
