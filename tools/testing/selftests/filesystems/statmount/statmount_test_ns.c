// SPDX-License-Identifier: GPL-2.0-or-later

#define _GNU_SOURCE

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <linux/nsfs.h>
#include <linux/stat.h>

#include "statmount.h"
#include "../../kselftest.h"

#define NSID_PASS 0
#define NSID_FAIL 1
#define NSID_SKIP 2
#define NSID_ERROR 3

static void handle_result(int ret, const char *testname)
{
	if (ret == NSID_PASS)
		ksft_test_result_pass("%s\n", testname);
	else if (ret == NSID_FAIL)
		ksft_test_result_fail("%s\n", testname);
	else if (ret == NSID_ERROR)
		ksft_exit_fail_msg("%s\n", testname);
	else
		ksft_test_result_skip("%s\n", testname);
}

static inline int wait_for_pid(pid_t pid)
{
	int status, ret;

again:
	ret = waitpid(pid, &status, 0);
	if (ret == -1) {
		if (errno == EINTR)
			goto again;

		ksft_print_msg("waitpid returned -1, errno=%d\n", errno);
		return -1;
	}

	if (!WIFEXITED(status)) {
		ksft_print_msg(
		       "waitpid !WIFEXITED, WIFSIGNALED=%d, WTERMSIG=%d\n",
		       WIFSIGNALED(status), WTERMSIG(status));
		return -1;
	}

	ret = WEXITSTATUS(status);
	return ret;
}

static int get_mnt_ns_id(const char *mnt_ns, uint64_t *mnt_ns_id)
{
	int fd = open(mnt_ns, O_RDONLY);

	if (fd < 0) {
		ksft_print_msg("failed to open for ns %s: %s\n",
			       mnt_ns, strerror(errno));
		sleep(60);
		return NSID_ERROR;
	}

	if (ioctl(fd, NS_GET_MNTNS_ID, mnt_ns_id) < 0) {
		ksft_print_msg("failed to get the nsid for ns %s: %s\n",
			       mnt_ns, strerror(errno));
		return NSID_ERROR;
	}
	close(fd);
	return NSID_PASS;
}

static int get_mnt_id(const char *path, uint64_t *mnt_id)
{
	struct statx sx;
	int ret;

	ret = statx(AT_FDCWD, path, 0, STATX_MNT_ID_UNIQUE, &sx);
	if (ret == -1) {
		ksft_print_msg("retrieving unique mount ID for %s: %s\n", path,
			       strerror(errno));
		return NSID_ERROR;
	}

	if (!(sx.stx_mask & STATX_MNT_ID_UNIQUE)) {
		ksft_print_msg("no unique mount ID available for %s\n", path);
		return NSID_ERROR;
	}

	*mnt_id = sx.stx_mnt_id;
	return NSID_PASS;
}

static int write_file(const char *path, const char *val)
{
	int fd = open(path, O_WRONLY);
	size_t len = strlen(val);
	int ret;

	if (fd == -1) {
		ksft_print_msg("opening %s for write: %s\n", path, strerror(errno));
		return NSID_ERROR;
	}

	ret = write(fd, val, len);
	if (ret == -1) {
		ksft_print_msg("writing to %s: %s\n", path, strerror(errno));
		return NSID_ERROR;
	}
	if (ret != len) {
		ksft_print_msg("short write to %s\n", path);
		return NSID_ERROR;
	}

	ret = close(fd);
	if (ret == -1) {
		ksft_print_msg("closing %s\n", path);
		return NSID_ERROR;
	}

	return NSID_PASS;
}

static int setup_namespace(void)
{
	int ret;
	char buf[32];
	uid_t uid = getuid();
	gid_t gid = getgid();

	ret = unshare(CLONE_NEWNS|CLONE_NEWUSER|CLONE_NEWPID);
	if (ret == -1)
		ksft_exit_fail_msg("unsharing mountns and userns: %s\n",
				   strerror(errno));

	sprintf(buf, "0 %d 1", uid);
	ret = write_file("/proc/self/uid_map", buf);
	if (ret != NSID_PASS)
		return ret;
	ret = write_file("/proc/self/setgroups", "deny");
	if (ret != NSID_PASS)
		return ret;
	sprintf(buf, "0 %d 1", gid);
	ret = write_file("/proc/self/gid_map", buf);
	if (ret != NSID_PASS)
		return ret;

	ret = mount("", "/", NULL, MS_REC|MS_PRIVATE, NULL);
	if (ret == -1) {
		ksft_print_msg("making mount tree private: %s\n",
			       strerror(errno));
		return NSID_ERROR;
	}

	return NSID_PASS;
}

static int _test_statmount_mnt_ns_id(void)
{
	struct statmount sm;
	uint64_t mnt_ns_id;
	uint64_t root_id;
	int ret;

	ret = get_mnt_ns_id("/proc/self/ns/mnt", &mnt_ns_id);
	if (ret != NSID_PASS)
		return ret;

	ret = get_mnt_id("/", &root_id);
	if (ret != NSID_PASS)
		return ret;

	ret = statmount(root_id, 0, STATMOUNT_MNT_NS_ID, &sm, sizeof(sm), 0);
	if (ret == -1) {
		ksft_print_msg("statmount mnt ns id: %s\n", strerror(errno));
		return NSID_ERROR;
	}

	if (sm.size != sizeof(sm)) {
		ksft_print_msg("unexpected size: %u != %u\n", sm.size,
			       (uint32_t)sizeof(sm));
		return NSID_FAIL;
	}
	if (sm.mask != STATMOUNT_MNT_NS_ID) {
		ksft_print_msg("statmount mnt ns id unavailable\n");
		return NSID_SKIP;
	}

	if (sm.mnt_ns_id != mnt_ns_id) {
		ksft_print_msg("unexpected mnt ns ID: 0x%llx != 0x%llx\n",
			       (unsigned long long)sm.mnt_ns_id,
			       (unsigned long long)mnt_ns_id);
		return NSID_FAIL;
	}

	return NSID_PASS;
}

static void test_statmount_mnt_ns_id(void)
{
	pid_t pid;
	int ret;

	pid = fork();
	if (pid < 0)
		ksft_exit_fail_msg("failed to fork: %s\n", strerror(errno));

	/* We're the original pid, wait for the result. */
	if (pid != 0) {
		ret = wait_for_pid(pid);
		handle_result(ret, "test statmount ns id");
		return;
	}

	ret = setup_namespace();
	if (ret != NSID_PASS)
		exit(ret);
	ret = _test_statmount_mnt_ns_id();
	exit(ret);
}

static int validate_external_listmount(pid_t pid, uint64_t child_nr_mounts)
{
	uint64_t list[256];
	uint64_t mnt_ns_id;
	uint64_t nr_mounts;
	char buf[256];
	int ret;

	/* Get the mount ns id for our child. */
	snprintf(buf, sizeof(buf), "/proc/%lu/ns/mnt", (unsigned long)pid);
	ret = get_mnt_ns_id(buf, &mnt_ns_id);

	nr_mounts = listmount(LSMT_ROOT, mnt_ns_id, 0, list, 256, 0);
	if (nr_mounts == (uint64_t)-1) {
		ksft_print_msg("listmount: %s\n", strerror(errno));
		return NSID_ERROR;
	}

	if (nr_mounts != child_nr_mounts) {
		ksft_print_msg("listmount results is %zi != %zi\n", nr_mounts,
			       child_nr_mounts);
		return NSID_FAIL;
	}

	/* Validate that all of our entries match our mnt_ns_id. */
	for (int i = 0; i < nr_mounts; i++) {
		struct statmount sm;

		ret = statmount(list[i], mnt_ns_id, STATMOUNT_MNT_NS_ID, &sm,
				sizeof(sm), 0);
		if (ret < 0) {
			ksft_print_msg("statmount mnt ns id: %s\n", strerror(errno));
			return NSID_ERROR;
		}

		if (sm.mask != STATMOUNT_MNT_NS_ID) {
			ksft_print_msg("statmount mnt ns id unavailable\n");
			return NSID_SKIP;
		}

		if (sm.mnt_ns_id != mnt_ns_id) {
			ksft_print_msg("listmount gave us the wrong ns id: 0x%llx != 0x%llx\n",
				       (unsigned long long)sm.mnt_ns_id,
				       (unsigned long long)mnt_ns_id);
			return NSID_FAIL;
		}
	}

	return NSID_PASS;
}

static void test_listmount_ns(void)
{
	uint64_t nr_mounts;
	char pval;
	int child_ready_pipe[2];
	int parent_ready_pipe[2];
	pid_t pid;
	int ret, child_ret;

	if (pipe(child_ready_pipe) < 0)
		ksft_exit_fail_msg("failed to create the child pipe: %s\n",
				   strerror(errno));
	if (pipe(parent_ready_pipe) < 0)
		ksft_exit_fail_msg("failed to create the parent pipe: %s\n",
				   strerror(errno));

	pid = fork();
	if (pid < 0)
		ksft_exit_fail_msg("failed to fork: %s\n", strerror(errno));

	if (pid == 0) {
		char cval;
		uint64_t list[256];

		close(child_ready_pipe[0]);
		close(parent_ready_pipe[1]);

		ret = setup_namespace();
		if (ret != NSID_PASS)
			exit(ret);

		nr_mounts = listmount(LSMT_ROOT, 0, 0, list, 256, 0);
		if (nr_mounts == (uint64_t)-1) {
			ksft_print_msg("listmount: %s\n", strerror(errno));
			exit(NSID_FAIL);
		}

		/*
		 * Tell our parent how many mounts we have, and then wait for it
		 * to tell us we're done.
		 */
		write(child_ready_pipe[1], &nr_mounts, sizeof(nr_mounts));
		read(parent_ready_pipe[0], &cval, sizeof(cval));
		exit(NSID_PASS);
	}

	close(child_ready_pipe[1]);
	close(parent_ready_pipe[0]);

	/* Wait until the child has created everything. */
	if (read(child_ready_pipe[0], &nr_mounts, sizeof(nr_mounts)) !=
	    sizeof(nr_mounts))
		ret = NSID_ERROR;

	ret = validate_external_listmount(pid, nr_mounts);

	if (write(parent_ready_pipe[1], &pval, sizeof(pval)) != sizeof(pval))
		ret = NSID_ERROR;

	child_ret = wait_for_pid(pid);
	if (child_ret != NSID_PASS)
		ret = child_ret;
	handle_result(ret, "test listmount ns id");
}

int main(void)
{
	int ret;

	ksft_print_header();
	ret = statmount(0, 0, 0, NULL, 0, 0);
	assert(ret == -1);
	if (errno == ENOSYS)
		ksft_exit_skip("statmount() syscall not supported\n");

	ksft_set_plan(2);
	test_statmount_mnt_ns_id();
	test_listmount_ns();

	if (ksft_get_fail_cnt() + ksft_get_error_cnt() > 0)
		ksft_exit_fail();
	else
		ksft_exit_pass();
}
