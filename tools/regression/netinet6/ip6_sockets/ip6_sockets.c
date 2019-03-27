/*-
 * Copyright (c) 2006 Robert N. M. Watson
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

#include <sys/socket.h>

#include <netinet/in.h>

#include <err.h>
#include <string.h>
#include <unistd.h>

/*
 * Simple regression test to create and close a variety of IPv6 socket types.
 */

int
main(int argc, char *argv[])
{
	struct sockaddr_in6 sin6;
	int s;

	/*
	 * UDPv6 simple test.
	 */
	s = socket(PF_INET6, SOCK_DGRAM, 0);
	if (s < 0)
		err(-1, "socket(PF_INET6, SOCK_DGRAM, 0)");
	close(s);

	/*
	 * UDPv6 connected case -- connect UDPv6 to an arbitrary port so that
	 * when we close the socket, it goes through the disconnect logic.
	 */
	s = socket(PF_INET6, SOCK_DGRAM, 0);
	if (s < 0)
		err(-1, "socket(PF_INET6, SOCK_DGRAM, 0)");
	bzero(&sin6, sizeof(sin6));
	sin6.sin6_len = sizeof(sin6);
	sin6.sin6_family = AF_INET6;
	sin6.sin6_addr = in6addr_loopback;
	sin6.sin6_port = htons(1024);
	if (connect(s, (struct sockaddr *)&sin6, sizeof(sin6)) < 0)
		err(-1, "connect(SOCK_DGRAM, ::1)");
	close(s);

	/*
	 * TCPv6.
	 */
	s = socket(PF_INET6, SOCK_STREAM, 0);
	if (s < 0)
		err(-1, "socket(PF_INET6, SOCK_STREAM, 0)");
	close(s);

	/*
	 * Raw IPv6.
	 */
	s = socket(PF_INET6, SOCK_RAW, 0);
	if (s < 0)
		err(-1, "socket(PF_INET6, SOCK_RAW, 0)");
	close(s);

	return (0);
}
