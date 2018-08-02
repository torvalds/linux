// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2018
 * Auxtrace support for s390 CPU-Measurement Sampling Facility
 *
 * Author(s):  Thomas Richter <tmricht@linux.ibm.com>
 */

#include <endian.h>
#include <errno.h>
#include <byteswap.h>
#include <inttypes.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/log2.h>

#include "cpumap.h"
#include "color.h"
#include "evsel.h"
#include "evlist.h"
#include "machine.h"
#include "session.h"
#include "util.h"
#include "thread.h"
#include "debug.h"
#include "auxtrace.h"
#include "s390-cpumsf.h"

struct s390_cpumsf {
	struct auxtrace		auxtrace;
	struct auxtrace_queues	queues;
	struct auxtrace_heap	heap;
	struct perf_session	*session;
	struct machine		*machine;
	u32			auxtrace_type;
	u32			pmu_type;
};

static int
s390_cpumsf_process_event(struct perf_session *session __maybe_unused,
			  union perf_event *event __maybe_unused,
			  struct perf_sample *sample __maybe_unused,
			  struct perf_tool *tool __maybe_unused)
{
	return 0;
}

static int
s390_cpumsf_process_auxtrace_event(struct perf_session *session __maybe_unused,
				   union perf_event *event __maybe_unused,
				   struct perf_tool *tool __maybe_unused)
{
	return 0;
}

static int s390_cpumsf_flush(struct perf_session *session __maybe_unused,
			     struct perf_tool *tool __maybe_unused)
{
	return 0;
}

static void s390_cpumsf_free_events(struct perf_session *session)
{
	struct s390_cpumsf *sf = container_of(session->auxtrace,
					      struct s390_cpumsf,
					       auxtrace);
	struct auxtrace_queues *queues = &sf->queues;
	unsigned int i;

	for (i = 0; i < queues->nr_queues; i++)
		zfree(&queues->queue_array[i].priv);
	auxtrace_queues__free(queues);
}

static void s390_cpumsf_free(struct perf_session *session)
{
	struct s390_cpumsf *sf = container_of(session->auxtrace,
					      struct s390_cpumsf,
					      auxtrace);

	auxtrace_heap__free(&sf->heap);
	s390_cpumsf_free_events(session);
	session->auxtrace = NULL;
	free(sf);
}

int s390_cpumsf_process_auxtrace_info(union perf_event *event,
				      struct perf_session *session)
{
	struct auxtrace_info_event *auxtrace_info = &event->auxtrace_info;
	struct s390_cpumsf *sf;
	int err;

	if (auxtrace_info->header.size < sizeof(struct auxtrace_info_event))
		return -EINVAL;

	sf = zalloc(sizeof(struct s390_cpumsf));
	if (sf == NULL)
		return -ENOMEM;

	err = auxtrace_queues__init(&sf->queues);
	if (err)
		goto err_free;

	sf->session = session;
	sf->machine = &session->machines.host; /* No kvm support */
	sf->auxtrace_type = auxtrace_info->type;
	sf->pmu_type = PERF_TYPE_RAW;

	sf->auxtrace.process_event = s390_cpumsf_process_event;
	sf->auxtrace.process_auxtrace_event = s390_cpumsf_process_auxtrace_event;
	sf->auxtrace.flush_events = s390_cpumsf_flush;
	sf->auxtrace.free_events = s390_cpumsf_free_events;
	sf->auxtrace.free = s390_cpumsf_free;
	session->auxtrace = &sf->auxtrace;

	return 0;

err_free:
	free(sf);
	return err;
}
