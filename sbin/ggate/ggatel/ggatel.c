/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <assert.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/bio.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syslog.h>

#include <geom/gate/g_gate.h>
#include "ggate.h"


static enum { UNSET, CREATE, DESTROY, LIST, RESCUE } action = UNSET;

static const char *path = NULL;
static int unit = G_GATE_UNIT_AUTO;
static unsigned flags = 0;
static int force = 0;
static unsigned sectorsize = 0;
static unsigned timeout = G_GATE_TIMEOUT;

static void
usage(void)
{

	fprintf(stderr, "usage: %s create [-v] [-o <ro|wo|rw>] "
	    "[-s sectorsize] [-t timeout] [-u unit] <path>\n", getprogname());
	fprintf(stderr, "       %s rescue [-v] [-o <ro|wo|rw>] <-u unit> "
	    "<path>\n", getprogname());
	fprintf(stderr, "       %s destroy [-f] <-u unit>\n", getprogname());
	fprintf(stderr, "       %s list [-v] [-u unit]\n", getprogname());
	exit(EXIT_FAILURE);
}

static int
g_gate_openflags(unsigned ggflags)
{

	if ((ggflags & G_GATE_FLAG_READONLY) != 0)
		return (O_RDONLY);
	else if ((ggflags & G_GATE_FLAG_WRITEONLY) != 0)
		return (O_WRONLY);
	return (O_RDWR);
}

static void
g_gatel_serve(int fd)
{
	struct g_gate_ctl_io ggio;
	size_t bsize;

	if (g_gate_verbose == 0) {
		if (daemon(0, 0) == -1) {
			g_gate_destroy(unit, 1);
			err(EXIT_FAILURE, "Cannot daemonize");
		}
	}
	g_gate_log(LOG_DEBUG, "Worker created: %u.", getpid());
	ggio.gctl_version = G_GATE_VERSION;
	ggio.gctl_unit = unit;
	bsize = sectorsize;
	ggio.gctl_data = malloc(bsize);
	for (;;) {
		int error;
once_again:
		ggio.gctl_length = bsize;
		ggio.gctl_error = 0;
		g_gate_ioctl(G_GATE_CMD_START, &ggio);
		error = ggio.gctl_error;
		switch (error) {
		case 0:
			break;
		case ECANCELED:
			/* Exit gracefully. */
			free(ggio.gctl_data);
			g_gate_close_device();
			close(fd);
			exit(EXIT_SUCCESS);
		case ENOMEM:
			/* Buffer too small. */
			assert(ggio.gctl_cmd == BIO_DELETE ||
			    ggio.gctl_cmd == BIO_WRITE);
			ggio.gctl_data = realloc(ggio.gctl_data,
			    ggio.gctl_length);
			if (ggio.gctl_data != NULL) {
				bsize = ggio.gctl_length;
				goto once_again;
			}
			/* FALLTHROUGH */
		case ENXIO:
		default:
			g_gate_xlog("ioctl(/dev/%s): %s.", G_GATE_CTL_NAME,
			    strerror(error));
		}

		error = 0;
		switch (ggio.gctl_cmd) {
		case BIO_READ:
			if ((size_t)ggio.gctl_length > bsize) {
				ggio.gctl_data = realloc(ggio.gctl_data,
				    ggio.gctl_length);
				if (ggio.gctl_data != NULL)
					bsize = ggio.gctl_length;
				else
					error = ENOMEM;
			}
			if (error == 0) {
				if (pread(fd, ggio.gctl_data, ggio.gctl_length,
				    ggio.gctl_offset) == -1) {
					error = errno;
				}
			}
			break;
		case BIO_DELETE:
		case BIO_WRITE:
			if (pwrite(fd, ggio.gctl_data, ggio.gctl_length,
			    ggio.gctl_offset) == -1) {
				error = errno;
			}
			break;
		default:
			error = EOPNOTSUPP;
		}

		ggio.gctl_error = error;
		g_gate_ioctl(G_GATE_CMD_DONE, &ggio);
	}
}

static void
g_gatel_create(void)
{
	struct g_gate_ctl_create ggioc;
	int fd;

	fd = open(path, g_gate_openflags(flags) | O_DIRECT | O_FSYNC);
	if (fd == -1)
		err(EXIT_FAILURE, "Cannot open %s", path);
	memset(&ggioc, 0, sizeof(ggioc));
	ggioc.gctl_version = G_GATE_VERSION;
	ggioc.gctl_unit = unit;
	ggioc.gctl_mediasize = g_gate_mediasize(fd);
	if (sectorsize == 0)
		sectorsize = g_gate_sectorsize(fd);
	ggioc.gctl_sectorsize = sectorsize;
	ggioc.gctl_timeout = timeout;
	ggioc.gctl_flags = flags;
	ggioc.gctl_maxcount = 0;
	strlcpy(ggioc.gctl_info, path, sizeof(ggioc.gctl_info));
	g_gate_ioctl(G_GATE_CMD_CREATE, &ggioc);
	if (unit == -1)
		printf("%s%u\n", G_GATE_PROVIDER_NAME, ggioc.gctl_unit);
	unit = ggioc.gctl_unit;
	g_gatel_serve(fd);
}

static void
g_gatel_rescue(void)
{
	struct g_gate_ctl_cancel ggioc;
	int fd;

	fd = open(path, g_gate_openflags(flags));
	if (fd == -1)
		err(EXIT_FAILURE, "Cannot open %s", path);

	ggioc.gctl_version = G_GATE_VERSION;
	ggioc.gctl_unit = unit;
	ggioc.gctl_seq = 0;
	g_gate_ioctl(G_GATE_CMD_CANCEL, &ggioc);

	g_gatel_serve(fd);
}

int
main(int argc, char *argv[])
{

	if (argc < 2)
		usage();
	if (strcasecmp(argv[1], "create") == 0)
		action = CREATE;
	else if (strcasecmp(argv[1], "rescue") == 0)
		action = RESCUE;
	else if (strcasecmp(argv[1], "destroy") == 0)
		action = DESTROY;
	else if (strcasecmp(argv[1], "list") == 0)
		action = LIST;
	else
		usage();
	argc -= 1;
	argv += 1;
	for (;;) {
		int ch;

		ch = getopt(argc, argv, "fo:s:t:u:v");
		if (ch == -1)
			break;
		switch (ch) {
		case 'f':
			if (action != DESTROY)
				usage();
			force = 1;
			break;
		case 'o':
			if (action != CREATE && action != RESCUE)
				usage();
			if (strcasecmp("ro", optarg) == 0)
				flags = G_GATE_FLAG_READONLY;
			else if (strcasecmp("wo", optarg) == 0)
				flags = G_GATE_FLAG_WRITEONLY;
			else if (strcasecmp("rw", optarg) == 0)
				flags = 0;
			else {
				errx(EXIT_FAILURE,
				    "Invalid argument for '-o' option.");
			}
			break;
		case 's':
			if (action != CREATE)
				usage();
			errno = 0;
			sectorsize = strtoul(optarg, NULL, 10);
			if (sectorsize == 0 && errno != 0)
				errx(EXIT_FAILURE, "Invalid sectorsize.");
			break;
		case 't':
			if (action != CREATE)
				usage();
			errno = 0;
			timeout = strtoul(optarg, NULL, 10);
			if (timeout == 0 && errno != 0)
				errx(EXIT_FAILURE, "Invalid timeout.");
			break;
		case 'u':
			errno = 0;
			unit = strtol(optarg, NULL, 10);
			if (unit == 0 && errno != 0)
				errx(EXIT_FAILURE, "Invalid unit number.");
			break;
		case 'v':
			if (action == DESTROY)
				usage();
			g_gate_verbose++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	switch (action) {
	case CREATE:
		if (argc != 1)
			usage();
		g_gate_load_module();
		g_gate_open_device();
		path = argv[0];
		g_gatel_create();
		break;
	case RESCUE:
		if (argc != 1)
			usage();
		if (unit == -1) {
			fprintf(stderr, "Required unit number.\n");
			usage();
		}
		g_gate_open_device();
		path = argv[0];
		g_gatel_rescue();
		break;
	case DESTROY:
		if (unit == -1) {
			fprintf(stderr, "Required unit number.\n");
			usage();
		}
		g_gate_verbose = 1;
		g_gate_open_device();
		g_gate_destroy(unit, force);
		break;
	case LIST:
		g_gate_list(unit, g_gate_verbose);
		break;
	case UNSET:
	default:
		usage();
	}
	g_gate_close_device();
	exit(EXIT_SUCCESS);
}
