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

static char *afstr(int af, int proto)
{
	if (proto == IPPROTO_TCP)
		return af == AF_INET ? "TCP/IPv4" : "TCP/IPv6";
	else
		return af == AF_INET ? "UDP/IPv4" : "UDP/IPv6";
}

int sk_peek_offset_probe(sa_family_t af, int proto)
{
	int type = (proto == IPPROTO_TCP ? SOCK_STREAM : SOCK_DGRAM);
	int optv = 0;
	int ret = 0;
	int s;

	s = socket(af, type, proto);
	if (s < 0) {
		ksft_perror("Temporary TCP socket creation failed");
	} else {
		if (!setsockopt(s, SOL_SOCKET, SO_PEEK_OFF, &optv, sizeof(int)))
			ret = 1;
		else
			printf("%s does not support SO_PEEK_OFF\n", afstr(af, proto));
		close(s);
	}
	return ret;
}

static void sk_peek_offset_set(int s, int offset)
{
	if (setsockopt(s, SOL_SOCKET, SO_PEEK_OFF, &offset, sizeof(offset)))
		ksft_perror("Failed to set SO_PEEK_OFF value\n");
}

static int sk_peek_offset_get(int s)
{
	int offset;
	socklen_t len = sizeof(offset);

	if (getsockopt(s, SOL_SOCKET, SO_PEEK_OFF, &offset, &len))
		ksft_perror("Failed to get SO_PEEK_OFF value\n");
	return offset;
}

static int sk_peek_offset_test(sa_family_t af, int proto)
{
	int type = (proto == IPPROTO_TCP ? SOCK_STREAM : SOCK_DGRAM);
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
	char buf[2];

	memset(&a, 0, sizeof(a));
	a.sa.sa_family = af;

	s[0] = recv_sock = socket(af, type, proto);
	s[1] = socket(af, type, proto);

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
	if (proto == IPPROTO_TCP && listen(s[0], 0) < 0) {
		ksft_perror("Temporary socket listen() failed\n");
		goto out;
	}
	if (connect(s[1], &a.sa, sizeof(a)) < 0) {
		ksft_perror("Temporary socket connect() failed\n");
		goto out;
	}
	if (proto == IPPROTO_TCP) {
		recv_sock = accept(s[0], NULL, NULL);
		if (recv_sock <= 0) {
			ksft_perror("Temporary socket accept() failed\n");
			goto out;
		}
	}

	/* Some basic tests of getting/setting offset */
	offset = sk_peek_offset_get(recv_sock);
	if (offset != -1) {
		ksft_perror("Initial value of socket offset not -1\n");
		goto out;
	}
	sk_peek_offset_set(recv_sock, 0);
	offset = sk_peek_offset_get(recv_sock);
	if (offset != 0) {
		ksft_perror("Failed to set socket offset to 0\n");
		goto out;
	}

	/* Transfer a message */
	if (send(s[1], (char *)("ab"), 2, 0) != 2) {
		ksft_perror("Temporary probe socket send() failed\n");
		goto out;
	}
	/* Read first byte */
	len = recv(recv_sock, buf, 1, MSG_PEEK);
	if (len != 1 || buf[0] != 'a') {
		ksft_perror("Failed to read first byte of message\n");
		goto out;
	}
	offset = sk_peek_offset_get(recv_sock);
	if (offset != 1) {
		ksft_perror("Offset not forwarded correctly at first byte\n");
		goto out;
	}
	/* Try to read beyond last byte */
	len = recv(recv_sock, buf, 2, MSG_PEEK);
	if (len != 1 || buf[0] != 'b') {
		ksft_perror("Failed to read last byte of message\n");
		goto out;
	}
	offset = sk_peek_offset_get(recv_sock);
	if (offset != 2) {
		ksft_perror("Offset not forwarded correctly at last byte\n");
		goto out;
	}
	/* Flush message */
	len = recv(recv_sock, buf, 2, MSG_TRUNC);
	if (len != 2) {
		ksft_perror("Failed to flush message\n");
		goto out;
	}
	offset = sk_peek_offset_get(recv_sock);
	if (offset != 0) {
		ksft_perror("Offset not reverted correctly after flush\n");
		goto out;
	}

	printf("%s with MSG_PEEK_OFF works correctly\n", afstr(af, proto));
	res = 1;
out:
	if (proto == IPPROTO_TCP && recv_sock >= 0)
		close(recv_sock);
	if (s[1] >= 0)
		close(s[1]);
	if (s[0] >= 0)
		close(s[0]);
	return res;
}

static int do_test(int proto)
{
	int res4, res6;

	res4 = sk_peek_offset_probe(AF_INET, proto);
	res6 = sk_peek_offset_probe(AF_INET6, proto);

	if (!res4 && !res6)
		return KSFT_SKIP;

	if (res4)
		res4 = sk_peek_offset_test(AF_INET, proto);

	if (res6)
		res6 = sk_peek_offset_test(AF_INET6, proto);

	if (!res4 || !res6)
		return KSFT_FAIL;

	return KSFT_PASS;
}

int main(void)
{
	int restcp, resudp;

	restcp = do_test(IPPROTO_TCP);
	resudp = do_test(IPPROTO_UDP);
	if (restcp == KSFT_FAIL || resudp == KSFT_FAIL)
		return KSFT_FAIL;

	return KSFT_PASS;
}
