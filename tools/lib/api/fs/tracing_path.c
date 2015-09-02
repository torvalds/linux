#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debugfs.h"
#include "tracefs.h"

#include "tracing_path.h"


char tracing_path[PATH_MAX + 1]        = "/sys/kernel/debug/tracing";
char tracing_events_path[PATH_MAX + 1] = "/sys/kernel/debug/tracing/events";


static void __tracing_path_set(const char *tracing, const char *mountpoint)
{
	snprintf(tracing_path, sizeof(tracing_path), "%s/%s",
		 mountpoint, tracing);
	snprintf(tracing_events_path, sizeof(tracing_events_path), "%s/%s%s",
		 mountpoint, tracing, "events");
}

static const char *tracing_path_tracefs_mount(void)
{
	const char *mnt;

	mnt = tracefs_mount(NULL);
	if (!mnt)
		return NULL;

	__tracing_path_set("", mnt);

	return mnt;
}

static const char *tracing_path_debugfs_mount(void)
{
	const char *mnt;

	mnt = debugfs_mount(NULL);
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
