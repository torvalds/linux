// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include <test_progs.h>
static int libbpf_debug_print(enum libbpf_print_level level,
			      const char *format, va_list args)
{
	if (level != LIBBPF_DEBUG)
		return 0;

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
	const char *scale[] = {
		"./test_verif_scale1.o", "./test_verif_scale2.o", "./test_verif_scale3.o"
	};
	const char *pyperf[] = {
		"./pyperf50.o",	"./pyperf100.o", "./pyperf180.o"
	};
	int err, i;

	if (verifier_stats)
		libbpf_set_print(libbpf_debug_print);

	for (i = 0; i < ARRAY_SIZE(scale); i++) {
		err = check_load(scale[i], BPF_PROG_TYPE_SCHED_CLS);
		printf("test_scale:%s:%s\n", scale[i], err ? "FAIL" : "OK");
	}

	for (i = 0; i < ARRAY_SIZE(pyperf); i++) {
		err = check_load(pyperf[i], BPF_PROG_TYPE_RAW_TRACEPOINT);
		printf("test_scale:%s:%s\n", pyperf[i], err ? "FAIL" : "OK");
	}
}
