/*	$OpenBSD: util.c,v 1.45 2024/07/01 14:15:15 yasuoka Exp $	*/

/*
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
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
#include <sys/uio.h>

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <ctype.h>
#include <event.h>

#include "iked.h"
#include "ikev2.h"

int
socket_af(struct sockaddr *sa, in_port_t port)
{
	errno = 0;
	switch (sa->sa_family) {
	case AF_INET:
		((struct sockaddr_in *)sa)->sin_port = port;
		((struct sockaddr_in *)sa)->sin_len =
		    sizeof(struct sockaddr_in);
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)sa)->sin6_port = port;
		((struct sockaddr_in6 *)sa)->sin6_len =
		    sizeof(struct sockaddr_in6);
		break;
	default:
		errno = EPFNOSUPPORT;
		return (-1);
	}

	return (0);
}

in_port_t
socket_getport(struct sockaddr *sa)
{
	switch (sa->sa_family) {
	case AF_INET:
		return (ntohs(((struct sockaddr_in *)sa)->sin_port));
	case AF_INET6:
		return (ntohs(((struct sockaddr_in6 *)sa)->sin6_port));
	default:
		return (0);
	}

	/* NOTREACHED */
	return (0);
}

int
socket_setport(struct sockaddr *sa, in_port_t port)
{
	switch (sa->sa_family) {
	case AF_INET:
		((struct sockaddr_in *)sa)->sin_port = htons(port);
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)sa)->sin6_port = htons(port);
		break;
	default:
		return (-1);
	}
	return (0);
}

int
socket_getaddr(int s, struct sockaddr_storage *ss)
{
	socklen_t sslen = sizeof(*ss);

	return (getsockname(s, (struct sockaddr *)ss, &sslen));
}

int
socket_bypass(int s, struct sockaddr *sa)
{
	int	 v, *a;
	int	 a4[] = {
		    IPPROTO_IP,
		    IP_AUTH_LEVEL,
		    IP_ESP_TRANS_LEVEL,
		    IP_ESP_NETWORK_LEVEL,
#ifdef IPV6_IPCOMP_LEVEL
		    IP_IPCOMP_LEVEL
#endif
	};
	int	 a6[] = {
		    IPPROTO_IPV6,
		    IPV6_AUTH_LEVEL,
		    IPV6_ESP_TRANS_LEVEL,
		    IPV6_ESP_NETWORK_LEVEL,
#ifdef IPV6_IPCOMP_LEVEL
		    IPV6_IPCOMP_LEVEL
#endif
	};

	switch (sa->sa_family) {
	case AF_INET:
		a = a4;
		break;
	case AF_INET6:
		a = a6;
		break;
	default:
		log_warn("%s: invalid address family", __func__);
		return (-1);
	}

	v = IPSEC_LEVEL_BYPASS;
	if (setsockopt(s, a[0], a[1], &v, sizeof(v)) == -1) {
		log_warn("%s: AUTH_LEVEL", __func__);
		return (-1);
	}
	if (setsockopt(s, a[0], a[2], &v, sizeof(v)) == -1) {
		log_warn("%s: ESP_TRANS_LEVEL", __func__);
		return (-1);
	}
	if (setsockopt(s, a[0], a[3], &v, sizeof(v)) == -1) {
		log_warn("%s: ESP_NETWORK_LEVEL", __func__);
		return (-1);
	}
#ifdef IP_IPCOMP_LEVEL
	if (setsockopt(s, a[0], a[4], &v, sizeof(v)) == -1) {
		log_warn("%s: IPCOMP_LEVEL", __func__);
		return (-1);
	}
#endif

	return (0);
}

int
udp_bind(struct sockaddr *sa, in_port_t port)
{
	int	 s, val;

	if (socket_af(sa, port) == -1) {
		log_warn("%s: failed to set UDP port", __func__);
		return (-1);
	}

	if ((s = socket(sa->sa_family,
	    SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP)) == -1) {
		log_warn("%s: failed to get UDP socket", __func__);
		return (-1);
	}

	/* Skip IPsec processing (don't encrypt) for IKE messages */
	if (socket_bypass(s, sa) == -1) {
		log_warn("%s: failed to bypass IPsec on IKE socket",
		    __func__);
		goto bad;
	}

	val = 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(int)) == -1) {
		log_warn("%s: failed to set reuseport", __func__);
		goto bad;
	}
	val = 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int)) == -1) {
		log_warn("%s: failed to set reuseaddr", __func__);
		goto bad;
	}

	if (sa->sa_family == AF_INET) {
		val = 1;
		if (setsockopt(s, IPPROTO_IP, IP_RECVDSTADDR,
		    &val, sizeof(int)) == -1) {
			log_warn("%s: failed to set IPv4 packet info",
			    __func__);
			goto bad;
		}
	} else {
		val = 1;
		if (setsockopt(s, IPPROTO_IPV6, IPV6_RECVPKTINFO,
		    &val, sizeof(int)) == -1) {
			log_warn("%s: failed to set IPv6 packet info",
			    __func__);
			goto bad;
		}
	}

	if (bind(s, sa, sa->sa_len) == -1) {
		log_warn("%s: failed to bind UDP socket", __func__);
		goto bad;
	}

	return (s);
 bad:
	close(s);
	return (-1);
}

int
sockaddr_cmp(struct sockaddr *a, struct sockaddr *b, int prefixlen)
{
	struct sockaddr_in	*a4, *b4;
	struct sockaddr_in6	*a6, *b6;
	uint32_t		 av[4], bv[4], mv[4];

	if (a->sa_family == AF_UNSPEC || b->sa_family == AF_UNSPEC)
		return (0);
	else if (a->sa_family > b->sa_family)
		return (1);
	else if (a->sa_family < b->sa_family)
		return (-1);

	if (prefixlen == -1)
		memset(&mv, 0xff, sizeof(mv));

	switch (a->sa_family) {
	case AF_INET:
		a4 = (struct sockaddr_in *)a;
		b4 = (struct sockaddr_in *)b;

		av[0] = a4->sin_addr.s_addr;
		bv[0] = b4->sin_addr.s_addr;
		if (prefixlen != -1)
			mv[0] = prefixlen2mask(prefixlen);

		if ((av[0] & mv[0]) > (bv[0] & mv[0]))
			return (1);
		if ((av[0] & mv[0]) < (bv[0] & mv[0]))
			return (-1);
		break;
	case AF_INET6:
		a6 = (struct sockaddr_in6 *)a;
		b6 = (struct sockaddr_in6 *)b;

		memcpy(&av, &a6->sin6_addr.s6_addr, 16);
		memcpy(&bv, &b6->sin6_addr.s6_addr, 16);
		if (prefixlen != -1)
			prefixlen2mask6(prefixlen, mv);

		if ((av[3] & mv[3]) > (bv[3] & mv[3]))
			return (1);
		if ((av[3] & mv[3]) < (bv[3] & mv[3]))
			return (-1);
		if ((av[2] & mv[2]) > (bv[2] & mv[2]))
			return (1);
		if ((av[2] & mv[2]) < (bv[2] & mv[2]))
			return (-1);
		if ((av[1] & mv[1]) > (bv[1] & mv[1]))
			return (1);
		if ((av[1] & mv[1]) < (bv[1] & mv[1]))
			return (-1);
		if ((av[0] & mv[0]) > (bv[0] & mv[0]))
			return (1);
		if ((av[0] & mv[0]) < (bv[0] & mv[0]))
			return (-1);
		break;
	}

	return (0);
}

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
print_spi(uint64_t spi, int size)
{
	static char		 buf[IKED_CYCLE_BUFFERS][32];
	static int		 i = 0;
	char			*ptr;

	ptr = buf[i];

	switch (size) {
	case 2:
		snprintf(ptr, 32, "0x%04x", (uint16_t)spi);
		break;
	case 4:
		snprintf(ptr, 32, "0x%08x", (uint32_t)spi);
		break;
	case 8:
		snprintf(ptr, 32, "0x%016llx", spi);
		break;
	default:
		snprintf(ptr, 32, "%llu", spi);
		break;
	}

	if (++i >= IKED_CYCLE_BUFFERS)
		i = 0;

	return (ptr);
}

const char *
print_map(unsigned int type, struct iked_constmap *map)
{
	unsigned int		 i;
	static char		 buf[IKED_CYCLE_BUFFERS][32];
	static int		 idx = 0;
	const char		*name = NULL;

	if (idx >= IKED_CYCLE_BUFFERS)
		idx = 0;
	bzero(buf[idx], sizeof(buf[idx]));

	for (i = 0; map[i].cm_name != NULL; i++) {
		if (map[i].cm_type == type)
			name = map[i].cm_name;
	}

	if (name == NULL)
		snprintf(buf[idx], sizeof(buf[idx]), "<UNKNOWN:%u>", type);
	else
		strlcpy(buf[idx], name, sizeof(buf[idx]));

	return (buf[idx++]);
}

void
lc_idtype(char *str)
{
	for (; *str != '\0' && *str != '/'; str++)
		*str = tolower((unsigned char)*str);
}

void
print_hex(const uint8_t *buf, off_t offset, size_t length)
{
	unsigned int	 i;

	if (log_getverbose() < 3 || !length)
		return;

	for (i = 0; i < length; i++) {
		if (i && (i % 4) == 0) {
			if ((i % 32) == 0)
				print_debug("\n");
			else
				print_debug(" ");
		}
		print_debug("%02x", buf[offset + i]);
	}
	print_debug("\n");
}

void
print_hexval(const uint8_t *buf, off_t offset, size_t length)
{
	unsigned int	 i;

	if (log_getverbose() < 2 || !length)
		return;

	print_debug("0x");
	for (i = 0; i < length; i++)
		print_debug("%02x", buf[offset + i]);
	print_debug("\n");
}

void
print_hexbuf(struct ibuf *ibuf)
{
	print_hex(ibuf_data(ibuf), 0, ibuf_size(ibuf));
}

const char *
print_bits(unsigned short v, unsigned char *bits)
{
	static char	 buf[IKED_CYCLE_BUFFERS][BUFSIZ];
	static int	 idx = 0;
	unsigned int	 i, any = 0, j = 0;
	unsigned char	 c;

	if (!bits)
		return ("");

	if (++idx >= IKED_CYCLE_BUFFERS)
		idx = 0;

	bzero(buf[idx], sizeof(buf[idx]));

	bits++;
	while ((i = *bits++)) {
		if (v & (1 << (i-1))) {
			if (any) {
				buf[idx][j++] = ',';
				if (j >= sizeof(buf[idx]))
					return (buf[idx]);
			}
			any = 1;
			for (; (c = *bits) > 32; bits++) {
				buf[idx][j++] = tolower((unsigned char)c);
				if (j >= sizeof(buf[idx]))
					return (buf[idx]);
			}
		} else
			for (; *bits > 32; bits++)
				;
	}

	return (buf[idx]);
}

uint8_t
mask2prefixlen(struct sockaddr *sa)
{
	struct sockaddr_in	*sa_in = (struct sockaddr_in *)sa;
	in_addr_t		 ina = sa_in->sin_addr.s_addr;

	if (ina == 0)
		return (0);
	else
		return (33 - ffs(ntohl(ina)));
}

uint8_t
mask2prefixlen6(struct sockaddr *sa)
{
	struct sockaddr_in6	*sa_in6 = (struct sockaddr_in6 *)sa;
	uint8_t			*ap, *ep;
	unsigned int		 l = 0;

	/*
	 * sin6_len is the size of the sockaddr so substract the offset of
	 * the possibly truncated sin6_addr struct.
	 */
	ap = (uint8_t *)&sa_in6->sin6_addr;
	ep = (uint8_t *)sa_in6 + sa_in6->sin6_len;
	for (; ap < ep; ap++) {
		/* this "beauty" is adopted from sbin/route/show.c ... */
		switch (*ap) {
		case 0xff:
			l += 8;
			break;
		case 0xfe:
			l += 7;
			goto done;
		case 0xfc:
			l += 6;
			goto done;
		case 0xf8:
			l += 5;
			goto done;
		case 0xf0:
			l += 4;
			goto done;
		case 0xe0:
			l += 3;
			goto done;
		case 0xc0:
			l += 2;
			goto done;
		case 0x80:
			l += 1;
			goto done;
		case 0x00:
			goto done;
		default:
			fatalx("non contiguous inet6 netmask");
		}
	}

done:
	if (l > sizeof(struct in6_addr) * 8)
		fatalx("%s: prefixlen %d out of bound", __func__, l);
	return (l);
}

uint32_t
prefixlen2mask(uint8_t prefixlen)
{
	if (prefixlen == 0)
		return (0);

	if (prefixlen > 32)
		prefixlen = 32;

	return (htonl(0xffffffff << (32 - prefixlen)));
}

struct in6_addr *
prefixlen2mask6(uint8_t prefixlen, uint32_t *mask)
{
	static struct in6_addr  s6;
	int			i;

	if (prefixlen > 128)
		prefixlen = 128;

	bzero(&s6, sizeof(s6));
	for (i = 0; i < prefixlen / 8; i++)
		s6.s6_addr[i] = 0xff;
	i = prefixlen % 8;
	if (i)
		s6.s6_addr[prefixlen / 8] = 0xff00 >> i;

	memcpy(mask, &s6, sizeof(s6));

	return (&s6);
}

const char *
print_addr(void *addr)
{
	static char	 sbuf[IKED_CYCLE_BUFFERS][NI_MAXHOST + 9];
	static int	 idx;
	struct sockaddr	*sa = addr;
	char		*buf, *hbuf;
	size_t		 len, hlen;
	char		 pbuf[7];
	in_port_t	 port;

	hbuf = buf = sbuf[idx];
	hlen = len = sizeof(sbuf[idx]);
	if (++idx >= IKED_CYCLE_BUFFERS)
		idx = 0;

	if (sa->sa_family == AF_UNSPEC) {
		strlcpy(buf, "any", len);
		return (buf);
	}

	if ((port = socket_getport(sa)) != 0 && sa->sa_family == AF_INET6) {
		/* surround [] */
		*(hbuf++) = '[';
		hlen--;
	}

	if (getnameinfo(sa, sa->sa_len,
	    hbuf, hlen, NULL, 0, NI_NUMERICHOST) != 0) {
		strlcpy(buf, "unknown", len);
		return (buf);
	}

	if (port != 0) {
		if (sa->sa_family == AF_INET6)
			(void)strlcat(buf, "]", len);
		snprintf(pbuf, sizeof(pbuf), ":%d", port);
		(void)strlcat(buf, pbuf, len);
	}

	return (buf);
}

char *
get_string(uint8_t *ptr, size_t len)
{
	size_t	 i;

	for (i = 0; i < len; i++)
		if (!isprint(ptr[i]))
			break;

	return strndup(ptr, i);
}

const char *
print_proto(uint8_t proto)
{
	struct protoent *p;
	static char	 buf[IKED_CYCLE_BUFFERS][BUFSIZ];
	static int	 idx = 0;

	if (idx >= IKED_CYCLE_BUFFERS)
		idx = 0;

	if ((p = getprotobynumber(proto)) != NULL)
		strlcpy(buf[idx], p->p_name, sizeof(buf[idx]));
	else
		snprintf(buf[idx], sizeof(buf[idx]), "%u", proto);

	return (buf[idx++]);
}

int
expand_string(char *label, size_t len, const char *srch, const char *repl)
{
	char *tmp;
	char *p, *q;

	if ((tmp = calloc(1, len)) == NULL) {
		log_debug("%s: calloc", __func__);
		return (-1);
	}
	p = label;
	while ((q = strstr(p, srch)) != NULL) {
		*q = '\0';
		if ((strlcat(tmp, p, len) >= len) ||
		    (strlcat(tmp, repl, len) >= len)) {
			log_debug("%s: string too long", __func__);
			free(tmp);
			return (-1);
		}
		q += strlen(srch);
		p = q;
	}
	if (strlcat(tmp, p, len) >= len) {
		log_debug("%s: string too long", __func__);
		free(tmp);
		return (-1);
	}
	strlcpy(label, tmp, len);	/* always fits */
	free(tmp);

	return (0);
}

uint8_t *
string2unicode(const char *ascii, size_t *outlen)
{
	uint8_t		*uc = NULL;
	size_t		 i, len = strlen(ascii);

	if ((uc = calloc(1, (len * 2) + 2)) == NULL)
		return (NULL);

	for (i = 0; i < len; i++) {
		/* XXX what about the byte order? */
		uc[i * 2] = ascii[i];
	}
	*outlen = len * 2;

	return (uc);
}

void
print_debug(const char *emsg, ...)
{
	va_list	 ap;

	if (log_getverbose() > 2) {
		va_start(ap, emsg);
		vfprintf(stderr, emsg, ap);
		va_end(ap);
	}
}

void
print_verbose(const char *emsg, ...)
{
	va_list	 ap;

	if (log_getverbose()) {
		va_start(ap, emsg);
		vfprintf(stderr, emsg, ap);
		va_end(ap);
	}
}
