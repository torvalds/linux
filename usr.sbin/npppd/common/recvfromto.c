/*	$OpenBSD: recvfromto.c,v 1.7 2024/02/26 08:25:51 yasuoka Exp $ */
/* adapted from ipsec-tools 0.6 src/racoon/sockmisc.c */
/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <string.h>

#include "recvfromto.h"

/*
 * Receive packet, with src/dst information.  It is assumed that necessary
 * setsockopt() have already performed on socket.
 */
int
recvfromto_nat_t(int s, void *buf, size_t buflen, int flags,
    struct sockaddr *from, u_int *fromlen, struct sockaddr *to, u_int *tolen,
    void *ipsec, u_int *ipseclen)
{
	int otolen;
	u_int oipseclen = 0;
	ssize_t len;
	struct sockaddr_storage ss;
	struct msghdr m;
	struct cmsghdr *cm;
	struct iovec iov[2];
	u_char cmsgbuf[256];
	struct in6_pktinfo *pi;
	struct sockaddr_in *sin4;
	socklen_t sslen;
	struct sockaddr_in6 *sin6;

	sslen = sizeof(ss);
	if (getsockname(s, (struct sockaddr *)&ss, &sslen) < 0)
		return -1;

	m.msg_name = (caddr_t)from;
	m.msg_namelen = *fromlen;
	iov[0].iov_base = (caddr_t)buf;
	iov[0].iov_len = buflen;
	m.msg_iov = iov;
	m.msg_iovlen = 1;
	memset(cmsgbuf, 0, sizeof(cmsgbuf));
	cm = (struct cmsghdr *)cmsgbuf;
	m.msg_control = (caddr_t)cm;
	m.msg_controllen = sizeof(cmsgbuf);
	if ((len = recvmsg(s, &m, flags)) <= 0) {
		return len;
	}
	*fromlen = m.msg_namelen;

	if (ipsec && ipseclen) {
		oipseclen = *ipseclen;
		*ipseclen = 0;
	}
	otolen = *tolen;
	*tolen = 0;
	for (cm = (struct cmsghdr *)CMSG_FIRSTHDR(&m);
	     m.msg_controllen != 0 && cm;
	     cm = (struct cmsghdr *)CMSG_NXTHDR(&m, cm)) {
		if (ss.ss_family == AF_INET6
		 && cm->cmsg_level == IPPROTO_IPV6
		 && cm->cmsg_type == IPV6_PKTINFO
		 && otolen >= sizeof(*sin6)) {
			pi = (struct in6_pktinfo *)(CMSG_DATA(cm));
			*tolen = sizeof(*sin6);
			sin6 = (struct sockaddr_in6 *)to;
			memset(sin6, 0, sizeof(*sin6));
			sin6->sin6_family = AF_INET6;
#ifndef __linux__
			sin6->sin6_len = sizeof(*sin6);
#endif
			memcpy(&sin6->sin6_addr, &pi->ipi6_addr,
				sizeof(sin6->sin6_addr));
			/* XXX other cases, such as site-local? */
			if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr))
				sin6->sin6_scope_id = pi->ipi6_ifindex;
			else
				sin6->sin6_scope_id = 0;
			sin6->sin6_port =
				((struct sockaddr_in6 *)&ss)->sin6_port;
			otolen = -1;	/* "to" already set */
			continue;
		}
#ifdef IP_IPSECFLOWINFO
		if (ss.ss_family == AF_INET	/* ?? */
		 && cm->cmsg_level == IPPROTO_IP
		 && cm->cmsg_type == IP_IPSECFLOWINFO
		 && oipseclen >= sizeof(u_int32_t)) {
			*ipseclen = sizeof(u_int32_t);
			memcpy(ipsec, CMSG_DATA(cm), *ipseclen);
			continue;
		}
#endif
#ifdef __linux__
		if (ss.ss_family == AF_INET
		 && cm->cmsg_level == IPPROTO_IP
		 && cm->cmsg_type == IP_PKTINFO
		 && otolen >= sizeof(sin4)) {
			struct in_pktinfo *pi = (struct in_pktinfo *)(CMSG_DATA(cm));
			*tolen = sizeof(*sin4);
			sin4 = (struct sockaddr_in *)to;
			memset(sin4, 0, sizeof(*sin4));
			sin4->sin_family = AF_INET;
			memcpy(&sin4->sin_addr, &pi->ipi_addr,
				sizeof(sin4->sin_addr));
			sin4->sin_port =
				((struct sockaddr_in *)&ss)->sin_port;
			otolen = -1;	/* "to" already set */
			continue;
		}
#endif
#ifdef IPV6_RECVDSTADDR
		if (ss.ss_family == AF_INET6
		      && cm->cmsg_level == IPPROTO_IPV6
		      && cm->cmsg_type == IPV6_RECVDSTADDR
		      && otolen >= sizeof(*sin6)) {
			*tolen = sizeof(*sin6);
			sin6 = (struct sockaddr_in6 *)to;
			memset(sin6, 0, sizeof(*sin6));
			sin6->sin6_family = AF_INET6;
			sin6->sin6_len = sizeof(*sin6);
			memcpy(&sin6->sin6_addr, CMSG_DATA(cm),
				sizeof(sin6->sin6_addr));
			sin6->sin6_port =
				((struct sockaddr_in6 *)&ss)->sin6_port;
			otolen = -1;	/* "to" already set */
			continue;
		}
#endif
#ifndef __linux__
		if (ss.ss_family == AF_INET
		 && cm->cmsg_level == IPPROTO_IP
		 && cm->cmsg_type == IP_RECVDSTADDR
		 && otolen >= sizeof(*sin4)) {
			*tolen = sizeof(*sin4);
			sin4 = (struct sockaddr_in *)to;
			memset(sin4, 0, sizeof(*sin4));
			sin4->sin_family = AF_INET;
			sin4->sin_len = sizeof(*sin4);
			memcpy(&sin4->sin_addr, CMSG_DATA(cm),
				sizeof(sin4->sin_addr));
			sin4->sin_port = ((struct sockaddr_in *)&ss)->sin_port;
			otolen = -1;	/* "to" already set */
			continue;
		}
#endif
	}

	return len;
}

int
recvfromto(int s, void *buf, size_t buflen, int flags, struct sockaddr *from,
    u_int *fromlen, struct sockaddr *to, u_int *tolen)
{
	return recvfromto_nat_t(s, buf, buflen, flags, from, fromlen,
	    to, tolen, NULL, NULL);
}

int
sendto_nat_t(int s, const void *buf, size_t buflen, int flags,
    struct sockaddr *to, u_int tolen, void *ipsec)
{
#ifdef IP_IPSECFLOWINFO
	if (ipsec) {
		struct iovec iov[1];
		struct msghdr msg;
		struct cmsghdr  *cmsg;
		union {
			struct cmsghdr  hdr;
			char            buf[CMSG_SPACE(sizeof(u_int32_t))];
		} cmsgbuf;

		iov[0].iov_base = (char *)buf;
		iov[0].iov_len = buflen;
		memset(&msg, 0, sizeof(msg));
		memset(&cmsgbuf, 0, sizeof(cmsgbuf));
		msg.msg_name = to;
		msg.msg_namelen = tolen;
		msg.msg_iov = iov;
		msg.msg_iovlen = 1;
		msg.msg_control = (caddr_t)&cmsgbuf.buf;
		msg.msg_controllen = sizeof(cmsgbuf.buf);
		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_len = CMSG_LEN(sizeof(u_int32_t));
		cmsg->cmsg_level = IPPROTO_IP;
		cmsg->cmsg_type = IP_IPSECFLOWINFO;
		memcpy(CMSG_DATA(cmsg), ipsec, sizeof(u_int32_t));
		return sendmsg(s, &msg, flags);
	}
#endif
	return sendto(s, buf, buflen, flags, to, tolen);
}
