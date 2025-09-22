/*	$OpenBSD: svc_run.c,v 1.25 2015/09/01 17:31:39 deraadt Exp $ */

/*
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This is the rpc server side idle loop
 * Wait for input, call server program.
 */
#include <rpc/rpc.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
svc_run(void)
{
	struct pollfd *pfd = NULL, *newp;
	int nready, saved_max_pollfd = 0;

	for (;;) {
		if (svc_max_pollfd > saved_max_pollfd) {
			newp = reallocarray(pfd, svc_max_pollfd, sizeof(*pfd));
			if (newp == NULL) {
				free(pfd);
				return;			/* XXX */
			}
			saved_max_pollfd = svc_max_pollfd;
			pfd = newp;
		}
		memcpy(pfd, svc_pollfd, sizeof(*pfd) * svc_max_pollfd);

		nready = poll(pfd, svc_max_pollfd, INFTIM);
		switch (nready) {
		case -1:
			if (errno == EINTR)
				continue;
			free(pfd);
			return;					/* XXX */
		case 0:
			/* should not happen */
			continue;
		default:
			svc_getreq_poll(pfd, nready);
		}
	}
}
