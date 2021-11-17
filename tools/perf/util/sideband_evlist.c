// SPDX-License-Identifier: GPL-2.0-only

#include "util/debug.h"
#include "util/evlist.h"
#include "util/evsel.h"
#include "util/mmap.h"
#include "util/perf_api_probe.h"
#include <perf/mmap.h>
#include <linux/perf_event.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>

int evlist__add_sb_event(struct evlist *evlist, struct perf_event_attr *attr,
			 evsel__sb_cb_t cb, void *data)
{
	struct evsel *evsel;

	if (!attr->sample_id_all) {
		pr_warning("enabling sample_id_all for all side band events\n");
		attr->sample_id_all = 1;
	}

	evsel = evsel__new_idx(attr, evlist->core.nr_entries);
	if (!evsel)
		return -1;

	evsel->side_band.cb = cb;
	evsel->side_band.data = data;
	evlist__add(evlist, evsel);
	return 0;
}

static void *perf_evlist__poll_thread(void *arg)
{
	struct evlist *evlist = arg;
	bool draining = false;
	int i, done = 0;
	/*
	 * In order to read symbols from other namespaces perf to needs to call
	 * setns(2).  This isn't permitted if the struct_fs has multiple users.
	 * unshare(2) the fs so that we may continue to setns into namespaces
	 * that we're observing when, for instance, reading the build-ids at
	 * the end of a 'perf record' session.
	 */
	unshare(CLONE_FS);

	while (!done) {
		bool got_data = false;

		if (evlist->thread.done)
			draining = true;

		if (!draining)
			evlist__poll(evlist, 1000);

		for (i = 0; i < evlist->core.nr_mmaps; i++) {
			struct mmap *map = &evlist->mmap[i];
			union perf_event *event;

			if (perf_mmap__read_init(&map->core))
				continue;
			while ((event = perf_mmap__read_event(&map->core)) != NULL) {
				struct evsel *evsel = evlist__event2evsel(evlist, event);

				if (evsel && evsel->side_band.cb)
					evsel->side_band.cb(event, evsel->side_band.data);
				else
					pr_warning("cannot locate proper evsel for the side band event\n");

				perf_mmap__consume(&map->core);
				got_data = true;
			}
			perf_mmap__read_done(&map->core);
		}

		if (draining && !got_data)
			break;
	}
	return NULL;
}

void evlist__set_cb(struct evlist *evlist, evsel__sb_cb_t cb, void *data)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		evsel->core.attr.sample_id_all    = 1;
		evsel->core.attr.watermark        = 1;
		evsel->core.attr.wakeup_watermark = 1;
		evsel->side_band.cb   = cb;
		evsel->side_band.data = data;
      }
}

int evlist__start_sb_thread(struct evlist *evlist, struct target *target)
{
	struct evsel *counter;

	if (!evlist)
		return 0;

	if (evlist__create_maps(evlist, target))
		goto out_delete_evlist;

	if (evlist->core.nr_entries > 1) {
		bool can_sample_identifier = perf_can_sample_identifier();

		evlist__for_each_entry(evlist, counter)
			evsel__set_sample_id(counter, can_sample_identifier);

		evlist__set_id_pos(evlist);
	}

	evlist__for_each_entry(evlist, counter) {
		if (evsel__open(counter, evlist->core.cpus, evlist->core.threads) < 0)
			goto out_delete_evlist;
	}

	if (evlist__mmap(evlist, UINT_MAX))
		goto out_delete_evlist;

	evlist__for_each_entry(evlist, counter) {
		if (evsel__enable(counter))
			goto out_delete_evlist;
	}

	evlist->thread.done = 0;
	if (pthread_create(&evlist->thread.th, NULL, perf_evlist__poll_thread, evlist))
		goto out_delete_evlist;

	return 0;

out_delete_evlist:
	evlist__delete(evlist);
	evlist = NULL;
	return -1;
}

void evlist__stop_sb_thread(struct evlist *evlist)
{
	if (!evlist)
		return;
	evlist->thread.done = 1;
	pthread_join(evlist->thread.th, NULL);
	evlist__delete(evlist);
}
