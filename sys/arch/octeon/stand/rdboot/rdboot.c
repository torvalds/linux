/*	$OpenBSD: rdboot.c,v 1.10 2025/08/23 16:48:17 gkoehler Exp $	*/

/*
 * Copyright (c) 2019-2020 Visa Hankala
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
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/select.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#include <machine/octboot.h>
#include <machine/param.h>

#include "cmd.h"
#include "disk.h"

#define DEVRANDOM	"/dev/random"
#define BOOTRANDOM	"/etc/random.seed"
#define BOOTRANDOM_MAX	256	/* no point being greater than RC4STATE */
#define KERNEL		"/bsd"

int	loadrandom(void);
void	kexec(int);

struct cmd_state cmd;
int octbootfd = -1;
const char version[] = "1.5";

int
main(void)
{
	char rootdev[PATH_MAX];
	int fd, hasboot, isupgrade = 0;

	fd = open(_PATH_CONSOLE, O_RDWR);
	login_tty(fd);

	/* Keep stdout unbuffered to mimic ordinary bootblocks. */
	setvbuf(stdout, NULL, _IONBF, 0);

	printf(">> OpenBSD/" MACHINE " BOOT %s\n", version);

	octbootfd = open("/dev/octboot", O_WRONLY);
	if (octbootfd == -1)
		err(1, "cannot open boot control device");

	memset(&cmd, 0, sizeof(cmd));
	cmd.boothowto = 0;
	cmd.conf = "/etc/boot.conf";
	strlcpy(cmd.image, KERNEL, sizeof(cmd.image));
	cmd.timeout = 5;

	if (ioctl(octbootfd, OBIOC_GETROOTDEV, rootdev) == -1) {
		if (errno != ENOENT)
			fprintf(stderr, "cannot get rootdev from kernel: %s\n",
			    strerror(errno));
	} else {
		snprintf(cmd.bootdev, sizeof(cmd.bootdev), "%s%sa",
		    rootdev, isduid(rootdev, OPENDEV_PART) ? "." : "");
	}

	disk_init();

	if (upgrade()) {
		strlcpy(cmd.image, "/bsd.upgrade", sizeof(cmd.image));
		printf("upgrade detected: switching to %s\n", cmd.image);
		isupgrade = 1;
	}

	hasboot = read_conf();

	for (;;) {
		if (hasboot <= 0) {
			do {
				printf("boot> ");
			} while (!getcmd());
		}

		if (loadrandom() == 0)
			cmd.boothowto |= RB_GOODRANDOM;

		kexec(isupgrade);

		hasboot = 0;
		strlcpy(cmd.image, KERNEL, sizeof(cmd.image));
		printf("will try %s\n", cmd.image);
	}

	return 0;
}

int
loadrandom(void)
{
	char buf[BOOTRANDOM_MAX];
	struct stat sb;
	int fd, ret = 0;

	/* Read the file from the device specified by the kernel path. */
	if (disk_open(cmd.path) == NULL)
		return -1;
	fd = open(BOOTRANDOM, O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "%s: cannot open %s: %s", __func__, BOOTRANDOM,
		    strerror(errno));
		disk_close();
		return -1;
	}
	if (fstat(fd, &sb) == 0) {
		if (sb.st_mode & S_ISTXT) {
			printf("NOTE: random seed is being reused.\n");
			ret = -1;
		}
		if (read(fd, buf, sizeof(buf)) != sizeof(buf))
			ret = -1;
		fchmod(fd, sb.st_mode | S_ISTXT);
	} else {
		ret = -1;
	}
	close(fd);
	disk_close();

	/*
	 * Push the whole buffer to the entropy pool.
	 * The kernel will use the entropy on kexec().
	 * It does not matter if some of the buffer content is uninitialized.
	 */
	fd = open(DEVRANDOM, O_WRONLY);
	if (fd == -1) {
		fprintf(stderr, "%s: cannot open %s: %s", __func__,
		    DEVRANDOM, strerror(errno));
		return -1;
	}
	write(fd, buf, sizeof(buf));
	close(fd);
	return ret;
}

void
kexec(int isupgrade)
{
	struct octboot_kexec_args kargs;
	struct stat sb;
	char boothowtostr[32];
	char rootdev[32];
	char *kimg = NULL;
	const char *path;
	ssize_t n;
	off_t pos;
	int argc, fd = -1, ret;

	path = disk_open(cmd.path);
	if (path == NULL)
		return;

	fd = open(path, O_RDONLY);
	if (fd == -1)
		goto load_failed;
	if (fstat(fd, &sb) == -1)
		goto load_failed;
	if (!S_ISREG(sb.st_mode) || sb.st_size == 0) {
		errno = ENOEXEC;
		goto load_failed;
	}

	/* Prevent re-upgrade: chmod a-x bsd.upgrade */
	if (isupgrade) {
		sb.st_mode &= ~(S_IXUSR|S_IXGRP|S_IXOTH);
		if (fchmod(fd, sb.st_mode) == -1)
			printf("fchmod a-x %s: failed\n", path);
	}

	kimg = malloc(sb.st_size);
	if (kimg == NULL)
		goto load_failed;

	pos = 0;
	while (pos < sb.st_size) {
		n = read(fd, kimg + pos, sb.st_size - pos);
		if (n == -1)
			goto load_failed;
		pos += n;
	}

	close(fd);
	disk_close();

	memset(&kargs, 0, sizeof(kargs));
	kargs.kimg = kimg;
	kargs.klen = sb.st_size;
	argc = 0;
	if (cmd.boothowto != 0) {
		snprintf(boothowtostr, sizeof(boothowtostr), "boothowto=%d",
		    cmd.boothowto);
		kargs.argv[argc++] = boothowtostr;
	}
	if (cmd.hasduid) {
		snprintf(rootdev, sizeof(rootdev),
		    "rootdev=%02x%02x%02x%02x%02x%02x%02x%02x",
		    cmd.bootduid[0], cmd.bootduid[1],
		    cmd.bootduid[2], cmd.bootduid[3],
		    cmd.bootduid[4], cmd.bootduid[5],
		    cmd.bootduid[6], cmd.bootduid[7]);
		kargs.argv[argc++] = rootdev;
	}

	printf("booting %s\n", cmd.path);
	ret = ioctl(octbootfd, OBIOC_KEXEC, &kargs);
	if (ret == -1)
		fprintf(stderr, "failed to execute kernel %s: %s\n",
		    cmd.path, strerror(errno));
	else
		fprintf(stderr, "kexec() returned unexpectedly\n");
	free(kimg);
	return;

load_failed:
	fprintf(stderr, "failed to load kernel %s: %s\n",
	    cmd.path, strerror(errno));
	if (fd != -1)
		close(fd);
	disk_close();
	free(kimg);
}
