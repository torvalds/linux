/*-
 * Copyright (c) 2008 Ganbold Tsagaankhuu
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/wait.h>
#include <err.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NUM_THREADS 100

static void *
vfork_test(void *threadid __unused)
{
	pid_t pid, wpid;
	int status;

	for (;;) {
		pid = vfork();
		if (pid == 0)
			_exit(0);
		else if (pid == -1)
			err(1, "Failed to vfork");
		else {
			wpid = waitpid(pid, &status, 0);
			if (wpid == -1)
				err(1, "waitpid");
		}
	}
	return (NULL);
}

static void
sighandler(int signo __unused)
{
}

/*
 * This program invokes multiple threads and each thread calls
 * vfork() system call.
 */
int
main(void)
{
	pthread_t threads[NUM_THREADS];
	struct sigaction reapchildren;
	sigset_t sigchld_mask;
	int rc, t;

	memset(&reapchildren, 0, sizeof(reapchildren));
	reapchildren.sa_handler = sighandler;
	if (sigaction(SIGCHLD, &reapchildren, NULL) == -1)
		err(1, "Could not sigaction(SIGCHLD)");

	sigemptyset(&sigchld_mask);
	sigaddset(&sigchld_mask, SIGCHLD);
	if (sigprocmask(SIG_BLOCK, &sigchld_mask, NULL) == -1)
		err(1, "sigprocmask");

	for (t = 0; t < NUM_THREADS; t++) {
		rc = pthread_create(&threads[t], NULL, vfork_test, &t);
		if (rc)
			errc(1, rc, "pthread_create");
	}
	pause();
	return (0);
}
