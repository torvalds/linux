/*-
 * Copyright (C) 2005 IronPort Systems, Inc. All rights reserved.
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
 * Prerequisities:
 * - AIO support must be compiled into the kernel (see sys/<arch>/NOTES for
 *   more details).
 *
 * Note: it is a good idea to run this against a physical drive to 
 * exercise the physio fast path (ie. aio_kqueue /dev/<something safe>)
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <aio.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "freebsd_test_suite/macros.h"
#include "local.h"

#define PATH_TEMPLATE   "aio.XXXXXXXXXX"

#define MAX_RUNS 300
/* #define DEBUG */

int
main (int argc, char *argv[])
{
	struct aiocb **iocb, *kq_iocb;
	char *file, pathname[sizeof(PATH_TEMPLATE)+1];
	struct kevent ke, kq_returned;
	struct timespec ts;
	char buffer[32768];
	int max_queue_per_proc;
	size_t max_queue_per_proc_size;
#ifdef DEBUG
	int cancel, error;
#endif
	int failed = 0, fd, kq, pending, result, run;
	int tmp_file = 0;
	int i, j;

	PLAIN_REQUIRE_KERNEL_MODULE("aio", 0);
	PLAIN_REQUIRE_UNSAFE_AIO(0);

	max_queue_per_proc_size = sizeof(max_queue_per_proc);
	if (sysctlbyname("vfs.aio.max_aio_queue_per_proc",
	    &max_queue_per_proc, &max_queue_per_proc_size, NULL, 0) != 0)
		err(1, "sysctlbyname");
	iocb = calloc(max_queue_per_proc, sizeof(struct aiocb*));
	if (iocb == NULL)
		err(1, "calloc");

	kq = kqueue();
	if (kq < 0) {
		perror("No kqeueue\n");
		exit(1);
	}

	if (argc == 1) { 
		strcpy(pathname, PATH_TEMPLATE);
		fd = mkstemp(pathname);
		file = pathname;
		tmp_file = 1;
	} else {
		file = argv[1];
		fd = open(file, O_RDWR|O_CREAT, 0666);
	}
	if (fd == -1)
		err(1, "Can't open %s\n", file);

	for (run = 0; run < MAX_RUNS; run++){
#ifdef DEBUG
		printf("Run %d\n", run);
#endif
		for (i = 0; i < max_queue_per_proc; i++) {
			iocb[i] = (struct aiocb *)calloc(1,
			    sizeof(struct aiocb));
			if (iocb[i] == NULL)
				err(1, "calloc");
		}

		pending = 0;
		for (i = 0; i < max_queue_per_proc; i++) {
			pending++;
			iocb[i]->aio_nbytes = sizeof(buffer);
			iocb[i]->aio_buf = buffer;
			iocb[i]->aio_fildes = fd;
			iocb[i]->aio_offset = iocb[i]->aio_nbytes * i * run;

			iocb[i]->aio_sigevent.sigev_notify_kqueue = kq;
			iocb[i]->aio_sigevent.sigev_value.sival_ptr = iocb[i];
			iocb[i]->aio_sigevent.sigev_notify = SIGEV_KEVENT;

			result = aio_write(iocb[i]);
			if (result != 0) {
				perror("aio_write");
				printf("Result %d iteration %d\n", result, i);
				exit(1);
			}
#ifdef DEBUG
			printf("WRITE %d is at %p\n", i, iocb[i]);
#endif
			result = rand();
			if (result < RAND_MAX/32) {
				if (result > RAND_MAX/64) {
					result = aio_cancel(fd, iocb[i]);
#ifdef DEBUG
					printf("Cancel %d %p result %d\n", i, iocb[i], result);
#endif
					if (result == AIO_CANCELED) {
						aio_return(iocb[i]);
						iocb[i] = NULL;
						pending--;
					}
				}
			}
		}
#ifdef DEBUG
		cancel = max_queue_per_proc - pending;
#endif

		i = 0;
		while (pending) {

			for (;;) {

				bzero(&ke, sizeof(ke));
				bzero(&kq_returned, sizeof(ke));
				ts.tv_sec = 0;
				ts.tv_nsec = 1;
				result = kevent(kq, NULL, 0,
						&kq_returned, 1, &ts);
#ifdef DEBUG
				error = errno;
#endif
				if (result < 0)
					perror("kevent error: ");
				kq_iocb = kq_returned.udata;
#ifdef DEBUG
				printf("kevent %d %d errno %d return.ident %p "
				       "return.data %p return.udata %p %p"
				       " filter %d flags %#x fflags %#x\n",
				       i, result, error,
				       (void*)kq_returned.ident,
				       (void*)kq_returned.data,
				       kq_returned.udata,
				       kq_iocb,
				       kq_returned.filter,
				       kq_returned.flags,
				       kq_returned.fflags);
				if (result > 0)
					printf("\tsigev_notify_kevent_flags %#x\n",
				       ((struct aiocb*)(kq_returned.ident))->aio_sigevent.sigev_notify_kevent_flags);
#endif

				if (kq_iocb)
					break;
#ifdef DEBUG
				printf("Try again left %d out of %d %d\n",
				    pending, max_queue_per_proc, cancel);
#endif
			}

			for (j = 0; j < max_queue_per_proc && iocb[j] != kq_iocb;
			   j++) ;
#ifdef DEBUG
			printf("kq_iocb %p\n", kq_iocb);

			printf("Error Result for %d is %d pending %d\n",
			    j, result, pending);
#endif
			result = aio_return(kq_iocb);
#ifdef DEBUG
			printf("Return Result for %d is %d\n\n", j, result);
#endif
			if (result != sizeof(buffer)) {
				printf("FAIL: run %d, operation %d, result %d "
				    " (errno=%d) should be %zu\n", run, pending,
				    result, errno, sizeof(buffer));
				failed++;
			} else
				printf("PASS: run %d, left %d\n", run,
				    pending - 1);

			free(kq_iocb);
			iocb[j] = NULL;
			pending--;
			i++;
		}

		for (i = 0; i < max_queue_per_proc; i++)
			free(iocb[i]);

	}

	if (tmp_file)
		unlink(pathname);

	if (failed != 0)
		printf("FAIL: %d tests failed\n", failed);
	else
		printf("PASS: All tests passed\n");

	exit (failed == 0 ? 0 : 1);
}
