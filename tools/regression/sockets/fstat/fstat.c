/*-
 * Copyright (c) 2008 Robert N. M. Watson
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
#include <sys/stat.h>

#include <err.h>
#include <unistd.h>

/*
 * Basic test to make sure that fstat(2) returns success on various socket
 * types.  In the future we should also validate the fields, confirming
 * expected results such as the effect of shutdown(2) on permissions, etc.
 */

static void
dotest(int domain, int type, int protocol)
{
	struct stat sb;
	int sock;

	sock = socket(domain, type, protocol);
	if (sock < 0)
		err(-1, "socket(%d, %d, %d)", domain, type, protocol);

	if (fstat(sock, &sb) < 0)
		err(-1, "fstat on socket(%d, %d, %d)", domain, type,
		    protocol);

	close(sock);
}

int
main(void)
{

	dotest(PF_INET, SOCK_DGRAM, 0);
	dotest(PF_INET, SOCK_STREAM, 0);
	dotest(PF_INET6, SOCK_DGRAM, 0);
	dotest(PF_INET6, SOCK_STREAM, 0);
	dotest(PF_LOCAL, SOCK_DGRAM, 0);
	dotest(PF_LOCAL, SOCK_STREAM, 0);

	return (0);
}
