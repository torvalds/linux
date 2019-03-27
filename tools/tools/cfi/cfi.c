/*-
 * Copyright (c) 2009 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

/*
 * cfi [-f device] op
 * (default device is /dev/cfi0).
 */
#include <sys/types.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/cfictl.h>

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *progname;
const char *dvname;

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-f device] op...\n", progname);
	fprintf(stderr, "where op's are:\n");
	fprintf(stderr, "fact\t\tread factory PR segment\n");
	fprintf(stderr, "oem\t\tread OEM segment\n");
	fprintf(stderr, "woem value\twrite OEM segment\n");
	fprintf(stderr, "plr\t\tread PLR\n");
	fprintf(stderr, "wplr\t\twrite PLR\n");
	exit(-1);
}

static int
getfd(int mode)
{
	int fd = open(dvname, mode, 0);
	if (fd < 0)
		err(-1, "open");
	return fd;
}

static uint64_t
getfactorypr(void)
{
	uint64_t v;
	int fd = getfd(O_RDONLY);
	if (ioctl(fd, CFIOCGFACTORYPR, &v) < 0)
		err(-1, "ioctl(CFIOCGFACTORYPR)");
	close(fd);
	return v;
}

static uint64_t
getoempr(void)
{
	uint64_t v;
	int fd = getfd(O_RDONLY);
	if (ioctl(fd, CFIOCGOEMPR, &v) < 0)
		err(-1, "ioctl(CFIOCGOEMPR)");
	close(fd);
	return v;
}

static void
setoempr(uint64_t v)
{
	int fd = getfd(O_WRONLY);
	if (ioctl(fd, CFIOCSOEMPR, &v) < 0)
		err(-1, "ioctl(CFIOCGOEMPR)");
	close(fd);
}

static uint32_t
getplr(void)
{
	uint32_t plr;
	int fd = getfd(O_RDONLY);
	if (ioctl(fd, CFIOCGPLR, &plr) < 0)
		err(-1, "ioctl(CFIOCGPLR)");
	close(fd);
	return plr;
}

static void
setplr(void)
{
	int fd = getfd(O_WRONLY);
	if (ioctl(fd, CFIOCSPLR, 0) < 0)
		err(-1, "ioctl(CFIOCPLR)");
	close(fd);
}

int
main(int argc, char *argv[])
{
	dvname = getenv("CFI");
	if (dvname == NULL)
		dvname = "/dev/cfi0";
	progname = argv[0];
	if (argc > 1) {
		if (strcmp(argv[1], "-f") == 0) {
			if (argc < 2)
				errx(1, "missing device name for -f option");
			dvname = argv[2];
			argc -= 2, argv += 2;
		} else if (strcmp(argv[1], "-?") == 0)
			usage();
	}
	for (; argc > 1; argc--, argv++) {
		if (strcasecmp(argv[1], "fact") == 0) {
			printf("0x%llx\n", (unsigned long long) getfactorypr());
		} else if (strcasecmp(argv[1], "oem") == 0) {
			printf("0x%llx\n", (unsigned long long) getoempr());
		} else if (strcasecmp(argv[1], "woem") == 0) {
			if (argc < 2)
				errx(1, "missing value for woem");
			setoempr((uint64_t) strtoull(argv[2], NULL, 0));
			argc--, argv++;
		} else if (strcasecmp(argv[1], "plr") == 0) {
			printf("0x%x\n", getplr());
		} else if (strcasecmp(argv[1], "wplr") == 0) {
			setplr();
		} else
			usage();
	}
}
