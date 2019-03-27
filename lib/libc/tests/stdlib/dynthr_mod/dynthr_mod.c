/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (C) 2019 Andrew Gierth
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
 * Though this file is initially distributed under the 2-clause BSD license,
 * the author grants permission for its redistribution under alternative
 * licenses as set forth at <https://rhodiumtoad.github.io/RELICENSE.txt>.
 * This paragraph and the RELICENSE.txt file are not part of the license and
 * may be omitted in redistributions.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

static pthread_t thr;

static void *
mod_thread(void *ptr)
{
	char *volatile dummy;

	dummy = malloc(500);
	return (NULL);
}

void
mod_main(int op)
{
	int rc;

	switch (op) {
	case 1:
		rc = pthread_create(&thr, NULL, mod_thread, NULL);
		if (rc != 0)
			_exit(1);
		break;
	case 0:
		pthread_join(thr, NULL);
		break;
	}
}

