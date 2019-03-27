/*-
 * Copyright (c) 2008 Dag-Erling Coïdan Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
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

#include <pthread.h>
#include <pthread_np.h>
#include <stdio.h>
#include <stdlib.h>

static void *
thread(void *arg)
{
	pthread_mutex_t *mtx = arg;

	if (pthread_mutex_isowned_np(mtx) != 0) {
		printf("pthread_mutex_isowned_np() returned non-zero\n"
		    "for a mutex held by another thread\n");
		exit(1);
	}
	return (NULL);
}

int
main(int argc, char *argv[])
{
	pthread_t thr;
	pthread_mutex_t mtx;

	pthread_mutex_init(&mtx, NULL);
	if (pthread_mutex_isowned_np(&mtx) != 0) {
		printf("pthread_mutex_isowned_np() returned non-zero\n"
		    "for a mutex that is not held\n");
		exit(1);
	}
	pthread_mutex_lock(&mtx);
	if (pthread_mutex_isowned_np(&mtx) == 0) {
		printf("pthread_mutex_isowned_np() returned zero\n"
		    "for a mutex we hold ourselves\n");
		exit(1);
	}
	pthread_create(&thr, NULL, thread, &mtx);
	pthread_join(thr, NULL);
	pthread_mutex_unlock(&mtx);
	if (pthread_mutex_isowned_np(&mtx) != 0) {
		printf("pthread_mutex_isowned_np() returned non-zero\n"
		    "for a mutex that is not held\n");
		exit(1);
	}

	printf("OK\n");
	exit(0);
}
