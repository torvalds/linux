/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999-2002 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
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
/*
 * Developed by the TrustedBSD Project.
 * Support for file system extended attribute.
 */

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/extattr.h>
#include <sys/param.h>
#include <sys/mount.h>

#include <ufs/ufs/extattr.h>

#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int initattr(int argc, char *argv[]);
int showattr(int argc, char *argv[]);
long num_inodes_by_path(char *path);
void usage(void);

void
usage(void)
{

	fprintf(stderr,
	    "usage:\n"
	    "  extattrctl start path\n"
	    "  extattrctl stop path\n"
	    "  extattrctl initattr [-f] [-p path] attrsize attrfile\n"
	    "  extattrctl showattr attrfile\n"
	    "  extattrctl enable path attrnamespace attrname attrfile\n"
	    "  extattrctl disable path attrnamespace attrname\n");
	exit(-1);
}

long
num_inodes_by_path(char *path)
{
	struct statfs	buf;
	int	error;

	error = statfs(path, &buf);
	if (error) {
		perror("statfs");
		return (-1);
	}

	return (buf.f_files);
}

static const char zero_buf[8192];

int
initattr(int argc, char *argv[])
{
	struct ufs_extattr_fileheader	uef;
	char	*fs_path = NULL;
	int	ch, i, error, flags;
	ssize_t	wlen;
	size_t	easize;

	flags = O_CREAT | O_WRONLY | O_TRUNC | O_EXCL;
	optind = 0;
	while ((ch = getopt(argc, argv, "fp:r:w:")) != -1)
		switch (ch) {
		case 'f':
			flags &= ~O_EXCL;
			break;
		case 'p':
			fs_path = optarg;
			break;
		case '?':
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	error = 0;
	if ((i = open(argv[1], flags, 0600)) == -1) {
		/* unable to open file */
		perror(argv[1]);
		return (-1);
	}
	uef.uef_magic = UFS_EXTATTR_MAGIC;
	uef.uef_version = UFS_EXTATTR_VERSION;
	uef.uef_size = atoi(argv[0]);
	if (write(i, &uef, sizeof(uef)) == -1)
		error = -1;
	else if (fs_path != NULL) {
		easize = (sizeof uef + uef.uef_size) *
		    num_inodes_by_path(fs_path);
		while (easize > 0) {
			if (easize > sizeof zero_buf)
				wlen = write(i, zero_buf, sizeof zero_buf);
			else
				wlen = write(i, zero_buf, easize);
			if (wlen == -1) {
				error = -1;
				break;
			}
			easize -= wlen;
		}
	}
	if (error == -1) {
		perror(argv[1]);
		unlink(argv[1]);
		close(i);
		return (-1);
	}

	close(i);
	return (0);
}

int
showattr(int argc, char *argv[])
{
	struct ufs_extattr_fileheader	uef;
	int i, fd;

	if (argc != 1)
		usage();

	fd = open(argv[0], O_RDONLY);
	if (fd == -1) {
		perror(argv[0]);
		return (-1);
	}

	i = read(fd, &uef, sizeof(uef));
	if (i == -1) {
		perror(argv[0]);
		close(fd);
		return (-1);
	}
	if (i != sizeof(uef)) {
		fprintf(stderr, "%s: invalid file header\n", argv[0]);
		close(fd);
		return (-1);
	}

	if (uef.uef_magic != UFS_EXTATTR_MAGIC) {
		fprintf(stderr, "%s: bad magic\n", argv[0]);
		close(fd);
		return (-1);
	}

	printf("%s: version %d, size %d\n", argv[0], uef.uef_version,
	    uef.uef_size);

	close(fd);
	return (0);
}

int
main(int argc, char *argv[])
{
	int	error = 0, attrnamespace;

	if (argc < 2)
		usage();

	if (!strcmp(argv[1], "start")) {
		if (argc != 3)
			usage();
		error = extattrctl(argv[2], UFS_EXTATTR_CMD_START, NULL, 0,
		    NULL);
		if (error) {
			perror("extattrctl start");
			return (-1);
		}
	} else if (!strcmp(argv[1], "stop")) {
		if (argc != 3)
			usage();
		error = extattrctl(argv[2], UFS_EXTATTR_CMD_STOP, NULL, 0,
		   NULL);
		if (error) {
			perror("extattrctl stop");
			return (-1);
		}
	} else if (!strcmp(argv[1], "enable")) {
		if (argc != 6)
			usage();
		error = extattr_string_to_namespace(argv[3], &attrnamespace);
		if (error) {
			perror("extattrctl enable");
			return (-1);
		}
		error = extattrctl(argv[2], UFS_EXTATTR_CMD_ENABLE, argv[5],
		    attrnamespace, argv[4]);
		if (error) {
			perror("extattrctl enable");
			return (-1);
		}
	} else if (!strcmp(argv[1], "disable")) {
		if (argc != 5)
			usage();
		error = extattr_string_to_namespace(argv[3], &attrnamespace);
		if (error) {
			perror("extattrctl disable");
			return (-1);
		}
		error = extattrctl(argv[2], UFS_EXTATTR_CMD_DISABLE, NULL,
		    attrnamespace, argv[4]);
		if (error) {
			perror("extattrctl disable");
			return (-1);
		}
	} else if (!strcmp(argv[1], "initattr")) {
		argc -= 2;
		argv += 2;
		error = initattr(argc, argv);
		if (error)
			return (-1);
	} else if (!strcmp(argv[1], "showattr")) {
		argc -= 2;
		argv += 2;
		error = showattr(argc, argv);
		if (error)
			return (-1);
	} else
		usage();

	return (0);
}
