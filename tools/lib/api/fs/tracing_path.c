#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "fs.h"

#include "tracing_path.h"


char tracing_mnt[PATH_MAX]         = "/sys/kernel/debug";
char tracing_path[PATH_MAX]        = "/sys/kernel/debug/tracing";
char tracing_events_path[PATH_MAX] = "/sys/kernel/debug/tracing/events";


static void __tracing_path_set(const char *tracing, const char *mountpoint)
{
	snprintf(tracing_mnt, sizeof(tracing_mnt), "%s", mountpoint);
	snprintf(tracing_path, sizeof(tracing_path), "%s/%s",
		 mountpoint, tracing);
	snprintf(tracing_events_path, sizeof(tracing_events_path), "%s/%s%s",
		 mountpoint, tracing, "events");
}

static const char *tracing_path_tracefs_mount(void)
{
	const char *mnt;

	mnt = tracefs__mount();
	if (!mnt)
		return NULL;

	__tracing_path_set("", mnt);

	return mnt;
}

static const char *tracing_path_debugfs_mount(void)
{
	const char *mnt;

	mnt = debugfs__mount();
	if (!mnt)
		return NULL;

	__tracing_path_set("tracing/", mnt);

	return mnt;
}

const char *tracing_path_mount(void)
{
	const char *mnt;

	mnt = tracing_path_tracefs_mount();
	if (mnt)
		return mnt;

	mnt = tracing_path_debugfs_mount();

	return mnt;
}

void tracing_path_set(const char *mntpt)
{
	__tracing_path_set("tracing/", mntpt);
}

char *get_tracing_file(const char *name)
{
	char *file;

	if (asprintf(&file, "%s/%s", tracing_path, name) < 0)
		return NULL;

	return file;
}

void put_tracing_file(char *file)
{
	free(file);
}

static int strerror_open(int err, char *buf, size_t size, const char *filename)
{
	char sbuf[128];

	switch (err) {
	case ENOENT:
		/*
		 * We will get here if we can't find the tracepoint, but one of
		 * debugfs or tracefs is configured, which means you probably
		 * want some tracepoint which wasn't compiled in your kernel.
		 * - jirka
		 */
		if (debugfs__configured() || tracefs__configured()) {
			snprintf(buf, size,
				 "Error:\tFile %s/%s not found.\n"
				 "Hint:\tPerhaps this kernel misses some CONFIG_ setting to enable this feature?.\n",
				 tracing_events_path, filename);
			break;
		}
		snprintf(buf, size, "%s",
			 "Error:\tUnable to find debugfs/tracefs\n"
			 "Hint:\tWas your kernel compiled with debugfs/tracefs support?\n"
			 "Hint:\tIs the debugfs/tracefs filesystem mounted?\n"
			 "Hint:\tTry 'sudo mount -t debugfs nodev /sys/kernel/debug'");
		break;
	case EACCES: {
		snprintf(buf, size,
			 "Error:\tNo permissions to read %s/%s\n"
			 "Hint:\tTry 'sudo mount -o remount,mode=755 %s'\n",
			 tracing_events_path, filename, tracing_mnt);
	}
		break;
	default:
		snprintf(buf, size, "%s", strerror_r(err, sbuf, sizeof(sbuf)));
		break;
	}

	return 0;
}

int tracing_path__strerror_open_tp(int err, char *buf, size_t size, const char *sys, const char *name)
{
	char path[PATH_MAX];

	snprintf(path, PATH_MAX, "%s/%s", sys, name ?: "*");

	return strerror_open(err, buf, size, path);
}
