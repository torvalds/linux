// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock - Tracepoint helpers
 *
 * Copyright © 2025 Microsoft Corporation
 * Copyright © 2026 Cloudflare, Inc.
 */

#include <kunit/test.h>
#include <linux/cleanup.h>
#include <linux/dcache.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/lsm_audit.h>
#include <net/sock.h>

#include "access.h"
#include "domain.h"
#include "fs.h"
#include "log.h"
#include "ruleset.h"
#include "trace.h"

/*
 * Generates the tracepoint definitions in this translation unit.  The trace
 * event header dereferences the traced objects in TP_fast_assign, so the full
 * struct definitions (e.g. ruleset.h, domain.h) must be included before it.
 */
#define CREATE_TRACE_POINTS
#include <trace/events/landlock.h>

/**
 * landlock_trace_free_domain - Emit a tracepoint on domain deallocation
 *
 * @hierarchy: The domain's hierarchy being deallocated.
 *
 * Fires only for a hierarchy whose creation event was emitted, i.e. one that
 * left LANDLOCK_LOG_UNCOMMITTED in landlock_restrict_self().  This keeps the
 * create/free pair balanced: a hierarchy that never became observable is freed
 * silently, while a domain that landlock_restrict_self() created and a
 * thread-sync failure then aborted still fires free_domain, because its
 * creation event already fired.
 *
 * Called from landlock_log_free_domain().
 */
void landlock_trace_free_domain(const struct landlock_hierarchy *const hierarchy)
{
	/*
	 * The log_status read is a correctness guard (keep the create/free pair
	 * balanced), not a cost guard, so this cold path needs no
	 * trace_..._enabled() check: the tracepoint is a static-branch no-op
	 * when disabled.  The denial path guards trace_..._enabled() instead
	 * because it does expensive __getname()/path work before emitting.
	 */
	if (READ_ONCE(hierarchy->log_status) != LANDLOCK_LOG_UNCOMMITTED)
		trace_landlock_free_domain(hierarchy);
}

/**
 * landlock_trace_denial - Emit a tracepoint for a denied access request
 *
 * @request: Detail of the user space request.
 * @youngest_denied: The youngest hierarchy node that denied the access.
 * @missing: The set of denied access rights.
 * @same_exec: Whether the current task is the same executable that called
 *             landlock_restrict_self() for the denying domain, as computed
 *             by landlock_log_denial().
 * @logged: Whether the domain's policy selects this denial for logging, as
 *          computed by landlock_log_denial().
 *
 * Emits the tracepoint matching @request->type when its event is enabled.
 * Unlike audit, fires regardless of @logged; the value is recorded in the event
 * so consumers can filter on it.
 *
 * Called from landlock_log_denial().
 */
void landlock_trace_denial(
	const struct landlock_request *const request,
	const struct landlock_hierarchy *const youngest_denied,
	const access_mask_t missing, const bool same_exec, const bool logged)
{
	switch (request->type) {
	case LANDLOCK_REQUEST_FS_ACCESS:
	case LANDLOCK_REQUEST_FS_CHANGE_TOPOLOGY:
		if (trace_landlock_deny_access_fs_enabled()) {
			char *buf __free(__putname) = __getname();
			struct path dentry_path;
			const char *pathname;
			const struct path *path = NULL;

			/*
			 * Selects the path from the audit data type, as
			 * dump_common_audit_data() does.  A FS_ACCESS denial
			 * carries a file (hook_file_truncate) or an ioctl op
			 * (hook_file_ioctl) rather than a path;
			 * FS_CHANGE_TOPOLOGY carries a path or a bare dentry.
			 * Reading the wrong union member would dereference
			 * garbage, so every reachable type is handled here.
			 */
			switch (request->audit.type) {
			case LSM_AUDIT_DATA_FILE:
				path = &request->audit.u.file->f_path;
				break;
			case LSM_AUDIT_DATA_IOCTL_OP:
				path = &request->audit.u.op->path;
				break;
			case LSM_AUDIT_DATA_DENTRY:
				/*
				 * Build a path on the stack with the real
				 * dentry so TP_fast_assign can extract dev and
				 * ino; the mnt field is unused there.
				 */
				dentry_path = (struct path){
					.dentry = request->audit.u.dentry,
				};
				path = &dentry_path;
				break;
			case LSM_AUDIT_DATA_PATH:
				path = &request->audit.u.path;
				break;
			default:
				WARN_ONCE(1,
					  "Unhandled Landlock FS audit type %d",
					  request->audit.type);
				break;
			}

			if (!path)
				break;

			if (!buf) {
				pathname = "<no_mem>";
			} else if (request->audit.type ==
				   LSM_AUDIT_DATA_DENTRY) {
				/* No vfsmount: render the dentry path alone. */
				pathname = dentry_path_raw(
					request->audit.u.dentry, buf, PATH_MAX);
				if (IS_ERR(pathname))
					pathname =
						PTR_ERR(pathname) ==
								-ENAMETOOLONG ?
							"<too_long>" :
							"<unreachable>";
			} else {
				pathname = resolve_path_for_trace(path, buf);
			}

			trace_landlock_deny_access_fs(youngest_denied,
						      same_exec, logged,
						      missing, path, pathname);
		}
		break;
	case LANDLOCK_REQUEST_NET_ACCESS:
		if (trace_landlock_deny_access_net_enabled())
			trace_landlock_deny_access_net(
				youngest_denied, same_exec, logged, missing,
				request->audit.u.net->sk,
				ntohs(request->audit.u.net->sport),
				ntohs(request->audit.u.net->dport));
		break;
	case LANDLOCK_REQUEST_PTRACE:
		if (trace_landlock_deny_ptrace_enabled())
			trace_landlock_deny_ptrace(youngest_denied, same_exec,
						   logged,
						   request->other_domain_id,
						   request->audit.u.tsk);
		break;
	case LANDLOCK_REQUEST_SCOPE_SIGNAL:
		if (trace_landlock_deny_scope_signal_enabled())
			trace_landlock_deny_scope_signal(
				youngest_denied, same_exec, logged,
				request->other_domain_id, request->audit.u.tsk);
		break;
	case LANDLOCK_REQUEST_SCOPE_ABSTRACT_UNIX_SOCKET:
		if (trace_landlock_deny_scope_abstract_unix_socket_enabled())
			trace_landlock_deny_scope_abstract_unix_socket(
				youngest_denied, same_exec, logged,
				request->other_domain_id,
				request->audit.u.net->sk);
		break;
	default:
		WARN_ONCE(1, "Unhandled Landlock request type %d",
			  request->type);
		break;
	}
}

#ifdef CONFIG_SECURITY_LANDLOCK_KUNIT_TEST

static void test_trace_seq_init(struct trace_seq *const seq, const size_t size)
{
	memset(seq, 0, sizeof(*seq));
	seq_buf_init(&seq->seq, seq->buffer, size);
}

static void test_untrusted_str_data(struct kunit *const test)
{
	const char binary[] = { 'a', '\0', '<' };
	static const char ellipsis[] = "\xe2\x80\xa6";
	struct trace_seq *const seq =
		kunit_kzalloc(test, sizeof(*seq), GFP_KERNEL);
	const char *output;

	KUNIT_ASSERT_NOT_NULL(test, seq);
	test_trace_seq_init(seq, TRACE_SEQ_BUFFER_SIZE);
	output = __trace_print_untrusted_str(seq, "<too_long>", 10);
	KUNIT_ASSERT_NOT_NULL(test, output);
	KUNIT_EXPECT_STREQ(test, output, "<too_long>");

	test_trace_seq_init(seq, TRACE_SEQ_BUFFER_SIZE);
	output = __trace_print_untrusted_str(seq, binary, sizeof(binary));
	KUNIT_ASSERT_NOT_NULL(test, output);
	KUNIT_EXPECT_STREQ(test, output, "a\\000<");

	/* Input ellipsis bytes are escaped and cannot mimic the raw marker. */
	test_trace_seq_init(seq, TRACE_SEQ_BUFFER_SIZE);
	output = __trace_print_untrusted_str(seq, ellipsis,
					     sizeof(ellipsis) - 1);
	KUNIT_ASSERT_NOT_NULL(test, output);
	KUNIT_EXPECT_STREQ(test, output, "\\342\\200\\246");
}

static void test_untrusted_str_boundaries(struct kunit *const test)
{
	static const char escaped_space[] = "\\040";
	const size_t output_size = TRACE_UNTRUSTED_STR_OUTPUT_SIZE;
	const size_t marker_len = sizeof(TRACE_TRUNCATION_MARKER) - 1;
	const size_t escape_len = sizeof(escaped_space) - 1;
	const size_t exact_prefix_len =
		output_size - marker_len - 1 - escape_len;
	const size_t short_prefix_len = exact_prefix_len + 1;
	struct trace_seq *const seq =
		kunit_kzalloc(test, sizeof(*seq), GFP_KERNEL);
	char *const input = kunit_kmalloc(test, output_size + 1, GFP_KERNEL);
	char *const expected = kunit_kmalloc(test, output_size, GFP_KERNEL);
	const char *output;

	KUNIT_ASSERT_NOT_NULL(test, seq);
	KUNIT_ASSERT_NOT_NULL(test, input);
	KUNIT_ASSERT_NOT_NULL(test, expected);

	/* The escaped string and its trailing NUL exactly fit the limit. */
	memset(input, 'a', output_size - 1);
	test_trace_seq_init(seq, TRACE_SEQ_BUFFER_SIZE);
	output = __trace_print_untrusted_str(seq, input, output_size - 1);
	KUNIT_ASSERT_NOT_NULL(test, output);
	KUNIT_EXPECT_EQ(test, seq->seq.len, output_size);
	KUNIT_EXPECT_EQ(test, memcmp(output, input, output_size - 1), 0);

	/* Stop before a four-byte escape when only three bytes remain. */
	memset(input, 'a', short_prefix_len);
	input[short_prefix_len] = ' ';
	memset(input + short_prefix_len + 1, 'b', 5);
	memset(expected, 'a', short_prefix_len);
	memcpy(expected + short_prefix_len, TRACE_TRUNCATION_MARKER,
	       marker_len + 1);
	test_trace_seq_init(seq, TRACE_SEQ_BUFFER_SIZE);
	output = __trace_print_untrusted_str(seq, input, short_prefix_len + 6);
	KUNIT_ASSERT_NOT_NULL(test, output);
	KUNIT_EXPECT_STREQ(test, output, expected);

	/* Include a four-byte escape that exactly fills the prefix capacity. */
	memset(input, 'a', exact_prefix_len);
	input[exact_prefix_len] = ' ';
	memset(input + exact_prefix_len + 1, 'b', marker_len + 1);
	memset(expected, 'a', exact_prefix_len);
	memcpy(expected + exact_prefix_len, escaped_space, escape_len);
	memcpy(expected + exact_prefix_len + escape_len,
	       TRACE_TRUNCATION_MARKER, marker_len + 1);
	test_trace_seq_init(seq, TRACE_SEQ_BUFFER_SIZE);
	output = __trace_print_untrusted_str(seq, input,
					     exact_prefix_len + marker_len + 2);
	KUNIT_ASSERT_NOT_NULL(test, output);
	KUNIT_EXPECT_STREQ(test, output, expected);

	/* Literal backslashes remain escaped in complete output. */
	test_trace_seq_init(seq, TRACE_SEQ_BUFFER_SIZE);
	output = __trace_print_untrusted_str(seq, "/\\000", 5);
	KUNIT_ASSERT_NOT_NULL(test, output);
	KUNIT_EXPECT_STREQ(test, output, "/\\\\000");
}

static void test_untrusted_str_cursor(struct kunit *const test)
{
	const size_t padding_len =
		TRACE_SEQ_BUFFER_SIZE - TRACE_UNTRUSTED_STR_OUTPUT_SIZE + 1;
	struct trace_seq *const seq =
		kunit_kzalloc(test, sizeof(*seq), GFP_KERNEL);
	char *const padding = kunit_kzalloc(test, padding_len, GFP_KERNEL);
	const char *output;

	KUNIT_ASSERT_NOT_NULL(test, seq);
	KUNIT_ASSERT_NOT_NULL(test, padding);

	/* Accept available space exactly equal to the fixed reservation. */
	test_trace_seq_init(seq, TRACE_SEQ_BUFFER_SIZE);
	trace_seq_putmem(seq, padding, padding_len - 1);
	output = __trace_print_untrusted_str(seq, "/a", 2);
	KUNIT_ASSERT_NOT_NULL(test, output);
	KUNIT_EXPECT_STREQ(test, output, "/a");
	KUNIT_EXPECT_EQ(test, seq->seq.len, padding_len - 1 + sizeof("/a"));

	/* Reject one byte less without changing the scratch cursor. */
	test_trace_seq_init(seq, TRACE_SEQ_BUFFER_SIZE);
	trace_seq_putmem(seq, padding, padding_len);
	output = __trace_print_untrusted_str(seq, "/a", 2);
	KUNIT_EXPECT_NULL(test, output);
	KUNIT_EXPECT_EQ(test, seq->seq.len, padding_len);
}

static void test_untrusted_str_composition(struct kunit *const test)
{
	static const struct trace_print_flags flags[] = {
		{ .mask = 1, .name = "read" },
	};
	const size_t output_size = TRACE_UNTRUSTED_STR_OUTPUT_SIZE;
	const size_t prefix_len = output_size - sizeof(TRACE_TRUNCATION_MARKER);
	struct trace_seq *const seq =
		kunit_kzalloc(test, sizeof(*seq), GFP_KERNEL);
	char *const expected = kunit_kmalloc(test, output_size, GFP_KERNEL);
	char *const path = kunit_kmalloc(test, output_size, GFP_KERNEL);
	const char *flags_output, *path_output;

	KUNIT_ASSERT_NOT_NULL(test, seq);
	KUNIT_ASSERT_NOT_NULL(test, expected);
	KUNIT_ASSERT_NOT_NULL(test, path);
	memset(path, 'a', output_size);
	memset(expected, 'a', prefix_len);
	memcpy(expected + prefix_len, TRACE_TRUNCATION_MARKER,
	       sizeof(TRACE_TRUNCATION_MARKER));

	/* Exercise both legal TP_printk() sibling evaluation orders. */
	test_trace_seq_init(seq, TRACE_SEQ_BUFFER_SIZE);
	path_output = __trace_print_untrusted_str(seq, path, output_size);
	flags_output =
		trace_print_flags_seq(seq, "|", 1, flags, ARRAY_SIZE(flags));
	KUNIT_ASSERT_NOT_NULL(test, path_output);
	KUNIT_EXPECT_STREQ(test, path_output, expected);
	KUNIT_EXPECT_STREQ(test, flags_output, "read");

	test_trace_seq_init(seq, TRACE_SEQ_BUFFER_SIZE);
	flags_output =
		trace_print_flags_seq(seq, "|", 1, flags, ARRAY_SIZE(flags));
	path_output = __trace_print_untrusted_str(seq, path, output_size);
	KUNIT_ASSERT_NOT_NULL(test, path_output);
	KUNIT_EXPECT_STREQ(test, path_output, expected);
	KUNIT_EXPECT_STREQ(test, flags_output, "read");
}

static struct kunit_case test_cases[] = {
	/* clang-format off */
	KUNIT_CASE(test_untrusted_str_data),
	KUNIT_CASE(test_untrusted_str_boundaries),
	KUNIT_CASE(test_untrusted_str_cursor),
	KUNIT_CASE(test_untrusted_str_composition),
	{}
	/* clang-format on */
};

static struct kunit_suite test_suite = {
	.name = "landlock_trace",
	.test_cases = test_cases,
};

kunit_test_suite(test_suite);

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */
