// SPDX-License-Identifier: GPL-2.0
#include "tracepoint.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <unistd.h>

#include <api/fs/tracing_path.h>
#include "fncache.h"

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
bool is_valid_tracepoint(const char *event_string)
{
	char *dst, *path = malloc(strlen(event_string) + 4); /* Space for "/id\0". */
	bool have_file = false; /* Conservatively return false if memory allocation failed. */
	const char *src;

	if (!path)
		return false;

	/* Copy event_string replacing the ':' with '/'. */
	for (src = event_string, dst = path; *src; src++, dst++)
		*dst = (*src == ':') ? '/' : *src;
	/* Add "/id\0". */
	memcpy(dst, "/id", 4);

	dst = get_events_file(path);
	if (dst)
		have_file = file_available(dst);
	free(dst);
	free(path);
	return have_file;
}
