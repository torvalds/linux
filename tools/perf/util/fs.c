
/* TODO merge/factor into tools/lib/lk/debugfs.c */

#include "util.h"
#include "util/fs.h"

static const char * const sysfs__fs_known_mountpoints[] = {
	"/sys",
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
	FS__SYSFS = 0,
};

static struct fs fs__entries[] = {
	[FS__SYSFS] = {
		.name	= "sysfs",
		.mounts	= sysfs__fs_known_mountpoints,
		.magic	= SYSFS_MAGIC,
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

static const char *fs__get_mountpoint(struct fs *fs)
{
	if (fs__check_mounts(fs))
		return fs->path;

	return fs__read_mounts(fs) ? fs->path : NULL;
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

FS__MOUNTPOINT(sysfs, FS__SYSFS);
