/*	$OpenBSD: shortseek.c,v 1.1 2013/11/03 00:20:24 schwarze Exp $	*/

/*	Written by Otto Moerbeek, 2006,  Public domain.	*/
/*	Modified by Ingo Schwarze, 2013,  Public domain. */

#include <sys/types.h>
#include <dirent.h>
#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "utils.h"

#define NFILES 5

static void
shortloop(DIR *dp, int iend, int iback)
{
	struct dirent *f;
	char fend[PATH_MAX], fback[PATH_MAX];
	long pos, t, remember = -1;
	int i;

	rewinddir(dp);
	snprintf(fend, sizeof fend, "%d", iend);
	snprintf(fback, sizeof fback, "%d", iback);

	/* Scan to iend, remember where iback is. */

	for (;;) {
		pos = telldir(dp);
		f = readdir(dp);
		if (f == NULL)
			errx(1, "file %s not found", fend);
		if (strcmp(fback, f->d_name) == 0)
			remember = pos;
		if (strcmp(fend, f->d_name) == 0)
			break;
	}
	if (remember == -1)
		errx(1, "file %s not found", fback);

	/* Go back to iback, checking seekdir, telldir and readdir. */

	seekdir(dp, remember);
	if ((t = telldir(dp)) != remember)
		errx(1, "tell after seek %s %ld != %ld", fback, t, remember);
	if ((t = telldir(dp)) != remember)
		errx(1, "tell after tell %s %ld != %ld", fback, t, remember);
	f = readdir(dp);
	if (f == NULL)
		errx(1, "readdir %s at %ld", fback, remember);

	if (strcmp(f->d_name, fback))
		errx(1, "name mismatch: %s != %s", f->d_name, fback);

	/* Check that readdir can iterate the remaining files. */

	for (i = iback + 1; i < NFILES; i++) {
		f = readdir(dp);
		if (f == NULL)
			errx(1, "readdir %i failed", i);
	}

	/* Check that readdir stops at the right place. */

	f = readdir(dp);
	if (f != NULL)
		errx(1, "readdir %i returned %s", NFILES, f->d_name);
}

void
shortseek(void)
{
	DIR *dp;
	int iend, iback;

	createfiles(NFILES);

	dp = opendir("d");
	if (dp == NULL)
		err(1, "shortseek: opendir");

	for (iend = 0; iend < NFILES; iend++)
		for (iback = 0; iback <= iend; iback++)
			shortloop(dp, iend, iback);

	closedir(dp);
	delfiles();
}
