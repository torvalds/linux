/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Robert Nordier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <md5.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int crc(int fd, uint32_t *cval, off_t *clen);

#define DISTMD5     1		/* MD5 format */
#define DISTINF     2		/* .inf format */
#define DISTTYPES   2		/* types supported */

#define E_UNKNOWN   1		/* Unknown format */
#define E_BADMD5    2		/* Invalid MD5 format */
#define E_BADINF    3		/* Invalid .inf format */
#define E_NAME      4		/* Can't derive component name */
#define E_LENGTH    5		/* Length mismatch */
#define E_CHKSUM    6		/* Checksum mismatch */
#define E_ERRNO     7		/* sys_errlist[errno] */

#define isfatal(err)   ((err) && (err) <= E_NAME)

#define NAMESIZE  256           /* filename buffer size */
#define MDSUMLEN   32           /* length of MD5 message digest */

#define isstdin(path)  ((path)[0] == '-' && !(path)[1])

static const char *opt_dir;	/* where to look for components */
static const char *opt_name;	/* name for accessing components */
static int opt_all;		/* report on all components */
static int opt_ignore;		/* ignore missing components */
static int opt_recurse;		/* search directories recursively */
static int opt_silent;		/* silent about inaccessible files */
static int opt_type;		/* dist type: md5 or inf */
static int opt_exist;		/* just verify existence */

static int ckdist(const char *path, int type);
static int chkmd5(FILE * fp, const char *path);
static int chkinf(FILE * fp, const char *path);
static int report(const char *path, const char *name, int error);
static const char *distname(const char *path, const char *name,
	                    const char *ext);
static const char *stripath(const char *path);
static int distfile(const char *path);
static int disttype(const char *name);
static int fail(const char *path, const char *msg);
static void usage(void);

int
main(int argc, char *argv[])
{
    static char *arg[2];
    struct stat sb;
    FTS *ftsp;
    FTSENT *f;
    int rval, c, type;

    while ((c = getopt(argc, argv, "ad:in:rst:x")) != -1)
	switch (c) {
	case 'a':
	    opt_all = 1;
	    break;
	case 'd':
	    opt_dir = optarg;
	    break;
	case 'i':
	    opt_ignore = 1;
	    break;
	case 'n':
	    opt_name = optarg;
	    break;
	case 'r':
	    opt_recurse = 1;
	    break;
	case 's':
	    opt_silent = 1;
	    break;
	case 't':
	    if ((opt_type = disttype(optarg)) == 0) {
		warnx("illegal argument to -t option");
		usage();
	    }
	    break;
	case 'x':
	    opt_exist = 1;
	    break;
	default:
	    usage();
	}
    argc -= optind;
    argv += optind;
    if (argc < 1)
	usage();
    if (opt_dir) {
	if (stat(opt_dir, &sb))
	    err(2, "%s", opt_dir);
	if (!S_ISDIR(sb.st_mode))
	    errx(2, "%s: not a directory", opt_dir);
    }
    rval = 0;
    do {
	if (isstdin(*argv))
	    rval |= ckdist(*argv, opt_type);
	else if (stat(*argv, &sb))
	    rval |= fail(*argv, NULL);
	else if (S_ISREG(sb.st_mode))
	    rval |= ckdist(*argv, opt_type);
	else {
	    arg[0] = *argv;
	    if ((ftsp = fts_open(arg, FTS_LOGICAL, NULL)) == NULL)
		err(2, "fts_open");
	    while ((f = fts_read(ftsp)) != NULL)
		switch (f->fts_info) {
		case FTS_DC:
		    rval = fail(f->fts_path, "Directory causes a cycle");
		    break;
		case FTS_DNR:
		case FTS_ERR:
		case FTS_NS:
		    rval = fail(f->fts_path, sys_errlist[f->fts_errno]);
		    break;
		case FTS_D:
		    if (!opt_recurse && f->fts_level > FTS_ROOTLEVEL &&
			fts_set(ftsp, f, FTS_SKIP))
			err(2, "fts_set");
		    break;
		case FTS_F:
		    if ((type = distfile(f->fts_name)) != 0 &&
			(!opt_type || type == opt_type))
			rval |= ckdist(f->fts_path, type);
		    break;
                default: ;
		}
	    if (errno)
		err(2, "fts_read");
	    if (fts_close(ftsp))
		err(2, "fts_close");
	}
    } while (*++argv);
    return rval;
}

static int
ckdist(const char *path, int type)
{
    FILE *fp;
    int rval, c;

    if (isstdin(path)) {
	path = "(stdin)";
	fp = stdin;
    } else if ((fp = fopen(path, "r")) == NULL)
	return fail(path, NULL);
    if (!type) {
	if (fp != stdin)
	    type = distfile(path);
	if (!type)
	    if ((c = fgetc(fp)) != EOF) {
		type = c == 'M' ? DISTMD5 : c == 'P' ? DISTINF : 0;
		(void)ungetc(c, fp);
	    }
    }
    switch (type) {
    case DISTMD5:
	rval = chkmd5(fp, path);
	break;
    case DISTINF:
	rval = chkinf(fp, path);
	break;
    default:
	rval = report(path, NULL, E_UNKNOWN);
    }
    if (ferror(fp))
	warn("%s", path);
    if (fp != stdin && fclose(fp))
	err(2, "%s", path);
    return rval;
}

static int
chkmd5(FILE * fp, const char *path)
{
    char buf[298];              /* "MD5 (NAMESIZE = MDSUMLEN" */
    char name[NAMESIZE + 1];
    char sum[MDSUMLEN + 1], chk[MDSUMLEN + 1];
    const char *dname;
    char *s;
    int rval, error, c, fd;
    char ch;

    rval = 0;
    while (fgets(buf, sizeof(buf), fp)) {
	dname = NULL;
	error = 0;
	if (((c = sscanf(buf, "MD5 (%256s = %32s%c", name, sum,
			 &ch)) != 3 && (!feof(fp) || c != 2)) ||
	    (c == 3 && ch != '\n') ||
	    (s = strrchr(name, ')')) == NULL ||
	    strlen(sum) != MDSUMLEN)
	    error = E_BADMD5;
	else {
	    *s = 0;
	    if ((dname = distname(path, name, NULL)) == NULL)
		error = E_NAME;
	    else if (opt_exist) {
		if ((fd = open(dname, O_RDONLY)) == -1)
		    error = E_ERRNO;
		else if (close(fd))
		    err(2, "%s", dname);
	    } else if (!MD5File(dname, chk))
		error = E_ERRNO;
	    else if (strcmp(chk, sum))
		error = E_CHKSUM;
	}
	if (opt_ignore && error == E_ERRNO && errno == ENOENT)
	    continue;
	if (error || opt_all)
	    rval |= report(path, dname, error);
	if (isfatal(error))
	    break;
    }
    return rval;
}

static int
chkinf(FILE * fp, const char *path)
{
    char buf[30];               /* "cksum.2 = 10 6" */
    char ext[3];
    struct stat sb;
    const char *dname;
    off_t len;
    u_long sum;
    intmax_t sumlen;
    uint32_t chk;
    int rval, error, c, pieces, cnt, fd;
    char ch;

    rval = 0;
    for (cnt = -1; fgets(buf, sizeof(buf), fp); cnt++) {
	fd = -1;
	dname = NULL;
	error = 0;
	if (cnt == -1) {
	    if ((c = sscanf(buf, "Pieces =  %d%c", &pieces, &ch)) != 2 ||
		ch != '\n' || pieces < 1)
		error = E_BADINF;
	} else if (((c = sscanf(buf, "cksum.%2s = %lu %jd%c", ext, &sum,
			        &sumlen, &ch)) != 4 &&
		    (!feof(fp) || c != 3)) || (c == 4 && ch != '\n') ||
		   ext[0] != 'a' + cnt / 26 || ext[1] != 'a' + cnt % 26)
	    error = E_BADINF;
	else if ((dname = distname(fp == stdin ? NULL : path, NULL,
				    ext)) == NULL)
	    error = E_NAME;
	else if ((fd = open(dname, O_RDONLY)) == -1)
	    error = E_ERRNO;
	else if (fstat(fd, &sb))
	    error = E_ERRNO;
	else if (sb.st_size != (off_t)sumlen)
	    error = E_LENGTH;
	else if (!opt_exist) {
	    if (crc(fd, &chk, &len))
		error = E_ERRNO;
	    else if (chk != sum)
		error = E_CHKSUM;
	}
	if (fd != -1 && close(fd))
	    err(2, "%s", dname);
	if (opt_ignore && error == E_ERRNO && errno == ENOENT)
	    continue;
	if (error || (opt_all && cnt >= 0))
	    rval |= report(path, dname, error);
	if (isfatal(error))
	    break;
    }
    return rval;
}

static int
report(const char *path, const char *name, int error)
{
    if (name)
	name = stripath(name);
    switch (error) {
    case E_UNKNOWN:
	printf("%s: Unknown format\n", path);
	break;
    case E_BADMD5:
	printf("%s: Invalid MD5 format\n", path);
	break;
    case E_BADINF:
	printf("%s: Invalid .inf format\n", path);
	break;
    case E_NAME:
	printf("%s: Can't derive component name\n", path);
	break;
    case E_LENGTH:
	printf("%s: %s: Size mismatch\n", path, name);
	break;
    case E_CHKSUM:
	printf("%s: %s: Checksum mismatch\n", path, name);
	break;
    case E_ERRNO:
	printf("%s: %s: %s\n", path, name, sys_errlist[errno]);
	break;
    default:
	printf("%s: %s: OK\n", path, name);
    }
    return error != 0;
}

static const char *
distname(const char *path, const char *name, const char *ext)
{
    static char buf[NAMESIZE];
    size_t plen, nlen;
    char *s;

    if (opt_name)
	name = opt_name;
    else if (!name) {
	if (!path)
	    return NULL;
	name = stripath(path);
    }
    nlen = strlen(name);
    if (ext && nlen > 4 && name[nlen - 4] == '.' &&
	disttype(name + nlen - 3) == DISTINF)
	nlen -= 4;
    if (opt_dir) {
	path = opt_dir;
	plen = strlen(path);
    } else
	plen = path && (s = strrchr(path, '/')) != NULL ? 
            (size_t)(s - path) : 0;
    if (plen + (plen > 0) + nlen + (ext ? 3 : 0) >= sizeof(buf))
	return NULL;
    s = buf;
    if (plen) {
	memcpy(s, path, plen);
	s += plen;
	*s++ = '/';
    }
    memcpy(s, name, nlen);
    s += nlen;
    if (ext) {
	*s++ = '.';
	memcpy(s, ext, 2);
	s += 2;
    }
    *s = 0;
    return buf;
}

static const char *
stripath(const char *path)
{
    const char *s;

    return ((s = strrchr(path, '/')) != NULL && s[1] ? 
		    s + 1 : path);
}

static int
distfile(const char *path)
{
    const char *s;
    int type;

    if ((type = disttype(path)) == DISTMD5 ||
	((s = strrchr(path, '.')) != NULL && s > path &&
	 (type = disttype(s + 1)) != 0))
	return type;
    return 0;
}

static int
disttype(const char *name)
{
    static const char dname[DISTTYPES][4] = {"md5", "inf"};
    int i;

    for (i = 0; i < DISTTYPES; i++)
	if (!strcmp(dname[i], name))
	    return 1 + i;
    return 0;
}

static int
fail(const char *path, const char *msg)
{
    if (opt_silent)
	return 0;
    warnx("%s: %s", path, msg ? msg : sys_errlist[errno]);
    return 2;
}

static void
usage(void)
{
    fprintf(stderr,
	    "usage: ckdist [-airsx] [-d dir] [-n name] [-t type] file ...\n");
    exit(2);
}
