// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mount.h>

#include "fs.h"
#include "../io.h"
#include "debug-internal.h"

#define _STR(x) #x
#define STR(x) _STR(x)

#ifndef SYSFS_MAGIC
#define SYSFS_MAGIC            0x62656572
#endif

#ifndef PROC_SUPER_MAGIC
#define PROC_SUPER_MAGIC       0x9fa0
#endif

#ifndef DEBUGFS_MAGIC
#define DEBUGFS_MAGIC          0x64626720
#endif

#ifndef TRACEFS_MAGIC
#define TRACEFS_MAGIC          0x74726163
#endif

#ifndef HUGETLBFS_MAGIC
#define HUGETLBFS_MAGIC        0x958458f6
#endif

#ifndef BPF_FS_MAGIC
#define BPF_FS_MAGIC           0xcafe4a11
#endif

static const char * const sysfs__known_mountpoints[] = {
	"/sys",
	0,
};

static const char * const procfs__known_mountpoints[] = {
	"/proc",
	0,
};

#ifndef DEBUGFS_DEFAULT_PATH
#define DEBUGFS_DEFAULT_PATH "/sys/kernel/debug"
#endif

static const char * const debugfs__known_mountpoints[] = {
	DEBUGFS_DEFAULT_PATH,
	"/debug",
	0,
};


#ifndef TRACEFS_DEFAULT_PATH
#define TRACEFS_DEFAULT_PATH "/sys/kernel/tracing"
#endif

static const char * const tracefs__known_mountpoints[] = {
	TRACEFS_DEFAULT_PATH,
	"/sys/kernel/debug/tracing",
	"/tracing",
	"/trace",
	0,
};

static const char * const hugetlbfs__known_mountpoints[] = {
	0,
};

static const char * const bpf_fs__known_mountpoints[] = {
	"/sys/fs/bpf",
	0,
};

struct fs {
	const char *		 const name;
	const char * const *	 const mounts;
	char			*path;
	pthread_mutex_t		 mount_mutex;
	const long		 magic;
};

#ifndef TRACEFS_MAGIC
#define TRACEFS_MAGIC 0x74726163
#endif

static void fs__init_once(struct fs *fs);
static const char *fs__mountpoint(const struct fs *fs);
static const char *fs__mount(struct fs *fs);

#define FS(lower_name, fs_name, upper_name)		\
static struct fs fs__##lower_name = {			\
	.name = #fs_name,				\
	.mounts = lower_name##__known_mountpoints,	\
	.magic = upper_name##_MAGIC,			\
	.mount_mutex = PTHREAD_MUTEX_INITIALIZER,	\
};							\
							\
static void lower_name##_init_once(void)		\
{							\
	struct fs *fs = &fs__##lower_name;		\
							\
	fs__init_once(fs);				\
}							\
							\
const char *lower_name##__mountpoint(void)		\
{							\
	static pthread_once_t init_once = PTHREAD_ONCE_INIT;	\
	struct fs *fs = &fs__##lower_name;		\
							\
	pthread_once(&init_once, lower_name##_init_once);	\
	return fs__mountpoint(fs);			\
}							\
							\
const char *lower_name##__mount(void)			\
{							\
	const char *mountpoint = lower_name##__mountpoint();	\
	struct fs *fs = &fs__##lower_name;		\
							\
	if (mountpoint)					\
		return mountpoint;			\
							\
	return fs__mount(fs);				\
}							\
							\
bool lower_name##__configured(void)			\
{							\
	return lower_name##__mountpoint() != NULL;	\
}

FS(sysfs, sysfs, SYSFS);
FS(procfs, procfs, PROC_SUPER);
FS(debugfs, debugfs, DEBUGFS);
FS(tracefs, tracefs, TRACEFS);
FS(hugetlbfs, hugetlbfs, HUGETLBFS);
FS(bpf_fs, bpf, BPF_FS);

static bool fs__read_mounts(struct fs *fs)
{
	char type[100];
	FILE *fp;
	char path[PATH_MAX + 1];

	fp = fopen("/proc/mounts", "r");
	if (fp == NULL)
		return false;

	while (fscanf(fp, "%*s %" STR(PATH_MAX) "s %99s %*s %*d %*d\n",
		      path, type) == 2) {

		if (strcmp(type, fs->name) == 0) {
			fs->path = strdup(path);
			fclose(fp);
			return fs->path != NULL;
		}
	}
	fclose(fp);
	return false;
}

static int fs__valid_mount(const char *fs, long magic)
{
	struct statfs st_fs;

	if (statfs(fs, &st_fs) < 0)
		return -ENOENT;
	else if ((long)st_fs.f_type != magic)
		return -ENOENT;

	return 0;
}

static bool fs__check_mounts(struct fs *fs)
{
	const char * const *ptr;

	ptr = fs->mounts;
	while (*ptr) {
		if (fs__valid_mount(*ptr, fs->magic) == 0) {
			fs->path = strdup(*ptr);
			if (!fs->path)
				return false;
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
 * testing). This matches the recommendation in Documentation/admin-guide/sysfs-rules.rst
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

	fs->path = strdup(override_path);
	if (!fs->path)
		return false;
	return true;
}

static void fs__init_once(struct fs *fs)
{
	if (!fs__env_override(fs) &&
	    !fs__check_mounts(fs) &&
	    !fs__read_mounts(fs)) {
		assert(!fs->path);
	} else {
		assert(fs->path);
	}
}

static const char *fs__mountpoint(const struct fs *fs)
{
	return fs->path;
}

static const char *mount_overload(struct fs *fs)
{
	size_t name_len = strlen(fs->name);
	/* "PERF_" + name + "_ENVIRONMENT" + '\0' */
	char upper_name[5 + name_len + 12 + 1];

	snprintf(upper_name, name_len, "PERF_%s_ENVIRONMENT", fs->name);
	mem_toupper(upper_name, name_len);

	return getenv(upper_name) ?: *fs->mounts;
}

static const char *fs__mount(struct fs *fs)
{
	const char *mountpoint;

	pthread_mutex_lock(&fs->mount_mutex);

	/* Check if path found inside the mutex to avoid races with other callers of mount. */
	mountpoint = fs__mountpoint(fs);
	if (mountpoint)
		goto out;

	mountpoint = mount_overload(fs);

	if (mount(NULL, mountpoint, fs->name, 0, NULL) == 0 &&
	    fs__valid_mount(mountpoint, fs->magic) == 0) {
		fs->path = strdup(mountpoint);
		mountpoint = fs->path;
	}
out:
	pthread_mutex_unlock(&fs->mount_mutex);
	return mountpoint;
}

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

static int filename__read_ull_base(const char *filename,
				   unsigned long long *value, int base)
{
	char line[64];
	int fd = open(filename, O_RDONLY), err = -1;

	if (fd < 0)
		return -1;

	if (read(fd, line, sizeof(line)) > 0) {
		*value = strtoull(line, NULL, base);
		if (*value != ULLONG_MAX)
			err = 0;
	}

	close(fd);
	return err;
}

/*
 * Parses @value out of @filename with strtoull.
 * By using 16 for base to treat the number as hex.
 */
int filename__read_xll(const char *filename, unsigned long long *value)
{
	return filename__read_ull_base(filename, value, 16);
}

/*
 * Parses @value out of @filename with strtoull.
 * By using 0 for base, the strtoull detects the
 * base automatically (see man strtoull).
 */
int filename__read_ull(const char *filename, unsigned long long *value)
{
	return filename__read_ull_base(filename, value, 0);
}

int filename__read_str(const char *filename, char **buf, size_t *sizep)
{
	struct io io;
	char bf[128];
	int err;

	io.fd = open(filename, O_RDONLY);
	if (io.fd < 0)
		return -errno;
	io__init(&io, io.fd, bf, sizeof(bf));
	*buf = NULL;
	err = io__getdelim(&io, buf, sizep, /*delim=*/-1);
	if (err < 0) {
		free(*buf);
		*buf = NULL;
	} else
		err = 0;
	close(io.fd);
	return err;
}

int filename__write_int(const char *filename, int value)
{
	int fd = open(filename, O_WRONLY), err = -1;
	char buf[64];

	if (fd < 0)
		return err;

	sprintf(buf, "%d", value);
	if (write(fd, buf, sizeof(buf)) == sizeof(buf))
		err = 0;

	close(fd);
	return err;
}

int procfs__read_str(const char *entry, char **buf, size_t *sizep)
{
	char path[PATH_MAX];
	const char *procfs = procfs__mountpoint();

	if (!procfs)
		return -1;

	snprintf(path, sizeof(path), "%s/%s", procfs, entry);

	return filename__read_str(path, buf, sizep);
}

static int sysfs__read_ull_base(const char *entry,
				unsigned long long *value, int base)
{
	char path[PATH_MAX];
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return -1;

	snprintf(path, sizeof(path), "%s/%s", sysfs, entry);

	return filename__read_ull_base(path, value, base);
}

int sysfs__read_xll(const char *entry, unsigned long long *value)
{
	return sysfs__read_ull_base(entry, value, 16);
}

int sysfs__read_ull(const char *entry, unsigned long long *value)
{
	return sysfs__read_ull_base(entry, value, 0);
}

int sysfs__read_int(const char *entry, int *value)
{
	char path[PATH_MAX];
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return -1;

	snprintf(path, sizeof(path), "%s/%s", sysfs, entry);

	return filename__read_int(path, value);
}

int sysfs__read_str(const char *entry, char **buf, size_t *sizep)
{
	char path[PATH_MAX];
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return -1;

	snprintf(path, sizeof(path), "%s/%s", sysfs, entry);

	return filename__read_str(path, buf, sizep);
}

int sysfs__read_bool(const char *entry, bool *value)
{
	struct io io;
	char bf[16];
	int ret = 0;
	char path[PATH_MAX];
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return -1;

	snprintf(path, sizeof(path), "%s/%s", sysfs, entry);
	io.fd = open(path, O_RDONLY);
	if (io.fd < 0)
		return -errno;

	io__init(&io, io.fd, bf, sizeof(bf));
	switch (io__get_char(&io)) {
	case '1':
	case 'y':
	case 'Y':
		*value = true;
		break;
	case '0':
	case 'n':
	case 'N':
		*value = false;
		break;
	default:
		ret = -1;
	}
	close(io.fd);

	return ret;
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

int sysfs__write_int(const char *entry, int value)
{
	char path[PATH_MAX];
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs)
		return -1;

	if (snprintf(path, sizeof(path), "%s/%s", sysfs, entry) >= PATH_MAX)
		return -1;

	return filename__write_int(path, value);
}
