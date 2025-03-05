// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */
#include <linux/capability.h>
#include <stdlib.h>
#include <regex.h>
#include <test_progs.h>
#include <bpf/btf.h>

#include "autoconf_helper.h"
#include "disasm_helpers.h"
#include "unpriv_helpers.h"
#include "cap_helpers.h"
#include "jit_disasm_helpers.h"

#define str_has_pfx(str, pfx) \
	(strncmp(str, pfx, __builtin_constant_p(pfx) ? sizeof(pfx) - 1 : strlen(pfx)) == 0)

#define TEST_LOADER_LOG_BUF_SZ 2097152

#define TEST_TAG_EXPECT_FAILURE "comment:test_expect_failure"
#define TEST_TAG_EXPECT_SUCCESS "comment:test_expect_success"
#define TEST_TAG_EXPECT_MSG_PFX "comment:test_expect_msg="
#define TEST_TAG_EXPECT_XLATED_PFX "comment:test_expect_xlated="
#define TEST_TAG_EXPECT_FAILURE_UNPRIV "comment:test_expect_failure_unpriv"
#define TEST_TAG_EXPECT_SUCCESS_UNPRIV "comment:test_expect_success_unpriv"
#define TEST_TAG_EXPECT_MSG_PFX_UNPRIV "comment:test_expect_msg_unpriv="
#define TEST_TAG_EXPECT_XLATED_PFX_UNPRIV "comment:test_expect_xlated_unpriv="
#define TEST_TAG_LOG_LEVEL_PFX "comment:test_log_level="
#define TEST_TAG_PROG_FLAGS_PFX "comment:test_prog_flags="
#define TEST_TAG_DESCRIPTION_PFX "comment:test_description="
#define TEST_TAG_RETVAL_PFX "comment:test_retval="
#define TEST_TAG_RETVAL_PFX_UNPRIV "comment:test_retval_unpriv="
#define TEST_TAG_AUXILIARY "comment:test_auxiliary"
#define TEST_TAG_AUXILIARY_UNPRIV "comment:test_auxiliary_unpriv"
#define TEST_BTF_PATH "comment:test_btf_path="
#define TEST_TAG_ARCH "comment:test_arch="
#define TEST_TAG_JITED_PFX "comment:test_jited="
#define TEST_TAG_JITED_PFX_UNPRIV "comment:test_jited_unpriv="
#define TEST_TAG_CAPS_UNPRIV "comment:test_caps_unpriv="
#define TEST_TAG_LOAD_MODE_PFX "comment:load_mode="

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

enum load_mode {
	JITED		= 1 << 0,
	NO_JITED	= 1 << 1,
};

struct expect_msg {
	const char *substr; /* substring match */
	regex_t regex;
	bool is_regex;
	bool on_next_line;
};

struct expected_msgs {
	struct expect_msg *patterns;
	size_t cnt;
};

struct test_subspec {
	char *name;
	bool expect_failure;
	struct expected_msgs expect_msgs;
	struct expected_msgs expect_xlated;
	struct expected_msgs jited;
	int retval;
	bool execute;
	__u64 caps;
};

struct test_spec {
	const char *prog_name;
	struct test_subspec priv;
	struct test_subspec unpriv;
	const char *btf_custom_path;
	int log_level;
	int prog_flags;
	int mode_mask;
	int arch_mask;
	int load_mask;
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
		if (msgs->patterns[i].is_regex)
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
	free_msgs(&spec->priv.expect_xlated);
	free_msgs(&spec->unpriv.expect_xlated);
	free_msgs(&spec->priv.jited);
	free_msgs(&spec->unpriv.jited);

	free(spec->priv.name);
	free(spec->unpriv.name);
	spec->priv.name = NULL;
	spec->unpriv.name = NULL;
}

/* Compiles regular expression matching pattern.
 * Pattern has a special syntax:
 *
 *   pattern := (<verbatim text> | regex)*
 *   regex := "{{" <posix extended regular expression> "}}"
 *
 * In other words, pattern is a verbatim text with inclusion
 * of regular expressions enclosed in "{{" "}}" pairs.
 * For example, pattern "foo{{[0-9]+}}" matches strings like
 * "foo0", "foo007", etc.
 */
static int compile_regex(const char *pattern, regex_t *regex)
{
	char err_buf[256], buf[256] = {}, *ptr, *buf_end;
	const char *original_pattern = pattern;
	bool in_regex = false;
	int err;

	buf_end = buf + sizeof(buf);
	ptr = buf;
	while (*pattern && ptr < buf_end - 2) {
		if (!in_regex && str_has_pfx(pattern, "{{")) {
			in_regex = true;
			pattern += 2;
			continue;
		}
		if (in_regex && str_has_pfx(pattern, "}}")) {
			in_regex = false;
			pattern += 2;
			continue;
		}
		if (in_regex) {
			*ptr++ = *pattern++;
			continue;
		}
		/* list of characters that need escaping for extended posix regex */
		if (strchr(".[]\\()*+?{}|^$", *pattern)) {
			*ptr++ = '\\';
			*ptr++ = *pattern++;
			continue;
		}
		*ptr++ = *pattern++;
	}
	if (*pattern) {
		PRINT_FAIL("Regexp too long: '%s'\n", original_pattern);
		return -EINVAL;
	}
	if (in_regex) {
		PRINT_FAIL("Regexp has open '{{' but no closing '}}': '%s'\n", original_pattern);
		return -EINVAL;
	}
	err = regcomp(regex, buf, REG_EXTENDED | REG_NEWLINE);
	if (err != 0) {
		regerror(err, regex, err_buf, sizeof(err_buf));
		PRINT_FAIL("Regexp compilation error in '%s': '%s'\n", buf, err_buf);
		return -EINVAL;
	}
	return 0;
}

static int __push_msg(const char *pattern, bool on_next_line, struct expected_msgs *msgs)
{
	struct expect_msg *msg;
	void *tmp;
	int err;

	tmp = realloc(msgs->patterns,
		      (1 + msgs->cnt) * sizeof(struct expect_msg));
	if (!tmp) {
		ASSERT_FAIL("failed to realloc memory for messages\n");
		return -ENOMEM;
	}
	msgs->patterns = tmp;
	msg = &msgs->patterns[msgs->cnt];
	msg->on_next_line = on_next_line;
	msg->substr = pattern;
	msg->is_regex = false;
	if (strstr(pattern, "{{")) {
		err = compile_regex(pattern, &msg->regex);
		if (err)
			return err;
		msg->is_regex = true;
	}
	msgs->cnt += 1;
	return 0;
}

static int clone_msgs(struct expected_msgs *from, struct expected_msgs *to)
{
	struct expect_msg *msg;
	int i, err;

	for (i = 0; i < from->cnt; i++) {
		msg = &from->patterns[i];
		err = __push_msg(msg->substr, msg->on_next_line, to);
		if (err)
			return err;
	}
	return 0;
}

static int push_msg(const char *substr, struct expected_msgs *msgs)
{
	return __push_msg(substr, false, msgs);
}

static int push_disasm_msg(const char *regex_str, bool *on_next_line, struct expected_msgs *msgs)
{
	int err;

	if (strcmp(regex_str, "...") == 0) {
		*on_next_line = false;
		return 0;
	}
	err = __push_msg(regex_str, *on_next_line, msgs);
	if (err)
		return err;
	*on_next_line = true;
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

static int parse_caps(const char *str, __u64 *val, const char *name)
{
	int cap_flag = 0;
	char *token = NULL, *saveptr = NULL;

	char *str_cpy = strdup(str);
	if (str_cpy == NULL) {
		PRINT_FAIL("Memory allocation failed\n");
		return -EINVAL;
	}

	token = strtok_r(str_cpy, "|", &saveptr);
	while (token != NULL) {
		errno = 0;
		if (!strncmp("CAP_", token, sizeof("CAP_") - 1)) {
			PRINT_FAIL("define %s constant in bpf_misc.h, failed to parse caps\n", token);
			return -EINVAL;
		}
		cap_flag = strtol(token, NULL, 10);
		if (!cap_flag || errno) {
			PRINT_FAIL("failed to parse caps %s\n", name);
			return -EINVAL;
		}
		*val |= (1ULL << cap_flag);
		token = strtok_r(NULL, "|", &saveptr);
	}

	free(str_cpy);
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

/* Matches a string of form '<pfx>[^=]=.*' and returns it's suffix.
 * Used to parse btf_decl_tag values.
 * Such values require unique prefix because compiler does not add
 * same __attribute__((btf_decl_tag(...))) twice.
 * Test suite uses two-component tags for such cases:
 *
 *   <pfx> __COUNTER__ '='
 *
 * For example, two consecutive __msg tags '__msg("foo") __msg("foo")'
 * would be encoded as:
 *
 *   [18] DECL_TAG 'comment:test_expect_msg=0=foo' type_id=15 component_idx=-1
 *   [19] DECL_TAG 'comment:test_expect_msg=1=foo' type_id=15 component_idx=-1
 *
 * And the purpose of this function is to extract 'foo' from the above.
 */
static const char *skip_dynamic_pfx(const char *s, const char *pfx)
{
	const char *msg;

	if (strncmp(s, pfx, strlen(pfx)) != 0)
		return NULL;
	msg = s + strlen(pfx);
	msg = strchr(msg, '=');
	if (!msg)
		return NULL;
	return msg + 1;
}

enum arch {
	ARCH_UNKNOWN	= 0x1,
	ARCH_X86_64	= 0x2,
	ARCH_ARM64	= 0x4,
	ARCH_RISCV64	= 0x8,
};

static int get_current_arch(void)
{
#if defined(__x86_64__)
	return ARCH_X86_64;
#elif defined(__aarch64__)
	return ARCH_ARM64;
#elif defined(__riscv) && __riscv_xlen == 64
	return ARCH_RISCV64;
#endif
	return ARCH_UNKNOWN;
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
	bool unpriv_xlated_on_next_line = true;
	bool xlated_on_next_line = true;
	bool unpriv_jit_on_next_line;
	bool jit_on_next_line;
	bool collect_jit = false;
	int func_id, i, err = 0;
	u32 arch_mask = 0;
	u32 load_mask = 0;
	struct btf *btf;
	enum arch arch;

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
		} else if ((msg = skip_dynamic_pfx(s, TEST_TAG_EXPECT_MSG_PFX))) {
			err = push_msg(msg, &spec->priv.expect_msgs);
			if (err)
				goto cleanup;
			spec->mode_mask |= PRIV;
		} else if ((msg = skip_dynamic_pfx(s, TEST_TAG_EXPECT_MSG_PFX_UNPRIV))) {
			err = push_msg(msg, &spec->unpriv.expect_msgs);
			if (err)
				goto cleanup;
			spec->mode_mask |= UNPRIV;
		} else if ((msg = skip_dynamic_pfx(s, TEST_TAG_JITED_PFX))) {
			if (arch_mask == 0) {
				PRINT_FAIL("__jited used before __arch_*");
				goto cleanup;
			}
			if (collect_jit) {
				err = push_disasm_msg(msg, &jit_on_next_line,
						      &spec->priv.jited);
				if (err)
					goto cleanup;
				spec->mode_mask |= PRIV;
			}
		} else if ((msg = skip_dynamic_pfx(s, TEST_TAG_JITED_PFX_UNPRIV))) {
			if (arch_mask == 0) {
				PRINT_FAIL("__unpriv_jited used before __arch_*");
				goto cleanup;
			}
			if (collect_jit) {
				err = push_disasm_msg(msg, &unpriv_jit_on_next_line,
						      &spec->unpriv.jited);
				if (err)
					goto cleanup;
				spec->mode_mask |= UNPRIV;
			}
		} else if ((msg = skip_dynamic_pfx(s, TEST_TAG_EXPECT_XLATED_PFX))) {
			err = push_disasm_msg(msg, &xlated_on_next_line,
					      &spec->priv.expect_xlated);
			if (err)
				goto cleanup;
			spec->mode_mask |= PRIV;
		} else if ((msg = skip_dynamic_pfx(s, TEST_TAG_EXPECT_XLATED_PFX_UNPRIV))) {
			err = push_disasm_msg(msg, &unpriv_xlated_on_next_line,
					      &spec->unpriv.expect_xlated);
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
		} else if (str_has_pfx(s, TEST_TAG_ARCH)) {
			val = s + sizeof(TEST_TAG_ARCH) - 1;
			if (strcmp(val, "X86_64") == 0) {
				arch = ARCH_X86_64;
			} else if (strcmp(val, "ARM64") == 0) {
				arch = ARCH_ARM64;
			} else if (strcmp(val, "RISCV64") == 0) {
				arch = ARCH_RISCV64;
			} else {
				PRINT_FAIL("bad arch spec: '%s'", val);
				err = -EINVAL;
				goto cleanup;
			}
			arch_mask |= arch;
			collect_jit = get_current_arch() == arch;
			unpriv_jit_on_next_line = true;
			jit_on_next_line = true;
		} else if (str_has_pfx(s, TEST_BTF_PATH)) {
			spec->btf_custom_path = s + sizeof(TEST_BTF_PATH) - 1;
		} else if (str_has_pfx(s, TEST_TAG_CAPS_UNPRIV)) {
			val = s + sizeof(TEST_TAG_CAPS_UNPRIV) - 1;
			err = parse_caps(val, &spec->unpriv.caps, "test caps");
			if (err)
				goto cleanup;
			spec->mode_mask |= UNPRIV;
		} else if (str_has_pfx(s, TEST_TAG_LOAD_MODE_PFX)) {
			val = s + sizeof(TEST_TAG_LOAD_MODE_PFX) - 1;
			if (strcmp(val, "jited") == 0) {
				load_mask = JITED;
			} else if (strcmp(val, "no_jited") == 0) {
				load_mask = NO_JITED;
			} else {
				PRINT_FAIL("bad load spec: '%s'", val);
				err = -EINVAL;
				goto cleanup;
			}
		}
	}

	spec->arch_mask = arch_mask ?: -1;
	spec->load_mask = load_mask ?: (JITED | NO_JITED);

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

		if (spec->unpriv.expect_msgs.cnt == 0)
			clone_msgs(&spec->priv.expect_msgs, &spec->unpriv.expect_msgs);
		if (spec->unpriv.expect_xlated.cnt == 0)
			clone_msgs(&spec->priv.expect_xlated, &spec->unpriv.expect_xlated);
		if (spec->unpriv.jited.cnt == 0)
			clone_msgs(&spec->priv.jited, &spec->unpriv.jited);
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

static void emit_xlated(const char *xlated, bool force)
{
	if (!force && env.verbosity == VERBOSE_NONE)
		return;
	fprintf(stdout, "XLATED:\n=============\n%s=============\n", xlated);
}

static void emit_jited(const char *jited, bool force)
{
	if (!force && env.verbosity == VERBOSE_NONE)
		return;
	fprintf(stdout, "JITED:\n=============\n%s=============\n", jited);
}

static void validate_msgs(char *log_buf, struct expected_msgs *msgs,
			  void (*emit_fn)(const char *buf, bool force))
{
	const char *log = log_buf, *prev_match;
	regmatch_t reg_match[1];
	int prev_match_line;
	int match_line;
	int i, j, err;

	prev_match_line = -1;
	match_line = 0;
	prev_match = log;
	for (i = 0; i < msgs->cnt; i++) {
		struct expect_msg *msg = &msgs->patterns[i];
		const char *match = NULL, *pat_status;
		bool wrong_line = false;

		if (!msg->is_regex) {
			match = strstr(log, msg->substr);
			if (match)
				log = match + strlen(msg->substr);
		} else {
			err = regexec(&msg->regex, log, 1, reg_match, 0);
			if (err == 0) {
				match = log + reg_match[0].rm_so;
				log += reg_match[0].rm_eo;
			}
		}

		if (match) {
			for (; prev_match < match; ++prev_match)
				if (*prev_match == '\n')
					++match_line;
			wrong_line = msg->on_next_line && prev_match_line >= 0 &&
				     prev_match_line + 1 != match_line;
		}

		if (!match || wrong_line) {
			PRINT_FAIL("expect_msg\n");
			if (env.verbosity == VERBOSE_NONE)
				emit_fn(log_buf, true /*force*/);
			for (j = 0; j <= i; j++) {
				msg = &msgs->patterns[j];
				if (j < i)
					pat_status = "MATCHED   ";
				else if (wrong_line)
					pat_status = "WRONG LINE";
				else
					pat_status = "EXPECTED  ";
				msg = &msgs->patterns[j];
				fprintf(stderr, "%s %s: '%s'\n",
					pat_status,
					msg->is_regex ? " REGEX" : "SUBSTR",
					msg->substr);
			}
			if (wrong_line) {
				fprintf(stderr,
					"expecting match at line %d, actual match is at line %d\n",
					prev_match_line + 1, match_line);
			}
			break;
		}

		prev_match_line = match_line;
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
		PRINT_FAIL("failed to drop capabilities: %i, %s\n", err, strerror(-err));
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
		PRINT_FAIL("failed to restore capabilities: %i, %s\n", err, strerror(-err));
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

/* Get a disassembly of BPF program after verifier applies all rewrites */
static int get_xlated_program_text(int prog_fd, char *text, size_t text_sz)
{
	struct bpf_insn *insn_start = NULL, *insn, *insn_end;
	__u32 insns_cnt = 0, i;
	char buf[64];
	FILE *out = NULL;
	int err;

	err = get_xlated_program(prog_fd, &insn_start, &insns_cnt);
	if (!ASSERT_OK(err, "get_xlated_program"))
		goto out;
	out = fmemopen(text, text_sz, "w");
	if (!ASSERT_OK_PTR(out, "open_memstream"))
		goto out;
	insn_end = insn_start + insns_cnt;
	insn = insn_start;
	while (insn < insn_end) {
		i = insn - insn_start;
		insn = disasm_insn(insn, buf, sizeof(buf));
		fprintf(out, "%d: %s\n", i, buf);
	}
	fflush(out);

out:
	free(insn_start);
	if (out)
		fclose(out);
	return err;
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
	int current_runtime = is_jit_enabled() ? JITED : NO_JITED;
	struct bpf_program *tprog = NULL, *tprog_iter;
	struct bpf_link *link, *links[32] = {};
	struct test_spec *spec_iter;
	struct cap_state caps = {};
	struct bpf_object *tobj;
	struct bpf_map *map;
	int retval, err, i;
	int links_cnt = 0;
	bool should_load;

	if (!test__start_subtest(subspec->name))
		return;

	if ((get_current_arch() & spec->arch_mask) == 0) {
		test__skip();
		return;
	}

	if ((current_runtime & spec->load_mask) == 0) {
		test__skip();
		return;
	}

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
		if (subspec->caps) {
			err = cap_enable_effective(subspec->caps, NULL);
			if (err) {
				PRINT_FAIL("failed to set capabilities: %i, %s\n", err, strerror(-err));
				goto subtest_cleanup;
			}
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
	validate_msgs(tester->log_buf, &subspec->expect_msgs, emit_verifier_log);

	if (subspec->expect_xlated.cnt) {
		err = get_xlated_program_text(bpf_program__fd(tprog),
					      tester->log_buf, tester->log_buf_sz);
		if (err)
			goto tobj_cleanup;
		emit_xlated(tester->log_buf, false /*force*/);
		validate_msgs(tester->log_buf, &subspec->expect_xlated, emit_xlated);
	}

	if (subspec->jited.cnt) {
		err = get_jited_program_text(bpf_program__fd(tprog),
					     tester->log_buf, tester->log_buf_sz);
		if (err == -EOPNOTSUPP) {
			printf("%s:SKIP: jited programs disassembly is not supported,\n", __func__);
			printf("%s:SKIP: tests are built w/o LLVM development libs\n", __func__);
			test__skip();
			goto tobj_cleanup;
		}
		if (!ASSERT_EQ(err, 0, "get_jited_program_text"))
			goto tobj_cleanup;
		emit_jited(tester->log_buf, false /*force*/);
		validate_msgs(tester->log_buf, &subspec->jited, emit_jited);
	}

	if (should_do_test_run(spec, subspec)) {
		/* For some reason test_verifier executes programs
		 * with all capabilities restored. Do the same here.
		 */
		if (restore_capabilities(&caps))
			goto tobj_cleanup;

		/* Do bpf_map__attach_struct_ops() for each struct_ops map.
		 * This should trigger bpf_struct_ops->reg callback on kernel side.
		 */
		bpf_object__for_each_map(map, tobj) {
			if (!bpf_map__autocreate(map) ||
			    bpf_map__type(map) != BPF_MAP_TYPE_STRUCT_OPS)
				continue;
			if (links_cnt >= ARRAY_SIZE(links)) {
				PRINT_FAIL("too many struct_ops maps");
				goto tobj_cleanup;
			}
			link = bpf_map__attach_struct_ops(map);
			if (!link) {
				PRINT_FAIL("bpf_map__attach_struct_ops failed for map %s: err=%d\n",
					   bpf_map__name(map), err);
				goto tobj_cleanup;
			}
			links[links_cnt++] = link;
		}

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
		/* redo bpf_map__attach_struct_ops for each test */
		while (links_cnt > 0)
			bpf_link__destroy(links[--links_cnt]);
	}

tobj_cleanup:
	while (links_cnt > 0)
		bpf_link__destroy(links[--links_cnt]);
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
