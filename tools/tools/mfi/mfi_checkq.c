/*-
 * Copyright (c) 2007 Scott Long
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
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/uio.h>

#include </sys/dev/mfi/mfireg.h>
#include </sys/dev/mfi/mfi_ioctl.h>

#include <errno.h>
#include <stdio.h>

/*
 * Simple program to print out the queue stats on the given queue index.
 * See /sys/sys/mfi_ioctl.h for the definitions of each queue index.
 */

void
usage(void)
{
	printf("Usage: mfi_checkq <queue_number>\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	union mfi_statrequest sr;
	int fd, retval, queue;

	if (argc != 2)
		usage();

	fd = open("/dev/mfi0", O_RDWR);
	if (fd == -1) {
		printf("couldn't open mfi0: %s\n", strerror(errno));
		return (-1);
	}

	queue = atoi(argv[1]);
	printf("Getting stats for queue %d\n", queue);
	bzero(&sr, sizeof(union mfi_statrequest));
	sr.ms_item = queue;
	retval = ioctl(fd, MFIIO_STATS, &sr);
	if (retval == -1) {
		printf("error on ioctl: %s\n", strerror(errno));
		return (-1);
	}

	printf("length= %d, max= %d\n",sr.ms_qstat.q_length, sr.ms_qstat.q_max);

	close(fd);
	return 0;
}
