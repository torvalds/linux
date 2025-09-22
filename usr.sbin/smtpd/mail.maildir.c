/*
 * Copyright (c) 2017 Gilles Chehade <gilles@poolp.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
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

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#define	MAILADDR_ESCAPE		"!#$%&'*/?^`{|}~"

static int	maildir_subdir(const char *, char *, size_t);
static void	maildir_mkdirs(const char *);
static void	maildir_engine(const char *, int);
static int	mkdirs_component(const char *, mode_t);
static int	mkdirs(const char *, mode_t);

int
main(int argc, char *argv[])
{
	int	ch;
	int	junk = 0;

	if (! geteuid())
		errx(1, "mail.maildir: may not be executed as root");

	while ((ch = getopt(argc, argv, "j")) != -1) {
		switch (ch) {
		case 'j':
			junk = 1;
			break;
		default:
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 1)
		errx(1, "mail.maildir: only one maildir is allowed");

	maildir_engine(argv[0], junk);

	return (0);
}

static int
maildir_subdir(const char *extension, char *dest, size_t len)
{
	char		*sanitized;

	if (strlcpy(dest, extension, len) >= len)
		return 0;

	for (sanitized = dest; *sanitized; sanitized++)
		if (strchr(MAILADDR_ESCAPE, *sanitized))
			*sanitized = ':';

	return 1;
}

static void
maildir_mkdirs(const char *dirname)
{
	uint	i;
	int	ret;
	char	pathname[PATH_MAX];
	char	*subdirs[] = { "cur", "tmp", "new" };

	if (mkdirs(dirname, 0700) == -1 && errno != EEXIST) {
		if (errno == EINVAL || errno == ENAMETOOLONG)
			err(1, NULL);
		err(EX_TEMPFAIL, NULL);
	}

	for (i = 0; i < nitems(subdirs); ++i) {
		ret = snprintf(pathname, sizeof pathname, "%s/%s", dirname,
		    subdirs[i]);
		if (ret < 0 || (size_t)ret >= sizeof pathname)
			errc(1, ENAMETOOLONG, "%s/%s", dirname, subdirs[i]);
		if (mkdir(pathname, 0700) == -1 && errno != EEXIST)
			err(EX_TEMPFAIL, NULL);
	}
}

static void
maildir_engine(const char *dirname, int junk)
{
	char	rootpath[PATH_MAX];
	char	junkpath[PATH_MAX];
	char	extpath[PATH_MAX];
	char	subdir[PATH_MAX];
	char	filename[PATH_MAX];
	char	hostname[HOST_NAME_MAX+1];

	char	tmp[PATH_MAX];
	char	new[PATH_MAX];

	int	ret;

	int	fd;
	FILE    *fp;
	char	*line = NULL;
	size_t	linesize = 0;
	struct stat	sb;
	char	*home;
	char	*extension;

	int	is_junk = 0;
	int	in_hdr  = 1;

	if (dirname == NULL) {
		if ((home = getenv("HOME")) == NULL)
			err(1, NULL);
		ret = snprintf(rootpath, sizeof rootpath, "%s/Maildir", home);
		if (ret < 0 || (size_t)ret >= sizeof rootpath)
			errc(1, ENAMETOOLONG, "%s/Maildir", home);
		dirname = rootpath;
	}
	maildir_mkdirs(dirname);

	if (junk) {
		/* create Junk subdirectory */
		ret = snprintf(junkpath, sizeof junkpath, "%s/.Junk", dirname);
		if (ret < 0 || (size_t)ret >= sizeof junkpath)
			errc(1, ENAMETOOLONG, "%s/.Junk", dirname);
		maildir_mkdirs(junkpath);
	}

	if ((extension = getenv("EXTENSION")) != NULL) {
		if (maildir_subdir(extension, subdir, sizeof(subdir)) &&
		    subdir[0]) {
			ret = snprintf(extpath, sizeof extpath, "%s/.%s",
			    dirname, subdir);
			if (ret < 0 || (size_t)ret >= sizeof extpath)
				errc(1, ENAMETOOLONG, "%s/.%s",
				    dirname, subdir);
			if (stat(extpath, &sb) != -1) {
				dirname = extpath;
				maildir_mkdirs(dirname);
			}
		}
	}

	if (gethostname(hostname, sizeof hostname) != 0)
		(void)strlcpy(hostname, "localhost", sizeof hostname);

	(void)snprintf(filename, sizeof filename, "%lld.%08x.%s",
	    (long long)time(NULL),
	    arc4random(),
	    hostname);

	(void)snprintf(tmp, sizeof tmp, "%s/tmp/%s", dirname, filename);

	fd = open(tmp, O_CREAT | O_EXCL | O_WRONLY, 0600);
	if (fd == -1)
		err(EX_TEMPFAIL, NULL);
	if ((fp = fdopen(fd, "w")) == NULL)
		err(EX_TEMPFAIL, NULL);

	while (getline(&line, &linesize, stdin) != -1) {
		line[strcspn(line, "\n")] = '\0';
		if (line[0] == '\0')
			in_hdr = 0;
		if (junk && in_hdr &&
		    (strcasecmp(line, "x-spam: yes") == 0 ||
			strcasecmp(line, "x-spam-flag: yes") == 0))
			is_junk = 1;
		fprintf(fp, "%s\n", line);
	}
	free(line);
	if (ferror(stdin))
		err(EX_TEMPFAIL, NULL);

	if (fflush(fp) == EOF ||
	    ferror(fp) ||
	    fsync(fd) == -1 ||
	    fclose(fp) == EOF)
		err(EX_TEMPFAIL, NULL);

	(void)snprintf(new, sizeof new, "%s/new/%s",
	    is_junk ? junkpath : dirname, filename);

	if (rename(tmp, new) == -1)
		err(EX_TEMPFAIL, NULL);

	exit(0);
}


static int
mkdirs_component(const char *path, mode_t mode)
{
	struct stat	sb;

	if (stat(path, &sb) == -1) {
		if (errno != ENOENT)
			return 0;
		if (mkdir(path, mode | S_IWUSR | S_IXUSR) == -1)
			return 0;
	}
	else if (!S_ISDIR(sb.st_mode)) {
		errno = ENOTDIR;
		return 0;
	}

	return 1;
}

static int
mkdirs(const char *path, mode_t mode)
{
	char	 buf[PATH_MAX];
	int	 i = 0;
	int	 done = 0;
	const char	*p;

	/* absolute path required */
	if (*path != '/') {
		errno = EINVAL;
		return 0;
	}

	/* make sure we don't exceed PATH_MAX */
	if (strlen(path) >= sizeof buf) {
		errno = ENAMETOOLONG;
		return 0;
	}

	memset(buf, 0, sizeof buf);
	for (p = path; *p; p++) {
		if (*p == '/') {
			if (buf[0] != '\0')
				if (!mkdirs_component(buf, mode))
					return 0;
			while (*p == '/')
				p++;
			buf[i++] = '/';
			buf[i++] = *p;
			if (*p == '\0' && ++done)
				break;
			continue;
		}
		buf[i++] = *p;
	}
	if (!done)
		if (!mkdirs_component(buf, mode))
			return 0;

	if (chmod(path, mode) == -1)
		return 0;

	return 1;
}
