/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019 Eugene Grosbein <eugen@FreeBSD.org>.
 * Contains code written by Alan Somers <asomers@FreeBSD.org>.
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
 */

#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <limits.h>
#include <paths.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

static bool	candelete(int fd);
static off_t	getsize(const char *path);
static int	opendev(const char *path, int flags);
static int	trim(const char *path, off_t offset, off_t length, bool dryrun, bool verbose);
static void	usage(const char *name);

int
main(int argc, char **argv)
{
	off_t offset, length;
	uint64_t usz;
	int ch, error;
	bool dryrun, verbose;
	char *fname, *name;

	error = 0;
	length = offset = 0;
	name = argv[0];
	dryrun = verbose = true;

	while ((ch = getopt(argc, argv, "Nfl:o:qr:v")) != -1)
		switch (ch) {
		case 'N':
			dryrun = true;
			verbose = true;
			break;
		case 'f':
			dryrun = false;
			break;
		case 'l':
		case 'o':
			if (expand_number(optarg, &usz) == -1 ||
					(off_t)usz < 0 || (usz == 0 && ch == 'l'))
				errx(EX_USAGE,
					"invalid %s of the region: %s",
					ch == 'o' ? "offset" : "length",
					optarg);
			if (ch == 'o')
				offset = (off_t)usz;
			else
				length = (off_t)usz;
			break;
		case 'q':
			verbose = false;
			break;
		case 'r':
			if ((length = getsize(optarg)) == 0)
				errx(EX_USAGE,
					"invalid zero length reference file"
					" for the region: %s", optarg);
			break;
		case 'v':
			verbose = true;
			break;
		default:
			usage(name);
			/* NOTREACHED */
		}

	/*
	 * Safety net: do not allow mistakes like
	 *
	 *	trim -f /dev/da0 -r rfile
	 *
	 * This would trim whole device then error on non-existing file -r.
	 * Following check prevents this while allowing this form still:
	 *
	 *	trim -f -- /dev/da0 -r rfile
	 */
	
	if (strcmp(argv[optind-1], "--") != 0) {
		for (ch = optind; ch < argc; ch++)
			if (argv[ch][0] == '-')
				usage(name);
	}

	argv += optind;
	argc -= optind;

	if (argc < 1)
		usage(name);

	while ((fname = *argv++) != NULL)
		if (trim(fname, offset, length, dryrun, verbose) < 0)
			error++;

	return (error ? EXIT_FAILURE : EXIT_SUCCESS);
}

static bool
candelete(int fd)
{
	struct diocgattr_arg arg;

	strlcpy(arg.name, "GEOM::candelete", sizeof(arg.name));
	arg.len = sizeof(arg.value.i);
	if (ioctl(fd, DIOCGATTR, &arg) == 0)
		return (arg.value.i != 0);
	else
		return (false);
}

static int
opendev(const char *path, int flags)
{
	int fd;
	char *tstr;

	if ((fd = open(path, flags)) < 0) {
		if (errno == ENOENT && path[0] != '/') {
			if (asprintf(&tstr, "%s%s", _PATH_DEV, path) < 0)
				errx(EX_OSERR, "no memory");
			fd = open(tstr, flags);
			free(tstr);
		}
	}

	if (fd < 0)
		err(EX_NOINPUT, "open failed: %s", path);

	return (fd);
}

static off_t
getsize(const char *path)
{
	struct stat sb;
	off_t mediasize;
	int fd;

	fd = opendev(path, O_RDONLY | O_DIRECT);

	if (fstat(fd, &sb) < 0)
		err(EX_IOERR, "fstat failed: %s", path);

	if (S_ISREG(sb.st_mode) || S_ISDIR(sb.st_mode)) {
		close(fd);
		return (sb.st_size);
	}

	if (!S_ISCHR(sb.st_mode) && !S_ISBLK(sb.st_mode))
		errx(EX_DATAERR,
			"invalid type of the file "
			"(not regular, directory nor special device): %s",
			path);

	if (ioctl(fd, DIOCGMEDIASIZE, &mediasize) < 0)
		err(EX_UNAVAILABLE,
			"ioctl(DIOCGMEDIASIZE) failed, probably not a disk: "
			"%s", path);

	close(fd);
	return (mediasize);
}

static int
trim(const char *path, off_t offset, off_t length, bool dryrun, bool verbose)
{
	off_t arg[2];
	int error, fd;

	if (length == 0)
		length = getsize(path);

	if (verbose)
		printf("trim %s offset %ju length %ju\n",
		    path, (uintmax_t)offset, (uintmax_t)length);

	if (dryrun) {
		printf("dry run: add -f to actually perform the operation\n");
		return (0);
	}

	fd = opendev(path, O_WRONLY | O_DIRECT);
	arg[0] = offset;
	arg[1] = length;

	error = ioctl(fd, DIOCGDELETE, arg);
	if (error < 0) {
		if (errno == EOPNOTSUPP && verbose && !candelete(fd))
			warnx("%s: TRIM/UNMAP not supported by driver", path);
		else
			warn("ioctl(DIOCGDELETE) failed: %s", path);
	}
	close(fd);
	return (error);
}

static void
usage(const char *name)
{
	(void)fprintf(stderr,
	    "usage: %s [-[lo] offset[K|k|M|m|G|g|T|t]] [-r rfile] [-Nfqv] device ...\n",
	    name);
	exit(EX_USAGE);
}
