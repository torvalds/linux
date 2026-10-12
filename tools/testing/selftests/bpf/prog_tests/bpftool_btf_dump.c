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
	int err;

	exp_path = sorted ? EXPECTED_SORTED : EXPECTED_UNSORTED;

	dump = dump_c(btf_path, sorted);
	if (!dump)
		return;

	exp = read_expected(exp_path);
	if (!exp)
		goto out_dump;

	err = compare_text_to_expected(dump, exp);
	ASSERT_OK(err, sorted ? "cmp_sorted" : "cmp_unsorted");

	free(exp);
out_dump:
	free(dump);
}

void test_bpftool_btf_dump(void)
{
	char path[PATH_MAX];
	struct btf *btf;

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
out_btf:
	btf__free(btf);
}
