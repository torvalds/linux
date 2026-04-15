// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>
#include <net/if.h>
#include "empty_skb.skel.h"

void test_empty_skb(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, tattr);
	struct empty_skb *bpf_obj = NULL;
	struct nstoken *tok = NULL;
	struct bpf_program *prog;
	struct ethhdr eth_hlen;
	char eth_hlen_pp[15];
	int veth_ifindex;
	int ipip_ifindex;
	int err;
	int i;

	struct {
		const char *msg;
		const void *data_in;
		__u32 data_size_in;
		int *ifindex;
		int err;
		int ret;
		int lwt_egress_ret; /* expected retval at lwt/egress */
		__be16 h_proto;
		bool success_on_tc;
		bool adjust_room;
	} tests[] = {
		/* Empty packets are always rejected. */

		{
			/* BPF_PROG_RUN ETH_HLEN size check */
			.msg = "veth empty ingress packet",
			.data_in = NULL,
			.data_size_in = 0,
			.ifindex = &veth_ifindex,
			.err = -EINVAL,
		},
		{
			/* BPF_PROG_RUN ETH_HLEN size check */
			.msg = "ipip empty ingress packet",
			.data_in = NULL,
			.data_size_in = 0,
			.ifindex = &ipip_ifindex,
			.err = -EINVAL,
		},

		/* ETH_HLEN-sized packets with IPv4/IPv6 EtherType but
		 * no L3 header are rejected.
		 */
		{
			.msg = "veth short IPv4 ingress packet",
			.data_in = &eth_hlen,
			.data_size_in = sizeof(eth_hlen),
			.ifindex = &veth_ifindex,
			.err = -EINVAL,
			.h_proto = htons(ETH_P_IP),
			.adjust_room = true,
		},
		{
			.msg = "veth short IPv6 ingress packet",
			.data_in = &eth_hlen,
			.data_size_in = sizeof(eth_hlen),
			.ifindex = &veth_ifindex,
			.err = -EINVAL,
			.h_proto = htons(ETH_P_IPV6),
			.adjust_room = true,
		},

		/* ETH_HLEN-sized packets:
		 * - can not be redirected at LWT_XMIT
		 * - can be redirected at TC to non-tunneling dest
		 */

		{
			/* __bpf_redirect_common */
			.msg = "veth ETH_HLEN packet ingress",
			.data_in = &eth_hlen,
			.data_size_in = sizeof(eth_hlen),
			.ifindex = &veth_ifindex,
			.ret = -ERANGE,
			.lwt_egress_ret = -ERANGE,
			.success_on_tc = true,
		},
		{
			/* __bpf_redirect_no_mac
			 *
			 * lwt: skb->len=0 <= skb_network_offset=0
			 * tc: skb->len=14 <= skb_network_offset=14
			 */
			.msg = "ipip ETH_HLEN packet ingress",
			.data_in = &eth_hlen,
			.data_size_in = sizeof(eth_hlen),
			.ifindex = &ipip_ifindex,
			.ret = -ERANGE,
			.lwt_egress_ret = -ERANGE,
		},

		/* ETH_HLEN+1-sized packet should be redirected. */

		{
			.msg = "veth ETH_HLEN+1 packet ingress",
			.data_in = eth_hlen_pp,
			.data_size_in = sizeof(eth_hlen_pp),
			.ifindex = &veth_ifindex,
			.lwt_egress_ret = 1, /* veth_xmit NET_XMIT_DROP */
		},
		{
			.msg = "ipip ETH_HLEN+1 packet ingress",
			.data_in = eth_hlen_pp,
			.data_size_in = sizeof(eth_hlen_pp),
			.ifindex = &ipip_ifindex,
		},
	};

	SYS(out, "ip netns add empty_skb");
	tok = open_netns("empty_skb");
	if (!ASSERT_OK_PTR(tok, "setns"))
		goto out;
	SYS(out, "ip link add veth0 type veth peer veth1");
	SYS(out, "ip link set dev veth0 up");
	SYS(out, "ip link set dev veth1 up");
	SYS(out, "ip addr add 10.0.0.1/8 dev veth0");
	SYS(out, "ip addr add 10.0.0.2/8 dev veth1");
	veth_ifindex = if_nametoindex("veth0");

	SYS(out, "ip link add ipip0 type ipip local 10.0.0.1 remote 10.0.0.2");
	SYS(out, "ip link set ipip0 up");
	SYS(out, "ip addr add 192.168.1.1/16 dev ipip0");
	ipip_ifindex = if_nametoindex("ipip0");

	memset(eth_hlen_pp, 0, sizeof(eth_hlen_pp));
	memset(&eth_hlen, 0, sizeof(eth_hlen));

	bpf_obj = empty_skb__open_and_load();
	if (!ASSERT_OK_PTR(bpf_obj, "open skeleton"))
		goto out;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		if (tests[i].data_in == &eth_hlen)
			eth_hlen.h_proto = tests[i].h_proto;

		bpf_object__for_each_program(prog, bpf_obj->obj) {
			bool at_egress = strstr(bpf_program__name(prog), "egress") != NULL;
			bool at_tc = !strncmp(bpf_program__section_name(prog), "tc", 2);
			bool is_adjust_room = !strcmp(bpf_program__name(prog), "tc_adjust_room");
			int expected_ret;
			char buf[128];

			if (tests[i].adjust_room != is_adjust_room)
				continue;

			expected_ret = at_egress && !at_tc ? tests[i].lwt_egress_ret : tests[i].ret;

			tattr.data_in = tests[i].data_in;
			tattr.data_size_in = tests[i].data_size_in;

			tattr.data_size_out = 0;
			bpf_obj->bss->ifindex = *tests[i].ifindex;
			bpf_obj->bss->ret = 0;
			err = bpf_prog_test_run_opts(bpf_program__fd(prog), &tattr);
			sprintf(buf, "err: %s [%s]", tests[i].msg, bpf_program__name(prog));

			if (at_tc && tests[i].success_on_tc)
				ASSERT_GE(err, 0, buf);
			else
				ASSERT_EQ(err, tests[i].err, buf);
			sprintf(buf, "ret: %s [%s]", tests[i].msg, bpf_program__name(prog));
			if (at_tc && tests[i].success_on_tc)
				ASSERT_GE(bpf_obj->bss->ret, 0, buf);
			else
				ASSERT_EQ(bpf_obj->bss->ret, expected_ret, buf);
		}
	}

out:
	if (bpf_obj)
		empty_skb__destroy(bpf_obj);
	if (tok)
		close_netns(tok);
	SYS_NOFAIL("ip netns del empty_skb");
}
