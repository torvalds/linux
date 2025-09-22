/*	$OpenBSD: switch.c,v 1.7 2022/12/04 23:50:46 cheloha Exp $	*/
/*
 * Copyright (c) 1993, 1994, 1995, 1996 by Chris Provenzano and contributors,
 * proven@mit.edu All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Chris Provenzano,
 *	the University of California, Berkeley, and contributors.
 * 4. Neither the name of Chris Provenzano, the University, nor the names of
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY CHRIS PROVENZANO AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL CHRIS PROVENZANO, THE REGENTS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* ==== test_switch.c ========================================================
 * Copyright (c) 1993 by Chris Provenzano, proven@athena.mit.edu
 *
 * Description : Test context switch functionality.
 *
 *  1.00 93/08/04 proven
 *      -Started coding this file.
 */

#include <pthread.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>

#include "test.h"

const char buf[] = "abcdefghijklmnopqrstuvwxyz";
char x[sizeof(buf)];

volatile int ending = 0;

/* ==========================================================================
 * usage();
 */
static __dead void
usage(void)
{
	fprintf(stderr, "usage: %s [-c count]\n", getprogname());
	fprintf(stderr, "count is number of theads, between 2 and 26\n");
	exit(1);
}

static void *
new_thread(void *arg)
{
	int i;

	SET_NAME("writer");
	while (!ending) {
		CHECKe(write(STDOUT_FILENO, (char *) arg, 1));
		x[(char *)arg - buf] = 1;
		for (i = 0; i < 999999; i += 1)
			;
	}
	return NULL;
}

int
main(int argc, char *argv[])
{
	pthread_t thread;
	int ch, count = 4;
	long i;
	const char *errstr;

	while ((ch = getopt(argc, argv, "c:")) != -1) {
		switch (ch) {
		case 'c':
			count = strtonum(optarg, 2, 26, &errstr);
			if (errstr != NULL)
				errx(1, "count is %s: %s", errstr, optarg);
			break;
		default:
			usage();
		}
	}

	/* create the threads */
	for (i = 0; i < count; i++)
		CHECKr(pthread_create(&thread, NULL, new_thread,
		    (void*)(buf+i)));

	/* give all threads a chance to run */
	sleep (2);

	ending = 1;
	for (i = 0; i < count; i++)
		ASSERT(x[i]);	/* make sure each thread ran */

	CHECKe(write(STDOUT_FILENO, "\n", 1));
	SUCCEED;
}
