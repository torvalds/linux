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
		strscpy(fix->veristat, "./veristat");
	else if (access("../veristat", F_OK) == 0)
		strscpy(fix->veristat, "../veristat");
	else
		PRINT_FAIL("Can't find veristat binary");

	snprintf(fix->tmpfile, sizeof(fix->tmpfile), "/tmp/test_veristat.XXXXXX");
	fix->fd = mkstemp(fix->tmpfile);
	fix->sz = 1000000;
	fix->output = malloc(fix->sz);
	return fix;
}

static void read_output(struct fixture *fix)
{
	ssize_t len = pread(fix->fd, fix->output, fix->sz - 1, 0);

	fix->output[len < 0 ? 0 : len] = 0;
	ASSERT_GE(len, 0, "pread");
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
	    " -G \"var_eb  =  EB2\" "\
	    " -G \"var_ec=EC2\" "\
	    " -G \"var_b = 1\" "\
	    " -G \"struct1[2].struct2[1][2].u.var_u8[2]=170\" "\
	    " -G \"union1.struct3.var_u8_l = 0xaa\" "\
	    " -G \"union1.struct3.var_u8_h = 0xaa\" "\
	    " -G \"arr[3]= 171\" "	\
	    " -G \"arr[EA2] =172\" "	\
	    " -G \"enum_arr[EC2]=EA3\" " \
	    " -G \"three_d[31][7][EA2]=173\"" \
	    " -G \"struct1[2].struct2[1][2].u.mat[5][3]=174\" " \
	    " -G \"struct11 [ 7 ] [ 5 ] .struct2[0][1].u.mat[3][0] = 175\" " \
	    " -vl2 > %s", fix->veristat, fix->tmpfile);

	read(fix->fd, fix->output, fix->sz);
	__CHECK_STR("=0xf000000000000001 ", "var_s64 = 0xf000000000000001");
	__CHECK_STR("=0xfedcba9876543210 ", "var_u64 = 0xfedcba9876543210");
	__CHECK_STR("=0x80000000 ", "var_s32 = -0x80000000");
	__CHECK_STR("=0x76543210 ", "var_u32 = 0x76543210");
	__CHECK_STR("=0x8000 ", "var_s16 = -32768");
	__CHECK_STR("=0xecec ", "var_u16 = 60652");
	__CHECK_STR("=128 ", "var_s8 = -128");
	__CHECK_STR("=255 ", "var_u8 = 255");
	__CHECK_STR("=11 ", "var_ea = EA2");
	__CHECK_STR("=12 ", "var_eb = EB2");
	__CHECK_STR("=13 ", "var_ec = EC2");
	__CHECK_STR("=1 ", "var_b = 1");
	__CHECK_STR("=170 ", "struct1[2].struct2[1][2].u.var_u8[2]=170");
	__CHECK_STR("=0xaaaa ", "union1.var_u16 = 0xaaaa");
	__CHECK_STR("=171 ", "arr[3]= 171");
	__CHECK_STR("=172 ", "arr[EA2] =172");
	__CHECK_STR("=10 ", "enum_arr[EC2]=EA3");
	__CHECK_STR("=173 ", "matrix[31][7][11]=173");
	__CHECK_STR("=174 ", "struct1[2].struct2[1][2].u.mat[5][3]=174");
	__CHECK_STR("=175 ", "struct11[7][5].struct2[0][1].u.mat[3][0]=175");

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
	__CHECK_STR("=0x8000 ", "var_s16 = -32768");
	__CHECK_STR("=0xecec ", "var_u16 = 60652");

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

static void test_unsupported_ptr_array_type(void)
{
	struct fixture *fix = init_fixture();

	SYS_FAIL(out,
		 "%s set_global_vars.bpf.o -G \"ptr_arr[0] = 0\" -vl2 2> %s",
		 fix->veristat, fix->tmpfile);

	read(fix->fd, fix->output, fix->sz);
	__CHECK_STR("Can't set ptr_arr[0]. Only ints and enums are supported", "ptr_arr");

out:
	teardown_fixture(fix);
}

static void test_array_out_of_bounds(void)
{
	struct fixture *fix = init_fixture();

	SYS_FAIL(out,
		 "%s set_global_vars.bpf.o -G \"arr[99] = 0\" -vl2 2> %s",
		 fix->veristat, fix->tmpfile);

	read(fix->fd, fix->output, fix->sz);
	__CHECK_STR("Array index 99 is out of bounds", "arr[99]");

out:
	teardown_fixture(fix);
}

static void test_array_index_not_found(void)
{
	struct fixture *fix = init_fixture();

	SYS_FAIL(out,
		 "%s set_global_vars.bpf.o -G \"arr[EG2] = 0\" -vl2 2> %s",
		 fix->veristat, fix->tmpfile);

	read(fix->fd, fix->output, fix->sz);
	__CHECK_STR("Can't resolve enum value EG2", "arr[EG2]");

out:
	teardown_fixture(fix);
}

static void test_array_index_for_non_array(void)
{
	struct fixture *fix = init_fixture();

	SYS_FAIL(out,
		 "%s set_global_vars.bpf.o -G \"var_b[0] = 1\" -vl2 2> %s",
		 fix->veristat, fix->tmpfile);

	pread(fix->fd, fix->output, fix->sz, 0);
	__CHECK_STR("Array index is not expected for var_b", "var_b[0] = 1");

	SYS_FAIL(out,
		 "%s set_global_vars.bpf.o -G \"union1.struct3[0].var_u8_l=1\" -vl2 2> %s",
		 fix->veristat, fix->tmpfile);

	pread(fix->fd, fix->output, fix->sz, 0);
	__CHECK_STR("Array index is not expected for struct3", "union1.struct3[0].var_u8_l=1");

out:
	teardown_fixture(fix);
}

static void test_no_array_index_for_array(void)
{
	struct fixture *fix = init_fixture();

	SYS_FAIL(out,
		 "%s set_global_vars.bpf.o -G \"arr = 1\" -vl2 2> %s",
		 fix->veristat, fix->tmpfile);

	pread(fix->fd, fix->output, fix->sz, 0);
	__CHECK_STR("Can't set arr. Only ints and enums are supported", "arr = 1");

	SYS_FAIL(out,
		 "%s set_global_vars.bpf.o -G \"struct1[0].struct2.u.var_u8[2]=1\" -vl2 2> %s",
		 fix->veristat, fix->tmpfile);

	pread(fix->fd, fix->output, fix->sz, 0);
	__CHECK_STR("Can't resolve field u for non-composite type", "struct1[0].struct2.u.var_u8[2]=1");

out:
	teardown_fixture(fix);
}

/*
 * Name filter tests below run veristat on veristat_foo.bpf.o and
 * veristat_bar.bpf.o, both defining programs 'foo', 'bar' and 'buz'.
 * Every entry describes a single (filters, file, prog) combination and
 * tells whether that program is expected in the veristat output:
 * 'true' if it is, 'false' if it is not and -1 if veristat is expected
 * to reject the filter.
 */
#define FILTER_OBJS "veristat_foo.bpf.o veristat_bar.bpf.o"

static const struct name_filter_case {
	const char *filters;
	const char *file;
	const char *prog;
	int included;
} name_filter_cases[] = {
	/* no filters, every program is processed */
	{ "",			"foo", "foo", true  },
	{ "",			"foo", "bar", true  },
	{ "",			"foo", "buz", true  },
	{ "",			"bar", "foo", true  },
	{ "",			"bar", "bar", true  },
	{ "",			"bar", "buz", true  },
	/* deny filters */
	{ "-f '!*foo*'",	"foo", "bar", false },
	{ "-f '!*foo*'",	"bar", "foo", false },
	{ "-f '!*foo*'",	"bar", "bar", true  },
	{ "-f '!*foo*/bar'",	"foo", "bar", false },
	{ "-f '!*foo*/bar'",	"foo", "buz", true  },
	{ "-f '!*foo*/bar'",	"bar", "bar", true  },
	{ "-f '!*foo*/'",	"foo", "bar", false },
	{ "-f '!*foo*/'",	"bar", "bar", true  },
	{ "-f '!/bar'",		"foo", "bar", false },
	{ "-f '!/bar'",		"foo", "foo", true  },
	{ "-f '!/'",		"foo", "bar", -1    },
	{ "-f '!'",		"foo", "bar", -1    },
	/* allow filters */
	{ "-f '*foo*'",		"foo", "bar", true  },
	{ "-f '*foo*'",		"bar", "foo", true  },
	{ "-f '*foo*'",		"bar", "bar", false },
	{ "-f '*foo*/bar'",	"foo", "bar", true  },
	{ "-f '*foo*/bar'",	"foo", "buz", false },
	{ "-f '*foo*/bar'",	"bar", "bar", false },
	{ "-f '*foo*/'",	"foo", "bar", true  },
	{ "-f '*foo*/'",	"bar", "bar", false },
	{ "-f '/bar'",		"foo", "bar", true  },
	{ "-f '/bar'",		"foo", "foo", false },
	{ "-f '/'",		"foo", "bar", -1    },
	{ "-f ''",		"foo", "bar", -1    },
	/* allow and deny filters combined */
	{ "-f '*foo*/' -f '!/bar'", "foo", "foo", true  },
	{ "-f '*foo*/' -f '!/bar'", "foo", "bar", false },
	{ "-f '*foo*/' -f '!/bar'", "bar", "foo", false },
};

static void test_name_filters(void)
{
	struct fixture *fix = init_fixture();
	const struct name_filter_case *t;
	char cmd[512], row[64], name[128];
	int i, err;

	for (i = 0; i < ARRAY_SIZE(name_filter_cases); i++) {
		t = &name_filter_cases[i];
		/* stderr is merged with stdout in order to catch error messages */
		snprintf(cmd, sizeof(cmd), "%s " FILTER_OBJS " -q -o csv -e file,prog %s > %s 2>&1",
			 fix->veristat, t->filters, fix->tmpfile);
		err = system(cmd);
		read_output(fix);

		snprintf(row, sizeof(row), "veristat_%s.bpf.o,%s", t->file, t->prog);
		snprintf(name, sizeof(name), "veristat %s: %s", t->filters, row);
		switch (t->included) {
		case true:
			ASSERT_OK(err, name);
			ASSERT_HAS_SUBSTR(fix->output, row, name);
			break;
		case false:
			ASSERT_OK(err, name);
			ASSERT_FALSE(!!strstr(fix->output, row), name);
			break;
		case -1:
			ASSERT_NEQ(err, 0, name);
			ASSERT_HAS_SUBSTR(fix->output, "Invalid filter", name);
			break;
		}
	}

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

	if (test__start_subtest("test_unsupported_ptr_array_type"))
		test_unsupported_ptr_array_type();

	if (test__start_subtest("test_array_out_of_bounds"))
		test_array_out_of_bounds();

	if (test__start_subtest("test_array_index_not_found"))
		test_array_index_not_found();

	if (test__start_subtest("test_array_index_for_non_array"))
		test_array_index_for_non_array();

	if (test__start_subtest("test_no_array_index_for_array"))
		test_no_array_index_for_array();

	if (test__start_subtest("name_filters"))
		test_name_filters();
}

#undef __CHECK_STR
