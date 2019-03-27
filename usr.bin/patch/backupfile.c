/*-
 * Copyright (C) 1990 Free Software Foundation, Inc.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * without restriction.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * backupfile.c -- make Emacs style backup file names
 *
 * David MacKenzie <djm@ai.mit.edu>. Some algorithms adapted from GNU Emacs.
 *
 * $OpenBSD: backupfile.c,v 1.20 2009/10/27 23:59:41 deraadt Exp $
 * $FreeBSD$
 */

#include <ctype.h>
#include <dirent.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backupfile.h"


#define ISDIGIT(c) (isascii ((unsigned char)c) && isdigit ((unsigned char)c))

/* Which type of backup file names are generated. */
enum backup_type backup_type = none;

/*
 * The extension added to file names to produce a simple (as opposed to
 * numbered) backup file name.
 */
const char	*simple_backup_suffix = "~";

static char	*concat(const char *, const char *);
static char	*make_version_name(const char *, int);
static int	max_backup_version(const char *, const char *);
static int	version_number(const char *, const char *, size_t);
static int	argmatch(const char *, const char **);
static void	invalid_arg(const char *, const char *, int);

/*
 * Return the name of the new backup file for file FILE, allocated with
 * malloc.  Return 0 if out of memory. FILE must not end with a '/' unless it
 * is the root directory. Do not call this function if backup_type == none.
 */
char *
find_backup_file_name(const char *file)
{
	char	*dir, *base_versions, *tmp_file;
	int	highest_backup;

	if (backup_type == simple)
		return concat(file, simple_backup_suffix);
	tmp_file = strdup(file);
	if (tmp_file == NULL)
		return NULL;
	base_versions = concat(basename(tmp_file), ".~");
	free(tmp_file);
	if (base_versions == NULL)
		return NULL;
	tmp_file = strdup(file);
	if (tmp_file == NULL) {
		free(base_versions);
		return NULL;
	}
	dir = dirname(tmp_file);
	if (dir == NULL) {
		free(base_versions);
		free(tmp_file);
		return NULL;
	}
	highest_backup = max_backup_version(base_versions, dir);
	free(base_versions);
	free(tmp_file);
	if (backup_type == numbered_existing && highest_backup == 0)
		return concat(file, simple_backup_suffix);
	return make_version_name(file, highest_backup + 1);
}

/*
 * Return the number of the highest-numbered backup file for file FILE in
 * directory DIR.  If there are no numbered backups of FILE in DIR, or an
 * error occurs reading DIR, return 0. FILE should already have ".~" appended
 * to it.
 */
static int
max_backup_version(const char *file, const char *dir)
{
	DIR	*dirp;
	struct dirent	*dp;
	int	highest_version, this_version;
	size_t	file_name_length;

	dirp = opendir(dir);
	if (dirp == NULL)
		return 0;

	highest_version = 0;
	file_name_length = strlen(file);

	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_namlen <= file_name_length)
			continue;

		this_version = version_number(file, dp->d_name, file_name_length);
		if (this_version > highest_version)
			highest_version = this_version;
	}
	closedir(dirp);
	return highest_version;
}

/*
 * Return a string, allocated with malloc, containing "FILE.~VERSION~".
 * Return 0 if out of memory.
 */
static char *
make_version_name(const char *file, int version)
{
	char	*backup_name;

	if (asprintf(&backup_name, "%s.~%d~", file, version) == -1)
		return NULL;
	return backup_name;
}

/*
 * If BACKUP is a numbered backup of BASE, return its version number;
 * otherwise return 0.  BASE_LENGTH is the length of BASE. BASE should
 * already have ".~" appended to it.
 */
static int
version_number(const char *base, const char *backup, size_t base_length)
{
	int		version;
	const char	*p;

	version = 0;
	if (!strncmp(base, backup, base_length) && ISDIGIT(backup[base_length])) {
		for (p = &backup[base_length]; ISDIGIT(*p); ++p)
			version = version * 10 + *p - '0';
		if (p[0] != '~' || p[1])
			version = 0;
	}
	return version;
}

/*
 * Return the newly-allocated concatenation of STR1 and STR2. If out of
 * memory, return 0.
 */
static char  *
concat(const char *str1, const char *str2)
{
	char	*newstr;

	if (asprintf(&newstr, "%s%s", str1, str2) == -1)
		return NULL;
	return newstr;
}

/*
 * If ARG is an unambiguous match for an element of the null-terminated array
 * OPTLIST, return the index in OPTLIST of the matched element, else -1 if it
 * does not match any element or -2 if it is ambiguous (is a prefix of more
 * than one element).
 */
static int
argmatch(const char *arg, const char **optlist)
{
	int	i;	/* Temporary index in OPTLIST. */
	size_t	arglen;	/* Length of ARG. */
	int	matchind = -1;	/* Index of first nonexact match. */
	int	ambiguous = 0;	/* If nonzero, multiple nonexact match(es). */

	arglen = strlen(arg);

	/* Test all elements for either exact match or abbreviated matches.  */
	for (i = 0; optlist[i]; i++) {
		if (!strncmp(optlist[i], arg, arglen)) {
			if (strlen(optlist[i]) == arglen)
				/* Exact match found.  */
				return i;
			else if (matchind == -1)
				/* First nonexact match found.  */
				matchind = i;
			else
				/* Second nonexact match found.  */
				ambiguous = 1;
		}
	}
	if (ambiguous)
		return -2;
	else
		return matchind;
}

/*
 * Error reporting for argmatch. KIND is a description of the type of entity
 * that was being matched. VALUE is the invalid value that was given. PROBLEM
 * is the return value from argmatch.
 */
static void
invalid_arg(const char *kind, const char *value, int problem)
{
	fprintf(stderr, "patch: ");
	if (problem == -1)
		fprintf(stderr, "invalid");
	else			/* Assume -2. */
		fprintf(stderr, "ambiguous");
	fprintf(stderr, " %s `%s'\n", kind, value);
}

static const char *backup_args[] = {
	"none", "never", "simple", "nil", "existing", "t", "numbered", 0
};

static enum backup_type backup_types[] = {
	none, simple, simple, numbered_existing,
	numbered_existing, numbered, numbered
};

/*
 * Return the type of backup indicated by VERSION. Unique abbreviations are
 * accepted.
 */
enum backup_type
get_version(const char *version)
{
	int	i;

	if (version == NULL || *version == '\0')
		return numbered_existing;
	i = argmatch(version, backup_args);
	if (i >= 0)
		return backup_types[i];
	invalid_arg("version control type", version, i);
	exit(2);
}
