// SPDX-License-Identifier: GPL-2.0-or-later
#include <errno.h>
#include <error.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/types.h>
#include <sys/socket.h>

enum {
	ERN_SUCCESS = 0,
	/* Well defined errors, callers may depend on these */
	ERN_SEND = 1,
	/* Informational, can reorder */
	ERN_HELP,
	ERN_SEND_SHORT,
	ERN_SOCK_CREATE,
	ERN_RESOLVE,
	ERN_CMSG_WR,
};

struct options {
	bool silent_send;
	const char *host;
	const char *service;
	struct {
		unsigned int type;
	} sock;
	struct {
		bool ena;
		unsigned int val;
	} mark;
} opt = {
	.sock = {
		.type	= SOCK_DGRAM,
	},
};

static void __attribute__((noreturn)) cs_usage(const char *bin)
{
	printf("Usage: %s [opts] <dst host> <dst port / service>\n", bin);
	printf("Options:\n"
	       "\t\t-s      Silent send() failures\n"
	       "\t\t-m val  Set SO_MARK with given value\n"
	       "");
	exit(ERN_HELP);
}

static void cs_parse_args(int argc, char *argv[])
{
	char o;

	while ((o = getopt(argc, argv, "sm:")) != -1) {
		switch (o) {
		case 's':
			opt.silent_send = true;
			break;
		case 'm':
			opt.mark.ena = true;
			opt.mark.val = atoi(optarg);
			break;
		}
	}

	if (optind != argc - 2)
		cs_usage(argv[0]);

	opt.host = argv[optind];
	opt.service = argv[optind + 1];
}

static void
cs_write_cmsg(struct msghdr *msg, char *cbuf, size_t cbuf_sz)
{
	struct cmsghdr *cmsg;
	size_t cmsg_len;

	msg->msg_control = cbuf;
	cmsg_len = 0;

	if (opt.mark.ena) {
		cmsg = (struct cmsghdr *)(cbuf + cmsg_len);
		cmsg_len += CMSG_SPACE(sizeof(__u32));
		if (cbuf_sz < cmsg_len)
			error(ERN_CMSG_WR, EFAULT, "cmsg buffer too small");

		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SO_MARK;
		cmsg->cmsg_len = CMSG_LEN(sizeof(__u32));
		*(__u32 *)CMSG_DATA(cmsg) = opt.mark.val;
	}

	if (cmsg_len)
		msg->msg_controllen = cmsg_len;
	else
		msg->msg_control = NULL;
}

int main(int argc, char *argv[])
{
	struct addrinfo hints, *ai;
	struct iovec iov[1];
	struct msghdr msg;
	char cbuf[1024];
	int err;
	int fd;

	cs_parse_args(argc, argv);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = opt.sock.type;

	ai = NULL;
	err = getaddrinfo(opt.host, opt.service, &hints, &ai);
	if (err) {
		fprintf(stderr, "Can't resolve address [%s]:%s: %s\n",
			opt.host, opt.service, strerror(errno));
		return ERN_SOCK_CREATE;
	}

	fd = socket(ai->ai_family, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) {
		fprintf(stderr, "Can't open socket: %s\n", strerror(errno));
		freeaddrinfo(ai);
		return ERN_RESOLVE;
	}

	iov[0].iov_base = "bla";
	iov[0].iov_len = 4;

	memset(&msg, 0, sizeof(msg));
	msg.msg_name = ai->ai_addr;
	msg.msg_namelen = ai->ai_addrlen;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	cs_write_cmsg(&msg, cbuf, sizeof(cbuf));

	err = sendmsg(fd, &msg, 0);
	if (err < 0) {
		if (!opt.silent_send)
			fprintf(stderr, "send failed: %s\n", strerror(errno));
		err = ERN_SEND;
	} else if (err != 4) {
		fprintf(stderr, "short send\n");
		err = ERN_SEND_SHORT;
	} else {
		err = ERN_SUCCESS;
	}

	close(fd);
	freeaddrinfo(ai);
	return err;
}
