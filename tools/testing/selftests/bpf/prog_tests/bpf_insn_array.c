// SPDX-License-Identifier: GPL-2.0

#include <bpf/bpf.h>
#include <bpf/btf.h>
#include <test_progs.h>

#if defined(__x86_64__) || defined(__powerpc__) || defined(__aarch64__) || \
	(defined(__riscv) && __riscv_xlen == 64)
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

#define GOTOX_LOG_SZ		(256 * 1024)

/* Load and return the fd, which the caller owns. */
static int gotox_prog_load_funcs_fd(struct bpf_insn *insns, __u32 insn_cnt,
				    int *fd_array, __u32 fd_array_cnt, char *log,
				    int btf_fd, struct bpf_func_info *fi, __u32 fi_cnt)
{
	LIBBPF_OPTS(bpf_prog_load_opts, opts);

	log[0] = 0;
	opts.fd_array = fd_array;
	opts.fd_array_cnt = fd_array_cnt;
	opts.log_buf = log;
	opts.log_size = GOTOX_LOG_SZ;
	opts.log_level = 1;
	if (fi_cnt) {
		opts.prog_btf_fd = btf_fd;
		opts.func_info = fi;
		opts.func_info_cnt = fi_cnt;
		opts.func_info_rec_size = sizeof(*fi);
	}

	return bpf_prog_load(BPF_PROG_TYPE_XDP, NULL, "GPL", insns, insn_cnt, &opts);
}

/* Load for the outcome only: 0 or the negative errno. */
static int gotox_prog_load_funcs(struct bpf_insn *insns, __u32 insn_cnt,
				 int *fd_array, __u32 fd_array_cnt, char *log,
				 int btf_fd, struct bpf_func_info *fi, __u32 fi_cnt)
{
	int prog_fd;

	prog_fd = gotox_prog_load_funcs_fd(insns, insn_cnt, fd_array, fd_array_cnt, log,
					   btf_fd, fi, fi_cnt);
	if (prog_fd < 0)
		return prog_fd;
	close(prog_fd);
	return 0;
}

static int gotox_prog_load(struct bpf_insn *insns, __u32 insn_cnt,
			   int *fd_array, __u32 fd_array_cnt, char *log)
{
	return gotox_prog_load_funcs(insns, insn_cnt, fd_array, fd_array_cnt, log,
				     -1, NULL, 0);
}

static int gotox_jt_create_offs(const __u32 *offs, __u32 cnt)
{
	struct bpf_insn_array_value val = {};
	int map_fd;
	__u32 i;

	map_fd = map_create(BPF_MAP_TYPE_INSN_ARRAY, cnt);
	if (!ASSERT_GE(map_fd, 0, "map_create"))
		return map_fd;

	for (i = 0; i < cnt; i++) {
		val.orig_off = offs[i];
		if (!ASSERT_EQ(bpf_map_update_elem(map_fd, &i, &val, 0), 0,
			       "bpf_map_update_elem"))
			goto err;
	}

	if (!ASSERT_EQ(bpf_map_freeze(map_fd), 0, "bpf_map_freeze"))
		goto err;

	return map_fd;
err:
	close(map_fd);
	return -1;
}

#define GOTOX_SUB_START		4
#define GOTOX_MAIN_TGT		2
#define GOTOX_SUB_TGT		8
#define GOTOX_TWO_INSN_CNT	10

static void gotox_two_subprogs_fill(struct bpf_insn *insns, __u32 jt_idx, __u32 jt_off)
{
	insns[0] = BPF_MOV64_IMM(BPF_REG_0, 0);
	insns[1] = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, BPF_PSEUDO_CALL, 0,
				GOTOX_SUB_START - 1 - 1);
	insns[GOTOX_MAIN_TGT] = BPF_MOV64_IMM(BPF_REG_0, 0);
	insns[3] = BPF_EXIT_INSN();

	/* r1 = &jt[0], by index 'jt_idx' into fd_array */
	insns[GOTOX_SUB_START] = (struct bpf_insn) {
		.code = BPF_LD | BPF_DW | BPF_IMM,
		.dst_reg = BPF_REG_1,
		.src_reg = BPF_PSEUDO_MAP_IDX_VALUE,
		.imm = jt_idx,
	};
	insns[GOTOX_SUB_START + 1] = (struct bpf_insn) { .imm = 0 };
	insns[6] = BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_1, jt_off * 8);
	insns[7] = BPF_RAW_INSN(BPF_JMP | BPF_JA | BPF_X, BPF_REG_1, 0, 0, 0);
	insns[GOTOX_SUB_TGT] = BPF_MOV64_IMM(BPF_REG_0, 1);
	insns[9] = BPF_EXIT_INSN();
}

/*
 * Load 'insns' with the two jump tables 'jt_a' and 'jt_b', in this order in
 * fd_array, and check the outcome. 'fi' describes the subprograms when given.
 */
static void check_gotox_two_maps(struct bpf_insn *insns, __u32 insn_cnt,
				 const __u32 *jt_a, __u32 a_cnt,
				 const __u32 *jt_b, __u32 b_cnt,
				 int btf_fd, struct bpf_func_info *fi, __u32 fi_cnt,
				 int expect_err, const char *expect_msg)
{
	int map_fd[2] = { -1, -1 };
	char *log;
	int err;

	log = calloc(1, GOTOX_LOG_SZ);
	if (!ASSERT_OK_PTR(log, "calloc log"))
		return;

	map_fd[0] = gotox_jt_create_offs(jt_a, a_cnt);
	if (map_fd[0] < 0)
		goto free_log;
	map_fd[1] = gotox_jt_create_offs(jt_b, b_cnt);
	if (map_fd[1] < 0)
		goto close_maps;

	if (fi)
		err = gotox_prog_load_funcs(insns, insn_cnt, map_fd, 2, log,
					    btf_fd, fi, fi_cnt);
	else
		err = gotox_prog_load(insns, insn_cnt, map_fd, 2, log);
	if (expect_err)
		ASSERT_EQ(err, expect_err, "program should have been rejected");
	else
		ASSERT_EQ(err, 0, "bpf(BPF_PROG_LOAD)");
	if (expect_msg)
		ASSERT_HAS_SUBSTR(log, expect_msg, "verifier log");

close_maps:
	close(map_fd[0]);
	if (map_fd[1] >= 0)
		close(map_fd[1]);
free_log:
	free(log);
}

static void check_gotox_target_other_subprog(void)
{
	const __u32 jt_main[] = { GOTOX_MAIN_TGT };
	const __u32 jt_sub[] = { GOTOX_SUB_TGT };
	struct bpf_insn insns[GOTOX_TWO_INSN_CNT];

	gotox_two_subprogs_fill(insns, 0, 0);

	check_gotox_two_maps(insns, ARRAY_SIZE(insns), jt_main, ARRAY_SIZE(jt_main),
			     jt_sub, ARRAY_SIZE(jt_sub), -1, NULL, 0,
			     -EINVAL, "indirect jump from insn 7 to 2 leaves the subprog [4,10)");
}

static void check_gotox_jt_per_subprog(void)
{
	const __u32 jt_main[] = { GOTOX_MAIN_TGT };
	const __u32 jt_sub[] = { GOTOX_SUB_TGT };
	struct bpf_insn insns[GOTOX_TWO_INSN_CNT];

	gotox_two_subprogs_fill(insns, 1, 0);

	check_gotox_two_maps(insns, ARRAY_SIZE(insns), jt_main, ARRAY_SIZE(jt_main),
			     jt_sub, ARRAY_SIZE(jt_sub), -1, NULL, 0,
			     0, NULL);
}

#define GOTOX_DEAD_MAIN_LEN	7
#define GOTOX_DEAD_SUB_LEN	6
#define GOTOX_DEAD_INSN_CNT	(GOTOX_DEAD_MAIN_LEN + 3 * GOTOX_DEAD_SUB_LEN)
#define GOTOX_DEAD_LIVE_CNT	10

/* r1 = &jt[0], by index 'jt_idx' into fd_array; r1 = *r1; gotox r1; r0 = 0; exit */
static void gotox_sub_fill(struct bpf_insn *insns, __u32 start, __u32 jt_idx)
{
	insns[start] = (struct bpf_insn) {
		.code = BPF_LD | BPF_DW | BPF_IMM,
		.dst_reg = BPF_REG_1,
		.src_reg = BPF_PSEUDO_MAP_IDX_VALUE,
		.imm = jt_idx,
	};
	insns[start + 1] = (struct bpf_insn) { .imm = 0 };
	insns[start + 2] = BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_1, 0);
	insns[start + 3] = BPF_RAW_INSN(BPF_JMP | BPF_JA | BPF_X, BPF_REG_1, 0, 0, 0);
	insns[start + 4] = BPF_MOV64_IMM(BPF_REG_0, 0);
	insns[start + 5] = BPF_EXIT_INSN();
}

/*
 * Two subprograms which are only called from dead code sit in front of the
 * live one, and all three own a jump table. Dead code removal drops both in
 * one go, which compacts subprog_info[] along with the tables.
 */
static void check_gotox_jt_dead_subprogs(void)
{
	const __u32 sub_start[3] = { GOTOX_DEAD_MAIN_LEN,
				     GOTOX_DEAD_MAIN_LEN + GOTOX_DEAD_SUB_LEN,
				     GOTOX_DEAD_MAIN_LEN + 2 * GOTOX_DEAD_SUB_LEN };
	struct bpf_insn insns[GOTOX_DEAD_INSN_CNT];
	int map_fd[3] = { -1, -1, -1 };
	struct bpf_prog_info info = {};
	__u32 len = sizeof(info), tgt, i;
	int prog_fd;
	char *log;

	log = calloc(1, GOTOX_LOG_SZ);
	if (!ASSERT_OK_PTR(log, "calloc log"))
		return;

	insns[0] = BPF_MOV64_IMM(BPF_REG_1, 0);
	insns[1] = BPF_JMP_IMM(BPF_JEQ, BPF_REG_1, 0, 2);
	insns[2] = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, BPF_PSEUDO_CALL, 0,
				sub_start[0] - 2 - 1);
	insns[3] = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, BPF_PSEUDO_CALL, 0,
				sub_start[1] - 3 - 1);
	insns[4] = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, BPF_PSEUDO_CALL, 0,
				sub_start[2] - 4 - 1);
	insns[5] = BPF_MOV64_IMM(BPF_REG_0, 0);
	insns[6] = BPF_EXIT_INSN();

	for (i = 0; i < ARRAY_SIZE(sub_start); i++) {
		gotox_sub_fill(insns, sub_start[i], i);
		tgt = sub_start[i] + 4;
		map_fd[i] = gotox_jt_create_offs(&tgt, 1);
		if (map_fd[i] < 0)
			goto close_maps;
	}

	prog_fd = gotox_prog_load_funcs_fd(insns, ARRAY_SIZE(insns), map_fd,
					   ARRAY_SIZE(map_fd), log, -1, NULL, 0);
	if (!ASSERT_GE(prog_fd, 0, "bpf(BPF_PROG_LOAD)"))
		goto close_maps;

	/* Both dead subprograms and the calls to them must be gone. */
	if (ASSERT_OK(bpf_prog_get_info_by_fd(prog_fd, &info, &len),
		      "bpf_prog_get_info_by_fd"))
		ASSERT_EQ(info.xlated_prog_len / sizeof(struct bpf_insn),
			  GOTOX_DEAD_LIVE_CNT, "xlated insn count");
	close(prog_fd);
close_maps:
	for (i = 0; i < ARRAY_SIZE(map_fd); i++)
		if (map_fd[i] >= 0)
			close(map_fd[i]);
	free(log);
}

#define GOTOX_FWD_GOTOX		11
#define GOTOX_FWD_OWN_TGT	12
#define GOTOX_FWD_SUB_START	14
#define GOTOX_FWD_INSN_CNT	16

static void gotox_from_main_fill(struct bpf_insn *insns)
{
	insns[0] = BPF_MOV64_REG(BPF_REG_6, BPF_REG_1);
	insns[1] = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, BPF_PSEUDO_CALL, 0,
				GOTOX_FWD_SUB_START - 1 - 1);
	insns[2] = BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_6,
			       offsetof(struct xdp_md, ingress_ifindex));
	insns[3] = BPF_JMP_IMM(BPF_JEQ, BPF_REG_2, 0, 4);

	/* r1 = &jt_leaves[0], by index 1 into fd_array */
	insns[4] = (struct bpf_insn) {
		.code = BPF_LD | BPF_DW | BPF_IMM,
		.dst_reg = BPF_REG_1,
		.src_reg = BPF_PSEUDO_MAP_IDX_VALUE,
		.imm = 1,
	};
	insns[5] = (struct bpf_insn) { .imm = 0 };
	insns[6] = BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_1, 0);
	insns[7] = BPF_JMP_A(3);

	/* r1 = &jt_own[0], by index 0 into fd_array */
	insns[8] = (struct bpf_insn) {
		.code = BPF_LD | BPF_DW | BPF_IMM,
		.dst_reg = BPF_REG_1,
		.src_reg = BPF_PSEUDO_MAP_IDX_VALUE,
		.imm = 0,
	};
	insns[9] = (struct bpf_insn) { .imm = 0 };
	insns[10] = BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_1, 0);

	insns[GOTOX_FWD_GOTOX] = BPF_RAW_INSN(BPF_JMP | BPF_JA | BPF_X, BPF_REG_1, 0, 0, 0);
	insns[GOTOX_FWD_OWN_TGT] = BPF_MOV64_IMM(BPF_REG_0, 0);
	insns[13] = BPF_EXIT_INSN();
	insns[GOTOX_FWD_SUB_START] = BPF_MOV64_IMM(BPF_REG_0, 1);
	insns[15] = BPF_EXIT_INSN();
}

static void check_gotox_target_subprog_from_main(void)
{
	const __u32 jt_own[] = { GOTOX_FWD_OWN_TGT };
	const __u32 jt_leaves[] = { GOTOX_FWD_SUB_START };
	struct bpf_insn insns[GOTOX_FWD_INSN_CNT];

	gotox_from_main_fill(insns);

	check_gotox_two_maps(insns, ARRAY_SIZE(insns), jt_own, ARRAY_SIZE(jt_own),
			     jt_leaves, ARRAY_SIZE(jt_leaves), -1, NULL, 0,
			     -EINVAL, "indirect jump from insn 11 to 14 leaves the subprog [0,14)");
}

/*
 * The only map of the subprog holding the gotox reaches past that subprog, so
 * the subprog is left without a jump table at all.
 */
static void check_gotox_jt_spans_subprogs(void)
{
	const __u32 jt_span[] = { GOTOX_FWD_OWN_TGT, GOTOX_FWD_SUB_START };
	const __u32 jt_leaves[] = { GOTOX_FWD_SUB_START };
	struct bpf_insn insns[GOTOX_FWD_INSN_CNT];

	gotox_from_main_fill(insns);

	check_gotox_two_maps(insns, ARRAY_SIZE(insns), jt_span, ARRAY_SIZE(jt_span),
			     jt_leaves, ARRAY_SIZE(jt_leaves), -1, NULL, 0,
			     -EINVAL, "jump table of subprog starting at 0 spans multiple subprogs");
}

/*
 * The subprog holding the gotox has a well formed jump table of its own and
 * also collects a map that reaches past its end. The spanning map is still
 * rejected, even though the subprog is not left without a table.
 */
static void check_gotox_jt_spans_with_own_table(void)
{
	const __u32 jt_own[] = { GOTOX_FWD_OWN_TGT };
	const __u32 jt_span[] = { GOTOX_FWD_OWN_TGT, GOTOX_FWD_SUB_START };
	struct bpf_insn insns[GOTOX_FWD_INSN_CNT];

	gotox_from_main_fill(insns);

	check_gotox_two_maps(insns, ARRAY_SIZE(insns), jt_own, ARRAY_SIZE(jt_own),
			     jt_span, ARRAY_SIZE(jt_span), -1, NULL, 0,
			     -EINVAL, "jump table of subprog starting at 0 spans multiple subprogs");
}

#define GOTOX_SLICE_SUB_START	6
#define GOTOX_SLICE_GOTOX	14
#define GOTOX_SLICE_SUB_TGT	15
#define GOTOX_SLICE_INSN_CNT	17

static void gotox_slice_fill(struct bpf_insn *insns)
{
	insns[0] = BPF_MOV64_IMM(BPF_REG_0, 0);
	insns[1] = BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, BPF_PSEUDO_CALL, 0,
				GOTOX_SLICE_SUB_START - 1 - 1);
	insns[2] = BPF_MOV64_IMM(BPF_REG_0, 0);
	insns[3] = BPF_MOV64_IMM(BPF_REG_0, 0);
	insns[4] = BPF_MOV64_IMM(BPF_REG_0, 0);
	insns[5] = BPF_EXIT_INSN();

	insns[GOTOX_SLICE_SUB_START] =
		BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_1,
			    offsetof(struct xdp_md, ingress_ifindex));
	insns[7] = BPF_ALU64_IMM(BPF_AND, BPF_REG_2, 1);
	insns[8] = BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, 1);
	insns[9] = BPF_ALU64_IMM(BPF_LSH, BPF_REG_2, 3);

	/* r1 = &jt_main[0], by index 0 into fd_array */
	insns[10] = (struct bpf_insn) {
		.code = BPF_LD | BPF_DW | BPF_IMM,
		.dst_reg = BPF_REG_1,
		.src_reg = BPF_PSEUDO_MAP_IDX_VALUE,
		.imm = 0,
	};
	insns[11] = (struct bpf_insn) { .imm = 0 };
	insns[12] = BPF_ALU64_REG(BPF_ADD, BPF_REG_1, BPF_REG_2);
	insns[13] = BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_1, 0);

	insns[GOTOX_SLICE_GOTOX] = BPF_RAW_INSN(BPF_JMP | BPF_JA | BPF_X, BPF_REG_1, 0, 0, 0);
	insns[GOTOX_SLICE_SUB_TGT] = BPF_MOV64_IMM(BPF_REG_0, 1);
	insns[16] = BPF_EXIT_INSN();
}

static void check_gotox_index_slice_other_subprog(void)
{
	const __u32 jt_main[] = { 2, 3, 4 };
	const __u32 jt_sub[] = { GOTOX_SLICE_SUB_TGT };
	struct bpf_insn insns[GOTOX_SLICE_INSN_CNT];

	gotox_slice_fill(insns);

	check_gotox_two_maps(insns, ARRAY_SIZE(insns), jt_main, ARRAY_SIZE(jt_main),
			     jt_sub, ARRAY_SIZE(jt_sub), -1, NULL, 0,
			     -EINVAL, "indirect jump from insn 14 to 3 leaves the subprog [6,17)");
}

static int gotox_btf_create(const __u32 *starts, const __u8 *linkage, __u32 cnt,
			    struct bpf_func_info *fi, struct btf **pbtf)
{
	int int_id, proto_id, id;
	struct btf *btf;
	char name[24];
	__u32 i;

	btf = btf__new_empty();
	if (!ASSERT_OK_PTR(btf, "btf__new_empty"))
		return -1;

	int_id = btf__add_int(btf, "int", 4, BTF_INT_SIGNED);
	if (!ASSERT_GT(int_id, 0, "btf__add_int"))
		goto err;

	proto_id = btf__add_func_proto(btf, int_id);
	if (!ASSERT_GT(proto_id, 0, "btf__add_func_proto"))
		goto err;

	for (i = 0; i < cnt; i++) {
		snprintf(name, sizeof(name), "gotox_f%u", i);
		id = btf__add_func(btf, name, linkage[i], proto_id);
		if (!ASSERT_GT(id, 0, "btf__add_func"))
			goto err;
		fi[i].insn_off = starts[i];
		fi[i].type_id = id;
	}

	if (!ASSERT_OK(btf__load_into_kernel(btf), "btf__load_into_kernel"))
		goto err;

	*pbtf = btf;
	return btf__fd(btf);
err:
	btf__free(btf);
	return -1;
}

static void check_gotox_target_other_global_subprog(void)
{
	const __u32 starts[] = { 0, GOTOX_SUB_START };
	const __u8 linkage[] = { BTF_FUNC_GLOBAL, BTF_FUNC_GLOBAL };
	const __u32 jt_main[] = { GOTOX_MAIN_TGT };
	const __u32 jt_sub[] = { GOTOX_SUB_TGT };
	struct bpf_insn insns[GOTOX_TWO_INSN_CNT];
	struct bpf_func_info fi[2];
	struct btf *btf = NULL;
	int btf_fd;

	gotox_two_subprogs_fill(insns, 0, 0);

	btf_fd = gotox_btf_create(starts, linkage, ARRAY_SIZE(starts), fi, &btf);
	if (btf_fd < 0)
		return;

	check_gotox_two_maps(insns, ARRAY_SIZE(insns), jt_main, ARRAY_SIZE(jt_main),
			     jt_sub, ARRAY_SIZE(jt_sub), btf_fd, fi, ARRAY_SIZE(fi),
			     -EINVAL, "indirect jump from insn 7 to 2 leaves the subprog [4,10)");
	btf__free(btf);
}

#define GOTOX_CB_MAIN_TGT	6
#define GOTOX_CB_START		8
#define GOTOX_CB_GOTOX		11
#define GOTOX_CB_TGT		12
#define GOTOX_CB_INSN_CNT	14

static void gotox_callback_fill(struct bpf_insn *insns)
{
	insns[0] = BPF_MOV64_IMM(BPF_REG_1, 1);
	/* r2 = &callback */
	insns[1] = (struct bpf_insn) {
		.code = BPF_LD | BPF_DW | BPF_IMM,
		.dst_reg = BPF_REG_2,
		.src_reg = BPF_PSEUDO_FUNC,
		.imm = GOTOX_CB_START - 1 - 1,
	};
	insns[2] = (struct bpf_insn) { .imm = 0 };
	insns[3] = BPF_MOV64_IMM(BPF_REG_3, 0);
	insns[4] = BPF_MOV64_IMM(BPF_REG_4, 0);
	insns[5] = BPF_EMIT_CALL(BPF_FUNC_loop);
	insns[GOTOX_CB_MAIN_TGT] = BPF_MOV64_IMM(BPF_REG_0, 0);
	insns[7] = BPF_EXIT_INSN();

	/* r1 = &jt_main[0], by index 0 into fd_array */
	insns[GOTOX_CB_START] = (struct bpf_insn) {
		.code = BPF_LD | BPF_DW | BPF_IMM,
		.dst_reg = BPF_REG_1,
		.src_reg = BPF_PSEUDO_MAP_IDX_VALUE,
		.imm = 0,
	};
	insns[9] = (struct bpf_insn) { .imm = 0 };
	insns[10] = BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_1, 0);
	insns[GOTOX_CB_GOTOX] = BPF_RAW_INSN(BPF_JMP | BPF_JA | BPF_X, BPF_REG_1, 0, 0, 0);
	insns[GOTOX_CB_TGT] = BPF_MOV64_IMM(BPF_REG_0, 0);
	insns[13] = BPF_EXIT_INSN();
}

static void check_gotox_callback_leaves_subprog(void)
{
	const __u32 starts[] = { 0, GOTOX_CB_START };
	const __u8 linkage[] = { BTF_FUNC_GLOBAL, BTF_FUNC_STATIC };
	const __u32 jt_main[] = { GOTOX_CB_MAIN_TGT };
	const __u32 jt_cb[] = { GOTOX_CB_TGT };
	struct bpf_insn insns[GOTOX_CB_INSN_CNT];
	struct bpf_func_info fi[2];
	struct btf *btf = NULL;
	int btf_fd;

	gotox_callback_fill(insns);

	btf_fd = gotox_btf_create(starts, linkage, ARRAY_SIZE(starts), fi, &btf);
	if (btf_fd < 0)
		return;

	check_gotox_two_maps(insns, ARRAY_SIZE(insns), jt_main, ARRAY_SIZE(jt_main),
			     jt_cb, ARRAY_SIZE(jt_cb), btf_fd, fi, ARRAY_SIZE(fi),
			     -EINVAL, "indirect jump from insn 11 to 6 leaves the subprog [8,14)");
	btf__free(btf);
}

static void check_bpf_side(void)
{
	check_bpf_no_lookup();
}

/* Return the xlated index of the first insn equal to 'insn', or -1. */
static int xlated_insn_index(int prog_fd, const struct bpf_insn *insn)
{
	struct bpf_insn *xlated;
	int idx = -1;
	__u32 cnt, i;

	if (!ASSERT_OK(get_xlated_program(prog_fd, &xlated, &cnt), "get_xlated_program"))
		return -1;
	for (i = 0; i < cnt; i++)
		if (!memcmp(&xlated[i], insn, sizeof(*insn))) {
			idx = i;
			break;
		}
	free(xlated);
	return idx;
}

/*
 * Load 'insns' with a jump table naming insn 0, which a prologue prepend
 * pushes down, and check that the slot followed it.
 */
static void check_gotox_target_prepend(enum bpf_prog_type prog_type,
				       struct bpf_insn *insns, __u32 insn_cnt)
{
	LIBBPF_OPTS(bpf_prog_load_opts, opts);
	struct bpf_insn_array_value val = {};
	int prog_fd = -1, map_fd, idx;
	__u32 key = 0;

	map_fd = map_create(BPF_MAP_TYPE_INSN_ARRAY, 1);
	if (!ASSERT_GE(map_fd, 0, "map_create"))
		return;

	val.orig_off = 0;
	if (!ASSERT_EQ(bpf_map_update_elem(map_fd, &key, &val, 0), 0, "bpf_map_update_elem"))
		goto cleanup;
	if (!ASSERT_EQ(bpf_map_freeze(map_fd), 0, "bpf_map_freeze"))
		goto cleanup;

	opts.fd_array = &map_fd;
	opts.fd_array_cnt = 1;
	prog_fd = bpf_prog_load(prog_type, NULL, "GPL", insns, insn_cnt, &opts);
	if (!ASSERT_GE(prog_fd, 0, "bpf(BPF_PROG_LOAD)"))
		goto cleanup;

	idx = xlated_insn_index(prog_fd, &insns[0]);
	if (!ASSERT_GT(idx, 0, "insn 0 was prepended to"))
		goto cleanup;
	if (!ASSERT_EQ(bpf_map_lookup_elem(map_fd, &key, &val), 0, "bpf_map_lookup_elem"))
		goto cleanup;
	ASSERT_EQ(val.xlated_off, (__u32)idx, "gotox target followed insn 0 past the prepend");
cleanup:
	if (prog_fd >= 0)
		close(prog_fd);
	close(map_fd);
}

static void check_gotox_target_prologue_shift(void)
{
	struct bpf_insn insns[] = {
		/* insn 0: gotox target and subprog start */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		/* may_goto +4 -> exit block, bounds the loop */
		BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, 0, 4, 0),
		/* r1 = &jt[0] (insns 2 and 3) */
		BPF_LD_IMM64_RAW(BPF_REG_1, BPF_PSEUDO_MAP_IDX_VALUE, 0),
		BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_1, 0), /* r1 = ips[0] */
		/* insn 5: gotox r1 -> insn 0 */
		BPF_RAW_INSN(BPF_JMP | BPF_JA | BPF_X, BPF_REG_1, 0, 0, 0),
		BPF_MOV64_IMM(BPF_REG_0, XDP_PASS), /* insn 6: exit block */
		BPF_EXIT_INSN(),
	};

	check_gotox_target_prepend(BPF_PROG_TYPE_XDP, insns, ARRAY_SIZE(insns));
}

static void check_gotox_target_ctx_prologue_shift(void)
{
	struct bpf_insn insns[] = {
		/* insn 0: gotox target and subprog start */
		BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_10, -8),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_7, 0x5a5a, 12), /* second visit exits */
		BPF_ST_MEM(BPF_DW, BPF_REG_10, -8, 0x5a5a),
		BPF_MOV64_REG(BPF_REG_6, BPF_REG_1), /* r6 = ctx */
		BPF_LDX_MEM(BPF_W, BPF_REG_2, BPF_REG_6, offsetof(struct __sk_buff, data)),
		BPF_LDX_MEM(BPF_W, BPF_REG_3, BPF_REG_6, offsetof(struct __sk_buff, data_end)),
		BPF_MOV64_REG(BPF_REG_4, BPF_REG_2),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, 1),
		BPF_JMP_REG(BPF_JGT, BPF_REG_4, BPF_REG_3, 5),
		/* insn 9: direct packet write -> tc unclone prologue prepended at insn 0 */
		BPF_ST_MEM(BPF_B, BPF_REG_2, 0, 0),
		/* r5 = &jt[0] (insns 10 and 11) */
		BPF_LD_IMM64_RAW(BPF_REG_5, BPF_PSEUDO_MAP_IDX_VALUE, 0),
		BPF_LDX_MEM(BPF_DW, BPF_REG_5, BPF_REG_5, 0), /* r5 = ips[0] */
		/* insn 13: gotox r5 -> insn 0 */
		BPF_RAW_INSN(BPF_JMP | BPF_JA | BPF_X, BPF_REG_5, 0, 0, 0),
		BPF_MOV64_IMM(BPF_REG_0, 0), /* insn 14: exit block */
		BPF_EXIT_INSN(),
	};

	check_gotox_target_prepend(BPF_PROG_TYPE_SCHED_CLS, insns, ARRAY_SIZE(insns));
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
	if (test__start_subtest("gotox-jt-spans-subprogs"))
		check_gotox_jt_spans_subprogs();
	if (test__start_subtest("gotox-jt-spans-with-own-table"))
		check_gotox_jt_spans_with_own_table();
	if (test__start_subtest("gotox-target-other-subprog"))
		check_gotox_target_other_subprog();
	if (test__start_subtest("gotox-jt-per-subprog"))
		check_gotox_jt_per_subprog();
	if (test__start_subtest("gotox-jt-dead-subprogs"))
		check_gotox_jt_dead_subprogs();
	if (test__start_subtest("gotox-target-subprog-from-main"))
		check_gotox_target_subprog_from_main();
	if (test__start_subtest("gotox-index-slice-other-subprog"))
		check_gotox_index_slice_other_subprog();
	if (test__start_subtest("gotox-target-other-global-subprog"))
		check_gotox_target_other_global_subprog();
	if (test__start_subtest("gotox-callback-leaves-subprog"))
		check_gotox_callback_leaves_subprog();
	if (test__start_subtest("gotox-target-prologue-shift"))
		check_gotox_target_prologue_shift();
	if (test__start_subtest("gotox-target-ctx-prologue-shift"))
		check_gotox_target_ctx_prologue_shift();
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
