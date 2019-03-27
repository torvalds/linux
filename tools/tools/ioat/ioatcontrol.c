/*-
 * Copyright (C) 2012 Intel Corporation
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

#include <sys/ioctl.h>
#include <sys/queue.h>

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <libutil.h>

#include "ioat_test.h"

static int prettyprint(struct ioat_test *);

static void
usage(void)
{

	printf("Usage: %s [-c period] [-EfmVz] channel-number num-txns [<bufsize> "
	    "[<chain-len> [duration]]]\n", getprogname());
	printf("       %s -r [-c period] [-vVwz] channel-number address [<bufsize>]\n\n",
	    getprogname());
	printf("           -c period - Enable interrupt coalescing (us) (default: 0)\n");
	printf("           -E        - Test non-contiguous 8k copy.\n");
	printf("           -f        - Test block fill (default: DMA copy).\n");
	printf("           -m        - Test memcpy instead of DMA.\n");
	printf("           -r        - Issue DMA to or from a specific address.\n");
	printf("           -V        - Enable verification\n");
	printf("           -v        - <address> is a kernel virtual address\n");
	printf("           -w        - Write to the specified address\n");
	printf("           -z        - Zero device stats before test\n");
	exit(EX_USAGE);
}

static void
main_raw(struct ioat_test *t, int argc, char **argv)
{
	int fd;

	/* Raw DMA defaults */
	t->testkind = IOAT_TEST_RAW_DMA;
	t->transactions = 1;
	t->chain_depth = 1;
	t->buffer_size = 4 * 1024;

	t->raw_target = strtoull(argv[1], NULL, 0);
	if (t->raw_target == 0) {
		printf("Target shoudln't be NULL\n");
		exit(EX_USAGE);
	}

	if (argc >= 3) {
		t->buffer_size = atoi(argv[2]);
		if (t->buffer_size == 0) {
			printf("Buffer size must be greater than zero\n");
			exit(EX_USAGE);
		}
	}

	fd = open("/dev/ioat_test", O_RDWR);
	if (fd < 0) {
		printf("Cannot open /dev/ioat_test\n");
		exit(EX_UNAVAILABLE);
	}

	(void)ioctl(fd, IOAT_DMATEST, t);
	close(fd);

	exit(prettyprint(t));
}

int
main(int argc, char **argv)
{
	struct ioat_test t;
	int fd, ch;
	bool fflag, rflag, Eflag, mflag;
	unsigned modeflags;

	memset(&t, 0, sizeof(t));

	fflag = rflag = Eflag = mflag = false;
	modeflags = 0;

	while ((ch = getopt(argc, argv, "c:EfmrvVwz")) != -1) {
		switch (ch) {
		case 'c':
			t.coalesce_period = atoi(optarg);
			break;
		case 'E':
			Eflag = true;
			modeflags++;
			break;
		case 'f':
			fflag = true;
			modeflags++;
			break;
		case 'm':
			mflag = true;
			modeflags++;
			break;
		case 'r':
			rflag = true;
			modeflags++;
			break;
		case 'v':
			t.raw_is_virtual = true;
			break;
		case 'V':
			t.verify = true;
			break;
		case 'w':
			t.raw_write = true;
			break;
		case 'z':
			t.zero_stats = true;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 2)
		usage();

	if (modeflags > 1) {
		printf("Invalid: Cannot use >1 mode flag (-E, -f, -m, or -r)\n");
		usage();
	}

	/* Defaults for optional args */
	t.buffer_size = 256 * 1024;
	t.chain_depth = 2;
	t.duration = 0;
	t.testkind = IOAT_TEST_DMA;

	if (fflag)
		t.testkind = IOAT_TEST_FILL;
	else if (Eflag) {
		t.testkind = IOAT_TEST_DMA_8K;
		t.buffer_size = 8 * 1024;
	} else if (mflag)
		t.testkind = IOAT_TEST_MEMCPY;

	t.channel_index = atoi(argv[0]);
	if (t.channel_index > 8) {
		printf("Channel number must be between 0 and 7.\n");
		return (EX_USAGE);
	}

	if (rflag) {
		main_raw(&t, argc, argv);
		return (EX_OK);
	}

	t.transactions = atoi(argv[1]);

	if (argc >= 3) {
		t.buffer_size = atoi(argv[2]);
		if (t.buffer_size == 0) {
			printf("Buffer size must be greater than zero\n");
			return (EX_USAGE);
		}
	}

	if (argc >= 4) {
		t.chain_depth = atoi(argv[3]);
		if (t.chain_depth < 1) {
			printf("Chain length must be greater than zero\n");
			return (EX_USAGE);
		}
	}

	if (argc >= 5) {
		t.duration = atoi(argv[4]);
		if (t.duration < 1) {
			printf("Duration must be greater than zero\n");
			return (EX_USAGE);
		}
	}

	fd = open("/dev/ioat_test", O_RDWR);
	if (fd < 0) {
		printf("Cannot open /dev/ioat_test\n");
		return (EX_UNAVAILABLE);
	}

	(void)ioctl(fd, IOAT_DMATEST, &t);
	close(fd);

	return (prettyprint(&t));
}

static int
prettyprint(struct ioat_test *t)
{
	char bps[10], bytesh[10];
	uintmax_t bytes;

	if (t->status[IOAT_TEST_NO_DMA_ENGINE] != 0 ||
	    t->status[IOAT_TEST_NO_MEMORY] != 0 ||
	    t->status[IOAT_TEST_MISCOMPARE] != 0) {
		printf("Errors:\n");
		if (t->status[IOAT_TEST_NO_DMA_ENGINE] != 0)
			printf("\tNo DMA engine present: %u\n",
			    (unsigned)t->status[IOAT_TEST_NO_DMA_ENGINE]);
		if (t->status[IOAT_TEST_NO_MEMORY] != 0)
			printf("\tOut of memory: %u\n",
			    (unsigned)t->status[IOAT_TEST_NO_MEMORY]);
		if (t->status[IOAT_TEST_MISCOMPARE] != 0)
			printf("\tMiscompares: %u\n",
			    (unsigned)t->status[IOAT_TEST_MISCOMPARE]);
	}

	printf("Processed %u txns\n", (unsigned)t->status[IOAT_TEST_OK] /
	    t->chain_depth);
	bytes = (uintmax_t)t->buffer_size * t->status[IOAT_TEST_OK];

	humanize_number(bytesh, sizeof(bytesh), (int64_t)bytes, "B",
	    HN_AUTOSCALE, HN_DECIMAL);
	if (t->duration) {
		humanize_number(bps, sizeof(bps),
		    (int64_t)1000 * bytes / t->duration, "B/s", HN_AUTOSCALE,
		    HN_DECIMAL);
		printf("%ju (%s) copied in %u ms (%s)\n", bytes, bytesh,
		    (unsigned)t->duration, bps);
	} else
		printf("%ju (%s) copied\n", bytes, bytesh);

	return (EX_OK);
}
