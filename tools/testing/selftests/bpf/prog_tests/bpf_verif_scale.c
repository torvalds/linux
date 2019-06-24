// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include <test_progs.h>
static int libbpf_debug_print(enum libbpf_print_level level,
			      const char *format, va_list args)
{
	if (level != LIBBPF_DEBUG)
		return vfprintf(stderr, format, args);

	if (!strstr(format, "verifier log"))
		return 0;
	return vfprintf(stderr, "%s", args);
}

static int check_load(const char *file, enum bpf_prog_type type)
{
	struct bpf_prog_load_attr attr;
	struct bpf_object *obj = NULL;
	int err, prog_fd;

	memset(&attr, 0, sizeof(struct bpf_prog_load_attr));
	attr.file = file;
	attr.prog_type = type;
	attr.log_level = 4;
	attr.prog_flags = BPF_F_TEST_RND_HI32;
	err = bpf_prog_load_xattr(&attr, &obj, &prog_fd);
	bpf_object__close(obj);
	if (err)
		error_cnt++;
	return err;
}

void test_bpf_verif_scale(void)
{
	const char *sched_cls[] = {
		"./test_verif_scale1.o", "./test_verif_scale2.o", "./test_verif_scale3.o",
	};
	const char *raw_tp[] = {
		/* full unroll by llvm */
		"./pyperf50.o",	"./pyperf100.o", "./pyperf180.o",

		/* partial unroll. llvm will unroll loop ~150 times.
		 * C loop count -> 600.
		 * Asm loop count -> 4.
		 * 16k insns in loop body.
		 * Total of 5 such loops. Total program size ~82k insns.
		 */
		"./pyperf600.o",

		/* no unroll at all.
		 * C loop count -> 600.
		 * ASM loop count -> 600.
		 * ~110 insns in loop body.
		 * Total of 5 such loops. Total program size ~1500 insns.
		 */
		"./pyperf600_nounroll.o",

		"./loop1.o", "./loop2.o",

		/* partial unroll. 19k insn in a loop.
		 * Total program size 20.8k insn.
		 * ~350k processed_insns
		 */
		"./strobemeta.o",

		/* no unroll, tiny loops */
		"./strobemeta_nounroll1.o",
		"./strobemeta_nounroll2.o",
	};
	const char *cg_sysctl[] = {
		"./test_sysctl_loop1.o", "./test_sysctl_loop2.o",
	};
	int err, i;

	if (verifier_stats)
		libbpf_set_print(libbpf_debug_print);

	err = check_load("./loop3.o", BPF_PROG_TYPE_RAW_TRACEPOINT);
	printf("test_scale:loop3:%s\n", err ? (error_cnt--, "OK") : "FAIL");

	for (i = 0; i < ARRAY_SIZE(sched_cls); i++) {
		err = check_load(sched_cls[i], BPF_PROG_TYPE_SCHED_CLS);
		printf("test_scale:%s:%s\n", sched_cls[i], err ? "FAIL" : "OK");
	}

	for (i = 0; i < ARRAY_SIZE(raw_tp); i++) {
		err = check_load(raw_tp[i], BPF_PROG_TYPE_RAW_TRACEPOINT);
		printf("test_scale:%s:%s\n", raw_tp[i], err ? "FAIL" : "OK");
	}

	for (i = 0; i < ARRAY_SIZE(cg_sysctl); i++) {
		err = check_load(cg_sysctl[i], BPF_PROG_TYPE_CGROUP_SYSCTL);
		printf("test_scale:%s:%s\n", cg_sysctl[i], err ? "FAIL" : "OK");
	}
	err = check_load("./test_xdp_loop.o", BPF_PROG_TYPE_XDP);
	printf("test_scale:test_xdp_loop:%s\n", err ? "FAIL" : "OK");

	err = check_load("./test_seg6_loop.o", BPF_PROG_TYPE_LWT_SEG6LOCAL);
	printf("test_scale:test_seg6_loop:%s\n", err ? "FAIL" : "OK");
}
