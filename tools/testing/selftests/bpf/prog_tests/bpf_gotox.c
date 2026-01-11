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

/*
 * The following subtests do not use skeleton rather than to check
 * if the test should be skipped.
 */

static int create_jt_map(__u32 max_entries)
{
	const char *map_name = "jt";
	__u32 key_size = 4;
	__u32 value_size = sizeof(struct bpf_insn_array_value);

	return bpf_map_create(BPF_MAP_TYPE_INSN_ARRAY, map_name,
			      key_size, value_size, max_entries, NULL);
}

static int prog_load(struct bpf_insn *insns, __u32 insn_cnt)
{
	return bpf_prog_load(BPF_PROG_TYPE_RAW_TRACEPOINT, NULL, "GPL", insns, insn_cnt, NULL);
}

static int __check_ldimm64_off_prog_load(__u32 max_entries, __u32 off)
{
	struct bpf_insn insns[] = {
		BPF_LD_IMM64_RAW(BPF_REG_1, BPF_PSEUDO_MAP_VALUE, 0),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	int map_fd, ret;

	map_fd = create_jt_map(max_entries);
	if (!ASSERT_GE(map_fd, 0, "create_jt_map"))
		return -1;
	if (!ASSERT_EQ(bpf_map_freeze(map_fd), 0, "bpf_map_freeze")) {
		close(map_fd);
		return -1;
	}

	insns[0].imm = map_fd;
	insns[1].imm = off;

	ret = prog_load(insns, ARRAY_SIZE(insns));
	close(map_fd);
	return ret;
}

/*
 * Check that loads from an instruction array map are only allowed with offsets
 * which are multiples of 8 and do not point to outside of the map.
 */
static void check_ldimm64_off_load(struct bpf_gotox *skel __always_unused)
{
	const __u32 max_entries = 10;
	int prog_fd;
	__u32 off;

	for (off = 0; off < max_entries; off++) {
		prog_fd = __check_ldimm64_off_prog_load(max_entries, off * 8);
		if (!ASSERT_GE(prog_fd, 0, "__check_ldimm64_off_prog_load"))
			return;
		close(prog_fd);
	}

	prog_fd = __check_ldimm64_off_prog_load(max_entries, 7 /* not a multiple of 8 */);
	if (!ASSERT_EQ(prog_fd, -EACCES, "__check_ldimm64_off_prog_load: should be -EACCES")) {
		close(prog_fd);
		return;
	}

	prog_fd = __check_ldimm64_off_prog_load(max_entries, max_entries * 8 /* too large */);
	if (!ASSERT_EQ(prog_fd, -EACCES, "__check_ldimm64_off_prog_load: should be -EACCES")) {
		close(prog_fd);
		return;
	}
}

static int __check_ldimm64_gotox_prog_load(struct bpf_insn *insns,
					   __u32 insn_cnt,
					   __u32 off1, __u32 off2)
{
	const __u32 values[] = {5, 7, 9, 11, 13, 15};
	const __u32 max_entries = ARRAY_SIZE(values);
	struct bpf_insn_array_value val = {};
	int map_fd, ret, i;

	map_fd = create_jt_map(max_entries);
	if (!ASSERT_GE(map_fd, 0, "create_jt_map"))
		return -1;

	for (i = 0; i < max_entries; i++) {
		val.orig_off = values[i];
		if (!ASSERT_EQ(bpf_map_update_elem(map_fd, &i, &val, 0), 0,
			       "bpf_map_update_elem")) {
			close(map_fd);
			return -1;
		}
	}

	if (!ASSERT_EQ(bpf_map_freeze(map_fd), 0, "bpf_map_freeze")) {
		close(map_fd);
		return -1;
	}

	/* r1 = &map + offset1 */
	insns[0].imm = map_fd;
	insns[1].imm = off1;

	/* r1 += off2 */
	insns[2].imm = off2;

	ret = prog_load(insns, insn_cnt);
	close(map_fd);
	return ret;
}

static void reject_offsets(struct bpf_insn *insns, __u32 insn_cnt, __u32 off1, __u32 off2)
{
	int prog_fd;

	prog_fd = __check_ldimm64_gotox_prog_load(insns, insn_cnt, off1, off2);
	if (!ASSERT_EQ(prog_fd, -EACCES, "__check_ldimm64_gotox_prog_load"))
		close(prog_fd);
}

/*
 * Verify a bit more complex programs which include indirect jumps
 * and with jump tables loaded with a non-zero offset
 */
static void check_ldimm64_off_gotox(struct bpf_gotox *skel __always_unused)
{
	struct bpf_insn insns[] = {
		/*
		 * The following instructions perform an indirect jump to
		 * labels below. Thus valid offsets in the map are {0,...,5}.
		 * The program rewrites the offsets in the instructions below:
		 *     r1 = &map + offset1
		 *     r1 += offset2
		 *     r1 = *r1
		 *     gotox r1
		 */
		BPF_LD_IMM64_RAW(BPF_REG_1, BPF_PSEUDO_MAP_VALUE, 0),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 0),
		BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_1, 0),
		BPF_RAW_INSN(BPF_JMP | BPF_JA | BPF_X, BPF_REG_1, 0, 0, 0),

		/* case 0: */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
		/* case 1: */
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
		/* case 2: */
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
		/* case 3: */
		BPF_MOV64_IMM(BPF_REG_0, 3),
		BPF_EXIT_INSN(),
		/* case 4: */
		BPF_MOV64_IMM(BPF_REG_0, 4),
		BPF_EXIT_INSN(),
		/* default: */
		BPF_MOV64_IMM(BPF_REG_0, 5),
		BPF_EXIT_INSN(),
	};
	int prog_fd, err;
	__u32 off1, off2;

	/* allow all combinations off1 + off2 < 6 */
	for (off1 = 0; off1 < 6; off1++) {
		for (off2 = 0; off1 + off2 < 6; off2++) {
			LIBBPF_OPTS(bpf_test_run_opts, topts);

			prog_fd = __check_ldimm64_gotox_prog_load(insns, ARRAY_SIZE(insns),
								  off1 * 8, off2 * 8);
			if (!ASSERT_GE(prog_fd, 0, "__check_ldimm64_gotox_prog_load"))
				return;

			err = bpf_prog_test_run_opts(prog_fd, &topts);
			if (!ASSERT_OK(err, "test_run_opts err")) {
				close(prog_fd);
				return;
			}

			if (!ASSERT_EQ(topts.retval, off1 + off2, "test_run_opts retval")) {
				close(prog_fd);
				return;
			}

			close(prog_fd);
		}
	}

	/* reject off1 + off2 >= 6 */
	reject_offsets(insns, ARRAY_SIZE(insns), 8 * 3, 8 * 3);
	reject_offsets(insns, ARRAY_SIZE(insns), 8 * 7, 8 * 0);
	reject_offsets(insns, ARRAY_SIZE(insns), 8 * 0, 8 * 7);

	/* reject (off1 + off2) % 8 != 0 */
	reject_offsets(insns, ARRAY_SIZE(insns), 3, 3);
	reject_offsets(insns, ARRAY_SIZE(insns), 7, 0);
	reject_offsets(insns, ARRAY_SIZE(insns), 0, 7);
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

	if (test__start_subtest("check-ldimm64-off"))
		__subtest(skel, check_ldimm64_off_load);

	if (test__start_subtest("check-ldimm64-off-gotox"))
		__subtest(skel, check_ldimm64_off_gotox);

	bpf_gotox__destroy(skel);
}
