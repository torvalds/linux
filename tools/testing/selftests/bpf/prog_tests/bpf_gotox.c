// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>

#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/in6.h>
#include <linux/udp.h>
#include <linux/tcp.h>

#include <sys/syscall.h>
#include <bpf/bpf.h>

#include "bpf_gotox.skel.h"

static void __test_run(struct bpf_program *prog, void *ctx_in, size_t ctx_size_in)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts,
			    .ctx_in = ctx_in,
			    .ctx_size_in = ctx_size_in,
		   );
	int err, prog_fd;

	prog_fd = bpf_program__fd(prog);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run_opts err");
}

static void __subtest(struct bpf_gotox *skel, void (*check)(struct bpf_gotox *))
{
	if (skel->data->skip)
		test__skip();
	else
		check(skel);
}

static void check_simple(struct bpf_gotox *skel,
			 struct bpf_program *prog,
			 __u64 ctx_in,
			 __u64 expected)
{
	skel->bss->ret_user = 0;

	__test_run(prog, &ctx_in, sizeof(ctx_in));

	if (!ASSERT_EQ(skel->bss->ret_user, expected, "skel->bss->ret_user"))
		return;
}

static void check_simple_fentry(struct bpf_gotox *skel,
				struct bpf_program *prog,
				__u64 ctx_in,
				__u64 expected)
{
	skel->bss->in_user = ctx_in;
	skel->bss->ret_user = 0;

	/* trigger */
	usleep(1);

	if (!ASSERT_EQ(skel->bss->ret_user, expected, "skel->bss->ret_user"))
		return;
}

/* validate that for two loads of the same jump table libbpf generates only one map */
static void check_one_map_two_jumps(struct bpf_gotox *skel)
{
	struct bpf_prog_info prog_info;
	struct bpf_map_info map_info;
	__u32 len;
	__u32 map_ids[16];
	int prog_fd, map_fd;
	int ret;
	int i;
	bool seen = false;

	memset(&prog_info, 0, sizeof(prog_info));
	prog_info.map_ids = (long)map_ids;
	prog_info.nr_map_ids = ARRAY_SIZE(map_ids);
	prog_fd = bpf_program__fd(skel->progs.one_map_two_jumps);
	if (!ASSERT_GE(prog_fd, 0, "bpf_program__fd(one_map_two_jumps)"))
		return;

	len = sizeof(prog_info);
	ret = bpf_obj_get_info_by_fd(prog_fd, &prog_info, &len);
	if (!ASSERT_OK(ret, "bpf_obj_get_info_by_fd(prog_fd)"))
		return;

	for (i = 0; i < prog_info.nr_map_ids; i++) {
		map_fd  = bpf_map_get_fd_by_id(map_ids[i]);
		if (!ASSERT_GE(map_fd, 0, "bpf_map_get_fd_by_id"))
			return;

		len = sizeof(map_info);
		memset(&map_info, 0, len);
		ret = bpf_obj_get_info_by_fd(map_fd, &map_info, &len);
		if (!ASSERT_OK(ret, "bpf_obj_get_info_by_fd(map_fd)")) {
			close(map_fd);
			return;
		}

		if (map_info.type == BPF_MAP_TYPE_INSN_ARRAY) {
			if (!ASSERT_EQ(seen, false, "more than one INSN_ARRAY map")) {
				close(map_fd);
				return;
			}
			seen = true;
		}
		close(map_fd);
	}

	ASSERT_EQ(seen, true, "no INSN_ARRAY map");
}

static void check_one_switch(struct bpf_gotox *skel)
{
	__u64 in[]   = {0, 1, 2, 3, 4,  5, 77};
	__u64 out[]  = {2, 3, 4, 5, 7, 19, 19};
	int i;

	for (i = 0; i < ARRAY_SIZE(in); i++)
		check_simple(skel, skel->progs.one_switch, in[i], out[i]);
}

static void check_one_switch_non_zero_sec_off(struct bpf_gotox *skel)
{
	__u64 in[]   = {0, 1, 2, 3, 4,  5, 77};
	__u64 out[]  = {2, 3, 4, 5, 7, 19, 19};
	int i;

	for (i = 0; i < ARRAY_SIZE(in); i++)
		check_simple(skel, skel->progs.one_switch_non_zero_sec_off, in[i], out[i]);
}

static void check_two_switches(struct bpf_gotox *skel)
{
	__u64 in[]   = {0, 1, 2, 3, 4,  5, 77};
	__u64 out[] = {103, 104, 107, 205, 115, 1019, 1019};
	int i;

	for (i = 0; i < ARRAY_SIZE(in); i++)
		check_simple(skel, skel->progs.two_switches, in[i], out[i]);
}

static void check_big_jump_table(struct bpf_gotox *skel)
{
	__u64 in[]  = {0, 11, 27, 31, 22, 45, 99};
	__u64 out[] = {2,  3,  4,  5, 19, 19, 19};
	int i;

	for (i = 0; i < ARRAY_SIZE(in); i++)
		check_simple(skel, skel->progs.big_jump_table, in[i], out[i]);
}

static void check_one_jump_two_maps(struct bpf_gotox *skel)
{
	__u64 in[]  = {0, 1, 2, 3, 4,  5, 77};
	__u64 out[] = {12, 15, 7 , 15, 12, 15, 15};
	int i;

	for (i = 0; i < ARRAY_SIZE(in); i++)
		check_simple(skel, skel->progs.one_jump_two_maps, in[i], out[i]);
}

static void check_static_global(struct bpf_gotox *skel)
{
	__u64 in[]   = {0, 1, 2, 3, 4,  5, 77};
	__u64 out[]  = {2, 3, 4, 5, 7, 19, 19};
	int i;

	for (i = 0; i < ARRAY_SIZE(in); i++)
		check_simple(skel, skel->progs.use_static_global1, in[i], out[i]);
	for (i = 0; i < ARRAY_SIZE(in); i++)
		check_simple(skel, skel->progs.use_static_global2, in[i], out[i]);
}

static void check_nonstatic_global(struct bpf_gotox *skel)
{
	__u64 in[]   = {0, 1, 2, 3, 4,  5, 77};
	__u64 out[]  = {2, 3, 4, 5, 7, 19, 19};
	int i;

	for (i = 0; i < ARRAY_SIZE(in); i++)
		check_simple(skel, skel->progs.use_nonstatic_global1, in[i], out[i]);

	for (i = 0; i < ARRAY_SIZE(in); i++)
		check_simple(skel, skel->progs.use_nonstatic_global2, in[i], out[i]);
}

static void check_other_sec(struct bpf_gotox *skel)
{
	struct bpf_link *link;
	__u64 in[]   = {0, 1, 2, 3, 4,  5, 77};
	__u64 out[]  = {2, 3, 4, 5, 7, 19, 19};
	int i;

	link = bpf_program__attach(skel->progs.simple_test_other_sec);
	if (!ASSERT_OK_PTR(link, "link"))
		return;

	for (i = 0; i < ARRAY_SIZE(in); i++)
		check_simple_fentry(skel, skel->progs.simple_test_other_sec, in[i], out[i]);

	bpf_link__destroy(link);
}

static void check_static_global_other_sec(struct bpf_gotox *skel)
{
	struct bpf_link *link;
	__u64 in[]   = {0, 1, 2, 3, 4,  5, 77};
	__u64 out[]  = {2, 3, 4, 5, 7, 19, 19};
	int i;

	link = bpf_program__attach(skel->progs.use_static_global_other_sec);
	if (!ASSERT_OK_PTR(link, "link"))
		return;

	for (i = 0; i < ARRAY_SIZE(in); i++)
		check_simple_fentry(skel, skel->progs.use_static_global_other_sec, in[i], out[i]);

	bpf_link__destroy(link);
}

static void check_nonstatic_global_other_sec(struct bpf_gotox *skel)
{
	struct bpf_link *link;
	__u64 in[]   = {0, 1, 2, 3, 4,  5, 77};
	__u64 out[]  = {2, 3, 4, 5, 7, 19, 19};
	int i;

	link = bpf_program__attach(skel->progs.use_nonstatic_global_other_sec);
	if (!ASSERT_OK_PTR(link, "link"))
		return;

	for (i = 0; i < ARRAY_SIZE(in); i++)
		check_simple_fentry(skel, skel->progs.use_nonstatic_global_other_sec, in[i], out[i]);

	bpf_link__destroy(link);
}

void test_bpf_gotox(void)
{
	struct bpf_gotox *skel;
	int ret;

	skel = bpf_gotox__open();
	if (!ASSERT_NEQ(skel, NULL, "bpf_gotox__open"))
		return;

	ret = bpf_gotox__load(skel);
	if (!ASSERT_OK(ret, "bpf_gotox__load"))
		return;

	skel->bss->pid = getpid();

	if (test__start_subtest("one-switch"))
		__subtest(skel, check_one_switch);

	if (test__start_subtest("one-switch-non-zero-sec-offset"))
		__subtest(skel, check_one_switch_non_zero_sec_off);

	if (test__start_subtest("two-switches"))
		__subtest(skel, check_two_switches);

	if (test__start_subtest("big-jump-table"))
		__subtest(skel, check_big_jump_table);

	if (test__start_subtest("static-global"))
		__subtest(skel, check_static_global);

	if (test__start_subtest("nonstatic-global"))
		__subtest(skel, check_nonstatic_global);

	if (test__start_subtest("other-sec"))
		__subtest(skel, check_other_sec);

	if (test__start_subtest("static-global-other-sec"))
		__subtest(skel, check_static_global_other_sec);

	if (test__start_subtest("nonstatic-global-other-sec"))
		__subtest(skel, check_nonstatic_global_other_sec);

	if (test__start_subtest("one-jump-two-maps"))
		__subtest(skel, check_one_jump_two_maps);

	if (test__start_subtest("one-map-two-jumps"))
		__subtest(skel, check_one_map_two_jumps);

	bpf_gotox__destroy(skel);
}
