// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Facebook */
#include <test_progs.h>
#include <bpf/libbpf.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "sk_storage_omem_uncharge.skel.h"

void test_sk_storage_omem_uncharge(void)
{
	struct sk_storage_omem_uncharge *skel;
	int sk_fd = -1, map_fd, err, value;
	socklen_t optlen;

	skel = sk_storage_omem_uncharge__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel open_and_load"))
		return;
	map_fd = bpf_map__fd(skel->maps.sk_storage);

	/* A standalone socket not binding to addr:port,
	 * so nentns is not needed.
	 */
	sk_fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (!ASSERT_GE(sk_fd, 0, "socket"))
		goto done;

	optlen = sizeof(skel->bss->cookie);
	err = getsockopt(sk_fd, SOL_SOCKET, SO_COOKIE, &skel->bss->cookie, &optlen);
	if (!ASSERT_OK(err, "getsockopt(SO_COOKIE)"))
		goto done;

	value = 0;
	err = bpf_map_update_elem(map_fd, &sk_fd, &value, 0);
	if (!ASSERT_OK(err, "bpf_map_update_elem(value=0)"))
		goto done;

	value = 0xdeadbeef;
	err = bpf_map_update_elem(map_fd, &sk_fd, &value, 0);
	if (!ASSERT_OK(err, "bpf_map_update_elem(value=0xdeadbeef)"))
		goto done;

	err = sk_storage_omem_uncharge__attach(skel);
	if (!ASSERT_OK(err, "attach"))
		goto done;

	close(sk_fd);
	sk_fd = -1;

	ASSERT_EQ(skel->bss->cookie_found, 2, "cookie_found");
	ASSERT_EQ(skel->bss->omem, 0, "omem");

done:
	sk_storage_omem_uncharge__destroy(skel);
	if (sk_fd != -1)
		close(sk_fd);
}
