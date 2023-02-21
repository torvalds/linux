// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */
#include <stdlib.h>
#include <test_progs.h>
#include <bpf/btf.h>

#define str_has_pfx(str, pfx) \
	(strncmp(str, pfx, __builtin_constant_p(pfx) ? sizeof(pfx) - 1 : strlen(pfx)) == 0)

#define TEST_LOADER_LOG_BUF_SZ 1048576

#define TEST_TAG_EXPECT_FAILURE "comment:test_expect_failure"
#define TEST_TAG_EXPECT_SUCCESS "comment:test_expect_success"
#define TEST_TAG_EXPECT_MSG_PFX "comment:test_expect_msg="
#define TEST_TAG_LOG_LEVEL_PFX "comment:test_log_level="

struct test_spec {
	const char *name;
	bool expect_failure;
	const char *expect_msg;
	int log_level;
};

static int tester_init(struct test_loader *tester)
{
	if (!tester->log_buf) {
		tester->log_buf_sz = TEST_LOADER_LOG_BUF_SZ;
		tester->log_buf = malloc(tester->log_buf_sz);
		if (!ASSERT_OK_PTR(tester->log_buf, "tester_log_buf"))
			return -ENOMEM;
	}

	return 0;
}

void test_loader_fini(struct test_loader *tester)
{
	if (!tester)
		return;

	free(tester->log_buf);
}

static int parse_test_spec(struct test_loader *tester,
			   struct bpf_object *obj,
			   struct bpf_program *prog,
			   struct test_spec *spec)
{
	struct btf *btf;
	int func_id, i;

	memset(spec, 0, sizeof(*spec));

	spec->name = bpf_program__name(prog);

	btf = bpf_object__btf(obj);
	if (!btf) {
		ASSERT_FAIL("BPF object has no BTF");
		return -EINVAL;
	}

	func_id = btf__find_by_name_kind(btf, spec->name, BTF_KIND_FUNC);
	if (func_id < 0) {
		ASSERT_FAIL("failed to find FUNC BTF type for '%s'", spec->name);
		return -EINVAL;
	}

	for (i = 1; i < btf__type_cnt(btf); i++) {
		const struct btf_type *t;
		const char *s;

		t = btf__type_by_id(btf, i);
		if (!btf_is_decl_tag(t))
			continue;

		if (t->type != func_id || btf_decl_tag(t)->component_idx != -1)
			continue;

		s = btf__str_by_offset(btf, t->name_off);
		if (strcmp(s, TEST_TAG_EXPECT_FAILURE) == 0) {
			spec->expect_failure = true;
		} else if (strcmp(s, TEST_TAG_EXPECT_SUCCESS) == 0) {
			spec->expect_failure = false;
		} else if (str_has_pfx(s, TEST_TAG_EXPECT_MSG_PFX)) {
			spec->expect_msg = s + sizeof(TEST_TAG_EXPECT_MSG_PFX) - 1;
		} else if (str_has_pfx(s, TEST_TAG_LOG_LEVEL_PFX)) {
			errno = 0;
			spec->log_level = strtol(s + sizeof(TEST_TAG_LOG_LEVEL_PFX) - 1, NULL, 0);
			if (errno) {
				ASSERT_FAIL("failed to parse test log level from '%s'", s);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static void prepare_case(struct test_loader *tester,
			 struct test_spec *spec,
			 struct bpf_object *obj,
			 struct bpf_program *prog)
{
	int min_log_level = 0;

	if (env.verbosity > VERBOSE_NONE)
		min_log_level = 1;
	if (env.verbosity > VERBOSE_VERY)
		min_log_level = 2;

	bpf_program__set_log_buf(prog, tester->log_buf, tester->log_buf_sz);

	/* Make sure we set at least minimal log level, unless test requirest
	 * even higher level already. Make sure to preserve independent log
	 * level 4 (verifier stats), though.
	 */
	if ((spec->log_level & 3) < min_log_level)
		bpf_program__set_log_level(prog, (spec->log_level & 4) | min_log_level);
	else
		bpf_program__set_log_level(prog, spec->log_level);

	tester->log_buf[0] = '\0';
}

static void emit_verifier_log(const char *log_buf, bool force)
{
	if (!force && env.verbosity == VERBOSE_NONE)
		return;
	fprintf(stdout, "VERIFIER LOG:\n=============\n%s=============\n", log_buf);
}

static void validate_case(struct test_loader *tester,
			  struct test_spec *spec,
			  struct bpf_object *obj,
			  struct bpf_program *prog,
			  int load_err)
{
	if (spec->expect_msg) {
		char *match;

		match = strstr(tester->log_buf, spec->expect_msg);
		if (!ASSERT_OK_PTR(match, "expect_msg")) {
			/* if we are in verbose mode, we've already emitted log */
			if (env.verbosity == VERBOSE_NONE)
				emit_verifier_log(tester->log_buf, true /*force*/);
			fprintf(stderr, "EXPECTED MSG: '%s'\n", spec->expect_msg);
			return;
		}
	}
}

/* this function is forced noinline and has short generic name to look better
 * in test_progs output (in case of a failure)
 */
static noinline
void run_subtest(struct test_loader *tester,
		 const char *skel_name,
		 skel_elf_bytes_fn elf_bytes_factory)
{
	LIBBPF_OPTS(bpf_object_open_opts, open_opts, .object_name = skel_name);
	struct bpf_object *obj = NULL, *tobj;
	struct bpf_program *prog, *tprog;
	const void *obj_bytes;
	size_t obj_byte_cnt;
	int err;

	if (tester_init(tester) < 0)
		return; /* failed to initialize tester */

	obj_bytes = elf_bytes_factory(&obj_byte_cnt);
	obj = bpf_object__open_mem(obj_bytes, obj_byte_cnt, &open_opts);
	if (!ASSERT_OK_PTR(obj, "obj_open_mem"))
		return;

	bpf_object__for_each_program(prog, obj) {
		const char *prog_name = bpf_program__name(prog);
		struct test_spec spec;

		if (!test__start_subtest(prog_name))
			continue;

		/* if we can't derive test specification, go to the next test */
		err = parse_test_spec(tester, obj, prog, &spec);
		if (!ASSERT_OK(err, "parse_test_spec"))
			continue;

		tobj = bpf_object__open_mem(obj_bytes, obj_byte_cnt, &open_opts);
		if (!ASSERT_OK_PTR(tobj, "obj_open_mem")) /* shouldn't happen */
			continue;

		bpf_object__for_each_program(tprog, tobj)
			bpf_program__set_autoload(tprog, false);

		bpf_object__for_each_program(tprog, tobj) {
			/* only load specified program */
			if (strcmp(bpf_program__name(tprog), prog_name) == 0) {
				bpf_program__set_autoload(tprog, true);
				break;
			}
		}

		prepare_case(tester, &spec, tobj, tprog);

		err = bpf_object__load(tobj);
		if (spec.expect_failure) {
			if (!ASSERT_ERR(err, "unexpected_load_success")) {
				emit_verifier_log(tester->log_buf, false /*force*/);
				goto tobj_cleanup;
			}
		} else {
			if (!ASSERT_OK(err, "unexpected_load_failure")) {
				emit_verifier_log(tester->log_buf, true /*force*/);
				goto tobj_cleanup;
			}
		}

		emit_verifier_log(tester->log_buf, false /*force*/);
		validate_case(tester, &spec, tobj, tprog, err);

tobj_cleanup:
		bpf_object__close(tobj);
	}

	bpf_object__close(obj);
}

void test_loader__run_subtests(struct test_loader *tester,
			       const char *skel_name,
			       skel_elf_bytes_fn elf_bytes_factory)
{
	/* see comment in run_subtest() for why we do this function nesting */
	run_subtest(tester, skel_name, elf_bytes_factory);
}
