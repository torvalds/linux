/*	$OpenBSD: bindresvport.c,v 1.19 2019/06/28 13:32:42 deraadt Exp $	*/

/*
 * Copyright 1996, Jason Downs.  All rights reserved.
 * Copyright 1998, Theo de Raadt.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

/*
 * Bind a socket to a privileged IP port
 */
int
bindresvport(int sd, struct sockaddr_in *sin)
{
	return bindresvport_sa(sd, (struct sockaddr *)sin);
}
DEF_WEAK(bindresvport);

/*
 * Bind a socket to a privileged port for whatever protocol.
 */
int
bindresvport_sa(int sd, struct sockaddr *sa)
{
	int old, error, af;
	struct sockaddr_storage myaddr;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	int proto, portrange, portlow;
	u_int16_t port;
	socklen_t salen;

	if (sa == NULL) {
		salen = sizeof(myaddr);
		sa = (struct sockaddr *)&myaddr;

		if (getsockname(sd, sa, &salen) == -1)
			return -1;	/* errno is correctly set */

		af = sa->sa_family;
		memset(&myaddr, 0, salen);
	} else
		af = sa->sa_family;

	if (af == AF_INET) {
		proto = IPPROTO_IP;
		portrange = IP_PORTRANGE;
		portlow = IP_PORTRANGE_LOW;
		sin = (struct sockaddr_in *)sa;
		salen = sizeof(struct sockaddr_in);
		port = sin->sin_port;
	} else if (af == AF_INET6) {
		proto = IPPROTO_IPV6;
		portrange = IPV6_PORTRANGE;
		portlow = IPV6_PORTRANGE_LOW;
		sin6 = (struct sockaddr_in6 *)sa;
		salen = sizeof(struct sockaddr_in6);
		port = sin6->sin6_port;
	} else {
		errno = EPFNOSUPPORT;
		return (-1);
	}
	sa->sa_family = af;
	sa->sa_len = salen;

	if (port == 0) {
		socklen_t oldlen = sizeof(old);

		error = getsockopt(sd, proto, portrange, &old, &oldlen);
		if (error == -1)
			return (error);

		error = setsockopt(sd, proto, portrange, &portlow,
		    sizeof(portlow));
		if (error == -1)
			return (error);
	}

	error = bind(sd, sa, salen);

	if (port == 0) {
		int saved_errno = errno;

		if (error) {
			if (setsockopt(sd, proto, portrange, &old,
			    sizeof(old)) == -1)
				errno = saved_errno;
			return (error);
		}

		if (sa != (struct sockaddr *)&myaddr) {
			/* Hmm, what did the kernel assign... */
			if (getsockname(sd, sa, &salen) == -1)
				errno = saved_errno;
			return (error);
		}
	}
	return (error);
}
DEF_WEAK(bindresvport_sa);
