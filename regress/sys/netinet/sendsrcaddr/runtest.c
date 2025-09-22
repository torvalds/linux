/*
 * Copyright (c) 2016 Vincent Gross <vincent.gross@kilob.yt>
 * Copyright (c) 2017 Alexander Bluhm <bluhm@openbsd.org>
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
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define PAYLOAD "payload"

int fuzzit;

void __dead usage(const char *);
int udp_bind(struct sockaddr_in *);
int udp_send(int, struct sockaddr_in *, struct sockaddr_in *);
struct sockaddr_in * udp_recv(int s, struct sockaddr_in *);

void __dead
usage(const char *msg)
{
	if (msg != NULL)
		fprintf(stderr, "%s\n", msg);
	fprintf(stderr, "runtest [-f] -D destination -B bind [-C cmesg] "
	    "[-E error] -R reserved -W wire\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int ch, error, errexpect, bind_sock, dest_sock, resv_sock;
	char addr[16];
	const char *errstr;
	struct addrinfo hints, *res;
	struct sockaddr_in *bind_sin, *cmsg_sin, *dest_sin, *resv_sin,
	    *wire_sin, *from_sin;

	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	bind_sin = cmsg_sin = dest_sin = resv_sin = wire_sin = NULL;
	errexpect = 0;

	while ((ch = getopt(argc, argv, "B:C:D:E:fR:W:")) != -1) {
		switch (ch) {
		case 'B':
			error = getaddrinfo(optarg, NULL, &hints, &res);
			if (error)
				errx(1, "-B: %s", gai_strerror(error));
			bind_sin = (struct sockaddr_in *)res->ai_addr;
			break;
		case 'C':
			error = getaddrinfo(optarg, NULL, &hints, &res);
			if (error)
				errx(1, "-C: %s", gai_strerror(error));
			cmsg_sin = (struct sockaddr_in *)res->ai_addr;
			break;
		case 'D':
			error = getaddrinfo(optarg, NULL, &hints, &res);
			if (error)
				errx(1, "-D: %s", gai_strerror(error));
			dest_sin = (struct sockaddr_in *)res->ai_addr;
			break;
		case 'E':
			errexpect = strtonum(optarg, 1, 255, &errstr);
			if (errstr != NULL)
				errx(1, "error number is %s: %s",
				    errstr, optarg);
			break;
		case 'f':
			fuzzit = 1;
			break;
		case 'R':
			error = getaddrinfo(optarg, NULL, &hints, &res);
			if (error)
				errx(1, "-R: %s", gai_strerror(error));
			resv_sin = (struct sockaddr_in *)res->ai_addr;
			break;
		case 'W':
			error = getaddrinfo(optarg, NULL, &hints, &res);
			if (error)
				errx(1, "-W: %s", gai_strerror(error));
			wire_sin = (struct sockaddr_in *)res->ai_addr;
			break;
		default:
			usage(NULL);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 0)
		usage("too many arguments");

	if (bind_sin == NULL)
		usage("no bind addr");

	if (dest_sin == NULL)
		usage("no destination addr");

	if (resv_sin == NULL)
		usage("no reserved addr");

	/* bind on address that cannot be used */
	resv_sock = udp_bind(resv_sin);

	/* bind socket that should receive the packet */
	dest_sock = udp_bind(dest_sin);

	/* bind socket that is used to send the packet */
	bind_sin->sin_port = resv_sin->sin_port;
	bind_sock = udp_bind(bind_sin);
	error = udp_send(bind_sock, cmsg_sin, dest_sin);

	if (errexpect && !error) {
		errno = errexpect;
		err(2, "udp send succeeded, but expected error");
	}
	if (!errexpect && error) {
		errno = error;
		err(2, "no error expected, but udp send failed");
	}
	if (errexpect != error) {
		errno = error;
		err(2, "expected error %d, but udp send failed", errexpect);
	}

	if (wire_sin != NULL) {
		from_sin = udp_recv(dest_sock, dest_sin);
		if (from_sin == NULL)
			errx(2, "receive timeout");
		inet_ntop(from_sin->sin_family, &from_sin->sin_addr,
		    addr, sizeof(addr));
		if (from_sin->sin_addr.s_addr != wire_sin->sin_addr.s_addr)
			errx(2, "receive addr %s", addr);
		if (from_sin->sin_port != bind_sin->sin_port)
			errx(2, "receive port %d", ntohs(from_sin->sin_port));
	}

	return 0;
}

int
udp_bind(struct sockaddr_in *src)
{
	int s, reuse, salen;
	char addr[16];

	inet_ntop(src->sin_family, &src->sin_addr, addr, sizeof(addr));

	if ((s = socket(src->sin_family, SOCK_DGRAM, 0)) == -1)
		err(1, "socket %s", addr);
	reuse = ntohl(src->sin_addr.s_addr) == INADDR_ANY ? 1 : 0;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))
	    == -1)
		err(1, "setsockopt %s", addr);
	if (bind(s, (struct sockaddr *)src, src->sin_len) == -1)
		err(1, "bind %s", addr);
	/* fill out port */
	salen = sizeof(*src);
	if (getsockname(s, (struct sockaddr *)src, &salen))
		err(1, "getsockname %s", addr);

	return s;
}

int
udp_send(int s, struct sockaddr_in *src, struct sockaddr_in *dst)
{
	struct msghdr		 msg;
	struct iovec		 iov;
	struct cmsghdr		*cmsg;
	struct in_addr		*sendopt;
	int			*hopopt;
#define CMSGSP_SADDR	CMSG_SPACE(sizeof(u_int32_t))
#define CMSGSP_HOPLIM	CMSG_SPACE(sizeof(int))
#define CMSGSP_BOGUS	CMSG_SPACE(12)
#define CMSGBUF_SP	CMSGSP_SADDR + CMSGSP_HOPLIM + CMSGSP_BOGUS + 3
	unsigned char		 cmsgbuf[CMSGBUF_SP];

	iov.iov_base = PAYLOAD;
	iov.iov_len = strlen(PAYLOAD) + 1;
	bzero(&msg, sizeof(msg));
	msg.msg_name = dst;
	msg.msg_namelen = dst->sin_len;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	if (src) {
		bzero(&cmsgbuf, sizeof(cmsgbuf));
		msg.msg_control = &cmsgbuf;
		msg.msg_controllen = CMSGSP_SADDR;
		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct in_addr));
		cmsg->cmsg_level = IPPROTO_IP;
		cmsg->cmsg_type = IP_SENDSRCADDR;
		sendopt = (struct in_addr *)CMSG_DATA(cmsg);
		memcpy(sendopt, &src->sin_addr, sizeof(*sendopt));
		if (fuzzit) {
			msg.msg_controllen = CMSGBUF_SP;
			cmsg = CMSG_NXTHDR(&msg, cmsg);
			cmsg->cmsg_len = CMSG_LEN(sizeof(int));
			cmsg->cmsg_level = IPPROTO_IPV6;
			cmsg->cmsg_type = IPV6_UNICAST_HOPS;
			hopopt = (int *)CMSG_DATA(cmsg);
			*hopopt = 8;

			cmsg = CMSG_NXTHDR(&msg, cmsg);
			cmsg->cmsg_len = CMSG_LEN(sizeof(int)) + 15;
			cmsg->cmsg_level = IPPROTO_IPV6;
			cmsg->cmsg_type = IPV6_UNICAST_HOPS;
		}
	}

	if (sendmsg(s, &msg, 0) == -1)
		return errno;

	return 0;
}

struct sockaddr_in *
udp_recv(int s, struct sockaddr_in *dst)
{
	struct sockaddr_in *src;
	struct pollfd pfd[1];
	char addr[16], buf[256];
	int nready, len, salen;

	inet_ntop(dst->sin_family, &dst->sin_addr, addr, sizeof(addr));

	pfd[0].fd = s;
	pfd[0].events = POLLIN;
	nready = poll(pfd, 1, 2 * 1000);
	if (nready == -1)
		err(1, "poll");
	if (nready == 0)
		return NULL;
	if ((pfd[0].revents & POLLIN) == 0)
		errx(1, "event %d %s", pfd[0].revents, addr);

	if ((src = malloc(sizeof(*src))) == NULL)
		err(1, "malloc");
	salen = sizeof(*src);
	if ((len = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)src,
	    &salen)) == -1)
		err(1, "recvfrom %s", addr);

	if (len != strlen(PAYLOAD) + 1)
		errx(1, "recvfrom %s len %d", addr, len);
	if (strcmp(buf, PAYLOAD) != 0)
		errx(1, "recvfrom %s payload", addr);

	return src;
}
