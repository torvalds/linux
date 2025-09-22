/*	$OpenBSD: pthread_join.c,v 1.5 2018/04/27 06:47:34 guenther Exp $	*/
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

/* ==== test_pthread_join.c =================================================
 * Copyright (c) 1993 by Chris Provenzano, proven@athena.mit.edu
 *
 * Description : Test pthread_join(). Run this after test_create()
 *
 *  1.23 94/05/04 proven
 *      -Started coding this file.
 */

#include <err.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "test.h"

static void
handler(int sig)
{
}

/* This thread yields so the creator has a live thread to wait on */
static void *
new_thread_1(void * new_buf)
{
	int i;

	snprintf((char *)new_buf, 512, "New thread %%d stack at %p\n", &i);
	pthread_yield();	/* (ensure parent can wait on live thread) */
	sleep(2);
	return(new_buf);
	PANIC("return");
}

/* This thread doesn't yield so the creator has a dead thread to wait on */
static void *
new_thread_2(void * new_buf)
{
	int i;

	snprintf((char *)new_buf, 512, "New thread %%d stack at %p\n", &i);
	return(new_buf);
	PANIC("return");
}

int
main(int argc, char *argv[])
{
	struct sigaction sa;
	sigset_t mask;
	char buf[256], *status;
	pthread_t thread;
	int debug = 1;
	int i = 0;

	sa.sa_handler = &handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGALRM, &sa, NULL))
		err(1, "sigaction");
	sigemptyset(&mask);
	sigaddset(&mask, SIGALRM);

	if (debug)
		printf("Original thread stack at %p\n", &i);

	if (sigprocmask(SIG_BLOCK, &mask, NULL))
		err(1, "sigprocmask");
	CHECKr(pthread_create(&thread, NULL, new_thread_1, (void *)buf));
	if (sigprocmask(SIG_UNBLOCK, &mask, NULL))
		err(1, "sigprocmask");
	alarm(1);
	CHECKr(pthread_join(thread, (void **)(&status)));
	if (debug) 
		printf(status, ++i);

	/* Now have the created thread finishing before the join. */
	CHECKr(pthread_create(&thread, NULL, new_thread_2, (void *)buf));
	pthread_yield();
	sleep(1); /* (ensure thread is dead) */
	CHECKr(pthread_join(thread, (void **)(&status)));

	if (debug)
		printf(status, ++i);

	SUCCEED;
}

