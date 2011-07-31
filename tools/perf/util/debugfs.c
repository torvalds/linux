#include "util.h"
#include "debugfs.h"
#include "cache.h"

static int debugfs_premounted;
static char debugfs_mountpoint[MAX_PATH+1];

static const char *debugfs_known_mountpoints[] = {
	"/sys/kernel/debug/",
	"/debug/",
	0,
};

/* use this to force a umount */
void debugfs_force_cleanup(void)
{
	debugfs_find_mountpoint();
	debugfs_premounted = 0;
	debugfs_umount();
}

/* construct a full path to a debugfs element */
int debugfs_make_path(const char *element, char *buffer, int size)
{
	int len;

	if (strlen(debugfs_mountpoint) == 0) {
		buffer[0] = '\0';
		return -1;
	}

	len = strlen(debugfs_mountpoint) + strlen(element) + 1;
	if (len >= size)
		return len+1;

	snprintf(buffer, size-1, "%s/%s", debugfs_mountpoint, element);
	return 0;
}

static int debugfs_found;

/* find the path to the mounted debugfs */
const char *debugfs_find_mountpoint(void)
{
	const char **ptr;
	char type[100];
	FILE *fp;

	if (debugfs_found)
		return (const char *) debugfs_mountpoint;

	ptr = debugfs_known_mountpoints;
	while (*ptr) {
		if (debugfs_valid_mountpoint(*ptr) == 0) {
			debugfs_found = 1;
			strcpy(debugfs_mountpoint, *ptr);
			return debugfs_mountpoint;
		}
		ptr++;
	}

	/* give up and parse /proc/mounts */
	fp = fopen("/proc/mounts", "r");
	if (fp == NULL)
		die("Can't open /proc/mounts for read");

	while (fscanf(fp, "%*s %"
		      STR(MAX_PATH)
		      "s %99s %*s %*d %*d\n",
		      debugfs_mountpoint, type) == 2) {
		if (strcmp(type, "debugfs") == 0)
			break;
	}
	fclose(fp);

	if (strcmp(type, "debugfs") != 0)
		return NULL;

	debugfs_found = 1;

	return debugfs_mountpoint;
}

/* verify that a mountpoint is actually a debugfs instance */

int debugfs_valid_mountpoint(const char *debugfs)
{
	struct statfs st_fs;

	if (statfs(debugfs, &st_fs) < 0)
		return -ENOENT;
	else if (st_fs.f_type != (long) DEBUGFS_MAGIC)
		return -ENOENT;

	return 0;
}


int debugfs_valid_entry(const char *path)
{
	struct stat st;

	if (stat(path, &st))
		return -errno;

	return 0;
}

/* mount the debugfs somewhere if it's not mounted */

char *debugfs_mount(const char *mountpoint)
{
	/* see if it's already mounted */
	if (debugfs_find_mountpoint()) {
		debugfs_premounted = 1;
		return debugfs_mountpoint;
	}

	/* if not mounted and no argument */
	if (mountpoint == NULL) {
		/* see if environment variable set */
		mountpoint = getenv(PERF_DEBUGFS_ENVIRONMENT);
		/* if no environment variable, use default */
		if (mountpoint == NULL)
			mountpoint = "/sys/kernel/debug";
	}

	if (mount(NULL, mountpoint, "debugfs", 0, NULL) < 0)
		return NULL;

	/* save the mountpoint */
	strncpy(debugfs_mountpoint, mountpoint, sizeof(debugfs_mountpoint));
	debugfs_found = 1;

	return debugfs_mountpoint;
}

/* umount the debugfs */

int debugfs_umount(void)
{
	char umountcmd[128];
	int ret;

	/* if it was already mounted, leave it */
	if (debugfs_premounted)
		return 0;

	/* make sure it's a valid mount point */
	ret = debugfs_valid_mountpoint(debugfs_mountpoint);
	if (ret)
		return ret;

	snprintf(umountcmd, sizeof(umountcmd),
		 "/bin/umount %s", debugfs_mountpoint);
	return system(umountcmd);
}

int debugfs_write(const char *entry, const char *value)
{
	char path[MAX_PATH+1];
	int ret, count;
	int fd;

	/* construct the path */
	snprintf(path, sizeof(path), "%s/%s", debugfs_mountpoint, entry);

	/* verify that it exists */
	ret = debugfs_valid_entry(path);
	if (ret)
		return ret;

	/* get how many chars we're going to write */
	count = strlen(value);

	/* open the debugfs entry */
	fd = open(path, O_RDWR);
	if (fd < 0)
		return -errno;

	while (count > 0) {
		/* write it */
		ret = write(fd, value, count);
		if (ret <= 0) {
			if (ret == EAGAIN)
				continue;
			close(fd);
			return -errno;
		}
		count -= ret;
	}

	/* close it */
	close(fd);

	/* return success */
	return 0;
}

/*
 * read a debugfs entry
 * returns the number of chars read or a negative errno
 */
int debugfs_read(const char *entry, char *buffer, size_t size)
{
	char path[MAX_PATH+1];
	int ret;
	int fd;

	/* construct the path */
	snprintf(path, sizeof(path), "%s/%s", debugfs_mountpoint, entry);

	/* verify that it exists */
	ret = debugfs_valid_entry(path);
	if (ret)
		return ret;

	/* open the debugfs entry */
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;

	do {
		/* read it */
		ret = read(fd, buffer, size);
		if (ret == 0) {
			close(fd);
			return EOF;
		}
	} while (ret < 0 && errno == EAGAIN);

	/* close it */
	close(fd);

	/* make *sure* there's a null character at the end */
	buffer[ret] = '\0';

	/* return the number of chars read */
	return ret;
}
