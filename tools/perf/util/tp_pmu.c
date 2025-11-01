// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
#include "tp_pmu.h"
#include "pmus.h"
#include <api/fs/fs.h>
#include <api/fs/tracing_path.h>
#include <api/io_dir.h>
#include <linux/kernel.h>
#include <errno.h>
#include <string.h>

int tp_pmu__id(const char *sys, const char *name)
{
	char *tp_dir = get_events_file(sys);
	char path[PATH_MAX];
	int id, err;

	if (!tp_dir)
		return -1;

	scnprintf(path, PATH_MAX, "%s/%s/id", tp_dir, name);
	put_events_file(tp_dir);
	err = filename__read_int(path, &id);
	if (err)
		return err;

	return id;
}


int tp_pmu__for_each_tp_event(const char *sys, void *state, tp_event_callback cb)
{
	char *evt_path;
	struct io_dirent64 *evt_ent;
	struct io_dir evt_dir;
	int ret = 0;

	evt_path = get_events_file(sys);
	if (!evt_path)
		return -errno;

	io_dir__init(&evt_dir, open(evt_path, O_CLOEXEC | O_DIRECTORY | O_RDONLY));
	if (evt_dir.dirfd < 0) {
		ret = -errno;
		put_events_file(evt_path);
		return ret;
	}
	put_events_file(evt_path);

	while (!ret && (evt_ent = io_dir__readdir(&evt_dir))) {
		if (!strcmp(evt_ent->d_name, ".")
		    || !strcmp(evt_ent->d_name, "..")
		    || !strcmp(evt_ent->d_name, "enable")
		    || !strcmp(evt_ent->d_name, "filter"))
			continue;

		ret = cb(state, sys, evt_ent->d_name);
		if (ret)
			break;
	}
	close(evt_dir.dirfd);
	return ret;
}

int tp_pmu__for_each_tp_sys(void *state, tp_sys_callback cb)
{
	struct io_dirent64 *events_ent;
	struct io_dir events_dir;
	int ret = 0;
	char *events_dir_path = get_tracing_file("events");

	if (!events_dir_path)
		return -errno;

	io_dir__init(&events_dir, open(events_dir_path, O_CLOEXEC | O_DIRECTORY | O_RDONLY));
	if (events_dir.dirfd < 0) {
		ret = -errno;
		put_events_file(events_dir_path);
		return ret;
	}
	put_events_file(events_dir_path);

	while (!ret && (events_ent = io_dir__readdir(&events_dir))) {
		if (!strcmp(events_ent->d_name, ".") ||
		    !strcmp(events_ent->d_name, "..") ||
		    !strcmp(events_ent->d_name, "enable") ||
		    !strcmp(events_ent->d_name, "header_event") ||
		    !strcmp(events_ent->d_name, "header_page"))
			continue;

		ret = cb(state, events_ent->d_name);
	}
	close(events_dir.dirfd);
	return ret;
}

bool perf_pmu__is_tracepoint(const struct perf_pmu *pmu)
{
	return pmu->type == PERF_TYPE_TRACEPOINT;
}

struct for_each_event_args {
	void *state;
	pmu_event_callback cb;
	const struct perf_pmu *pmu;
};

static int for_each_event_cb(void *state, const char *sys_name, const char *evt_name)
{
	struct for_each_event_args *args = state;
	char name[2 * FILENAME_MAX + 2];
	/* 16 possible hex digits and 22 other characters and \0. */
	char encoding[16 + 22];
	char *format = NULL;
	size_t format_size;
	struct pmu_event_info info = {
		.pmu = args->pmu,
		.pmu_name = args->pmu->name,
		.event_type_desc = "Tracepoint event",
	};
	char *tp_dir = get_events_file(sys_name);
	char path[PATH_MAX];
	int id, err;

	if (!tp_dir)
		return -1;

	scnprintf(path, sizeof(path), "%s/%s/id", tp_dir, evt_name);
	err = filename__read_int(path, &id);
	if (err == 0) {
		snprintf(encoding, sizeof(encoding), "tracepoint/config=0x%x/", id);
		info.encoding_desc = encoding;
	}

	scnprintf(path, sizeof(path), "%s/%s/format", tp_dir, evt_name);
	put_events_file(tp_dir);
	err = filename__read_str(path, &format, &format_size);
	if (err == 0) {
		info.long_desc = format;
		for (size_t i = 0 ; i < format_size; i++) {
			/* Swap tabs to spaces due to some rendering issues. */
			if (format[i] == '\t')
				format[i] = ' ';
		}
	}
	snprintf(name, sizeof(name), "%s:%s", sys_name, evt_name);
	info.name = name;
	err = args->cb(args->state, &info);
	free(format);
	return err;
}

static int for_each_event_sys_cb(void *state, const char *sys_name)
{
	return tp_pmu__for_each_tp_event(sys_name, state, for_each_event_cb);
}

int tp_pmu__for_each_event(struct perf_pmu *pmu, void *state, pmu_event_callback cb)
{
	struct for_each_event_args args = {
		.state = state,
		.cb = cb,
		.pmu = pmu,
	};

	return tp_pmu__for_each_tp_sys(&args, for_each_event_sys_cb);
}

static int num_events_cb(void *state, const char *sys_name __maybe_unused,
			 const char *evt_name __maybe_unused)
{
	size_t *count = state;

	(*count)++;
	return 0;
}

static int num_events_sys_cb(void *state, const char *sys_name)
{
	return tp_pmu__for_each_tp_event(sys_name, state, num_events_cb);
}

size_t tp_pmu__num_events(struct perf_pmu *pmu __maybe_unused)
{
	size_t count = 0;

	tp_pmu__for_each_tp_sys(&count, num_events_sys_cb);
	return count;
}

bool tp_pmu__have_event(struct perf_pmu *pmu __maybe_unused, const char *name)
{
	char *dup_name, *colon;
	int id;

	colon = strchr(name, ':');
	if (colon == NULL)
		return false;

	dup_name = strdup(name);
	if (!dup_name)
		return false;

	colon = dup_name + (colon - name);
	*colon = '\0';
	id = tp_pmu__id(dup_name, colon + 1);
	free(dup_name);
	return id >= 0;
}
