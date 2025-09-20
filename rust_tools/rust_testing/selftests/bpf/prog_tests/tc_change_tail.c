// SPDX-License-Identifier: GPL-2.0
#include <error.h>
#include <test_progs.h>
#include <linux/pkt_cls.h>

#include "test_tc_change_tail.skel.h"
#include "socket_helpers.h"

#define LO_IFINDEX 1

void test_tc_change_tail(void)
{
	LIBBPF_OPTS(bpf_tcx_opts, tcx_opts);
	struct test_tc_change_tail *skel = NULL;
	struct bpf_link *link;
	int c1, p1;
	char buf[2];
	int ret;

	skel = test_tc_change_tail__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_tc_change_tail__open_and_load"))
		return;

	link = bpf_program__attach_tcx(skel->progs.change_tail, LO_IFINDEX,
				     &tcx_opts);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach_tcx"))
		goto destroy;

	skel->links.change_tail = link;
	ret = create_pair(AF_INET, SOCK_DGRAM, &c1, &p1);
	if (!ASSERT_OK(ret, "create_pair"))
		goto destroy;

	ret = xsend(p1, "Tr", 2, 0);
	ASSERT_EQ(ret, 2, "xsend(p1)");
	ret = recv(c1, buf, 2, 0);
	ASSERT_EQ(ret, 2, "recv(c1)");
	ASSERT_EQ(skel->data->change_tail_ret, 0, "change_tail_ret");

	ret = xsend(p1, "G", 1, 0);
	ASSERT_EQ(ret, 1, "xsend(p1)");
	ret = recv(c1, buf, 2, 0);
	ASSERT_EQ(ret, 1, "recv(c1)");
	ASSERT_EQ(skel->data->change_tail_ret, 0, "change_tail_ret");

	ret = xsend(p1, "E", 1, 0);
	ASSERT_EQ(ret, 1, "xsend(p1)");
	ret = recv(c1, buf, 1, 0);
	ASSERT_EQ(ret, 1, "recv(c1)");
	ASSERT_EQ(skel->data->change_tail_ret, -EINVAL, "change_tail_ret");

	ret = xsend(p1, "Z", 1, 0);
	ASSERT_EQ(ret, 1, "xsend(p1)");
	ret = recv(c1, buf, 1, 0);
	ASSERT_EQ(ret, 1, "recv(c1)");
	ASSERT_EQ(skel->data->change_tail_ret, -EINVAL, "change_tail_ret");

	close(c1);
	close(p1);
destroy:
	test_tc_change_tail__destroy(skel);
}
