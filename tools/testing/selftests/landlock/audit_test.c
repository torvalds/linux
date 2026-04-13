// SPDX-License-Identifier: GPL-2.0
/*
 * Landlock tests - Audit
 *
 * Copyright © 2024-2025 Microsoft Corporation
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/landlock.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "audit.h"
#include "common.h"

static int matches_log_signal(struct __test_metadata *const _metadata,
			      int audit_fd, const pid_t opid, __u64 *domain_id)
{
	static const char log_template[] = REGEX_LANDLOCK_PREFIX
		" blockers=scope\\.signal opid=%d ocomm=\"audit_test\"$";
	char log_match[sizeof(log_template) + 10];
	int log_match_len;

	log_match_len =
		snprintf(log_match, sizeof(log_match), log_template, opid);
	if (log_match_len > sizeof(log_match))
		return -E2BIG;

	return audit_match_record(audit_fd, AUDIT_LANDLOCK_ACCESS, log_match,
				  domain_id);
}

FIXTURE(audit)
{
	struct audit_filter audit_filter;
	int audit_fd;
};

FIXTURE_SETUP(audit)
{
	disable_caps(_metadata);
	set_cap(_metadata, CAP_AUDIT_CONTROL);
	self->audit_fd = audit_init_with_exe_filter(&self->audit_filter);
	EXPECT_LE(0, self->audit_fd)
	{
		const char *error_msg;

		/* kill "$(auditctl -s | sed -ne 's/^pid \([0-9]\+\)$/\1/p')" */
		if (self->audit_fd == -EEXIST)
			error_msg = "socket already in use (e.g. auditd)";
		else
			error_msg = strerror(-self->audit_fd);
		TH_LOG("Failed to initialize audit: %s", error_msg);
	}
	clear_cap(_metadata, CAP_AUDIT_CONTROL);
}

FIXTURE_TEARDOWN(audit)
{
	set_cap(_metadata, CAP_AUDIT_CONTROL);
	EXPECT_EQ(0, audit_cleanup(self->audit_fd, &self->audit_filter));
	clear_cap(_metadata, CAP_AUDIT_CONTROL);
}

TEST_F(audit, layers)
{
	const struct landlock_ruleset_attr ruleset_attr = {
		.scoped = LANDLOCK_SCOPE_SIGNAL,
	};
	int status, ruleset_fd, i;
	__u64(*domain_stack)[16];
	__u64 prev_dom = 3;
	pid_t child;

	domain_stack = mmap(NULL, sizeof(*domain_stack), PROT_READ | PROT_WRITE,
			    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	ASSERT_NE(MAP_FAILED, domain_stack);
	memset(domain_stack, 0, sizeof(*domain_stack));

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);
	EXPECT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));

	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		for (i = 0; i < ARRAY_SIZE(*domain_stack); i++) {
			__u64 denial_dom = 1;
			__u64 allocated_dom = 2;

			EXPECT_EQ(0, landlock_restrict_self(ruleset_fd, 0));

			/* Creates a denial to get the domain ID. */
			EXPECT_EQ(-1, kill(getppid(), 0));
			EXPECT_EQ(EPERM, errno);
			EXPECT_EQ(0,
				  matches_log_signal(_metadata, self->audit_fd,
						     getppid(), &denial_dom));
			EXPECT_EQ(0, matches_log_domain_allocated(
					     self->audit_fd, getpid(),
					     &allocated_dom));
			EXPECT_NE(denial_dom, 1);
			EXPECT_NE(denial_dom, 0);
			EXPECT_EQ(denial_dom, allocated_dom);

			/* Checks that the new domain is younger than the previous one. */
			EXPECT_GT(allocated_dom, prev_dom);
			prev_dom = allocated_dom;
			(*domain_stack)[i] = allocated_dom;
		}

		/* Checks that we reached the maximum number of layers. */
		EXPECT_EQ(-1, landlock_restrict_self(ruleset_fd, 0));
		EXPECT_EQ(E2BIG, errno);

		/* Updates filter rules to match the drop record. */
		set_cap(_metadata, CAP_AUDIT_CONTROL);
		EXPECT_EQ(0, audit_filter_drop(self->audit_fd, AUDIT_ADD_RULE));
		EXPECT_EQ(0,
			  audit_filter_exe(self->audit_fd, &self->audit_filter,
					   AUDIT_DEL_RULE));
		clear_cap(_metadata, CAP_AUDIT_CONTROL);

		_exit(_metadata->exit_code);
		return;
	}

	ASSERT_EQ(child, waitpid(child, &status, 0));
	if (WIFSIGNALED(status) || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != EXIT_SUCCESS)
		_metadata->exit_code = KSFT_FAIL;

	/*
	 * Purges log from deallocated domains.  Records arrive in LIFO order
	 * (innermost domain first) because landlock_put_hierarchy() walks the
	 * chain sequentially in a single kworker context.
	 */
	for (i = ARRAY_SIZE(*domain_stack) - 1; i >= 0; i--) {
		__u64 deallocated_dom = 2;

		EXPECT_EQ(0, matches_log_domain_deallocated(self->audit_fd, 1,
							    (*domain_stack)[i],
							    &deallocated_dom));
		EXPECT_EQ((*domain_stack)[i], deallocated_dom)
		{
			TH_LOG("Failed to match domain %llx (#%d)",
			       (unsigned long long)(*domain_stack)[i], i);
		}
	}
	EXPECT_EQ(0, munmap(domain_stack, sizeof(*domain_stack)));
	EXPECT_EQ(0, close(ruleset_fd));
}

struct thread_data {
	pid_t parent_pid;
	int ruleset_fd, pipe_child, pipe_parent;
	bool mute_subdomains;
};

static void *thread_audit_test(void *arg)
{
	const struct thread_data *data = (struct thread_data *)arg;
	uintptr_t err = 0;
	char buffer;

	/* TGID and TID are different for a second thread. */
	if (getpid() == gettid()) {
		err = 1;
		goto out;
	}

	if (landlock_restrict_self(data->ruleset_fd, 0)) {
		err = 2;
		goto out;
	}

	if (close(data->ruleset_fd)) {
		err = 3;
		goto out;
	}

	/* Creates a denial to get the domain ID. */
	if (kill(data->parent_pid, 0) != -1) {
		err = 4;
		goto out;
	}

	if (EPERM != errno) {
		err = 5;
		goto out;
	}

	/* Signals the parent to read denial logs. */
	if (write(data->pipe_child, ".", 1) != 1) {
		err = 6;
		goto out;
	}

	/* Waits for the parent to update audit filters. */
	if (read(data->pipe_parent, &buffer, 1) != 1) {
		err = 7;
		goto out;
	}

out:
	close(data->pipe_child);
	close(data->pipe_parent);
	return (void *)err;
}

/* Checks that the PID tied to a domain is not a TID but the TGID. */
TEST_F(audit, thread)
{
	const struct landlock_ruleset_attr ruleset_attr = {
		.scoped = LANDLOCK_SCOPE_SIGNAL,
	};
	__u64 denial_dom = 1;
	__u64 allocated_dom = 2;
	__u64 deallocated_dom = 3;
	pthread_t thread;
	int pipe_child[2], pipe_parent[2];
	char buffer;
	struct thread_data child_data;

	child_data.parent_pid = getppid();
	ASSERT_EQ(0, pipe2(pipe_child, O_CLOEXEC));
	child_data.pipe_child = pipe_child[1];
	ASSERT_EQ(0, pipe2(pipe_parent, O_CLOEXEC));
	child_data.pipe_parent = pipe_parent[0];
	child_data.ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, child_data.ruleset_fd);

	/* TGID and TID are the same for the initial thread . */
	EXPECT_EQ(getpid(), gettid());
	EXPECT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));
	ASSERT_EQ(0, pthread_create(&thread, NULL, thread_audit_test,
				    &child_data));

	/* Waits for the child to generate a denial. */
	ASSERT_EQ(1, read(pipe_child[0], &buffer, 1));
	EXPECT_EQ(0, close(pipe_child[0]));

	/* Matches the signal log to get the domain ID. */
	EXPECT_EQ(0, matches_log_signal(_metadata, self->audit_fd,
					child_data.parent_pid, &denial_dom));
	EXPECT_NE(denial_dom, 1);
	EXPECT_NE(denial_dom, 0);

	EXPECT_EQ(0, matches_log_domain_allocated(self->audit_fd, getpid(),
						  &allocated_dom));
	EXPECT_EQ(denial_dom, allocated_dom);

	/* Updates filter rules to match the drop record. */
	set_cap(_metadata, CAP_AUDIT_CONTROL);
	EXPECT_EQ(0, audit_filter_drop(self->audit_fd, AUDIT_ADD_RULE));
	EXPECT_EQ(0, audit_filter_exe(self->audit_fd, &self->audit_filter,
				      AUDIT_DEL_RULE));
	clear_cap(_metadata, CAP_AUDIT_CONTROL);

	/* Signals the thread to exit, which will generate a domain deallocation. */
	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));
	EXPECT_EQ(0, close(pipe_parent[1]));
	ASSERT_EQ(0, pthread_join(thread, NULL));

	EXPECT_EQ(0, matches_log_domain_deallocated(
			     self->audit_fd, 1, denial_dom, &deallocated_dom));
	EXPECT_EQ(denial_dom, deallocated_dom);
}

/*
 * Verifies that log_subdomains_off set via the ruleset_fd=-1 path (without
 * creating a domain) is inherited by children across fork().  This exercises
 * the hook_cred_transfer() fix: the Landlock credential blob must be copied
 * even when the source credential has no domain.
 *
 * Phase 1 (baseline): a child without muting creates a domain and triggers a
 * denial that IS logged.
 *
 * Phase 2 (after muting): the parent mutes subdomain logs, forks another child
 * who creates a domain and triggers a denial that is NOT logged.
 */
TEST_F(audit, log_subdomains_off_fork)
{
	const struct landlock_ruleset_attr ruleset_attr = {
		.scoped = LANDLOCK_SCOPE_SIGNAL,
	};
	struct audit_records records;
	int ruleset_fd, status;
	pid_t child;

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, ruleset_fd);

	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));

	/*
	 * Phase 1: forks a child that creates a domain and triggers a denial
	 * before any muting.  This proves the audit path works.
	 */
	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		ASSERT_EQ(0, landlock_restrict_self(ruleset_fd, 0));
		ASSERT_EQ(-1, kill(getppid(), 0));
		ASSERT_EQ(EPERM, errno);
		_exit(0);
		return;
	}

	ASSERT_EQ(child, waitpid(child, &status, 0));
	ASSERT_EQ(true, WIFEXITED(status));
	ASSERT_EQ(0, WEXITSTATUS(status));

	/* The denial must be logged (baseline). */
	EXPECT_EQ(0, matches_log_signal(_metadata, self->audit_fd, getpid(),
					NULL));

	/* Drains any remaining records (e.g. domain allocation). */
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));

	/*
	 * Mutes subdomain logs without creating a domain.  The parent's
	 * credential has domain=NULL and log_subdomains_off=1.
	 */
	ASSERT_EQ(0, landlock_restrict_self(
			     -1, LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF));

	/*
	 * Phase 2: forks a child that creates a domain and triggers a denial.
	 * Because log_subdomains_off was inherited via fork(), the child's
	 * domain has log_status=LANDLOCK_LOG_DISABLED.
	 */
	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		ASSERT_EQ(0, landlock_restrict_self(ruleset_fd, 0));
		ASSERT_EQ(-1, kill(getppid(), 0));
		ASSERT_EQ(EPERM, errno);
		_exit(0);
		return;
	}

	ASSERT_EQ(child, waitpid(child, &status, 0));
	ASSERT_EQ(true, WIFEXITED(status));
	ASSERT_EQ(0, WEXITSTATUS(status));

	/* No denial record should appear. */
	EXPECT_EQ(-EAGAIN, matches_log_signal(_metadata, self->audit_fd,
					      getpid(), NULL));

	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);

	EXPECT_EQ(0, close(ruleset_fd));
}

/*
 * Thread function: runs two rounds of (create domain, trigger denial, signal
 * back), waiting for the main thread before each round.  When mute_subdomains
 * is set, phase 1 also mutes subdomain logs via the fd=-1 path before creating
 * the domain.  The ruleset_fd is kept open across both rounds so each
 * restrict_self call stacks a new domain layer.
 */
static void *thread_sandbox_deny_twice(void *arg)
{
	const struct thread_data *data = (struct thread_data *)arg;
	uintptr_t err = 0;
	char buffer;

	/* Phase 1: optionally mutes, creates a domain, and triggers a denial. */
	if (read(data->pipe_parent, &buffer, 1) != 1) {
		err = 1;
		goto out;
	}

	if (data->mute_subdomains &&
	    landlock_restrict_self(-1,
				   LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF)) {
		err = 2;
		goto out;
	}

	if (landlock_restrict_self(data->ruleset_fd, 0)) {
		err = 3;
		goto out;
	}

	if (kill(data->parent_pid, 0) != -1 || errno != EPERM) {
		err = 4;
		goto out;
	}

	if (write(data->pipe_child, ".", 1) != 1) {
		err = 5;
		goto out;
	}

	/* Phase 2: stacks another domain and triggers a denial. */
	if (read(data->pipe_parent, &buffer, 1) != 1) {
		err = 6;
		goto out;
	}

	if (landlock_restrict_self(data->ruleset_fd, 0)) {
		err = 7;
		goto out;
	}

	if (kill(data->parent_pid, 0) != -1 || errno != EPERM) {
		err = 8;
		goto out;
	}

	if (write(data->pipe_child, ".", 1) != 1) {
		err = 9;
		goto out;
	}

out:
	close(data->ruleset_fd);
	close(data->pipe_child);
	close(data->pipe_parent);
	return (void *)err;
}

/*
 * Verifies that LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF with
 * LANDLOCK_RESTRICT_SELF_TSYNC and ruleset_fd=-1 propagates log_subdomains_off
 * to a sibling thread, suppressing audit logging on domains it subsequently
 * creates.
 *
 * Phase 1 (before TSYNC) acts as an inline baseline: the sibling creates a
 * domain and triggers a denial that IS logged.
 *
 * Phase 2 (after TSYNC) verifies suppression: the sibling stacks another domain
 * and triggers a denial that is NOT logged.
 */
TEST_F(audit, log_subdomains_off_tsync)
{
	const struct landlock_ruleset_attr ruleset_attr = {
		.scoped = LANDLOCK_SCOPE_SIGNAL,
	};
	struct audit_records records;
	struct thread_data child_data = {};
	int pipe_child[2], pipe_parent[2];
	char buffer;
	pthread_t thread;
	void *thread_ret;

	child_data.parent_pid = getppid();
	ASSERT_EQ(0, pipe2(pipe_child, O_CLOEXEC));
	child_data.pipe_child = pipe_child[1];
	ASSERT_EQ(0, pipe2(pipe_parent, O_CLOEXEC));
	child_data.pipe_parent = pipe_parent[0];
	child_data.ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, child_data.ruleset_fd);

	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));

	/* Creates the sibling thread. */
	ASSERT_EQ(0, pthread_create(&thread, NULL, thread_sandbox_deny_twice,
				    &child_data));

	/*
	 * Phase 1: the sibling creates a domain and triggers a denial before
	 * any log muting.  This proves the audit path works.
	 */
	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));
	ASSERT_EQ(1, read(pipe_child[0], &buffer, 1));

	/* The denial must be logged. */
	EXPECT_EQ(0, matches_log_signal(_metadata, self->audit_fd,
					child_data.parent_pid, NULL));

	/* Drains any remaining records (e.g. domain allocation). */
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));

	/*
	 * Mutes subdomain logs and propagates to the sibling thread via TSYNC,
	 * without creating a domain.
	 */
	ASSERT_EQ(0, landlock_restrict_self(
			     -1, LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF |
					 LANDLOCK_RESTRICT_SELF_TSYNC));

	/*
	 * Phase 2: the sibling stacks another domain and triggers a denial.
	 * Because log_subdomains_off was propagated via TSYNC, the new domain
	 * has log_status=LANDLOCK_LOG_DISABLED.
	 */
	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));
	ASSERT_EQ(1, read(pipe_child[0], &buffer, 1));

	/* No denial record should appear. */
	EXPECT_EQ(-EAGAIN, matches_log_signal(_metadata, self->audit_fd,
					      child_data.parent_pid, NULL));

	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);

	EXPECT_EQ(0, close(pipe_child[0]));
	EXPECT_EQ(0, close(pipe_parent[1]));
	ASSERT_EQ(0, pthread_join(thread, &thread_ret));
	EXPECT_EQ(NULL, thread_ret);
}

/*
 * Verifies that LANDLOCK_RESTRICT_SELF_TSYNC without
 * LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF overrides a sibling thread's
 * log_subdomains_off, re-enabling audit logging on domains the sibling
 * subsequently creates.
 *
 * Phase 1: the sibling sets log_subdomains_off, creates a muted domain, and
 * triggers a denial that is NOT logged.
 *
 * Phase 2 (after TSYNC without LOG_SUBDOMAINS_OFF): the sibling stacks another
 * domain and triggers a denial that IS logged, proving the muting was
 * overridden.
 */
TEST_F(audit, tsync_override_log_subdomains_off)
{
	const struct landlock_ruleset_attr ruleset_attr = {
		.scoped = LANDLOCK_SCOPE_SIGNAL,
	};
	struct audit_records records;
	struct thread_data child_data = {};
	int pipe_child[2], pipe_parent[2];
	char buffer;
	pthread_t thread;
	void *thread_ret;

	child_data.parent_pid = getppid();
	ASSERT_EQ(0, pipe2(pipe_child, O_CLOEXEC));
	child_data.pipe_child = pipe_child[1];
	ASSERT_EQ(0, pipe2(pipe_parent, O_CLOEXEC));
	child_data.pipe_parent = pipe_parent[0];
	child_data.ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	ASSERT_LE(0, child_data.ruleset_fd);

	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));

	child_data.mute_subdomains = true;

	/* Creates the sibling thread. */
	ASSERT_EQ(0, pthread_create(&thread, NULL, thread_sandbox_deny_twice,
				    &child_data));

	/*
	 * Phase 1: the sibling mutes subdomain logs, creates a domain, and
	 * triggers a denial.  The denial must not be logged.
	 */
	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));
	ASSERT_EQ(1, read(pipe_child[0], &buffer, 1));

	EXPECT_EQ(-EAGAIN, matches_log_signal(_metadata, self->audit_fd,
					      child_data.parent_pid, NULL));

	/* Drains any remaining records. */
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);

	/*
	 * Overrides the sibling's log_subdomains_off by calling TSYNC without
	 * LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF.
	 */
	ASSERT_EQ(0, landlock_restrict_self(child_data.ruleset_fd,
					    LANDLOCK_RESTRICT_SELF_TSYNC));

	/*
	 * Phase 2: the sibling stacks another domain and triggers a denial.
	 * Because TSYNC replaced its log_subdomains_off with 0, the new domain
	 * has log_status=LANDLOCK_LOG_PENDING.
	 */
	ASSERT_EQ(1, write(pipe_parent[1], ".", 1));
	ASSERT_EQ(1, read(pipe_child[0], &buffer, 1));

	/* The denial must be logged. */
	EXPECT_EQ(0, matches_log_signal(_metadata, self->audit_fd,
					child_data.parent_pid, NULL));

	EXPECT_EQ(0, close(pipe_child[0]));
	EXPECT_EQ(0, close(pipe_parent[1]));
	ASSERT_EQ(0, pthread_join(thread, &thread_ret));
	EXPECT_EQ(NULL, thread_ret);
}

FIXTURE(audit_flags)
{
	struct audit_filter audit_filter;
	int audit_fd;
	__u64 *domain_id;
};

FIXTURE_VARIANT(audit_flags)
{
	const int restrict_flags;
};

/* clang-format off */
FIXTURE_VARIANT_ADD(audit_flags, default) {
	/* clang-format on */
	.restrict_flags = 0,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(audit_flags, same_exec_off) {
	/* clang-format on */
	.restrict_flags = LANDLOCK_RESTRICT_SELF_LOG_SAME_EXEC_OFF,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(audit_flags, subdomains_off) {
	/* clang-format on */
	.restrict_flags = LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(audit_flags, cross_exec_on) {
	/* clang-format on */
	.restrict_flags = LANDLOCK_RESTRICT_SELF_LOG_NEW_EXEC_ON,
};

FIXTURE_SETUP(audit_flags)
{
	disable_caps(_metadata);
	set_cap(_metadata, CAP_AUDIT_CONTROL);
	self->audit_fd = audit_init_with_exe_filter(&self->audit_filter);
	EXPECT_LE(0, self->audit_fd)
	{
		const char *error_msg;

		/* kill "$(auditctl -s | sed -ne 's/^pid \([0-9]\+\)$/\1/p')" */
		if (self->audit_fd == -EEXIST)
			error_msg = "socket already in use (e.g. auditd)";
		else
			error_msg = strerror(-self->audit_fd);
		TH_LOG("Failed to initialize audit: %s", error_msg);
	}
	clear_cap(_metadata, CAP_AUDIT_CONTROL);

	self->domain_id = mmap(NULL, sizeof(*self->domain_id),
			       PROT_READ | PROT_WRITE,
			       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	ASSERT_NE(MAP_FAILED, self->domain_id);
	/* Domain IDs are greater or equal to 2^32. */
	*self->domain_id = 1;
}

FIXTURE_TEARDOWN(audit_flags)
{
	EXPECT_EQ(0, munmap(self->domain_id, sizeof(*self->domain_id)));

	set_cap(_metadata, CAP_AUDIT_CONTROL);
	EXPECT_EQ(0, audit_cleanup(self->audit_fd, &self->audit_filter));
	clear_cap(_metadata, CAP_AUDIT_CONTROL);
}

TEST_F(audit_flags, signal)
{
	int status;
	pid_t child;
	struct audit_records records;
	__u64 deallocated_dom = 2;

	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		const struct landlock_ruleset_attr ruleset_attr = {
			.scoped = LANDLOCK_SCOPE_SIGNAL,
		};
		int ruleset_fd;

		/* Add filesystem restrictions. */
		ruleset_fd = landlock_create_ruleset(&ruleset_attr,
						     sizeof(ruleset_attr), 0);
		ASSERT_LE(0, ruleset_fd);
		EXPECT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));
		ASSERT_EQ(0, landlock_restrict_self(ruleset_fd,
						    variant->restrict_flags));
		EXPECT_EQ(0, close(ruleset_fd));

		/* First signal checks to test log entries. */
		EXPECT_EQ(-1, kill(getppid(), 0));
		EXPECT_EQ(EPERM, errno);

		if (variant->restrict_flags &
		    LANDLOCK_RESTRICT_SELF_LOG_SAME_EXEC_OFF) {
			EXPECT_EQ(-EAGAIN, matches_log_signal(
						   _metadata, self->audit_fd,
						   getppid(), self->domain_id));
			EXPECT_EQ(*self->domain_id, 1);
		} else {
			__u64 allocated_dom = 3;

			EXPECT_EQ(0, matches_log_signal(
					     _metadata, self->audit_fd,
					     getppid(), self->domain_id));

			/* Checks domain information records. */
			EXPECT_EQ(0, matches_log_domain_allocated(
					     self->audit_fd, getpid(),
					     &allocated_dom));
			EXPECT_NE(*self->domain_id, 1);
			EXPECT_NE(*self->domain_id, 0);
			EXPECT_EQ(*self->domain_id, allocated_dom);
		}

		/* Second signal checks to test audit_count_records(). */
		EXPECT_EQ(-1, kill(getppid(), 0));
		EXPECT_EQ(EPERM, errno);

		/* Makes sure there is no superfluous logged records. */
		EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
		if (variant->restrict_flags &
		    LANDLOCK_RESTRICT_SELF_LOG_SAME_EXEC_OFF) {
			EXPECT_EQ(0, records.access);
		} else {
			EXPECT_EQ(1, records.access);
		}

		/* Updates filter rules to match the drop record. */
		set_cap(_metadata, CAP_AUDIT_CONTROL);
		EXPECT_EQ(0, audit_filter_drop(self->audit_fd, AUDIT_ADD_RULE));
		EXPECT_EQ(0,
			  audit_filter_exe(self->audit_fd, &self->audit_filter,
					   AUDIT_DEL_RULE));
		clear_cap(_metadata, CAP_AUDIT_CONTROL);

		_exit(_metadata->exit_code);
		return;
	}

	ASSERT_EQ(child, waitpid(child, &status, 0));
	if (WIFSIGNALED(status) || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != EXIT_SUCCESS)
		_metadata->exit_code = KSFT_FAIL;

	if (variant->restrict_flags &
	    LANDLOCK_RESTRICT_SELF_LOG_SAME_EXEC_OFF) {
		/*
		 * No deallocation record: denials=0 never matches a real
		 * record.
		 */
		EXPECT_EQ(-EAGAIN,
			  matches_log_domain_deallocated(self->audit_fd, 0, 0,
							 &deallocated_dom));
		EXPECT_EQ(deallocated_dom, 2);
	} else {
		EXPECT_EQ(0, matches_log_domain_deallocated(self->audit_fd, 2,
							    *self->domain_id,
							    &deallocated_dom));
		EXPECT_NE(deallocated_dom, 2);
		EXPECT_NE(deallocated_dom, 0);
		EXPECT_EQ(deallocated_dom, *self->domain_id);
	}
}

static int matches_log_fs_read_root(int audit_fd)
{
	return audit_match_record(
		audit_fd, AUDIT_LANDLOCK_ACCESS,
		REGEX_LANDLOCK_PREFIX
		" blockers=fs\\.read_dir path=\"/\" dev=\"[^\"]\\+\" ino=[0-9]\\+$",
		NULL);
}

FIXTURE(audit_exec)
{
	struct audit_filter audit_filter;
	int audit_fd;
};

FIXTURE_VARIANT(audit_exec)
{
	const int restrict_flags;
};

/* clang-format off */
FIXTURE_VARIANT_ADD(audit_exec, default) {
	/* clang-format on */
	.restrict_flags = 0,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(audit_exec, same_exec_off) {
	/* clang-format on */
	.restrict_flags = LANDLOCK_RESTRICT_SELF_LOG_SAME_EXEC_OFF,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(audit_exec, subdomains_off) {
	/* clang-format on */
	.restrict_flags = LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(audit_exec, cross_exec_on) {
	/* clang-format on */
	.restrict_flags = LANDLOCK_RESTRICT_SELF_LOG_NEW_EXEC_ON,
};

/* clang-format off */
FIXTURE_VARIANT_ADD(audit_exec, subdomains_off_and_cross_exec_on) {
	/* clang-format on */
	.restrict_flags = LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF |
			  LANDLOCK_RESTRICT_SELF_LOG_NEW_EXEC_ON,
};

FIXTURE_SETUP(audit_exec)
{
	disable_caps(_metadata);
	set_cap(_metadata, CAP_AUDIT_CONTROL);

	self->audit_fd = audit_init();
	EXPECT_LE(0, self->audit_fd)
	{
		const char *error_msg;

		/* kill "$(auditctl -s | sed -ne 's/^pid \([0-9]\+\)$/\1/p')" */
		if (self->audit_fd == -EEXIST)
			error_msg = "socket already in use (e.g. auditd)";
		else
			error_msg = strerror(-self->audit_fd);
		TH_LOG("Failed to initialize audit: %s", error_msg);
	}

	/* Applies test filter for the bin_wait_pipe_sandbox program. */
	EXPECT_EQ(0, audit_init_filter_exe(&self->audit_filter,
					   bin_wait_pipe_sandbox));
	EXPECT_EQ(0, audit_filter_exe(self->audit_fd, &self->audit_filter,
				      AUDIT_ADD_RULE));

	clear_cap(_metadata, CAP_AUDIT_CONTROL);
}

FIXTURE_TEARDOWN(audit_exec)
{
	set_cap(_metadata, CAP_AUDIT_CONTROL);
	EXPECT_EQ(0, audit_filter_exe(self->audit_fd, &self->audit_filter,
				      AUDIT_DEL_RULE));
	clear_cap(_metadata, CAP_AUDIT_CONTROL);
	EXPECT_EQ(0, close(self->audit_fd));
}

TEST_F(audit_exec, signal_and_open)
{
	struct audit_records records;
	int pipe_child[2], pipe_parent[2];
	char buf_parent;
	pid_t child;
	int status;

	ASSERT_EQ(0, pipe2(pipe_child, 0));
	ASSERT_EQ(0, pipe2(pipe_parent, 0));

	child = fork();
	ASSERT_LE(0, child);
	if (child == 0) {
		const struct landlock_ruleset_attr layer1 = {
			.scoped = LANDLOCK_SCOPE_SIGNAL,
		};
		char pipe_child_str[12], pipe_parent_str[12];
		char *const argv[] = { (char *)bin_wait_pipe_sandbox,
				       pipe_child_str, pipe_parent_str, NULL };
		int ruleset_fd;

		/* Passes the pipe FDs to the executed binary. */
		EXPECT_EQ(0, close(pipe_child[0]));
		EXPECT_EQ(0, close(pipe_parent[1]));
		snprintf(pipe_child_str, sizeof(pipe_child_str), "%d",
			 pipe_child[1]);
		snprintf(pipe_parent_str, sizeof(pipe_parent_str), "%d",
			 pipe_parent[0]);

		ruleset_fd =
			landlock_create_ruleset(&layer1, sizeof(layer1), 0);
		if (ruleset_fd < 0) {
			perror("Failed to create a ruleset");
			_exit(1);
		}
		prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
		if (landlock_restrict_self(ruleset_fd,
					   variant->restrict_flags)) {
			perror("Failed to restrict self");
			_exit(1);
		}
		close(ruleset_fd);

		ASSERT_EQ(0, execve(argv[0], argv, NULL))
		{
			TH_LOG("Failed to execute \"%s\": %s", argv[0],
			       strerror(errno));
		};
		_exit(1);
		return;
	}

	EXPECT_EQ(0, close(pipe_child[1]));
	EXPECT_EQ(0, close(pipe_parent[0]));

	/* Waits for the child. */
	EXPECT_EQ(1, read(pipe_child[0], &buf_parent, 1));

	/* Tests that there was no denial until now. */
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);

	/*
	 * Wait for the child to do a first denied action by layer1 and
	 * sandbox itself with layer2.
	 */
	EXPECT_EQ(1, write(pipe_parent[1], ".", 1));
	EXPECT_EQ(1, read(pipe_child[0], &buf_parent, 1));

	/* Tests that the audit record only matches the child. */
	if (variant->restrict_flags & LANDLOCK_RESTRICT_SELF_LOG_NEW_EXEC_ON) {
		/* Matches the current domain. */
		EXPECT_EQ(0, matches_log_signal(_metadata, self->audit_fd,
						getpid(), NULL));
	}

	/* Checks that we didn't miss anything. */
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);

	/*
	 * Wait for the child to do a second denied action by layer1 and
	 * layer2, and sandbox itself with layer3.
	 */
	EXPECT_EQ(1, write(pipe_parent[1], ".", 1));
	EXPECT_EQ(1, read(pipe_child[0], &buf_parent, 1));

	/* Tests that the audit record only matches the child. */
	if (variant->restrict_flags & LANDLOCK_RESTRICT_SELF_LOG_NEW_EXEC_ON) {
		/* Matches the current domain. */
		EXPECT_EQ(0, matches_log_signal(_metadata, self->audit_fd,
						getpid(), NULL));
	}

	if (!(variant->restrict_flags &
	      LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF)) {
		/* Matches the child domain. */
		EXPECT_EQ(0, matches_log_fs_read_root(self->audit_fd));
	}

	/* Checks that we didn't miss anything. */
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);

	/* Waits for the child to terminate. */
	EXPECT_EQ(1, write(pipe_parent[1], ".", 1));
	ASSERT_EQ(child, waitpid(child, &status, 0));
	ASSERT_EQ(1, WIFEXITED(status));
	ASSERT_EQ(0, WEXITSTATUS(status));

	/* Tests that the audit record only matches the child. */
	if (!(variant->restrict_flags &
	      LANDLOCK_RESTRICT_SELF_LOG_SUBDOMAINS_OFF)) {
		/*
		 * Matches the child domains, which tests that the
		 * llcred->domain_exec bitmask is correctly updated with a new
		 * domain.
		 */
		EXPECT_EQ(0, matches_log_fs_read_root(self->audit_fd));
		EXPECT_EQ(0, matches_log_signal(_metadata, self->audit_fd,
						getpid(), NULL));
	}

	/* Checks that we didn't miss anything. */
	EXPECT_EQ(0, audit_count_records(self->audit_fd, &records));
	EXPECT_EQ(0, records.access);
}

TEST_HARNESS_MAIN
