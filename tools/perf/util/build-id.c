/*
 * build-id.c
 *
 * build-id support
 *
 * Copyright (C) 2009, 2010 Red Hat Inc.
 * Copyright (C) 2009, 2010 Arnaldo Carvalho de Melo <acme@redhat.com>
 */
#include "build-id.h"
#include "event.h"
#include "symbol.h"
#include <linux/kernel.h>

static int build_id__mark_dso_hit(event_t *event, struct perf_session *session)
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
			      event->ip.ip, &al);

	if (al.map != NULL)
		al.map->dso->hit = 1;

	return 0;
}

struct perf_event_ops build_id__mark_dso_hit_ops = {
	.sample	= build_id__mark_dso_hit,
	.mmap	= event__process_mmap,
	.fork	= event__process_task,
};
