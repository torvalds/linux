/*
 * vsock_diag_test - vsock_diag.ko test suite
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * Author: Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <getopt.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/list.h>
#include <linux/net.h>
#include <linux/netlink.h>
#include <linux/sock_diag.h>
#include <netinet/tcp.h>

#include "../../../include/uapi/linux/vm_sockets.h"
#include "../../../include/uapi/linux/vm_sockets_diag.h"

#include "timeout.h"
#include "control.h"

enum test_mode {
	TEST_MODE_UNSET,
	TEST_MODE_CLIENT,
	TEST_MODE_SERVER
};

/* Per-socket status */
struct vsock_stat {
	struct list_head list;
	struct vsock_diag_msg msg;
};

static const char *sock_type_str(int type)
{
	switch (type) {
	case SOCK_DGRAM:
		return "DGRAM";
	case SOCK_STREAM:
		return "STREAM";
	default:
		return "INVALID TYPE";
	}
}

static const char *sock_state_str(int state)
{
	switch (state) {
	case TCP_CLOSE:
		return "UNCONNECTED";
	case TCP_SYN_SENT:
		return "CONNECTING";
	case TCP_ESTABLISHED:
		return "CONNECTED";
	case TCP_CLOSING:
		return "DISCONNECTING";
	case TCP_LISTEN:
		return "LISTEN";
	default:
		return "INVALID STATE";
	}
}

static const char *sock_shutdown_str(int shutdown)
{
	switch (shutdown) {
	case 1:
		return "RCV_SHUTDOWN";
	case 2:
		return "SEND_SHUTDOWN";
	case 3:
		return "RCV_SHUTDOWN | SEND_SHUTDOWN";
	default:
		return "0";
	}
}

static void print_vsock_addr(FILE *fp, unsigned int cid, unsigned int port)
{
	if (cid == VMADDR_CID_ANY)
		fprintf(fp, "*:");
	else
		fprintf(fp, "%u:", cid);

	if (port == VMADDR_PORT_ANY)
		fprintf(fp, "*");
	else
		fprintf(fp, "%u", port);
}

static void print_vsock_stat(FILE *fp, struct vsock_stat *st)
{
	print_vsock_addr(fp, st->msg.vdiag_src_cid, st->msg.vdiag_src_port);
	fprintf(fp, " ");
	print_vsock_addr(fp, st->msg.vdiag_dst_cid, st->msg.vdiag_dst_port);
	fprintf(fp, " %s %s %s %u\n",
		sock_type_str(st->msg.vdiag_type),
		sock_state_str(st->msg.vdiag_state),
		sock_shutdown_str(st->msg.vdiag_shutdown),
		st->msg.vdiag_ino);
}

static void print_vsock_stats(FILE *fp, struct list_head *head)
{
	struct vsock_stat *st;

	list_for_each_entry(st, head, list)
		print_vsock_stat(fp, st);
}

static struct vsock_stat *find_vsock_stat(struct list_head *head, int fd)
{
	struct vsock_stat *st;
	struct stat stat;

	if (fstat(fd, &stat) < 0) {
		perror("fstat");
		exit(EXIT_FAILURE);
	}

	list_for_each_entry(st, head, list)
		if (st->msg.vdiag_ino == stat.st_ino)
			return st;

	fprintf(stderr, "cannot find fd %d\n", fd);
	exit(EXIT_FAILURE);
}

static void check_no_sockets(struct list_head *head)
{
	if (!list_empty(head)) {
		fprintf(stderr, "expected no sockets\n");
		print_vsock_stats(stderr, head);
		exit(1);
	}
}

static void check_num_sockets(struct list_head *head, int expected)
{
	struct list_head *node;
	int n = 0;

	list_for_each(node, head)
		n++;

	if (n != expected) {
		fprintf(stderr, "expected %d sockets, found %d\n",
			expected, n);
		print_vsock_stats(stderr, head);
		exit(EXIT_FAILURE);
	}
}

static void check_socket_state(struct vsock_stat *st, __u8 state)
{
	if (st->msg.vdiag_state != state) {
		fprintf(stderr, "expected socket state %#x, got %#x\n",
			state, st->msg.vdiag_state);
		exit(EXIT_FAILURE);
	}
}

static void send_req(int fd)
{
	struct sockaddr_nl nladdr = {
		.nl_family = AF_NETLINK,
	};
	struct {
		struct nlmsghdr nlh;
		struct vsock_diag_req vreq;
	} req = {
		.nlh = {
			.nlmsg_len = sizeof(req),
			.nlmsg_type = SOCK_DIAG_BY_FAMILY,
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP,
		},
		.vreq = {
			.sdiag_family = AF_VSOCK,
			.vdiag_states = ~(__u32)0,
		},
	};
	struct iovec iov = {
		.iov_base = &req,
		.iov_len = sizeof(req),
	};
	struct msghdr msg = {
		.msg_name = &nladdr,
		.msg_namelen = sizeof(nladdr),
		.msg_iov = &iov,
		.msg_iovlen = 1,
	};

	for (;;) {
		if (sendmsg(fd, &msg, 0) < 0) {
			if (errno == EINTR)
				continue;

			perror("sendmsg");
			exit(EXIT_FAILURE);
		}

		return;
	}
}

static ssize_t recv_resp(int fd, void *buf, size_t len)
{
	struct sockaddr_nl nladdr = {
		.nl_family = AF_NETLINK,
	};
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = len,
	};
	struct msghdr msg = {
		.msg_name = &nladdr,
		.msg_namelen = sizeof(nladdr),
		.msg_iov = &iov,
		.msg_iovlen = 1,
	};
	ssize_t ret;

	do {
		ret = recvmsg(fd, &msg, 0);
	} while (ret < 0 && errno == EINTR);

	if (ret < 0) {
		perror("recvmsg");
		exit(EXIT_FAILURE);
	}

	return ret;
}

static void add_vsock_stat(struct list_head *sockets,
			   const struct vsock_diag_msg *resp)
{
	struct vsock_stat *st;

	st = malloc(sizeof(*st));
	if (!st) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	st->msg = *resp;
	list_add_tail(&st->list, sockets);
}

/*
 * Read vsock stats into a list.
 */
static void read_vsock_stat(struct list_head *sockets)
{
	long buf[8192 / sizeof(long)];
	int fd;

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_SOCK_DIAG);
	if (fd < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	send_req(fd);

	for (;;) {
		const struct nlmsghdr *h;
		ssize_t ret;

		ret = recv_resp(fd, buf, sizeof(buf));
		if (ret == 0)
			goto done;
		if (ret < sizeof(*h)) {
			fprintf(stderr, "short read of %zd bytes\n", ret);
			exit(EXIT_FAILURE);
		}

		h = (struct nlmsghdr *)buf;

		while (NLMSG_OK(h, ret)) {
			if (h->nlmsg_type == NLMSG_DONE)
				goto done;

			if (h->nlmsg_type == NLMSG_ERROR) {
				const struct nlmsgerr *err = NLMSG_DATA(h);

				if (h->nlmsg_len < NLMSG_LENGTH(sizeof(*err)))
					fprintf(stderr, "NLMSG_ERROR\n");
				else {
					errno = -err->error;
					perror("NLMSG_ERROR");
				}

				exit(EXIT_FAILURE);
			}

			if (h->nlmsg_type != SOCK_DIAG_BY_FAMILY) {
				fprintf(stderr, "unexpected nlmsg_type %#x\n",
					h->nlmsg_type);
				exit(EXIT_FAILURE);
			}
			if (h->nlmsg_len <
			    NLMSG_LENGTH(sizeof(struct vsock_diag_msg))) {
				fprintf(stderr, "short vsock_diag_msg\n");
				exit(EXIT_FAILURE);
			}

			add_vsock_stat(sockets, NLMSG_DATA(h));

			h = NLMSG_NEXT(h, ret);
		}
	}

done:
	close(fd);
}

static void free_sock_stat(struct list_head *sockets)
{
	struct vsock_stat *st;
	struct vsock_stat *next;

	list_for_each_entry_safe(st, next, sockets, list)
		free(st);
}

static void test_no_sockets(unsigned int peer_cid)
{
	LIST_HEAD(sockets);

	read_vsock_stat(&sockets);

	check_no_sockets(&sockets);

	free_sock_stat(&sockets);
}

static void test_listen_socket_server(unsigned int peer_cid)
{
	union {
		struct sockaddr sa;
		struct sockaddr_vm svm;
	} addr = {
		.svm = {
			.svm_family = AF_VSOCK,
			.svm_port = 1234,
			.svm_cid = VMADDR_CID_ANY,
		},
	};
	LIST_HEAD(sockets);
	struct vsock_stat *st;
	int fd;

	fd = socket(AF_VSOCK, SOCK_STREAM, 0);

	if (bind(fd, &addr.sa, sizeof(addr.svm)) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}

	if (listen(fd, 1) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	read_vsock_stat(&sockets);

	check_num_sockets(&sockets, 1);
	st = find_vsock_stat(&sockets, fd);
	check_socket_state(st, TCP_LISTEN);

	close(fd);
	free_sock_stat(&sockets);
}

static void test_connect_client(unsigned int peer_cid)
{
	union {
		struct sockaddr sa;
		struct sockaddr_vm svm;
	} addr = {
		.svm = {
			.svm_family = AF_VSOCK,
			.svm_port = 1234,
			.svm_cid = peer_cid,
		},
	};
	int fd;
	int ret;
	LIST_HEAD(sockets);
	struct vsock_stat *st;

	control_expectln("LISTENING");

	fd = socket(AF_VSOCK, SOCK_STREAM, 0);

	timeout_begin(TIMEOUT);
	do {
		ret = connect(fd, &addr.sa, sizeof(addr.svm));
		timeout_check("connect");
	} while (ret < 0 && errno == EINTR);
	timeout_end();

	if (ret < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	read_vsock_stat(&sockets);

	check_num_sockets(&sockets, 1);
	st = find_vsock_stat(&sockets, fd);
	check_socket_state(st, TCP_ESTABLISHED);

	control_expectln("DONE");
	control_writeln("DONE");

	close(fd);
	free_sock_stat(&sockets);
}

static void test_connect_server(unsigned int peer_cid)
{
	union {
		struct sockaddr sa;
		struct sockaddr_vm svm;
	} addr = {
		.svm = {
			.svm_family = AF_VSOCK,
			.svm_port = 1234,
			.svm_cid = VMADDR_CID_ANY,
		},
	};
	union {
		struct sockaddr sa;
		struct sockaddr_vm svm;
	} clientaddr;
	socklen_t clientaddr_len = sizeof(clientaddr.svm);
	LIST_HEAD(sockets);
	struct vsock_stat *st;
	int fd;
	int client_fd;

	fd = socket(AF_VSOCK, SOCK_STREAM, 0);

	if (bind(fd, &addr.sa, sizeof(addr.svm)) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}

	if (listen(fd, 1) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	control_writeln("LISTENING");

	timeout_begin(TIMEOUT);
	do {
		client_fd = accept(fd, &clientaddr.sa, &clientaddr_len);
		timeout_check("accept");
	} while (client_fd < 0 && errno == EINTR);
	timeout_end();

	if (client_fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}
	if (clientaddr.sa.sa_family != AF_VSOCK) {
		fprintf(stderr, "expected AF_VSOCK from accept(2), got %d\n",
			clientaddr.sa.sa_family);
		exit(EXIT_FAILURE);
	}
	if (clientaddr.svm.svm_cid != peer_cid) {
		fprintf(stderr, "expected peer CID %u from accept(2), got %u\n",
			peer_cid, clientaddr.svm.svm_cid);
		exit(EXIT_FAILURE);
	}

	read_vsock_stat(&sockets);

	check_num_sockets(&sockets, 2);
	find_vsock_stat(&sockets, fd);
	st = find_vsock_stat(&sockets, client_fd);
	check_socket_state(st, TCP_ESTABLISHED);

	control_writeln("DONE");
	control_expectln("DONE");

	close(client_fd);
	close(fd);
	free_sock_stat(&sockets);
}

static struct {
	const char *name;
	void (*run_client)(unsigned int peer_cid);
	void (*run_server)(unsigned int peer_cid);
} test_cases[] = {
	{
		.name = "No sockets",
		.run_server = test_no_sockets,
	},
	{
		.name = "Listen socket",
		.run_server = test_listen_socket_server,
	},
	{
		.name = "Connect",
		.run_client = test_connect_client,
		.run_server = test_connect_server,
	},
	{},
};

static void init_signals(void)
{
	struct sigaction act = {
		.sa_handler = sigalrm,
	};

	sigaction(SIGALRM, &act, NULL);
	signal(SIGPIPE, SIG_IGN);
}

static unsigned int parse_cid(const char *str)
{
	char *endptr = NULL;
	unsigned long int n;

	errno = 0;
	n = strtoul(str, &endptr, 10);
	if (errno || *endptr != '\0') {
		fprintf(stderr, "malformed CID \"%s\"\n", str);
		exit(EXIT_FAILURE);
	}
	return n;
}

static const char optstring[] = "";
static const struct option longopts[] = {
	{
		.name = "control-host",
		.has_arg = required_argument,
		.val = 'H',
	},
	{
		.name = "control-port",
		.has_arg = required_argument,
		.val = 'P',
	},
	{
		.name = "mode",
		.has_arg = required_argument,
		.val = 'm',
	},
	{
		.name = "peer-cid",
		.has_arg = required_argument,
		.val = 'p',
	},
	{
		.name = "help",
		.has_arg = no_argument,
		.val = '?',
	},
	{},
};

static void usage(void)
{
	fprintf(stderr, "Usage: vsock_diag_test [--help] [--control-host=<host>] --control-port=<port> --mode=client|server --peer-cid=<cid>\n"
		"\n"
		"  Server: vsock_diag_test --control-port=1234 --mode=server --peer-cid=3\n"
		"  Client: vsock_diag_test --control-host=192.168.0.1 --control-port=1234 --mode=client --peer-cid=2\n"
		"\n"
		"Run vsock_diag.ko tests.  Must be launched in both\n"
		"guest and host.  One side must use --mode=client and\n"
		"the other side must use --mode=server.\n"
		"\n"
		"A TCP control socket connection is used to coordinate tests\n"
		"between the client and the server.  The server requires a\n"
		"listen address and the client requires an address to\n"
		"connect to.\n"
		"\n"
		"The CID of the other side must be given with --peer-cid=<cid>.\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
	const char *control_host = NULL;
	const char *control_port = NULL;
	int mode = TEST_MODE_UNSET;
	unsigned int peer_cid = VMADDR_CID_ANY;
	int i;

	init_signals();

	for (;;) {
		int opt = getopt_long(argc, argv, optstring, longopts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'H':
			control_host = optarg;
			break;
		case 'm':
			if (strcmp(optarg, "client") == 0)
				mode = TEST_MODE_CLIENT;
			else if (strcmp(optarg, "server") == 0)
				mode = TEST_MODE_SERVER;
			else {
				fprintf(stderr, "--mode must be \"client\" or \"server\"\n");
				return EXIT_FAILURE;
			}
			break;
		case 'p':
			peer_cid = parse_cid(optarg);
			break;
		case 'P':
			control_port = optarg;
			break;
		case '?':
		default:
			usage();
		}
	}

	if (!control_port)
		usage();
	if (mode == TEST_MODE_UNSET)
		usage();
	if (peer_cid == VMADDR_CID_ANY)
		usage();

	if (!control_host) {
		if (mode != TEST_MODE_SERVER)
			usage();
		control_host = "0.0.0.0";
	}

	control_init(control_host, control_port, mode == TEST_MODE_SERVER);

	for (i = 0; test_cases[i].name; i++) {
		void (*run)(unsigned int peer_cid);

		printf("%s...", test_cases[i].name);
		fflush(stdout);

		if (mode == TEST_MODE_CLIENT)
			run = test_cases[i].run_client;
		else
			run = test_cases[i].run_server;

		if (run)
			run(peer_cid);

		printf("ok\n");
	}

	control_cleanup();
	return EXIT_SUCCESS;
}
