// SPDX-License-Identifier: GPL-2.0
#include <net/if.h>
#include <test_progs.h>
#include <network_helpers.h>

#define LOCAL_NETNS "xdp_dev_bound_only_netns"

static int load_dummy_prog(char *name, __u32 ifindex, __u32 flags)
{
	struct bpf_insn insns[] = { BPF_MOV64_IMM(BPF_REG_0, 0), BPF_EXIT_INSN() };
	LIBBPF_OPTS(bpf_prog_load_opts, opts);

	opts.prog_flags = flags;
	opts.prog_ifindex = ifindex;
	return bpf_prog_load(BPF_PROG_TYPE_XDP, name, "GPL", insns, ARRAY_SIZE(insns), &opts);
}

/* A test case for bpf_offload_netdev->offload handling bug:
 * - create a veth device (does not support offload);
 * - create a device bound XDP program with BPF_F_XDP_DEV_BOUND_ONLY flag
 *   (such programs are not offloaded);
 * - create a device bound XDP program without flags (such programs are offloaded).
 * This might lead to 'BUG: kernel NULL pointer dereference'.
 */
void test_xdp_dev_bound_only_offdev(void)
{
	struct nstoken *tok = NULL;
	__u32 ifindex;
	int fd1 = -1;
	int fd2 = -1;

	SYS(out, "ip netns add " LOCAL_NETNS);
	tok = open_netns(LOCAL_NETNS);
	if (!ASSERT_OK_PTR(tok, "open_netns"))
		goto out;
	SYS(out, "ip link add eth42 type veth");
	ifindex = if_nametoindex("eth42");
	if (!ASSERT_NEQ(ifindex, 0, "if_nametoindex")) {
		perror("if_nametoindex");
		goto out;
	}
	fd1 = load_dummy_prog("dummy1", ifindex, BPF_F_XDP_DEV_BOUND_ONLY);
	if (!ASSERT_GE(fd1, 0, "load_dummy_prog #1")) {
		perror("load_dummy_prog #1");
		goto out;
	}
	/* Program with ifindex is considered offloaded, however veth
	 * does not support offload => error should be reported.
	 */
	fd2 = load_dummy_prog("dummy2", ifindex, 0);
	ASSERT_EQ(fd2, -EINVAL, "load_dummy_prog #2 (offloaded)");

out:
	close(fd1);
	close(fd2);
	close_netns(tok);
	/* eth42 was added inside netns, removing the netns will
	 * also remove eth42 veth pair.
	 */
	SYS_NOFAIL("ip netns del " LOCAL_NETNS);
}
