// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>
#include "test_xdp_noinline.skel.h"

void test_xdp_noinline(void)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	struct test_xdp_noinline *skel;
	struct vip key = {.protocol = 6};
	struct vip_meta {
		__u32 flags;
		__u32 vip_num;
	} value = {.vip_num = VIP_NUM};
	__u32 stats_key = VIP_NUM;
	struct vip_stats {
		__u64 bytes;
		__u64 pkts;
	} stats[nr_cpus];
	struct real_definition {
		union {
			__be32 dst;
			__be32 dstv6[4];
		};
		__u8 flags;
	} real_def = {.dst = MAGIC_VAL};
	__u32 ch_key = 11, real_num = 3;
	__u32 duration = 0, retval, size;
	int err, i;
	__u64 bytes = 0, pkts = 0;
	char buf[128];
	u32 *magic = (u32 *)buf;

	skel = test_xdp_noinline__open_and_load();
	if (CHECK(!skel, "skel_open_and_load", "failed\n"))
		return;

	bpf_map_update_elem(bpf_map__fd(skel->maps.vip_map), &key, &value, 0);
	bpf_map_update_elem(bpf_map__fd(skel->maps.ch_rings), &ch_key, &real_num, 0);
	bpf_map_update_elem(bpf_map__fd(skel->maps.reals), &real_num, &real_def, 0);

	err = bpf_prog_test_run(bpf_program__fd(skel->progs.balancer_ingress_v4),
				NUM_ITER, &pkt_v4, sizeof(pkt_v4),
				buf, &size, &retval, &duration);
	CHECK(err || retval != 1 || size != 54 ||
	      *magic != MAGIC_VAL, "ipv4",
	      "err %d errno %d retval %d size %d magic %x\n",
	      err, errno, retval, size, *magic);

	err = bpf_prog_test_run(bpf_program__fd(skel->progs.balancer_ingress_v6),
				NUM_ITER, &pkt_v6, sizeof(pkt_v6),
				buf, &size, &retval, &duration);
	CHECK(err || retval != 1 || size != 74 ||
	      *magic != MAGIC_VAL, "ipv6",
	      "err %d errno %d retval %d size %d magic %x\n",
	      err, errno, retval, size, *magic);

	bpf_map_lookup_elem(bpf_map__fd(skel->maps.stats), &stats_key, stats);
	for (i = 0; i < nr_cpus; i++) {
		bytes += stats[i].bytes;
		pkts += stats[i].pkts;
	}
	CHECK(bytes != MAGIC_BYTES * NUM_ITER * 2 || pkts != NUM_ITER * 2,
	      "stats", "bytes %lld pkts %lld\n",
	      (unsigned long long)bytes, (unsigned long long)pkts);
	test_xdp_noinline__destroy(skel);
}
