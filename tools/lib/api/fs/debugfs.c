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

int debugfs__strerror_open(int err, char *buf, size_t size, const char *filename)
{
	char sbuf[128];

	switch (err) {
	case ENOENT:
		if (debugfs_found) {
			snprintf(buf, size,
				 "Error:\tFile %s/%s not found.\n"
				 "Hint:\tPerhaps this kernel misses some CONFIG_ setting to enable this feature?.\n",
				 debugfs_mountpoint, filename);
			break;
		}
		snprintf(buf, size, "%s",
			 "Error:\tUnable to find debugfs\n"
			 "Hint:\tWas your kernel compiled with debugfs support?\n"
			 "Hint:\tIs the debugfs filesystem mounted?\n"
			 "Hint:\tTry 'sudo mount -t debugfs nodev /sys/kernel/debug'");
		break;
	case EACCES: {
		const char *mountpoint = debugfs_mountpoint;

		if (!access(debugfs_mountpoint, R_OK) && strncmp(filename, "tracing/", 8) == 0) {
			const char *tracefs_mntpoint = tracefs_find_mountpoint();

			if (tracefs_mntpoint)
				mountpoint = tracefs_mntpoint;
		}

		snprintf(buf, size,
			 "Error:\tNo permissions to read %s/%s\n"
			 "Hint:\tTry 'sudo mount -o remount,mode=755 %s'\n",
			 debugfs_mountpoint, filename, mountpoint);
	}
		break;
	default:
		snprintf(buf, size, "%s", strerror_r(err, sbuf, sizeof(sbuf)));
		break;
	}

	return 0;
}

int debugfs__strerror_open_tp(int err, char *buf, size_t size, const char *sys, const char *name)
{
	char path[PATH_MAX];

	snprintf(path, PATH_MAX, "tracing/events/%s/%s", sys, name ?: "*");

	return debugfs__strerror_open(err, buf, size, path);
}
