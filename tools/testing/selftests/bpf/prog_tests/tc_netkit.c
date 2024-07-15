// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Isovalent */
#include <uapi/linux/if_link.h>
#include <net/if.h>
#include <test_progs.h>

#define netkit_peer "nk0"
#define netkit_name "nk1"

#define ping_addr_neigh		0x0a000002 /* 10.0.0.2 */
#define ping_addr_noneigh	0x0a000003 /* 10.0.0.3 */

#include "test_tc_link.skel.h"
#include "netlink_helpers.h"
#include "tc_helpers.h"

#define ICMP_ECHO 8

struct icmphdr {
	__u8		type;
	__u8		code;
	__sum16		checksum;
	struct {
		__be16	id;
		__be16	sequence;
	} echo;
};

struct iplink_req {
	struct nlmsghdr  n;
	struct ifinfomsg i;
	char             buf[1024];
};

static int create_netkit(int mode, int policy, int peer_policy, int *ifindex,
			 bool same_netns)
{
	struct rtnl_handle rth = { .fd = -1 };
	struct iplink_req req = {};
	struct rtattr *linkinfo, *data;
	const char *type = "netkit";
	int err;

	err = rtnl_open(&rth, 0);
	if (!ASSERT_OK(err, "open_rtnetlink"))
		return err;

	memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL;
	req.n.nlmsg_type = RTM_NEWLINK;
	req.i.ifi_family = AF_UNSPEC;

	addattr_l(&req.n, sizeof(req), IFLA_IFNAME, netkit_name,
		  strlen(netkit_name));
	linkinfo = addattr_nest(&req.n, sizeof(req), IFLA_LINKINFO);
	addattr_l(&req.n, sizeof(req), IFLA_INFO_KIND, type, strlen(type));
	data = addattr_nest(&req.n, sizeof(req), IFLA_INFO_DATA);
	addattr32(&req.n, sizeof(req), IFLA_NETKIT_POLICY, policy);
	addattr32(&req.n, sizeof(req), IFLA_NETKIT_PEER_POLICY, peer_policy);
	addattr32(&req.n, sizeof(req), IFLA_NETKIT_MODE, mode);
	addattr_nest_end(&req.n, data);
	addattr_nest_end(&req.n, linkinfo);

	err = rtnl_talk(&rth, &req.n, NULL);
	ASSERT_OK(err, "talk_rtnetlink");
	rtnl_close(&rth);
	*ifindex = if_nametoindex(netkit_name);

	ASSERT_GT(*ifindex, 0, "retrieve_ifindex");
	ASSERT_OK(system("ip netns add foo"), "create netns");
	ASSERT_OK(system("ip link set dev " netkit_name " up"),
			 "up primary");
	ASSERT_OK(system("ip addr add dev " netkit_name " 10.0.0.1/24"),
			 "addr primary");
	if (same_netns) {
		ASSERT_OK(system("ip link set dev " netkit_peer " up"),
				 "up peer");
		ASSERT_OK(system("ip addr add dev " netkit_peer " 10.0.0.2/24"),
				 "addr peer");
	} else {
		ASSERT_OK(system("ip link set " netkit_peer " netns foo"),
				 "move peer");
		ASSERT_OK(system("ip netns exec foo ip link set dev "
				 netkit_peer " up"), "up peer");
		ASSERT_OK(system("ip netns exec foo ip addr add dev "
				 netkit_peer " 10.0.0.2/24"), "addr peer");
	}
	return err;
}

static void destroy_netkit(void)
{
	ASSERT_OK(system("ip link del dev " netkit_name), "del primary");
	ASSERT_OK(system("ip netns del foo"), "delete netns");
	ASSERT_EQ(if_nametoindex(netkit_name), 0, netkit_name "_ifindex");
}

static int __send_icmp(__u32 dest)
{
	struct sockaddr_in addr;
	struct icmphdr icmp;
	int sock, ret;

	ret = write_sysctl("/proc/sys/net/ipv4/ping_group_range", "0 0");
	if (!ASSERT_OK(ret, "write_sysctl(net.ipv4.ping_group_range)"))
		return ret;

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
	if (!ASSERT_GE(sock, 0, "icmp_socket"))
		return -errno;

	ret = setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE,
			 netkit_name, strlen(netkit_name) + 1);
	if (!ASSERT_OK(ret, "setsockopt(SO_BINDTODEVICE)"))
		goto out;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(dest);

	memset(&icmp, 0, sizeof(icmp));
	icmp.type = ICMP_ECHO;
	icmp.echo.id = 1234;
	icmp.echo.sequence = 1;

	ret = sendto(sock, &icmp, sizeof(icmp), 0,
		     (struct sockaddr *)&addr, sizeof(addr));
	if (!ASSERT_GE(ret, 0, "icmp_sendto"))
		ret = -errno;
	else
		ret = 0;
out:
	close(sock);
	return ret;
}

static int send_icmp(void)
{
	return __send_icmp(ping_addr_neigh);
}

void serial_test_tc_netkit_basic(void)
{
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	LIBBPF_OPTS(bpf_netkit_opts, optl);
	__u32 prog_ids[2], link_ids[2];
	__u32 pid1, pid2, lid1, lid2;
	struct test_tc_link *skel;
	struct bpf_link *link;
	int err, ifindex;

	err = create_netkit(NETKIT_L2, NETKIT_PASS, NETKIT_PASS,
			    &ifindex, false);
	if (err)
		return;

	skel = test_tc_link__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		goto cleanup;

	ASSERT_EQ(bpf_program__set_expected_attach_type(skel->progs.tc1,
		  BPF_NETKIT_PRIMARY), 0, "tc1_attach_type");
	ASSERT_EQ(bpf_program__set_expected_attach_type(skel->progs.tc2,
		  BPF_NETKIT_PEER), 0, "tc2_attach_type");

	err = test_tc_link__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	pid1 = id_from_prog_fd(bpf_program__fd(skel->progs.tc1));
	pid2 = id_from_prog_fd(bpf_program__fd(skel->progs.tc2));

	ASSERT_NEQ(pid1, pid2, "prog_ids_1_2");

	assert_mprog_count_ifindex(ifindex, BPF_NETKIT_PRIMARY, 0);
	assert_mprog_count_ifindex(ifindex, BPF_NETKIT_PEER, 0);

	ASSERT_EQ(skel->bss->seen_tc1, false, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, false, "seen_tc2");

	link = bpf_program__attach_netkit(skel->progs.tc1, ifindex, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc1 = link;

	lid1 = id_from_link_fd(bpf_link__fd(skel->links.tc1));

	assert_mprog_count_ifindex(ifindex, BPF_NETKIT_PRIMARY, 1);
	assert_mprog_count_ifindex(ifindex, BPF_NETKIT_PEER, 0);

	optq.prog_ids = prog_ids;
	optq.link_ids = link_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(ifindex, BPF_NETKIT_PRIMARY, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 1, "count");
	ASSERT_EQ(optq.revision, 2, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid1, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid1, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], 0, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], 0, "link_ids[1]");

	tc_skel_reset_all_seen(skel);
	ASSERT_EQ(send_icmp(), 0, "icmp_pkt");

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, false, "seen_tc2");

	link = bpf_program__attach_netkit(skel->progs.tc2, ifindex, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc2 = link;

	lid2 = id_from_link_fd(bpf_link__fd(skel->links.tc2));
	ASSERT_NEQ(lid1, lid2, "link_ids_1_2");

	assert_mprog_count_ifindex(ifindex, BPF_NETKIT_PRIMARY, 1);
	assert_mprog_count_ifindex(ifindex, BPF_NETKIT_PEER, 1);

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(ifindex, BPF_NETKIT_PEER, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 1, "count");
	ASSERT_EQ(optq.revision, 2, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid2, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid2, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], 0, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], 0, "link_ids[1]");

	tc_skel_reset_all_seen(skel);
	ASSERT_EQ(send_icmp(), 0, "icmp_pkt");

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
cleanup:
	test_tc_link__destroy(skel);

	assert_mprog_count_ifindex(ifindex, BPF_NETKIT_PRIMARY, 0);
	assert_mprog_count_ifindex(ifindex, BPF_NETKIT_PEER, 0);
	destroy_netkit();
}

static void serial_test_tc_netkit_multi_links_target(int mode, int target)
{
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	LIBBPF_OPTS(bpf_netkit_opts, optl);
	__u32 prog_ids[3], link_ids[3];
	__u32 pid1, pid2, lid1, lid2;
	struct test_tc_link *skel;
	struct bpf_link *link;
	int err, ifindex;

	err = create_netkit(mode, NETKIT_PASS, NETKIT_PASS,
			    &ifindex, false);
	if (err)
		return;

	skel = test_tc_link__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		goto cleanup;

	ASSERT_EQ(bpf_program__set_expected_attach_type(skel->progs.tc1,
		  target), 0, "tc1_attach_type");
	ASSERT_EQ(bpf_program__set_expected_attach_type(skel->progs.tc2,
		  target), 0, "tc2_attach_type");

	err = test_tc_link__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	pid1 = id_from_prog_fd(bpf_program__fd(skel->progs.tc1));
	pid2 = id_from_prog_fd(bpf_program__fd(skel->progs.tc2));

	ASSERT_NEQ(pid1, pid2, "prog_ids_1_2");

	assert_mprog_count_ifindex(ifindex, target, 0);

	ASSERT_EQ(skel->bss->seen_tc1, false, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_eth, false, "seen_eth");
	ASSERT_EQ(skel->bss->seen_tc2, false, "seen_tc2");

	link = bpf_program__attach_netkit(skel->progs.tc1, ifindex, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc1 = link;

	lid1 = id_from_link_fd(bpf_link__fd(skel->links.tc1));

	assert_mprog_count_ifindex(ifindex, target, 1);

	optq.prog_ids = prog_ids;
	optq.link_ids = link_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(ifindex, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 1, "count");
	ASSERT_EQ(optq.revision, 2, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid1, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid1, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], 0, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], 0, "link_ids[1]");

	tc_skel_reset_all_seen(skel);
	ASSERT_EQ(send_icmp(), 0, "icmp_pkt");

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_eth, true, "seen_eth");
	ASSERT_EQ(skel->bss->seen_tc2, false, "seen_tc2");

	LIBBPF_OPTS_RESET(optl,
		.flags = BPF_F_BEFORE,
		.relative_fd = bpf_program__fd(skel->progs.tc1),
	);

	link = bpf_program__attach_netkit(skel->progs.tc2, ifindex, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc2 = link;

	lid2 = id_from_link_fd(bpf_link__fd(skel->links.tc2));
	ASSERT_NEQ(lid1, lid2, "link_ids_1_2");

	assert_mprog_count_ifindex(ifindex, target, 2);

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(ifindex, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 3, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid2, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid2, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], pid1, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], lid1, "link_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");
	ASSERT_EQ(optq.link_ids[2], 0, "link_ids[2]");

	tc_skel_reset_all_seen(skel);
	ASSERT_EQ(send_icmp(), 0, "icmp_pkt");

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_eth, true, "seen_eth");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");
cleanup:
	test_tc_link__destroy(skel);

	assert_mprog_count_ifindex(ifindex, target, 0);
	destroy_netkit();
}

void serial_test_tc_netkit_multi_links(void)
{
	serial_test_tc_netkit_multi_links_target(NETKIT_L2, BPF_NETKIT_PRIMARY);
	serial_test_tc_netkit_multi_links_target(NETKIT_L3, BPF_NETKIT_PRIMARY);
	serial_test_tc_netkit_multi_links_target(NETKIT_L2, BPF_NETKIT_PEER);
	serial_test_tc_netkit_multi_links_target(NETKIT_L3, BPF_NETKIT_PEER);
}

static void serial_test_tc_netkit_multi_opts_target(int mode, int target)
{
	LIBBPF_OPTS(bpf_prog_attach_opts, opta);
	LIBBPF_OPTS(bpf_prog_detach_opts, optd);
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	__u32 pid1, pid2, fd1, fd2;
	__u32 prog_ids[3];
	struct test_tc_link *skel;
	int err, ifindex;

	err = create_netkit(mode, NETKIT_PASS, NETKIT_PASS,
			    &ifindex, false);
	if (err)
		return;

	skel = test_tc_link__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto cleanup;

	fd1 = bpf_program__fd(skel->progs.tc1);
	fd2 = bpf_program__fd(skel->progs.tc2);

	pid1 = id_from_prog_fd(fd1);
	pid2 = id_from_prog_fd(fd2);

	ASSERT_NEQ(pid1, pid2, "prog_ids_1_2");

	assert_mprog_count_ifindex(ifindex, target, 0);

	ASSERT_EQ(skel->bss->seen_tc1, false, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_eth, false, "seen_eth");
	ASSERT_EQ(skel->bss->seen_tc2, false, "seen_tc2");

	err = bpf_prog_attach_opts(fd1, ifindex, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup;

	assert_mprog_count_ifindex(ifindex, target, 1);

	optq.prog_ids = prog_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(ifindex, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_fd1;

	ASSERT_EQ(optq.count, 1, "count");
	ASSERT_EQ(optq.revision, 2, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid1, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], 0, "prog_ids[1]");

	tc_skel_reset_all_seen(skel);
	ASSERT_EQ(send_icmp(), 0, "icmp_pkt");

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_eth, true, "seen_eth");
	ASSERT_EQ(skel->bss->seen_tc2, false, "seen_tc2");

	LIBBPF_OPTS_RESET(opta,
		.flags = BPF_F_BEFORE,
		.relative_fd = fd1,
	);

	err = bpf_prog_attach_opts(fd2, ifindex, target, &opta);
	if (!ASSERT_EQ(err, 0, "prog_attach"))
		goto cleanup_fd1;

	assert_mprog_count_ifindex(ifindex, target, 2);

	memset(prog_ids, 0, sizeof(prog_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(ifindex, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup_fd2;

	ASSERT_EQ(optq.count, 2, "count");
	ASSERT_EQ(optq.revision, 3, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid2, "prog_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], pid1, "prog_ids[1]");
	ASSERT_EQ(optq.prog_ids[2], 0, "prog_ids[2]");

	tc_skel_reset_all_seen(skel);
	ASSERT_EQ(send_icmp(), 0, "icmp_pkt");

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_eth, true, "seen_eth");
	ASSERT_EQ(skel->bss->seen_tc2, true, "seen_tc2");

cleanup_fd2:
	err = bpf_prog_detach_opts(fd2, ifindex, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count_ifindex(ifindex, target, 1);
cleanup_fd1:
	err = bpf_prog_detach_opts(fd1, ifindex, target, &optd);
	ASSERT_OK(err, "prog_detach");
	assert_mprog_count_ifindex(ifindex, target, 0);
cleanup:
	test_tc_link__destroy(skel);

	assert_mprog_count_ifindex(ifindex, target, 0);
	destroy_netkit();
}

void serial_test_tc_netkit_multi_opts(void)
{
	serial_test_tc_netkit_multi_opts_target(NETKIT_L2, BPF_NETKIT_PRIMARY);
	serial_test_tc_netkit_multi_opts_target(NETKIT_L3, BPF_NETKIT_PRIMARY);
	serial_test_tc_netkit_multi_opts_target(NETKIT_L2, BPF_NETKIT_PEER);
	serial_test_tc_netkit_multi_opts_target(NETKIT_L3, BPF_NETKIT_PEER);
}

void serial_test_tc_netkit_device(void)
{
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	LIBBPF_OPTS(bpf_netkit_opts, optl);
	__u32 prog_ids[2], link_ids[2];
	__u32 pid1, pid2, lid1;
	struct test_tc_link *skel;
	struct bpf_link *link;
	int err, ifindex, ifindex2;

	err = create_netkit(NETKIT_L3, NETKIT_PASS, NETKIT_PASS,
			    &ifindex, true);
	if (err)
		return;

	ifindex2 = if_nametoindex(netkit_peer);
	ASSERT_NEQ(ifindex, ifindex2, "ifindex_1_2");

	skel = test_tc_link__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		goto cleanup;

	ASSERT_EQ(bpf_program__set_expected_attach_type(skel->progs.tc1,
		  BPF_NETKIT_PRIMARY), 0, "tc1_attach_type");
	ASSERT_EQ(bpf_program__set_expected_attach_type(skel->progs.tc2,
		  BPF_NETKIT_PEER), 0, "tc2_attach_type");
	ASSERT_EQ(bpf_program__set_expected_attach_type(skel->progs.tc3,
		  BPF_NETKIT_PRIMARY), 0, "tc3_attach_type");

	err = test_tc_link__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	pid1 = id_from_prog_fd(bpf_program__fd(skel->progs.tc1));
	pid2 = id_from_prog_fd(bpf_program__fd(skel->progs.tc2));

	ASSERT_NEQ(pid1, pid2, "prog_ids_1_2");

	assert_mprog_count_ifindex(ifindex, BPF_NETKIT_PRIMARY, 0);
	assert_mprog_count_ifindex(ifindex, BPF_NETKIT_PEER, 0);

	ASSERT_EQ(skel->bss->seen_tc1, false, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, false, "seen_tc2");

	link = bpf_program__attach_netkit(skel->progs.tc1, ifindex, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc1 = link;

	lid1 = id_from_link_fd(bpf_link__fd(skel->links.tc1));

	assert_mprog_count_ifindex(ifindex, BPF_NETKIT_PRIMARY, 1);
	assert_mprog_count_ifindex(ifindex, BPF_NETKIT_PEER, 0);

	optq.prog_ids = prog_ids;
	optq.link_ids = link_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(ifindex, BPF_NETKIT_PRIMARY, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 1, "count");
	ASSERT_EQ(optq.revision, 2, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid1, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid1, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], 0, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], 0, "link_ids[1]");

	tc_skel_reset_all_seen(skel);
	ASSERT_EQ(send_icmp(), 0, "icmp_pkt");

	ASSERT_EQ(skel->bss->seen_tc1, true, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_tc2, false, "seen_tc2");

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(ifindex2, BPF_NETKIT_PRIMARY, &optq);
	ASSERT_EQ(err, -EACCES, "prog_query_should_fail");

	err = bpf_prog_query_opts(ifindex2, BPF_NETKIT_PEER, &optq);
	ASSERT_EQ(err, -EACCES, "prog_query_should_fail");

	link = bpf_program__attach_netkit(skel->progs.tc2, ifindex2, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	link = bpf_program__attach_netkit(skel->progs.tc3, ifindex2, &optl);
	if (!ASSERT_ERR_PTR(link, "link_attach_should_fail")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	assert_mprog_count_ifindex(ifindex, BPF_NETKIT_PRIMARY, 1);
	assert_mprog_count_ifindex(ifindex, BPF_NETKIT_PEER, 0);
cleanup:
	test_tc_link__destroy(skel);

	assert_mprog_count_ifindex(ifindex, BPF_NETKIT_PRIMARY, 0);
	assert_mprog_count_ifindex(ifindex, BPF_NETKIT_PEER, 0);
	destroy_netkit();
}

static void serial_test_tc_netkit_neigh_links_target(int mode, int target)
{
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	LIBBPF_OPTS(bpf_netkit_opts, optl);
	__u32 prog_ids[2], link_ids[2];
	__u32 pid1, lid1;
	struct test_tc_link *skel;
	struct bpf_link *link;
	int err, ifindex;

	err = create_netkit(mode, NETKIT_PASS, NETKIT_PASS,
			    &ifindex, false);
	if (err)
		return;

	skel = test_tc_link__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		goto cleanup;

	ASSERT_EQ(bpf_program__set_expected_attach_type(skel->progs.tc1,
		  BPF_NETKIT_PRIMARY), 0, "tc1_attach_type");

	err = test_tc_link__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	pid1 = id_from_prog_fd(bpf_program__fd(skel->progs.tc1));

	assert_mprog_count_ifindex(ifindex, target, 0);

	ASSERT_EQ(skel->bss->seen_tc1, false, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_eth, false, "seen_eth");

	link = bpf_program__attach_netkit(skel->progs.tc1, ifindex, &optl);
	if (!ASSERT_OK_PTR(link, "link_attach"))
		goto cleanup;

	skel->links.tc1 = link;

	lid1 = id_from_link_fd(bpf_link__fd(skel->links.tc1));

	assert_mprog_count_ifindex(ifindex, target, 1);

	optq.prog_ids = prog_ids;
	optq.link_ids = link_ids;

	memset(prog_ids, 0, sizeof(prog_ids));
	memset(link_ids, 0, sizeof(link_ids));
	optq.count = ARRAY_SIZE(prog_ids);

	err = bpf_prog_query_opts(ifindex, target, &optq);
	if (!ASSERT_OK(err, "prog_query"))
		goto cleanup;

	ASSERT_EQ(optq.count, 1, "count");
	ASSERT_EQ(optq.revision, 2, "revision");
	ASSERT_EQ(optq.prog_ids[0], pid1, "prog_ids[0]");
	ASSERT_EQ(optq.link_ids[0], lid1, "link_ids[0]");
	ASSERT_EQ(optq.prog_ids[1], 0, "prog_ids[1]");
	ASSERT_EQ(optq.link_ids[1], 0, "link_ids[1]");

	tc_skel_reset_all_seen(skel);
	ASSERT_EQ(__send_icmp(ping_addr_noneigh), 0, "icmp_pkt");

	ASSERT_EQ(skel->bss->seen_tc1, true /* L2: ARP */, "seen_tc1");
	ASSERT_EQ(skel->bss->seen_eth, mode == NETKIT_L3, "seen_eth");
cleanup:
	test_tc_link__destroy(skel);

	assert_mprog_count_ifindex(ifindex, target, 0);
	destroy_netkit();
}

void serial_test_tc_netkit_neigh_links(void)
{
	serial_test_tc_netkit_neigh_links_target(NETKIT_L2, BPF_NETKIT_PRIMARY);
	serial_test_tc_netkit_neigh_links_target(NETKIT_L3, BPF_NETKIT_PRIMARY);
}
