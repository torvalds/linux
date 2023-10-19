// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>
#include <net/if.h>
#include "empty_skb.skel.h"

#define SYS(cmd) ({ \
	if (!ASSERT_OK(system(cmd), (cmd))) \
		goto out; \
})

void serial_test_empty_skb(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, tattr);
	struct empty_skb *bpf_obj = NULL;
	struct nstoken *tok = NULL;
	struct bpf_program *prog;
	char eth_hlen_pp[15];
	char eth_hlen[14];
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
		bool success_on_tc;
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

		/* ETH_HLEN-sized packets:
		 * - can not be redirected at LWT_XMIT
		 * - can be redirected at TC to non-tunneling dest
		 */

		{
			/* __bpf_redirect_common */
			.msg = "veth ETH_HLEN packet ingress",
			.data_in = eth_hlen,
			.data_size_in = sizeof(eth_hlen),
			.ifindex = &veth_ifindex,
			.ret = -ERANGE,
			.success_on_tc = true,
		},
		{
			/* __bpf_redirect_no_mac
			 *
			 * lwt: skb->len=0 <= skb_network_offset=0
			 * tc: skb->len=14 <= skb_network_offset=14
			 */
			.msg = "ipip ETH_HLEN packet ingress",
			.data_in = eth_hlen,
			.data_size_in = sizeof(eth_hlen),
			.ifindex = &ipip_ifindex,
			.ret = -ERANGE,
		},

		/* ETH_HLEN+1-sized packet should be redirected. */

		{
			.msg = "veth ETH_HLEN+1 packet ingress",
			.data_in = eth_hlen_pp,
			.data_size_in = sizeof(eth_hlen_pp),
			.ifindex = &veth_ifindex,
		},
		{
			.msg = "ipip ETH_HLEN+1 packet ingress",
			.data_in = eth_hlen_pp,
			.data_size_in = sizeof(eth_hlen_pp),
			.ifindex = &ipip_ifindex,
		},
	};

	SYS("ip netns add empty_skb");
	tok = open_netns("empty_skb");
	SYS("ip link add veth0 type veth peer veth1");
	SYS("ip link set dev veth0 up");
	SYS("ip link set dev veth1 up");
	SYS("ip addr add 10.0.0.1/8 dev veth0");
	SYS("ip addr add 10.0.0.2/8 dev veth1");
	veth_ifindex = if_nametoindex("veth0");

	SYS("ip link add ipip0 type ipip local 10.0.0.1 remote 10.0.0.2");
	SYS("ip link set ipip0 up");
	SYS("ip addr add 192.168.1.1/16 dev ipip0");
	ipip_ifindex = if_nametoindex("ipip0");

	bpf_obj = empty_skb__open_and_load();
	if (!ASSERT_OK_PTR(bpf_obj, "open skeleton"))
		goto out;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		bpf_object__for_each_program(prog, bpf_obj->obj) {
			char buf[128];
			bool at_tc = !strncmp(bpf_program__section_name(prog), "tc", 2);

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
				ASSERT_EQ(bpf_obj->bss->ret, tests[i].ret, buf);
		}
	}

out:
	if (bpf_obj)
		empty_skb__destroy(bpf_obj);
	if (tok)
		close_netns(tok);
	system("ip netns del empty_skb");
}
