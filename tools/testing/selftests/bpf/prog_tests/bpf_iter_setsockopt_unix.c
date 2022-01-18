// SPDX-License-Identifier: GPL-2.0
/* Copyright Amazon.com Inc. or its affiliates. */
#include <sys/socket.h>
#include <sys/un.h>
#include <test_progs.h>
#include "bpf_iter_setsockopt_unix.skel.h"

#define NR_CASES 5

static int create_unix_socket(struct bpf_iter_setsockopt_unix *skel)
{
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
		.sun_path = "",
	};
	socklen_t len;
	int fd, err;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (!ASSERT_NEQ(fd, -1, "socket"))
		return -1;

	len = offsetof(struct sockaddr_un, sun_path);
	err = bind(fd, (struct sockaddr *)&addr, len);
	if (!ASSERT_OK(err, "bind"))
		return -1;

	len = sizeof(addr);
	err = getsockname(fd, (struct sockaddr *)&addr, &len);
	if (!ASSERT_OK(err, "getsockname"))
		return -1;

	memcpy(&skel->bss->sun_path, &addr.sun_path,
	       len - offsetof(struct sockaddr_un, sun_path));

	return fd;
}

static void test_sndbuf(struct bpf_iter_setsockopt_unix *skel, int fd)
{
	socklen_t optlen;
	int i, err;

	for (i = 0; i < NR_CASES; i++) {
		if (!ASSERT_NEQ(skel->data->sndbuf_getsockopt[i], -1,
				"bpf_(get|set)sockopt"))
			return;

		err = setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
				 &(skel->data->sndbuf_setsockopt[i]),
				 sizeof(skel->data->sndbuf_setsockopt[i]));
		if (!ASSERT_OK(err, "setsockopt"))
			return;

		optlen = sizeof(skel->bss->sndbuf_getsockopt_expected[i]);
		err = getsockopt(fd, SOL_SOCKET, SO_SNDBUF,
				 &(skel->bss->sndbuf_getsockopt_expected[i]),
				 &optlen);
		if (!ASSERT_OK(err, "getsockopt"))
			return;

		if (!ASSERT_EQ(skel->data->sndbuf_getsockopt[i],
			       skel->bss->sndbuf_getsockopt_expected[i],
			       "bpf_(get|set)sockopt"))
			return;
	}
}

void test_bpf_iter_setsockopt_unix(void)
{
	struct bpf_iter_setsockopt_unix *skel;
	int err, unix_fd, iter_fd;
	char buf;

	skel = bpf_iter_setsockopt_unix__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;

	unix_fd = create_unix_socket(skel);
	if (!ASSERT_NEQ(unix_fd, -1, "create_unix_server"))
		goto destroy;

	skel->links.change_sndbuf = bpf_program__attach_iter(skel->progs.change_sndbuf, NULL);
	if (!ASSERT_OK_PTR(skel->links.change_sndbuf, "bpf_program__attach_iter"))
		goto destroy;

	iter_fd = bpf_iter_create(bpf_link__fd(skel->links.change_sndbuf));
	if (!ASSERT_GE(iter_fd, 0, "bpf_iter_create"))
		goto destroy;

	while ((err = read(iter_fd, &buf, sizeof(buf))) == -1 &&
	       errno == EAGAIN)
		;
	if (!ASSERT_OK(err, "read iter error"))
		goto destroy;

	test_sndbuf(skel, unix_fd);
destroy:
	bpf_iter_setsockopt_unix__destroy(skel);
}
