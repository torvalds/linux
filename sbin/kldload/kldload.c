/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Doug Rabson
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/linker.h>
#include <sys/sysctl.h>
#include <sys/stat.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define	PATHCTL	"kern.module_path"

/*
 * Check to see if the requested module is specified as a filename with no
 * path.  If so and if a file by the same name exists in the module path,
 * warn the user that the module in the path will be used in preference.
 */
static int
path_check(const char *kldname, int quiet)
{
	char	*path, *tmppath, *element;
	struct	stat sb;
	int	mib[5];
	char	kldpath[MAXPATHLEN];
	size_t	miblen, pathlen;
	dev_t	dev;
	ino_t	ino;
	int	found;

	if (strchr(kldname, '/') != NULL)
		return (0);
	if (strstr(kldname, ".ko") == NULL)
		return (0);
	if (stat(kldname, &sb) != 0)
		return (0);

	found = 0;
	dev = sb.st_dev;
	ino = sb.st_ino;

	miblen = nitems(mib);
	if (sysctlnametomib(PATHCTL, mib, &miblen) != 0)
		err(1, "sysctlnametomib(%s)", PATHCTL);
	if (sysctl(mib, miblen, NULL, &pathlen, NULL, 0) == -1)
		err(1, "getting path: sysctl(%s) - size only", PATHCTL);
	path = malloc(pathlen + 1);
	if (path == NULL)
		err(1, "allocating %lu bytes for the path",
		    (unsigned long)pathlen + 1);
	if (sysctl(mib, miblen, path, &pathlen, NULL, 0) == -1)
		err(1, "getting path: sysctl(%s)", PATHCTL);
	tmppath = path;

	while ((element = strsep(&tmppath, ";")) != NULL) {
		strlcpy(kldpath, element, MAXPATHLEN);
		if (kldpath[strlen(kldpath) - 1] != '/') {
			strlcat(kldpath, "/", MAXPATHLEN);
		}
		strlcat(kldpath, kldname, MAXPATHLEN);

		if (stat(kldpath, &sb) == -1)
			continue;

		found = 1;

		if (sb.st_dev != dev || sb.st_ino != ino) {
			if (!quiet)
				warnx("%s will be loaded from %s, not the "
				    "current directory", kldname, element);
			break;
		} else if (sb.st_dev == dev && sb.st_ino == ino)
			break;
	}

	free(path);

	if (!found) {
		if (!quiet)
			warnx("%s is not in the module path", kldname);
		return (-1);
	}

	return (0);
}

static void
usage(void)
{

	fprintf(stderr, "usage: kldload [-nqv] file ...\n");
	exit(1);
}

int
main(int argc, char** argv)
{
	int c;
	int check_loaded;
	int errors;
	int fileid;
	int quiet;
	int verbose;

	errors = 0;
	verbose = 0;
	quiet = 0;
	check_loaded = 0;

	while ((c = getopt(argc, argv, "nqv")) != -1) {
		switch (c) {
		case 'q':
			quiet = 1;
			verbose = 0;
			break;
		case 'v':
			verbose = 1;
			quiet = 0;
			break;
		case 'n':
			check_loaded = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		usage();

	while (argc-- != 0) {
		if (path_check(argv[0], quiet) == 0) {
			fileid = kldload(argv[0]);
			if (fileid < 0) {
				if (check_loaded != 0 && errno == EEXIST) {
					if (verbose)
						printf("%s is already "
						    "loaded\n", argv[0]);
				} else {
					switch (errno) {
					case EEXIST:
						warnx("can't load %s: module "
						    "already loaded or "
						    "in kernel", argv[0]);
						break;
					case ENOEXEC:
						warnx("an error occurred while "
						    "loading module %s. "
						    "Please check dmesg(8) for "
						    "more details.", argv[0]);
						break;
					default:
						warn("can't load %s", argv[0]);
						break;
					}
					errors++;
				}
			} else {
				if (verbose)
					printf("Loaded %s, id=%d\n", argv[0],
					    fileid);
			}
		} else
			errors++;
		argv++;
	}

	return (errors ? 1 : 0);
}
