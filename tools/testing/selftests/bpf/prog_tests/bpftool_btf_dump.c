// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <bpftool_helpers.h>
#include <bpf/btf.h>
#include <limits.h>
#include <unistd.h>
#include "btf_helpers.h"
#include "testing_helpers.h"

#define DUMP_BUF_SZ		(16 * 1024)

#define EXPECTED_SORTED		"bpftool_btf_dump_sorted.expected"
#define EXPECTED_UNSORTED	"bpftool_btf_dump_unsorted.expected"

/*
 *	struct holey {
 *		int c;
 *		<64-bit hole>
 *		int tail;
 *	};
 *	enum { E0 = 1 };
 *	struct s { int f; };
 */
static struct btf *mk_btf(void)
{
	struct btf *btf;

	btf = btf__new_empty();
	if (!ASSERT_OK_PTR(btf, "new_empty"))
		return NULL;

	btf__add_int(btf, "int", 4, BTF_INT_SIGNED);
	btf__add_int(btf, "long int", 4, BTF_INT_SIGNED);

	btf__add_struct(btf, "holey", 16);
	btf__add_field(btf, "c", 1, 0, 0);
	btf__add_field(btf, "tail", 1, 96, 0);

	btf__add_enum(btf, NULL, 4);
	btf__add_enum_value(btf, "E0", 1);

	btf__add_struct(btf, "s", 4);
	btf__add_field(btf, "f", 1, 0, 0);

	VALIDATE_RAW_BTF(
		btf,
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[2] INT 'long int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED",
		"[3] STRUCT 'holey' size=16 vlen=2\n"
		"\t'c' type_id=1 bits_offset=0\n"
		"\t'tail' type_id=1 bits_offset=96",
		"[4] ENUM '(anon)' encoding=UNSIGNED size=4 vlen=1\n"
		"\t'E0' val=1",
		"[5] STRUCT 's' size=4 vlen=1\n"
		"\t'f' type_id=1 bits_offset=0");

	return btf;
}

static struct btf *mk_loc_btf(void)
{
	struct btf *btf;

	btf = btf__new_empty();
	if (!ASSERT_OK_PTR(btf, "new_empty"))
		return NULL;

	btf__add_int(btf, "int", 4, BTF_INT_SIGNED);
	btf__add_func_proto(btf, 1);
	btf__add_func_param(btf, "arg1", 1);
	btf__add_func_param(btf, "arg2", 1);
	btf__add_func_param(btf, "arg3", 1);
	btf__add_func_param(btf, "arg4", 1);
	btf__add_func_param(btf, "arg5", 1);
	btf__add_func_param(btf, "arg6", 1);
	btf__add_func(btf, "foo", BTF_FUNC_STATIC, 2);

	btf__add_loc_param(btf, 4, BTF_LOC_PARAM_REG);
	btf__add_loc_param_value(btf, 1);
	btf__add_loc_param(btf, 8, BTF_LOC_PARAM_REG |
			    BTF_LOC_PARAM_DEREF | BTF_LOC_PARAM_OFFSET);
	btf__add_loc_param_value(btf, 2);
	btf__add_loc_param_value(btf, 0x10);
	btf__add_loc_param(btf, 8, BTF_LOC_PARAM_REG |
			   BTF_LOC_PARAM_OFFSET | BTF_LOC_PARAM_SIGNED);
	btf__add_loc_param_value(btf, BTF_LOC_PARAM_FBREG);
	btf__add_loc_param_value(btf, -0x10);
	btf__add_loc_param(btf, 8, BTF_LOC_PARAM_ADDR | BTF_LOC_PARAM_CONST);
	btf__add_loc_param_value(btf, 0x9abcdef0);
	btf__add_loc_param_value(btf, 0x12345678);
	btf__add_loc_param(btf, 8, BTF_LOC_PARAM_CONST);
	btf__add_loc_param_value(btf, 0xfeedface);
	btf__add_loc_param_value(btf, 0xdeadbeef);
	btf__add_loc_param(btf, 8, BTF_LOC_PARAM_REG |
			    BTF_LOC_PARAM_DEREF | BTF_LOC_PARAM_OFFSET |
			    BTF_LOC_PARAM_SIGNED);
	btf__add_loc_param_value(btf, BTF_LOC_PARAM_FBREG);
	btf__add_loc_param_value(btf, -0x20);
	btf__add_loc_proto(btf);
	btf__add_loc_proto_param(btf, 4);
	btf__add_loc_proto_param(btf, 5);
	btf__add_loc_proto_param(btf, 6);
	btf__add_loc_proto_param(btf, 7);
	btf__add_loc_proto_param(btf, 8);
	btf__add_loc_proto_param(btf, 9);
	btf__add_locsec(btf, "inline.text");
	btf__add_locsec_loc(btf, 3, 10, 64);

	return btf;
}

static int btf_to_tmpfile(const struct btf *btf, char *path)
{
	ssize_t written;
	const void *raw;
	__u32 sz;
	int fd;

	raw = btf__raw_data(btf, &sz);
	if (!ASSERT_OK_PTR(raw, "raw_data"))
		return -1;

	snprintf(path, PATH_MAX, "/tmp/bpftool_btf_dump.XXXXXX");
	fd = mkstemp(path);
	if (!ASSERT_OK_FD(fd, "mkstemp_btf"))
		return -1;

	written = write(fd, raw, sz);
	close(fd);
	if (!ASSERT_EQ(written, sz, "write_btf")) {
		unlink(path);
		return -1;
	}

	return 0;
}

static char *dump_c(const char *btf_path, bool sorted)
{
	char args[MAX_BPFTOOL_CMD_LEN];
	char *buf;
	int err;

	buf = malloc(DUMP_BUF_SZ);
	if (!ASSERT_OK_PTR(buf, "alloc_dump"))
		return NULL;

	snprintf(args, sizeof(args), "btf dump file %s format c%s",
		 btf_path, sorted ? "" : " unsorted");

	err = get_bpftool_command_output(args, buf, DUMP_BUF_SZ);
	if (!ASSERT_OK(err, "btf_dump_format_c")) {
		free(buf);
		return NULL;
	}

	return buf;
}

static char *dump_raw(const char *btf_path)
{
	char args[MAX_BPFTOOL_CMD_LEN];
	char *buf;
	int err;

	buf = malloc(DUMP_BUF_SZ);
	if (!ASSERT_OK_PTR(buf, "alloc_dump"))
		return NULL;

	snprintf(args, sizeof(args), "btf dump file %s", btf_path);
	err = get_bpftool_command_output(args, buf, DUMP_BUF_SZ);
	if (!ASSERT_OK(err, "btf_dump_raw")) {
		free(buf);
		return NULL;
	}

	return buf;
}

static char *read_expected(const char *path)
{
	char *buf = NULL;
	size_t cap = 0;
	FILE *f;
	int err;

	f = fopen(path, "r");
	if (!f) {
		err = errno;
		PRINT_FAIL("can't open expected output '%s': errno %d\n", path, err);
		return NULL;
	}

	/* no NUL in a generated header, so this reads to the end */
	if (getdelim(&buf, &cap, '\0', f) < 0) {
		err = errno;
		PRINT_FAIL("can't read expected output '%s': errno %d\n", path, err);
		free(buf);
		buf = NULL;
	}

	fclose(f);
	return buf;
}

static void test_dump(const char *btf_path, bool sorted)
{
	const char *exp_path;
	char *dump, *exp;

	exp_path = sorted ? EXPECTED_SORTED : EXPECTED_UNSORTED;

	dump = dump_c(btf_path, sorted);
	if (!dump)
		return;

	exp = read_expected(exp_path);
	if (!exp)
		goto out_dump;

	ASSERT_TEXT_EQ(dump, exp, sorted ? "cmp_sorted" : "cmp_unsorted");

	free(exp);
out_dump:
	free(dump);
}

static void test_loc_dump(const char *btf_path)
{
	const char expected[] =
		"[1] INT 'int' size=4 bits_offset=0 nr_bits=32 encoding=SIGNED\n"
		"[2] FUNC_PROTO '(anon)' ret_type_id=1 vlen=6\n"
		"\t'arg1' type_id=1\n"
		"\t'arg2' type_id=1\n"
		"\t'arg3' type_id=1\n"
		"\t'arg4' type_id=1\n"
		"\t'arg5' type_id=1\n"
		"\t'arg6' type_id=1\n"
		"[3] FUNC 'foo' type_id=2 linkage=static\n"
		"[4] LOC_PARAM '(anon)' size=4 flags=0x8 vlen=1 values='reg1'\n"
		"[5] LOC_PARAM '(anon)' size=8 flags=0x38 vlen=2 values='*(reg2 + 0x10)'\n"
		"[6] LOC_PARAM '(anon)' size=8 flags=0x29 vlen=2 values='fbreg - 0x10'\n"
		"[7] LOC_PARAM '(anon)' size=8 flags=0x6 vlen=2 values='address 0x123456789abcdef0'\n"
		"[8] LOC_PARAM '(anon)' size=8 flags=0x2 vlen=2 values='const 0xdeadbeeffeedface'\n"
		"[9] LOC_PARAM '(anon)' size=8 flags=0x39 vlen=2 values='*(fbreg - 0x20)'\n"
		"[10] LOC_PROTO '(anon)' vlen=6\n"
		"\ttype_id=4 value='reg1'\n"
		"\ttype_id=5 value='*(reg2 + 0x10)'\n"
		"\ttype_id=6 value='fbreg - 0x10'\n"
		"\ttype_id=7 value='address 0x123456789abcdef0'\n"
		"\ttype_id=8 value='const 0xdeadbeeffeedface'\n"
		"\ttype_id=9 value='*(fbreg - 0x20)'\n"
		"[11] LOCSEC 'inline.text' vlen=1\n"
		"\tfunc='foo(arg1 [reg1], arg2 [*(reg2 + 0x10)], arg3 [fbreg - 0x10], arg4 [address 0x123456789abcdef0], arg5 [const 0xdeadbeeffeedface], arg6 [*(fbreg - 0x20)])' func_type_id=3 loc_proto_type_id=10 offset=64\n";
	char *dump;

	dump = dump_raw(btf_path);
	if (!dump)
		return;

	ASSERT_TEXT_EQ(dump, expected, "cmp_loc_dump");
	free(dump);
}

void test_bpftool_btf_dump(void)
{
	char path[PATH_MAX];
	struct btf *btf = NULL;

	btf = mk_btf();
	if (!btf)
		return;

	if (btf_to_tmpfile(btf, path))
		goto out_btf;

	if (test__start_subtest("c_sorted"))
		test_dump(path, true);
	if (test__start_subtest("c_unsorted"))
		test_dump(path, false);
	unlink(path);

	btf__free(btf);

	btf = mk_loc_btf();
	if (!btf)
		return;

	if (btf_to_tmpfile(btf, path))
		goto out_btf;

	if (test__start_subtest("loc_dump"))
		test_loc_dump(path);
	unlink(path);
out_btf:
	btf__free(btf);
}
