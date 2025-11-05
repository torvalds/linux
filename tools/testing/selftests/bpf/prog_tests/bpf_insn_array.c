// SPDX-License-Identifier: GPL-2.0

#include <bpf/bpf.h>
#include <test_progs.h>

#ifdef __x86_64__
static int map_create(__u32 map_type, __u32 max_entries)
{
	const char *map_name = "insn_array";
	__u32 key_size = 4;
	__u32 value_size = sizeof(struct bpf_insn_array_value);

	return bpf_map_create(map_type, map_name, key_size, value_size, max_entries, NULL);
}

static int prog_load(struct bpf_insn *insns, __u32 insn_cnt, int *fd_array, __u32 fd_array_cnt)
{
	LIBBPF_OPTS(bpf_prog_load_opts, opts);

	opts.fd_array = fd_array;
	opts.fd_array_cnt = fd_array_cnt;

	return bpf_prog_load(BPF_PROG_TYPE_XDP, NULL, "GPL", insns, insn_cnt, &opts);
}

static void __check_success(struct bpf_insn *insns, __u32 insn_cnt, __u32 *map_in, __u32 *map_out)
{
	struct bpf_insn_array_value val = {};
	int prog_fd = -1, map_fd, i;

	map_fd = map_create(BPF_MAP_TYPE_INSN_ARRAY, insn_cnt);
	if (!ASSERT_GE(map_fd, 0, "map_create"))
		return;

	for (i = 0; i < insn_cnt; i++) {
		val.orig_off = map_in[i];
		if (!ASSERT_EQ(bpf_map_update_elem(map_fd, &i, &val, 0), 0, "bpf_map_update_elem"))
			goto cleanup;
	}

	if (!ASSERT_EQ(bpf_map_freeze(map_fd), 0, "bpf_map_freeze"))
		goto cleanup;

	prog_fd = prog_load(insns, insn_cnt, &map_fd, 1);
	if (!ASSERT_GE(prog_fd, 0, "bpf(BPF_PROG_LOAD)"))
		goto cleanup;

	for (i = 0; i < insn_cnt; i++) {
		char buf[64];

		if (!ASSERT_EQ(bpf_map_lookup_elem(map_fd, &i, &val), 0, "bpf_map_lookup_elem"))
			goto cleanup;

		snprintf(buf, sizeof(buf), "val.xlated_off should be equal map_out[%d]", i);
		ASSERT_EQ(val.xlated_off, map_out[i], buf);
	}

cleanup:
	close(prog_fd);
	close(map_fd);
}

/*
 * Load a program, which will not be anyhow mangled by the verifier.  Add an
 * insn_array map pointing to every instruction. Check that it hasn't changed
 * after the program load.
 */
static void check_one_to_one_mapping(void)
{
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 4),
		BPF_MOV64_IMM(BPF_REG_0, 3),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	__u32 map_in[] = {0, 1, 2, 3, 4, 5};
	__u32 map_out[] = {0, 1, 2, 3, 4, 5};

	__check_success(insns, ARRAY_SIZE(insns), map_in, map_out);
}

/*
 * Load a program with two patches (get jiffies, for simplicity). Add an
 * insn_array map pointing to every instruction. Check how it was changed
 * after the program load.
 */
static void check_simple(void)
{
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_jiffies64),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_jiffies64),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	__u32 map_in[] = {0, 1, 2, 3, 4, 5};
	__u32 map_out[] = {0, 1, 4, 5, 8, 9};

	__check_success(insns, ARRAY_SIZE(insns), map_in, map_out);
}

/*
 * Verifier can delete code in two cases: nops & dead code. From insn
 * array's point of view, the two cases are the same, so test using
 * the simplest method: by loading some nops
 */
static void check_deletions(void)
{
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_JMP_IMM(BPF_JA, 0, 0, 0), /* nop */
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_JMP_IMM(BPF_JA, 0, 0, 0), /* nop */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	__u32 map_in[] = {0, 1, 2, 3, 4, 5};
	__u32 map_out[] = {0, -1, 1, -1, 2, 3};

	__check_success(insns, ARRAY_SIZE(insns), map_in, map_out);
}

/*
 * Same test as check_deletions, but also add code which adds instructions
 */
static void check_deletions_with_functions(void)
{
	struct bpf_insn insns[] = {
		BPF_JMP_IMM(BPF_JA, 0, 0, 0), /* nop */
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_jiffies64),
		BPF_JMP_IMM(BPF_JA, 0, 0, 0), /* nop */
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
		BPF_JMP_IMM(BPF_JA, 0, 0, 0), /* nop */
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_jiffies64),
		BPF_JMP_IMM(BPF_JA, 0, 0, 0), /* nop */
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
	};
	__u32 map_in[] =  { 0, 1,  2, 3, 4, 5, /* func */  6, 7,  8, 9, 10};
	__u32 map_out[] = {-1, 0, -1, 3, 4, 5, /* func */ -1, 6, -1, 9, 10};

	__check_success(insns, ARRAY_SIZE(insns), map_in, map_out);
}

/*
 * Try to load a program with a map which points to outside of the program
 */
static void check_out_of_bounds_index(void)
{
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 4),
		BPF_MOV64_IMM(BPF_REG_0, 3),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	int prog_fd, map_fd;
	struct bpf_insn_array_value val = {};
	int key;

	map_fd = map_create(BPF_MAP_TYPE_INSN_ARRAY, 1);
	if (!ASSERT_GE(map_fd, 0, "map_create"))
		return;

	key = 0;
	val.orig_off = ARRAY_SIZE(insns); /* too big */
	if (!ASSERT_EQ(bpf_map_update_elem(map_fd, &key, &val, 0), 0, "bpf_map_update_elem"))
		goto cleanup;

	if (!ASSERT_EQ(bpf_map_freeze(map_fd), 0, "bpf_map_freeze"))
		goto cleanup;

	prog_fd = prog_load(insns, ARRAY_SIZE(insns), &map_fd, 1);
	if (!ASSERT_EQ(prog_fd, -EINVAL, "program should have been rejected (prog_fd != -EINVAL)")) {
		close(prog_fd);
		goto cleanup;
	}

cleanup:
	close(map_fd);
}

/*
 * Try to load a program with a map which points to the middle of 16-bit insn
 */
static void check_mid_insn_index(void)
{
	struct bpf_insn insns[] = {
		BPF_LD_IMM64(BPF_REG_0, 0), /* 2 x 8 */
		BPF_EXIT_INSN(),
	};
	int prog_fd, map_fd;
	struct bpf_insn_array_value val = {};
	int key;

	map_fd = map_create(BPF_MAP_TYPE_INSN_ARRAY, 1);
	if (!ASSERT_GE(map_fd, 0, "map_create"))
		return;

	key = 0;
	val.orig_off = 1; /* middle of 16-byte instruction */
	if (!ASSERT_EQ(bpf_map_update_elem(map_fd, &key, &val, 0), 0, "bpf_map_update_elem"))
		goto cleanup;

	if (!ASSERT_EQ(bpf_map_freeze(map_fd), 0, "bpf_map_freeze"))
		goto cleanup;

	prog_fd = prog_load(insns, ARRAY_SIZE(insns), &map_fd, 1);
	if (!ASSERT_EQ(prog_fd, -EINVAL, "program should have been rejected (prog_fd != -EINVAL)")) {
		close(prog_fd);
		goto cleanup;
	}

cleanup:
	close(map_fd);
}

static void check_incorrect_index(void)
{
	check_out_of_bounds_index();
	check_mid_insn_index();
}

static int set_bpf_jit_harden(char *level)
{
	char old_level;
	int err = -1;
	int fd = -1;

	fd = open("/proc/sys/net/core/bpf_jit_harden", O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		ASSERT_FAIL("open .../bpf_jit_harden returned %d (errno=%d)", fd, errno);
		return -1;
	}

	err = read(fd, &old_level, 1);
	if (err != 1) {
		ASSERT_FAIL("read from .../bpf_jit_harden returned %d (errno=%d)", err, errno);
		err = -1;
		goto end;
	}

	lseek(fd, 0, SEEK_SET);

	err = write(fd, level, 1);
	if (err != 1) {
		ASSERT_FAIL("write to .../bpf_jit_harden returned %d (errno=%d)", err, errno);
		err = -1;
		goto end;
	}

	err = 0;
	*level = old_level;
end:
	if (fd >= 0)
		close(fd);
	return err;
}

static void check_blindness(void)
{
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 4),
		BPF_MOV64_IMM(BPF_REG_0, 3),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	};
	int prog_fd = -1, map_fd;
	struct bpf_insn_array_value val = {};
	char bpf_jit_harden = '@'; /* non-exizsting value */
	int i;

	map_fd = map_create(BPF_MAP_TYPE_INSN_ARRAY, ARRAY_SIZE(insns));
	if (!ASSERT_GE(map_fd, 0, "map_create"))
		return;

	for (i = 0; i < ARRAY_SIZE(insns); i++) {
		val.orig_off = i;
		if (!ASSERT_EQ(bpf_map_update_elem(map_fd, &i, &val, 0), 0, "bpf_map_update_elem"))
			goto cleanup;
	}

	if (!ASSERT_EQ(bpf_map_freeze(map_fd), 0, "bpf_map_freeze"))
		goto cleanup;

	bpf_jit_harden = '2';
	if (set_bpf_jit_harden(&bpf_jit_harden)) {
		bpf_jit_harden = '@'; /* open, read or write failed => no write was done */
		goto cleanup;
	}

	prog_fd = prog_load(insns, ARRAY_SIZE(insns), &map_fd, 1);
	if (!ASSERT_GE(prog_fd, 0, "bpf(BPF_PROG_LOAD)"))
		goto cleanup;

	for (i = 0; i < ARRAY_SIZE(insns); i++) {
		char fmt[32];

		if (!ASSERT_EQ(bpf_map_lookup_elem(map_fd, &i, &val), 0, "bpf_map_lookup_elem"))
			goto cleanup;

		snprintf(fmt, sizeof(fmt), "val should be equal 3*%d", i);
		ASSERT_EQ(val.xlated_off, i * 3, fmt);
	}

cleanup:
	/* restore the old one */
	if (bpf_jit_harden != '@')
		set_bpf_jit_harden(&bpf_jit_harden);

	close(prog_fd);
	close(map_fd);
}

/* Once map was initialized, it should be frozen */
static void check_load_unfrozen_map(void)
{
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	int prog_fd = -1, map_fd;
	struct bpf_insn_array_value val = {};
	int i;

	map_fd = map_create(BPF_MAP_TYPE_INSN_ARRAY, ARRAY_SIZE(insns));
	if (!ASSERT_GE(map_fd, 0, "map_create"))
		return;

	for (i = 0; i < ARRAY_SIZE(insns); i++) {
		val.orig_off = i;
		if (!ASSERT_EQ(bpf_map_update_elem(map_fd, &i, &val, 0), 0, "bpf_map_update_elem"))
			goto cleanup;
	}

	prog_fd = prog_load(insns, ARRAY_SIZE(insns), &map_fd, 1);
	if (!ASSERT_EQ(prog_fd, -EINVAL, "program should have been rejected (prog_fd != -EINVAL)"))
		goto cleanup;

	/* correctness: now freeze the map, the program should load fine */

	if (!ASSERT_EQ(bpf_map_freeze(map_fd), 0, "bpf_map_freeze"))
		goto cleanup;

	prog_fd = prog_load(insns, ARRAY_SIZE(insns), &map_fd, 1);
	if (!ASSERT_GE(prog_fd, 0, "bpf(BPF_PROG_LOAD)"))
		goto cleanup;

	for (i = 0; i < ARRAY_SIZE(insns); i++) {
		if (!ASSERT_EQ(bpf_map_lookup_elem(map_fd, &i, &val), 0, "bpf_map_lookup_elem"))
			goto cleanup;

		ASSERT_EQ(val.xlated_off, i, "val should be equal i");
	}

cleanup:
	close(prog_fd);
	close(map_fd);
}

/* Map can be used only by one BPF program */
static void check_no_map_reuse(void)
{
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	int prog_fd = -1, map_fd, extra_fd = -1;
	struct bpf_insn_array_value val = {};
	int i;

	map_fd = map_create(BPF_MAP_TYPE_INSN_ARRAY, ARRAY_SIZE(insns));
	if (!ASSERT_GE(map_fd, 0, "map_create"))
		return;

	for (i = 0; i < ARRAY_SIZE(insns); i++) {
		val.orig_off = i;
		if (!ASSERT_EQ(bpf_map_update_elem(map_fd, &i, &val, 0), 0, "bpf_map_update_elem"))
			goto cleanup;
	}

	if (!ASSERT_EQ(bpf_map_freeze(map_fd), 0, "bpf_map_freeze"))
		goto cleanup;

	prog_fd = prog_load(insns, ARRAY_SIZE(insns), &map_fd, 1);
	if (!ASSERT_GE(prog_fd, 0, "bpf(BPF_PROG_LOAD)"))
		goto cleanup;

	for (i = 0; i < ARRAY_SIZE(insns); i++) {
		if (!ASSERT_EQ(bpf_map_lookup_elem(map_fd, &i, &val), 0, "bpf_map_lookup_elem"))
			goto cleanup;

		ASSERT_EQ(val.xlated_off, i, "val should be equal i");
	}

	extra_fd = prog_load(insns, ARRAY_SIZE(insns), &map_fd, 1);
	if (!ASSERT_EQ(extra_fd, -EBUSY, "program should have been rejected (extra_fd != -EBUSY)"))
		goto cleanup;

	/* correctness: check that prog is still loadable without fd_array */
	extra_fd = prog_load(insns, ARRAY_SIZE(insns), NULL, 0);
	if (!ASSERT_GE(extra_fd, 0, "bpf(BPF_PROG_LOAD): expected no error"))
		goto cleanup;

cleanup:
	close(extra_fd);
	close(prog_fd);
	close(map_fd);
}

static void check_bpf_no_lookup(void)
{
	struct bpf_insn insns[] = {
		BPF_LD_MAP_FD(BPF_REG_1, 0),
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0),
		BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -8),
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
		BPF_EXIT_INSN(),
	};
	int prog_fd = -1, map_fd;

	map_fd = map_create(BPF_MAP_TYPE_INSN_ARRAY, 1);
	if (!ASSERT_GE(map_fd, 0, "map_create"))
		return;

	insns[0].imm = map_fd;

	if (!ASSERT_EQ(bpf_map_freeze(map_fd), 0, "bpf_map_freeze"))
		goto cleanup;

	prog_fd = prog_load(insns, ARRAY_SIZE(insns), NULL, 0);
	if (!ASSERT_EQ(prog_fd, -EINVAL, "program should have been rejected (prog_fd != -EINVAL)"))
		goto cleanup;

	/* correctness: check that prog is still loadable with normal map */
	close(map_fd);
	map_fd = map_create(BPF_MAP_TYPE_ARRAY, 1);
	insns[0].imm = map_fd;
	prog_fd = prog_load(insns, ARRAY_SIZE(insns), NULL, 0);
	if (!ASSERT_GE(prog_fd, 0, "bpf(BPF_PROG_LOAD)"))
		goto cleanup;

cleanup:
	close(prog_fd);
	close(map_fd);
}

static void check_bpf_side(void)
{
	check_bpf_no_lookup();
}

static void __test_bpf_insn_array(void)
{
	/* Test if offsets are adjusted properly */

	if (test__start_subtest("one2one"))
		check_one_to_one_mapping();

	if (test__start_subtest("simple"))
		check_simple();

	if (test__start_subtest("deletions"))
		check_deletions();

	if (test__start_subtest("deletions-with-functions"))
		check_deletions_with_functions();

	if (test__start_subtest("blindness"))
		check_blindness();

	/* Check all kinds of operations and related restrictions */

	if (test__start_subtest("incorrect-index"))
		check_incorrect_index();

	if (test__start_subtest("load-unfrozen-map"))
		check_load_unfrozen_map();

	if (test__start_subtest("no-map-reuse"))
		check_no_map_reuse();

	if (test__start_subtest("bpf-side-ops"))
		check_bpf_side();
}
#else
static void __test_bpf_insn_array(void)
{
	test__skip();
}
#endif

void test_bpf_insn_array(void)
{
	__test_bpf_insn_array();
}
