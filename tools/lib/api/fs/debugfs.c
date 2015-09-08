#define _GNU_SOURCE
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

#include "debugfs.h"
#include "tracefs.h"

#ifndef DEBUGFS_DEFAULT_PATH
#define DEBUGFS_DEFAULT_PATH		"/sys/kernel/debug"
#endif

char debugfs_mountpoint[PATH_MAX + 1] = DEBUGFS_DEFAULT_PATH;

static const char * const debugfs_known_mountpoints[] = {
	DEBUGFS_DEFAULT_PATH,
	"/debug",
	0,
};

static bool debugfs_found;

bool debugfs_configured(void)
{
	return debugfs_find_mountpoint() != NULL;
}

/* find the path to the mounted debugfs */
const char *debugfs_find_mountpoint(void)
{
	const char *ret;

	if (debugfs_found)
		return (const char *)debugfs_mountpoint;

	ret = find_mountpoint("debugfs", (long) DEBUGFS_MAGIC,
			      debugfs_mountpoint, PATH_MAX + 1,
			      debugfs_known_mountpoints);
	if (ret)
		debugfs_found = true;

	return ret;
}

/* mount the debugfs somewhere if it's not mounted */
char *debugfs_mount(const char *mountpoint)
{
	/* see if it's already mounted */
	if (debugfs_find_mountpoint())
		goto out;

	/* if not mounted and no argument */
	if (mountpoint == NULL) {
		/* see if environment variable set */
		mountpoint = getenv(PERF_DEBUGFS_ENVIRONMENT);
		/* if no environment variable, use default */
		if (mountpoint == NULL)
			mountpoint = DEBUGFS_DEFAULT_PATH;
	}

	if (mount(NULL, mountpoint, "debugfs", 0, NULL) < 0)
		return NULL;

	/* save the mountpoint */
	debugfs_found = true;
	strncpy(debugfs_mountpoint, mountpoint, sizeof(debugfs_mountpoint));
out:
	return debugfs_mountpoint;
}
