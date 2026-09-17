// SPDX-License-Identifier: GPL-2.0

/* Test cases that can't load programs using libbpf and need direct
 * BPF syscall access
 */

#include <sys/syscall.h>
#include <bpf/libbpf.h>
#include <bpf/btf.h>

#include "test_progs.h"
#include "test_btf.h"
#include "bpf/libbpf_internal.h"

static char log[16 * 1024];

static int load_core_relo_insns(int btf_fd, struct bpf_insn *insns, int insn_cnt,
				struct bpf_func_info *funcs, int func_cnt,
				int enum_id, int access_str_off, int insn_idx,
				bool relocate)
{
	struct bpf_core_relo relo = {
		.insn_off = insn_idx * sizeof(struct bpf_insn),
		.type_id = enum_id,
		.access_str_off = access_str_off,
		.kind = BPF_CORE_ENUMVAL_VALUE,
	};
	union bpf_attr attr = {
		.prog_type = BPF_PROG_TYPE_SOCKET_FILTER,
		.insn_cnt = insn_cnt,
		.insns = (__u64)insns,
		.license = (__u64)"GPL",
		.log_buf = (__u64)log,
		.log_size = sizeof(log),
		.log_level = 2,
		.prog_btf_fd = btf_fd,
		.func_info_rec_size = sizeof(struct bpf_func_info),
		.func_info = (__u64)funcs,
		.func_info_cnt = func_cnt,
	};

	if (relocate) {
		attr.core_relo_cnt = 1;
		attr.core_relos = (__u64)&relo;
		attr.core_relo_rec_size = sizeof(relo);
	}
	memset(log, 0, sizeof(log));
	return sys_bpf_prog_load(&attr, sizeof(attr), 1);
}

static void test_early_core_relo(void)
{
	struct test_btf {
		struct btf_header hdr;
		__u32 types[18];
		char strings[64];
	} raw_btf = {
		.hdr = {
			.magic = BTF_MAGIC,
			.version = BTF_VERSION,
			.hdr_len = sizeof(struct btf_header),
			.type_off = 0,
			.type_len = sizeof(raw_btf.types),
			.str_off = offsetof(struct test_btf, strings) -
				   offsetof(struct test_btf, types),
			.str_len = sizeof(raw_btf.strings),
		},
		.types = {
			BTF_TYPE_INT_ENC(1, BTF_INT_SIGNED, 0, 32, 4), /* [1] int */
			BTF_FUNC_PROTO_ENC(1, 0),	/* [2] int (*)(void) */
			BTF_FUNC_ENC(5, 2),		/* [3] main_fn */
			BTF_FUNC_ENC(13, 2),		/* [4] sub_fn */
			BTF_TYPE_ENC(20, BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 4), /* [5] enum */
			BTF_ENUM_ENC(45, 0),		/* value = 0 */
		},
		.strings = "\0int\0main_fn\0sub_fn\0core_relo_poison_missing\0value\0" "0",
	};
	struct bpf_func_info funcs[] = {
		{ .insn_off = 0, .type_id = 3 },
		{ .insn_off = 3, .type_id = 4 },
	};
	struct bpf_insn core_only[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	struct bpf_insn subprog[] = {
		BPF_CALL_REL(2),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	struct bpf_insn truncated_ldimm64[] = {
		BPF_RAW_INSN(BPF_LD | BPF_IMM | BPF_DW, 0, 0, 0, 0),
	};
	int access_str_off = 51; /* offset of "0" */
	int enum_id = 5;
	int btf_fd, prog_fd = -1;

	btf_fd = bpf_btf_load(&raw_btf, sizeof(raw_btf), NULL);
	if (!ASSERT_GE(btf_fd, 0, "btf_load"))
		goto cleanup;

	if (test__start_subtest("without_func_info")) {
		prog_fd = load_core_relo_insns(btf_fd, core_only, ARRAY_SIZE(core_only), NULL, 0,
					       enum_id, access_str_off, 2, false);
		if (!ASSERT_GE(prog_fd, 0, "control_load"))
			goto cleanup;
		close(prog_fd);
		prog_fd = load_core_relo_insns(btf_fd, core_only, ARRAY_SIZE(core_only), NULL, 0,
					       enum_id, access_str_off, 2, true);
		if (!ASSERT_GE(prog_fd, 0, "poisoned_load"))
			goto cleanup;
		ASSERT_HAS_SUBSTR(log, "substituting insn #2", "poison_log");
		close(prog_fd);
		prog_fd = -1;
	}

	if (test__start_subtest("before_subprog_validation")) {
		prog_fd = load_core_relo_insns(btf_fd, subprog, ARRAY_SIZE(subprog), funcs, 2,
					       enum_id, access_str_off, 2, true);
		if (!ASSERT_LT(prog_fd, 0, "poisoned_load"))
			goto cleanup;
		ASSERT_HAS_SUBSTR(log, "substituting insn #2", "poison_log");
		ASSERT_HAS_SUBSTR(log, "last insn is not an exit or jmp", "poisoned_load_log");
	}

	if (test__start_subtest("truncated_ldimm64")) {
		prog_fd = load_core_relo_insns(btf_fd, truncated_ldimm64,
					       ARRAY_SIZE(truncated_ldimm64), NULL, 0,
					       enum_id, access_str_off, 0, true);
		if (!ASSERT_LT(prog_fd, 0, "truncated_load"))
			goto cleanup;
		ASSERT_HAS_SUBSTR(log, "invalid bpf_ld_imm64 insn", "truncated_load_log");
	}

cleanup:
	if (env.verbosity > VERBOSE_NORMAL && log[0]) {
		printf("-------- program load log start --------\n");
		printf("%s", log);
		printf("-------- program load log end ----------\n");
	}
	close(prog_fd);
	close(btf_fd);
}

/* Check that verifier rejects BPF program containing relocation
 * pointing to non-existent BTF type.
 */
static void test_bad_local_id(void)
{
	struct test_btf {
		struct btf_header hdr;
		__u32 types[15];
		char strings[128];
	} raw_btf = {
		.hdr = {
			.magic = BTF_MAGIC,
			.version = BTF_VERSION,
			.hdr_len = sizeof(struct btf_header),
			.type_off = 0,
			.type_len = sizeof(raw_btf.types),
			.str_off = offsetof(struct test_btf, strings) -
				   offsetof(struct test_btf, types),
			.str_len = sizeof(raw_btf.strings),
		},
		.types = {
			BTF_PTR_ENC(0),					/* [1] void*  */
			BTF_TYPE_INT_ENC(1, BTF_INT_SIGNED, 0, 32, 4),	/* [2] int    */
			BTF_FUNC_PROTO_ENC(2, 1),			/* [3] int (*)(void*) */
			BTF_FUNC_PROTO_ARG_ENC(8, 1),
			BTF_FUNC_ENC(8, 3)			/* [4] FUNC 'foo' type_id=2   */
		},
		.strings = "\0int\0 0\0foo\0"
	};
	__u32 log_level = 1 | 2 | 4;
	LIBBPF_OPTS(bpf_btf_load_opts, opts,
		    .log_buf = log,
		    .log_size = sizeof(log),
		    .log_level = log_level,
	);
	struct bpf_insn insns[] = {
		BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	struct bpf_func_info funcs[] = {
		{
			.insn_off = 0,
			.type_id = 4,
		}
	};
	struct bpf_core_relo relos[] = {
		{
			.insn_off = 0,		/* patch first instruction (r0 = 0) */
			.type_id = 100500,	/* !!! this type id does not exist */
			.access_str_off = 6,	/* offset of "0" */
			.kind = BPF_CORE_TYPE_ID_LOCAL,
		}
	};
	union bpf_attr attr;
	int saved_errno;
	int prog_fd = -1;
	int btf_fd = -1;

	btf_fd = bpf_btf_load(&raw_btf, sizeof(raw_btf), &opts);
	saved_errno = errno;
	if (btf_fd < 0 || env.verbosity > VERBOSE_NORMAL) {
		printf("-------- BTF load log start --------\n");
		printf("%s", log);
		printf("-------- BTF load log end ----------\n");
	}
	if (btf_fd < 0) {
		PRINT_FAIL("bpf_btf_load() failed, errno=%d\n", saved_errno);
		return;
	}

	log[0] = 0;
	memset(&attr, 0, sizeof(attr));
	attr.prog_btf_fd = btf_fd;
	attr.prog_type = BPF_TRACE_RAW_TP;
	attr.license = (__u64)"GPL";
	attr.insns = (__u64)&insns;
	attr.insn_cnt = sizeof(insns) / sizeof(*insns);
	attr.log_buf = (__u64)log;
	attr.log_size = sizeof(log);
	attr.log_level = log_level;
	attr.func_info = (__u64)funcs;
	attr.func_info_cnt = sizeof(funcs) / sizeof(*funcs);
	attr.func_info_rec_size = sizeof(*funcs);
	attr.core_relos = (__u64)relos;
	attr.core_relo_cnt = sizeof(relos) / sizeof(*relos);
	attr.core_relo_rec_size = sizeof(*relos);
	prog_fd = sys_bpf_prog_load(&attr, sizeof(attr), 1);
	saved_errno = errno;
	if (prog_fd < 0 || env.verbosity > VERBOSE_NORMAL) {
		printf("-------- program load log start --------\n");
		printf("%s", log);
		printf("-------- program load log end ----------\n");
	}
	if (prog_fd >= 0) {
		PRINT_FAIL("sys_bpf_prog_load() expected to fail\n");
		goto out;
	}
	ASSERT_HAS_SUBSTR(log, "relo #0: bad type id 100500", "program load log");

out:
	close(prog_fd);
	close(btf_fd);
}

void test_core_reloc_raw(void)
{
	test_early_core_relo();
	if (test__start_subtest("bad_local_id"))
		test_bad_local_id();
}
