// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <net/if.h>
#include "test_xdp.skel.h"
#include "test_xdp_bpf2bpf.skel.h"

void test_xdp_bpf2bpf(void)
{
	__u32 duration = 0, retval, size;
	char buf[128];
	int err, pkt_fd, map_fd;
	struct iphdr *iph = (void *)buf + sizeof(struct ethhdr);
	struct iptnl_info value4 = {.family = AF_INET};
	struct test_xdp *pkt_skel = NULL;
	struct test_xdp_bpf2bpf *ftrace_skel = NULL;
	struct vip key4 = {.protocol = 6, .family = AF_INET};
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts);

	/* Load XDP program to introspect */
	pkt_skel = test_xdp__open_and_load();
	if (CHECK(!pkt_skel, "pkt_skel_load", "test_xdp skeleton failed\n"))
		return;

	pkt_fd = bpf_program__fd(pkt_skel->progs._xdp_tx_iptunnel);

	map_fd = bpf_map__fd(pkt_skel->maps.vip2tnl);
	bpf_map_update_elem(map_fd, &key4, &value4, 0);

	/* Load trace program */
	opts.attach_prog_fd = pkt_fd,
	ftrace_skel = test_xdp_bpf2bpf__open_opts(&opts);
	if (CHECK(!ftrace_skel, "__open", "ftrace skeleton failed\n"))
		goto out;

	err = test_xdp_bpf2bpf__load(ftrace_skel);
	if (CHECK(err, "__load", "ftrace skeleton failed\n"))
		goto out;

	err = test_xdp_bpf2bpf__attach(ftrace_skel);
	if (CHECK(err, "ftrace_attach", "ftrace attach failed: %d\n", err))
		goto out;

	/* Run test program */
	err = bpf_prog_test_run(pkt_fd, 1, &pkt_v4, sizeof(pkt_v4),
				buf, &size, &retval, &duration);

	if (CHECK(err || retval != XDP_TX || size != 74 ||
		  iph->protocol != IPPROTO_IPIP, "ipv4",
		  "err %d errno %d retval %d size %d\n",
		  err, errno, retval, size))
		goto out;

	/* Verify test results */
	if (CHECK(ftrace_skel->bss->test_result_fentry != if_nametoindex("lo"),
		  "result", "fentry failed err %llu\n",
		  ftrace_skel->bss->test_result_fentry))
		goto out;

	CHECK(ftrace_skel->bss->test_result_fexit != XDP_TX, "result",
	      "fexit failed err %llu\n", ftrace_skel->bss->test_result_fexit);

out:
	test_xdp__destroy(pkt_skel);
	test_xdp_bpf2bpf__destroy(ftrace_skel);
}
