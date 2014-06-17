/*
 * cgroup_event_listener.c - Simple listener of cgroup events
 *
 * Copyright (C) Kirill A. Shutemov <kirill@shutemov.name>
 */

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/eventfd.h>

#define USAGE_STR "Usage: cgroup_event_listener <path-to-control-file> <args>"

int main(int argc, char **argv)
{
	int efd = -1;
	int cfd = -1;
	int event_control = -1;
	char event_control_path[PATH_MAX];
	char line[LINE_MAX];
	int ret;

	if (argc != 3)
		errx(1, "%s", USAGE_STR);

	cfd = open(argv[1], O_RDONLY);
	if (cfd == -1)
		err(1, "Cannot open %s", argv[1]);

	ret = snprintf(event_control_path, PATH_MAX, "%s/cgroup.event_control",
			dirname(argv[1]));
	if (ret >= PATH_MAX)
		errx(1, "Path to cgroup.event_control is too long");

	event_control = open(event_control_path, O_WRONLY);
	if (event_control == -1)
		err(1, "Cannot open %s", event_control_path);

	efd = eventfd(0, 0);
	if (efd == -1)
		err(1, "eventfd() failed");

	ret = snprintf(line, LINE_MAX, "%d %d %s", efd, cfd, argv[2]);
	if (ret >= LINE_MAX)
		errx(1, "Arguments string is too long");

	ret = write(event_control, line, strlen(line) + 1);
	if (ret == -1)
		err(1, "Cannot write to cgroup.event_control");

	while (1) {
		uint64_t result;

		ret = read(efd, &result, sizeof(result));
		if (ret == -1) {
			if (errno == EINTR)
				continue;
			err(1, "Cannot read from eventfd");
		}
		assert(ret == sizeof(result));

		ret = access(event_control_path, W_OK);
		if ((ret == -1) && (errno == ENOENT)) {
			puts("The cgroup seems to have removed.");
			break;
		}

		if (ret == -1)
			err(1, "cgroup.event_control is not accessible any more");

		printf("%s %s: crossed\n", argv[1], argv[2]);
	}

	return 0;
}
