/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2012-2013 Intel Corporation
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

#include <sys/param.h>
#include <sys/ioccom.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nvmecontrol.h"

#define PERFTEST_USAGE							       \
	"perftest <-n num_threads> <-o read|write>\n"	       \
	"         <-s size_in_bytes> <-t time_in_seconds>\n"	       \
	"         <-i intr|wait> [-f refthread] [-p]\n"	       \
	"         <namespace id>\n"

static void
print_perftest(struct nvme_io_test *io_test, bool perthread)
{
	uint64_t	io_completed = 0, iops, mbps;
	uint32_t	i;

	for (i = 0; i < io_test->num_threads; i++)
		io_completed += io_test->io_completed[i];

	iops = io_completed/io_test->time;
	mbps = iops * io_test->size / (1024*1024);

	printf("Threads: %2d Size: %6d %5s Time: %3d IO/s: %7ju MB/s: %4ju\n",
	    io_test->num_threads, io_test->size,
	    io_test->opc == NVME_OPC_READ ? "READ" : "WRITE",
	    io_test->time, (uintmax_t)iops, (uintmax_t)mbps);

	if (perthread)
		for (i = 0; i < io_test->num_threads; i++)
			printf("\t%3d: %8ju IO/s\n", i,
			    (uintmax_t)io_test->io_completed[i]/io_test->time);
}

static void
perftest(const struct nvme_function *nf, int argc, char *argv[])
{
	struct nvme_io_test		io_test;
	int				fd;
	int				opt;
	char				*p;
	u_long				ioctl_cmd = NVME_IO_TEST;
	bool				nflag, oflag, sflag, tflag;
	int				perthread = 0;

	nflag = oflag = sflag = tflag = false;

	memset(&io_test, 0, sizeof(io_test));

	while ((opt = getopt(argc, argv, "f:i:n:o:ps:t:")) != -1) {
		switch (opt) {
		case 'f':
			if (!strcmp(optarg, "refthread"))
				io_test.flags |= NVME_TEST_FLAG_REFTHREAD;
			break;
		case 'i':
			if (!strcmp(optarg, "bio") ||
			    !strcmp(optarg, "wait"))
				ioctl_cmd = NVME_BIO_TEST;
			else if (!strcmp(optarg, "io") ||
				 !strcmp(optarg, "intr"))
				ioctl_cmd = NVME_IO_TEST;
			break;
		case 'n':
			nflag = true;
			io_test.num_threads = strtoul(optarg, &p, 0);
			if (p != NULL && *p != '\0') {
				fprintf(stderr,
				    "\"%s\" not valid number of threads.\n",
				    optarg);
				usage(nf);
			} else if (io_test.num_threads == 0 ||
				   io_test.num_threads > 128) {
				fprintf(stderr,
				    "\"%s\" not valid number of threads.\n",
				    optarg);
				usage(nf);
			}
			break;
		case 'o':
			oflag = true;
			if (!strcmp(optarg, "read") || !strcmp(optarg, "READ"))
				io_test.opc = NVME_OPC_READ;
			else if (!strcmp(optarg, "write") ||
				 !strcmp(optarg, "WRITE"))
				io_test.opc = NVME_OPC_WRITE;
			else {
				fprintf(stderr, "\"%s\" not valid opcode.\n",
				    optarg);
				usage(nf);
			}
			break;
		case 'p':
			perthread = 1;
			break;
		case 's':
			sflag = true;
			io_test.size = strtoul(optarg, &p, 0);
			if (p == NULL || *p == '\0' || toupper(*p) == 'B') {
				// do nothing
			} else if (toupper(*p) == 'K') {
				io_test.size *= 1024;
			} else if (toupper(*p) == 'M') {
				io_test.size *= 1024 * 1024;
			} else {
				fprintf(stderr, "\"%s\" not valid size.\n",
				    optarg);
				usage(nf);
			}
			break;
		case 't':
			tflag = true;
			io_test.time = strtoul(optarg, &p, 0);
			if (p != NULL && *p != '\0') {
				fprintf(stderr,
				    "\"%s\" not valid time duration.\n",
				    optarg);
				usage(nf);
			}
			break;
		}
	}

	if (!nflag || !oflag || !sflag || !tflag || optind >= argc)
		usage(nf);


	open_dev(argv[optind], &fd, 1, 1);
	if (ioctl(fd, ioctl_cmd, &io_test) < 0)
		err(1, "ioctl NVME_IO_TEST failed");

	close(fd);
	print_perftest(&io_test, perthread);
	exit(0);
}

NVME_COMMAND(top, perftest, perftest, PERFTEST_USAGE);
