/* TODO merge/factor in debugfs.c here */

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "debugfs.h"
#include "fs.h"

static const char * const sysfs__fs_known_mountpoints[] = {
	"/sys",
	0,
};

static const char * const procfs__known_mountpoints[] = {
	"/proc",
	0,
};

struct fs {
	const char		*name;
	const char * const	*mounts;
	char			 path[PATH_MAX + 1];
	bool			 found;
	long			 magic;
};

enum {
	FS__SYSFS  = 0,
	FS__PROCFS = 1,
};

static struct fs fs__entries[] = {
	[FS__SYSFS] = {
		.name	= "sysfs",
		.mounts	= sysfs__fs_known_mountpoints,
		.magic	= SYSFS_MAGIC,
	},
	[FS__PROCFS] = {
		.name	= "proc",
		.mounts	= procfs__known_mountpoints,
		.magic	= PROC_SUPER_MAGIC,
	},
};

static bool fs__read_mounts(struct fs *fs)
{
	bool found = false;
	char type[100];
	FILE *fp;

	fp = fopen("/proc/mounts", "r");
	if (fp == NULL)
		return NULL;

	while (!found &&
	       fscanf(fp, "%*s %" STR(PATH_MAX) "s %99s %*s %*d %*d\n",
		      fs->path, type) == 2) {

		if (strcmp(type, fs->name) == 0)
			found = true;
	}

	fclose(fp);
	return fs->found = found;
}

static int fs__valid_mount(const char *fs, long magic)
{
	struct statfs st_fs;

	if (statfs(fs, &st_fs) < 0)
		return -ENOENT;
	else if (st_fs.f_type != magic)
		return -ENOENT;

	return 0;
}

static bool fs__check_mounts(struct fs *fs)
{
	const char * const *ptr;

	ptr = fs->mounts;
	while (*ptr) {
		if (fs__valid_mount(*ptr, fs->magic) == 0) {
			fs->found = true;
			strcpy(fs->path, *ptr);
			return true;
		}
		ptr++;
	}

	return false;
}

static void mem_toupper(char *f, size_t len)
{
	while (len) {
		*f = toupper(*f);
		f++;
		len--;
	}
}

/*
 * Check for "NAME_PATH" environment variable to override fs location (for
 * testing). This matches the recommendation in Documentation/sysfs-rules.txt
 * for SYSFS_PATH.
 */
static bool fs__env_override(struct fs *fs)
{
	char *override_path;
	size_t name_len = strlen(fs->name);
	/* name + "_PATH" + '\0' */
	char upper_name[name_len + 5 + 1];
	memcpy(upper_name, fs->name, name_len);
	mem_toupper(upper_name, name_len);
	strcpy(&upper_name[name_len], "_PATH");

	override_path = getenv(upper_name);
	if (!override_path)
		return false;

	fs->found = true;
	strncpy(fs->path, override_path, sizeof(fs->path));
	return true;
}

static const char *fs__get_mountpoint(struct fs *fs)
{
	if (fs__env_override(fs))
		return fs->path;

	if (fs__check_mounts(fs))
		return fs->path;

	if (fs__read_mounts(fs))
		return fs->path;

	return NULL;
}

static const char *fs__mountpoint(int idx)
{
	struct fs *fs = &fs__entries[idx];

	if (fs->found)
		return (const char *)fs->path;

	return fs__get_mountpoint(fs);
}

#define FS__MOUNTPOINT(name, idx)	\
const char *name##__mountpoint(void)	\
{					\
	return fs__mountpoint(idx);	\
}

FS__MOUNTPOINT(sysfs,  FS__SYSFS);
FS__MOUNTPOINT(procfs, FS__PROCFS);

int filename__read_int(const char *filename, int *value)
{
	char line[64];
	int fd = open(filename, O_RDONLY), err = -1;

	if (fd < 0)
		return -1;

	if (read(fd, line, sizeof(line)) > 0) {
		*value = atoi(line);
		err = 0;
	}

	close(fd);
	return err;
}

int sysctl__read_int(const char *sysctl, int *value)
{
	char path[PATH_MAX];
	const char *procfs = procfs__mountpoint();

	if (!procfs)
		return -1;

	snprintf(path, sizeof(path), "%s/sys/%s", procfs, sysctl);

	return filename__read_int(path, value);
}
