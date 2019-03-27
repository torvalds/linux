/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 The FreeBSD Foundation
 * Copyright (c) 2015 Mariusz Zaborski <oshogbo@FreeBSD.org>
 * Copyright (c) 2017 Robert N. M. Watson
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
 *
 * All rights reserved.
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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
#include <sys/capsicum.h>
#include <sys/procdesc.h>
#include <sys/socket.h>
#include <sys/nv.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include "zygote.h"

/* Zygote info. */
static int	zygote_sock = -1;

#define	ZYGOTE_SERVICE_EXECUTE	1

int
zygote_clone(uint64_t funcidx, int *chanfdp, int *procfdp)
{
	nvlist_t *nvl;
	int error;

	if (zygote_sock == -1) {
		/* Zygote didn't start. */
		errno = ENXIO;
		return (-1);
	}

	nvl = nvlist_create(0);
	nvlist_add_number(nvl, "funcidx", funcidx);
	nvl = nvlist_xfer(zygote_sock, nvl, 0);
	if (nvl == NULL)
		return (-1);
	if (nvlist_exists_number(nvl, "error")) {
		error = (int)nvlist_get_number(nvl, "error");
		nvlist_destroy(nvl);
		errno = error;
		return (-1);
	}

	*chanfdp = nvlist_take_descriptor(nvl, "chanfd");
	*procfdp = nvlist_take_descriptor(nvl, "procfd");

	nvlist_destroy(nvl);
	return (0);
}

int
zygote_clone_service_execute(int *chanfdp, int *procfdp)
{

	return (zygote_clone(ZYGOTE_SERVICE_EXECUTE, chanfdp, procfdp));
}

/*
 * This function creates sandboxes on-demand whoever has access to it via
 * 'sock' socket. Function sends two descriptors to the caller: process
 * descriptor of the sandbox and socket pair descriptor for communication
 * between sandbox and its owner.
 */
static void
zygote_main(int sock)
{
	int error, procfd;
	int chanfd[2];
	nvlist_t *nvlin, *nvlout;
	uint64_t funcidx;
	zygote_func_t *func;
	pid_t pid;

	assert(sock > STDERR_FILENO);

	setproctitle("zygote");

	for (;;) {
		nvlin = nvlist_recv(sock, 0);
		if (nvlin == NULL) {
			if (errno == ENOTCONN) {
				/* Casper exited. */
				_exit(0);
			}
			continue;
		}
		funcidx = nvlist_get_number(nvlin, "funcidx");
		nvlist_destroy(nvlin);

		switch (funcidx) {
		case ZYGOTE_SERVICE_EXECUTE:
			func = service_execute;
			break;
		default:
			_exit(0);
		}

		/*
		 * Someone is requesting a new process, create one.
		 */
		procfd = -1;
		chanfd[0] = -1;
		chanfd[1] = -1;
		error = 0;
		if (socketpair(PF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0,
		    chanfd) == -1) {
			error = errno;
			goto send;
		}
		pid = pdfork(&procfd, 0);
		switch (pid) {
		case -1:
			/* Failure. */
			error = errno;
			break;
		case 0:
			/* Child. */
			close(sock);
			close(chanfd[0]);
			func(chanfd[1]);
			/* NOTREACHED */
			_exit(1);
		default:
			/* Parent. */
			close(chanfd[1]);
			break;
		}
send:
		nvlout = nvlist_create(0);
		if (error != 0) {
			nvlist_add_number(nvlout, "error", (uint64_t)error);
			if (chanfd[0] >= 0)
				close(chanfd[0]);
			if (procfd >= 0)
				close(procfd);
		} else {
			nvlist_move_descriptor(nvlout, "chanfd", chanfd[0]);
			nvlist_move_descriptor(nvlout, "procfd", procfd);
		}
		(void)nvlist_send(sock, nvlout);
		nvlist_destroy(nvlout);
	}
	/* NOTREACHED */
}

int
zygote_init(void)
{
	int serrno, sp[2];
	pid_t pid;

	if (socketpair(PF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sp) == -1)
		return (-1);

	pid = fork();
	switch (pid) {
	case -1:
		/* Failure. */
		serrno = errno;
		close(sp[0]);
		close(sp[1]);
		errno = serrno;
		return (-1);
	case 0:
		/* Child. */
		close(sp[0]);
		zygote_main(sp[1]);
		/* NOTREACHED */
		abort();
	default:
		/* Parent. */
		zygote_sock = sp[0];
		close(sp[1]);
		return (0);
	}
	/* NOTREACHED */
}
