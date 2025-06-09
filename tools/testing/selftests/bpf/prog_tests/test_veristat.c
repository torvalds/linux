// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <string.h>
#include <stdio.h>

#define __CHECK_STR(str, name)					    \
	do {							    \
		if (!ASSERT_HAS_SUBSTR(fix->output, (str), (name))) \
			goto out;				    \
	} while (0)

struct fixture {
	char tmpfile[80];
	int fd;
	char *output;
	size_t sz;
	char veristat[80];
};

static struct fixture *init_fixture(void)
{
	struct fixture *fix = malloc(sizeof(struct fixture));

	/* for no_alu32 and cpuv4 veristat is in parent folder */
	if (access("./veristat", F_OK) == 0)
		strcpy(fix->veristat, "./veristat");
	else if (access("../veristat", F_OK) == 0)
		strcpy(fix->veristat, "../veristat");
	else
		PRINT_FAIL("Can't find veristat binary");

	snprintf(fix->tmpfile, sizeof(fix->tmpfile), "/tmp/test_veristat.XXXXXX");
	fix->fd = mkstemp(fix->tmpfile);
	fix->sz = 1000000;
	fix->output = malloc(fix->sz);
	return fix;
}

static void teardown_fixture(struct fixture *fix)
{
	free(fix->output);
	close(fix->fd);
	remove(fix->tmpfile);
	free(fix);
}

static void test_set_global_vars_succeeds(void)
{
	struct fixture *fix = init_fixture();

	SYS(out,
	    "%s set_global_vars.bpf.o"\
	    " -G \"var_s64 = 0xf000000000000001\" "\
	    " -G \"var_u64 = 0xfedcba9876543210\" "\
	    " -G \"var_s32 = -0x80000000\" "\
	    " -G \"var_u32 = 0x76543210\" "\
	    " -G \"var_s16 = -32768\" "\
	    " -G \"var_u16 = 60652\" "\
	    " -G \"var_s8 = -128\" "\
	    " -G \"var_u8 = 255\" "\
	    " -G \"var_ea = EA2\" "\
	    " -G \"var_eb = EB2\" "\
	    " -G \"var_ec = EC2\" "\
	    " -G \"var_b = 1\" "\
	    " -G \"struct1.struct2.u.var_u8 = 170\" "\
	    " -G \"union1.struct3.var_u8_l = 0xaa\" "\
	    " -G \"union1.struct3.var_u8_h = 0xaa\" "\
	    "-vl2 > %s", fix->veristat, fix->tmpfile);

	read(fix->fd, fix->output, fix->sz);
	__CHECK_STR("_w=0xf000000000000001 ", "var_s64 = 0xf000000000000001");
	__CHECK_STR("_w=0xfedcba9876543210 ", "var_u64 = 0xfedcba9876543210");
	__CHECK_STR("_w=0x80000000 ", "var_s32 = -0x80000000");
	__CHECK_STR("_w=0x76543210 ", "var_u32 = 0x76543210");
	__CHECK_STR("_w=0x8000 ", "var_s16 = -32768");
	__CHECK_STR("_w=0xecec ", "var_u16 = 60652");
	__CHECK_STR("_w=128 ", "var_s8 = -128");
	__CHECK_STR("_w=255 ", "var_u8 = 255");
	__CHECK_STR("_w=11 ", "var_ea = EA2");
	__CHECK_STR("_w=12 ", "var_eb = EB2");
	__CHECK_STR("_w=13 ", "var_ec = EC2");
	__CHECK_STR("_w=1 ", "var_b = 1");
	__CHECK_STR("_w=170 ", "struct1.struct2.u.var_u8 = 170");
	__CHECK_STR("_w=0xaaaa ", "union1.var_u16 = 0xaaaa");

out:
	teardown_fixture(fix);
}

static void test_set_global_vars_from_file_succeeds(void)
{
	struct fixture *fix = init_fixture();
	char input_file[80];
	const char *vars = "var_s16 = -32768\nvar_u16 = 60652";
	int fd;

	snprintf(input_file, sizeof(input_file), "/tmp/veristat_input.XXXXXX");
	fd = mkstemp(input_file);
	if (!ASSERT_GE(fd, 0, "valid fd"))
		goto out;

	write(fd, vars, strlen(vars));
	syncfs(fd);
	SYS(out, "%s set_global_vars.bpf.o -G \"@%s\" -vl2 > %s",
	    fix->veristat, input_file, fix->tmpfile);
	read(fix->fd, fix->output, fix->sz);
	__CHECK_STR("_w=0x8000 ", "var_s16 = -32768");
	__CHECK_STR("_w=0xecec ", "var_u16 = 60652");

out:
	close(fd);
	remove(input_file);
	teardown_fixture(fix);
}

static void test_set_global_vars_out_of_range(void)
{
	struct fixture *fix = init_fixture();

	SYS_FAIL(out,
		 "%s set_global_vars.bpf.o -G \"var_s32 = 2147483648\" -vl2 2> %s",
		 fix->veristat, fix->tmpfile);

	read(fix->fd, fix->output, fix->sz);
	__CHECK_STR("is out of range [-2147483648; 2147483647]", "out of range");

out:
	teardown_fixture(fix);
}

void test_veristat(void)
{
	if (test__start_subtest("set_global_vars_succeeds"))
		test_set_global_vars_succeeds();

	if (test__start_subtest("set_global_vars_out_of_range"))
		test_set_global_vars_out_of_range();

	if (test__start_subtest("set_global_vars_from_file_succeeds"))
		test_set_global_vars_from_file_succeeds();
}

#undef __CHECK_STR
