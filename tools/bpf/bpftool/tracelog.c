// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (c) 2015-2017 Daniel Borkmann */
/* Copyright (c) 2018 Netronome Systems, Inc. */

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/magic.h>
#include <sys/fcntl.h>
#include <sys/vfs.h>

#include "main.h"

#ifndef TRACEFS_MAGIC
# define TRACEFS_MAGIC	0x74726163
#endif

#define _textify(x)	#x
#define textify(x)	_textify(x)

FILE *trace_pipe_fd;
char *buff;

static int validate_tracefs_mnt(const char *mnt, unsigned long magic)
{
	struct statfs st_fs;

	if (statfs(mnt, &st_fs) < 0)
		return -ENOENT;
	if ((unsigned long)st_fs.f_type != magic)
		return -ENOENT;

	return 0;
}

static bool
find_tracefs_mnt_single(unsigned long magic, char *mnt, const char *mntpt)
{
	size_t src_len;

	if (validate_tracefs_mnt(mntpt, magic))
		return false;

	src_len = strlen(mntpt);
	if (src_len + 1 >= PATH_MAX) {
		p_err("tracefs mount point name too long");
		return false;
	}

	strcpy(mnt, mntpt);
	return true;
}

static bool get_tracefs_pipe(char *mnt)
{
	static const char * const known_mnts[] = {
		"/sys/kernel/debug/tracing",
		"/sys/kernel/tracing",
		"/tracing",
		"/trace",
	};
	const char *pipe_name = "/trace_pipe";
	const char *fstype = "tracefs";
	char type[100], format[32];
	const char * const *ptr;
	bool found = false;
	FILE *fp;

	for (ptr = known_mnts; ptr < known_mnts + ARRAY_SIZE(known_mnts); ptr++)
		if (find_tracefs_mnt_single(TRACEFS_MAGIC, mnt, *ptr))
			goto exit_found;

	fp = fopen("/proc/mounts", "r");
	if (!fp)
		return false;

	/* Allow room for NULL terminating byte and pipe file name */
	snprintf(format, sizeof(format), "%%*s %%%zds %%99s %%*s %%*d %%*d\\n",
		 PATH_MAX - strlen(pipe_name) - 1);
	while (fscanf(fp, format, mnt, type) == 2)
		if (strcmp(type, fstype) == 0) {
			found = true;
			break;
		}
	fclose(fp);

	/* The string from fscanf() might be truncated, check mnt is valid */
	if (found && validate_tracefs_mnt(mnt, TRACEFS_MAGIC))
		goto exit_found;

	if (block_mount)
		return false;

	p_info("could not find tracefs, attempting to mount it now");
	/* Most of the time, tracefs is automatically mounted by debugfs at
	 * /sys/kernel/debug/tracing when we try to access it. If we could not
	 * find it, it is likely that debugfs is not mounted. Let's give one
	 * attempt at mounting just tracefs at /sys/kernel/tracing.
	 */
	strcpy(mnt, known_mnts[1]);
	if (mount_tracefs(mnt))
		return false;

exit_found:
	strcat(mnt, pipe_name);
	return true;
}

static void exit_tracelog(int signum)
{
	fclose(trace_pipe_fd);
	free(buff);

	if (json_output) {
		jsonw_end_array(json_wtr);
		jsonw_destroy(&json_wtr);
	}

	exit(0);
}

int do_tracelog(int argc, char **argv)
{
	const struct sigaction act = {
		.sa_handler = exit_tracelog
	};
	char trace_pipe[PATH_MAX];
	size_t buff_len = 0;

	if (json_output)
		jsonw_start_array(json_wtr);

	if (!get_tracefs_pipe(trace_pipe))
		return -1;

	trace_pipe_fd = fopen(trace_pipe, "r");
	if (!trace_pipe_fd) {
		p_err("could not open trace pipe: %s", strerror(errno));
		return -1;
	}

	sigaction(SIGHUP, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGTERM, &act, NULL);
	while (1) {
		ssize_t ret;

		ret = getline(&buff, &buff_len, trace_pipe_fd);
		if (ret <= 0) {
			p_err("failed to read content from trace pipe: %s",
			      strerror(errno));
			break;
		}
		if (json_output)
			jsonw_string(json_wtr, buff);
		else
			printf("%s", buff);
	}

	fclose(trace_pipe_fd);
	free(buff);
	return -1;
}
