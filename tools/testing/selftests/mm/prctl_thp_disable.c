// SPDX-License-Identifier: GPL-2.0
/*
 * Basic tests for PR_GET/SET_THP_DISABLE prctl calls
 *
 * Author(s): Usama Arif <usamaarif642@gmail.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>

#include "../kselftest_harness.h"
#include "thp_settings.h"
#include "vm_util.h"

#ifndef PR_THP_DISABLE_EXCEPT_ADVISED
#define PR_THP_DISABLE_EXCEPT_ADVISED (1 << 1)
#endif

enum thp_collapse_type {
	THP_COLLAPSE_NONE,
	THP_COLLAPSE_MADV_NOHUGEPAGE,
	THP_COLLAPSE_MADV_HUGEPAGE,	/* MADV_HUGEPAGE before access */
	THP_COLLAPSE_MADV_COLLAPSE,	/* MADV_COLLAPSE after access */
};

/*
 * Function to mmap a buffer, fault it in, madvise it appropriately (before
 * page fault for MADV_HUGE, and after for MADV_COLLAPSE), and check if the
 * mmap region is huge.
 * Returns:
 * 0 if test doesn't give hugepage
 * 1 if test gives a hugepage
 * -errno if mmap fails
 */
static int test_mmap_thp(enum thp_collapse_type madvise_buf, size_t pmdsize)
{
	char *mem, *mmap_mem;
	size_t mmap_size;
	int ret;

	/* For alignment purposes, we need twice the THP size. */
	mmap_size = 2 * pmdsize;
	mmap_mem = (char *)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
				    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mmap_mem == MAP_FAILED)
		return -errno;

	/* We need a THP-aligned memory area. */
	mem = (char *)(((uintptr_t)mmap_mem + pmdsize) & ~(pmdsize - 1));

	if (madvise_buf == THP_COLLAPSE_MADV_HUGEPAGE)
		madvise(mem, pmdsize, MADV_HUGEPAGE);
	else if (madvise_buf == THP_COLLAPSE_MADV_NOHUGEPAGE)
		madvise(mem, pmdsize, MADV_NOHUGEPAGE);

	/* Ensure memory is allocated */
	memset(mem, 1, pmdsize);

	if (madvise_buf == THP_COLLAPSE_MADV_COLLAPSE)
		madvise(mem, pmdsize, MADV_COLLAPSE);

	/* HACK: make sure we have a separate VMA that we can check reliably. */
	mprotect(mem, pmdsize, PROT_READ);

	ret = check_huge_anon(mem, 1, pmdsize);
	munmap(mmap_mem, mmap_size);
	return ret;
}

static void prctl_thp_disable_completely_test(struct __test_metadata *const _metadata,
					      size_t pmdsize,
					      enum thp_enabled thp_policy)
{
	ASSERT_EQ(prctl(PR_GET_THP_DISABLE, NULL, NULL, NULL, NULL), 1);

	/* tests after prctl overrides global policy */
	ASSERT_EQ(test_mmap_thp(THP_COLLAPSE_NONE, pmdsize), 0);

	ASSERT_EQ(test_mmap_thp(THP_COLLAPSE_MADV_NOHUGEPAGE, pmdsize), 0);

	ASSERT_EQ(test_mmap_thp(THP_COLLAPSE_MADV_HUGEPAGE, pmdsize), 0);

	ASSERT_EQ(test_mmap_thp(THP_COLLAPSE_MADV_COLLAPSE, pmdsize), 0);

	/* Reset to global policy */
	ASSERT_EQ(prctl(PR_SET_THP_DISABLE, 0, NULL, NULL, NULL), 0);

	/* tests after prctl is cleared, and only global policy is effective */
	ASSERT_EQ(test_mmap_thp(THP_COLLAPSE_NONE, pmdsize),
		  thp_policy == THP_ALWAYS ? 1 : 0);

	ASSERT_EQ(test_mmap_thp(THP_COLLAPSE_MADV_NOHUGEPAGE, pmdsize), 0);

	ASSERT_EQ(test_mmap_thp(THP_COLLAPSE_MADV_HUGEPAGE, pmdsize),
		  thp_policy == THP_NEVER ? 0 : 1);

	ASSERT_EQ(test_mmap_thp(THP_COLLAPSE_MADV_COLLAPSE, pmdsize), 1);
}

FIXTURE(prctl_thp_disable_completely)
{
	struct thp_settings settings;
	size_t pmdsize;
};

FIXTURE_VARIANT(prctl_thp_disable_completely)
{
	enum thp_enabled thp_policy;
};

FIXTURE_VARIANT_ADD(prctl_thp_disable_completely, never)
{
	.thp_policy = THP_NEVER,
};

FIXTURE_VARIANT_ADD(prctl_thp_disable_completely, madvise)
{
	.thp_policy = THP_MADVISE,
};

FIXTURE_VARIANT_ADD(prctl_thp_disable_completely, always)
{
	.thp_policy = THP_ALWAYS,
};

FIXTURE_SETUP(prctl_thp_disable_completely)
{
	if (!thp_available())
		SKIP(return, "Transparent Hugepages not available\n");

	self->pmdsize = read_pmd_pagesize();
	if (!self->pmdsize)
		SKIP(return, "Unable to read PMD size\n");

	if (prctl(PR_SET_THP_DISABLE, 1, NULL, NULL, NULL))
		SKIP(return, "Unable to disable THPs completely for the process\n");

	thp_save_settings();
	thp_read_settings(&self->settings);
	self->settings.thp_enabled = variant->thp_policy;
	self->settings.hugepages[sz2ord(self->pmdsize, getpagesize())].enabled = THP_INHERIT;
	thp_write_settings(&self->settings);
}

FIXTURE_TEARDOWN(prctl_thp_disable_completely)
{
	thp_restore_settings();
}

TEST_F(prctl_thp_disable_completely, nofork)
{
	prctl_thp_disable_completely_test(_metadata, self->pmdsize, variant->thp_policy);
}

TEST_F(prctl_thp_disable_completely, fork)
{
	int ret = 0;
	pid_t pid;

	/* Make sure prctl changes are carried across fork */
	pid = fork();
	ASSERT_GE(pid, 0);

	if (!pid) {
		prctl_thp_disable_completely_test(_metadata, self->pmdsize, variant->thp_policy);
		return;
	}

	wait(&ret);
	if (WIFEXITED(ret))
		ret = WEXITSTATUS(ret);
	else
		ret = -EINVAL;
	ASSERT_EQ(ret, 0);
}

static void prctl_thp_disable_except_madvise_test(struct __test_metadata *const _metadata,
						  size_t pmdsize,
						  enum thp_enabled thp_policy)
{
	ASSERT_EQ(prctl(PR_GET_THP_DISABLE, NULL, NULL, NULL, NULL), 3);

	/* tests after prctl overrides global policy */
	ASSERT_EQ(test_mmap_thp(THP_COLLAPSE_NONE, pmdsize), 0);

	ASSERT_EQ(test_mmap_thp(THP_COLLAPSE_MADV_NOHUGEPAGE, pmdsize), 0);

	ASSERT_EQ(test_mmap_thp(THP_COLLAPSE_MADV_HUGEPAGE, pmdsize),
		  thp_policy == THP_NEVER ? 0 : 1);

	ASSERT_EQ(test_mmap_thp(THP_COLLAPSE_MADV_COLLAPSE, pmdsize), 1);

	/* Reset to global policy */
	ASSERT_EQ(prctl(PR_SET_THP_DISABLE, 0, NULL, NULL, NULL), 0);

	/* tests after prctl is cleared, and only global policy is effective */
	ASSERT_EQ(test_mmap_thp(THP_COLLAPSE_NONE, pmdsize),
		  thp_policy == THP_ALWAYS ? 1 : 0);

	ASSERT_EQ(test_mmap_thp(THP_COLLAPSE_MADV_NOHUGEPAGE, pmdsize), 0);

	ASSERT_EQ(test_mmap_thp(THP_COLLAPSE_MADV_HUGEPAGE, pmdsize),
		  thp_policy == THP_NEVER ? 0 : 1);

	ASSERT_EQ(test_mmap_thp(THP_COLLAPSE_MADV_COLLAPSE, pmdsize), 1);
}

FIXTURE(prctl_thp_disable_except_madvise)
{
	struct thp_settings settings;
	size_t pmdsize;
};

FIXTURE_VARIANT(prctl_thp_disable_except_madvise)
{
	enum thp_enabled thp_policy;
};

FIXTURE_VARIANT_ADD(prctl_thp_disable_except_madvise, never)
{
	.thp_policy = THP_NEVER,
};

FIXTURE_VARIANT_ADD(prctl_thp_disable_except_madvise, madvise)
{
	.thp_policy = THP_MADVISE,
};

FIXTURE_VARIANT_ADD(prctl_thp_disable_except_madvise, always)
{
	.thp_policy = THP_ALWAYS,
};

FIXTURE_SETUP(prctl_thp_disable_except_madvise)
{
	if (!thp_available())
		SKIP(return, "Transparent Hugepages not available\n");

	self->pmdsize = read_pmd_pagesize();
	if (!self->pmdsize)
		SKIP(return, "Unable to read PMD size\n");

	if (prctl(PR_SET_THP_DISABLE, 1, PR_THP_DISABLE_EXCEPT_ADVISED, NULL, NULL))
		SKIP(return, "Unable to set PR_THP_DISABLE_EXCEPT_ADVISED\n");

	thp_save_settings();
	thp_read_settings(&self->settings);
	self->settings.thp_enabled = variant->thp_policy;
	self->settings.hugepages[sz2ord(self->pmdsize, getpagesize())].enabled = THP_INHERIT;
	thp_write_settings(&self->settings);
}

FIXTURE_TEARDOWN(prctl_thp_disable_except_madvise)
{
	thp_restore_settings();
}

TEST_F(prctl_thp_disable_except_madvise, nofork)
{
	prctl_thp_disable_except_madvise_test(_metadata, self->pmdsize, variant->thp_policy);
}

TEST_F(prctl_thp_disable_except_madvise, fork)
{
	int ret = 0;
	pid_t pid;

	/* Make sure prctl changes are carried across fork */
	pid = fork();
	ASSERT_GE(pid, 0);

	if (!pid) {
		prctl_thp_disable_except_madvise_test(_metadata, self->pmdsize,
						      variant->thp_policy);
		return;
	}

	wait(&ret);
	if (WIFEXITED(ret))
		ret = WEXITSTATUS(ret);
	else
		ret = -EINVAL;
	ASSERT_EQ(ret, 0);
}

TEST_HARNESS_MAIN
