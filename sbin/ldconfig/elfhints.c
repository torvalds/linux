/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 John D. Polstra
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <ctype.h>
#include <dirent.h>
#include <elf-hints.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ldconfig.h"

#define MAXDIRS		1024		/* Maximum directories in path */
#define MAXFILESIZE	(16*1024)	/* Maximum hints file size */

static void	add_dir(const char *, const char *, int);
static void	read_dirs_from_file(const char *, const char *);
static void	read_elf_hints(const char *, int);
static void	write_elf_hints(const char *);

static const char	*dirs[MAXDIRS];
static int		 ndirs;
int			 insecure;

static void
add_dir(const char *hintsfile, const char *name, int trusted)
{
	struct stat 	stbuf;
	int		i;

	/* Do some security checks */
	if (!trusted && !insecure) {
		if (stat(name, &stbuf) == -1) {
			warn("%s", name);
			return;
		}
		if (stbuf.st_uid != 0) {
			warnx("%s: ignoring directory not owned by root", name);
			return;
		}
		if ((stbuf.st_mode & S_IWOTH) != 0) {
			warnx("%s: ignoring world-writable directory", name);
			return;
		}
		if ((stbuf.st_mode & S_IWGRP) != 0) {
			warnx("%s: ignoring group-writable directory", name);
			return;
		}
	}

	for (i = 0;  i < ndirs;  i++)
		if (strcmp(dirs[i], name) == 0)
			return;
	if (ndirs >= MAXDIRS)
		errx(1, "\"%s\": Too many directories in path", hintsfile);
	dirs[ndirs++] = name;
}

void
list_elf_hints(const char *hintsfile)
{
	int	i;
	int	nlibs;

	read_elf_hints(hintsfile, 1);
	printf("%s:\n", hintsfile);
	printf("\tsearch directories:");
	for (i = 0;  i < ndirs;  i++)
		printf("%c%s", i == 0 ? ' ' : ':', dirs[i]);
	printf("\n");

	nlibs = 0;
	for (i = 0;  i < ndirs;  i++) {
		DIR		*dirp;
		struct dirent	*dp;

		if ((dirp = opendir(dirs[i])) == NULL)
			continue;
		while ((dp = readdir(dirp)) != NULL) {
			int		 len;
			int		 namelen;
			const char	*name;
			const char	*vers;

			/* Name can't be shorter than "libx.so.0" */
			if ((len = strlen(dp->d_name)) < 9 ||
			    strncmp(dp->d_name, "lib", 3) != 0)
				continue;
			name = dp->d_name + 3;
			vers = dp->d_name + len;
			while (vers > dp->d_name && isdigit(*(vers-1)))
				vers--;
			if (vers == dp->d_name + len)
				continue;
			if (vers < dp->d_name + 4 ||
			    strncmp(vers - 4, ".so.", 4) != 0)
				continue;

			/* We have a valid shared library name. */
			namelen = (vers - 4) - name;
			printf("\t%d:-l%.*s.%s => %s/%s\n", nlibs,
			    namelen, name, vers, dirs[i], dp->d_name);
			nlibs++;
		}
		closedir(dirp);
	}
}

static void
read_dirs_from_file(const char *hintsfile, const char *listfile)
{
	FILE	*fp;
	char	 buf[MAXPATHLEN];
	int	 linenum;

	if ((fp = fopen(listfile, "r")) == NULL)
		err(1, "%s", listfile);

	linenum = 0;
	while (fgets(buf, sizeof buf, fp) != NULL) {
		char	*cp, *sp;

		linenum++;
		cp = buf;
		/* Skip leading white space. */
		while (isspace(*cp))
			cp++;
		if (*cp == '#' || *cp == '\0')
			continue;
		sp = cp;
		/* Advance over the directory name. */
		while (!isspace(*cp) && *cp != '\0')
			cp++;
		/* Terminate the string and skip trailing white space. */
		if (*cp != '\0') {
			*cp++ = '\0';
			while (isspace(*cp))
				cp++;
		}
		/* Now we had better be at the end of the line. */
		if (*cp != '\0')
			warnx("%s:%d: trailing characters ignored",
			    listfile, linenum);

		if ((sp = strdup(sp)) == NULL)
			errx(1, "Out of memory");
		add_dir(hintsfile, sp, 0);
	}

	fclose(fp);
}

static void
read_elf_hints(const char *hintsfile, int must_exist)
{
	int	 		 fd;
	struct stat		 s;
	void			*mapbase;
	struct elfhints_hdr	*hdr;
	char			*strtab;
	char			*dirlist;
	char			*p;

	if ((fd = open(hintsfile, O_RDONLY)) == -1) {
		if (errno == ENOENT && !must_exist)
			return;
		err(1, "Cannot open \"%s\"", hintsfile);
	}
	if (fstat(fd, &s) == -1)
		err(1, "Cannot stat \"%s\"", hintsfile);
	if (s.st_size > MAXFILESIZE)
		errx(1, "\"%s\" is unreasonably large", hintsfile);
	/*
	 * We use a read-write, private mapping so that we can null-terminate
	 * some strings in it without affecting the underlying file.
	 */
	mapbase = mmap(NULL, s.st_size, PROT_READ|PROT_WRITE,
	    MAP_PRIVATE, fd, 0);
	if (mapbase == MAP_FAILED)
		err(1, "Cannot mmap \"%s\"", hintsfile);
	close(fd);

	hdr = (struct elfhints_hdr *)mapbase;
	if (hdr->magic != ELFHINTS_MAGIC)
		errx(1, "\"%s\": invalid file format", hintsfile);
	if (hdr->version != 1)
		errx(1, "\"%s\": unrecognized file version (%d)", hintsfile,
		    hdr->version);

	strtab = (char *)mapbase + hdr->strtab;
	dirlist = strtab + hdr->dirlist;

	if (*dirlist != '\0')
		while ((p = strsep(&dirlist, ":")) != NULL)
			add_dir(hintsfile, p, 1);
}

void
update_elf_hints(const char *hintsfile, int argc, char **argv, int merge)
{
	int	i;

	if (merge)
		read_elf_hints(hintsfile, 0);
	for (i = 0;  i < argc;  i++) {
		struct stat	s;

		if (stat(argv[i], &s) == -1)
			warn("warning: %s", argv[i]);
		else if (S_ISREG(s.st_mode))
			read_dirs_from_file(hintsfile, argv[i]);
		else
			add_dir(hintsfile, argv[i], 0);
	}
	write_elf_hints(hintsfile);
}

static void
write_elf_hints(const char *hintsfile)
{
	struct elfhints_hdr	 hdr;
	char			*tempname;
	int			 fd;
	FILE			*fp;
	int			 i;

	if (asprintf(&tempname, "%s.XXXXXX", hintsfile) == -1)
		errx(1, "Out of memory");
	if ((fd = mkstemp(tempname)) ==  -1)
		err(1, "mkstemp(%s)", tempname);
	if (fchmod(fd, 0444) == -1)
		err(1, "fchmod(%s)", tempname);
	if ((fp = fdopen(fd, "wb")) == NULL)
		err(1, "fdopen(%s)", tempname);

	hdr.magic = ELFHINTS_MAGIC;
	hdr.version = 1;
	hdr.strtab = sizeof hdr;
	hdr.strsize = 0;
	hdr.dirlist = 0;
	memset(hdr.spare, 0, sizeof hdr.spare);

	/* Count up the size of the string table. */
	if (ndirs > 0) {
		hdr.strsize += strlen(dirs[0]);
		for (i = 1;  i < ndirs;  i++)
			hdr.strsize += 1 + strlen(dirs[i]);
	}
	hdr.dirlistlen = hdr.strsize;
	hdr.strsize++;	/* For the null terminator */

	/* Write the header. */
	if (fwrite(&hdr, 1, sizeof hdr, fp) != sizeof hdr)
		err(1, "%s: write error", tempname);
	/* Write the strings. */
	if (ndirs > 0) {
		if (fputs(dirs[0], fp) == EOF)
			err(1, "%s: write error", tempname);
		for (i = 1;  i < ndirs;  i++)
			if (fprintf(fp, ":%s", dirs[i]) < 0)
				err(1, "%s: write error", tempname);
	}
	if (putc('\0', fp) == EOF || fclose(fp) == EOF)
		err(1, "%s: write error", tempname);

	if (rename(tempname, hintsfile) == -1)
		err(1, "rename %s to %s", tempname, hintsfile);
	free(tempname);
}
