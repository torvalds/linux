/*	$OpenBSD: util.c,v 1.1.1.1 2022/09/01 14:20:33 martijn Exp $	*/
/*
 * Copyright (c) 2014 Bret Stephen Lambert <blambert@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include <net/if.h>

#include <ber.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <event.h>

#include "snmpd.h"

ssize_t
sendtofrom(int s, void *buf, size_t len, int flags, struct sockaddr *to,
    socklen_t tolen, struct sockaddr *from, socklen_t fromlen)
{
	struct iovec		 iov;
	struct msghdr		 msg;
	struct cmsghdr		*cmsg;
	struct in6_pktinfo	*pkt6;
	struct sockaddr_in	*in;
	struct sockaddr_in6	*in6;
	union {
		struct cmsghdr	hdr;
		char		inbuf[CMSG_SPACE(sizeof(struct in_addr))];
		char		in6buf[CMSG_SPACE(sizeof(struct in6_pktinfo))];
	} cmsgbuf;

	bzero(&msg, sizeof(msg));
	bzero(&cmsgbuf, sizeof(cmsgbuf));

	iov.iov_base = buf;
	iov.iov_len = len;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = to;
	msg.msg_namelen = tolen;
	msg.msg_control = &cmsgbuf;
	msg.msg_controllen = sizeof(cmsgbuf);

	cmsg = CMSG_FIRSTHDR(&msg);
	switch (to->sa_family) {
	case AF_INET:
		msg.msg_controllen = sizeof(cmsgbuf.inbuf);
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct in_addr));
		cmsg->cmsg_level = IPPROTO_IP;
		cmsg->cmsg_type = IP_SENDSRCADDR;
		in = (struct sockaddr_in *)from;
		memcpy(CMSG_DATA(cmsg), &in->sin_addr, sizeof(struct in_addr));
		break;
	case AF_INET6:
		msg.msg_controllen = sizeof(cmsgbuf.in6buf);
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
		cmsg->cmsg_level = IPPROTO_IPV6;
		cmsg->cmsg_type = IPV6_PKTINFO;
		in6 = (struct sockaddr_in6 *)from;
		pkt6 = (struct in6_pktinfo *)CMSG_DATA(cmsg);
		pkt6->ipi6_addr = in6->sin6_addr;
		break;
	}

	return sendmsg(s, &msg, flags);
}

ssize_t
recvfromto(int s, void *buf, size_t len, int flags, struct sockaddr *from,
    socklen_t *fromlen, struct sockaddr *to, socklen_t *tolen)
{
	struct iovec		 iov;
	struct msghdr		 msg;
	struct cmsghdr		*cmsg;
	struct in6_pktinfo	*pkt6;
	struct sockaddr_in	*in;
	struct sockaddr_in6	*in6;
	ssize_t			 ret;
	union {
		struct cmsghdr hdr;
		char	buf[CMSG_SPACE(sizeof(struct sockaddr_storage))];
	} cmsgbuf;

	bzero(&msg, sizeof(msg));
	bzero(&cmsgbuf.buf, sizeof(cmsgbuf.buf));

	iov.iov_base = buf;
	iov.iov_len = len;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = from;
	msg.msg_namelen = *fromlen;
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	if ((ret = recvmsg(s, &msg, flags)) == -1)
		return (-1);

	*fromlen = from->sa_len;
	*tolen = 0;

	if (getsockname(s, to, tolen) != 0)
		*tolen = 0;

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
	    cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		switch (from->sa_family) {
		case AF_INET:
			if (cmsg->cmsg_level == IPPROTO_IP &&
			    cmsg->cmsg_type == IP_RECVDSTADDR) {
				in = (struct sockaddr_in *)to;
				in->sin_family = AF_INET;
				in->sin_len = *tolen = sizeof(*in);
				memcpy(&in->sin_addr, CMSG_DATA(cmsg),
				    sizeof(struct in_addr));
			}
			break;
		case AF_INET6:
			if (cmsg->cmsg_level == IPPROTO_IPV6 &&
			    cmsg->cmsg_type == IPV6_PKTINFO) {
				in6 = (struct sockaddr_in6 *)to;
				in6->sin6_family = AF_INET6;
				in6->sin6_len = *tolen = sizeof(*in6);
				pkt6 = (struct in6_pktinfo *)CMSG_DATA(cmsg);
				memcpy(&in6->sin6_addr, &pkt6->ipi6_addr,
				    sizeof(struct in6_addr));
				if (IN6_IS_ADDR_LINKLOCAL(&in6->sin6_addr))
					in6->sin6_scope_id =
					    pkt6->ipi6_ifindex;
			}
			break;
		}
	}

	return (ret);
}

const char *
log_in6addr(const struct in6_addr *addr)
{
	static char		buf[NI_MAXHOST];
	struct sockaddr_in6	sa_in6;
	u_int16_t		tmp16;

	bzero(&sa_in6, sizeof(sa_in6));
	sa_in6.sin6_len = sizeof(sa_in6);
	sa_in6.sin6_family = AF_INET6;
	memcpy(&sa_in6.sin6_addr, addr, sizeof(sa_in6.sin6_addr));

	/* XXX thanks, KAME, for this ugliness... adopted from route/show.c */
	if (IN6_IS_ADDR_LINKLOCAL(&sa_in6.sin6_addr) ||
	    IN6_IS_ADDR_MC_LINKLOCAL(&sa_in6.sin6_addr)) {
		memcpy(&tmp16, &sa_in6.sin6_addr.s6_addr[2], sizeof(tmp16));
		sa_in6.sin6_scope_id = ntohs(tmp16);
		sa_in6.sin6_addr.s6_addr[2] = 0;
		sa_in6.sin6_addr.s6_addr[3] = 0;
	}

	return (print_host((struct sockaddr_storage *)&sa_in6, buf,
	    NI_MAXHOST));
}

const char *
print_host(struct sockaddr_storage *ss, char *buf, size_t len)
{
	if (getnameinfo((struct sockaddr *)ss, ss->ss_len,
	    buf, len, NULL, 0, NI_NUMERICHOST) != 0) {
		buf[0] = '\0';
		return (NULL);
	}
	return (buf);
}

char *
tohexstr(uint8_t *bstr, int len)
{
#define MAXHEXSTRLEN		256
	static char hstr[2 * MAXHEXSTRLEN + 1];
	static const char hex[] = "0123456789abcdef";
	int i;

	if (len > MAXHEXSTRLEN)
		len = MAXHEXSTRLEN;	/* truncate */
	for (i = 0; i < len; i++) {
		hstr[i + i] = hex[bstr[i] >> 4];
		hstr[i + i + 1] = hex[bstr[i] & 0x0f];
	}
	hstr[i + i] = '\0';
	return hstr;
}

uint8_t *
fromhexstr(uint8_t *bstr, const char *hstr, size_t len)
{
	size_t i;
	char hex[3];

	if (len % 2 != 0)
		return NULL;

	hex[2] = '\0';
	for (i = 0; i < len; i += 2) {
		if (!isxdigit(hstr[i]) || !isxdigit(hstr[i + 1]))
			return NULL;
		hex[0] = hstr[i];
		hex[1] = hstr[i + 1];
		bstr[i / 2] = strtol(hex, NULL, 16);
	}

	return bstr;
}
