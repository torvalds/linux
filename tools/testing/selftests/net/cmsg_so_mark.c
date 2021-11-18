// SPDX-License-Identifier: GPL-2.0-or-later
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/types.h>
#include <sys/socket.h>

int main(int argc, const char **argv)
{
	char cbuf[CMSG_SPACE(sizeof(__u32))];
	struct addrinfo hints, *ai;
	struct cmsghdr *cmsg;
	struct iovec iov[1];
	struct msghdr msg;
	int mark;
	int err;
	int fd;

	if (argc != 4) {
		fprintf(stderr, "Usage: %s <dst_ip> <port> <mark>\n", argv[0]);
		return 1;
	}
	mark = atoi(argv[3]);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	ai = NULL;
	err = getaddrinfo(argv[1], argv[2], &hints, &ai);
	if (err) {
		fprintf(stderr, "Can't resolve address: %s\n", strerror(errno));
		return 1;
	}

	fd = socket(ai->ai_family, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) {
		fprintf(stderr, "Can't open socket: %s\n", strerror(errno));
		freeaddrinfo(ai);
		return 1;
	}

	iov[0].iov_base = "bla";
	iov[0].iov_len = 4;

	msg.msg_name = ai->ai_addr;
	msg.msg_namelen = ai->ai_addrlen;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cbuf;
	msg.msg_controllen = sizeof(cbuf);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SO_MARK;
	cmsg->cmsg_len = CMSG_LEN(sizeof(__u32));
	*(__u32 *)CMSG_DATA(cmsg) = mark;

	err = sendmsg(fd, &msg, 0);

	close(fd);
	freeaddrinfo(ai);
	return err != 4;
}
