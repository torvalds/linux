/*	$OpenBSD: macppc_installboot.c,v 1.12 2025/09/17 16:12:10 deraadt Exp $	*/

/*
 * Copyright (c) 2011 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2010 Otto Moerbeek <otto@openbsd.org>
 * Copyright (c) 2003 Tom Cosgrove <tom.cosgrove@arches-consulting.com>
 * Copyright (c) 1997 Michael Shalayeff
 * Copyright (c) 1994 Paul Kranenburg
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Paul Kranenburg.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>	/* DEV_BSIZE */
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <util.h>
#include <endian.h>

#include "installboot.h"

static int	create_filesystem(struct disklabel *, char);
static void	write_filesystem(struct disklabel *, char);
static int	findmbrfat(int, struct disklabel *);

void
md_init(void)
{
	stages = 1;
	stage1 = "/usr/mdec/ofwboot";
}

void
md_loadboot(void)
{
}

void
md_prepareboot(int devfd, char *dev)
{
	struct disklabel dl;
	int part;

	/* Get and check disklabel. */
	if (ioctl(devfd, DIOCGDINFO, &dl) == -1)
		err(1, "disklabel: %s", dev);
	if (dl.d_magic != DISKMAGIC)
		errx(1, "bad disklabel magic=0x%08x", dl.d_magic);

	/* Warn on unknown disklabel types. */
	if (dl.d_type == 0)
		warnx("disklabel type unknown");

	part = findmbrfat(devfd, &dl);
	if (part != -1) {
		create_filesystem(&dl, (char)part);
		return;
	}
}

void
md_installboot(int devfd, char *dev)
{
	struct disklabel dl;
	int part;

	/* Get and check disklabel. */
	if (ioctl(devfd, DIOCGDINFO, &dl) == -1)
		err(1, "disklabel: %s", dev);
	if (dl.d_magic != DISKMAGIC)
		errx(1, "bad disklabel magic=0x%08x", dl.d_magic);

	/* Warn on unknown disklabel types. */
	if (dl.d_type == 0)
		warnx("disklabel type unknown");

	part = findmbrfat(devfd, &dl);
	if (part != -1) {
		write_filesystem(&dl, (char)part);
		return;
	}
}

static int
create_filesystem(struct disklabel *dl, char part)
{
	static const char *newfsfmt = "/sbin/newfs -t msdos %s >/dev/null";
	struct msdosfs_args args;
	char cmd[60];
	int rslt;

	/* Newfs <duid>.<part> as msdos filesystem. */
	memset(&args, 0, sizeof(args));
	rslt = asprintf(&args.fspec,
	    "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx.%c",
            dl->d_uid[0], dl->d_uid[1], dl->d_uid[2], dl->d_uid[3],
            dl->d_uid[4], dl->d_uid[5], dl->d_uid[6], dl->d_uid[7],
	    part);
	if (rslt == -1) {
		warn("bad special device");
		return rslt;
	}

	rslt = snprintf(cmd, sizeof(cmd), newfsfmt, args.fspec);
	if (rslt >= sizeof(cmd)) {
		warnx("can't build newfs command");
		free(args.fspec);
		rslt = -1;
		return rslt;
	}

	if (verbose)
		fprintf(stderr, "%s %s\n",
		    (nowrite ? "would newfs" : "newfsing"), args.fspec);
	if (!nowrite) {
		rslt = system(cmd);
		if (rslt == -1) {
			warn("system('%s') failed", cmd);
			free(args.fspec);
			return rslt;
		}
	}

	free(args.fspec);
	return 0;
}

static void
write_filesystem(struct disklabel *dl, char part)
{
	static const char *fsckfmt = "/sbin/fsck -t msdos %s >/dev/null";
	struct msdosfs_args args;
	char cmd[60];
	char dst[PATH_MAX];
	size_t mntlen;
	int rslt;

	/* Create directory for temporary mount point. */
	strlcpy(dst, "/tmp/installboot.XXXXXXXXXX", sizeof(dst));
	if (mkdtemp(dst) == NULL)
		err(1, "mkdtemp('%s') failed", dst);
	mntlen = strlen(dst);

	/* Mount <duid>.<part> as msdos filesystem. */
	memset(&args, 0, sizeof(args));
	rslt = asprintf(&args.fspec,
	    "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx.%c",
            dl->d_uid[0], dl->d_uid[1], dl->d_uid[2], dl->d_uid[3],
            dl->d_uid[4], dl->d_uid[5], dl->d_uid[6], dl->d_uid[7],
	    part);
	if (rslt == -1) {
		warn("bad special device");
		goto rmdir;
	}

	args.export_info.ex_root = -2;
	args.export_info.ex_flags = 0;
	args.flags = MSDOSFSMNT_LONGNAME;

	if (mount(MOUNT_MSDOS, dst, 0, &args) == -1) {
		/* Try fsck'ing it. */
		rslt = snprintf(cmd, sizeof(cmd), fsckfmt, args.fspec);
		if (rslt >= sizeof(cmd)) {
			warnx("can't build fsck command");
			rslt = -1;
			goto rmdir;
		}
		rslt = system(cmd);
		if (rslt == -1) {
			warn("system('%s') failed", cmd);
			goto rmdir;
		}
		if (mount(MOUNT_MSDOS, dst, 0, &args) == -1) {
			/* Try newfs'ing it. */
			rslt = create_filesystem(dl, part);
			if (rslt == -1)
				goto rmdir;
			rslt = mount(MOUNT_MSDOS, dst, 0, &args);
			if (rslt == -1) {
				warn("unable to mount MSDOS partition");
				goto rmdir;
			}
		}
	}

	/*
	 * Copy /usr/mdec/ofwboot to $FS/ofwboot.
	 */
	if (strlcat(dst, "/ofwboot", sizeof(dst)) >= sizeof(dst)) {
		rslt = -1;
		warn("unable to build /ofwboot path");
		goto umount;
	}
	if (verbose)
		fprintf(stderr, "%s %s to %s\n",
		    (nowrite ? "would copy" : "copying"), stage1, dst);
	if (!nowrite) {
		rslt = filecopy(stage1, dst);
		if (rslt == -1)
			goto umount;
	}

	rslt = 0;

umount:
	dst[mntlen] = '\0';
	if (unmount(dst, MNT_FORCE) == -1)
		err(1, "unmount('%s') failed", dst);

rmdir:
	free(args.fspec);
	dst[mntlen] = '\0';
	if (rmdir(dst) == -1)
		err(1, "rmdir('%s') failed", dst);

	if (rslt == -1)
		exit(1);
}

int
findmbrfat(int devfd, struct disklabel *dl)
{
	struct dos_partition	 dp[NDOSPART];
	ssize_t			 len;
	u_int64_t		 start = 0;
	int			 i;
	u_int8_t		*secbuf;

	if ((secbuf = malloc(dl->d_secsize)) == NULL)
		err(1, NULL);

	/* Read MBR. */
	len = pread(devfd, secbuf, dl->d_secsize, 0);
	if (len != dl->d_secsize)
		err(4, "can't read mbr");
	memcpy(dp, &secbuf[DOSPARTOFF], sizeof(dp));

	for (i = 0; i < NDOSPART; i++) {
		if (dp[i].dp_typ == DOSPTYP_UNUSED)
			continue;
		if (dp[i].dp_typ == DOSPTYP_FAT16L ||
		    dp[i].dp_typ == DOSPTYP_FAT32L ||
		    dp[i].dp_typ == DOSPTYP_FAT16B)
			start = letoh32(dp[i].dp_start);
	}

	free(secbuf);

	if (start) {
		for (i = 0; i < MAXPARTITIONS; i++) {
			if (DL_GETPSIZE(&dl->d_partitions[i]) > 0 &&
			    DL_GETPOFFSET(&dl->d_partitions[i]) == start)
				return (DL_PARTNUM2NAME(i));
		}
	}

	return (-1);
}
