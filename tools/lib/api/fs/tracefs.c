#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <linux/kernel.h>

#include "tracefs.h"

#ifndef TRACEFS_DEFAULT_PATH
#define TRACEFS_DEFAULT_PATH		"/sys/kernel/tracing"
#endif

char tracefs_mountpoint[PATH_MAX + 1] = TRACEFS_DEFAULT_PATH;

static const char * const tracefs_known_mountpoints[] = {
	TRACEFS_DEFAULT_PATH,
	"/sys/kernel/debug/tracing",
	"/tracing",
	"/trace",
	0,
};

static bool tracefs_found;

bool tracefs_configured(void)
{
	return tracefs_find_mountpoint() != NULL;
}

/* find the path to the mounted tracefs */
const char *tracefs_find_mountpoint(void)
{
	const char *ret;

	if (tracefs_found)
		return (const char *)tracefs_mountpoint;

	ret = find_mountpoint("tracefs", (long) TRACEFS_MAGIC,
			      tracefs_mountpoint, PATH_MAX + 1,
			      tracefs_known_mountpoints);

	if (ret)
		tracefs_found = true;

	return ret;
}

/* mount the tracefs somewhere if it's not mounted */
char *tracefs_mount(const char *mountpoint)
{
	/* see if it's already mounted */
	if (tracefs_find_mountpoint())
		goto out;

	/* if not mounted and no argument */
	if (mountpoint == NULL) {
		/* see if environment variable set */
		mountpoint = getenv(PERF_TRACEFS_ENVIRONMENT);
		/* if no environment variable, use default */
		if (mountpoint == NULL)
			mountpoint = TRACEFS_DEFAULT_PATH;
	}

	if (mount(NULL, mountpoint, "tracefs", 0, NULL) < 0)
		return NULL;

	/* save the mountpoint */
	tracefs_found = true;
	strncpy(tracefs_mountpoint, mountpoint, sizeof(tracefs_mountpoint));
out:
	return tracefs_mountpoint;
}
