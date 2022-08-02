// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

static void test_l4lb(const char *file)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
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
	int err, i, prog_fd, map_fd;
	__u64 bytes = 0, pkts = 0;
	struct bpf_object *obj;
	char buf[128];
	u32 *magic = (u32 *)buf;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_out = buf,
		.data_size_out = sizeof(buf),
		.repeat = NUM_ITER,
	);

	err = bpf_prog_test_load(file, BPF_PROG_TYPE_SCHED_CLS, &obj, &prog_fd);
	if (CHECK_FAIL(err))
		return;

	map_fd = bpf_find_map(__func__, obj, "vip_map");
	if (map_fd < 0)
		goto out;
	bpf_map_update_elem(map_fd, &key, &value, 0);

	map_fd = bpf_find_map(__func__, obj, "ch_rings");
	if (map_fd < 0)
		goto out;
	bpf_map_update_elem(map_fd, &ch_key, &real_num, 0);

	map_fd = bpf_find_map(__func__, obj, "reals");
	if (map_fd < 0)
		goto out;
	bpf_map_update_elem(map_fd, &real_num, &real_def, 0);

	topts.data_in = &pkt_v4;
	topts.data_size_in = sizeof(pkt_v4);

	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 7 /*TC_ACT_REDIRECT*/, "ipv4 test_run retval");
	ASSERT_EQ(topts.data_size_out, 54, "ipv4 test_run data_size_out");
	ASSERT_EQ(*magic, MAGIC_VAL, "ipv4 magic");

	topts.data_in = &pkt_v6;
	topts.data_size_in = sizeof(pkt_v6);
	topts.data_size_out = sizeof(buf); /* reset out size */

	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 7 /*TC_ACT_REDIRECT*/, "ipv6 test_run retval");
	ASSERT_EQ(topts.data_size_out, 74, "ipv6 test_run data_size_out");
	ASSERT_EQ(*magic, MAGIC_VAL, "ipv6 magic");

	map_fd = bpf_find_map(__func__, obj, "stats");
	if (map_fd < 0)
		goto out;
	bpf_map_lookup_elem(map_fd, &stats_key, stats);
	for (i = 0; i < nr_cpus; i++) {
		bytes += stats[i].bytes;
		pkts += stats[i].pkts;
	}
	if (CHECK_FAIL(bytes != MAGIC_BYTES * NUM_ITER * 2 ||
		       pkts != NUM_ITER * 2))
		printf("test_l4lb:FAIL:stats %lld %lld\n", bytes, pkts);
out:
	bpf_object__close(obj);
}

void test_l4lb_all(void)
{
	if (test__start_subtest("l4lb_inline"))
		test_l4lb("test_l4lb.o");
	if (test__start_subtest("l4lb_noinline"))
		test_l4lb("test_l4lb_noinline.o");
}
