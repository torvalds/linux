/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021 Hengqi Chen */

#include <test_progs.h>
#include <sys/un.h>
#include "test_skc_to_unix_sock.skel.h"

static const char *sock_path = "@skc_to_unix_sock";

void test_skc_to_unix_sock(void)
{
	struct test_skc_to_unix_sock *skel;
	struct sockaddr_un sockaddr;
	int err, sockfd = 0;

	skel = test_skc_to_unix_sock__open();
	if (!ASSERT_OK_PTR(skel, "could not open BPF object"))
		return;

	skel->rodata->my_pid = getpid();

	err = test_skc_to_unix_sock__load(skel);
	if (!ASSERT_OK(err, "could not load BPF object"))
		goto cleanup;

	err = test_skc_to_unix_sock__attach(skel);
	if (!ASSERT_OK(err, "could not attach BPF object"))
		goto cleanup;

	/* trigger unix_listen */
	sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (!ASSERT_GT(sockfd, 0, "socket failed"))
		goto cleanup;

	memset(&sockaddr, 0, sizeof(sockaddr));
	sockaddr.sun_family = AF_UNIX;
	strncpy(sockaddr.sun_path, sock_path, strlen(sock_path));
	sockaddr.sun_path[0] = '\0';

	err = bind(sockfd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
	if (!ASSERT_OK(err, "bind failed"))
		goto cleanup;

	err = listen(sockfd, 1);
	if (!ASSERT_OK(err, "listen failed"))
		goto cleanup;

	ASSERT_EQ(strcmp(skel->bss->path, sock_path), 0, "bpf_skc_to_unix_sock failed");

cleanup:
	if (sockfd)
		close(sockfd);
	test_skc_to_unix_sock__destroy(skel);
}
