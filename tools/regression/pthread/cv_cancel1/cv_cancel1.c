/*-
 * Copyright (c) 2006, David Xu <davidxu@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#define NLOOPS	10

pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

int wake;
int stop;

void *
thr_routine(void *arg)
{
	pthread_mutex_lock(&m);
	while (wake == 0)
		pthread_cond_wait(&cv, &m);
	pthread_mutex_unlock(&m);

	while (stop == 0)
		pthread_yield();
	return (NULL);
}

int main(int argc, char **argv)
{
	pthread_t td;
	int i;
	void *result;

	pthread_setconcurrency(1);
	for (i = 0; i < NLOOPS; ++i) {
		stop = 0;
		wake = 0;

		pthread_create(&td, NULL, thr_routine, NULL);
		sleep(1);
		printf("trying: %d\n", i);
		pthread_mutex_lock(&m);
		wake = 1;
		pthread_cond_signal(&cv);
		pthread_cancel(td);
		pthread_mutex_unlock(&m);
		stop = 1;
		result = NULL;
		pthread_join(td, &result);
		if (result == PTHREAD_CANCELED) {
			printf("the condition variable implementation does not\n"
			       "conform to SUSv3, a thread unblocked from\n"
			       "condition variable still can be canceled.\n");
			return (1);
		}
	}

	printf("OK\n");
	return (0);
}
