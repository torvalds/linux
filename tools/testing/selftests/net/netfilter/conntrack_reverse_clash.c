// SPDX-License-Identifier: GPL-2.0
/*
 * Needs something like:
 *
 * iptables -t nat -A POSTROUTING -o nomatch -j MASQUERADE
 *
 * so NAT engine attaches a NAT null-binding to each connection.
 *
 * With unmodified kernels, child or parent will exit with
 * "Port number changed" error, even though no port translation
 * was requested.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>

#define LEN 512
#define PORT 56789
#define TEST_TIME 5

static void die(const char *e)
{
	perror(e);
	exit(111);
}

static void die_port(uint16_t got, uint16_t want)
{
	fprintf(stderr, "Port number changed, wanted %d got %d\n", want, ntohs(got));
	exit(1);
}

static int udp_socket(void)
{
	static const struct timeval tv = {
		.tv_sec = 1,
	};
	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (fd < 0)
		die("socket");

	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	return fd;
}

int main(int argc, char *argv[])
{
	struct sockaddr_in sa1 = {
		.sin_family = AF_INET,
	};
	struct sockaddr_in sa2 = {
		.sin_family = AF_INET,
	};
	int s1, s2, status;
	time_t end, now;
	socklen_t plen;
	char buf[LEN];
	bool child;

	sa1.sin_port = htons(PORT);
	sa2.sin_port = htons(PORT + 1);

	s1 = udp_socket();
	s2 = udp_socket();

	inet_pton(AF_INET, "127.0.0.11", &sa1.sin_addr);
	inet_pton(AF_INET, "127.0.0.12", &sa2.sin_addr);

	if (bind(s1, (struct sockaddr *)&sa1, sizeof(sa1)) < 0)
		die("bind 1");
	if (bind(s2, (struct sockaddr *)&sa2, sizeof(sa2)) < 0)
		die("bind 2");

	child = fork() == 0;

	now = time(NULL);
	end = now + TEST_TIME;

	while (now < end) {
		struct sockaddr_in peer;
		socklen_t plen = sizeof(peer);

		now = time(NULL);

		if (child) {
			if (sendto(s1, buf, LEN, 0, (struct sockaddr *)&sa2, sizeof(sa2)) != LEN)
				continue;

			if (recvfrom(s2, buf, LEN, 0, (struct sockaddr *)&peer, &plen) < 0)
				die("child recvfrom");

			if (peer.sin_port != htons(PORT))
				die_port(peer.sin_port, PORT);
		} else {
			if (sendto(s2, buf, LEN, 0, (struct sockaddr *)&sa1, sizeof(sa1)) != LEN)
				continue;

			if (recvfrom(s1, buf, LEN, 0, (struct sockaddr *)&peer, &plen) < 0)
				die("parent recvfrom");

			if (peer.sin_port != htons((PORT + 1)))
				die_port(peer.sin_port, PORT + 1);
		}
	}

	if (child)
		return 0;

	wait(&status);

	if (WIFEXITED(status))
		return WEXITSTATUS(status);

	return 1;
}
