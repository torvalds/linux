/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1992, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#if 0
#ifndef lint
static char sccsid[] = "@(#)compress.c	8.2 (Berkeley) 1/7/94";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "zopen.h"

static void	compress(const char *, const char *, int);
static void	cwarn(const char *, ...) __printflike(1, 2);
static void	cwarnx(const char *, ...) __printflike(1, 2);
static void	decompress(const char *, const char *, int);
static int	permission(const char *);
static void	setfile(const char *, struct stat *);
static void	usage(int);

static int eval, force, verbose;

int
main(int argc, char *argv[])
{
	enum {COMPRESS, DECOMPRESS} style;
	size_t len;
	int bits, cat, ch;
	char *p, newname[MAXPATHLEN];

	cat = 0;
	if ((p = strrchr(argv[0], '/')) == NULL)
		p = argv[0];
	else
		++p;
	if (!strcmp(p, "uncompress"))
		style = DECOMPRESS;
	else if (!strcmp(p, "compress"))
		style = COMPRESS;
	else if (!strcmp(p, "zcat")) {
		cat = 1;
		style = DECOMPRESS;
	} else
		errx(1, "unknown program name");

	bits = 0;
	while ((ch = getopt(argc, argv, "b:cdfv")) != -1)
		switch(ch) {
		case 'b':
			bits = strtol(optarg, &p, 10);
			if (*p)
				errx(1, "illegal bit count -- %s", optarg);
			break;
		case 'c':
			cat = 1;
			break;
		case 'd':		/* Backward compatible. */
			style = DECOMPRESS;
			break;
		case 'f':
			force = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case '?':
		default:
			usage(style == COMPRESS);
		}
	argc -= optind;
	argv += optind;

	if (argc == 0) {
		switch(style) {
		case COMPRESS:
			(void)compress("/dev/stdin", "/dev/stdout", bits);
			break;
		case DECOMPRESS:
			(void)decompress("/dev/stdin", "/dev/stdout", bits);
			break;
		}
		exit (eval);
	}

	if (cat == 1 && style == COMPRESS && argc > 1)
		errx(1, "the -c option permits only a single file argument");

	for (; *argv; ++argv)
		switch(style) {
		case COMPRESS:
			if (strcmp(*argv, "-") == 0) {
				compress("/dev/stdin", "/dev/stdout", bits);
				break;
			} else if (cat) {
				compress(*argv, "/dev/stdout", bits);
				break;
			}
			if ((p = strrchr(*argv, '.')) != NULL &&
			    !strcmp(p, ".Z")) {
				cwarnx("%s: name already has trailing .Z",
				    *argv);
				break;
			}
			len = strlen(*argv);
			if (len > sizeof(newname) - 3) {
				cwarnx("%s: name too long", *argv);
				break;
			}
			memmove(newname, *argv, len);
			newname[len] = '.';
			newname[len + 1] = 'Z';
			newname[len + 2] = '\0';
			compress(*argv, newname, bits);
			break;
		case DECOMPRESS:
			if (strcmp(*argv, "-") == 0) {
				decompress("/dev/stdin", "/dev/stdout", bits);
				break;
			}
			len = strlen(*argv);
			if ((p = strrchr(*argv, '.')) == NULL ||
			    strcmp(p, ".Z")) {
				if (len > sizeof(newname) - 3) {
					cwarnx("%s: name too long", *argv);
					break;
				}
				memmove(newname, *argv, len);
				newname[len] = '.';
				newname[len + 1] = 'Z';
				newname[len + 2] = '\0';
				decompress(newname,
				    cat ? "/dev/stdout" : *argv, bits);
			} else {
				if (len - 2 > sizeof(newname) - 1) {
					cwarnx("%s: name too long", *argv);
					break;
				}
				memmove(newname, *argv, len - 2);
				newname[len - 2] = '\0';
				decompress(*argv,
				    cat ? "/dev/stdout" : newname, bits);
			}
			break;
		}
	exit (eval);
}

static void
compress(const char *in, const char *out, int bits)
{
	size_t nr;
	struct stat isb, sb;
	FILE *ifp, *ofp;
	int exists, isreg, oreg;
	u_char buf[1024];

	exists = !stat(out, &sb);
	if (!force && exists && S_ISREG(sb.st_mode) && !permission(out))
		return;
	isreg = oreg = !exists || S_ISREG(sb.st_mode);

	ifp = ofp = NULL;
	if ((ifp = fopen(in, "r")) == NULL) {
		cwarn("%s", in);
		return;
	}
	if (stat(in, &isb)) {		/* DON'T FSTAT! */
		cwarn("%s", in);
		goto err;
	}
	if (!S_ISREG(isb.st_mode))
		isreg = 0;

	if ((ofp = zopen(out, "w", bits)) == NULL) {
		cwarn("%s", out);
		goto err;
	}
	while ((nr = fread(buf, 1, sizeof(buf), ifp)) != 0)
		if (fwrite(buf, 1, nr, ofp) != nr) {
			cwarn("%s", out);
			goto err;
		}

	if (ferror(ifp) || fclose(ifp)) {
		cwarn("%s", in);
		goto err;
	}
	ifp = NULL;

	if (fclose(ofp)) {
		cwarn("%s", out);
		goto err;
	}
	ofp = NULL;

	if (isreg) {
		if (stat(out, &sb)) {
			cwarn("%s", out);
			goto err;
		}

		if (!force && sb.st_size >= isb.st_size) {
			if (verbose)
		(void)fprintf(stderr, "%s: file would grow; left unmodified\n",
		    in);
			eval = 2;
			if (unlink(out))
				cwarn("%s", out);
			goto err;
		}

		setfile(out, &isb);

		if (unlink(in))
			cwarn("%s", in);

		if (verbose) {
			(void)fprintf(stderr, "%s: ", out);
			if (isb.st_size > sb.st_size)
				(void)fprintf(stderr, "%.0f%% compression\n",
				    ((float)sb.st_size / isb.st_size) * 100.0);
			else
				(void)fprintf(stderr, "%.0f%% expansion\n",
				    ((float)isb.st_size / sb.st_size) * 100.0);
		}
	}
	return;

err:	if (ofp) {
		if (oreg)
			(void)unlink(out);
		(void)fclose(ofp);
	}
	if (ifp)
		(void)fclose(ifp);
}

static void
decompress(const char *in, const char *out, int bits)
{
	size_t nr;
	struct stat sb;
	FILE *ifp, *ofp;
	int exists, isreg, oreg;
	u_char buf[1024];

	exists = !stat(out, &sb);
	if (!force && exists && S_ISREG(sb.st_mode) && !permission(out))
		return;
	isreg = oreg = !exists || S_ISREG(sb.st_mode);

	ifp = ofp = NULL;
	if ((ifp = zopen(in, "r", bits)) == NULL) {
		cwarn("%s", in);
		return;
	}
	if (stat(in, &sb)) {
		cwarn("%s", in);
		goto err;
	}
	if (!S_ISREG(sb.st_mode))
		isreg = 0;

	/*
	 * Try to read the first few uncompressed bytes from the input file
	 * before blindly truncating the output file.
	 */
	if ((nr = fread(buf, 1, sizeof(buf), ifp)) == 0) {
		cwarn("%s", in);
		(void)fclose(ifp);
		return;
	}
	if ((ofp = fopen(out, "w")) == NULL ||
	    (nr != 0 && fwrite(buf, 1, nr, ofp) != nr)) {
		cwarn("%s", out);
		if (ofp)
			(void)fclose(ofp);
		(void)fclose(ifp);
		return;
	}

	while ((nr = fread(buf, 1, sizeof(buf), ifp)) != 0)
		if (fwrite(buf, 1, nr, ofp) != nr) {
			cwarn("%s", out);
			goto err;
		}

	if (ferror(ifp) || fclose(ifp)) {
		cwarn("%s", in);
		goto err;
	}
	ifp = NULL;

	if (fclose(ofp)) {
		cwarn("%s", out);
		goto err;
	}

	if (isreg) {
		setfile(out, &sb);

		if (unlink(in))
			cwarn("%s", in);
	}
	return;

err:	if (ofp) {
		if (oreg)
			(void)unlink(out);
		(void)fclose(ofp);
	}
	if (ifp)
		(void)fclose(ifp);
}

static void
setfile(const char *name, struct stat *fs)
{
	static struct timespec tspec[2];

	fs->st_mode &= S_ISUID|S_ISGID|S_IRWXU|S_IRWXG|S_IRWXO;

	tspec[0] = fs->st_atim;
	tspec[1] = fs->st_mtim;
	if (utimensat(AT_FDCWD, name, tspec, 0))
		cwarn("utimensat: %s", name);

	/*
	 * Changing the ownership probably won't succeed, unless we're root
	 * or POSIX_CHOWN_RESTRICTED is not set.  Set uid/gid before setting
	 * the mode; current BSD behavior is to remove all setuid bits on
	 * chown.  If chown fails, lose setuid/setgid bits.
	 */
	if (chown(name, fs->st_uid, fs->st_gid)) {
		if (errno != EPERM)
			cwarn("chown: %s", name);
		fs->st_mode &= ~(S_ISUID|S_ISGID);
	}
	if (chmod(name, fs->st_mode) && errno != EOPNOTSUPP)
		cwarn("chmod: %s", name);

	if (chflags(name, fs->st_flags) && errno != EOPNOTSUPP)
		cwarn("chflags: %s", name);
}

static int
permission(const char *fname)
{
	int ch, first;

	if (!isatty(fileno(stderr)))
		return (0);
	(void)fprintf(stderr, "overwrite %s? ", fname);
	first = ch = getchar();
	while (ch != '\n' && ch != EOF)
		ch = getchar();
	return (first == 'y');
}

static void
usage(int iscompress)
{
	if (iscompress)
		(void)fprintf(stderr,
		    "usage: compress [-cfv] [-b bits] [file ...]\n");
	else
		(void)fprintf(stderr,
		    "usage: uncompress [-c] [-b bits] [file ...]\n");
	exit(1);
}

static void
cwarnx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarnx(fmt, ap);
	va_end(ap);
	eval = 1;
}

static void
cwarn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarn(fmt, ap);
	va_end(ap);
	eval = 1;
}
