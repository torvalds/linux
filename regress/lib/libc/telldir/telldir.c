/*	$OpenBSD: telldir.c,v 1.4 2013/11/03 00:20:24 schwarze Exp $	*/

/*	Written by Otto Moerbeek, 2006,  Public domain.	*/

#include <sys/types.h>
#include <dirent.h>
#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "utils.h"

#define NFILES 1000

static void
longloop(DIR *dp, int i)
{
	struct dirent *f;
	char file[PATH_MAX];
	long pos, t, remember = -1;

	rewinddir(dp);
	snprintf(file, sizeof file, "%d", i);

	/* Scan through all files, remember where file i is. */

	for (;;) {
		pos = telldir(dp);
		f = readdir(dp);
		if (f == NULL)
			break;
		if (strcmp(file, f->d_name) == 0)
			remember = pos;
	}
	if (remember == -1)
		errx(1, "remember %s", file);

	/* Go back to i, checking seekdir, telldir and readdir. */

	seekdir(dp, remember);
	if ((t = telldir(dp)) != remember)
		errx(1, "tell after seek %s %ld != %ld", file, t, remember);
	if ((t = telldir(dp)) != remember)
		errx(1, "tell after tell %s %ld != %ld", file, t, remember);
	f = readdir(dp);
	if (f == NULL)
		errx(1, "readdir %s at %ld", file, remember);

	if (strcmp(f->d_name, file) != 0)
		errx(1, "name mismatch: %s != %s", f->d_name, file);
}

void
longseek(void)
{
	DIR *dp;
	int i;

	createfiles(NFILES);

	dp = opendir("d");
	if (dp == NULL)
		err(1, "longseek: opendir");

	for (i = 0; i < NFILES; i++)
		longloop(dp, (i + NFILES/2) % NFILES);

	closedir(dp);
	delfiles();
}
