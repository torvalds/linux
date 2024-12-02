// SPDX-License-Identifier: GPL-2.0

#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static int child_pid;

static unsigned long timediff(struct timeval s, struct timeval e)
{
	unsigned long s_us, e_us;

	s_us = s.tv_sec * 1000000 + s.tv_usec;
	e_us = e.tv_sec * 1000000 + e.tv_usec;
	if (s_us > e_us)
		return 0;
	return e_us - s_us;
}

static void client(int port)
{
	int sock = 0;
	struct sockaddr_in addr, laddr;
	socklen_t len = sizeof(laddr);
	struct linger sl;
	int flag = 1;
	int buffer;
	struct timeval start, end;
	unsigned long lat, sum_lat = 0, nr_lat = 0;

	while (1) {
		gettimeofday(&start, NULL);

		sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock < 0)
			error(-1, errno, "socket creation");

		sl.l_onoff = 1;
		sl.l_linger = 0;
		if (setsockopt(sock, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl)))
			error(-1, errno, "setsockopt(linger)");

		if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
					&flag, sizeof(flag)))
			error(-1, errno, "setsockopt(nodelay)");

		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);

		if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) <= 0)
			error(-1, errno, "inet_pton");

		if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
			error(-1, errno, "connect");

		send(sock, &buffer, sizeof(buffer), 0);
		if (read(sock, &buffer, sizeof(buffer)) == -1)
			error(-1, errno, "waiting read");

		gettimeofday(&end, NULL);
		lat = timediff(start, end);
		sum_lat += lat;
		nr_lat++;
		if (lat < 100000)
			goto close;

		if (getsockname(sock, (struct sockaddr *)&laddr, &len) == -1)
			error(-1, errno, "getsockname");
		printf("port: %d, lat: %lu, avg: %lu, nr: %lu\n",
				ntohs(laddr.sin_port), lat,
				sum_lat / nr_lat, nr_lat);
close:
		fflush(stdout);
		close(sock);
	}
}

static void server(int sock, struct sockaddr_in address)
{
	int accepted;
	int addrlen = sizeof(address);
	int buffer;

	while (1) {
		accepted = accept(sock, (struct sockaddr *)&address,
				(socklen_t *)&addrlen);
		if (accepted < 0)
			error(-1, errno, "accept");

		if (read(accepted, &buffer, sizeof(buffer)) == -1)
			error(-1, errno, "read");
		close(accepted);
	}
}

static void sig_handler(int signum)
{
	kill(SIGTERM, child_pid);
	exit(0);
}

int main(int argc, char const *argv[])
{
	int sock;
	int opt = 1;
	struct sockaddr_in address;
	struct sockaddr_in laddr;
	socklen_t len = sizeof(laddr);

	if (signal(SIGTERM, sig_handler) == SIG_ERR)
		error(-1, errno, "signal");

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		error(-1, errno, "socket");

	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
				&opt, sizeof(opt)) == -1)
		error(-1, errno, "setsockopt");

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	/* dynamically allocate unused port */
	address.sin_port = 0;

	if (bind(sock, (struct sockaddr *)&address, sizeof(address)) < 0)
		error(-1, errno, "bind");

	if (listen(sock, 3) < 0)
		error(-1, errno, "listen");

	if (getsockname(sock, (struct sockaddr *)&laddr, &len) == -1)
		error(-1, errno, "getsockname");

	fprintf(stderr, "server port: %d\n", ntohs(laddr.sin_port));
	child_pid = fork();
	if (!child_pid)
		client(ntohs(laddr.sin_port));
	else
		server(sock, laddr);

	return 0;
}
