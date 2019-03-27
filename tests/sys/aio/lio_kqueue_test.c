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
 * Note: it is a good idea to run this against a physical drive to
 * exercise the physio fast path (ie. lio_kqueue_test /dev/<something safe>)
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <aio.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "freebsd_test_suite/macros.h"
#include "local.h"

#define PATH_TEMPLATE   "aio.XXXXXXXXXX"

#define LIO_MAX 5
#define MAX_IOCBS_PER_LIO	64
#define MAX_IOCBS (LIO_MAX * MAX_IOCBS_PER_LIO)
#define MAX_RUNS 300

int
main(int argc, char *argv[])
{
	int fd;
	struct aiocb *iocb[MAX_IOCBS];
	struct aiocb **lio[LIO_MAX], **kq_lio;
	int i, result, run, error, j, k, max_queue_per_proc;
	int max_iocbs, iocbs_per_lio;
	size_t max_queue_per_proc_size;
	char buffer[32768];
	int kq;
	struct kevent ke, kq_returned;
	struct timespec ts;
	struct sigevent sig;
	time_t time1, time2;
	char *file, pathname[sizeof(PATH_TEMPLATE)];
	int tmp_file = 0, failed = 0;

	PLAIN_REQUIRE_KERNEL_MODULE("aio", 0);
	PLAIN_REQUIRE_UNSAFE_AIO(0);

	max_queue_per_proc_size = sizeof(max_queue_per_proc);
	if (sysctlbyname("vfs.aio.max_aio_queue_per_proc",
	    &max_queue_per_proc, &max_queue_per_proc_size, NULL, 0) != 0)
		err(1, "sysctlbyname");
	iocbs_per_lio = max_queue_per_proc / LIO_MAX;
	max_iocbs = LIO_MAX * iocbs_per_lio;

	kq = kqueue();
	if (kq < 0)
		err(1, "kqeueue(2) failed");

	if (argc == 1) {
		strcpy(pathname, PATH_TEMPLATE);
		fd = mkstemp(pathname);
		file = pathname;
		tmp_file = 1;
	} else {
		file = argv[1];
		fd = open(file, O_RDWR|O_CREAT, 0666);
        }
	if (fd < 0)
		err(1, "can't open %s", argv[1]);

#ifdef DEBUG
	printf("Hello kq %d fd %d\n", kq, fd);
#endif

	for (run = 0; run < MAX_RUNS; run++) {
#ifdef DEBUG
		printf("Run %d\n", run);
#endif
		for (j = 0; j < LIO_MAX; j++) {
			lio[j] =
			    malloc(sizeof(struct aiocb *) * iocbs_per_lio);
			for (i = 0; i < iocbs_per_lio; i++) {
				k = (iocbs_per_lio * j) + i;
				lio[j][i] = iocb[k] =
				    calloc(1, sizeof(struct aiocb));
				iocb[k]->aio_nbytes = sizeof(buffer);
				iocb[k]->aio_buf = buffer;
				iocb[k]->aio_fildes = fd;
				iocb[k]->aio_offset
				    = iocb[k]->aio_nbytes * k * (run + 1);

#ifdef DEBUG
				printf("hello iocb[k] %jd\n",
				       (intmax_t)iocb[k]->aio_offset);
#endif
				iocb[k]->aio_lio_opcode = LIO_WRITE;
			}
			sig.sigev_notify_kqueue = kq;
			sig.sigev_value.sival_ptr = lio[j];
			sig.sigev_notify = SIGEV_KEVENT;
			time(&time1);
			result = lio_listio(LIO_NOWAIT, lio[j],
					    iocbs_per_lio, &sig);
			error = errno;
			time(&time2);
#ifdef DEBUG
			printf("Time %jd %jd %jd result -> %d\n",
			    (intmax_t)time1, (intmax_t)time2,
			    (intmax_t)time2-time1, result);
#endif
			if (result != 0) {
			        errno = error;
				err(1, "FAIL: Result %d iteration %d\n",
				    result, j);
			}
#ifdef DEBUG
			printf("write %d is at %p\n", j, lio[j]);
#endif
		}

		for (i = 0; i < LIO_MAX; i++) {
			for (j = LIO_MAX - 1; j >=0; j--) {
				if (lio[j])
					break;
			}

			for (;;) {
				bzero(&ke, sizeof(ke));
				bzero(&kq_returned, sizeof(ke));
				ts.tv_sec = 0;
				ts.tv_nsec = 1;
#ifdef DEBUG
				printf("FOO lio %d -> %p\n", j, lio[j]);
#endif
				EV_SET(&ke, (uintptr_t)lio[j],
				       EVFILT_LIO, EV_ONESHOT, 0, 0, iocb[j]);
				result = kevent(kq, NULL, 0,
						&kq_returned, 1, &ts);
				error = errno;
				if (result < 0) {
					perror("kevent error: ");
				}
				kq_lio = kq_returned.udata;
#ifdef DEBUG
				printf("kevent %d %d errno %d return.ident %p "
				       "return.data %p return.udata %p %p\n",
				       i, result, error,
				       (void*)kq_returned.ident,
				       (void*)kq_returned.data,
				       kq_returned.udata,
				       lio[j]);
#endif

				if (kq_lio)
					break;
#ifdef DEBUG
				printf("Try again\n");
#endif
			}

#ifdef DEBUG
			printf("lio %p\n", lio);
#endif

			for (j = 0; j < LIO_MAX; j++) {
				if (lio[j] == kq_lio)
					break;
			}
			if (j == LIO_MAX)
				errx(1, "FAIL: ");

#ifdef DEBUG
			printf("Error Result for %d is %d\n", j, result);
#endif
			if (result < 0) {
				printf("FAIL: run %d, operation %d result %d \n", run, LIO_MAX - i -1, result);
				failed++;
			} else
				printf("PASS: run %d, operation %d result %d \n", run, LIO_MAX - i -1, result);
			for (k = 0; k < max_iocbs / LIO_MAX; k++) {
				result = aio_return(kq_lio[k]);
#ifdef DEBUG
				printf("Return Result for %d %d is %d\n", j, k, result);
#endif
				if (result != sizeof(buffer)) {
					printf("FAIL: run %d, operation %d sub-opt %d  result %d (errno=%d) should be %zu\n",
					   run, LIO_MAX - i -1, k, result, errno, sizeof(buffer));
				} else {
					printf("PASS: run %d, operation %d sub-opt %d  result %d\n",
					   run, LIO_MAX - i -1, k, result);
				}
			}
#ifdef DEBUG
			printf("\n");
#endif

			for (k = 0; k < max_iocbs / LIO_MAX; k++)
				free(lio[j][k]);
			free(lio[j]);
			lio[j] = NULL;
		}
	}
#ifdef DEBUG
	printf("Done\n");
#endif

	if (tmp_file)
		unlink(pathname);

	if (failed)
		errx(1, "FAIL: %d testcases failed", failed);
	else
		errx(0, "PASS: All\n");

}
