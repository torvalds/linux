// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include <bpf/bpf.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

#define ARENA_PAGES 32

static char log_buf[16384];

static void test_arena_direct_value_one_past_end(void)
{
	char expected[128];
	__u32 arena_sz = ARENA_PAGES * getpagesize();
	struct bpf_insn insns[] = {
		BPF_LD_IMM64_RAW(BPF_REG_1, BPF_PSEUDO_MAP_VALUE, 0),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	LIBBPF_OPTS(bpf_map_create_opts, map_opts);
	LIBBPF_OPTS(bpf_prog_load_opts, prog_opts);
	void *arena;
	int map_fd, prog_fd;

	map_opts.map_flags = BPF_F_MMAPABLE;
	prog_opts.log_buf = log_buf;
	prog_opts.log_size = sizeof(log_buf);
	prog_opts.log_level = 1;

	map_fd = bpf_map_create(BPF_MAP_TYPE_ARENA, "arena_direct_value",
				0, 0, ARENA_PAGES, &map_opts);
	if (map_fd < 0) {
		if (errno == EOPNOTSUPP) {
			test__skip();
			return;
		}
		ASSERT_GE(map_fd, 0, "bpf_map_create");
		return;
	}

	arena = mmap(NULL, arena_sz, PROT_READ | PROT_WRITE, MAP_SHARED, map_fd, 0);
	if (!ASSERT_NEQ(arena, MAP_FAILED, "arena_mmap"))
		goto cleanup;

	insns[0].imm = map_fd;
	insns[1].imm = arena_sz;

	prog_fd = bpf_prog_load(BPF_PROG_TYPE_RAW_TRACEPOINT,
				"arena_direct_value", "GPL", insns,
				ARRAY_SIZE(insns), &prog_opts);
	if (!ASSERT_LT(prog_fd, 0, "prog_load")) {
		close(prog_fd);
		goto cleanup;
	}

	snprintf(expected, sizeof(expected),
		 "invalid access to map value pointer, value_size=0 off=%u",
		 arena_sz);
	ASSERT_HAS_SUBSTR(log_buf, expected, "verifier_log");

cleanup:
	if (arena != MAP_FAILED)
		munmap(arena, arena_sz);
	close(map_fd);
}

void test_arena_direct_value(void)
{
	if (test__start_subtest("one_past_end"))
		test_arena_direct_value_one_past_end();
}
