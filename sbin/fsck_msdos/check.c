/*	$OpenBSD: check.c,v 1.21 2021/07/12 15:09:18 beck Exp $	*/
/*	$NetBSD: check.c,v 1.8 1997/10/17 11:19:29 ws Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997 Wolfgang Solfrank
 * Copyright (c) 1995 Martin Husemann
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/dkio.h>
#include <sys/disklabel.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <util.h>
#include <err.h>

#include "ext.h"

struct disklabel lab;

int
checkfilesys(const char *fname)
{
	int dosfs;
	struct bootblock boot;
	struct fatEntry *fat = NULL;
	char *realdev;
	int i;
	int mod = 0;

	if (unveil("/dev", "rw") == -1)
		err(1, "unveil /dev");

	rdonly = alwaysno;

	dosfs = opendev(fname, rdonly ? O_RDONLY : O_RDWR, 0, &realdev);
	if (dosfs == -1 && !rdonly) {
		dosfs = opendev(fname, O_RDONLY, 0, &realdev);
		rdonly = 1;
	}
	if (dosfs == -1) {
		xperror("Can't open");
		return (8);
	}

	if (!preen) {
		printf("** %s", realdev);
		if (strncmp(fname, realdev, PATH_MAX) != 0)
			printf(" (%s)", fname);
		if (rdonly)
			printf(" (NO WRITE)");
		printf("\n");
	}

	if (ioctl(dosfs, DIOCGDINFO, (char *)&lab) == -1)
		pfatal("can't read disk label for %s\n", fname);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (readboot(dosfs, &boot) != FSOK) {
		(void)close(dosfs);
		return (8);
	}

	if (!preen) {
		if (boot.ValidFat < 0)
			printf("** Phase 1 - Read and Compare FATs\n");
		else
			printf("** Phase 1 - Read FAT\n");
	}

	mod |= readfat(dosfs, &boot, boot.ValidFat >= 0 ? boot.ValidFat : 0, &fat);
	if (mod & FSFATAL) {
		(void)close(dosfs);
		return 8;
	}

	if (boot.ValidFat < 0)
		for (i = 1; i < boot.FATs; i++) {
			struct fatEntry *currentFat;
			mod |= readfat(dosfs, &boot, i, &currentFat);

			if (mod & FSFATAL) {
				free(fat);
				(void)close(dosfs);
				return 8;
			}

			mod |= comparefat(&boot, fat, currentFat, i);
			free(currentFat);
			if (mod & FSFATAL) {
				free(fat);
				(void)close(dosfs);
				return (8);
			}
		}

	if (!preen)
		printf("** Phase 2 - Check Cluster Chains\n");

	mod |= checkfat(&boot, fat);
	if (mod & FSFATAL) {
		free(fat);
		(void)close(dosfs);
		return (8);
	}

	if (mod & FSFATMOD)
		mod |= writefat(dosfs, &boot, fat); /* delay writing fats?	XXX */
	if (mod & FSFATAL) {
		free(fat);
		(void)close(dosfs);
		return (8);
	}

	if (!preen)
		printf("** Phase 3 - Check Directories\n");

	mod |= resetDosDirSection(&boot, fat);
	if (mod & FSFATAL) {
		free(fat);
		close(dosfs);
		return 8;
	}

	if (mod & FSFATMOD)
		mod |= writefat(dosfs, &boot, fat); /* delay writing fats?	XXX */
	if (mod & FSFATAL) {
		finishDosDirSection();
		free(fat);
		(void)close(dosfs);
		return (8);
	}

	mod |= handleDirTree(dosfs, &boot, fat);
	if (mod & FSFATAL) {
		finishDosDirSection();
		free(fat);
		(void)close(dosfs);
		return (8);
	}

	if (!preen)
		printf("** Phase 4 - Check for Lost Files\n");

	mod |= checklost(dosfs, &boot, fat);
	if (mod & FSFATAL) {
		finishDosDirSection();
		free(fat);
		(void)close(dosfs);
		return 8;
	}

	if (mod & FSFATMOD)
		mod |= writefat(dosfs, &boot, fat); /* delay writing fats?    XXX */

	finishDosDirSection();
	free(fat);
	(void)close(dosfs);
	if (mod & FSFATAL)
		return 8;

	if (boot.NumBad)
		pwarn("%d files, %d free (%d clusters), %d bad (%d clusters)\n",
		      boot.NumFiles,
		      boot.NumFree * boot.ClusterSize / 1024, boot.NumFree,
		      boot.NumBad * boot.ClusterSize / 1024, boot.NumBad);
	else
		pwarn("%d files, %d free (%d clusters)\n",
		      boot.NumFiles,
		      boot.NumFree * boot.ClusterSize / 1024, boot.NumFree);

	if (mod & (FSFATAL | FSERROR))
		return (8);
	if (mod) {
		pwarn("\n***** FILE SYSTEM WAS MODIFIED *****\n");
		return (4);
	}
	return (0);
}
