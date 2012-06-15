
#include "util.h"
#include "sysfs.h"

static const char * const sysfs_known_mountpoints[] = {
	"/sys",
	0,
};

static int sysfs_found;
char sysfs_mountpoint[PATH_MAX];

static int sysfs_valid_mountpoint(const char *sysfs)
{
	struct statfs st_fs;

	if (statfs(sysfs, &st_fs) < 0)
		return -ENOENT;
	else if (st_fs.f_type != (long) SYSFS_MAGIC)
		return -ENOENT;

	return 0;
}

const char *sysfs_find_mountpoint(void)
{
	const char * const *ptr;
	char type[100];
	FILE *fp;

	if (sysfs_found)
		return (const char *) sysfs_mountpoint;

	ptr = sysfs_known_mountpoints;
	while (*ptr) {
		if (sysfs_valid_mountpoint(*ptr) == 0) {
			sysfs_found = 1;
			strcpy(sysfs_mountpoint, *ptr);
			return sysfs_mountpoint;
		}
		ptr++;
	}

	/* give up and parse /proc/mounts */
	fp = fopen("/proc/mounts", "r");
	if (fp == NULL)
		return NULL;

	while (!sysfs_found &&
	       fscanf(fp, "%*s %" STR(PATH_MAX) "s %99s %*s %*d %*d\n",
		      sysfs_mountpoint, type) == 2) {

		if (strcmp(type, "sysfs") == 0)
			sysfs_found = 1;
	}

	fclose(fp);

	return sysfs_found ? sysfs_mountpoint : NULL;
}
