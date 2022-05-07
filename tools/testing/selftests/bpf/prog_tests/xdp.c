// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

void test_xdp(void)
{
	struct vip key4 = {.protocol = 6, .family = AF_INET};
	struct vip key6 = {.protocol = 6, .family = AF_INET6};
	struct iptnl_info value4 = {.family = AF_INET};
	struct iptnl_info value6 = {.family = AF_INET6};
	const char *file = "./test_xdp.o";
	struct bpf_object *obj;
	char buf[128];
	struct ipv6hdr *iph6 = (void *)buf + sizeof(struct ethhdr);
	struct iphdr *iph = (void *)buf + sizeof(struct ethhdr);
	__u32 duration, retval, size;
	int err, prog_fd, map_fd;

	err = bpf_prog_load(file, BPF_PROG_TYPE_XDP, &obj, &prog_fd);
	if (CHECK_FAIL(err))
		return;

	map_fd = bpf_find_map(__func__, obj, "vip2tnl");
	if (map_fd < 0)
		goto out;
	bpf_map_update_elem(map_fd, &key4, &value4, 0);
	bpf_map_update_elem(map_fd, &key6, &value6, 0);

	err = bpf_prog_test_run(prog_fd, 1, &pkt_v4, sizeof(pkt_v4),
				buf, &size, &retval, &duration);

	CHECK(err || retval != XDP_TX || size != 74 ||
	      iph->protocol != IPPROTO_IPIP, "ipv4",
	      "err %d errno %d retval %d size %d\n",
	      err, errno, retval, size);

	err = bpf_prog_test_run(prog_fd, 1, &pkt_v6, sizeof(pkt_v6),
				buf, &size, &retval, &duration);
	CHECK(err || retval != XDP_TX || size != 114 ||
	      iph6->nexthdr != IPPROTO_IPV6, "ipv6",
	      "err %d errno %d retval %d size %d\n",
	      err, errno, retval, size);
out:
	bpf_object__close(obj);
}
