// SPDX-License-Identifier: GPL-2.0-or-later

#define _GNU_SOURCE
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

#include "../kselftest.h"

int sys_fchmodat2(int dfd, const char *filename, mode_t mode, int flags)
{
	int ret = syscall(__NR_fchmodat2, dfd, filename, mode, flags);

	return ret >= 0 ? ret : -errno;
}

int setup_testdir(void)
{
	int dfd, ret;
	char dirname[] = "/tmp/ksft-fchmodat2.XXXXXX";

	/* Make the top-level directory. */
	if (!mkdtemp(dirname))
		ksft_exit_fail_msg("%s: failed to create tmpdir\n", __func__);

	dfd = open(dirname, O_PATH | O_DIRECTORY);
	if (dfd < 0)
		ksft_exit_fail_msg("%s: failed to open tmpdir\n", __func__);

	ret = openat(dfd, "regfile", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (ret < 0)
		ksft_exit_fail_msg("%s: failed to create file in tmpdir\n",
				__func__);
	close(ret);

	ret = symlinkat("regfile", dfd, "symlink");
	if (ret < 0)
		ksft_exit_fail_msg("%s: failed to create symlink in tmpdir\n",
				__func__);

	return dfd;
}

int expect_mode(int dfd, const char *filename, mode_t expect_mode)
{
	struct stat st;
	int ret = fstatat(dfd, filename, &st, AT_SYMLINK_NOFOLLOW);

	if (ret)
		ksft_exit_fail_msg("%s: %s: fstatat failed\n",
				__func__, filename);

	return (st.st_mode == expect_mode);
}

void test_regfile(void)
{
	int dfd, ret;

	dfd = setup_testdir();

	ret = sys_fchmodat2(dfd, "regfile", 0640, 0);

	if (ret < 0)
		ksft_exit_fail_msg("%s: fchmodat2(noflag) failed\n", __func__);

	if (!expect_mode(dfd, "regfile", 0100640))
		ksft_exit_fail_msg("%s: wrong file mode bits after fchmodat2\n",
				__func__);

	ret = sys_fchmodat2(dfd, "regfile", 0600, AT_SYMLINK_NOFOLLOW);

	if (ret < 0)
		ksft_exit_fail_msg("%s: fchmodat2(AT_SYMLINK_NOFOLLOW) failed\n",
				__func__);

	if (!expect_mode(dfd, "regfile", 0100600))
		ksft_exit_fail_msg("%s: wrong file mode bits after fchmodat2 with nofollow\n",
				__func__);

	ksft_test_result_pass("fchmodat2(regfile)\n");
}

void test_symlink(void)
{
	int dfd, ret;

	dfd = setup_testdir();

	ret = sys_fchmodat2(dfd, "symlink", 0640, 0);

	if (ret < 0)
		ksft_exit_fail_msg("%s: fchmodat2(noflag) failed\n", __func__);

	if (!expect_mode(dfd, "regfile", 0100640))
		ksft_exit_fail_msg("%s: wrong file mode bits after fchmodat2\n",
				__func__);

	if (!expect_mode(dfd, "symlink", 0120777))
		ksft_exit_fail_msg("%s: wrong symlink mode bits after fchmodat2\n",
				__func__);

	ret = sys_fchmodat2(dfd, "symlink", 0600, AT_SYMLINK_NOFOLLOW);

	/*
	 * On certain filesystems (xfs or btrfs), chmod operation fails. So we
	 * first check the symlink target but if the operation fails we mark the
	 * test as skipped.
	 *
	 * https://sourceware.org/legacy-ml/libc-alpha/2020-02/msg00467.html
	 */
	if (ret == 0 && !expect_mode(dfd, "symlink", 0120600))
		ksft_exit_fail_msg("%s: wrong symlink mode bits after fchmodat2 with nofollow\n",
				__func__);

	if (!expect_mode(dfd, "regfile", 0100640))
		ksft_exit_fail_msg("%s: wrong file mode bits after fchmodat2 with nofollow\n",
				__func__);

	if (ret != 0)
		ksft_test_result_skip("fchmodat2(symlink)\n");
	else
		ksft_test_result_pass("fchmodat2(symlink)\n");
}

#define NUM_TESTS 2

int main(int argc, char **argv)
{
	ksft_print_header();
	ksft_set_plan(NUM_TESTS);

	test_regfile();
	test_symlink();

	if (ksft_get_fail_cnt() + ksft_get_error_cnt() > 0)
		ksft_exit_fail();
	else
		ksft_exit_pass();
}
