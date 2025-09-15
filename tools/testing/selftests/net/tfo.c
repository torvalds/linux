// SPDX-License-Identifier: GPL-2.0
#include <error.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <errno.h>

static int cfg_server;
static int cfg_client;
static int cfg_port = 8000;
static struct sockaddr_in6 cfg_addr;
static char *cfg_outfile;

static int parse_address(const char *str, int port, struct sockaddr_in6 *sin6)
{
	int ret;

	sin6->sin6_family = AF_INET6;
	sin6->sin6_port = htons(port);

	ret = inet_pton(sin6->sin6_family, str, &sin6->sin6_addr);
	if (ret != 1) {
		/* fallback to plain IPv4 */
		ret = inet_pton(AF_INET, str, &sin6->sin6_addr.s6_addr32[3]);
		if (ret != 1)
			return -1;

		/* add ::ffff prefix */
		sin6->sin6_addr.s6_addr32[0] = 0;
		sin6->sin6_addr.s6_addr32[1] = 0;
		sin6->sin6_addr.s6_addr16[4] = 0;
		sin6->sin6_addr.s6_addr16[5] = 0xffff;
	}

	return 0;
}

static void run_server(void)
{
	unsigned long qlen = 32;
	int fd, opt, connfd;
	socklen_t len;
	char buf[64];
	FILE *outfile;

	outfile = fopen(cfg_outfile, "w");
	if (!outfile)
		error(1, errno, "fopen() outfile");

	fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (fd == -1)
		error(1, errno, "socket()");

	opt = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
		error(1, errno, "setsockopt(SO_REUSEADDR)");

	if (setsockopt(fd, SOL_TCP, TCP_FASTOPEN, &qlen, sizeof(qlen)) < 0)
		error(1, errno, "setsockopt(TCP_FASTOPEN)");

	if (bind(fd, (struct sockaddr *)&cfg_addr, sizeof(cfg_addr)) < 0)
		error(1, errno, "bind()");

	if (listen(fd, 5) < 0)
		error(1, errno, "listen()");

	len = sizeof(cfg_addr);
	connfd = accept(fd, (struct sockaddr *)&cfg_addr, &len);
	if (connfd < 0)
		error(1, errno, "accept()");

	len = sizeof(opt);
	if (getsockopt(connfd, SOL_SOCKET, SO_INCOMING_NAPI_ID, &opt, &len) < 0)
		error(1, errno, "getsockopt(SO_INCOMING_NAPI_ID)");

	read(connfd, buf, 64);
	fprintf(outfile, "%d\n", opt);

	fclose(outfile);
	close(connfd);
	close(fd);
}

static void run_client(void)
{
	int fd;
	char *msg = "Hello, world!";

	fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (fd == -1)
		error(1, errno, "socket()");

	sendto(fd, msg, strlen(msg), MSG_FASTOPEN, (struct sockaddr *)&cfg_addr, sizeof(cfg_addr));

	close(fd);
}

static void usage(const char *filepath)
{
	error(1, 0, "Usage: %s (-s|-c) -h<server_ip> -p<port> -o<outfile> ", filepath);
}

static void parse_opts(int argc, char **argv)
{
	struct sockaddr_in6 *addr6 = (void *) &cfg_addr;
	char *addr = NULL;
	int ret;
	int c;

	if (argc <= 1)
		usage(argv[0]);

	while ((c = getopt(argc, argv, "sch:p:o:")) != -1) {
		switch (c) {
		case 's':
			if (cfg_client)
				error(1, 0, "Pass one of -s or -c");
			cfg_server = 1;
			break;
		case 'c':
			if (cfg_server)
				error(1, 0, "Pass one of -s or -c");
			cfg_client = 1;
			break;
		case 'h':
			addr = optarg;
			break;
		case 'p':
			cfg_port = strtoul(optarg, NULL, 0);
			break;
		case 'o':
			cfg_outfile = strdup(optarg);
			if (!cfg_outfile)
				error(1, 0, "outfile invalid");
			break;
		}
	}

	if (cfg_server && addr)
		error(1, 0, "Server cannot have -h specified");

	memset(addr6, 0, sizeof(*addr6));
	addr6->sin6_family = AF_INET6;
	addr6->sin6_port = htons(cfg_port);
	addr6->sin6_addr = in6addr_any;
	if (addr) {
		ret = parse_address(addr, cfg_port, addr6);
		if (ret)
			error(1, 0, "Client address parse error: %s", addr);
	}
}

int main(int argc, char **argv)
{
	parse_opts(argc, argv);

	if (cfg_server)
		run_server();
	else if (cfg_client)
		run_client();

	return 0;
}
