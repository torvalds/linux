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
	struct btf_dump_opts opts;
} btf_dump_test_cases[] = {
	{"btf_dump: syntax", "btf_dump_test_case_syntax", {}},
	{"btf_dump: ordering", "btf_dump_test_case_ordering", {}},
	{"btf_dump: padding", "btf_dump_test_case_padding", {}},
	{"btf_dump: packing", "btf_dump_test_case_packing", {}},
	{"btf_dump: bitfields", "btf_dump_test_case_bitfields", {}},
	{"btf_dump: multidim", "btf_dump_test_case_multidim", {}},
	{"btf_dump: namespacing", "btf_dump_test_case_namespacing", {}},
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

	snprintf(out_file, sizeof(out_file), "/tmp/%s.output.XXXXXX", t->file);
	fd = mkstemp(out_file);
	if (CHECK(fd < 0, "create_tmp", "failed to create file: %d\n", fd)) {
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

void test_btf_dump() {
	int i;

	for (i = 0; i < ARRAY_SIZE(btf_dump_test_cases); i++) {
		struct btf_dump_test_case *t = &btf_dump_test_cases[i];

		if (!test__start_subtest(t->name))
			continue;

		test_btf_dump_case(i, &btf_dump_test_cases[i]);
	}
}
