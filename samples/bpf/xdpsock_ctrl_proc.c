// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2018 Intel Corporation. */

#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <bpf/bpf.h>
#include <bpf/xsk.h>
#include "xdpsock.h"

static const char *opt_if = "";

static struct option long_options[] = {
	{"interface", required_argument, 0, 'i'},
	{0, 0, 0, 0}
};

static void usage(const char *prog)
{
	const char *str =
		"  Usage: %s [OPTIONS]\n"
		"  Options:\n"
		"  -i, --interface=n	Run on interface n\n"
		"\n";
	fprintf(stderr, "%s\n", str);

	exit(0);
}

static void parse_command_line(int argc, char **argv)
{
	int option_index, c;

	opterr = 0;

	for (;;) {
		c = getopt_long(argc, argv, "i:",
				long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'i':
			opt_if = optarg;
			break;
		default:
			usage(basename(argv[0]));
		}
	}
}

static int send_xsks_map_fd(int sock, int fd)
{
	char cmsgbuf[CMSG_SPACE(sizeof(int))];
	struct msghdr msg;
	struct iovec iov;
	int value = 0;

	if (fd == -1) {
		fprintf(stderr, "Incorrect fd = %d\n", fd);
		return -1;
	}
	iov.iov_base = &value;
	iov.iov_len = sizeof(int);

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;
	msg.msg_control = cmsgbuf;
	msg.msg_controllen = CMSG_LEN(sizeof(int));

	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);

	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));

	*(int *)CMSG_DATA(cmsg) = fd;
	int ret = sendmsg(sock, &msg, 0);

	if (ret == -1) {
		fprintf(stderr, "Sendmsg failed with %s", strerror(errno));
		return -errno;
	}

	return ret;
}

int
main(int argc, char **argv)
{
	struct sockaddr_un server;
	int listening = 1;
	int rval, msgsock;
	int ifindex = 0;
	int flag = 1;
	int cmd = 0;
	int sock;
	int err;
	int xsks_map_fd;

	parse_command_line(argc, argv);

	ifindex = if_nametoindex(opt_if);
	if (ifindex == 0) {
		fprintf(stderr, "Unable to get ifindex for Interface %s. Reason:%s",
			opt_if, strerror(errno));
		return -errno;
	}

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		fprintf(stderr, "Opening socket stream failed: %s", strerror(errno));
		return -errno;
	}

	server.sun_family = AF_UNIX;
	strcpy(server.sun_path, SOCKET_NAME);

	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int));

	if (bind(sock, (struct sockaddr *)&server, sizeof(struct sockaddr_un))) {
		fprintf(stderr, "Binding to socket stream failed: %s", strerror(errno));
		return -errno;
	}

	listen(sock, MAX_NUM_OF_CLIENTS);

	err = xsk_setup_xdp_prog(ifindex, &xsks_map_fd);
	if (err) {
		fprintf(stderr, "Setup of xdp program failed\n");
		goto close_sock;
	}

	while (listening) {
		msgsock = accept(sock, 0, 0);
		if (msgsock == -1) {
			fprintf(stderr, "Error accepting connection: %s", strerror(errno));
			err = -errno;
			goto close_sock;
		}
		err = send_xsks_map_fd(msgsock, xsks_map_fd);
		if (err <= 0) {
			fprintf(stderr, "Error %d sending xsks_map_fd\n", err);
			goto cleanup;
		}
		do {
			rval = read(msgsock, &cmd, sizeof(int));
			if (rval < 0) {
				fprintf(stderr, "Error reading stream message");
			} else {
				if (cmd != CLOSE_CONN)
					fprintf(stderr, "Recv unknown cmd = %d\n", cmd);
				listening = 0;
				break;
			}
		} while (rval > 0);
	}
	close(msgsock);
	close(sock);
	unlink(SOCKET_NAME);

	/* Unset fd for given ifindex */
	err = bpf_set_link_xdp_fd(ifindex, -1, 0);
	if (err) {
		fprintf(stderr, "Error when unsetting bpf prog_fd for ifindex(%d)\n", ifindex);
		return err;
	}

	return 0;

cleanup:
	close(msgsock);
close_sock:
	close(sock);
	unlink(SOCKET_NAME);
	return err;
}
