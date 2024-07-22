// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */
#include <linux/capability.h>
#include <stdlib.h>
#include <regex.h>
#include <test_progs.h>
#include <bpf/btf.h>

#include "autoconf_helper.h"
#include "unpriv_helpers.h"
#include "cap_helpers.h"

#define str_has_pfx(str, pfx) \
	(strncmp(str, pfx, __builtin_constant_p(pfx) ? sizeof(pfx) - 1 : strlen(pfx)) == 0)

#define TEST_LOADER_LOG_BUF_SZ 2097152

#define TEST_TAG_EXPECT_FAILURE "comment:test_expect_failure"
#define TEST_TAG_EXPECT_SUCCESS "comment:test_expect_success"
#define TEST_TAG_EXPECT_MSG_PFX "comment:test_expect_msg="
#define TEST_TAG_EXPECT_REGEX_PFX "comment:test_expect_regex="
#define TEST_TAG_EXPECT_FAILURE_UNPRIV "comment:test_expect_failure_unpriv"
#define TEST_TAG_EXPECT_SUCCESS_UNPRIV "comment:test_expect_success_unpriv"
#define TEST_TAG_EXPECT_MSG_PFX_UNPRIV "comment:test_expect_msg_unpriv="
#define TEST_TAG_EXPECT_REGEX_PFX_UNPRIV "comment:test_expect_regex_unpriv="
#define TEST_TAG_LOG_LEVEL_PFX "comment:test_log_level="
#define TEST_TAG_PROG_FLAGS_PFX "comment:test_prog_flags="
#define TEST_TAG_DESCRIPTION_PFX "comment:test_description="
#define TEST_TAG_RETVAL_PFX "comment:test_retval="
#define TEST_TAG_RETVAL_PFX_UNPRIV "comment:test_retval_unpriv="
#define TEST_TAG_AUXILIARY "comment:test_auxiliary"
#define TEST_TAG_AUXILIARY_UNPRIV "comment:test_auxiliary_unpriv"
#define TEST_BTF_PATH "comment:test_btf_path="

/* Warning: duplicated in bpf_misc.h */
#define POINTER_VALUE	0xcafe4all
#define TEST_DATA_LEN	64

#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
#define EFFICIENT_UNALIGNED_ACCESS 1
#else
#define EFFICIENT_UNALIGNED_ACCESS 0
#endif

static int sysctl_unpriv_disabled = -1;

enum mode {
	PRIV = 1,
	UNPRIV = 2
};

struct expect_msg {
	const char *substr; /* substring match */
	const char *regex_str; /* regex-based match */
	regex_t regex;
};

struct expected_msgs {
	struct expect_msg *patterns;
	size_t cnt;
};

struct test_subspec {
	char *name;
	bool expect_failure;
	struct expected_msgs expect_msgs;
	int retval;
	bool execute;
};

struct test_spec {
	const char *prog_name;
	struct test_subspec priv;
	struct test_subspec unpriv;
	const char *btf_custom_path;
	int log_level;
	int prog_flags;
	int mode_mask;
	bool auxiliary;
	bool valid;
};

static int tester_init(struct test_loader *tester)
{
	if (!tester->log_buf) {
		tester->log_buf_sz = TEST_LOADER_LOG_BUF_SZ;
		tester->log_buf = calloc(tester->log_buf_sz, 1);
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

static void free_msgs(struct expected_msgs *msgs)
{
	int i;

	for (i = 0; i < msgs->cnt; i++)
		if (msgs->patterns[i].regex_str)
			regfree(&msgs->patterns[i].regex);
	free(msgs->patterns);
	msgs->patterns = NULL;
	msgs->cnt = 0;
}

static void free_test_spec(struct test_spec *spec)
{
	/* Deallocate expect_msgs arrays. */
	free_msgs(&spec->priv.expect_msgs);
	free_msgs(&spec->unpriv.expect_msgs);

	free(spec->priv.name);
	free(spec->unpriv.name);
	spec->priv.name = NULL;
	spec->unpriv.name = NULL;
}

static int push_msg(const char *substr, const char *regex_str, struct expected_msgs *msgs)
{
	void *tmp;
	int regcomp_res;
	char error_msg[100];
	struct expect_msg *msg;

	tmp = realloc(msgs->patterns,
		      (1 + msgs->cnt) * sizeof(struct expect_msg));
	if (!tmp) {
		ASSERT_FAIL("failed to realloc memory for messages\n");
		return -ENOMEM;
	}
	msgs->patterns = tmp;
	msg = &msgs->patterns[msgs->cnt];

	if (substr) {
		msg->substr = substr;
		msg->regex_str = NULL;
	} else {
		msg->regex_str = regex_str;
		msg->substr = NULL;
		regcomp_res = regcomp(&msg->regex, regex_str, REG_EXTENDED|REG_NEWLINE);
		if (regcomp_res != 0) {
			regerror(regcomp_res, &msg->regex, error_msg, sizeof(error_msg));
			PRINT_FAIL("Regexp compilation error in '%s': '%s'\n",
				   regex_str, error_msg);
			return -EINVAL;
		}
	}

	msgs->cnt += 1;
	return 0;
}

static int parse_int(const char *str, int *val, const char *name)
{
	char *end;
	long tmp;

	errno = 0;
	if (str_has_pfx(str, "0x"))
		tmp = strtol(str + 2, &end, 16);
	else
		tmp = strtol(str, &end, 10);
	if (errno || end[0] != '\0') {
		PRINT_FAIL("failed to parse %s from '%s'\n", name, str);
		return -EINVAL;
	}
	*val = tmp;
	return 0;
}

static int parse_retval(const char *str, int *val, const char *name)
{
	struct {
		char *name;
		int val;
	} named_values[] = {
		{ "INT_MIN"      , INT_MIN },
		{ "POINTER_VALUE", POINTER_VALUE },
		{ "TEST_DATA_LEN", TEST_DATA_LEN },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(named_values); ++i) {
		if (strcmp(str, named_values[i].name) != 0)
			continue;
		*val = named_values[i].val;
		return 0;
	}

	return parse_int(str, val, name);
}

static void update_flags(int *flags, int flag, bool clear)
{
	if (clear)
		*flags &= ~flag;
	else
		*flags |= flag;
}

/* Uses btf_decl_tag attributes to describe the expected test
 * behavior, see bpf_misc.h for detailed description of each attribute
 * and attribute combinations.
 */
static int parse_test_spec(struct test_loader *tester,
			   struct bpf_object *obj,
			   struct bpf_program *prog,
			   struct test_spec *spec)
{
	const char *description = NULL;
	bool has_unpriv_result = false;
	bool has_unpriv_retval = false;
	int func_id, i, err = 0;
	struct btf *btf;

	memset(spec, 0, sizeof(*spec));

	spec->prog_name = bpf_program__name(prog);
	spec->prog_flags = testing_prog_flags();

	btf = bpf_object__btf(obj);
	if (!btf) {
		ASSERT_FAIL("BPF object has no BTF");
		return -EINVAL;
	}

	func_id = btf__find_by_name_kind(btf, spec->prog_name, BTF_KIND_FUNC);
	if (func_id < 0) {
		ASSERT_FAIL("failed to find FUNC BTF type for '%s'", spec->prog_name);
		return -EINVAL;
	}

	for (i = 1; i < btf__type_cnt(btf); i++) {
		const char *s, *val, *msg;
		const struct btf_type *t;
		bool clear;
		int flags;

		t = btf__type_by_id(btf, i);
		if (!btf_is_decl_tag(t))
			continue;

		if (t->type != func_id || btf_decl_tag(t)->component_idx != -1)
			continue;

		s = btf__str_by_offset(btf, t->name_off);
		if (str_has_pfx(s, TEST_TAG_DESCRIPTION_PFX)) {
			description = s + sizeof(TEST_TAG_DESCRIPTION_PFX) - 1;
		} else if (strcmp(s, TEST_TAG_EXPECT_FAILURE) == 0) {
			spec->priv.expect_failure = true;
			spec->mode_mask |= PRIV;
		} else if (strcmp(s, TEST_TAG_EXPECT_SUCCESS) == 0) {
			spec->priv.expect_failure = false;
			spec->mode_mask |= PRIV;
		} else if (strcmp(s, TEST_TAG_EXPECT_FAILURE_UNPRIV) == 0) {
			spec->unpriv.expect_failure = true;
			spec->mode_mask |= UNPRIV;
			has_unpriv_result = true;
		} else if (strcmp(s, TEST_TAG_EXPECT_SUCCESS_UNPRIV) == 0) {
			spec->unpriv.expect_failure = false;
			spec->mode_mask |= UNPRIV;
			has_unpriv_result = true;
		} else if (strcmp(s, TEST_TAG_AUXILIARY) == 0) {
			spec->auxiliary = true;
			spec->mode_mask |= PRIV;
		} else if (strcmp(s, TEST_TAG_AUXILIARY_UNPRIV) == 0) {
			spec->auxiliary = true;
			spec->mode_mask |= UNPRIV;
		} else if (str_has_pfx(s, TEST_TAG_EXPECT_MSG_PFX)) {
			msg = s + sizeof(TEST_TAG_EXPECT_MSG_PFX) - 1;
			err = push_msg(msg, NULL, &spec->priv.expect_msgs);
			if (err)
				goto cleanup;
			spec->mode_mask |= PRIV;
		} else if (str_has_pfx(s, TEST_TAG_EXPECT_MSG_PFX_UNPRIV)) {
			msg = s + sizeof(TEST_TAG_EXPECT_MSG_PFX_UNPRIV) - 1;
			err = push_msg(msg, NULL, &spec->unpriv.expect_msgs);
			if (err)
				goto cleanup;
			spec->mode_mask |= UNPRIV;
		} else if (str_has_pfx(s, TEST_TAG_EXPECT_REGEX_PFX)) {
			msg = s + sizeof(TEST_TAG_EXPECT_REGEX_PFX) - 1;
			err = push_msg(NULL, msg, &spec->priv.expect_msgs);
			if (err)
				goto cleanup;
			spec->mode_mask |= PRIV;
		} else if (str_has_pfx(s, TEST_TAG_EXPECT_REGEX_PFX_UNPRIV)) {
			msg = s + sizeof(TEST_TAG_EXPECT_REGEX_PFX_UNPRIV) - 1;
			err = push_msg(NULL, msg, &spec->unpriv.expect_msgs);
			if (err)
				goto cleanup;
			spec->mode_mask |= UNPRIV;
		} else if (str_has_pfx(s, TEST_TAG_RETVAL_PFX)) {
			val = s + sizeof(TEST_TAG_RETVAL_PFX) - 1;
			err = parse_retval(val, &spec->priv.retval, "__retval");
			if (err)
				goto cleanup;
			spec->priv.execute = true;
			spec->mode_mask |= PRIV;
		} else if (str_has_pfx(s, TEST_TAG_RETVAL_PFX_UNPRIV)) {
			val = s + sizeof(TEST_TAG_RETVAL_PFX_UNPRIV) - 1;
			err = parse_retval(val, &spec->unpriv.retval, "__retval_unpriv");
			if (err)
				goto cleanup;
			spec->mode_mask |= UNPRIV;
			spec->unpriv.execute = true;
			has_unpriv_retval = true;
		} else if (str_has_pfx(s, TEST_TAG_LOG_LEVEL_PFX)) {
			val = s + sizeof(TEST_TAG_LOG_LEVEL_PFX) - 1;
			err = parse_int(val, &spec->log_level, "test log level");
			if (err)
				goto cleanup;
		} else if (str_has_pfx(s, TEST_TAG_PROG_FLAGS_PFX)) {
			val = s + sizeof(TEST_TAG_PROG_FLAGS_PFX) - 1;

			clear = val[0] == '!';
			if (clear)
				val++;

			if (strcmp(val, "BPF_F_STRICT_ALIGNMENT") == 0) {
				update_flags(&spec->prog_flags, BPF_F_STRICT_ALIGNMENT, clear);
			} else if (strcmp(val, "BPF_F_ANY_ALIGNMENT") == 0) {
				update_flags(&spec->prog_flags, BPF_F_ANY_ALIGNMENT, clear);
			} else if (strcmp(val, "BPF_F_TEST_RND_HI32") == 0) {
				update_flags(&spec->prog_flags, BPF_F_TEST_RND_HI32, clear);
			} else if (strcmp(val, "BPF_F_TEST_STATE_FREQ") == 0) {
				update_flags(&spec->prog_flags, BPF_F_TEST_STATE_FREQ, clear);
			} else if (strcmp(val, "BPF_F_SLEEPABLE") == 0) {
				update_flags(&spec->prog_flags, BPF_F_SLEEPABLE, clear);
			} else if (strcmp(val, "BPF_F_XDP_HAS_FRAGS") == 0) {
				update_flags(&spec->prog_flags, BPF_F_XDP_HAS_FRAGS, clear);
			} else if (strcmp(val, "BPF_F_TEST_REG_INVARIANTS") == 0) {
				update_flags(&spec->prog_flags, BPF_F_TEST_REG_INVARIANTS, clear);
			} else /* assume numeric value */ {
				err = parse_int(val, &flags, "test prog flags");
				if (err)
					goto cleanup;
				update_flags(&spec->prog_flags, flags, clear);
			}
		} else if (str_has_pfx(s, TEST_BTF_PATH)) {
			spec->btf_custom_path = s + sizeof(TEST_BTF_PATH) - 1;
		}
	}

	if (spec->mode_mask == 0)
		spec->mode_mask = PRIV;

	if (!description)
		description = spec->prog_name;

	if (spec->mode_mask & PRIV) {
		spec->priv.name = strdup(description);
		if (!spec->priv.name) {
			PRINT_FAIL("failed to allocate memory for priv.name\n");
			err = -ENOMEM;
			goto cleanup;
		}
	}

	if (spec->mode_mask & UNPRIV) {
		int descr_len = strlen(description);
		const char *suffix = " @unpriv";
		char *name;

		name = malloc(descr_len + strlen(suffix) + 1);
		if (!name) {
			PRINT_FAIL("failed to allocate memory for unpriv.name\n");
			err = -ENOMEM;
			goto cleanup;
		}

		strcpy(name, description);
		strcpy(&name[descr_len], suffix);
		spec->unpriv.name = name;
	}

	if (spec->mode_mask & (PRIV | UNPRIV)) {
		if (!has_unpriv_result)
			spec->unpriv.expect_failure = spec->priv.expect_failure;

		if (!has_unpriv_retval) {
			spec->unpriv.retval = spec->priv.retval;
			spec->unpriv.execute = spec->priv.execute;
		}

		if (spec->unpriv.expect_msgs.cnt == 0) {
			for (i = 0; i < spec->priv.expect_msgs.cnt; i++) {
				struct expect_msg *msg = &spec->priv.expect_msgs.patterns[i];

				err = push_msg(msg->substr, msg->regex_str,
					       &spec->unpriv.expect_msgs);
				if (err)
					goto cleanup;
			}
		}
	}

	spec->valid = true;

	return 0;

cleanup:
	free_test_spec(spec);
	return err;
}

static void prepare_case(struct test_loader *tester,
			 struct test_spec *spec,
			 struct bpf_object *obj,
			 struct bpf_program *prog)
{
	int min_log_level = 0, prog_flags;

	if (env.verbosity > VERBOSE_NONE)
		min_log_level = 1;
	if (env.verbosity > VERBOSE_VERY)
		min_log_level = 2;

	bpf_program__set_log_buf(prog, tester->log_buf, tester->log_buf_sz);

	/* Make sure we set at least minimal log level, unless test requires
	 * even higher level already. Make sure to preserve independent log
	 * level 4 (verifier stats), though.
	 */
	if ((spec->log_level & 3) < min_log_level)
		bpf_program__set_log_level(prog, (spec->log_level & 4) | min_log_level);
	else
		bpf_program__set_log_level(prog, spec->log_level);

	prog_flags = bpf_program__flags(prog);
	bpf_program__set_flags(prog, prog_flags | spec->prog_flags);

	tester->log_buf[0] = '\0';
}

static void emit_verifier_log(const char *log_buf, bool force)
{
	if (!force && env.verbosity == VERBOSE_NONE)
		return;
	fprintf(stdout, "VERIFIER LOG:\n=============\n%s=============\n", log_buf);
}

static void validate_msgs(char *log_buf, struct expected_msgs *msgs)
{
	regmatch_t reg_match[1];
	const char *log = log_buf;
	int i, j, err;

	for (i = 0; i < msgs->cnt; i++) {
		struct expect_msg *msg = &msgs->patterns[i];
		const char *match = NULL;

		if (msg->substr) {
			match = strstr(log, msg->substr);
			if (match)
				log += strlen(msg->substr);
		} else {
			err = regexec(&msg->regex, log, 1, reg_match, 0);
			if (err == 0) {
				match = log + reg_match[0].rm_so;
				log += reg_match[0].rm_eo;
			}
		}

		if (!ASSERT_OK_PTR(match, "expect_msg")) {
			if (env.verbosity == VERBOSE_NONE)
				emit_verifier_log(log_buf, true /*force*/);
			for (j = 0; j <= i; j++) {
				msg = &msgs->patterns[j];
				fprintf(stderr, "%s %s: '%s'\n",
					j < i ? "MATCHED " : "EXPECTED",
					msg->substr ? "SUBSTR" : " REGEX",
					msg->substr ?: msg->regex_str);
			}
			return;
		}
	}
}

struct cap_state {
	__u64 old_caps;
	bool initialized;
};

static int drop_capabilities(struct cap_state *caps)
{
	const __u64 caps_to_drop = (1ULL << CAP_SYS_ADMIN | 1ULL << CAP_NET_ADMIN |
				    1ULL << CAP_PERFMON   | 1ULL << CAP_BPF);
	int err;

	err = cap_disable_effective(caps_to_drop, &caps->old_caps);
	if (err) {
		PRINT_FAIL("failed to drop capabilities: %i, %s\n", err, strerror(err));
		return err;
	}

	caps->initialized = true;
	return 0;
}

static int restore_capabilities(struct cap_state *caps)
{
	int err;

	if (!caps->initialized)
		return 0;

	err = cap_enable_effective(caps->old_caps, NULL);
	if (err)
		PRINT_FAIL("failed to restore capabilities: %i, %s\n", err, strerror(err));
	caps->initialized = false;
	return err;
}

static bool can_execute_unpriv(struct test_loader *tester, struct test_spec *spec)
{
	if (sysctl_unpriv_disabled < 0)
		sysctl_unpriv_disabled = get_unpriv_disabled() ? 1 : 0;
	if (sysctl_unpriv_disabled)
		return false;
	if ((spec->prog_flags & BPF_F_ANY_ALIGNMENT) && !EFFICIENT_UNALIGNED_ACCESS)
		return false;
	return true;
}

static bool is_unpriv_capable_map(struct bpf_map *map)
{
	enum bpf_map_type type;
	__u32 flags;

	type = bpf_map__type(map);

	switch (type) {
	case BPF_MAP_TYPE_HASH:
	case BPF_MAP_TYPE_PERCPU_HASH:
	case BPF_MAP_TYPE_HASH_OF_MAPS:
		flags = bpf_map__map_flags(map);
		return !(flags & BPF_F_ZERO_SEED);
	case BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE:
	case BPF_MAP_TYPE_ARRAY:
	case BPF_MAP_TYPE_RINGBUF:
	case BPF_MAP_TYPE_PROG_ARRAY:
	case BPF_MAP_TYPE_CGROUP_ARRAY:
	case BPF_MAP_TYPE_PERCPU_ARRAY:
	case BPF_MAP_TYPE_USER_RINGBUF:
	case BPF_MAP_TYPE_ARRAY_OF_MAPS:
	case BPF_MAP_TYPE_CGROUP_STORAGE:
	case BPF_MAP_TYPE_PERF_EVENT_ARRAY:
		return true;
	default:
		return false;
	}
}

static int do_prog_test_run(int fd_prog, int *retval, bool empty_opts)
{
	__u8 tmp_out[TEST_DATA_LEN << 2] = {};
	__u8 tmp_in[TEST_DATA_LEN] = {};
	int err, saved_errno;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = tmp_in,
		.data_size_in = sizeof(tmp_in),
		.data_out = tmp_out,
		.data_size_out = sizeof(tmp_out),
		.repeat = 1,
	);

	if (empty_opts) {
		memset(&topts, 0, sizeof(struct bpf_test_run_opts));
		topts.sz = sizeof(struct bpf_test_run_opts);
	}
	err = bpf_prog_test_run_opts(fd_prog, &topts);
	saved_errno = errno;

	if (err) {
		PRINT_FAIL("FAIL: Unexpected bpf_prog_test_run error: %d (%s) ",
			   saved_errno, strerror(saved_errno));
		return err;
	}

	ASSERT_OK(0, "bpf_prog_test_run");
	*retval = topts.retval;

	return 0;
}

static bool should_do_test_run(struct test_spec *spec, struct test_subspec *subspec)
{
	if (!subspec->execute)
		return false;

	if (subspec->expect_failure)
		return false;

	if ((spec->prog_flags & BPF_F_ANY_ALIGNMENT) && !EFFICIENT_UNALIGNED_ACCESS) {
		if (env.verbosity != VERBOSE_NONE)
			printf("alignment prevents execution\n");
		return false;
	}

	return true;
}

/* this function is forced noinline and has short generic name to look better
 * in test_progs output (in case of a failure)
 */
static noinline
void run_subtest(struct test_loader *tester,
		 struct bpf_object_open_opts *open_opts,
		 const void *obj_bytes,
		 size_t obj_byte_cnt,
		 struct test_spec *specs,
		 struct test_spec *spec,
		 bool unpriv)
{
	struct test_subspec *subspec = unpriv ? &spec->unpriv : &spec->priv;
	struct bpf_program *tprog = NULL, *tprog_iter;
	struct test_spec *spec_iter;
	struct cap_state caps = {};
	struct bpf_object *tobj;
	struct bpf_map *map;
	int retval, err, i;
	bool should_load;

	if (!test__start_subtest(subspec->name))
		return;

	if (unpriv) {
		if (!can_execute_unpriv(tester, spec)) {
			test__skip();
			test__end_subtest();
			return;
		}
		if (drop_capabilities(&caps)) {
			test__end_subtest();
			return;
		}
	}

	/* Implicitly reset to NULL if next test case doesn't specify */
	open_opts->btf_custom_path = spec->btf_custom_path;

	tobj = bpf_object__open_mem(obj_bytes, obj_byte_cnt, open_opts);
	if (!ASSERT_OK_PTR(tobj, "obj_open_mem")) /* shouldn't happen */
		goto subtest_cleanup;

	i = 0;
	bpf_object__for_each_program(tprog_iter, tobj) {
		spec_iter = &specs[i++];
		should_load = false;

		if (spec_iter->valid) {
			if (strcmp(bpf_program__name(tprog_iter), spec->prog_name) == 0) {
				tprog = tprog_iter;
				should_load = true;
			}

			if (spec_iter->auxiliary &&
			    spec_iter->mode_mask & (unpriv ? UNPRIV : PRIV))
				should_load = true;
		}

		bpf_program__set_autoload(tprog_iter, should_load);
	}

	prepare_case(tester, spec, tobj, tprog);

	/* By default bpf_object__load() automatically creates all
	 * maps declared in the skeleton. Some map types are only
	 * allowed in priv mode. Disable autoload for such maps in
	 * unpriv mode.
	 */
	bpf_object__for_each_map(map, tobj)
		bpf_map__set_autocreate(map, !unpriv || is_unpriv_capable_map(map));

	err = bpf_object__load(tobj);
	if (subspec->expect_failure) {
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
	validate_msgs(tester->log_buf, &subspec->expect_msgs);

	if (should_do_test_run(spec, subspec)) {
		/* For some reason test_verifier executes programs
		 * with all capabilities restored. Do the same here.
		 */
		if (restore_capabilities(&caps))
			goto tobj_cleanup;

		if (tester->pre_execution_cb) {
			err = tester->pre_execution_cb(tobj);
			if (err) {
				PRINT_FAIL("pre_execution_cb failed: %d\n", err);
				goto tobj_cleanup;
			}
		}

		do_prog_test_run(bpf_program__fd(tprog), &retval,
				 bpf_program__type(tprog) == BPF_PROG_TYPE_SYSCALL ? true : false);
		if (retval != subspec->retval && subspec->retval != POINTER_VALUE) {
			PRINT_FAIL("Unexpected retval: %d != %d\n", retval, subspec->retval);
			goto tobj_cleanup;
		}
	}

tobj_cleanup:
	bpf_object__close(tobj);
subtest_cleanup:
	test__end_subtest();
	restore_capabilities(&caps);
}

static void process_subtest(struct test_loader *tester,
			    const char *skel_name,
			    skel_elf_bytes_fn elf_bytes_factory)
{
	LIBBPF_OPTS(bpf_object_open_opts, open_opts, .object_name = skel_name);
	struct test_spec *specs = NULL;
	struct bpf_object *obj = NULL;
	struct bpf_program *prog;
	const void *obj_bytes;
	int err, i, nr_progs;
	size_t obj_byte_cnt;

	if (tester_init(tester) < 0)
		return; /* failed to initialize tester */

	obj_bytes = elf_bytes_factory(&obj_byte_cnt);
	obj = bpf_object__open_mem(obj_bytes, obj_byte_cnt, &open_opts);
	if (!ASSERT_OK_PTR(obj, "obj_open_mem"))
		return;

	nr_progs = 0;
	bpf_object__for_each_program(prog, obj)
		++nr_progs;

	specs = calloc(nr_progs, sizeof(struct test_spec));
	if (!ASSERT_OK_PTR(specs, "specs_alloc"))
		return;

	i = 0;
	bpf_object__for_each_program(prog, obj) {
		/* ignore tests for which  we can't derive test specification */
		err = parse_test_spec(tester, obj, prog, &specs[i++]);
		if (err)
			PRINT_FAIL("Can't parse test spec for program '%s'\n",
				   bpf_program__name(prog));
	}

	i = 0;
	bpf_object__for_each_program(prog, obj) {
		struct test_spec *spec = &specs[i++];

		if (!spec->valid || spec->auxiliary)
			continue;

		if (spec->mode_mask & PRIV)
			run_subtest(tester, &open_opts, obj_bytes, obj_byte_cnt,
				    specs, spec, false);
		if (spec->mode_mask & UNPRIV)
			run_subtest(tester, &open_opts, obj_bytes, obj_byte_cnt,
				    specs, spec, true);

	}

	for (i = 0; i < nr_progs; ++i)
		free_test_spec(&specs[i]);
	free(specs);
	bpf_object__close(obj);
}

void test_loader__run_subtests(struct test_loader *tester,
			       const char *skel_name,
			       skel_elf_bytes_fn elf_bytes_factory)
{
	/* see comment in run_subtest() for why we do this function nesting */
	process_subtest(tester, skel_name, elf_bytes_factory);
}
