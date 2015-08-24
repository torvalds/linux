/******************************************************************************
 *
 * Copyright FUJITSU LIMITED 2010
 * Copyright KOSAKI Motohiro <kosaki.motohiro@jp.fujitsu.com>
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 * DESCRIPTION
 *      Wait on uninitialized heap. It shold be zero and FUTEX_WAIT should
 *      return immediately. This test is intent to test zero page handling in
 *      futex.
 *
 * AUTHOR
 *      KOSAKI Motohiro <kosaki.motohiro@jp.fujitsu.com>
 *
 * HISTORY
 *      2010-Jan-6: Initial version by KOSAKI Motohiro <kosaki.motohiro@jp.fujitsu.com>
 *
 *****************************************************************************/

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <linux/futex.h>
#include <libgen.h>

#include "logging.h"
#include "futextest.h"

#define WAIT_US 5000000

static int child_blocked = 1;
static int child_ret;
void *buf;

void usage(char *prog)
{
	printf("Usage: %s\n", prog);
	printf("  -c	Use color\n");
	printf("  -h	Display this help message\n");
	printf("  -v L	Verbosity level: %d=QUIET %d=CRITICAL %d=INFO\n",
	       VQUIET, VCRITICAL, VINFO);
}

void *wait_thread(void *arg)
{
	int res;

	child_ret = RET_PASS;
	res = futex_wait(buf, 1, NULL, 0);
	child_blocked = 0;

	if (res != 0 && errno != EWOULDBLOCK) {
		error("futex failure\n", errno);
		child_ret = RET_ERROR;
	}
	pthread_exit(NULL);
}

int main(int argc, char **argv)
{
	int c, ret = RET_PASS;
	long page_size;
	pthread_t thr;

	while ((c = getopt(argc, argv, "chv:")) != -1) {
		switch (c) {
		case 'c':
			log_color(1);
			break;
		case 'h':
			usage(basename(argv[0]));
			exit(0);
		case 'v':
			log_verbosity(atoi(optarg));
			break;
		default:
			usage(basename(argv[0]));
			exit(1);
		}
	}

	page_size = sysconf(_SC_PAGESIZE);

	buf = mmap(NULL, page_size, PROT_READ|PROT_WRITE,
		   MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
	if (buf == (void *)-1) {
		error("mmap\n", errno);
		exit(1);
	}

	printf("%s: Test the uninitialized futex value in FUTEX_WAIT\n",
	       basename(argv[0]));


	ret = pthread_create(&thr, NULL, wait_thread, NULL);
	if (ret) {
		error("pthread_create\n", errno);
		ret = RET_ERROR;
		goto out;
	}

	info("waiting %dus for child to return\n", WAIT_US);
	usleep(WAIT_US);

	ret = child_ret;
	if (child_blocked) {
		fail("child blocked in kernel\n");
		ret = RET_FAIL;
	}

 out:
	print_result(ret);
	return ret;
}
