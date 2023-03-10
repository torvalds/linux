// SPDX-License-Identifier: GPL-2.0-only
/*
 * vsock_perf - benchmark utility for vsock.
 *
 * Copyright (C) 2022 SberDevices.
 *
 * Author: Arseniy Krasnov <AVKrasnov@sberdevices.ru>
 */
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <poll.h>
#include <sys/socket.h>
#include <linux/vm_sockets.h>

#define DEFAULT_BUF_SIZE_BYTES	(128 * 1024)
#define DEFAULT_TO_SEND_BYTES	(64 * 1024)
#define DEFAULT_VSOCK_BUF_BYTES (256 * 1024)
#define DEFAULT_RCVLOWAT_BYTES	1
#define DEFAULT_PORT		1234

#define BYTES_PER_GB		(1024 * 1024 * 1024ULL)
#define NSEC_PER_SEC		(1000000000ULL)

static unsigned int port = DEFAULT_PORT;
static unsigned long buf_size_bytes = DEFAULT_BUF_SIZE_BYTES;
static unsigned long vsock_buf_bytes = DEFAULT_VSOCK_BUF_BYTES;

static void error(const char *s)
{
	perror(s);
	exit(EXIT_FAILURE);
}

static time_t current_nsec(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_REALTIME, &ts))
		error("clock_gettime");

	return (ts.tv_sec * NSEC_PER_SEC) + ts.tv_nsec;
}

/* From lib/cmdline.c. */
static unsigned long memparse(const char *ptr)
{
	char *endptr;

	unsigned long long ret = strtoull(ptr, &endptr, 0);

	switch (*endptr) {
	case 'E':
	case 'e':
		ret <<= 10;
	case 'P':
	case 'p':
		ret <<= 10;
	case 'T':
	case 't':
		ret <<= 10;
	case 'G':
	case 'g':
		ret <<= 10;
	case 'M':
	case 'm':
		ret <<= 10;
	case 'K':
	case 'k':
		ret <<= 10;
		endptr++;
	default:
		break;
	}

	return ret;
}

static void vsock_increase_buf_size(int fd)
{
	if (setsockopt(fd, AF_VSOCK, SO_VM_SOCKETS_BUFFER_MAX_SIZE,
		       &vsock_buf_bytes, sizeof(vsock_buf_bytes)))
		error("setsockopt(SO_VM_SOCKETS_BUFFER_MAX_SIZE)");

	if (setsockopt(fd, AF_VSOCK, SO_VM_SOCKETS_BUFFER_SIZE,
		       &vsock_buf_bytes, sizeof(vsock_buf_bytes)))
		error("setsockopt(SO_VM_SOCKETS_BUFFER_SIZE)");
}

static int vsock_connect(unsigned int cid, unsigned int port)
{
	union {
		struct sockaddr sa;
		struct sockaddr_vm svm;
	} addr = {
		.svm = {
			.svm_family = AF_VSOCK,
			.svm_port = port,
			.svm_cid = cid,
		},
	};
	int fd;

	fd = socket(AF_VSOCK, SOCK_STREAM, 0);

	if (fd < 0) {
		perror("socket");
		return -1;
	}

	if (connect(fd, &addr.sa, sizeof(addr.svm)) < 0) {
		perror("connect");
		close(fd);
		return -1;
	}

	return fd;
}

static float get_gbps(unsigned long bits, time_t ns_delta)
{
	return ((float)bits / 1000000000ULL) /
	       ((float)ns_delta / NSEC_PER_SEC);
}

static void run_receiver(unsigned long rcvlowat_bytes)
{
	unsigned int read_cnt;
	time_t rx_begin_ns;
	time_t in_read_ns;
	size_t total_recv;
	int client_fd;
	char *data;
	int fd;
	union {
		struct sockaddr sa;
		struct sockaddr_vm svm;
	} addr = {
		.svm = {
			.svm_family = AF_VSOCK,
			.svm_port = port,
			.svm_cid = VMADDR_CID_ANY,
		},
	};
	union {
		struct sockaddr sa;
		struct sockaddr_vm svm;
	} clientaddr;

	socklen_t clientaddr_len = sizeof(clientaddr.svm);

	printf("Run as receiver\n");
	printf("Listen port %u\n", port);
	printf("RX buffer %lu bytes\n", buf_size_bytes);
	printf("vsock buffer %lu bytes\n", vsock_buf_bytes);
	printf("SO_RCVLOWAT %lu bytes\n", rcvlowat_bytes);

	fd = socket(AF_VSOCK, SOCK_STREAM, 0);

	if (fd < 0)
		error("socket");

	if (bind(fd, &addr.sa, sizeof(addr.svm)) < 0)
		error("bind");

	if (listen(fd, 1) < 0)
		error("listen");

	client_fd = accept(fd, &clientaddr.sa, &clientaddr_len);

	if (client_fd < 0)
		error("accept");

	vsock_increase_buf_size(client_fd);

	if (setsockopt(client_fd, SOL_SOCKET, SO_RCVLOWAT,
		       &rcvlowat_bytes,
		       sizeof(rcvlowat_bytes)))
		error("setsockopt(SO_RCVLOWAT)");

	data = malloc(buf_size_bytes);

	if (!data) {
		fprintf(stderr, "'malloc()' failed\n");
		exit(EXIT_FAILURE);
	}

	read_cnt = 0;
	in_read_ns = 0;
	total_recv = 0;
	rx_begin_ns = current_nsec();

	while (1) {
		struct pollfd fds = { 0 };

		fds.fd = client_fd;
		fds.events = POLLIN | POLLERR |
			     POLLHUP | POLLRDHUP;

		if (poll(&fds, 1, -1) < 0)
			error("poll");

		if (fds.revents & POLLERR) {
			fprintf(stderr, "'poll()' error\n");
			exit(EXIT_FAILURE);
		}

		if (fds.revents & POLLIN) {
			ssize_t bytes_read;
			time_t t;

			t = current_nsec();
			bytes_read = read(fds.fd, data, buf_size_bytes);
			in_read_ns += (current_nsec() - t);
			read_cnt++;

			if (!bytes_read)
				break;

			if (bytes_read < 0) {
				perror("read");
				exit(EXIT_FAILURE);
			}

			total_recv += bytes_read;
		}

		if (fds.revents & (POLLHUP | POLLRDHUP))
			break;
	}

	printf("total bytes received: %zu\n", total_recv);
	printf("rx performance: %f Gbits/s\n",
	       get_gbps(total_recv * 8, current_nsec() - rx_begin_ns));
	printf("total time in 'read()': %f sec\n", (float)in_read_ns / NSEC_PER_SEC);
	printf("average time in 'read()': %f ns\n", (float)in_read_ns / read_cnt);
	printf("POLLIN wakeups: %i\n", read_cnt);

	free(data);
	close(client_fd);
	close(fd);
}

static void run_sender(int peer_cid, unsigned long to_send_bytes)
{
	time_t tx_begin_ns;
	time_t tx_total_ns;
	size_t total_send;
	void *data;
	int fd;

	printf("Run as sender\n");
	printf("Connect to %i:%u\n", peer_cid, port);
	printf("Send %lu bytes\n", to_send_bytes);
	printf("TX buffer %lu bytes\n", buf_size_bytes);

	fd = vsock_connect(peer_cid, port);

	if (fd < 0)
		exit(EXIT_FAILURE);

	data = malloc(buf_size_bytes);

	if (!data) {
		fprintf(stderr, "'malloc()' failed\n");
		exit(EXIT_FAILURE);
	}

	memset(data, 0, buf_size_bytes);
	total_send = 0;
	tx_begin_ns = current_nsec();

	while (total_send < to_send_bytes) {
		ssize_t sent;

		sent = write(fd, data, buf_size_bytes);

		if (sent <= 0)
			error("write");

		total_send += sent;
	}

	tx_total_ns = current_nsec() - tx_begin_ns;

	printf("total bytes sent: %zu\n", total_send);
	printf("tx performance: %f Gbits/s\n",
	       get_gbps(total_send * 8, tx_total_ns));
	printf("total time in 'write()': %f sec\n",
	       (float)tx_total_ns / NSEC_PER_SEC);

	close(fd);
	free(data);
}

static const char optstring[] = "";
static const struct option longopts[] = {
	{
		.name = "help",
		.has_arg = no_argument,
		.val = 'H',
	},
	{
		.name = "sender",
		.has_arg = required_argument,
		.val = 'S',
	},
	{
		.name = "port",
		.has_arg = required_argument,
		.val = 'P',
	},
	{
		.name = "bytes",
		.has_arg = required_argument,
		.val = 'M',
	},
	{
		.name = "buf-size",
		.has_arg = required_argument,
		.val = 'B',
	},
	{
		.name = "vsk-size",
		.has_arg = required_argument,
		.val = 'V',
	},
	{
		.name = "rcvlowat",
		.has_arg = required_argument,
		.val = 'R',
	},
	{},
};

static void usage(void)
{
	printf("Usage: ./vsock_perf [--help] [options]\n"
	       "\n"
	       "This is benchmarking utility, to test vsock performance.\n"
	       "It runs in two modes: sender or receiver. In sender mode, it\n"
	       "connects to the specified CID and starts data transmission.\n"
	       "\n"
	       "Options:\n"
	       "  --help			This message\n"
	       "  --sender   <cid>		Sender mode (receiver default)\n"
	       "                                <cid> of the receiver to connect to\n"
	       "  --port     <port>		Port (default %d)\n"
	       "  --bytes    <bytes>KMG		Bytes to send (default %d)\n"
	       "  --buf-size <bytes>KMG		Data buffer size (default %d). In sender mode\n"
	       "                                it is the buffer size, passed to 'write()'. In\n"
	       "                                receiver mode it is the buffer size passed to 'read()'.\n"
	       "  --vsk-size <bytes>KMG		Socket buffer size (default %d)\n"
	       "  --rcvlowat <bytes>KMG		SO_RCVLOWAT value (default %d)\n"
	       "\n", DEFAULT_PORT, DEFAULT_TO_SEND_BYTES,
	       DEFAULT_BUF_SIZE_BYTES, DEFAULT_VSOCK_BUF_BYTES,
	       DEFAULT_RCVLOWAT_BYTES);
	exit(EXIT_FAILURE);
}

static long strtolx(const char *arg)
{
	long value;
	char *end;

	value = strtol(arg, &end, 10);

	if (end != arg + strlen(arg))
		usage();

	return value;
}

int main(int argc, char **argv)
{
	unsigned long to_send_bytes = DEFAULT_TO_SEND_BYTES;
	unsigned long rcvlowat_bytes = DEFAULT_RCVLOWAT_BYTES;
	int peer_cid = -1;
	bool sender = false;

	while (1) {
		int opt = getopt_long(argc, argv, optstring, longopts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'V': /* Peer buffer size. */
			vsock_buf_bytes = memparse(optarg);
			break;
		case 'R': /* SO_RCVLOWAT value. */
			rcvlowat_bytes = memparse(optarg);
			break;
		case 'P': /* Port to connect to. */
			port = strtolx(optarg);
			break;
		case 'M': /* Bytes to send. */
			to_send_bytes = memparse(optarg);
			break;
		case 'B': /* Size of rx/tx buffer. */
			buf_size_bytes = memparse(optarg);
			break;
		case 'S': /* Sender mode. CID to connect to. */
			peer_cid = strtolx(optarg);
			sender = true;
			break;
		case 'H': /* Help. */
			usage();
			break;
		default:
			usage();
		}
	}

	if (!sender)
		run_receiver(rcvlowat_bytes);
	else
		run_sender(peer_cid, to_send_bytes);

	return 0;
}
