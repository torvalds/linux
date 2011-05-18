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

static int build_id__mark_dso_hit(union perf_event *event,
				  struct perf_sample *sample __used,
				  struct perf_evsel *evsel __used,
				  struct perf_session *session)
{
	struct addr_location al;
	u8 cpumode = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;
	struct thread *thread = perf_session__findnew(session, event->ip.pid);

	if (thread == NULL) {
		pr_err("problem processing %d event, skipping it.\n",
			event->header.type);
		return -1;
	}

	thread__find_addr_map(thread, session, cpumode, MAP__FUNCTION,
			      event->ip.pid, event->ip.ip, &al);

	if (al.map != NULL)
		al.map->dso->hit = 1;

	return 0;
}

static int perf_event__exit_del_thread(union perf_event *event,
				       struct perf_sample *sample __used,
				       struct perf_session *session)
{
	struct thread *thread = perf_session__findnew(session, event->fork.tid);

	dump_printf("(%d:%d):(%d:%d)\n", event->fork.pid, event->fork.tid,
		    event->fork.ppid, event->fork.ptid);

	if (thread) {
		rb_erase(&thread->rb_node, &session->threads);
		session->last_match = NULL;
		thread__delete(thread);
	}

	return 0;
}

struct perf_event_ops build_id__mark_dso_hit_ops = {
	.sample	= build_id__mark_dso_hit,
	.mmap	= perf_event__process_mmap,
	.fork	= perf_event__process_task,
	.exit	= perf_event__exit_del_thread,
};

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
