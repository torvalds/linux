// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../kselftest.h"

static char *afstr(int af)
{
	return af == AF_INET ? "TCP/IPv4" : "TCP/IPv6";
}

int tcp_peek_offset_probe(sa_family_t af)
{
	int optv = 0;
	int ret = 0;
	int s;

	s = socket(af, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
	if (s < 0) {
		ksft_perror("Temporary TCP socket creation failed");
	} else {
		if (!setsockopt(s, SOL_SOCKET, SO_PEEK_OFF, &optv, sizeof(int)))
			ret = 1;
		else
			printf("%s does not support SO_PEEK_OFF\n", afstr(af));
		close(s);
	}
	return ret;
}

static void tcp_peek_offset_set(int s, int offset)
{
	if (setsockopt(s, SOL_SOCKET, SO_PEEK_OFF, &offset, sizeof(offset)))
		ksft_perror("Failed to set SO_PEEK_OFF value\n");
}

static int tcp_peek_offset_get(int s)
{
	int offset;
	socklen_t len = sizeof(offset);

	if (getsockopt(s, SOL_SOCKET, SO_PEEK_OFF, &offset, &len))
		ksft_perror("Failed to get SO_PEEK_OFF value\n");
	return offset;
}

static int tcp_peek_offset_test(sa_family_t af)
{
	union {
		struct sockaddr sa;
		struct sockaddr_in a4;
		struct sockaddr_in6 a6;
	} a;
	int res = 0;
	int s[2] = {0, 0};
	int recv_sock = 0;
	int offset = 0;
	ssize_t len;
	char buf;

	memset(&a, 0, sizeof(a));
	a.sa.sa_family = af;

	s[0] = socket(af, SOCK_STREAM, IPPROTO_TCP);
	s[1] = socket(af, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);

	if (s[0] < 0 || s[1] < 0) {
		ksft_perror("Temporary socket creation failed\n");
		goto out;
	}
	if (bind(s[0], &a.sa, sizeof(a)) < 0) {
		ksft_perror("Temporary socket bind() failed\n");
		goto out;
	}
	if (getsockname(s[0], &a.sa, &((socklen_t) { sizeof(a) })) < 0) {
		ksft_perror("Temporary socket getsockname() failed\n");
		goto out;
	}
	if (listen(s[0], 0) < 0) {
		ksft_perror("Temporary socket listen() failed\n");
		goto out;
	}
	if (connect(s[1], &a.sa, sizeof(a)) >= 0 || errno != EINPROGRESS) {
		ksft_perror("Temporary socket connect() failed\n");
		goto out;
	}
	recv_sock = accept(s[0], NULL, NULL);
	if (recv_sock <= 0) {
		ksft_perror("Temporary socket accept() failed\n");
		goto out;
	}

	/* Some basic tests of getting/setting offset */
	offset = tcp_peek_offset_get(recv_sock);
	if (offset != -1) {
		ksft_perror("Initial value of socket offset not -1\n");
		goto out;
	}
	tcp_peek_offset_set(recv_sock, 0);
	offset = tcp_peek_offset_get(recv_sock);
	if (offset != 0) {
		ksft_perror("Failed to set socket offset to 0\n");
		goto out;
	}

	/* Transfer a message */
	if (send(s[1], (char *)("ab"), 2, 0) <= 0 || errno != EINPROGRESS) {
		ksft_perror("Temporary probe socket send() failed\n");
		goto out;
	}
	/* Read first byte */
	len = recv(recv_sock, &buf, 1, MSG_PEEK);
	if (len != 1 || buf != 'a') {
		ksft_perror("Failed to read first byte of message\n");
		goto out;
	}
	offset = tcp_peek_offset_get(recv_sock);
	if (offset != 1) {
		ksft_perror("Offset not forwarded correctly at first byte\n");
		goto out;
	}
	/* Try to read beyond last byte */
	len = recv(recv_sock, &buf, 2, MSG_PEEK);
	if (len != 1 || buf != 'b') {
		ksft_perror("Failed to read last byte of message\n");
		goto out;
	}
	offset = tcp_peek_offset_get(recv_sock);
	if (offset != 2) {
		ksft_perror("Offset not forwarded correctly at last byte\n");
		goto out;
	}
	/* Flush message */
	len = recv(recv_sock, NULL, 2, MSG_TRUNC);
	if (len != 2) {
		ksft_perror("Failed to flush message\n");
		goto out;
	}
	offset = tcp_peek_offset_get(recv_sock);
	if (offset != 0) {
		ksft_perror("Offset not reverted correctly after flush\n");
		goto out;
	}

	printf("%s with MSG_PEEK_OFF works correctly\n", afstr(af));
	res = 1;
out:
	if (recv_sock >= 0)
		close(recv_sock);
	if (s[1] >= 0)
		close(s[1]);
	if (s[0] >= 0)
		close(s[0]);
	return res;
}

int main(void)
{
	int res4, res6;

	res4 = tcp_peek_offset_probe(AF_INET);
	res6 = tcp_peek_offset_probe(AF_INET6);

	if (!res4 && !res6)
		return KSFT_SKIP;

	if (res4)
		res4 = tcp_peek_offset_test(AF_INET);

	if (res6)
		res6 = tcp_peek_offset_test(AF_INET6);

	if (!res4 || !res6)
		return KSFT_FAIL;

	return KSFT_PASS;
}
