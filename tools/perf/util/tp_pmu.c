// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
#include "tp_pmu.h"
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
		if (ret)
			break;
	}
	close(events_dir.dirfd);
	return ret;
}
