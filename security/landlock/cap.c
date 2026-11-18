// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock - Capability hooks
 *
 * Copyright © 2026 Cloudflare, Inc.
 */

#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/lsm_audit.h>
#include <linux/lsm_hooks.h>
#include <linux/security.h>
#include <uapi/linux/landlock.h>

#include "cap.h"
#include "cred.h"
#include "domain.h"
#include "limits.h"
#include "log.h"
#include "ruleset.h"
#include "setup.h"

static const struct access_masks cap_permission = {
	.permissions = LANDLOCK_PERMISSION_CAPABILITY_USE,
};

/**
 * check_cap - Check a capability use against the domain of @cred
 *
 * @cred: Credentials being checked.
 * @ns: Target user namespace used by commoncap's capability check, which is
 *      not necessarily @cred->user_ns.  It is forwarded unchanged to typed
 *      trace context but is not used for the verdict (policy depends on @cred
 *      and @cap).
 * @cap: Capability number (CAP_*).
 * @opts: Capability check options.  CAP_OPT_NOAUDIT skips the denial record:
 *        no audit record, no trace event, and no denial count.
 *
 * Pure bitmask check: denies the capability if it is not in the layer's allowed
 * set.  It does not check that @cred holds @cap: only call it for a capability
 * that commoncap already granted.
 *
 * Return: 0 if allowed, -EPERM if capability use is denied.
 */
static int check_cap(const struct cred *const cred,
		     struct user_namespace *const ns, const int cap,
		     const unsigned int opts)
{
	const struct landlock_cred_security *subject;
	size_t denied_layer;

	subject = landlock_get_applicable_subject(cred, cap_permission, NULL);
	if (!subject)
		return 0;

	denied_layer = landlock_permission_is_denied(
		subject->domain, LANDLOCK_PERMISSION_CAPABILITY_USE,
		landlock_cap_to_bit(cap));
	if (!denied_layer)
		return 0;

	if (!(opts & CAP_OPT_NOAUDIT))
		landlock_log_denial(
			subject,
			&(struct landlock_request){
				.type = LANDLOCK_REQUEST_CAPABILITY,
				.audit.type = LSM_AUDIT_DATA_CAP,
				.audit.u.cap = cap,
				.permission =
					LANDLOCK_PERMISSION_CAPABILITY_USE,
				.layer_plus_one = denied_layer,
#ifdef CONFIG_TRACEPOINTS
				.trace_cap =
					&(struct landlock_cap_trace){
						.cred = cred,
						.user_ns = ns,
					},
#endif /* CONFIG_TRACEPOINTS */
			});

	return -EPERM;
}

/**
 * hook_capable - Deny capability use for Landlock-sandboxed processes
 *
 * @cred: Credentials being checked.
 * @ns: Target user namespace, used only as typed trace context.
 * @cap: Capability number (CAP_*).
 * @opts: Capability check options, see check_cap().
 *
 * This hook is purely restrictive: commoncap is registered with LSM_ORDER_FIRST
 * so cap_capable() always runs first, which means Landlock can deny
 * capabilities that commoncap would allow, but never grant capabilities that
 * commoncap denied.
 *
 * Return: 0 if allowed, -EPERM if capability use is denied.
 */
static int hook_capable(const struct cred *cred, struct user_namespace *ns,
			int cap, unsigned int opts)
{
	return check_cap(cred, ns, cap, opts);
}

/**
 * hook_capset - Restrict inheritable capability changes
 *
 * @new: New credentials (unused).
 * @old: Current credentials.
 * @effective: New effective set (unused).
 * @inheritable: New inheritable set.
 * @permitted: New permitted set (unused).
 *
 * cap_capset() denies a new inheritable set that is not a subset of the old
 * inheritable and permitted sets, unless cap_inh_is_capped() finds CAP_SETPCAP.
 * This check uses cap_capable(), which skips hook_capable().  commoncap runs
 * first, so such a set reaching this hook means that commoncap granted
 * CAP_SETPCAP: check it against the domain.  capset(2) discards @new if this
 * hook fails, so nothing is committed.
 *
 * Return: 0 if allowed, -EPERM if CAP_SETPCAP use is denied.
 */
static int hook_capset(struct cred *new, const struct cred *old,
		       const kernel_cap_t *effective,
		       const kernel_cap_t *inheritable,
		       const kernel_cap_t *permitted)
{
	if (cap_issubset(*inheritable,
			 cap_combine(old->cap_inheritable, old->cap_permitted)))
		return 0;

	return check_cap(old, old->user_ns, CAP_SETPCAP, CAP_OPT_NONE);
}

/**
 * hook_mmap_addr - Restrict mappings below dac_mmap_min_addr
 *
 * @addr: Address of the new mapping.
 *
 * cap_mmap_addr() requires CAP_SYS_RAWIO to map below dac_mmap_min_addr, but
 * calls cap_capable() directly, which skips hook_capable().  It checks the
 * acting task's credentials against init_user_ns because this global limit must
 * not be bypassed with a capability only held in a nested user namespace.
 * Mirror the condition, using the same credential for the domain check and both
 * objects for typed trace context.  commoncap runs first, so this only sees
 * mappings that commoncap allowed.
 *
 * Return: 0 if allowed, -EPERM if CAP_SYS_RAWIO use is denied.
 */
static int hook_mmap_addr(unsigned long addr)
{
	if (addr >= dac_mmap_min_addr)
		return 0;

	return check_cap(current_cred(), &init_user_ns, CAP_SYS_RAWIO,
			 CAP_OPT_NONE);
}

/**
 * hook_vm_enough_memory - Restrict the admin memory reserve
 *
 * @mm: Address space being charged (unused).
 * @pages: Number of pages being charged (unused).
 *
 * cap_vm_enough_memory() lets CAP_SYS_ADMIN use the vm.admin_reserve_kbytes
 * reserve, but calls cap_capable() directly, which skips hook_capable().  It
 * checks the acting task's credentials against init_user_ns because a
 * capability only held in a nested user namespace must not grant access to the
 * global reserve.  Mirror the check, using the same credential for the domain
 * check and both objects for typed trace context.  commoncap runs first, so
 * this only sees tasks that commoncap let use the reserve.  Like commoncap, do
 * not log denials: this only selects a memory limit.
 *
 * Return: 0 if the domain does not withhold the reserve, -EPERM if
 * CAP_SYS_ADMIN use is denied.  An error does not fail the allocation by
 * itself, but without the reserve, the allocation may fail with -ENOMEM.
 */
static int hook_vm_enough_memory(struct mm_struct *mm, long pages)
{
	return check_cap(current_cred(), &init_user_ns, CAP_SYS_ADMIN,
			 CAP_OPT_NOAUDIT);
}

static struct security_hook_list landlock_hooks[] __ro_after_init = {
	LSM_HOOK_INIT(capable, hook_capable),
	LSM_HOOK_INIT(capset, hook_capset),
	LSM_HOOK_INIT(mmap_addr, hook_mmap_addr),
	LSM_HOOK_INIT(vm_enough_memory, hook_vm_enough_memory),
};

__init void landlock_add_cap_hooks(void)
{
	security_add_hooks(landlock_hooks, ARRAY_SIZE(landlock_hooks),
			   &landlock_lsmid);
}

#ifdef CONFIG_SECURITY_LANDLOCK_KUNIT_TEST

#include <kunit/test.h>

static void test_cap_to_bit(struct kunit *const test)
{
	KUNIT_EXPECT_EQ(test, BIT_ULL(0), landlock_cap_to_bit(0));
	KUNIT_EXPECT_EQ(test, BIT_ULL(CAP_NET_RAW),
			landlock_cap_to_bit(CAP_NET_RAW));
	KUNIT_EXPECT_EQ(test, BIT_ULL(CAP_SYS_ADMIN),
			landlock_cap_to_bit(CAP_SYS_ADMIN));
	KUNIT_EXPECT_EQ(test, BIT_ULL(CAP_LAST_CAP),
			landlock_cap_to_bit(CAP_LAST_CAP));
}

static void test_cap_to_bit_invalid(struct kunit *const test)
{
	if (!IS_ENABLED(CONFIG_BUG))
		kunit_skip(test, "requires CONFIG_BUG");

	/* clang-format off */
	kunit_warning_suppress(test) {
		/* clang-format on */
		KUNIT_EXPECT_EQ(test, 0ULL, landlock_cap_to_bit(-1));
		KUNIT_EXPECT_EQ(test, 0ULL,
				landlock_cap_to_bit(CAP_LAST_CAP + 1));
	}
}

static void test_caps_to_bits_valid(struct kunit *const test)
{
	KUNIT_EXPECT_EQ(test, (u64)CAP_VALID_MASK,
			landlock_caps_to_bits(CAP_VALID_MASK));
	KUNIT_EXPECT_EQ(test, BIT_ULL(CAP_NET_RAW),
			landlock_caps_to_bits(BIT_ULL(CAP_NET_RAW)));
}

static void test_caps_to_bits_unknown(struct kunit *const test)
{
	if (!IS_ENABLED(CONFIG_BUG))
		kunit_skip(test, "requires CONFIG_BUG");

	/* clang-format off */
	kunit_warning_suppress(test) {
		/* clang-format on */
		KUNIT_EXPECT_EQ(
			test, 0ULL,
			landlock_caps_to_bits(BIT_ULL(CAP_LAST_CAP + 1)));
	}
}

static void test_caps_to_bits_zero(struct kunit *const test)
{
	KUNIT_EXPECT_EQ(test, 0ULL, landlock_caps_to_bits(0));
}

static struct kunit_case test_cases[] = {
	/* clang-format off */
	KUNIT_CASE(test_cap_to_bit),
	KUNIT_CASE(test_cap_to_bit_invalid),
	KUNIT_CASE(test_caps_to_bits_valid),
	KUNIT_CASE(test_caps_to_bits_unknown),
	KUNIT_CASE(test_caps_to_bits_zero),
	{}
	/* clang-format on */
};

static struct kunit_suite test_suite = {
	.name = "landlock_cap",
	.test_cases = test_cases,
};

kunit_test_suite(test_suite);

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */
