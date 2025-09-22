/* $OpenBSD: pthread_rwlock2.c,v 1.1 2019/03/04 08:23:05 semarie Exp $ */
/*
 * Copyright (c) 2019 Sebastien Marie <semarie@online.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <err.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOOP_MAIN	64	/* number of loop to try */
#define NTHREADS	16	/* number of concurrents threads */

/*
 * start several threads that share a buffer protected by rwlock.
 *
 * for each thread, take lock rw to set the buffer, and next take lock
 * rd to read (print) the buffer content.
 */

static pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;
static char msg[128] = "default message";

void
set_msg(int self_n, char *new_msg)
{
	pthread_t self = pthread_self();

	if (pthread_rwlock_wrlock(&rwlock) != 0)
		err(EXIT_FAILURE, "set_msg: pthread_rwlock_wrlock");

	printf("%p: %d: set_msg\n", self, self_n);
	strlcpy(msg, new_msg, sizeof(msg));
	
	if (pthread_rwlock_unlock(&rwlock) != 0)
		err(EXIT_FAILURE, "set_msg: pthread_rwlock_unlock");
}

void
print_msg(int self_n)
{
	pthread_t self = pthread_self();
	
	if (pthread_rwlock_rdlock(&rwlock) != 0)
		err(EXIT_FAILURE, "print_msg: pthread_rwlock_rdlock");
		
	printf("%p: %d: msg: \"%s\"\n", self, self_n, msg);

	if (pthread_rwlock_unlock(&rwlock) != 0)
		err(EXIT_FAILURE, "print_msg: pthread_rwlock_unlock");
}

void *
run(void *data)
{
	int self_n = (int)data;
	pthread_t self = pthread_self();

	printf("%p: %d: enter run()\n", self, self_n);

	set_msg(self_n, "new message");
	print_msg(self_n);

	printf("%p: %d: exit run()\n", self, self_n);
	return NULL;
}

int
main(int argc, char *argv[])
{
	int i, j;

	/* enable some rthread debug code (env take precedence) */
	if (setenv("RTHREAD_DEBUG", "9", 0) == -1)
		err(EXIT_FAILURE, "setenv");

	/* test in loop */
	for (i=0; i < LOOP_MAIN; i++) {
		pthread_t handlers[NTHREADS];

		printf("main: %d\n", i);

		/* launch a serie of threads */
		for (j=0; j < NTHREADS; j++) {
			if (pthread_create(&(handlers[j]), NULL,
			    &run, (void *)(long)j) != 0)
				err(EXIT_FAILURE, "main: pthread_create");
		}

		/* wait for them to finish */
		for (j=0; j < NTHREADS; j++) {
			if (pthread_join(handlers[j], NULL) != 0)
				err(EXIT_FAILURE, "main: pthread_join");
		}
	}

	return EXIT_SUCCESS;
}
