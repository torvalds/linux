/*-
 * Copyright (c) 2017 Hartmut Brandt <harti@FreeBSD.org>
 * Copyright (c) 2017 Gleb Smirnoff <glebius@FreeBSD.org>
 * All rights reserved.
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <sys/filio.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <err.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * This regression test is against a scenario when a socket is first added
 * to a kqueue, and only then is put into listening state.
 * This weird scenario was made a valid one in r313043, and shouldn't be
 * broken later.
 */

int
main()
{
	struct sockaddr_in addr;
	struct kevent ev[2];
	socklen_t socklen;
	int kq, sock, opt, pid, nev, fd;

	if ((kq = kqueue()) == -1)
		err(1, "kqueue");

	if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1)
		err(1, "socket");

	EV_SET(&ev[0], sock, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
	EV_SET(&ev[1], sock, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, NULL);

	opt = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt)) == -1)
		err(1, "setsockopt");

	if (kevent(kq, ev, 2, NULL, 0, NULL) == -1)
	    err(1, "kevent");

	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
		err(1, "setsockopt");

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(10000);

	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1)
		err(1, "bind");
	if (listen(sock, 0x80) == -1)
		err(1, "listen");

	if (ioctl(sock, FIONBIO, &opt) == -1)
		err(1, "ioctl(FIONBIO)");

	if (kevent(kq, ev, 2, NULL, 0, NULL) == -1)
		err(1, "kevent");

	pid = fork();
	if (pid == -1)
		err(1, "fork");
	if (pid == 0) {
		if (close(sock) == -1)
			err(1, "close");
		if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1)
			err(1, "socket");
		if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1)
			err(1, "connect");
	} else {
		nev = kevent(kq, NULL, 0, ev, 2, NULL);
		if (nev < 1)
			err(1, "kevent");
		for (int i = 0; i < nev; ++i) {
			if (ev[i].ident == (uintptr_t )sock) {
				fd = accept(ev[i].ident,
				    (struct sockaddr *)&addr, &socklen);
				if (fd == -1)
					err(1, "accept");
				printf("OK\n");
			}
		}
	}
}
