/* $FreeBSD$ */
/*
 * Copyright (c) 2000 by Matthew Jacob
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * the GNU Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * Matthew Jacob
 * Feral Software
 * mjacob@feral.com
 */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "ses.h"

/*
 * Continuously monitor all named SES devices
 * and turn all but INFO enclosure status
 * values into CRITICAL enclosure status.
 */
#define	BADSTAT	\
	(SES_ENCSTAT_UNRECOV|SES_ENCSTAT_CRITICAL|SES_ENCSTAT_NONCRITICAL)
int
main(int a, char **v)
{
	int fd, delay, dev;
	ses_encstat stat, *carray;

	if (a < 3) {
		fprintf(stderr, "usage: %s polling-interval device "
		    "[ device ... ]\n", *v);
		return (1);
	}
	delay = atoi(v[1]);
	carray = malloc(a);
	if (carray == NULL) {
		perror("malloc");
		return (1);
	}
	bzero((void *)carray, a);

	for (;;) {
		for (dev = 2; dev < a; dev++) {
			fd = open(v[dev], O_RDWR);
			if (fd < 0) {
				perror(v[dev]);
				continue;
			}
			/*
			 * First clear any enclosure status, in case it is
			 * a latched status.
			 */
			stat = 0;
			if (ioctl(fd, SESIOC_SETENCSTAT, (caddr_t) &stat) < 0) {
				fprintf(stderr, "%s: SESIOC_SETENCSTAT1: %s\n",
				    v[dev], strerror(errno));
				(void) close(fd);
				continue;
			}
			/*
			 * Now get the actual current enclosure status.
			 */
			if (ioctl(fd, SESIOC_GETENCSTAT, (caddr_t) &stat) < 0) {
				fprintf(stderr, "%s: SESIOC_GETENCSTAT: %s\n",
				    v[dev], strerror(errno));
				(void) close(fd);
				continue;
			}

			if ((stat & BADSTAT) == 0) {
				if (carray[dev]) {
					fprintf(stdout, "%s: Clearing CRITICAL "
					    "condition\n", v[dev]);
					carray[dev] = 0;
				}
				(void) close(fd);
				continue;
			}
			carray[dev] = 1;
			fprintf(stdout, "%s: Setting CRITICAL from:", v[dev]);
			if (stat & SES_ENCSTAT_UNRECOV)
				fprintf(stdout, " UNRECOVERABLE");
		
			if (stat & SES_ENCSTAT_CRITICAL)
				fprintf(stdout, " CRITICAL");
		
			if (stat & SES_ENCSTAT_NONCRITICAL)
				fprintf(stdout, " NONCRITICAL");
			putchar('\n');
			stat = SES_ENCSTAT_CRITICAL;
			if (ioctl(fd, SESIOC_SETENCSTAT, (caddr_t) &stat) < 0) {
				fprintf(stderr, "%s: SESIOC_SETENCSTAT 2: %s\n",
				    v[dev], strerror(errno));
			}
			(void) close(fd);
		}
		sleep(delay);
	}
	/* NOTREACHED */
}
