/*	$OpenBSD: cmsgsize.c,v 1.4 2024/08/23 12:56:26 anton Exp $ */
/*
 * Copyright (c) 2017 Alexander Markert <alexander.markert@siemens.com>
 * Copyright (c) 2018 Alexander Bluhm <bluhm@openbsd.org>
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
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CFG_PORT			5000
#define CFG_SO_MAX_SEND_BUFFER		1024

char payload[CFG_SO_MAX_SEND_BUFFER];

int test_cmsgsize(int, struct in_addr *, struct in_addr *,
    unsigned int, unsigned int);

int
main(int argc, char *argv[])
{
	int so, bytes;
	struct in_addr src, dst;

	if (argc != 3)
		errx(2, "usage: %s <source_address> <destination_address>",
		    argv[0]);

	if (inet_pton(AF_INET, argv[1], &src) != 1)
		err(1, "unable to parse source address");
	if (inet_pton(AF_INET, argv[2], &dst) != 1)
		err(1, "unable to parse destination address");

	/* 1: !blocking, cmsg + payload > sndbufsize => EMSGSIZE */
	so = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
	if (so < 0)
		err(1, "1: socket");
	bytes = test_cmsgsize(so, &src, &dst, CFG_SO_MAX_SEND_BUFFER,
	    CFG_SO_MAX_SEND_BUFFER);
	if (bytes >= 0)
		errx(1, "1: %d bytes sent", bytes);
	if (errno != EMSGSIZE)
		err(-1, "1: incorrect errno");
	close(so);

	/* 2: blocking, cmsg + payload > sndbufsize => EMSGSIZE */
	so = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (so < 0)
		err(1, "2: socket");
	bytes = test_cmsgsize(so, &src, &dst, CFG_SO_MAX_SEND_BUFFER,
	    CFG_SO_MAX_SEND_BUFFER);
	if (bytes >= 0)
		errx(1, "2: %d bytes sent", bytes);
	if (errno != EMSGSIZE)
		err(-1, "2: incorrect errno");
	close(so);

	/* 3: !blocking, cmsg + payload < sndbufsize => OK */
	so = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
	if (so < 0)
		err(1, "3: socket 3");
	bytes = test_cmsgsize(so, &src, &dst, CFG_SO_MAX_SEND_BUFFER,
	    CFG_SO_MAX_SEND_BUFFER/2);
	if (bytes < 0)
		err(1, "3: got errno");
	close(so);

	/* 4: blocking, cmsg + payload < sndbufsize => OK */
	so = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (so < 0)
		err(1, "4: socket");
	bytes = test_cmsgsize(so, &src, &dst, CFG_SO_MAX_SEND_BUFFER,
	    CFG_SO_MAX_SEND_BUFFER/2);
	if (bytes < 0)
		err(4, "3: got errno");
	close(so);

	return 0;
}

int
test_cmsgsize(int so, struct in_addr *src, struct in_addr *dst,
    unsigned int sndbuf_size, unsigned int payload_size)
{
	char cmsgbuf[CMSG_SPACE(sizeof(struct in_addr))];
	struct sockaddr_in to;
	struct in_addr *source_address;
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec iov;

	if (setsockopt(so, SOL_SOCKET, SO_SNDBUF, &sndbuf_size,
	    sizeof(sndbuf_size)) < 0)
		err(1, "setsockopt send buffer");

	/* setup remote address */
	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_addr = *dst;
	to.sin_port = htons(CFG_PORT);

	/* setup buffer to be sent */
	memset(&msg, 0, sizeof(msg));
	msg.msg_name = &to;
	msg.msg_namelen = sizeof(to);
	iov.iov_base = payload;
	iov.iov_len = payload_size;
	msg.msg_iovlen = 1;
	msg.msg_iov = &iov;

	/* setup configuration for source address */
	memset(cmsgbuf, 0, sizeof(cmsgbuf));
	msg.msg_control = cmsgbuf;
	msg.msg_controllen = sizeof(cmsgbuf);
	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = IPPROTO_IP;
	cmsg->cmsg_type = IP_SENDSRCADDR;
	cmsg->cmsg_len = CMSG_LEN(sizeof(struct in_addr));
	source_address = (struct in_addr *)(CMSG_DATA(cmsg));
	memcpy(source_address, src, sizeof(struct in_addr));

	return sendmsg(so, &msg, 0);
}
