// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <bpf/btf.h>

static int duration = 0;

void btf_dump_printf(void *ctx, const char *fmt, va_list args)
{
	vfprintf(ctx, fmt, args);
}

static struct btf_dump_test_case {
	const char *name;
	const char *file;
	bool known_ptr_sz;
	struct btf_dump_opts opts;
} btf_dump_test_cases[] = {
	{"btf_dump: syntax", "btf_dump_test_case_syntax", true, {}},
	{"btf_dump: ordering", "btf_dump_test_case_ordering", false, {}},
	{"btf_dump: padding", "btf_dump_test_case_padding", true, {}},
	{"btf_dump: packing", "btf_dump_test_case_packing", true, {}},
	{"btf_dump: bitfields", "btf_dump_test_case_bitfields", true, {}},
	{"btf_dump: multidim", "btf_dump_test_case_multidim", false, {}},
	{"btf_dump: namespacing", "btf_dump_test_case_namespacing", false, {}},
};

static int btf_dump_all_types(const struct btf *btf,
			      const struct btf_dump_opts *opts)
{
	size_t type_cnt = btf__get_nr_types(btf);
	struct btf_dump *d;
	int err = 0, id;

	d = btf_dump__new(btf, NULL, opts, btf_dump_printf);
	if (IS_ERR(d))
		return PTR_ERR(d);

	for (id = 1; id <= type_cnt; id++) {
		err = btf_dump__dump_type(d, id);
		if (err)
			goto done;
	}

done:
	btf_dump__free(d);
	return err;
}

static int test_btf_dump_case(int n, struct btf_dump_test_case *t)
{
	char test_file[256], out_file[256], diff_cmd[1024];
	struct btf *btf = NULL;
	int err = 0, fd = -1;
	FILE *f = NULL;

	snprintf(test_file, sizeof(test_file), "%s.o", t->file);

	btf = btf__parse_elf(test_file, NULL);
	if (CHECK(IS_ERR(btf), "btf_parse_elf",
	    "failed to load test BTF: %ld\n", PTR_ERR(btf))) {
		err = -PTR_ERR(btf);
		btf = NULL;
		goto done;
	}

	/* tests with t->known_ptr_sz have no "long" or "unsigned long" type,
	 * so it's impossible to determine correct pointer size; but if they
	 * do, it should be 8 regardless of host architecture, becaues BPF
	 * target is always 64-bit
	 */
	if (!t->known_ptr_sz) {
		btf__set_pointer_size(btf, 8);
	} else {
		CHECK(btf__pointer_size(btf) != 8, "ptr_sz", "exp %d, got %zu\n",
		      8, btf__pointer_size(btf));
	}

	snprintf(out_file, sizeof(out_file), "/tmp/%s.output.XXXXXX", t->file);
	fd = mkstemp(out_file);
	if (!ASSERT_GE(fd, 0, "create_tmp")) {
		err = fd;
		goto done;
	}
	f = fdopen(fd, "w");
	if (CHECK(f == NULL, "open_tmp",  "failed to open file: %s(%d)\n",
		  strerror(errno), errno)) {
		close(fd);
		goto done;
	}

	t->opts.ctx = f;
	err = btf_dump_all_types(btf, &t->opts);
	fclose(f);
	close(fd);
	if (CHECK(err, "btf_dump", "failure during C dumping: %d\n", err)) {
		goto done;
	}

	snprintf(test_file, sizeof(test_file), "progs/%s.c", t->file);
	if (access(test_file, R_OK) == -1)
		/*
		 * When the test is run with O=, kselftest copies TEST_FILES
		 * without preserving the directory structure.
		 */
		snprintf(test_file, sizeof(test_file), "%s.c", t->file);
	/*
	 * Diff test output and expected test output, contained between
	 * START-EXPECTED-OUTPUT and END-EXPECTED-OUTPUT lines in test case.
	 * For expected output lines, everything before '*' is stripped out.
	 * Also lines containing comment start and comment end markers are
	 * ignored. 
	 */
	snprintf(diff_cmd, sizeof(diff_cmd),
		 "awk '/START-EXPECTED-OUTPUT/{out=1;next} "
		 "/END-EXPECTED-OUTPUT/{out=0} "
		 "/\\/\\*|\\*\\//{next} " /* ignore comment start/end lines */
		 "out {sub(/^[ \\t]*\\*/, \"\"); print}' '%s' | diff -u - '%s'",
		 test_file, out_file);
	err = system(diff_cmd);
	if (CHECK(err, "diff",
		  "differing test output, output=%s, err=%d, diff cmd:\n%s\n",
		  out_file, err, diff_cmd))
		goto done;

	remove(out_file);

done:
	btf__free(btf);
	return err;
}

static char *dump_buf;
static size_t dump_buf_sz;
static FILE *dump_buf_file;

void test_btf_dump_incremental(void)
{
	struct btf *btf = NULL;
	struct btf_dump *d = NULL;
	struct btf_dump_opts opts;
	int id, err, i;

	dump_buf_file = open_memstream(&dump_buf, &dump_buf_sz);
	if (!ASSERT_OK_PTR(dump_buf_file, "dump_memstream"))
		return;
	btf = btf__new_empty();
	if (!ASSERT_OK_PTR(btf, "new_empty"))
		goto err_out;
	opts.ctx = dump_buf_file;
	d = btf_dump__new(btf, NULL, &opts, btf_dump_printf);
	if (!ASSERT_OK(libbpf_get_error(d), "btf_dump__new"))
		goto err_out;

	/* First, generate BTF corresponding to the following C code:
	 *
	 * enum { VAL = 1 };
	 *
	 * struct s { int x; };
	 *
	 */
	id = btf__add_enum(btf, NULL, 4);
	ASSERT_EQ(id, 1, "enum_id");
	err = btf__add_enum_value(btf, "VAL", 1);
	ASSERT_OK(err, "enum_val_ok");

	id = btf__add_int(btf, "int", 4, BTF_INT_SIGNED);
	ASSERT_EQ(id, 2, "int_id");

	id = btf__add_struct(btf, "s", 4);
	ASSERT_EQ(id, 3, "struct_id");
	err = btf__add_field(btf, "x", 2, 0, 0);
	ASSERT_OK(err, "field_ok");

	for (i = 1; i <= btf__get_nr_types(btf); i++) {
		err = btf_dump__dump_type(d, i);
		ASSERT_OK(err, "dump_type_ok");
	}

	fflush(dump_buf_file);
	dump_buf[dump_buf_sz] = 0; /* some libc implementations don't do this */
	ASSERT_STREQ(dump_buf,
"enum {\n"
"	VAL = 1,\n"
"};\n"
"\n"
"struct s {\n"
"	int x;\n"
"};\n\n", "c_dump1");

	/* Now, after dumping original BTF, append another struct that embeds
	 * anonymous enum. It also has a name conflict with the first struct:
	 *
	 * struct s___2 {
	 *     enum { VAL___2 = 1 } x;
	 *     struct s s;
	 * };
	 *
	 * This will test that btf_dump'er maintains internal state properly.
	 * Note that VAL___2 enum value. It's because we've already emitted
	 * that enum as a global anonymous enum, so btf_dump will ensure that
	 * enum values don't conflict;
	 *
	 */
	fseek(dump_buf_file, 0, SEEK_SET);

	id = btf__add_struct(btf, "s", 4);
	ASSERT_EQ(id, 4, "struct_id");
	err = btf__add_field(btf, "x", 1, 0, 0);
	ASSERT_OK(err, "field_ok");
	err = btf__add_field(btf, "s", 3, 32, 0);
	ASSERT_OK(err, "field_ok");

	for (i = 1; i <= btf__get_nr_types(btf); i++) {
		err = btf_dump__dump_type(d, i);
		ASSERT_OK(err, "dump_type_ok");
	}

	fflush(dump_buf_file);
	dump_buf[dump_buf_sz] = 0; /* some libc implementations don't do this */
	ASSERT_STREQ(dump_buf,
"struct s___2 {\n"
"	enum {\n"
"		VAL___2 = 1,\n"
"	} x;\n"
"	struct s s;\n"
"};\n\n" , "c_dump1");

err_out:
	fclose(dump_buf_file);
	free(dump_buf);
	btf_dump__free(d);
	btf__free(btf);
}

void test_btf_dump() {
	int i;

	for (i = 0; i < ARRAY_SIZE(btf_dump_test_cases); i++) {
		struct btf_dump_test_case *t = &btf_dump_test_cases[i];

		if (!test__start_subtest(t->name))
			continue;

		test_btf_dump_case(i, &btf_dump_test_cases[i]);
	}
	if (test__start_subtest("btf_dump: incremental"))
		test_btf_dump_incremental();
}
