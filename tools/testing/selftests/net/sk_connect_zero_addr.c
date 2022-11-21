// SPDX-License-Identifier: GPL-2.0

#include <arpa/inet.h>
#include <error.h>
#include <errno.h>
#include <unistd.h>

int main(void)
{
	int fd1, fd2, one = 1;
	struct sockaddr_in6 bind_addr = {
		.sin6_family = AF_INET6,
		.sin6_port = htons(20000),
		.sin6_flowinfo = htonl(0),
		.sin6_addr = {},
		.sin6_scope_id = 0,
	};

	inet_pton(AF_INET6, "::", &bind_addr.sin6_addr);

	fd1 = socket(AF_INET6, SOCK_STREAM, IPPROTO_IP);
	if (fd1 < 0) {
		error(1, errno, "socket fd1");
		return -1;
	}

	if (setsockopt(fd1, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one))) {
		error(1, errno, "setsockopt(SO_REUSEADDR) fd1");
		goto out_err1;
	}

	if (bind(fd1, (struct sockaddr *)&bind_addr, sizeof(bind_addr))) {
		error(1, errno, "bind fd1");
		goto out_err1;
	}

	if (listen(fd1, 0)) {
		error(1, errno, "listen");
		goto out_err1;
	}

	fd2 = socket(AF_INET6, SOCK_STREAM, IPPROTO_IP);
	if (fd2 < 0) {
		error(1, errno, "socket fd2");
		goto out_err1;
	}

	if (connect(fd2, (struct sockaddr *)&bind_addr, sizeof(bind_addr))) {
		error(1, errno, "bind fd2");
		goto out_err2;
	}

	close(fd2);
	close(fd1);
	return 0;

out_err2:
	close(fd2);
out_err1:
	close(fd1);
	return -1;
}
