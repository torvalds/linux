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

static int check_load(const char *file)
{
	struct bpf_prog_load_attr attr;
	struct bpf_object *obj;
	int err, prog_fd;

	memset(&attr, 0, sizeof(struct bpf_prog_load_attr));
	attr.file = file;
	attr.prog_type = BPF_PROG_TYPE_SCHED_CLS;
	attr.log_level = 4;
	err = bpf_prog_load_xattr(&attr, &obj, &prog_fd);
	bpf_object__close(obj);
	if (err)
		error_cnt++;
	return err;
}

void test_bpf_verif_scale(void)
{
	const char *file1 = "./test_verif_scale1.o";
	const char *file2 = "./test_verif_scale2.o";
	const char *file3 = "./test_verif_scale3.o";
	int err;

	if (verifier_stats)
		libbpf_set_print(libbpf_debug_print);

	err = check_load(file1);
	err |= check_load(file2);
	err |= check_load(file3);
	if (!err)
		printf("test_verif_scale:OK\n");
	else
		printf("test_verif_scale:FAIL\n");
}
