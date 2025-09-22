/*	$OpenBSD: bindconnect.c,v 1.5 2024/01/04 00:19:17 bluhm Exp $	*/

/*
 * Copyright (c) 2023-2024 Alexander Bluhm <bluhm@openbsd.org>
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

#include <sys/resource.h>
#include <sys/socket.h>

#include <net/route.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXIMUM(a, b)	((a) > (b) ? (a) : (b))

#define s6_addr8	__u6_addr.__u6_addr8
#define s6_addr16	__u6_addr.__u6_addr16
#define s6_addr32	__u6_addr.__u6_addr32

union sockaddr_union {
	struct sockaddr		su_sa;
	struct sockaddr_in	su_sin;
	struct sockaddr_in6	su_sin6;
};

union inaddr_union {
	struct in_addr	au_inaddr;
	struct in6_addr	au_in6addr;
};

int fd_base;
unsigned int fd_num = 128;
unsigned int run_time = 10;
unsigned int socket_num = 1, close_num = 1, bind_num = 1, connect_num = 1,
    delroute_num = 0;
int reuse_port = 0;
union inaddr_union addr, mask;
int af = AF_INET, type, proto = IPPROTO_UDP, prefix = -1, route_sock = -1;

static void __dead
usage(void)
{
	fprintf(stderr,
	    "bindconnect [-r] [-b bind] [-c connect] [-d delroute] "
	    "[-f family] [-N addr/net] [-n num] [-o close] [-p proto] "
	    "[-s socket] [-t time]\n"
	    "    -b bind      threads binding sockets, default %u\n"
	    "    -c connect   threads connecting sockets, default %u\n"
	    "    -d delroute  threads deleting cloned routes, default %u\n"
	    "    -f family    address family inet or inet6, default inet\n"
	    "    -N addr/net  connect to any address within network\n"
	    "    -n num       number of file descriptors, default %u\n"
	    "    -o close     threads closing sockets, default %u\n"
	    "    -p proto     protocol udp, tcp, name or number, default udp\n"
	    "    -r           set reuse port socket option\n"
	    "    -s socket    threads creating sockets, default %u\n"
	    "    -t time      run time in seconds, default %u\n",
	    bind_num, connect_num, delroute_num, fd_num, close_num,
	    socket_num, run_time);
	exit(2);
}

static void
in_prefixlen2mask(struct in_addr *maskp, int plen)
{
	if (plen == 0)
		maskp->s_addr = 0;
	else
		maskp->s_addr = htonl(0xffffffff << (32 - plen));
}

static void
in6_prefixlen2mask(struct in6_addr *maskp, int len)
{
	u_char maskarray[8] = {0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff};
	int bytelen, bitlen, i;

	bzero(maskp, sizeof(*maskp));
	bytelen = len / 8;
	bitlen = len % 8;
	for (i = 0; i < bytelen; i++)
		maskp->s6_addr[i] = 0xff;
	/* len == 128 is ok because bitlen == 0 then */
	if (bitlen)
		maskp->s6_addr[bytelen] = maskarray[bitlen - 1];
}

static void
fill_sockaddr(union sockaddr_union *su)
{
	memset(su, 0, sizeof(*su));
	su->su_sa.sa_family = af;
	if (af == AF_INET) {
		su->su_sin.sin_len = sizeof(su->su_sin);
		if (prefix >= 0)
			su->su_sin.sin_addr = addr.au_inaddr;
		else
			su->su_sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	}
	if (af == AF_INET6) {
		su->su_sin6.sin6_len = sizeof(su->su_sin6);
		if (prefix >= 0)
			su->su_sin6.sin6_addr = addr.au_in6addr;
		else
			su->su_sin6.sin6_addr = in6addr_loopback;
	}
}

static void
mask_sockaddr(union sockaddr_union *su)
{
	if (af == AF_INET) {
		if (prefix >=0 && prefix != 32) {
			su->su_sin.sin_addr.s_addr &=
			    mask.au_inaddr.s_addr;
			/* do only 8 bits variation, routes should be reused */
			su->su_sin.sin_addr.s_addr |= htonl(255) &
			    ~mask.au_inaddr.s_addr & arc4random();
		}
	}
	if (af == AF_INET6) {
		if (prefix >=0 && prefix != 128) {
			su->su_sin6.sin6_addr.s6_addr32[0] &=
			    mask.au_in6addr.s6_addr32[0];
			su->su_sin6.sin6_addr.s6_addr32[1] &=
			    mask.au_in6addr.s6_addr32[1];
			su->su_sin6.sin6_addr.s6_addr32[2] &=
			    mask.au_in6addr.s6_addr32[2];
			su->su_sin6.sin6_addr.s6_addr32[3] &=
			    mask.au_in6addr.s6_addr32[3];
			/* do only 8 bits variation, routes should be reused */
			su->su_sin6.sin6_addr.s6_addr32[3] |= htonl(255) &
			    ~mask.au_in6addr.s6_addr32[3] & arc4random();
		}
	}
}

static void *
thread_socket(void *arg)
{
	volatile int *run = arg;
	unsigned long count;
	int fd;

	for (count = 0; *run; count++) {
		int opt;

		fd = socket(af, type | SOCK_NONBLOCK, proto);
		if (fd < 0 || !reuse_port)
			continue;
		opt = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
	}

	return (void *)count;
}

static void *
thread_close(void *arg)
{
	volatile int *run = arg;
	unsigned long count;
	int fd;

	for (count = 0; *run; count++) {
		fd = fd_base + arc4random_uniform(fd_num);
		close(fd);
	}

	return (void *)count;
}

static void *
thread_bind(void *arg)
{
	volatile int *run = arg;
	unsigned long count;
	int fd;
	union sockaddr_union su;

	fill_sockaddr(&su);

	for (count = 0; *run; count++) {
		fd = fd_base + arc4random_uniform(fd_num);
		bind(fd, &su.su_sa, su.su_sa.sa_len);
	}

	return (void *)count;
}

static void *
thread_connect(void *arg)
{
	volatile int *run = arg;
	unsigned long count;
	int fd;
	union sockaddr_union su;

	fill_sockaddr(&su);

	for (count = 0; *run; count++) {
		fd = fd_base + arc4random_uniform(fd_num);
		mask_sockaddr(&su);
		if (af == AF_INET)
			su.su_sin.sin_port = arc4random();
		if (af == AF_INET6)
			su.su_sin6.sin6_port = arc4random();
		connect(fd, &su.su_sa, su.su_sa.sa_len);
	}

	return (void *)count;
}

static void *
thread_delroute(void *arg)
{
	volatile int *run = arg;
	unsigned long count;
	int seq = 0;
	struct {
		struct rt_msghdr	m_rtm;
		char			m_space[512];
	} m_rtmsg;
	union sockaddr_union su;

#define rtm \
	m_rtmsg.m_rtm
#define ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))
#define ADVANCE(x, n) \
	(x += ROUNDUP((n)->sa_len))
#define NEXTADDR(w, sa)				\
	if (rtm.rtm_addrs & (w)) {		\
		int l = ROUNDUP((sa)->sa_len);	\
		memcpy(cp, (sa), l);		\
		cp += l;			\
	}

	memset(&m_rtmsg, 0, sizeof(m_rtmsg));
	rtm.rtm_type = RTM_DELETE;
	rtm.rtm_flags = RTF_HOST;
	rtm.rtm_version = RTM_VERSION;
	rtm.rtm_addrs = RTA_DST;
	rtm.rtm_hdrlen = sizeof(rtm);

	fill_sockaddr(&su);

	for (count = 0; *run; count++) {
		char *cp = m_rtmsg.m_space;

		rtm.rtm_seq = ++seq;
		mask_sockaddr(&su);
		NEXTADDR(RTA_DST, &su.su_sa);
		rtm.rtm_msglen = cp - (char *)&m_rtmsg;
		write(route_sock, &m_rtmsg, rtm.rtm_msglen);
	}

#undef rtm
#undef ROUNDUP
#undef ADVANCE
#undef NEXTADDR

	return (void *)count;
}

int
main(int argc, char *argv[])
{
	struct rlimit rlim;
	struct protoent *pent;
	pthread_t *tsocket, *tclose, *tbind, *tconnect, *tdelroute;
	const char *errstr, *addr_net = NULL;
	char buf[128], *p;
	int ch, run;
	unsigned int n;
	unsigned long socket_count, close_count, bind_count, connect_count,
	    delroute_count;
	union sockaddr_union su;

	while ((ch = getopt(argc, argv, "b:c:d:f:N:n:o:p:rs:t:")) != -1) {
		switch (ch) {
		case 'b':
			bind_num = strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "bind is %s: %s", errstr, optarg);
			break;
		case 'c':
			connect_num = strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "connect is %s: %s", errstr, optarg);
			break;
		case 'd':
			delroute_num = strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "delroute is %s: %s", errstr, optarg);
			break;
		case 'f':
			if (strcmp(optarg, "inet") == 0)
				af = AF_INET;
			else if (strcmp(optarg, "inet6") == 0)
				af = AF_INET6;
			else
				errx(1, "bad address family %s", optarg);
			break;
		case 'N':
			addr_net = optarg;
			break;
		case 'n':
			fd_num = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "num is %s: %s", errstr, optarg);
			break;
		case 'o':
			close_num = strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "close is %s: %s", errstr, optarg);
			break;
		case 'p':
			pent = getprotobyname(optarg);
			if (pent != NULL) {
				proto = pent->p_proto;
				break;
			}
			proto = strtonum(optarg, 0, IPPROTO_MAX -1 , &errstr);
			if (errstr != NULL)
				errx(1, "proto is %s: %s", errstr, optarg);
			break;
		case 'r':
			reuse_port = 1;
			break;
		case 's':
			socket_num = strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "socket is %s: %s", errstr, optarg);
			break;
		case 't':
			run_time = strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "time is %s: %s", errstr, optarg);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 0)
		usage();

	/* split addr/net into addr, mask, prefix */
	if (addr_net != NULL) {
		prefix = inet_net_pton(af, addr_net, &addr, sizeof(addr));
		if (prefix < 0)
			err(1, "inet_net_pton %s", addr_net);
		if (af == AF_INET6) {
			/*
			 * Man page says inet_net_pton() preserves lower
			 * bits.  That is not true, call inet_pton() again.
			 */
			if (strlcpy(buf, addr_net, sizeof(buf)) >= sizeof(buf))
				err(1, "strlcpy %s", addr_net);
			p = strchr(buf, '/');
			if (p != NULL ) {
				*p = '\0';
				if (inet_pton(af, buf, &addr) < 0)
					err(1, "inet_pton %s", buf);
			}
		}
		if (af == AF_INET)
			in_prefixlen2mask(&mask.au_inaddr, prefix);
		if (af == AF_INET6)
			in6_prefixlen2mask(&mask.au_in6addr, prefix);
	}

	/* preopen route socket before file descriptor limits are set */
	if (delroute_num > 0) {
		if (prefix < 0 || prefix == 32)
			errx(1, "delroute %u needs addr/net", delroute_num);
		route_sock = socket(AF_ROUTE, SOCK_RAW, af);
		if (route_sock < 0)
			err(1, "socket route");
		if (shutdown(route_sock, SHUT_RD) < 0)
			err(1, "shutdown read route");
	}

	/* detect lowest file desciptor, test bind, close everything above */
	switch (proto) {
	case IPPROTO_TCP:
		type = SOCK_STREAM;
		break;
	case IPPROTO_UDP:
		type = SOCK_DGRAM;
		break;
	default:
		type = SOCK_RAW;
		break;
	}
	fd_base = socket(af, type, proto);
	if (fd_base < 0)
		err(1, "socket fd_base");
	if (fd_base > INT_MAX - (int)fd_num)
		err(1, "fd base %d and num %u overflow", fd_base, fd_num);
	fill_sockaddr(&su);
	if (bind(fd_base, &su.su_sa, su.su_sa.sa_len) < 0)
		err(1, "bind %s", inet_ntop(af, &addr, buf, sizeof(buf)));
	if (closefrom(fd_base) < 0)
		err(1, "closefrom %d", fd_base);

	if (getrlimit(RLIMIT_NOFILE, &rlim) < 0)
		err(1, "getrlimit");
	rlim.rlim_max = MAXIMUM(rlim.rlim_max, fd_base + fd_num);
	rlim.rlim_cur = fd_base + fd_num;
	if (setrlimit(RLIMIT_NOFILE, &rlim) < 0)
		err(1, "setrlimit %llu", rlim.rlim_cur);

	run = 1;

	tsocket = calloc(socket_num, sizeof(pthread_t));
	if (tsocket == NULL)
		err(1, "tsocket");
	for (n = 0; n < socket_num; n++) {
		errno = pthread_create(&tsocket[n], NULL, thread_socket, &run);
		if (errno)
			err(1, "pthread_create socket %u", n);
	}

	tclose = calloc(close_num, sizeof(pthread_t));
	if (tclose == NULL)
		err(1, "tclose");
	for (n = 0; n < close_num; n++) {
		errno = pthread_create(&tclose[n], NULL, thread_close, &run);
		if (errno)
			err(1, "pthread_create close %u", n);
	}

	tbind = calloc(bind_num, sizeof(pthread_t));
	if (tbind == NULL)
		err(1, "tbind");
	for (n = 0; n < bind_num; n++) {
		errno = pthread_create(&tbind[n], NULL, thread_bind, &run);
		if (errno)
			err(1, "pthread_create bind %u", n);
	}

	tconnect = calloc(connect_num, sizeof(pthread_t));
	if (tconnect == NULL)
		err(1, "tconnect");
	for (n = 0; n < connect_num; n++) {
		errno = pthread_create(&tconnect[n], NULL, thread_connect,
		    &run);
		if (errno)
			err(1, "pthread_create connect %u", n);
	}

	tdelroute = calloc(delroute_num, sizeof(pthread_t));
	if (tdelroute == NULL)
		err(1, "tdelroute");
	for (n = 0; n < delroute_num; n++) {
		errno = pthread_create(&tdelroute[n], NULL, thread_delroute,
		    &run);
		if (errno)
			err(1, "pthread_create delroute %u", n);
	}

	if (run_time > 0) {
		if (sleep(run_time) < 0)
			err(1, "sleep %u", run_time);
	}

	run = 0;
	socket_count = 0;
	for (n = 0; n < socket_num; n++) {
		unsigned long count;

		errno = pthread_join(tsocket[n], (void **)&count);
		if (errno)
			err(1, "pthread_join socket %u", n);
		socket_count += count;
	}
	free(tsocket);

	close_count = 0;
	for (n = 0; n < close_num; n++) {
		unsigned long count;

		errno = pthread_join(tclose[n], (void **)&count);
		if (errno)
			err(1, "pthread_join close %u", n);
		close_count += count;
	}
	free(tclose);

	bind_count = 0;
	for (n = 0; n < bind_num; n++) {
		unsigned long count;

		errno = pthread_join(tbind[n], (void **)&count);
		if (errno)
			err(1, "pthread_join bind %u", n);
		bind_count += count;
	}
	free(tbind);

	connect_count = 0;
	for (n = 0; n < connect_num; n++) {
		unsigned long count;

		errno = pthread_join(tconnect[n], (void **)&count);
		if (errno)
			err(1, "pthread_join connect %u", n);
		connect_count += count;
	}
	free(tconnect);

	delroute_count = 0;
	for (n = 0; n < delroute_num; n++) {
		unsigned long count;

		errno = pthread_join(tdelroute[n], (void **)&count);
		if (errno)
			err(1, "pthread_join delroute %u", n);
		delroute_count += count;
	}
	free(tdelroute);

	printf("count: socket %lu, close %lu, bind %lu, connect %lu, "
	    "delroute %lu\n",
	    socket_count, close_count, bind_count, connect_count,
	    delroute_count);

	return 0;
}
