// SPDX-License-Identifier: GPL-2.0
/*
 * The kernel replaces the offsets of functions in a frozen read-only map with
 * their addresses when the program is loaded. The program has to be the only
 * user of the map, and no other program can use the map after that.
 */
#include <test_progs.h>
#include <linux/filter.h>
#include <bpf/btf.h>

#if defined(__x86_64__) || defined(__aarch64__) || \
    (defined(__riscv) && __riscv_xlen == 64)

#define CALLEE_INSN	6
#define DATA		0x1234

/*
 * main:   r2 = &value; r2 = *(u64 *)(r2 + 8); r1 = 10; callx r2; exit
 * add1:   r0 = r1; r0 += 1; exit
 *
 * where value is { DATA, offset of add1 in the program }.
 */
static const struct bpf_insn callx_insns[] = {
	BPF_LD_MAP_VALUE(BPF_REG_2, 0, 0),
	BPF_LDX_MEM(BPF_DW, BPF_REG_2, BPF_REG_2, 8),
	BPF_MOV64_IMM(BPF_REG_1, 10),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL | BPF_X, BPF_REG_2, 0, 0, 0),
	BPF_EXIT_INSN(),
	BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 1),
	BPF_EXIT_INSN(),
};

/* reads the data of the map */
static const struct bpf_insn reader_insns[] = {
	BPF_LD_MAP_VALUE(BPF_REG_2, 0, 0),
	BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_2, 0),
	BPF_EXIT_INSN(),
};

/* refers to the map and is rejected: r0 is not set */
static const struct bpf_insn bad_insns[] = {
	BPF_LD_MAP_VALUE(BPF_REG_2, 0, 0),
	BPF_EXIT_INSN(),
};

static char log_buf[16 * 1024];
static struct bpf_func_info func_info[2];
static int btf_fd;

static int create_map(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts, .map_flags = BPF_F_RDONLY_PROG);
	__u64 value[2] = { DATA, CALLEE_INSN * sizeof(struct bpf_insn) };
	int fd, zero = 0;

	fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "callx_rodata", sizeof(int), sizeof(value), 1,
			    &opts);
	if (!ASSERT_OK_FD(fd, "map_create"))
		return -1;
	if (!ASSERT_OK(bpf_map_update_elem(fd, &zero, value, 0), "map_update") ||
	    !ASSERT_OK(bpf_map_freeze(fd), "map_freeze")) {
		close(fd);
		return -1;
	}
	return fd;
}

static int load(const struct bpf_insn *prog_insns, int cnt, int map_fd)
{
	LIBBPF_OPTS(bpf_prog_load_opts, opts,
		.log_buf = log_buf,
		.log_size = sizeof(log_buf),
		.log_level = 1,
	);
	struct bpf_insn insns[ARRAY_SIZE(callx_insns)];

	if (prog_insns == callx_insns) {
		/* add1() is referred to by the data only and is found through func_info */
		opts.prog_btf_fd = btf_fd;
		opts.func_info = func_info;
		opts.func_info_cnt = 2;
		opts.func_info_rec_size = sizeof(func_info[0]);
	}
	memcpy(insns, prog_insns, cnt * sizeof(insns[0]));
	insns[0].imm = map_fd;
	log_buf[0] = 0;
	return bpf_prog_load(BPF_PROG_TYPE_SOCKET_FILTER, "callx_map", "GPL", insns, cnt, &opts);
}

#define LOAD(insns, map_fd) load(insns, ARRAY_SIZE(insns), map_fd)

static void run(int prog_fd, int expected, const char *name)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	char pkt[64] = {};

	topts.data_in = pkt;
	topts.data_size_in = sizeof(pkt);
	if (ASSERT_OK(bpf_prog_test_run_opts(prog_fd, &topts), name))
		ASSERT_EQ(topts.retval, expected, name);
}

static void __test_callx_func_ptr_map(void)
{
	int int_id, proto_id, map_fd = -1, prog_fd = -1, reader_fd = -1, fd, zero = 0;
	int main_id, add1_id;
	struct btf *btf;
	__u64 value[2];

	/* there is no support for callx in the interpreter */
	if (!is_jit_enabled()) {
		test__skip();
		return;
	}

	btf = btf__new_empty();
	if (!ASSERT_OK_PTR(btf, "btf_new"))
		return;
	int_id = btf__add_int(btf, "int", 4, BTF_INT_SIGNED);
	proto_id = btf__add_func_proto(btf, int_id);
	main_id = btf__add_func(btf, "main_prog", BTF_FUNC_GLOBAL, proto_id);
	add1_id = btf__add_func(btf, "add1", BTF_FUNC_STATIC, proto_id);
	if (!ASSERT_GT(main_id, 0, "btf_add_main") ||
	    !ASSERT_GT(add1_id, 0, "btf_add_add1") ||
	    !ASSERT_OK(btf__load_into_kernel(btf), "btf_load"))
		goto out;
	func_info[0].insn_off = 0;
	func_info[0].type_id = main_id;
	func_info[1].insn_off = CALLEE_INSN;
	func_info[1].type_id = add1_id;
	btf_fd = btf__fd(btf);

	/*
	 * Another program relies on what the map has: it's verified with
	 * the data folded into constants. Pointers to functions are not looked
	 * for in such map.
	 */
	map_fd = create_map();
	if (map_fd < 0)
		goto out;
	reader_fd = LOAD(reader_insns, map_fd);
	if (!ASSERT_OK_FD(reader_fd, "load_reader"))
		goto out;
	fd = LOAD(callx_insns, map_fd);
	if (!ASSERT_LT(fd, 0, "load_shared"))
		close(fd);
	ASSERT_HAS_SUBSTR(log_buf, "unreachable insn 6", "log_shared");
	run(reader_fd, DATA, "run_reader");
	close(reader_fd);
	reader_fd = -1;
	close(map_fd);

	map_fd = create_map();
	if (map_fd < 0)
		goto out;

	/* a program that is rejected is not a user, libbpf loads it again to get the log */
	fd = LOAD(bad_insns, map_fd);
	if (!ASSERT_LT(fd, 0, "load_bad"))
		close(fd);

	prog_fd = LOAD(callx_insns, map_fd);
	if (!ASSERT_OK_FD(prog_fd, "load_callx")) {
		printf("%s\n", log_buf);
		goto out;
	}
	run(prog_fd, 11, "run_callx");

	/* the offset of the function is gone from the map, the data is intact */
	if (ASSERT_OK(bpf_map_lookup_elem(map_fd, &zero, value), "map_lookup")) {
		ASSERT_EQ(value[0], DATA, "data");
		ASSERT_NEQ(value[1], CALLEE_INSN * sizeof(struct bpf_insn), "pointer");
	}

	/* no other program can use the map now, another instance of the same one too */
	fd = LOAD(reader_insns, map_fd);
	if (!ASSERT_EQ(fd, -EBUSY, "load_reader_after"))
		close(fd);
	ASSERT_HAS_SUBSTR(log_buf, "has addresses of functions of another program", "log_reader");
	fd = LOAD(callx_insns, map_fd);
	if (!ASSERT_EQ(fd, -EBUSY, "load_second_instance"))
		close(fd);

	run(prog_fd, 11, "run_callx_again");
out:
	if (reader_fd >= 0)
		close(reader_fd);
	if (prog_fd >= 0)
		close(prog_fd);
	if (map_fd >= 0)
		close(map_fd);
	btf__free(btf);
}

#else

static void __test_callx_func_ptr_map(void)
{
	test__skip();
}

#endif

void test_callx_func_ptr_map(void)
{
	__test_callx_func_ptr_map();
}
