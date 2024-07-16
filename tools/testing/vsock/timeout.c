// SPDX-License-Identifier: GPL-2.0-only
/* Timeout API for single-threaded programs that use blocking
 * syscalls (read/write/send/recv/connect/accept).
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * Author: Stefan Hajnoczi <stefanha@redhat.com>
 */

/* Use the following pattern:
 *
 *   timeout_begin(TIMEOUT);
 *   do {
 *       ret = accept(...);
 *       timeout_check("accept");
 *   } while (ret < 0 && ret == EINTR);
 *   timeout_end();
 */

#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include "timeout.h"

static volatile bool timeout;

/* SIGALRM handler function.  Do not use sleep(2), alarm(2), or
 * setitimer(2) while using this API - they may interfere with each
 * other.
 */
void sigalrm(int signo)
{
	timeout = true;
}

/* Start a timeout.  Call timeout_check() to verify that the timeout hasn't
 * expired.  timeout_end() must be called to stop the timeout.  Timeouts cannot
 * be nested.
 */
void timeout_begin(unsigned int seconds)
{
	alarm(seconds);
}

/* Exit with an error message if the timeout has expired */
void timeout_check(const char *operation)
{
	if (timeout) {
		fprintf(stderr, "%s timed out\n", operation);
		exit(EXIT_FAILURE);
	}
}

/* Stop a timeout */
void timeout_end(void)
{
	alarm(0);
	timeout = false;
}
