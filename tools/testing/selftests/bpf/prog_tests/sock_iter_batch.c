// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2024 Meta

#include <test_progs.h>
#include "network_helpers.h"
#include "sock_iter_batch.skel.h"

#define TEST_NS "sock_iter_batch_netns"

static const int nr_soreuse = 4;

static void do_test(int sock_type, bool onebyone)
{
	int err, i, nread, to_read, total_read, iter_fd = -1;
	int first_idx, second_idx, indices[nr_soreuse];
	struct bpf_link *link = NULL;
	struct sock_iter_batch *skel;
	int *fds[2] = {};

	skel = sock_iter_batch__open();
	if (!ASSERT_OK_PTR(skel, "sock_iter_batch__open"))
		return;

	/* Prepare 2 buckets of sockets in the kernel hashtable */
	for (i = 0; i < ARRAY_SIZE(fds); i++) {
		int local_port;

		fds[i] = start_reuseport_server(AF_INET6, sock_type, "::1", 0, 0,
						nr_soreuse);
		if (!ASSERT_OK_PTR(fds[i], "start_reuseport_server"))
			goto done;
		local_port = get_socket_local_port(*fds[i]);
		if (!ASSERT_GE(local_port, 0, "get_socket_local_port"))
			goto done;
		skel->rodata->ports[i] = ntohs(local_port);
	}

	err = sock_iter_batch__load(skel);
	if (!ASSERT_OK(err, "sock_iter_batch__load"))
		goto done;

	link = bpf_program__attach_iter(sock_type == SOCK_STREAM ?
					skel->progs.iter_tcp_soreuse :
					skel->progs.iter_udp_soreuse,
					NULL);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach_iter"))
		goto done;

	iter_fd = bpf_iter_create(bpf_link__fd(link));
	if (!ASSERT_GE(iter_fd, 0, "bpf_iter_create"))
		goto done;

	/* Test reading a bucket (either from fds[0] or fds[1]).
	 * Only read "nr_soreuse - 1" number of sockets
	 * from a bucket and leave one socket out from
	 * that bucket on purpose.
	 */
	to_read = (nr_soreuse - 1) * sizeof(*indices);
	total_read = 0;
	first_idx = -1;
	do {
		nread = read(iter_fd, indices, onebyone ? sizeof(*indices) : to_read);
		if (nread <= 0 || nread % sizeof(*indices))
			break;
		total_read += nread;

		if (first_idx == -1)
			first_idx = indices[0];
		for (i = 0; i < nread / sizeof(*indices); i++)
			ASSERT_EQ(indices[i], first_idx, "first_idx");
	} while (total_read < to_read);
	ASSERT_EQ(nread, onebyone ? sizeof(*indices) : to_read, "nread");
	ASSERT_EQ(total_read, to_read, "total_read");

	free_fds(fds[first_idx], nr_soreuse);
	fds[first_idx] = NULL;

	/* Read the "whole" second bucket */
	to_read = nr_soreuse * sizeof(*indices);
	total_read = 0;
	second_idx = !first_idx;
	do {
		nread = read(iter_fd, indices, onebyone ? sizeof(*indices) : to_read);
		if (nread <= 0 || nread % sizeof(*indices))
			break;
		total_read += nread;

		for (i = 0; i < nread / sizeof(*indices); i++)
			ASSERT_EQ(indices[i], second_idx, "second_idx");
	} while (total_read <= to_read);
	ASSERT_EQ(nread, 0, "nread");
	/* Both so_reuseport ports should be in different buckets, so
	 * total_read must equal to the expected to_read.
	 *
	 * For a very unlikely case, both ports collide at the same bucket,
	 * the bucket offset (i.e. 3) will be skipped and it cannot
	 * expect the to_read number of bytes.
	 */
	if (skel->bss->bucket[0] != skel->bss->bucket[1])
		ASSERT_EQ(total_read, to_read, "total_read");

done:
	for (i = 0; i < ARRAY_SIZE(fds); i++)
		free_fds(fds[i], nr_soreuse);
	if (iter_fd < 0)
		close(iter_fd);
	bpf_link__destroy(link);
	sock_iter_batch__destroy(skel);
}

void test_sock_iter_batch(void)
{
	struct nstoken *nstoken = NULL;

	SYS_NOFAIL("ip netns del " TEST_NS " &> /dev/null");
	SYS(done, "ip netns add %s", TEST_NS);
	SYS(done, "ip -net %s link set dev lo up", TEST_NS);

	nstoken = open_netns(TEST_NS);
	if (!ASSERT_OK_PTR(nstoken, "open_netns"))
		goto done;

	if (test__start_subtest("tcp")) {
		do_test(SOCK_STREAM, true);
		do_test(SOCK_STREAM, false);
	}
	if (test__start_subtest("udp")) {
		do_test(SOCK_DGRAM, true);
		do_test(SOCK_DGRAM, false);
	}
	close_netns(nstoken);

done:
	SYS_NOFAIL("ip netns del " TEST_NS " &> /dev/null");
}
