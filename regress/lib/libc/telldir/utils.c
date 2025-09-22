/*	$OpenBSD: utils.c,v 1.2 2017/07/27 15:08:37 bluhm Exp $	*/

/*	Written by Otto Moerbeek, 2006,  Public domain.	*/

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

void
createfiles(int nfiles)
{
	int i, fd;
	char file[PATH_MAX];

	mkdir("d", 0755);
	for (i = 0; i < nfiles; i++) {
		snprintf(file, sizeof file, "d/%d", i);
		if ((fd = open(file, O_CREAT | O_WRONLY, 0600)) == -1)
			err(1, "open %s", file);
		close(fd);
	}
}

void
delfiles(void)
{
	DIR *dp;
	struct dirent *f;
	char file[PATH_MAX];

	dp = opendir("d");
	if (dp == NULL)
		err(1, "opendir");
	while ((f = readdir(dp))) {
		if (strcmp(f->d_name, ".") == 0 ||
		    strcmp(f->d_name, "..") == 0)
			continue;
		snprintf(file, sizeof file, "d/%s", f->d_name);
		if (unlink(file) == -1)
			err(1, "unlink %s", f->d_name);
	}
	closedir(dp);
	if (rmdir("d") == -1)
		err(1, "rmdir");
}
