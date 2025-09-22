/*	$OpenBSD: disk.c,v 1.4 2023/10/18 22:44:42 kettenis Exp $	*/

/*
 * Copyright (c) 2019 Visa Hankala
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "cmd.h"

int	disk_proberoot(const char *);

int mounted = 0;
int rdroot = -1;		/* fd that points to the root of the ramdisk */

const u_char zeroduid[8];

void
disk_init(void)
{
	char rootdevs[1024];
	char bootduid[17];
	char *devname, *disknames, *ptr;
	size_t size;
	int mib[2];

	rdroot = open("/", O_RDONLY);
	if (rdroot == -1)
		err(1, "failed to open root directory fd");

	if (strlen(cmd.bootdev) != 0)
		return;

	mib[0] = CTL_HW;
	mib[1] = HW_DISKNAMES;
	size = 0;
	if (sysctl(mib, 2, NULL, &size, NULL, 0) == -1) {
		fprintf(stderr, "%s: cannot get hw.disknames: %s\n", __func__,
		    strerror(errno));
		return;
	}
	disknames = malloc(size);
	if (disknames == NULL) {
		fprintf(stderr, "%s: out of memory\n", __func__);
		return;
	}
	if (sysctl(mib, 2, disknames, &size, NULL, 0) == -1) {
		fprintf(stderr, "%s: cannot get hw.disknames: %s\n", __func__,
		    strerror(errno));
		free(disknames);
		return;
	}

	snprintf(bootduid, sizeof(bootduid),
	    "%02x%02x%02x%02x%02x%02x%02x%02x", cmd.bootduid[0],
	    cmd.bootduid[1], cmd.bootduid[2], cmd.bootduid[3], cmd.bootduid[4],
	    cmd.bootduid[5], cmd.bootduid[6], cmd.bootduid[7]);

	printf("probing disks\n");
	rootdevs[0] = '\0';
	ptr = disknames;
	while ((devname = strsep(&ptr, ",")) != NULL) {
		char *duid;

		duid = strchr(devname, ':');
		if (duid == NULL)
			continue;
		*duid++ = '\0';

		/* Disk without a duid cannot be a root device. */
		if (strlen(duid) == 0)
			continue;

		/* If we have a bootduid match, nail it down! */
		if (strcmp(duid, bootduid) == 0) {
			snprintf(cmd.bootdev, sizeof(cmd.bootdev),
			    "%sa", devname);
		}

		/* Otherwise pick the first potential root disk. */
		if (disk_proberoot(devname)) {
			if (memcmp(cmd.bootduid, zeroduid, 8) == 0) {
				snprintf(cmd.bootdev, sizeof(cmd.bootdev),
				    "%sa", devname);
			}
			(void)strlcat(rootdevs, " ", sizeof(rootdevs));
			(void)strlcat(rootdevs, devname, sizeof(rootdevs));
		}
	}
	if (strlen(rootdevs) != 0)
		printf("available root devices:%s\n", rootdevs);
	else
		printf("no root devices found\n");
}

int
disk_proberoot(const char *devname)
{
	static const char *const names[] = {
		"bin", "dev", "etc", "home", "mnt", "root", "sbin", "tmp",
		"usr", "var", NULL
	};
	struct ufs_args ffs_args;
	struct stat st;
	char path[32];
	int i, is_root = 1;

	snprintf(path, sizeof(path), "/dev/%sa", devname);
	memset(&ffs_args, 0, sizeof(ffs_args));
	ffs_args.fspec = path;
	if (mount(MOUNT_FFS, "/mnt", MNT_RDONLY, &ffs_args) == -1)
		return 0;
	for (i = 0; names[i] != NULL; i++) {
		snprintf(path, sizeof(path), "/mnt/%s", names[i]);
		if (stat(path, &st) == -1 || !S_ISDIR(st.st_mode)) {
			is_root = 0;
			break;
		}
	}
	(void)unmount("/mnt", 0);

	return is_root;
}

const char *
disk_open(const char *path)
{
	struct ufs_args ffs_args;
	struct disklabel label;
	char devname[32];
	char *devpath;
	const char *ptr;
	int fd;

	if (mounted) {
		fprintf(stderr, "%s: cannot nest\n", __func__);
		return NULL;
	}

	ptr = strchr(path, ':');
	if (ptr != NULL) {
		snprintf(devname, sizeof(devname), "%.*s",
		    (int)(ptr - path), path);
		ptr++;	/* skip ':' */
	} else {
		strlcpy(devname, cmd.bootdev, sizeof(devname));
		ptr = path;
	}
	if (strlen(devname) == 0) {
		fprintf(stderr, "no device specified\n");
		return NULL;
	}

	cmd.hasduid = 0;
	fd = opendev(devname, O_RDONLY, OPENDEV_BLCK, &devpath);
	if (fd != -1) {
		if (ioctl(fd, DIOCGDINFO, &label) != -1) {
			memcpy(cmd.bootduid, label.d_uid, 8);
			cmd.hasduid = 1;
		}
		close(fd);
	} else {
		fprintf(stderr, "failed to open device %s: %s\n", devname,
		    strerror(errno));
		return NULL;
	}

	memset(&ffs_args, 0, sizeof(ffs_args));
	ffs_args.fspec = devpath;
	if (mount(MOUNT_FFS, "/mnt", MNT_NOATIME, &ffs_args) == -1) {
		if (mount(MOUNT_FFS, "/mnt", MNT_RDONLY, &ffs_args) == -1) {
			fprintf(stderr, "failed to mount %s: %s\n", devpath,
			    strerror(errno));
			return NULL;
		}
		fprintf(stderr, "%s: mounted read-only\n", devpath);
	}
	if (chroot("/mnt") == -1) {
		fprintf(stderr, "failed to chroot: %s\n", strerror(errno));
		(void)unmount("/mnt", 0);
		return NULL;
	}
	mounted = 1;

	return ptr;
}

void
disk_close(void)
{
	if (mounted) {
		(void)fchdir(rdroot);
		(void)chroot(".");
		mounted = 0;
		(void)unmount("/mnt", 0);
	}
}
