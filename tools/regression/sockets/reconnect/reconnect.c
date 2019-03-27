/*-
 * Copyright (c) 2005 Maxim Sobolev
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

/*
 * The reconnect regression test is designed to catch kernel bug that may
 * prevent changing association of already associated datagram unix domain
 * socket when server side of connection has been closed.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

static char uds_name1[] = "reconnect.XXXXXXXX";
static char uds_name2[] = "reconnect.XXXXXXXX";

#define	sstosa(ss)	((struct sockaddr *)(ss))

static void
prepare_ifsun(struct sockaddr_un *ifsun, const char *path)
{

    memset(ifsun, '\0', sizeof(*ifsun));
#if !defined(__linux__) && !defined(__solaris__)
    ifsun->sun_len = strlen(path);
#endif
    ifsun->sun_family = AF_LOCAL;
    strcpy(ifsun->sun_path, path);
}

static int
create_uds_server(const char *path)
{
    struct sockaddr_un ifsun;
    int sock;

    prepare_ifsun(&ifsun, path);

    unlink(ifsun.sun_path);

    sock = socket(PF_LOCAL, SOCK_DGRAM, 0);
    if (sock == -1)
        err(1, "can't create socket");
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &sock, sizeof(sock));
    if (bind(sock, sstosa(&ifsun), sizeof(ifsun)) < 0)
        err(1, "can't bind to a socket");

    return sock;
}

static void
connect_uds_server(int sock, const char *path)
{
    struct sockaddr_un ifsun;
    int e;

    prepare_ifsun(&ifsun, path);

    e = connect(sock, sstosa(&ifsun), sizeof(ifsun));
    if (e < 0)
        err(1, "can't connect to a socket");
}

static void
cleanup(void)
{

    unlink(uds_name1);
    unlink(uds_name2);
}

int
main(void)
{
    int s_sock1, s_sock2, c_sock;

    atexit(cleanup);

    if (mkstemp(uds_name1) == -1)
	err(1, "mkstemp");
    unlink(uds_name1);
    s_sock1 = create_uds_server(uds_name1);

    if (mkstemp(uds_name2) == -1)
        err(1, "mkstemp");
    unlink(uds_name2);
    s_sock2 = create_uds_server(uds_name2);

    c_sock = socket(PF_LOCAL, SOCK_DGRAM, 0);
    if (c_sock < 0)
        err(1, "can't create socket");

    connect_uds_server(c_sock, uds_name1);
    close(s_sock1);
    connect_uds_server(c_sock, uds_name2);
    close(s_sock2);

    exit (0);
}
