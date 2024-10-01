/* SPDX-License-Identifier: GPL-2.0 */

#define _GNU_SOURCE
#include <linux/limits.h>
#include <linux/sched.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>

#include "../kselftest.h"
#include "cgroup_util.h"

static int touch_anon(char *buf, size_t size)
{
	int fd;
	char *pos = buf;

	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		return -1;

	while (size > 0) {
		ssize_t ret = read(fd, pos, size);

		if (ret < 0) {
			if (errno != EINTR) {
				close(fd);
				return -1;
			}
		} else {
			pos += ret;
			size -= ret;
		}
	}
	close(fd);

	return 0;
}

static int alloc_and_touch_anon_noexit(const char *cgroup, void *arg)
{
	int ppid = getppid();
	size_t size = (size_t)arg;
	void *buf;

	buf = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON,
		   0, 0);
	if (buf == MAP_FAILED)
		return -1;

	if (touch_anon((char *)buf, size)) {
		munmap(buf, size);
		return -1;
	}

	while (getppid() == ppid)
		sleep(1);

	munmap(buf, size);
	return 0;
}

/*
 * Create a child process that allocates and touches 100MB, then waits to be
 * killed. Wait until the child is attached to the cgroup, kill all processes
 * in that cgroup and wait until "cgroup.procs" is empty. At this point try to
 * destroy the empty cgroup. The test helps detect race conditions between
 * dying processes leaving the cgroup and cgroup destruction path.
 */
static int test_cgcore_destroy(const char *root)
{
	int ret = KSFT_FAIL;
	char *cg_test = NULL;
	int child_pid;
	char buf[PAGE_SIZE];

	cg_test = cg_name(root, "cg_test");

	if (!cg_test)
		goto cleanup;

	for (int i = 0; i < 10; i++) {
		if (cg_create(cg_test))
			goto cleanup;

		child_pid = cg_run_nowait(cg_test, alloc_and_touch_anon_noexit,
					  (void *) MB(100));

		if (child_pid < 0)
			goto cleanup;

		/* wait for the child to enter cgroup */
		if (cg_wait_for_proc_count(cg_test, 1))
			goto cleanup;

		if (cg_killall(cg_test))
			goto cleanup;

		/* wait for cgroup to be empty */
		while (1) {
			if (cg_read(cg_test, "cgroup.procs", buf, sizeof(buf)))
				goto cleanup;
			if (buf[0] == '\0')
				break;
			usleep(1000);
		}

		if (rmdir(cg_test))
			goto cleanup;

		if (waitpid(child_pid, NULL, 0) < 0)
			goto cleanup;
	}
	ret = KSFT_PASS;
cleanup:
	if (cg_test)
		cg_destroy(cg_test);
	free(cg_test);
	return ret;
}

/*
 * A(0) - B(0) - C(1)
 *        \ D(0)
 *
 * A, B and C's "populated" fields would be 1 while D's 0.
 * test that after the one process in C is moved to root,
 * A,B and C's "populated" fields would flip to "0" and file
 * modified events will be generated on the
 * "cgroup.events" files of both cgroups.
 */
static int test_cgcore_populated(const char *root)
{
	int ret = KSFT_FAIL;
	int err;
	char *cg_test_a = NULL, *cg_test_b = NULL;
	char *cg_test_c = NULL, *cg_test_d = NULL;
	int cgroup_fd = -EBADF;
	pid_t pid;

	cg_test_a = cg_name(root, "cg_test_a");
	cg_test_b = cg_name(root, "cg_test_a/cg_test_b");
	cg_test_c = cg_name(root, "cg_test_a/cg_test_b/cg_test_c");
	cg_test_d = cg_name(root, "cg_test_a/cg_test_b/cg_test_d");

	if (!cg_test_a || !cg_test_b || !cg_test_c || !cg_test_d)
		goto cleanup;

	if (cg_create(cg_test_a))
		goto cleanup;

	if (cg_create(cg_test_b))
		goto cleanup;

	if (cg_create(cg_test_c))
		goto cleanup;

	if (cg_create(cg_test_d))
		goto cleanup;

	if (cg_enter_current(cg_test_c))
		goto cleanup;

	if (cg_read_strcmp(cg_test_a, "cgroup.events", "populated 1\n"))
		goto cleanup;

	if (cg_read_strcmp(cg_test_b, "cgroup.events", "populated 1\n"))
		goto cleanup;

	if (cg_read_strcmp(cg_test_c, "cgroup.events", "populated 1\n"))
		goto cleanup;

	if (cg_read_strcmp(cg_test_d, "cgroup.events", "populated 0\n"))
		goto cleanup;

	if (cg_enter_current(root))
		goto cleanup;

	if (cg_read_strcmp(cg_test_a, "cgroup.events", "populated 0\n"))
		goto cleanup;

	if (cg_read_strcmp(cg_test_b, "cgroup.events", "populated 0\n"))
		goto cleanup;

	if (cg_read_strcmp(cg_test_c, "cgroup.events", "populated 0\n"))
		goto cleanup;

	if (cg_read_strcmp(cg_test_d, "cgroup.events", "populated 0\n"))
		goto cleanup;

	/* Test that we can directly clone into a new cgroup. */
	cgroup_fd = dirfd_open_opath(cg_test_d);
	if (cgroup_fd < 0)
		goto cleanup;

	pid = clone_into_cgroup(cgroup_fd);
	if (pid < 0) {
		if (errno == ENOSYS)
			goto cleanup_pass;
		goto cleanup;
	}

	if (pid == 0) {
		if (raise(SIGSTOP))
			exit(EXIT_FAILURE);
		exit(EXIT_SUCCESS);
	}

	err = cg_read_strcmp(cg_test_d, "cgroup.events", "populated 1\n");

	(void)clone_reap(pid, WSTOPPED);
	(void)kill(pid, SIGCONT);
	(void)clone_reap(pid, WEXITED);

	if (err)
		goto cleanup;

	if (cg_read_strcmp(cg_test_d, "cgroup.events", "populated 0\n"))
		goto cleanup;

	/* Remove cgroup. */
	if (cg_test_d) {
		cg_destroy(cg_test_d);
		free(cg_test_d);
		cg_test_d = NULL;
	}

	pid = clone_into_cgroup(cgroup_fd);
	if (pid < 0)
		goto cleanup_pass;
	if (pid == 0)
		exit(EXIT_SUCCESS);
	(void)clone_reap(pid, WEXITED);
	goto cleanup;

cleanup_pass:
	ret = KSFT_PASS;

cleanup:
	if (cg_test_d)
		cg_destroy(cg_test_d);
	if (cg_test_c)
		cg_destroy(cg_test_c);
	if (cg_test_b)
		cg_destroy(cg_test_b);
	if (cg_test_a)
		cg_destroy(cg_test_a);
	free(cg_test_d);
	free(cg_test_c);
	free(cg_test_b);
	free(cg_test_a);
	if (cgroup_fd >= 0)
		close(cgroup_fd);
	return ret;
}

/*
 * A (domain threaded) - B (threaded) - C (domain)
 *
 * test that C can't be used until it is turned into a
 * threaded cgroup.  "cgroup.type" file will report "domain (invalid)" in
 * these cases. Operations which fail due to invalid topology use
 * EOPNOTSUPP as the errno.
 */
static int test_cgcore_invalid_domain(const char *root)
{
	int ret = KSFT_FAIL;
	char *grandparent = NULL, *parent = NULL, *child = NULL;

	grandparent = cg_name(root, "cg_test_grandparent");
	parent = cg_name(root, "cg_test_grandparent/cg_test_parent");
	child = cg_name(root, "cg_test_grandparent/cg_test_parent/cg_test_child");
	if (!parent || !child || !grandparent)
		goto cleanup;

	if (cg_create(grandparent))
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;

	if (cg_create(child))
		goto cleanup;

	if (cg_write(parent, "cgroup.type", "threaded"))
		goto cleanup;

	if (cg_read_strcmp(child, "cgroup.type", "domain invalid\n"))
		goto cleanup;

	if (!cg_enter_current(child))
		goto cleanup;

	if (errno != EOPNOTSUPP)
		goto cleanup;

	if (!clone_into_cgroup_run_wait(child))
		goto cleanup;

	if (errno == ENOSYS)
		goto cleanup_pass;

	if (errno != EOPNOTSUPP)
		goto cleanup;

cleanup_pass:
	ret = KSFT_PASS;

cleanup:
	cg_enter_current(root);
	if (child)
		cg_destroy(child);
	if (parent)
		cg_destroy(parent);
	if (grandparent)
		cg_destroy(grandparent);
	free(child);
	free(parent);
	free(grandparent);
	return ret;
}

/*
 * Test that when a child becomes threaded
 * the parent type becomes domain threaded.
 */
static int test_cgcore_parent_becomes_threaded(const char *root)
{
	int ret = KSFT_FAIL;
	char *parent = NULL, *child = NULL;

	parent = cg_name(root, "cg_test_parent");
	child = cg_name(root, "cg_test_parent/cg_test_child");
	if (!parent || !child)
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;

	if (cg_create(child))
		goto cleanup;

	if (cg_write(child, "cgroup.type", "threaded"))
		goto cleanup;

	if (cg_read_strcmp(parent, "cgroup.type", "domain threaded\n"))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (child)
		cg_destroy(child);
	if (parent)
		cg_destroy(parent);
	free(child);
	free(parent);
	return ret;

}

/*
 * Test that there's no internal process constrain on threaded cgroups.
 * You can add threads/processes on a parent with a controller enabled.
 */
static int test_cgcore_no_internal_process_constraint_on_threads(const char *root)
{
	int ret = KSFT_FAIL;
	char *parent = NULL, *child = NULL;

	if (cg_read_strstr(root, "cgroup.controllers", "cpu") ||
	    cg_write(root, "cgroup.subtree_control", "+cpu")) {
		ret = KSFT_SKIP;
		goto cleanup;
	}

	parent = cg_name(root, "cg_test_parent");
	child = cg_name(root, "cg_test_parent/cg_test_child");
	if (!parent || !child)
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;

	if (cg_create(child))
		goto cleanup;

	if (cg_write(parent, "cgroup.type", "threaded"))
		goto cleanup;

	if (cg_write(child, "cgroup.type", "threaded"))
		goto cleanup;

	if (cg_write(parent, "cgroup.subtree_control", "+cpu"))
		goto cleanup;

	if (cg_enter_current(parent))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	cg_enter_current(root);
	cg_enter_current(root);
	if (child)
		cg_destroy(child);
	if (parent)
		cg_destroy(parent);
	free(child);
	free(parent);
	return ret;
}

/*
 * Test that you can't enable a controller on a child if it's not enabled
 * on the parent.
 */
static int test_cgcore_top_down_constraint_enable(const char *root)
{
	int ret = KSFT_FAIL;
	char *parent = NULL, *child = NULL;

	parent = cg_name(root, "cg_test_parent");
	child = cg_name(root, "cg_test_parent/cg_test_child");
	if (!parent || !child)
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;

	if (cg_create(child))
		goto cleanup;

	if (!cg_write(child, "cgroup.subtree_control", "+memory"))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (child)
		cg_destroy(child);
	if (parent)
		cg_destroy(parent);
	free(child);
	free(parent);
	return ret;
}

/*
 * Test that you can't disable a controller on a parent
 * if it's enabled in a child.
 */
static int test_cgcore_top_down_constraint_disable(const char *root)
{
	int ret = KSFT_FAIL;
	char *parent = NULL, *child = NULL;

	parent = cg_name(root, "cg_test_parent");
	child = cg_name(root, "cg_test_parent/cg_test_child");
	if (!parent || !child)
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;

	if (cg_create(child))
		goto cleanup;

	if (cg_write(parent, "cgroup.subtree_control", "+memory"))
		goto cleanup;

	if (cg_write(child, "cgroup.subtree_control", "+memory"))
		goto cleanup;

	if (!cg_write(parent, "cgroup.subtree_control", "-memory"))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (child)
		cg_destroy(child);
	if (parent)
		cg_destroy(parent);
	free(child);
	free(parent);
	return ret;
}

/*
 * Test internal process constraint.
 * You can't add a pid to a domain parent if a controller is enabled.
 */
static int test_cgcore_internal_process_constraint(const char *root)
{
	int ret = KSFT_FAIL;
	char *parent = NULL, *child = NULL;

	parent = cg_name(root, "cg_test_parent");
	child = cg_name(root, "cg_test_parent/cg_test_child");
	if (!parent || !child)
		goto cleanup;

	if (cg_create(parent))
		goto cleanup;

	if (cg_create(child))
		goto cleanup;

	if (cg_write(parent, "cgroup.subtree_control", "+memory"))
		goto cleanup;

	if (!cg_enter_current(parent))
		goto cleanup;

	if (!clone_into_cgroup_run_wait(parent))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	if (child)
		cg_destroy(child);
	if (parent)
		cg_destroy(parent);
	free(child);
	free(parent);
	return ret;
}

static void *dummy_thread_fn(void *arg)
{
	return (void *)(size_t)pause();
}

/*
 * Test threadgroup migration.
 * All threads of a process are migrated together.
 */
static int test_cgcore_proc_migration(const char *root)
{
	int ret = KSFT_FAIL;
	int t, c_threads = 0, n_threads = 13;
	char *src = NULL, *dst = NULL;
	pthread_t threads[n_threads];

	src = cg_name(root, "cg_src");
	dst = cg_name(root, "cg_dst");
	if (!src || !dst)
		goto cleanup;

	if (cg_create(src))
		goto cleanup;
	if (cg_create(dst))
		goto cleanup;

	if (cg_enter_current(src))
		goto cleanup;

	for (c_threads = 0; c_threads < n_threads; ++c_threads) {
		if (pthread_create(&threads[c_threads], NULL, dummy_thread_fn, NULL))
			goto cleanup;
	}

	cg_enter_current(dst);
	if (cg_read_lc(dst, "cgroup.threads") != n_threads + 1)
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	for (t = 0; t < c_threads; ++t) {
		pthread_cancel(threads[t]);
	}

	for (t = 0; t < c_threads; ++t) {
		pthread_join(threads[t], NULL);
	}

	cg_enter_current(root);

	if (dst)
		cg_destroy(dst);
	if (src)
		cg_destroy(src);
	free(dst);
	free(src);
	return ret;
}

static void *migrating_thread_fn(void *arg)
{
	int g, i, n_iterations = 1000;
	char **grps = arg;
	char lines[3][PATH_MAX];

	for (g = 1; g < 3; ++g)
		snprintf(lines[g], sizeof(lines[g]), "0::%s", grps[g] + strlen(grps[0]));

	for (i = 0; i < n_iterations; ++i) {
		cg_enter_current_thread(grps[(i % 2) + 1]);

		if (proc_read_strstr(0, 1, "cgroup", lines[(i % 2) + 1]))
			return (void *)-1;
	}
	return NULL;
}

/*
 * Test single thread migration.
 * Threaded cgroups allow successful migration of a thread.
 */
static int test_cgcore_thread_migration(const char *root)
{
	int ret = KSFT_FAIL;
	char *dom = NULL;
	char line[PATH_MAX];
	char *grps[3] = { (char *)root, NULL, NULL };
	pthread_t thr;
	void *retval;

	dom = cg_name(root, "cg_dom");
	grps[1] = cg_name(root, "cg_dom/cg_src");
	grps[2] = cg_name(root, "cg_dom/cg_dst");
	if (!grps[1] || !grps[2] || !dom)
		goto cleanup;

	if (cg_create(dom))
		goto cleanup;
	if (cg_create(grps[1]))
		goto cleanup;
	if (cg_create(grps[2]))
		goto cleanup;

	if (cg_write(grps[1], "cgroup.type", "threaded"))
		goto cleanup;
	if (cg_write(grps[2], "cgroup.type", "threaded"))
		goto cleanup;

	if (cg_enter_current(grps[1]))
		goto cleanup;

	if (pthread_create(&thr, NULL, migrating_thread_fn, grps))
		goto cleanup;

	if (pthread_join(thr, &retval))
		goto cleanup;

	if (retval)
		goto cleanup;

	snprintf(line, sizeof(line), "0::%s", grps[1] + strlen(grps[0]));
	if (proc_read_strstr(0, 1, "cgroup", line))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	cg_enter_current(root);
	if (grps[2])
		cg_destroy(grps[2]);
	if (grps[1])
		cg_destroy(grps[1]);
	if (dom)
		cg_destroy(dom);
	free(grps[2]);
	free(grps[1]);
	free(dom);
	return ret;
}

/*
 * cgroup migration permission check should be performed based on the
 * credentials at the time of open instead of write.
 */
static int test_cgcore_lesser_euid_open(const char *root)
{
	const uid_t test_euid = 65534;	/* usually nobody, any !root is fine */
	int ret = KSFT_FAIL;
	char *cg_test_a = NULL, *cg_test_b = NULL;
	char *cg_test_a_procs = NULL, *cg_test_b_procs = NULL;
	int cg_test_b_procs_fd = -1;
	uid_t saved_uid;

	cg_test_a = cg_name(root, "cg_test_a");
	cg_test_b = cg_name(root, "cg_test_b");

	if (!cg_test_a || !cg_test_b)
		goto cleanup;

	cg_test_a_procs = cg_name(cg_test_a, "cgroup.procs");
	cg_test_b_procs = cg_name(cg_test_b, "cgroup.procs");

	if (!cg_test_a_procs || !cg_test_b_procs)
		goto cleanup;

	if (cg_create(cg_test_a) || cg_create(cg_test_b))
		goto cleanup;

	if (cg_enter_current(cg_test_a))
		goto cleanup;

	if (chown(cg_test_a_procs, test_euid, -1) ||
	    chown(cg_test_b_procs, test_euid, -1))
		goto cleanup;

	saved_uid = geteuid();
	if (seteuid(test_euid))
		goto cleanup;

	cg_test_b_procs_fd = open(cg_test_b_procs, O_RDWR);

	if (seteuid(saved_uid))
		goto cleanup;

	if (cg_test_b_procs_fd < 0)
		goto cleanup;

	if (write(cg_test_b_procs_fd, "0", 1) >= 0 || errno != EACCES)
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	cg_enter_current(root);
	if (cg_test_b_procs_fd >= 0)
		close(cg_test_b_procs_fd);
	if (cg_test_b)
		cg_destroy(cg_test_b);
	if (cg_test_a)
		cg_destroy(cg_test_a);
	free(cg_test_b_procs);
	free(cg_test_a_procs);
	free(cg_test_b);
	free(cg_test_a);
	return ret;
}

struct lesser_ns_open_thread_arg {
	const char	*path;
	int		fd;
	int		err;
};

static int lesser_ns_open_thread_fn(void *arg)
{
	struct lesser_ns_open_thread_arg *targ = arg;

	targ->fd = open(targ->path, O_RDWR);
	targ->err = errno;
	return 0;
}

/*
 * cgroup migration permission check should be performed based on the cgroup
 * namespace at the time of open instead of write.
 */
static int test_cgcore_lesser_ns_open(const char *root)
{
	static char stack[65536];
	const uid_t test_euid = 65534;	/* usually nobody, any !root is fine */
	int ret = KSFT_FAIL;
	char *cg_test_a = NULL, *cg_test_b = NULL;
	char *cg_test_a_procs = NULL, *cg_test_b_procs = NULL;
	int cg_test_b_procs_fd = -1;
	struct lesser_ns_open_thread_arg targ = { .fd = -1 };
	pid_t pid;
	int status;

	cg_test_a = cg_name(root, "cg_test_a");
	cg_test_b = cg_name(root, "cg_test_b");

	if (!cg_test_a || !cg_test_b)
		goto cleanup;

	cg_test_a_procs = cg_name(cg_test_a, "cgroup.procs");
	cg_test_b_procs = cg_name(cg_test_b, "cgroup.procs");

	if (!cg_test_a_procs || !cg_test_b_procs)
		goto cleanup;

	if (cg_create(cg_test_a) || cg_create(cg_test_b))
		goto cleanup;

	if (cg_enter_current(cg_test_b))
		goto cleanup;

	if (chown(cg_test_a_procs, test_euid, -1) ||
	    chown(cg_test_b_procs, test_euid, -1))
		goto cleanup;

	targ.path = cg_test_b_procs;
	pid = clone(lesser_ns_open_thread_fn, stack + sizeof(stack),
		    CLONE_NEWCGROUP | CLONE_FILES | CLONE_VM | SIGCHLD,
		    &targ);
	if (pid < 0)
		goto cleanup;

	if (waitpid(pid, &status, 0) < 0)
		goto cleanup;

	if (!WIFEXITED(status))
		goto cleanup;

	cg_test_b_procs_fd = targ.fd;
	if (cg_test_b_procs_fd < 0)
		goto cleanup;

	if (cg_enter_current(cg_test_a))
		goto cleanup;

	if ((status = write(cg_test_b_procs_fd, "0", 1)) >= 0 || errno != ENOENT)
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	cg_enter_current(root);
	if (cg_test_b_procs_fd >= 0)
		close(cg_test_b_procs_fd);
	if (cg_test_b)
		cg_destroy(cg_test_b);
	if (cg_test_a)
		cg_destroy(cg_test_a);
	free(cg_test_b_procs);
	free(cg_test_a_procs);
	free(cg_test_b);
	free(cg_test_a);
	return ret;
}

#define T(x) { x, #x }
struct corecg_test {
	int (*fn)(const char *root);
	const char *name;
} tests[] = {
	T(test_cgcore_internal_process_constraint),
	T(test_cgcore_top_down_constraint_enable),
	T(test_cgcore_top_down_constraint_disable),
	T(test_cgcore_no_internal_process_constraint_on_threads),
	T(test_cgcore_parent_becomes_threaded),
	T(test_cgcore_invalid_domain),
	T(test_cgcore_populated),
	T(test_cgcore_proc_migration),
	T(test_cgcore_thread_migration),
	T(test_cgcore_destroy),
	T(test_cgcore_lesser_euid_open),
	T(test_cgcore_lesser_ns_open),
};
#undef T

int main(int argc, char *argv[])
{
	char root[PATH_MAX];
	int i, ret = EXIT_SUCCESS;

	if (cg_find_unified_root(root, sizeof(root)))
		ksft_exit_skip("cgroup v2 isn't mounted\n");

	if (cg_read_strstr(root, "cgroup.subtree_control", "memory"))
		if (cg_write(root, "cgroup.subtree_control", "+memory"))
			ksft_exit_skip("Failed to set memory controller\n");

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		switch (tests[i].fn(root)) {
		case KSFT_PASS:
			ksft_test_result_pass("%s\n", tests[i].name);
			break;
		case KSFT_SKIP:
			ksft_test_result_skip("%s\n", tests[i].name);
			break;
		default:
			ret = EXIT_FAILURE;
			ksft_test_result_fail("%s\n", tests[i].name);
			break;
		}
	}

	return ret;
}
