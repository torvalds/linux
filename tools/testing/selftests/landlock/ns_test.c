// SPDX-License-Identifier: GPL-2.0
/*
 * Landlock tests - Namespace restriction
 *
 * Copyright © 2026 Cloudflare, Inc.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <linux/landlock.h>
#include <linux/mount.h>
#include <linux/nsfs.h>
#include <sched.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

/* Defines AT_RECURSIVE without type conflicts. */
#define _ASM_GENERIC_FCNTL_H
#include <linux/fcntl.h>

#include "audit.h"
#include "common.h"
#include "trace.h"

#define TRACE_TASK "ns_test"

/*
 * Max length for /proc/self/ns/<name> paths (longest: "/proc/self/ns/cgroup").
 */
#define NS_PROC_PATH_MAX 32

static int create_ns_ruleset(void)
{
	const struct landlock_ruleset_attr attr = {
		.handled_permissions = LANDLOCK_PERMISSION_NAMESPACE_USE,
	};

	return landlock_create_ruleset(&attr, sizeof(attr), 0);
}

static int add_ns_rule(int ruleset_fd, __u64 ns_type)
{
	const struct landlock_namespace_attr attr = {
		.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE,
		.allowed_namespace_types = ns_type,
	};

	return landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE, &attr, 0);
}

static int add_ns_rule_full(int ruleset_fd, __u64 allowed, __u64 quiet)
{
	const struct landlock_namespace_attr attr = {
		.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE,
		.allowed_namespace_types = allowed,
		.quiet_namespace_types = quiet,
	};

	return landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE, &attr, 0);
}

/*
 * Returns the /proc/self/NS entry name for a given CLONE_NEW* type, or NULL if
 * unknown.  Used to check kernel support without side effects.
 */
static const char *ns_proc_name(__u64 ns_type)
{
	switch (ns_type) {
	case CLONE_NEWNS:
		return "mnt";
	case CLONE_NEWCGROUP:
		return "cgroup";
	case CLONE_NEWUTS:
		return "uts";
	case CLONE_NEWIPC:
		return "ipc";
	case CLONE_NEWUSER:
		return "user";
	case CLONE_NEWPID:
		return "pid";
	case CLONE_NEWNET:
		return "net";
	case CLONE_NEWTIME:
		return "time";
	default:
		return NULL;
	}
}

static bool ns_is_supported(__u64 ns_type, char *proc_path, size_t size)
{
	const char *ns_name;

	ns_name = ns_proc_name(ns_type);
	if (!ns_name)
		return false;

	snprintf(proc_path, size, "/proc/self/ns/%s", ns_name);
	return access(proc_path, F_OK) == 0;
}

/* Rule validation tests */

TEST(add_rule_bad_attr)
{
	const struct landlock_ruleset_attr cap_only_attr = {
		.handled_permissions = LANDLOCK_PERMISSION_CAPABILITY_USE,
	};
	int ruleset_fd;
	struct landlock_namespace_attr attr = {};

	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);

	/* Invalid rule pointer returns EFAULT. */
	ASSERT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
					NULL, 0));
	ASSERT_EQ(EFAULT, errno);

	/* Empty permissions selector returns ENOMSG. */
	attr.permissions = 0;
	attr.allowed_namespace_types = CLONE_NEWUTS;
	ASSERT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
					&attr, 0));
	ASSERT_EQ(ENOMSG, errno);

	/* Valid namespace selector plus an extra unhandled selector bit. */
	attr.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE |
			   LANDLOCK_PERMISSION_CAPABILITY_USE;
	attr.allowed_namespace_types = CLONE_NEWUTS;
	ASSERT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
					&attr, 0));
	ASSERT_EQ(EINVAL, errno);

	/* permissions with wrong type. */
	attr.permissions = LANDLOCK_PERMISSION_CAPABILITY_USE;
	attr.allowed_namespace_types = CLONE_NEWUTS;
	ASSERT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
					&attr, 0));
	ASSERT_EQ(EINVAL, errno);

	/*
	 * Unknown namespace bits (e.g. bit 63) are silently accepted for
	 * forward compatibility.  Only known CLONE_NEW* bits are stored.
	 */
	attr.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE;
	attr.allowed_namespace_types = 1ULL << 63;
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
				       &attr, 0));

	/* Useless rule: neither allowed nor quiet types set returns ENOMSG. */
	attr.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE;
	attr.allowed_namespace_types = 0;
	attr.quiet_namespace_types = 0;
	ASSERT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
					&attr, 0));
	ASSERT_EQ(ENOMSG, errno);

	/* Quiet-only rule (empty allowed set) is legal. */
	attr.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE;
	attr.allowed_namespace_types = 0;
	attr.quiet_namespace_types = CLONE_NEWUTS;
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
				       &attr, 0));

	/* Allow and quiet in the same rule. */
	attr.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE;
	attr.allowed_namespace_types = CLONE_NEWNET;
	attr.quiet_namespace_types = CLONE_NEWUTS;
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
				       &attr, 0));
	attr.quiet_namespace_types = 0;

	/*
	 * Bit 1 is not a CLONE_NEW* value but is silently accepted for forward
	 * compatibility (no hole rejection).
	 */
	attr.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE;
	attr.allowed_namespace_types = (1ULL << 1);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
				       &attr, 0));

	/* Multi-bit values are valid (bitmask allows multiple types). */
	attr.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE;
	attr.allowed_namespace_types = CLONE_NEWUTS | CLONE_NEWNET;
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
				       &attr, 0));

	/*
	 * LANDLOCK_ADD_RULE_QUIET (and any other flag) is filesystem/network
	 * only and must be rejected for namespace rules, even when every attr
	 * field is otherwise valid.
	 */
	attr.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE;
	attr.allowed_namespace_types = CLONE_NEWUTS;
	ASSERT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
					&attr, LANDLOCK_ADD_RULE_QUIET));
	ASSERT_EQ(EINVAL, errno);

	EXPECT_EQ(0, close(ruleset_fd));

	/*
	 * Ruleset handles LANDLOCK_PERMISSION_CAPABILITY_USE but not
	 * LANDLOCK_PERMISSION_NAMESPACE_USE: adding a namespace rule must be
	 * rejected.
	 */
	ruleset_fd = landlock_create_ruleset(&cap_only_attr,
					     sizeof(cap_only_attr), 0);
	ASSERT_LE(0, ruleset_fd);
	attr.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE;
	attr.allowed_namespace_types = CLONE_NEWUTS;
	ASSERT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
					&attr, 0));
	ASSERT_EQ(EINVAL, errno);
	EXPECT_EQ(0, close(ruleset_fd));
}

/*
 * Unknown namespace types in the upper range are silently accepted (allow-list:
 * they have no effect since the kernel never checks them).
 */
TEST(add_rule_unknown)
{
	int ruleset_fd;
	struct landlock_namespace_attr attr = {
		.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE,
	};

	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);

	/*
	 * Bit 31 is in the lower 32 bits but not a CLONE_NEW* value.  Silently
	 * accepted for forward compatibility (no hole rejection).
	 */
	attr.allowed_namespace_types = 1ULL << 31;
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
				       &attr, 0));

	/* Bit 32 is in the unknown upper range: silently accepted. */
	attr.allowed_namespace_types = 1ULL << 32;
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
				       &attr, 0));

	EXPECT_EQ(0, close(ruleset_fd));
}

/*
 * A rule that lists only namespace bits unknown to the running kernel is
 * accepted by landlock_add_rule() but has no runtime effect: once the domain is
 * enforced, any actual CLONE_NEW* operation is still denied by the per-category
 * deny-by-default behaviour.  This documents the forward-compatibility
 * contract: unknown bits are silently accepted so the same policy can be loaded
 * across kernels, but they never grant a permission that the running kernel
 * knows nothing about.
 */
TEST(add_rule_unknown_no_runtime_effect)
{
	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_permissions = LANDLOCK_PERMISSION_NAMESPACE_USE,
	};
	struct landlock_namespace_attr attr = {
		.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE,
		/* Only unknown bits: bit 31 (in lower 32) and bit 32. */
		.allowed_namespace_types = (1ULL << 31) | (1ULL << 32),
	};
	int ruleset_fd;

	disable_caps(_metadata);

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);

	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
				       &attr, 0));

	/*
	 * Baseline: absent the domain, creating CLONE_NEWUTS succeeds.  This
	 * ensures the EPERM below is attributable to Landlock rather than to a
	 * missing capability, and fails if the hook always allows.
	 */
	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0, unshare(CLONE_NEWUTS));

	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	/*
	 * CLONE_NEWUTS is a real, known CLONE_NEW* type but was not authorised
	 * by the rule above; deny-by-default applies.
	 */
	EXPECT_EQ(-1, unshare(CLONE_NEWUTS));
	EXPECT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

/*
 * The install-hook mirror of add_rule_unknown_no_runtime_effect: a rule listing
 * only unknown namespace bits has no runtime effect on setns(2) either.  Once
 * enforced, entering a known namespace type is still denied by the per-category
 * deny-by-default behaviour.
 */
TEST(add_rule_unknown_no_runtime_effect_setns)
{
	const struct landlock_ruleset_attr ruleset_attr = {
		.handled_permissions = LANDLOCK_PERMISSION_NAMESPACE_USE,
	};
	struct landlock_namespace_attr attr = {
		.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE,
		/* Only unknown bits: bit 31 (in lower 32) and bit 32. */
		.allowed_namespace_types = (1ULL << 31) | (1ULL << 32),
	};
	int ruleset_fd, ns_fd;

	disable_caps(_metadata);

	/* Open the NS FD before enforcing the domain. */
	ns_fd = open("/proc/self/ns/uts", O_RDONLY);
	ASSERT_LE(0, ns_fd);

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
				       &attr, 0));

	/*
	 * Baseline: absent the domain, entering the process's own UTS namespace
	 * succeeds, so the later EPERM is attributable to Landlock rather than
	 * to a missing capability, and fails if the hook always allows.
	 */
	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0, setns(ns_fd, CLONE_NEWUTS));

	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	/*
	 * CLONE_NEWUTS is a real, known CLONE_NEW* type but was not authorised
	 * by the rule above; deny-by-default applies at the install hook.
	 */
	EXPECT_EQ(-1, setns(ns_fd, CLONE_NEWUTS));
	EXPECT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, close(ns_fd));
}

/* Namespace creation tests (variant-based positive/negative) */

/* clang-format off */
FIXTURE(ns_create) {
	/* clang-format on */
	char proc_path[NS_PROC_PATH_MAX];
};

FIXTURE_VARIANT(ns_create)
{
	const __u64 namespace_types;
	const bool is_sandboxed;
	const bool has_rule;
	const bool drop_all_caps;
	const int expected_result;
};

/*
 * Unsandboxed baseline: no Landlock domain is enforced. User namespace creation
 * should succeed without any restriction.
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, user_unsandboxed) {
	/* clang-format on */
	.namespace_types = CLONE_NEWUSER,
	.is_sandboxed = false,
	.has_rule = false,
	.drop_all_caps = false,
	.expected_result = 0,
};

/*
 * User namespace creation denied: handled by Landlock but no rule allows
 * CLONE_NEWUSER.
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, user_denied) {
	/* clang-format on */
	.namespace_types = CLONE_NEWUSER,
	.is_sandboxed = true,
	.has_rule = false,
	.drop_all_caps = false,
	.expected_result = EPERM,
};

/* User namespace creation allowed: Landlock rule permits CLONE_NEWUSER. */
/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, user_allowed) {
	/* clang-format on */
	.namespace_types = CLONE_NEWUSER,
	.is_sandboxed = true,
	.has_rule = true,
	.drop_all_caps = false,
	.expected_result = 0,
};

/*
 * User namespace creation while unprivileged: the process has no capabilities
 * but unshare(CLONE_NEWUSER) is an unprivileged operation so it still succeeds.
 * The Landlock rule allows it.  For setns, the capability check (CAP_SYS_ADMIN)
 * fails first since the process has no capabilities, yielding EPERM.
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, user_unprivileged) {
	/* clang-format on */
	.namespace_types = CLONE_NEWUSER,
	.is_sandboxed = true,
	.has_rule = true,
	.drop_all_caps = true,
	.expected_result = 0,
};

/*
 * Unsandboxed baseline for non-user namespace: no Landlock domain, process has
 * CAP_SYS_ADMIN.  UTS creation should succeed.
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, uts_unsandboxed) {
	/* clang-format on */
	.namespace_types = CLONE_NEWUTS,
	.is_sandboxed = false,
	.has_rule = false,
	.drop_all_caps = false,
	.expected_result = 0,
};

/*
 * Non-user namespace denied: process has CAP_SYS_ADMIN (passes ns_capable), but
 * Landlock denies (no rule).
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, uts_denied) {
	/* clang-format on */
	.namespace_types = CLONE_NEWUTS,
	.is_sandboxed = true,
	.has_rule = false,
	.drop_all_caps = false,
	.expected_result = EPERM,
};

/*
 * Non-user namespace allowed: process has CAP_SYS_ADMIN and Landlock rule
 * permits CLONE_NEWUTS.
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, uts_allowed) {
	.namespace_types = CLONE_NEWUTS,
	.is_sandboxed = true,
	.has_rule = true,
	.drop_all_caps = false,
	.expected_result = 0,
};
/* clang-format on */

/*
 * Unprivileged namespace creation: process lacks CAP_SYS_ADMIN, so the kernel
 * denies creation regardless of Landlock rules.  Landlock cannot authorize what
 * the kernel denied (LSM hooks are restriction-only).  The rule is present to
 * verify Landlock does not change the error code.
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, uts_unprivileged) {
	/* clang-format on */
	.namespace_types = CLONE_NEWUTS,
	.is_sandboxed = true,
	.has_rule = true,
	.drop_all_caps = true,
	.expected_result = EPERM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, ipc_denied) {
	/* clang-format on */
	.namespace_types = CLONE_NEWIPC,
	.is_sandboxed = true,
	.has_rule = false,
	.drop_all_caps = false,
	.expected_result = EPERM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, ipc_allowed) {
	.namespace_types = CLONE_NEWIPC,
	.is_sandboxed = true,
	.has_rule = true,
	.drop_all_caps = false,
	.expected_result = 0,
};
/* clang-format on */

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, ipc_unprivileged) {
	/* clang-format on */
	.namespace_types = CLONE_NEWIPC,
	.is_sandboxed = true,
	.has_rule = true,
	.drop_all_caps = true,
	.expected_result = EPERM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, mnt_denied) {
	/* clang-format on */
	.namespace_types = CLONE_NEWNS,
	.is_sandboxed = true,
	.has_rule = false,
	.drop_all_caps = false,
	.expected_result = EPERM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, mnt_allowed) {
	.namespace_types = CLONE_NEWNS,
	.is_sandboxed = true,
	.has_rule = true,
	.drop_all_caps = false,
	.expected_result = 0,
};
/* clang-format on */

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, mnt_unprivileged) {
	/* clang-format on */
	.namespace_types = CLONE_NEWNS,
	.is_sandboxed = true,
	.has_rule = true,
	.drop_all_caps = true,
	.expected_result = EPERM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, cgroup_denied) {
	/* clang-format on */
	.namespace_types = CLONE_NEWCGROUP,
	.is_sandboxed = true,
	.has_rule = false,
	.drop_all_caps = false,
	.expected_result = EPERM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, cgroup_allowed) {
	/* clang-format on */
	.namespace_types = CLONE_NEWCGROUP,
	.is_sandboxed = true,
	.has_rule = true,
	.drop_all_caps = false,
	.expected_result = 0,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, cgroup_unprivileged) {
	/* clang-format on */
	.namespace_types = CLONE_NEWCGROUP,
	.is_sandboxed = true,
	.has_rule = true,
	.drop_all_caps = true,
	.expected_result = EPERM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, pid_denied) {
	/* clang-format on */
	.namespace_types = CLONE_NEWPID,
	.is_sandboxed = true,
	.has_rule = false,
	.drop_all_caps = false,
	.expected_result = EPERM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, pid_allowed) {
	.namespace_types = CLONE_NEWPID,
	.is_sandboxed = true,
	.has_rule = true,
	.drop_all_caps = false,
	.expected_result = 0,
};
/* clang-format on */

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, pid_unprivileged) {
	/* clang-format on */
	.namespace_types = CLONE_NEWPID,
	.is_sandboxed = true,
	.has_rule = true,
	.drop_all_caps = true,
	.expected_result = EPERM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, net_denied) {
	/* clang-format on */
	.namespace_types = CLONE_NEWNET,
	.is_sandboxed = true,
	.has_rule = false,
	.drop_all_caps = false,
	.expected_result = EPERM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, net_allowed) {
	.namespace_types = CLONE_NEWNET,
	.is_sandboxed = true,
	.has_rule = true,
	.drop_all_caps = false,
	.expected_result = 0,
};
/* clang-format on */

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, net_unprivileged) {
	/* clang-format on */
	.namespace_types = CLONE_NEWNET,
	.is_sandboxed = true,
	.has_rule = true,
	.drop_all_caps = true,
	.expected_result = EPERM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, time_denied) {
	/* clang-format on */
	.namespace_types = CLONE_NEWTIME,
	.is_sandboxed = true,
	.has_rule = false,
	.drop_all_caps = false,
	.expected_result = EPERM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, time_allowed) {
	/* clang-format on */
	.namespace_types = CLONE_NEWTIME,
	.is_sandboxed = true,
	.has_rule = true,
	.drop_all_caps = false,
	.expected_result = 0,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create, time_unprivileged) {
	/* clang-format on */
	.namespace_types = CLONE_NEWTIME,
	.is_sandboxed = true,
	.has_rule = true,
	.drop_all_caps = true,
	.expected_result = EPERM,
};

FIXTURE_SETUP(ns_create)
{
	ASSERT_TRUE(ns_is_supported(variant->namespace_types, self->proc_path,
				    sizeof(self->proc_path)))
	{
		TH_LOG("Namespace type 0x%llx not supported",
		       (unsigned long long)variant->namespace_types);
	}

	if (variant->drop_all_caps)
		drop_caps(_metadata);
	else
		disable_caps(_metadata);
}

FIXTURE_TEARDOWN(ns_create)
{
}

TEST_F(ns_create, unshare)
{
	int ruleset_fd, err;

	if (variant->is_sandboxed) {
		ruleset_fd = create_ns_ruleset();
		ASSERT_LE(0, ruleset_fd);

		if (variant->has_rule)
			ASSERT_EQ(0, add_ns_rule(ruleset_fd,
						 variant->namespace_types));

		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	/*
	 * Non-user namespaces need CAP_SYS_ADMIN for the privileged path.  User
	 * namespaces and unprivileged tests skip this.
	 */
	if (!variant->drop_all_caps &&
	    variant->namespace_types != CLONE_NEWUSER)
		set_cap(_metadata, CAP_SYS_ADMIN);

	err = unshare(variant->namespace_types);
	if (variant->expected_result) {
		EXPECT_EQ(-1, err);
		EXPECT_EQ(variant->expected_result, errno);
	} else {
		EXPECT_EQ(0, err);
	}

	if (!variant->drop_all_caps &&
	    variant->namespace_types != CLONE_NEWUSER)
		clear_cap(_metadata, CAP_SYS_ADMIN);
}

/*
 * clone3 exercises a different kernel entry point than unshare: it goes through
 * kernel_clone() -> copy_process() -> copy_namespaces() ->
 * create_new_namespaces().  Both paths converge at __ns_common_init() ->
 * security_namespace_init(), but the entry point and argument handling differ.
 */
TEST_F(ns_create, clone3)
{
	int ruleset_fd, status;
	pid_t pid;
	struct clone_args args = {};

	if (variant->is_sandboxed) {
		ruleset_fd = create_ns_ruleset();
		ASSERT_LE(0, ruleset_fd);

		if (variant->has_rule)
			ASSERT_EQ(0, add_ns_rule(ruleset_fd,
						 variant->namespace_types));

		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	if (!variant->drop_all_caps &&
	    variant->namespace_types != CLONE_NEWUSER)
		set_cap(_metadata, CAP_SYS_ADMIN);

	args.flags = variant->namespace_types;
	args.exit_signal = SIGCHLD;
	pid = sys_clone3(&args, sizeof(args));
	if (pid == 0)
		_exit(EXIT_SUCCESS);

	if (variant->expected_result) {
		EXPECT_EQ(-1, pid);
		EXPECT_EQ(variant->expected_result, errno);
	} else {
		ASSERT_LE(0, pid);
		ASSERT_EQ(pid, waitpid(pid, &status, 0));
		ASSERT_EQ(1, WIFEXITED(status));
		ASSERT_EQ(EXIT_SUCCESS, WEXITSTATUS(status));
	}

	if (!variant->drop_all_caps &&
	    variant->namespace_types != CLONE_NEWUSER)
		clear_cap(_metadata, CAP_SYS_ADMIN);
}

/*
 * setns exercises the namespace install path: validate_ns() ->
 * security_namespace_install() -> hook_namespace_install().  This is a
 * different LSM hook than creation, so it must be tested separately for each
 * type.
 *
 * Mount namespace setns requires both CAP_SYS_ADMIN and CAP_SYS_CHROOT (checked
 * by mntns_install), so the allowed variant sets both.
 */
TEST_F(ns_create, setns)
{
	int ruleset_fd, ns_fd, err, expected;

	/*
	 * setns into the process's own user NS returns EINVAL from
	 * userns_install() (rejects re-entry), but when Landlock denies the
	 * operation, security_namespace_install() returns EPERM before
	 * userns_install() runs.
	 */
	if (variant->namespace_types == CLONE_NEWUSER &&
	    !variant->expected_result) {
		expected = EINVAL;
	} else {
		expected = variant->expected_result;
	}

	/* Open the NS FD before enforcing the domain. */
	ns_fd = open(self->proc_path, O_RDONLY);
	ASSERT_LE(0, ns_fd);

	if (variant->is_sandboxed) {
		ruleset_fd = create_ns_ruleset();
		ASSERT_LE(0, ruleset_fd);

		if (variant->has_rule)
			ASSERT_EQ(0, add_ns_rule(ruleset_fd,
						 variant->namespace_types));

		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	if (!variant->drop_all_caps) {
		set_cap(_metadata, CAP_SYS_ADMIN);
		/*
		 * mntns_install() requires CAP_SYS_CHROOT in addition to
		 * CAP_SYS_ADMIN.
		 */
		if (variant->namespace_types == CLONE_NEWNS)
			set_cap(_metadata, CAP_SYS_CHROOT);
	}

	err = setns(ns_fd, variant->namespace_types);
	if (expected) {
		EXPECT_EQ(-1, err);
		EXPECT_EQ(expected, errno);
	} else {
		EXPECT_EQ(0, err);
	}

	if (!variant->drop_all_caps) {
		clear_cap(_metadata, CAP_SYS_ADMIN);
		if (variant->namespace_types == CLONE_NEWNS)
			clear_cap(_metadata, CAP_SYS_CHROOT);
	}

	EXPECT_EQ(0, close(ns_fd));
}

/* Additional namespace creation tests */

/*
 * When LANDLOCK_PERMISSION_NAMESPACE_USE is not handled by any domain,
 * namespace creation must produce the same result as without Landlock.  Unlike
 * the unsandboxed variants of ns_create (which have no domain at all), this
 * test verifies that a domain handling only FS access does not interfere with
 * namespace operations.
 */
TEST(ns_create_unhandled)
{
	const struct landlock_ruleset_attr attr = {
		.handled_access_fs = LANDLOCK_ACCESS_FS_READ_FILE,
	};
	int ruleset_fd;

	disable_caps(_metadata);

	ruleset_fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
	ASSERT_LE(0, ruleset_fd);

	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	/* User namespace creation should still work (unhandled). */
	EXPECT_EQ(0, unshare(CLONE_NEWUSER));
}

/*
 * Layer stacking: both layers must allow CLONE_NEWUSER for the operation to
 * succeed.  Variants exercise the three combinations of per-layer allow/deny
 * that exercise distinct semantics; the (deny, deny) combination is omitted
 * because it is covered by every other "deny" test in this file.
 */
/* clang-format off */
FIXTURE(ns_stacking) {};
/* clang-format on */

FIXTURE_VARIANT(ns_stacking)
{
	bool first_layer_allows;
	bool second_layer_allows;
};

/* Layer 1 allows, layer 2 denies -> child denies. */
/* clang-format off */
FIXTURE_VARIANT_ADD(ns_stacking, deny) {
	/* clang-format on */
	.first_layer_allows = true,
	.second_layer_allows = false,
};

/* Both layers allow CLONE_NEWUSER -> operation succeeds. */
/* clang-format off */
FIXTURE_VARIANT_ADD(ns_stacking, allow) {
	/* clang-format on */
	.first_layer_allows = true,
	.second_layer_allows = true,
};

/*
 * Layer 1 denies, layer 2 allows -> still denied: a child layer cannot grant
 * what an ancestor layer withheld.  This complements the
 * parent-allows/child-denies variant above; together they verify the walker
 * checks both layers and accepts only the (allow, allow) cell.
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(ns_stacking, parent_denies) {
	/* clang-format on */
	.first_layer_allows = false,
	.second_layer_allows = true,
};

FIXTURE_SETUP(ns_stacking)
{
	disable_caps(_metadata);
}

FIXTURE_TEARDOWN(ns_stacking)
{
}

/*
 * Verify that any layer can deny an operation: enforcement requires all layers
 * to allow.  Variants exercise the three combinations that exercise distinct
 * walker paths (allow/deny, allow/allow, deny/allow); only allow/allow lets the
 * operation through.
 */
TEST_F(ns_stacking, two_layers)
{
	int ruleset_fd;
	const bool expect_success = variant->first_layer_allows &&
				    variant->second_layer_allows;

	/* First layer: allow or deny depending on variant. */
	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	if (variant->first_layer_allows)
		ASSERT_EQ(0, add_ns_rule(ruleset_fd, CLONE_NEWUSER));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	/* Second layer: allow or deny depending on variant. */
	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	if (variant->second_layer_allows)
		ASSERT_EQ(0, add_ns_rule(ruleset_fd, CLONE_NEWUSER));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	if (expect_success) {
		EXPECT_EQ(0, unshare(CLONE_NEWUSER));
	} else {
		EXPECT_EQ(-1, unshare(CLONE_NEWUSER));
		EXPECT_EQ(EPERM, errno);
	}
}

/*
 * Combined capability and namespace permissions in a single domain.  Verifies
 * that both permission types can coexist and are enforced independently.
 */
TEST(combined_cap_ns)
{
	const struct landlock_ruleset_attr attr = {
		.handled_permissions = LANDLOCK_PERMISSION_CAPABILITY_USE |
				       LANDLOCK_PERMISSION_NAMESPACE_USE,
	};
	const struct landlock_capability_attr cap_attr = {
		.permissions = LANDLOCK_PERMISSION_CAPABILITY_USE,
		.allowed_capabilities = (1ULL << CAP_SYS_ADMIN),
	};
	const struct landlock_namespace_attr ns_attr = {
		.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE,
		.allowed_namespace_types = CLONE_NEWUSER,
	};
	int ruleset_fd;

	/* Isolate hostname changes from other tests. */
	ASSERT_EQ(0, unshare(CLONE_NEWUTS));

	disable_caps(_metadata);

	ruleset_fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
	ASSERT_LE(0, ruleset_fd);

	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_CAPABILITY,
				       &cap_attr, 0));
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
				       &ns_attr, 0));

	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	/* CAP_SYS_ADMIN use allowed by capability rule. */
	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, sethostname("test", 4));
	clear_cap(_metadata, CAP_SYS_ADMIN);

	/* CAP_SYS_CHROOT denied (not in allowed capability rules). */
	set_cap(_metadata, CAP_SYS_CHROOT);
	EXPECT_EQ(-1, chroot("/"));
	EXPECT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_CHROOT);

	/*
	 * UTS namespace creation denied by Landlock (not in allowed namespace
	 * rules).  CAP_SYS_ADMIN is needed for the kernel's ns_capable() check
	 * to pass, so that Landlock's hook is actually reached.
	 */
	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(-1, unshare(CLONE_NEWUTS));
	EXPECT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_ADMIN);

	/* User namespace creation allowed by namespace rule. */
	EXPECT_EQ(0, unshare(CLONE_NEWUSER));
}

/*
 * Partial allow: one namespace type is allowed, another is denied.  Verifies
 * that rules are per-type.
 */
TEST(ns_create_partial)
{
	int ruleset_fd;

	disable_caps(_metadata);

	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);

	/* Only allow UTS namespace creation. */
	ASSERT_EQ(0, add_ns_rule(ruleset_fd, CLONE_NEWUTS));

	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	/* UTS namespace should be allowed. */
	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, unshare(CLONE_NEWUTS));
	clear_cap(_metadata, CAP_SYS_ADMIN);

	/* User namespace should be denied (no rule). */
	EXPECT_EQ(-1, unshare(CLONE_NEWUSER));
	EXPECT_EQ(EPERM, errno);
}

/*
 * open_tree(2) and fsmount(2) acquire a file descriptor referring to a
 * newly-created mount namespace.  Both call paths funnel into
 * security_namespace_init() with CLONE_NEWNS, gated by
 * LANDLOCK_PERMISSION_NAMESPACE_USE.  Without coverage here, regressions in
 * those paths would slip past the suite.
 */
/* clang-format off */
FIXTURE(ns_mount_fd) {};
/* clang-format on */

FIXTURE_VARIANT(ns_mount_fd)
{
	bool sandboxed;
	bool has_rule;
	int expected_errno;
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_mount_fd, denied) {
	/* clang-format on */
	.sandboxed = true,
	.has_rule = false,
	.expected_errno = EPERM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_mount_fd, allowed) {
	/* clang-format on */
	.sandboxed = true,
	.has_rule = true,
	.expected_errno = 0,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_mount_fd, unsandboxed) {
	/* clang-format on */
	.sandboxed = false,
	.has_rule = false,
	.expected_errno = 0,
};

FIXTURE_SETUP(ns_mount_fd)
{
}

FIXTURE_TEARDOWN(ns_mount_fd)
{
}

/*
 * open_tree(OPEN_TREE_CLONE) creates an anonymous mount namespace to hold the
 * cloned mount tree.  hook_namespace_init() fires with CLONE_NEWNS.
 */
TEST_F(ns_mount_fd, open_tree_clone)
{
	int ruleset_fd, fd;

	disable_caps(_metadata);

	if (variant->sandboxed) {
		ruleset_fd = create_ns_ruleset();
		ASSERT_LE(0, ruleset_fd);
		if (variant->has_rule)
			ASSERT_EQ(0, add_ns_rule(ruleset_fd, CLONE_NEWNS));
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	set_cap(_metadata, CAP_SYS_ADMIN);
	fd = sys_open_tree(AT_FDCWD, "/",
			   OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC | AT_RECURSIVE);
	if (variant->expected_errno) {
		EXPECT_EQ(-1, fd);
		EXPECT_EQ(variant->expected_errno, errno);
	} else {
		ASSERT_LE(0, fd);
		EXPECT_EQ(0, close(fd));
	}
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

/*
 * open_tree(OPEN_TREE_NAMESPACE) clones the mount tree into a new
 * (non-anonymous) mount namespace.  Same hook (CLONE_NEWNS) but a different
 * code path inside fs/namespace.c (open_new_namespace -> alloc_mnt_ns).
 * OPEN_TREE_NAMESPACE and OPEN_TREE_CLONE are mutually exclusive.
 */
TEST_F(ns_mount_fd, open_tree_namespace)
{
	int ruleset_fd, fd;

	disable_caps(_metadata);

	if (variant->sandboxed) {
		ruleset_fd = create_ns_ruleset();
		ASSERT_LE(0, ruleset_fd);
		if (variant->has_rule)
			ASSERT_EQ(0, add_ns_rule(ruleset_fd, CLONE_NEWNS));
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	set_cap(_metadata, CAP_SYS_ADMIN);
	fd = sys_open_tree(AT_FDCWD, "/",
			   OPEN_TREE_NAMESPACE | OPEN_TREE_CLOEXEC |
				   AT_RECURSIVE);
	if (variant->expected_errno) {
		EXPECT_EQ(-1, fd);
		EXPECT_EQ(variant->expected_errno, errno);
	} else {
		ASSERT_LE(0, fd);
		EXPECT_EQ(0, close(fd));
	}
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

/*
 * fsmount(2) without FSMOUNT_NAMESPACE creates an anonymous mount namespace to
 * attach the new superblock.  hook_namespace_init() fires with CLONE_NEWNS.
 * The fs context (fsopen + fsconfig) is set up before sandboxing because
 * Landlock here only handles the namespace permission.
 */
TEST_F(ns_mount_fd, fsmount_default)
{
	int ruleset_fd, fs_fd, mnt_fd;

	disable_caps(_metadata);

	set_cap(_metadata, CAP_SYS_ADMIN);
	fs_fd = sys_fsopen("tmpfs", 0);
	ASSERT_LE(0, fs_fd);
	ASSERT_EQ(0, sys_fsconfig(fs_fd, FSCONFIG_CMD_CREATE, NULL, NULL, 0));

	if (variant->sandboxed) {
		ruleset_fd = create_ns_ruleset();
		ASSERT_LE(0, ruleset_fd);
		if (variant->has_rule)
			ASSERT_EQ(0, add_ns_rule(ruleset_fd, CLONE_NEWNS));
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	mnt_fd = sys_fsmount(fs_fd, FSMOUNT_CLOEXEC, 0);
	if (variant->expected_errno) {
		EXPECT_EQ(-1, mnt_fd);
		EXPECT_EQ(variant->expected_errno, errno);
	} else {
		ASSERT_LE(0, mnt_fd);
		EXPECT_EQ(0, close(mnt_fd));
	}
	EXPECT_EQ(0, close(fs_fd));
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

/*
 * fsmount(2) with FSMOUNT_NAMESPACE creates a (non-anonymous) mount namespace
 * for the new mount.  Same hook as the default path, different code path.
 */
TEST_F(ns_mount_fd, fsmount_namespace)
{
	int ruleset_fd, fs_fd, mnt_fd;

	disable_caps(_metadata);

	set_cap(_metadata, CAP_SYS_ADMIN);
	fs_fd = sys_fsopen("tmpfs", 0);
	ASSERT_LE(0, fs_fd);
	ASSERT_EQ(0, sys_fsconfig(fs_fd, FSCONFIG_CMD_CREATE, NULL, NULL, 0));

	if (variant->sandboxed) {
		ruleset_fd = create_ns_ruleset();
		ASSERT_LE(0, ruleset_fd);
		if (variant->has_rule)
			ASSERT_EQ(0, add_ns_rule(ruleset_fd, CLONE_NEWNS));
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	mnt_fd = sys_fsmount(fs_fd, FSMOUNT_CLOEXEC | FSMOUNT_NAMESPACE, 0);
	if (variant->expected_errno) {
		EXPECT_EQ(-1, mnt_fd);
		EXPECT_EQ(variant->expected_errno, errno);
	} else {
		ASSERT_LE(0, mnt_fd);
		EXPECT_EQ(0, close(mnt_fd));
	}
	EXPECT_EQ(0, close(fs_fd));
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

/*
 * unshare(2) with multiple CLONE_NEW* flags: when
 * LANDLOCK_PERMISSION_NAMESPACE_USE denies any of the requested types, the
 * entire syscall fails with EPERM.  This documents the kernel's atomic
 * behavior: create_new_namespaces(), called by copy_namespaces(), creates
 * namespaces sequentially, and the first Landlock denial rolls back the whole
 * operation.  Mixing CLONE_NEWUSER (no capability check) with another
 * CLONE_NEW* type is the typical container-runtime bootstrap pattern.
 */
/* clang-format off */
FIXTURE(ns_create_multi_flag) {};
/* clang-format on */

FIXTURE_VARIANT(ns_create_multi_flag)
{
	__u64 allowed_types;
	int expected_errno;
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create_multi_flag, partial_denied) {
	/* clang-format on */
	/* User namespace allowed; UTS namespace denied. */
	.allowed_types = CLONE_NEWUSER,
	.expected_errno = EPERM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_create_multi_flag, both_allowed) {
	/* clang-format on */
	.allowed_types = CLONE_NEWUSER | CLONE_NEWUTS,
	.expected_errno = 0,
};

FIXTURE_SETUP(ns_create_multi_flag)
{
}

FIXTURE_TEARDOWN(ns_create_multi_flag)
{
}

TEST_F(ns_create_multi_flag, unshare)
{
	int ruleset_fd, status, err;
	pid_t child;

	disable_caps(_metadata);

	/* Run unshare(2) in a child to avoid polluting the test process. */
	child = fork();
	ASSERT_LE(0, child);

	if (child == 0) {
		ruleset_fd = create_ns_ruleset();
		ASSERT_LE(0, ruleset_fd);
		ASSERT_EQ(0, add_ns_rule(ruleset_fd, variant->allowed_types));
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));

		err = unshare(CLONE_NEWUSER | CLONE_NEWUTS);
		if (variant->expected_errno) {
			EXPECT_EQ(-1, err);
			EXPECT_EQ(variant->expected_errno, errno);
		} else {
			EXPECT_EQ(0, err);
		}
		_exit(_metadata->exit_code);
	}

	ASSERT_EQ(child, waitpid(child, &status, 0));
	ASSERT_EQ(1, WIFEXITED(status));
	ASSERT_EQ(EXIT_SUCCESS, WEXITSTATUS(status));
}

/* clang-format off */
FIXTURE(setns_cross_process) {};
/* clang-format on */

FIXTURE_VARIANT(setns_cross_process)
{
	bool is_sandboxed;
	bool has_rule;
	int expected_setns;
};

/* clang-format off */
FIXTURE_VARIANT_ADD(setns_cross_process, denied) {
	/* clang-format on */
	.is_sandboxed = true,
	.has_rule = false,
	.expected_setns = EPERM,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(setns_cross_process, sandboxed_allowed) {
	/* clang-format on */
	.is_sandboxed = true,
	.has_rule = true,
	.expected_setns = 0,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(setns_cross_process, unsandboxed) {
	/* clang-format on */
	.is_sandboxed = false,
	.has_rule = false,
	.expected_setns = 0,
};

FIXTURE_SETUP(setns_cross_process)
{
}

FIXTURE_TEARDOWN(setns_cross_process)
{
}

/*
 * setns into a child's UTS namespace: when sandboxed with
 * LANDLOCK_PERMISSION_NAMESPACE_USE denying UTS, the rule-based check applies
 * regardless of which process created the namespace.  This fixture exercises
 * only CLONE_NEWUTS; the same enforcement applies to every namespace type (see
 * hook_namespace_install() in security/landlock/ns.c), so per-type variants
 * would not exercise different code paths.
 */
TEST_F(setns_cross_process, setns)
{
	int ruleset_fd, ns_fd, status;
	pid_t child;
	int pipe_parent[2], pipe_child[2];
	char buf, path[64];

	disable_caps(_metadata);

	/*
	 * Enable dumpable so the parent can read /proc/<child>/ns/uts.  Without
	 * this, ptrace access checks (PTRACE_MODE_READ) prevent opening another
	 * process's namespace entries.
	 */
	ASSERT_EQ(0, prctl(PR_SET_DUMPABLE, 1, 0, 0, 0));

	ASSERT_EQ(0, pipe2(pipe_parent, O_CLOEXEC));
	ASSERT_EQ(0, pipe2(pipe_child, O_CLOEXEC));

	child = fork();
	ASSERT_LE(0, child);

	if (child == 0) {
		EXPECT_EQ(0, close(pipe_parent[1]));
		EXPECT_EQ(0, close(pipe_child[0]));

		/* Child: create a UTS namespace. */
		set_cap(_metadata, CAP_SYS_ADMIN);
		ASSERT_EQ(0, unshare(CLONE_NEWUTS));

		drop_caps(_metadata);
		ASSERT_EQ(0, prctl(PR_SET_DUMPABLE, 1, 0, 0, 0));

		/* Signal parent that the namespace is ready. */
		ASSERT_EQ(1, write(pipe_child[1], ".", 1));

		/* Wait for parent to finish testing. */
		ASSERT_EQ(1, read(pipe_parent[0], &buf, 1));
		_exit(_metadata->exit_code);
	}

	EXPECT_EQ(0, close(pipe_parent[0]));
	EXPECT_EQ(0, close(pipe_child[1]));

	/* Wait for child namespace. */
	ASSERT_EQ(1, read(pipe_child[0], &buf, 1));
	EXPECT_EQ(0, close(pipe_child[0]));

	/* Open the child's NS FD BEFORE creating the domain. */
	snprintf(path, sizeof(path), "/proc/%d/ns/uts", child);
	ns_fd = open(path, O_RDONLY);
	ASSERT_LE(0, ns_fd);

	if (variant->is_sandboxed) {
		ruleset_fd = create_ns_ruleset();
		ASSERT_LE(0, ruleset_fd);
		if (variant->has_rule)
			ASSERT_EQ(0, add_ns_rule(ruleset_fd, CLONE_NEWUTS));
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));
	}

	set_cap(_metadata, CAP_SYS_ADMIN);
	if (variant->expected_setns) {
		EXPECT_EQ(-1, setns(ns_fd, CLONE_NEWUTS));
		EXPECT_EQ(variant->expected_setns, errno);
	} else {
		EXPECT_EQ(0, setns(ns_fd, CLONE_NEWUTS));
	}
	clear_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, close(ns_fd));

	/* Release child. */
	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));
	EXPECT_EQ(0, close(pipe_parent[1]));
	ASSERT_EQ(child, waitpid(child, &status, 0));
	ASSERT_EQ(1, WIFEXITED(status));
	ASSERT_EQ(EXIT_SUCCESS, WEXITSTATUS(status));
}

/*
 * Verify that LANDLOCK_PERMISSION_NAMESPACE_USE and
 * LANDLOCK_PERMISSION_CAPABILITY_USE apply simultaneously: creating/entering a
 * non-user namespace requires both the namespace type to be allowed AND
 * CAP_SYS_ADMIN to be allowed.  User namespace creation is the exception (no
 * capable() call from the kernel).
 */
TEST(setns_and_create)
{
	int ruleset_fd, ns_fd;
	const struct landlock_ruleset_attr attr = {
		.handled_permissions = LANDLOCK_PERMISSION_NAMESPACE_USE |
				       LANDLOCK_PERMISSION_CAPABILITY_USE,
	};
	const struct landlock_namespace_attr ns_attr = {
		.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE,
		.allowed_namespace_types = CLONE_NEWUTS,
	};
	const struct landlock_capability_attr cap_attr = {
		.permissions = LANDLOCK_PERMISSION_CAPABILITY_USE,
		.allowed_capabilities = (1ULL << CAP_SYS_ADMIN),
	};

	disable_caps(_metadata);

	ruleset_fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
				       &ns_attr, 0));
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_CAPABILITY,
				       &cap_attr, 0));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	/* UTS unshare: allowed by NS rule + CAP_SYS_ADMIN allowed. */
	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0, unshare(CLONE_NEWUTS));

	/* IPC unshare: denied by NS rule (type not allowed). */
	EXPECT_EQ(-1, unshare(CLONE_NEWIPC));
	EXPECT_EQ(EPERM, errno);

	/* setns into current UTS: allowed by NS rule. */
	ns_fd = open("/proc/self/ns/uts", O_RDONLY);
	ASSERT_LE(0, ns_fd);
	EXPECT_EQ(0, setns(ns_fd, CLONE_NEWUTS));
	clear_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, close(ns_fd));

	/*
	 * User namespace creation: only LANDLOCK_PERMISSION_NAMESPACE_USE
	 * needed (no capable() call from the kernel for user NS).  Denied
	 * because CLONE_NEWUSER is not in the allowed namespace types.
	 */
	EXPECT_EQ(-1, unshare(CLONE_NEWUSER));
	EXPECT_EQ(EPERM, errno);
}

/*
 * Verify that LANDLOCK_PERMISSION_CAPABILITY_USE can deny the CAP_SYS_ADMIN
 * check that the kernel performs before the Landlock namespace hook is reached.
 * The NS type is allowed but the required capability is not, so the operation
 * fails on the capability check.
 *
 * User namespace creation is the exception: no capable() call, so the operation
 * succeeds with just LANDLOCK_PERMISSION_NAMESPACE_USE.
 */
TEST(two_permissions_cap_denied)
{
	const struct landlock_ruleset_attr attr = {
		.handled_permissions = LANDLOCK_PERMISSION_NAMESPACE_USE |
				       LANDLOCK_PERMISSION_CAPABILITY_USE,
	};
	const struct landlock_namespace_attr ns_attr = {
		.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE,
		.allowed_namespace_types = CLONE_NEWUTS | CLONE_NEWUSER,
	};
	/* CAP_SYS_ADMIN is NOT allowed. */
	const struct landlock_capability_attr cap_attr = {
		.permissions = LANDLOCK_PERMISSION_CAPABILITY_USE,
		.allowed_capabilities = (1ULL << CAP_SYS_CHROOT),
	};
	int ruleset_fd, ns_fd;

	disable_caps(_metadata);

	ruleset_fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
				       &ns_attr, 0));
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_CAPABILITY,
				       &cap_attr, 0));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	/*
	 * UTS creation: the process holds CAP_SYS_ADMIN but Landlock denies it
	 * (not in the cap rule), so the kernel's ns_capable(CAP_SYS_ADMIN) gate
	 * fails before the namespace hook is reached.
	 */
	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(-1, unshare(CLONE_NEWUTS));
	EXPECT_EQ(EPERM, errno);

	/*
	 * setns into the current UTS namespace: the type is allowed but
	 * CAP_SYS_ADMIN is denied, so the kernel's ns_capable(CAP_SYS_ADMIN)
	 * gate fails before the namespace hook is reached (the setns capability
	 * leg, complementing the unshare/create leg above).
	 */
	ns_fd = open("/proc/self/ns/uts", O_RDONLY);
	ASSERT_LE(0, ns_fd);
	EXPECT_EQ(-1, setns(ns_fd, CLONE_NEWUTS));
	EXPECT_EQ(EPERM, errno);
	EXPECT_EQ(0, close(ns_fd));
	clear_cap(_metadata, CAP_SYS_ADMIN);

	/*
	 * User NS creation: no capable() call from the kernel, so only
	 * LANDLOCK_PERMISSION_NAMESPACE_USE applies.  CLONE_NEWUSER is in the
	 * allowed set, so this succeeds.
	 */
	EXPECT_EQ(0, unshare(CLONE_NEWUSER));
}

/*
 * Combining CLONE_NEWUSER with another namespace type in a single unshare(2)
 * call does not exempt the non-user namespace from the capability check: the
 * kernel checks ns_capable(new_user_ns, CAP_SYS_ADMIN) for it, and the
 * namespace-agnostic capability hook denies it when the domain handles
 * LANDLOCK_PERMISSION_CAPABILITY_USE without allowing CAP_SYS_ADMIN.  Both
 * namespace types are allowed here, yet the combined call fails atomically,
 * exactly like the equivalent two-call sequence.
 */
TEST(two_permissions_combined_unshare_denied)
{
	const struct landlock_ruleset_attr attr = {
		.handled_permissions = LANDLOCK_PERMISSION_NAMESPACE_USE |
				       LANDLOCK_PERMISSION_CAPABILITY_USE,
	};
	/* Both namespace types are allowed; no capability is allowed. */
	const struct landlock_namespace_attr ns_attr = {
		.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE,
		.allowed_namespace_types = CLONE_NEWUSER | CLONE_NEWUTS,
	};
	int ruleset_fd;

	disable_caps(_metadata);

	ruleset_fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
				       &ns_attr, 0));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	/*
	 * CAP_SYS_ADMIN is held so the kernel's commoncap check passes (via
	 * ownership of the new user namespace) and the denial is attributable
	 * to Landlock.  The UTS leg needs CAP_SYS_ADMIN, which the capability
	 * hook denies, so the atomic unshare(2) fails and no namespace is
	 * created.
	 */
	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(-1, unshare(CLONE_NEWUSER | CLONE_NEWUTS));
	EXPECT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_ADMIN);

	/* The user namespace alone has no capability check, so it succeeds. */
	EXPECT_EQ(0, unshare(CLONE_NEWUSER));
}

/*
 * Positive complement to two_permissions_combined_unshare_denied: allowing
 * CAP_SYS_ADMIN lets the same combined unshare(CLONE_NEWUSER | CLONE_NEWUTS)
 * succeed, proving the denial above comes from the missing capability allowance
 * and not the namespace rule.  Run in a child because a successful unshare(2)
 * mutates the caller.
 */
TEST(two_permissions_combined_unshare_allowed)
{
	const struct landlock_ruleset_attr attr = {
		.handled_permissions = LANDLOCK_PERMISSION_NAMESPACE_USE |
				       LANDLOCK_PERMISSION_CAPABILITY_USE,
	};
	const struct landlock_namespace_attr ns_attr = {
		.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE,
		.allowed_namespace_types = CLONE_NEWUSER | CLONE_NEWUTS,
	};
	const struct landlock_capability_attr cap_attr = {
		.permissions = LANDLOCK_PERMISSION_CAPABILITY_USE,
		.allowed_capabilities = (1ULL << CAP_SYS_ADMIN),
	};
	int ruleset_fd, status;
	pid_t child;

	disable_caps(_metadata);

	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		ruleset_fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
		ASSERT_LE(0, ruleset_fd);
		ASSERT_EQ(0,
			  landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
					    &ns_attr, 0));
		ASSERT_EQ(0, landlock_add_rule(ruleset_fd,
					       LANDLOCK_RULE_CAPABILITY,
					       &cap_attr, 0));
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));

		set_cap(_metadata, CAP_SYS_ADMIN);
		EXPECT_EQ(0, unshare(CLONE_NEWUSER | CLONE_NEWUTS));
		clear_cap(_metadata, CAP_SYS_ADMIN);
		_exit(_metadata->exit_code);
	}
	ASSERT_EQ(child, waitpid(child, &status, 0));
	ASSERT_EQ(1, WIFEXITED(status));
	ASSERT_EQ(EXIT_SUCCESS, WEXITSTATUS(status));
}

/*
 * Inverse of two_permissions_cap_denied: the required capability is allowed but
 * the namespace type is not, so the Landlock namespace hook denies the
 * operation (the capability leg passes).  A positive control (an allowed type
 * creates successfully with the same allowed capability) proves CAP_SYS_ADMIN
 * is genuinely exercisable, so the UTS denial is attributable to the namespace
 * hook rather than the capability hook.
 */
TEST(two_permissions_ns_denied)
{
	const struct landlock_ruleset_attr attr = {
		.handled_permissions = LANDLOCK_PERMISSION_NAMESPACE_USE |
				       LANDLOCK_PERMISSION_CAPABILITY_USE,
	};
	/* CLONE_NEWUTS is NOT allowed; CLONE_NEWNET is. */
	const struct landlock_namespace_attr ns_attr = {
		.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE,
		.allowed_namespace_types = CLONE_NEWNET,
	};
	const struct landlock_capability_attr cap_attr = {
		.permissions = LANDLOCK_PERMISSION_CAPABILITY_USE,
		.allowed_capabilities = (1ULL << CAP_SYS_ADMIN),
	};
	int ruleset_fd;

	disable_caps(_metadata);

	ruleset_fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
				       &ns_attr, 0));
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_CAPABILITY,
				       &cap_attr, 0));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	/*
	 * Positive control: CLONE_NEWNET is allowed and CAP_SYS_ADMIN is
	 * allowed, so the kernel's ns_capable() gate passes and the namespace
	 * hook allows it.
	 */
	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, unshare(CLONE_NEWNET));

	/*
	 * CAP_SYS_ADMIN is allowed (ns_capable passes), but CLONE_NEWUTS is not
	 * in the allowed namespace types, so the Landlock namespace hook denies
	 * it.
	 */
	EXPECT_EQ(-1, unshare(CLONE_NEWUTS));
	EXPECT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

/*
 * Mount namespace setns is unique: the kernel checks both CAP_SYS_ADMIN and
 * CAP_SYS_CHROOT in mntns_install().  Verify that allowing CAP_SYS_ADMIN alone
 * is not sufficient.
 */
TEST(two_permissions_mnt_setns)
{
	const struct landlock_ruleset_attr attr = {
		.handled_permissions = LANDLOCK_PERMISSION_NAMESPACE_USE |
				       LANDLOCK_PERMISSION_CAPABILITY_USE,
	};
	const struct landlock_namespace_attr ns_attr = {
		.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE,
		.allowed_namespace_types = CLONE_NEWNS,
	};
	const struct landlock_capability_attr cap_admin = {
		.permissions = LANDLOCK_PERMISSION_CAPABILITY_USE,
		.allowed_capabilities = (1ULL << CAP_SYS_ADMIN),
	};
	const struct landlock_capability_attr cap_admin_chroot = {
		.permissions = LANDLOCK_PERMISSION_CAPABILITY_USE,
		.allowed_capabilities = (1ULL << CAP_SYS_ADMIN) |
					(1ULL << CAP_SYS_CHROOT),
	};
	int ruleset_fd, ns_fd;

	disable_caps(_metadata);

	/* Layer 1: allow mount NS + CAP_SYS_ADMIN only (no CAP_SYS_CHROOT). */
	ruleset_fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
				       &ns_attr, 0));
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_CAPABILITY,
				       &cap_admin, 0));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	ns_fd = open("/proc/self/ns/mnt", O_RDONLY);
	ASSERT_LE(0, ns_fd);

	/*
	 * Fails: mntns_install() checks CAP_SYS_ADMIN (allowed) then
	 * CAP_SYS_CHROOT (denied by LANDLOCK_PERMISSION_CAPABILITY_USE).
	 */
	set_cap(_metadata, CAP_SYS_ADMIN);
	set_cap(_metadata, CAP_SYS_CHROOT);
	EXPECT_EQ(-1, setns(ns_fd, CLONE_NEWNS));
	EXPECT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_ADMIN);
	clear_cap(_metadata, CAP_SYS_CHROOT);

	/* Layer 2: also allows CAP_SYS_CHROOT. */
	ruleset_fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
				       &ns_attr, 0));
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_CAPABILITY,
				       &cap_admin_chroot, 0));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	/*
	 * Still fails: layer 1 still denies CAP_SYS_CHROOT.  Landlock layer
	 * stacking means the most restrictive layer wins.
	 */
	set_cap(_metadata, CAP_SYS_ADMIN);
	set_cap(_metadata, CAP_SYS_CHROOT);
	EXPECT_EQ(-1, setns(ns_fd, CLONE_NEWNS));
	EXPECT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_ADMIN);
	clear_cap(_metadata, CAP_SYS_CHROOT);
	EXPECT_EQ(0, close(ns_fd));
}

/*
 * Positive complement to two_permissions_mnt_setns: a single layer that allows
 * the mount namespace type AND both CAP_SYS_ADMIN and CAP_SYS_CHROOT lets
 * setns(CLONE_NEWNS) into the process's own mount namespace succeed.  Proves
 * the denial legs above are caused by the withheld CAP_SYS_CHROOT, not an
 * unconditional block of mount setns.
 */
TEST(two_permissions_mnt_setns_allowed)
{
	const struct landlock_ruleset_attr attr = {
		.handled_permissions = LANDLOCK_PERMISSION_NAMESPACE_USE |
				       LANDLOCK_PERMISSION_CAPABILITY_USE,
	};
	const struct landlock_namespace_attr ns_attr = {
		.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE,
		.allowed_namespace_types = CLONE_NEWNS,
	};
	const struct landlock_capability_attr cap_attr = {
		.permissions = LANDLOCK_PERMISSION_CAPABILITY_USE,
		.allowed_capabilities = (1ULL << CAP_SYS_ADMIN) |
					(1ULL << CAP_SYS_CHROOT),
	};
	int ruleset_fd, ns_fd;

	disable_caps(_metadata);

	/* Open the NS FD before enforcing the domain. */
	ns_fd = open("/proc/self/ns/mnt", O_RDONLY);
	ASSERT_LE(0, ns_fd);

	ruleset_fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
				       &ns_attr, 0));
	ASSERT_EQ(0, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_CAPABILITY,
				       &cap_attr, 0));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	/*
	 * mntns_install() checks CAP_SYS_ADMIN then CAP_SYS_CHROOT (both
	 * allowed by the rule) and the mount NS type is allowed, so setns into
	 * the process's own mount namespace succeeds.
	 */
	set_cap(_metadata, CAP_SYS_ADMIN);
	set_cap(_metadata, CAP_SYS_CHROOT);
	EXPECT_EQ(0, setns(ns_fd, CLONE_NEWNS));
	clear_cap(_metadata, CAP_SYS_ADMIN);
	clear_cap(_metadata, CAP_SYS_CHROOT);
	EXPECT_EQ(0, close(ns_fd));
}

/*
 * setns() through a namespace fd inherited across fork() is still governed by
 * the child's own Landlock domain: the child enforces a domain denying
 * CLONE_NEWUTS, then joining the inherited /proc/self/ns/uts fd is denied.
 */
TEST(setns_inherited_fd)
{
	int ns_fd, status;
	pid_t child;

	disable_caps(_metadata);

	/* Open the UTS ns fd before fork() so the child inherits it. */
	ns_fd = open("/proc/self/ns/uts", O_RDONLY);
	ASSERT_LE(0, ns_fd);

	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		int ruleset_fd;

		/*
		 * Baseline: joining the inherited fd works before the domain,
		 * so the later EPERM is attributable to Landlock.
		 */
		set_cap(_metadata, CAP_SYS_ADMIN);
		ASSERT_EQ(0, setns(ns_fd, CLONE_NEWUTS));
		clear_cap(_metadata, CAP_SYS_ADMIN);

		ruleset_fd = create_ns_ruleset();
		ASSERT_LE(0, ruleset_fd);
		/* No rule allows UTS. */
		enforce_ruleset(_metadata, ruleset_fd);
		EXPECT_EQ(0, close(ruleset_fd));

		set_cap(_metadata, CAP_SYS_ADMIN);
		EXPECT_EQ(-1, setns(ns_fd, CLONE_NEWUTS));
		EXPECT_EQ(EPERM, errno);
		clear_cap(_metadata, CAP_SYS_ADMIN);

		_exit(_metadata->exit_code);
		return;
	}

	EXPECT_EQ(0, close(ns_fd));
	ASSERT_EQ(child, waitpid(child, &status, 0));
	if (WIFSIGNALED(status) || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != EXIT_SUCCESS)
		_metadata->exit_code = KSFT_FAIL;
}

/* Audit tests */

static int matches_log_ns_create(int audit_fd, __u64 ns_type)
{
	/* A denied creation logs the ID allocated for the new namespace. */
	static const char log_template[] = REGEX_LANDLOCK_PREFIX
		" blockers=namespace\\.use namespace_type=0x%x namespace_id=[1-9][0-9]*$";
	char log_match[sizeof(log_template) + 10];
	int log_match_len;

	log_match_len = snprintf(log_match, sizeof(log_match), log_template,
				 (unsigned int)ns_type);
	if (log_match_len >= sizeof(log_match))
		return -E2BIG;

	return audit_match_record(audit_fd, AUDIT_LANDLOCK_ACCESS, log_match,
				  NULL);
}

static int matches_log_ns_setns(int audit_fd, __u64 ns_type, __u64 ns_id)
{
	static const char log_template[] = REGEX_LANDLOCK_PREFIX
		" blockers=namespace\\.use namespace_type=0x%x namespace_id=%llu$";
	char log_match[sizeof(log_template) + 32];
	int log_match_len;

	log_match_len = snprintf(log_match, sizeof(log_match), log_template,
				 (unsigned int)ns_type,
				 (unsigned long long)ns_id);
	if (log_match_len >= sizeof(log_match))
		return -E2BIG;

	return audit_match_record(audit_fd, AUDIT_LANDLOCK_ACCESS, log_match,
				  NULL);
}

FIXTURE(ns_audit)
{
	struct audit_filter audit_filter;
	int audit_fd;
};

FIXTURE_SETUP(ns_audit)
{
	ASSERT_TRUE(is_in_init_user_ns());

	disable_caps(_metadata);

	set_cap(_metadata, CAP_AUDIT_CONTROL);
	self->audit_fd = audit_init_with_exe_filter(&self->audit_filter);
	EXPECT_LE(0, self->audit_fd);
	clear_cap(_metadata, CAP_AUDIT_CONTROL);
}

FIXTURE_TEARDOWN(ns_audit)
{
	set_cap(_metadata, CAP_AUDIT_CONTROL);
	EXPECT_EQ(0, audit_cleanup(self->audit_fd, &self->audit_filter));
	clear_cap(_metadata, CAP_AUDIT_CONTROL);
}

/*
 * Verifies that a denied namespace creation produces the expected audit record
 * with the namespace.use blocker string and namespace_type.
 */
TEST_F(ns_audit, create_denied)
{
	struct audit_records records = {};
	int ruleset_fd;
	__u64 domain_id;

	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(-1, unshare(CLONE_NEWUTS));
	EXPECT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_ADMIN);

	EXPECT_EQ(0, matches_log_ns_create(self->audit_fd, CLONE_NEWUTS));

	/*
	 * The domain allocation record is emitted in the same event as the
	 * first denial; anchor its status=allocated and enforcing pid.  Both
	 * access and domain records are now consumed, so none remain.
	 */
	EXPECT_EQ(0, matches_log_domain_allocated(self->audit_fd, getpid(),
						  &domain_id));
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
	EXPECT_EQ(0, records.domain);
}

TEST_F(ns_audit, create_allowed)
{
	struct audit_records records = {};
	int ruleset_fd;

	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, add_ns_rule(ruleset_fd, CLONE_NEWUTS));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, unshare(CLONE_NEWUTS));
	clear_cap(_metadata, CAP_SYS_ADMIN);

	/* No records: allowed operations never trigger audit logging. */
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
}

TEST_F(ns_audit, setns_allowed)
{
	struct audit_records records = {};
	int ruleset_fd, ns_fd;

	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, add_ns_rule(ruleset_fd, CLONE_NEWUTS));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	ns_fd = open("/proc/self/ns/uts", O_RDONLY);
	ASSERT_LE(0, ns_fd);

	/* Allowed: should succeed with no audit record. */
	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, setns(ns_fd, CLONE_NEWUTS));
	clear_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, close(ns_fd));

	/* No records: allowed setns never triggers audit logging. */
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
}

TEST_F(ns_audit, setns_denied)
{
	struct audit_records records = {};
	int ruleset_fd, ns_fd;
	__u64 ns_id, domain_id;

	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	/* No rule allows UTS -> denied. */
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	ns_fd = open("/proc/self/ns/uts", O_RDONLY);
	ASSERT_LE(0, ns_fd);

	/* The audit record logs the target namespace's exact ns_id. */
	ASSERT_EQ(0, ioctl(ns_fd, NS_GET_ID, &ns_id));

	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(-1, setns(ns_fd, CLONE_NEWUTS));
	EXPECT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, close(ns_fd));

	/* Verify the audit record for setns denial, anchored to the ns_id. */
	EXPECT_EQ(0, matches_log_ns_setns(self->audit_fd, CLONE_NEWUTS, ns_id));

	/*
	 * The domain allocation record is emitted in the same event as the
	 * first denial; anchor its status=allocated and enforcing pid.  Both
	 * access and domain records are now consumed, so none remain.
	 */
	EXPECT_EQ(0, matches_log_domain_allocated(self->audit_fd, getpid(),
						  &domain_id));
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
	EXPECT_EQ(0, records.domain);
}

/*
 * A quieted denial is still denied (EPERM); only its audit record is
 * suppressed.  Quiet-only rule (empty allowed set).
 */
TEST_F(ns_audit, create_quieted)
{
	struct audit_records records = {};
	int ruleset_fd;

	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, add_ns_rule_full(ruleset_fd, 0, CLONE_NEWUTS));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(-1, unshare(CLONE_NEWUTS));
	EXPECT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_ADMIN);

	/* The denial is suppressed: no access record. */
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
}

/*
 * A quieted setns denial is still denied (EPERM); only its audit record is
 * suppressed.  The install-hook mirror of create_quieted.
 */
TEST_F(ns_audit, setns_quieted)
{
	struct audit_records records = {};
	int ruleset_fd, ns_fd;

	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, add_ns_rule_full(ruleset_fd, 0, CLONE_NEWUTS));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	ns_fd = open("/proc/self/ns/uts", O_RDONLY);
	ASSERT_LE(0, ns_fd);

	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(-1, setns(ns_fd, CLONE_NEWUTS));
	EXPECT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, close(ns_fd));

	/* The denial is suppressed: no access record. */
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
}

/*
 * Quiet is per-member: quieting one namespace type does not silence the denial
 * of another type denied by the same layer.
 */
TEST_F(ns_audit, quiet_is_per_member)
{
	struct audit_records records = {};
	int ruleset_fd;
	__u64 domain_id;

	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	/*
	 * Quiet CLONE_NEWNET only; CLONE_NEWUTS is neither allowed nor quiet.
	 */
	ASSERT_EQ(0, add_ns_rule_full(ruleset_fd, 0, CLONE_NEWNET));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(-1, unshare(CLONE_NEWUTS));
	EXPECT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_ADMIN);

	/* The unquieted CLONE_NEWUTS denial is still logged. */
	EXPECT_EQ(0, matches_log_ns_create(self->audit_fd, CLONE_NEWUTS));
	EXPECT_EQ(0, matches_log_domain_allocated(self->audit_fd, getpid(),
						  &domain_id));
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
	EXPECT_EQ(0, records.domain);
}

/*
 * Quieting an unknown namespace bit has no effect: a rule that quiets only a
 * bit the running kernel does not know still logs the denial of a known type.
 * Mirrors the forward-compatibility contract for the quiet mask.
 */
TEST_F(ns_audit, quiet_unknown_bit_no_effect)
{
	struct audit_records records = {};
	int ruleset_fd;
	__u64 domain_id;

	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	/* Quiet only an unknown bit (bit 31 is not a CLONE_NEW* value). */
	ASSERT_EQ(0, add_ns_rule_full(ruleset_fd, 0, (1ULL << 31)));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(-1, unshare(CLONE_NEWUTS));
	EXPECT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_ADMIN);

	/* The unknown quiet bit does not suppress the known denial. */
	EXPECT_EQ(0, matches_log_ns_create(self->audit_fd, CLONE_NEWUTS));
	EXPECT_EQ(0, matches_log_domain_allocated(self->audit_fd, getpid(),
						  &domain_id));
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
	EXPECT_EQ(0, records.domain);
}

/* A single rule can both allow one member and quiet the denial of another. */
TEST_F(ns_audit, allow_and_quiet)
{
	struct audit_records records = {};
	int ruleset_fd;

	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, add_ns_rule_full(ruleset_fd, CLONE_NEWNET, CLONE_NEWUTS));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	/* Allowed member: succeeds. */
	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, unshare(CLONE_NEWNET));
	/* Quieted member: denied but not logged. */
	EXPECT_EQ(-1, unshare(CLONE_NEWUTS));
	EXPECT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_ADMIN);

	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
}

/*
 * Quieting a member that is also allowed is inert: an allowed member is never
 * denied, so its quiet bit has no effect.
 */
TEST_F(ns_audit, allow_and_quiet_same_member)
{
	struct audit_records records = {};
	int ruleset_fd;

	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	/* Same member both allowed and quieted. */
	ASSERT_EQ(0, add_ns_rule_full(ruleset_fd, CLONE_NEWNET, CLONE_NEWNET));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	/* The member is allowed (quiet is inert), so it succeeds. */
	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(0, unshare(CLONE_NEWNET));
	clear_cap(_metadata, CAP_SYS_ADMIN);

	/* Nothing denied, nothing logged. */
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
}

/*
 * Only the youngest denying layer decides quieting: a parent that quiets a
 * member cannot silence a denial by a deeper layer that handles but does not
 * quiet it.
 */
TEST_F(ns_audit, quiet_youngest_layer_wins)
{
	struct audit_records records = {};
	int ruleset_fd;

	/* Parent layer: handles namespaces and quiets CLONE_NEWUTS. */
	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, add_ns_rule_full(ruleset_fd, 0, CLONE_NEWUTS));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	/* Child layer: handles namespaces but does not quiet CLONE_NEWUTS. */
	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(-1, unshare(CLONE_NEWUTS));
	EXPECT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_ADMIN);

	/* The child layer denies and does not quiet: logged. */
	EXPECT_EQ(0, matches_log_ns_create(self->audit_fd, CLONE_NEWUTS));
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
}

/*
 * A parent layer that quiets and denies a member suppresses its own denial even
 * when it is not the youngest layer: a child layer allows the member, so the
 * parent is the youngest (and only) denying layer and its stored quiet mask
 * fires.  Complements quiet_youngest_layer_wins.
 */
TEST_F(ns_audit, quiet_parent_only_denying_layer)
{
	struct audit_records records = {};
	int ruleset_fd;

	/* Parent layer: quiet-only, so it denies CLONE_NEWUTS. */
	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, add_ns_rule_full(ruleset_fd, 0, CLONE_NEWUTS));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	/* Child layer: allows CLONE_NEWUTS, so only the parent denies it. */
	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, add_ns_rule(ruleset_fd, CLONE_NEWUTS));
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	set_cap(_metadata, CAP_SYS_ADMIN);
	EXPECT_EQ(-1, unshare(CLONE_NEWUTS));
	EXPECT_EQ(EPERM, errno);
	clear_cap(_metadata, CAP_SYS_ADMIN);

	/* The parent quiets and is the youngest denying layer: suppressed. */
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
}

/* Trace tests */

/* clang-format off */
FIXTURE(ns_trace) {
	/* clang-format on */
	int tracefs_ok;
};

FIXTURE_SETUP(ns_trace)
{
	int ret;

	set_cap(_metadata, CAP_SYS_ADMIN);
	ASSERT_EQ(0, unshare(CLONE_NEWNS));
	ASSERT_EQ(0, mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL));

	ret = tracefs_fixture_setup();
	if (ret) {
		clear_cap(_metadata, CAP_SYS_ADMIN);
		self->tracefs_ok = 0;
		SKIP(return, "tracefs not available");
	}
	self->tracefs_ok = 1;

	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_CREATE_RULESET_ENABLE, true));
	ASSERT_EQ(0, tracefs_enable_event(TRACEFS_ADD_RULE_NAMESPACE_ENABLE,
					  true));
	ASSERT_EQ(0, tracefs_enable_event(
			     TRACEFS_DENY_PERMISSION_NAMESPACE_ENABLE, true));
	ASSERT_EQ(0, tracefs_clear());
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

FIXTURE_TEARDOWN(ns_trace)
{
	if (!self->tracefs_ok)
		return;

	set_cap(_metadata, CAP_SYS_ADMIN);
	tracefs_enable_event(TRACEFS_CREATE_RULESET_ENABLE, false);
	tracefs_enable_event(TRACEFS_ADD_RULE_NAMESPACE_ENABLE, false);
	tracefs_enable_event(TRACEFS_DENY_PERMISSION_NAMESPACE_ENABLE, false);
	tracefs_fixture_teardown();
	clear_cap(_metadata, CAP_SYS_ADMIN);
}

/*
 * Check the permission fields and version history, including two rejected calls
 * and two successful calls that do not extend the effective policy: a repeated
 * rule and an unknown-only rule.
 */
TEST_F(ns_trace, rule_events)
{
	const char *const version_1 =
		REGEX_ADD_RULE_NAMESPACE_VERSION(TRACE_TASK, "1");
	const char *const version_2 =
		REGEX_ADD_RULE_NAMESPACE_VERSION(TRACE_TASK, "2");
	const char *const version_3 =
		REGEX_ADD_RULE_NAMESPACE_VERSION(TRACE_TASK, "3");
	const struct landlock_namespace_attr valid_attr = {
		.permissions = LANDLOCK_PERMISSION_NAMESPACE_USE,
		.allowed_namespace_types = CLONE_NEWUTS,
		.quiet_namespace_types = CLONE_NEWNET,
	};
	const struct landlock_namespace_attr invalid_attr = {
		.permissions = LANDLOCK_PERMISSION_CAPABILITY_USE,
		.allowed_namespace_types = CLONE_NEWUTS,
	};
	char expected[32], field[64];
	char *buf;
	int ruleset_fd;

	if (!self->tracefs_ok)
		SKIP(return, "tracefs not available");

	ASSERT_EQ(0, tracefs_clear_buf());

	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	ASSERT_EQ(0, add_ns_rule_full(ruleset_fd, CLONE_NEWUTS, CLONE_NEWNET));
	ASSERT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
					&invalid_attr, 0));
	ASSERT_EQ(EINVAL, errno);
	ASSERT_EQ(-1, landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NAMESPACE,
					&valid_attr, LANDLOCK_ADD_RULE_QUIET));
	ASSERT_EQ(EINVAL, errno);
	ASSERT_EQ(0, add_ns_rule_full(ruleset_fd, CLONE_NEWUTS, CLONE_NEWNET));
	ASSERT_EQ(0, add_ns_rule(ruleset_fd, 1ULL << 63));
	EXPECT_EQ(0, close(ruleset_fd));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);

	ASSERT_EQ(1,
		  tracefs_count_matches(buf, REGEX_CREATE_RULESET(TRACE_TASK)));
	ASSERT_EQ(0, tracefs_extract_field(
			     buf, REGEX_CREATE_RULESET(TRACE_TASK),
			     "handled_permissions", field, sizeof(field)));
	EXPECT_STREQ("namespace.use", field);

	ASSERT_EQ(3,
		  tracefs_count_matches(buf, "landlock_add_rule_namespace: "));
	ASSERT_EQ(3, tracefs_count_matches(
			     buf, REGEX_ADD_RULE_NAMESPACE(TRACE_TASK)));
	EXPECT_EQ(1, tracefs_count_matches(buf, version_1));
	EXPECT_EQ(1, tracefs_count_matches(buf, version_2));
	EXPECT_EQ(1, tracefs_count_matches(buf, version_3));

	ASSERT_EQ(0, tracefs_extract_field(buf, version_1, "permissions", field,
					   sizeof(field)));
	EXPECT_STREQ("namespace.use", field);
	ASSERT_EQ(0, tracefs_extract_field(buf, version_1,
					   "allowed_namespace_types", field,
					   sizeof(field)));
	/* Trace fields use raw CLONE_NEW* values, not internal bit indexes. */
	snprintf(expected, sizeof(expected), "0x%llx",
		 (unsigned long long)CLONE_NEWUTS);
	EXPECT_STREQ(expected, field);
	ASSERT_EQ(0,
		  tracefs_extract_field(buf, version_1, "quiet_namespace_types",
					field, sizeof(field)));
	snprintf(expected, sizeof(expected), "0x%llx",
		 (unsigned long long)CLONE_NEWNET);
	EXPECT_STREQ(expected, field);

	/* The rejected calls emit no event and do not consume version 2. */
	ASSERT_EQ(0, tracefs_extract_field(buf, version_2, "permissions", field,
					   sizeof(field)));
	EXPECT_STREQ("namespace.use", field);
	ASSERT_EQ(0, tracefs_extract_field(buf, version_2,
					   "allowed_namespace_types", field,
					   sizeof(field)));
	snprintf(expected, sizeof(expected), "0x%llx",
		 (unsigned long long)CLONE_NEWUTS);
	EXPECT_STREQ(expected, field);
	ASSERT_EQ(0,
		  tracefs_extract_field(buf, version_2, "quiet_namespace_types",
					field, sizeof(field)));
	snprintf(expected, sizeof(expected), "0x%llx",
		 (unsigned long long)CLONE_NEWNET);
	EXPECT_STREQ(expected, field);

	/* The unknown-only call succeeds but contributes no effective bits. */
	ASSERT_EQ(0, tracefs_extract_field(buf, version_3, "permissions", field,
					   sizeof(field)));
	EXPECT_STREQ("namespace.use", field);
	ASSERT_EQ(0, tracefs_extract_field(buf, version_3,
					   "allowed_namespace_types", field,
					   sizeof(field)));
	EXPECT_STREQ("0x0", field);
	ASSERT_EQ(0,
		  tracefs_extract_field(buf, version_3, "quiet_namespace_types",
					field, sizeof(field)));
	EXPECT_STREQ("0x0", field);

	free(buf);
}

static void test_ns_trace_denial(struct __test_metadata *const _metadata,
				 const bool use_setns, const bool quiet)
{
	char expected[32], field[64];
	int ruleset_fd, ns_fd = -1, ret;
	__u64 expected_ns_id = 0;
	char *buf;

	ASSERT_EQ(0, tracefs_clear_buf());

	if (use_setns) {
		ns_fd = open("/proc/self/ns/uts", O_RDONLY | O_CLOEXEC);
		ASSERT_LE(0, ns_fd);
		ASSERT_EQ(0, ioctl(ns_fd, NS_GET_ID, &expected_ns_id));
	}

	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	if (quiet)
		ASSERT_EQ(0, add_ns_rule_full(ruleset_fd, 0, CLONE_NEWUTS));

	set_cap(_metadata, CAP_SYS_ADMIN);
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	if (use_setns)
		ret = setns(ns_fd, CLONE_NEWUTS);
	else
		ret = unshare(CLONE_NEWUTS);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EPERM, errno);

	clear_cap(_metadata, CAP_SYS_ADMIN);
	if (ns_fd >= 0)
		EXPECT_EQ(0, close(ns_fd));

	buf = tracefs_read_buf();
	ASSERT_NE(NULL, buf);
	ASSERT_EQ(1, tracefs_count_matches(
			     buf, REGEX_DENY_PERMISSION_NAMESPACE(TRACE_TASK)))
	{
		TH_LOG("Expected one namespace denial event\n%s", buf);
	}

	ASSERT_EQ(0, tracefs_extract_field(
			     buf, REGEX_DENY_PERMISSION_NAMESPACE(TRACE_TASK),
			     "domain", field, sizeof(field)));
	EXPECT_STRNE("0", field);
	ASSERT_EQ(0, tracefs_extract_field(
			     buf, REGEX_DENY_PERMISSION_NAMESPACE(TRACE_TASK),
			     "same_exec", field, sizeof(field)));
	EXPECT_STREQ("1", field);
	ASSERT_EQ(0, tracefs_extract_field(
			     buf, REGEX_DENY_PERMISSION_NAMESPACE(TRACE_TASK),
			     "logged", field, sizeof(field)));
	EXPECT_STREQ(quiet ? "0" : "1", field);
	ASSERT_EQ(0, tracefs_extract_field(
			     buf, REGEX_DENY_PERMISSION_NAMESPACE(TRACE_TASK),
			     "blockers", field, sizeof(field)));
	EXPECT_STREQ("use", field);
	ASSERT_EQ(0, tracefs_extract_field(
			     buf, REGEX_DENY_PERMISSION_NAMESPACE(TRACE_TASK),
			     "namespace_type", field, sizeof(field)));
	snprintf(expected, sizeof(expected), "0x%llx",
		 (unsigned long long)CLONE_NEWUTS);
	EXPECT_STREQ(expected, field);
	ASSERT_EQ(0, tracefs_extract_field(
			     buf, REGEX_DENY_PERMISSION_NAMESPACE(TRACE_TASK),
			     "namespace_id", field, sizeof(field)));
	EXPECT_STRNE("0", field);
	if (use_setns)
		EXPECT_EQ(expected_ns_id, strtoull(field, NULL, 10));

	free(buf);
}

TEST_F(ns_trace, deny_create)
{
	if (!self->tracefs_ok)
		SKIP(return, "tracefs not available");
	test_ns_trace_denial(_metadata, false, false);
}

TEST_F(ns_trace, deny_create_quiet)
{
	if (!self->tracefs_ok)
		SKIP(return, "tracefs not available");
	test_ns_trace_denial(_metadata, false, true);
}

TEST_F(ns_trace, deny_setns)
{
	if (!self->tracefs_ok)
		SKIP(return, "tracefs not available");
	test_ns_trace_denial(_metadata, true, false);
}

/* clang-format off */
FIXTURE(ns_proc_open) {
	/* clang-format on */
	struct audit_filter audit_filter;
	int audit_fd;
};

FIXTURE_VARIANT(ns_proc_open)
{
	__u64 ns_type;
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_proc_open, mnt) {
	/* clang-format on */
	.ns_type = CLONE_NEWNS,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(ns_proc_open, user) {
	/* clang-format on */
	.ns_type = CLONE_NEWUSER,
};

FIXTURE_SETUP(ns_proc_open)
{
	ASSERT_TRUE(is_in_init_user_ns());

	disable_caps(_metadata);

	set_cap(_metadata, CAP_AUDIT_CONTROL);
	self->audit_fd = audit_init_with_exe_filter(&self->audit_filter);
	EXPECT_LE(0, self->audit_fd);
	clear_cap(_metadata, CAP_AUDIT_CONTROL);
}

FIXTURE_TEARDOWN(ns_proc_open)
{
	set_cap(_metadata, CAP_AUDIT_CONTROL);
	EXPECT_EQ(0, audit_cleanup(self->audit_fd, &self->audit_filter));
	clear_cap(_metadata, CAP_AUDIT_CONTROL);
}

/*
 * Opening /proc/self/ns/<type> only acquires a procfs reference, not membership
 * or an acquired fd of the kind LANDLOCK_PERMISSION_NAMESPACE_USE gates.
 * Verify the open is unrestricted even when the permission is handled with no
 * rules.
 */
TEST_F(ns_proc_open, open_unrestricted)
{
	char proc_path[NS_PROC_PATH_MAX];
	struct audit_records records = {};
	int ruleset_fd, fd;

	ASSERT_TRUE(
		ns_is_supported(variant->ns_type, proc_path, sizeof(proc_path)))
	{
		TH_LOG("Namespace type 0x%llx not supported",
		       (unsigned long long)variant->ns_type);
	}

	ruleset_fd = create_ns_ruleset();
	ASSERT_LE(0, ruleset_fd);
	enforce_ruleset(_metadata, ruleset_fd);
	EXPECT_EQ(0, close(ruleset_fd));

	fd = open(proc_path, O_RDONLY);
	ASSERT_LE(0, fd)
	{
		TH_LOG("open(%s) failed: %s", proc_path, strerror(errno));
	}

	/* No Landlock denial. */
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);

	EXPECT_EQ(0, close(fd));
}

TEST_HARNESS_MAIN
