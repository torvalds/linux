#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <linux/err.h>
#include <btf.h>

#define CHECK(condition, format...) ({					\
	int __ret = !!(condition);					\
	if (__ret) {							\
		fprintf(stderr, "%s:%d:FAIL ", __func__, __LINE__);	\
		fprintf(stderr, format);				\
	}								\
	__ret;								\
})

void btf_dump_printf(void *ctx, const char *fmt, va_list args)
{
	vfprintf(ctx, fmt, args);
}

struct btf_dump_test_case {
	const char *name;
	struct btf_dump_opts opts;
} btf_dump_test_cases[] = {
	{.name = "btf_dump_test_case_syntax", .opts = {}},
	{.name = "btf_dump_test_case_ordering", .opts = {}},
	{.name = "btf_dump_test_case_padding", .opts = {}},
	{.name = "btf_dump_test_case_packing", .opts = {}},
	{.name = "btf_dump_test_case_bitfields", .opts = {}},
	{.name = "btf_dump_test_case_multidim", .opts = {}},
	{.name = "btf_dump_test_case_namespacing", .opts = {}},
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

int test_btf_dump_case(int n, struct btf_dump_test_case *test_case)
{
	char test_file[256], out_file[256], diff_cmd[1024];
	struct btf *btf = NULL;
	int err = 0, fd = -1;
	FILE *f = NULL;

	fprintf(stderr, "Test case #%d (%s): ", n, test_case->name);

	snprintf(test_file, sizeof(test_file), "%s.o", test_case->name);

	btf = btf__parse_elf(test_file, NULL);
	if (CHECK(IS_ERR(btf),
	    "failed to load test BTF: %ld\n", PTR_ERR(btf))) {
		err = -PTR_ERR(btf);
		btf = NULL;
		goto done;
	}

	snprintf(out_file, sizeof(out_file),
		 "/tmp/%s.output.XXXXXX", test_case->name);
	fd = mkstemp(out_file);
	if (CHECK(fd < 0, "failed to create temp output file: %d\n", fd)) {
		err = fd;
		goto done;
	}
	f = fdopen(fd, "w");
	if (CHECK(f == NULL, "failed to open temp output file: %s(%d)\n",
		  strerror(errno), errno)) {
		close(fd);
		goto done;
	}

	test_case->opts.ctx = f;
	err = btf_dump_all_types(btf, &test_case->opts);
	fclose(f);
	close(fd);
	if (CHECK(err, "failure during C dumping: %d\n", err)) {
		goto done;
	}

	snprintf(test_file, sizeof(test_file), "progs/%s.c", test_case->name);
	if (access(test_file, R_OK) == -1)
		/*
		 * When the test is run with O=, kselftest copies TEST_FILES
		 * without preserving the directory structure.
		 */
		snprintf(test_file, sizeof(test_file), "%s.c",
			test_case->name);
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
	if (CHECK(err,
		  "differing test output, output=%s, err=%d, diff cmd:\n%s\n",
		  out_file, err, diff_cmd))
		goto done;

	remove(out_file);
	fprintf(stderr, "OK\n");

done:
	btf__free(btf);
	return err;
}

int main() {
	int test_case_cnt, i, err, failed = 0;

	test_case_cnt = sizeof(btf_dump_test_cases) /
			sizeof(btf_dump_test_cases[0]);

	for (i = 0; i < test_case_cnt; i++) {
		err = test_btf_dump_case(i, &btf_dump_test_cases[i]);
		if (err)
			failed++;
	}

	fprintf(stderr, "%d tests succeeded, %d tests failed.\n",
		test_case_cnt - failed, failed);

	return failed;
}
