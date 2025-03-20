// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock - Audit helpers
 *
 * Copyright © 2023-2025 Microsoft Corporation
 */

#include <kunit/test.h>
#include <linux/audit.h>
#include <linux/lsm_audit.h>

#include "audit.h"
#include "cred.h"
#include "domain.h"
#include "limits.h"
#include "ruleset.h"

static const char *get_blocker(const enum landlock_request_type type)
{
	switch (type) {
	case LANDLOCK_REQUEST_PTRACE:
		return "ptrace";
	}

	WARN_ON_ONCE(1);
	return "unknown";
}

static void log_blockers(struct audit_buffer *const ab,
			 const enum landlock_request_type type)
{
	audit_log_format(ab, "%s", get_blocker(type));
}

static struct landlock_hierarchy *
get_hierarchy(const struct landlock_ruleset *const domain, const size_t layer)
{
	struct landlock_hierarchy *hierarchy = domain->hierarchy;
	ssize_t i;

	if (WARN_ON_ONCE(layer >= domain->num_layers))
		return hierarchy;

	for (i = domain->num_layers - 1; i > layer; i--) {
		if (WARN_ON_ONCE(!hierarchy->parent))
			break;

		hierarchy = hierarchy->parent;
	}

	return hierarchy;
}

#ifdef CONFIG_SECURITY_LANDLOCK_KUNIT_TEST

static void test_get_hierarchy(struct kunit *const test)
{
	struct landlock_hierarchy dom0_hierarchy = {
		.id = 10,
	};
	struct landlock_hierarchy dom1_hierarchy = {
		.parent = &dom0_hierarchy,
		.id = 20,
	};
	struct landlock_hierarchy dom2_hierarchy = {
		.parent = &dom1_hierarchy,
		.id = 30,
	};
	struct landlock_ruleset dom2 = {
		.hierarchy = &dom2_hierarchy,
		.num_layers = 3,
	};

	KUNIT_EXPECT_EQ(test, 10, get_hierarchy(&dom2, 0)->id);
	KUNIT_EXPECT_EQ(test, 20, get_hierarchy(&dom2, 1)->id);
	KUNIT_EXPECT_EQ(test, 30, get_hierarchy(&dom2, 2)->id);
	KUNIT_EXPECT_EQ(test, 30, get_hierarchy(&dom2, -1)->id);
}

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */

static bool is_valid_request(const struct landlock_request *const request)
{
	if (WARN_ON_ONCE(request->layer_plus_one > LANDLOCK_MAX_NUM_LAYERS))
		return false;

	if (WARN_ON_ONCE(!request->layer_plus_one))
		return false;

	return true;
}

/**
 * landlock_log_denial - Create audit records related to a denial
 *
 * @subject: The Landlock subject's credential denying an action.
 * @request: Detail of the user space request.
 */
void landlock_log_denial(const struct landlock_cred_security *const subject,
			 const struct landlock_request *const request)
{
	struct audit_buffer *ab;
	struct landlock_hierarchy *youngest_denied;
	size_t youngest_layer;

	if (WARN_ON_ONCE(!subject || !subject->domain ||
			 !subject->domain->hierarchy || !request))
		return;

	if (!is_valid_request(request))
		return;

	if (!audit_enabled)
		return;

	youngest_layer = request->layer_plus_one - 1;
	youngest_denied = get_hierarchy(subject->domain, youngest_layer);

	/* Ignores denials after an execution. */
	if (!(subject->domain_exec & (1 << youngest_layer)))
		return;

	/* Uses consistent allocation flags wrt common_lsm_audit(). */
	ab = audit_log_start(audit_context(), GFP_ATOMIC | __GFP_NOWARN,
			     AUDIT_LANDLOCK_ACCESS);
	if (!ab)
		return;

	audit_log_format(ab, "domain=%llx blockers=", youngest_denied->id);
	log_blockers(ab, request->type);
	audit_log_lsm_data(ab, &request->audit);
	audit_log_end(ab);
}

#ifdef CONFIG_SECURITY_LANDLOCK_KUNIT_TEST

static struct kunit_case test_cases[] = {
	/* clang-format off */
	KUNIT_CASE(test_get_hierarchy),
	{}
	/* clang-format on */
};

static struct kunit_suite test_suite = {
	.name = "landlock_audit",
	.test_cases = test_cases,
};

kunit_test_suite(test_suite);

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */
