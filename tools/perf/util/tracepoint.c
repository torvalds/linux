// SPDX-License-Identifier: GPL-2.0
#include "tracepoint.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/param.h>
#include <unistd.h>

#include <api/fs/tracing_path.h>

int tp_event_has_id(const char *dir_path, struct dirent *evt_dir)
{
	char evt_path[MAXPATHLEN];
	int fd;

	snprintf(evt_path, MAXPATHLEN, "%s/%s/id", dir_path, evt_dir->d_name);
	fd = open(evt_path, O_RDONLY);
	if (fd < 0)
		return -EINVAL;
	close(fd);

	return 0;
}

/*
 * Check whether event is in <debugfs_mount_point>/tracing/events
 */
int is_valid_tracepoint(const char *event_string)
{
	DIR *sys_dir, *evt_dir;
	struct dirent *sys_dirent, *evt_dirent;
	char evt_path[MAXPATHLEN];
	char *dir_path;

	sys_dir = tracing_events__opendir();
	if (!sys_dir)
		return 0;

	for_each_subsystem(sys_dir, sys_dirent) {
		dir_path = get_events_file(sys_dirent->d_name);
		if (!dir_path)
			continue;
		evt_dir = opendir(dir_path);
		if (!evt_dir)
			goto next;

		for_each_event(dir_path, evt_dir, evt_dirent) {
			snprintf(evt_path, MAXPATHLEN, "%s:%s",
				 sys_dirent->d_name, evt_dirent->d_name);
			if (!strcmp(evt_path, event_string)) {
				closedir(evt_dir);
				put_events_file(dir_path);
				closedir(sys_dir);
				return 1;
			}
		}
		closedir(evt_dir);
next:
		put_events_file(dir_path);
	}
	closedir(sys_dir);
	return 0;
}
