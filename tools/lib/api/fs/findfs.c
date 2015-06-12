#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/vfs.h>

#include "findfs.h"

/* verify that a mountpoint is actually the type we want */

int valid_mountpoint(const char *mount, long magic)
{
	struct statfs st_fs;

	if (statfs(mount, &st_fs) < 0)
		return -ENOENT;
	else if ((long)st_fs.f_type != magic)
		return -ENOENT;

	return 0;
}

/* find the path to a mounted file system */
const char *find_mountpoint(const char *fstype, long magic,
			    char *mountpoint, int len,
			    const char * const *known_mountpoints)
{
	const char * const *ptr;
	char format[128];
	char type[100];
	FILE *fp;

	if (known_mountpoints) {
		ptr = known_mountpoints;
		while (*ptr) {
			if (valid_mountpoint(*ptr, magic) == 0) {
				strncpy(mountpoint, *ptr, len - 1);
				mountpoint[len-1] = 0;
				return mountpoint;
			}
			ptr++;
		}
	}

	/* give up and parse /proc/mounts */
	fp = fopen("/proc/mounts", "r");
	if (fp == NULL)
		return NULL;

	snprintf(format, 128, "%%*s %%%ds %%99s %%*s %%*d %%*d\n", len);

	while (fscanf(fp, format, mountpoint, type) == 2) {
		if (strcmp(type, fstype) == 0)
			break;
	}
	fclose(fp);

	if (strcmp(type, fstype) != 0)
		return NULL;

	return mountpoint;
}
