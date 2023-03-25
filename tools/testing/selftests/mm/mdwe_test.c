// SPDX-License-Identifier: GPL-2.0

#ifdef __aarch64__
#include <asm/hwcap.h>
#endif

#include <linux/mman.h>
#include <linux/prctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../kselftest_harness.h"

#ifndef __aarch64__
# define PROT_BTI	0
#endif

TEST(prctl_flags)
{
	EXPECT_LT(prctl(PR_SET_MDWE, 7L, 0L, 0L, 0L), 0);
	EXPECT_LT(prctl(PR_SET_MDWE, 0L, 7L, 0L, 0L), 0);
	EXPECT_LT(prctl(PR_SET_MDWE, 0L, 0L, 7L, 0L), 0);
	EXPECT_LT(prctl(PR_SET_MDWE, 0L, 0L, 0L, 7L), 0);

	EXPECT_LT(prctl(PR_GET_MDWE, 7L, 0L, 0L, 0L), 0);
	EXPECT_LT(prctl(PR_GET_MDWE, 0L, 7L, 0L, 0L), 0);
	EXPECT_LT(prctl(PR_GET_MDWE, 0L, 0L, 7L, 0L), 0);
	EXPECT_LT(prctl(PR_GET_MDWE, 0L, 0L, 0L, 7L), 0);
}

FIXTURE(mdwe)
{
	void *p;
	int flags;
	size_t size;
	pid_t pid;
};

FIXTURE_VARIANT(mdwe)
{
	bool enabled;
	bool forked;
};

FIXTURE_VARIANT_ADD(mdwe, stock)
{
        .enabled = false,
	.forked = false,
};

FIXTURE_VARIANT_ADD(mdwe, enabled)
{
        .enabled = true,
	.forked = false,
};

FIXTURE_VARIANT_ADD(mdwe, forked)
{
        .enabled = true,
	.forked = true,
};

FIXTURE_SETUP(mdwe)
{
	int ret, status;

	self->p = NULL;
	self->flags = MAP_SHARED | MAP_ANONYMOUS;
	self->size = getpagesize();

	if (!variant->enabled)
		return;

	ret = prctl(PR_SET_MDWE, PR_MDWE_REFUSE_EXEC_GAIN, 0L, 0L, 0L);
	ASSERT_EQ(ret, 0) {
		TH_LOG("PR_SET_MDWE failed or unsupported");
	}

	ret = prctl(PR_GET_MDWE, 0L, 0L, 0L, 0L);
	ASSERT_EQ(ret, 1);

	if (variant->forked) {
		self->pid = fork();
		ASSERT_GE(self->pid, 0) {
			TH_LOG("fork failed\n");
		}

		if (self->pid > 0) {
			ret = waitpid(self->pid, &status, 0);
			ASSERT_TRUE(WIFEXITED(status));
			exit(WEXITSTATUS(status));
		}
	}
}

FIXTURE_TEARDOWN(mdwe)
{
	if (self->p && self->p != MAP_FAILED)
		munmap(self->p, self->size);
}

TEST_F(mdwe, mmap_READ_EXEC)
{
	self->p = mmap(NULL, self->size, PROT_READ | PROT_EXEC, self->flags, 0, 0);
	EXPECT_NE(self->p, MAP_FAILED);
}

TEST_F(mdwe, mmap_WRITE_EXEC)
{
	self->p = mmap(NULL, self->size, PROT_WRITE | PROT_EXEC, self->flags, 0, 0);
	if (variant->enabled) {
		EXPECT_EQ(self->p, MAP_FAILED);
	} else {
		EXPECT_NE(self->p, MAP_FAILED);
	}
}

TEST_F(mdwe, mprotect_stay_EXEC)
{
	int ret;

	self->p = mmap(NULL, self->size, PROT_READ | PROT_EXEC, self->flags, 0, 0);
	ASSERT_NE(self->p, MAP_FAILED);

	ret = mprotect(self->p, self->size, PROT_READ | PROT_EXEC);
	EXPECT_EQ(ret, 0);
}

TEST_F(mdwe, mprotect_add_EXEC)
{
	int ret;

	self->p = mmap(NULL, self->size, PROT_READ, self->flags, 0, 0);
	ASSERT_NE(self->p, MAP_FAILED);

	ret = mprotect(self->p, self->size, PROT_READ | PROT_EXEC);
	if (variant->enabled) {
		EXPECT_LT(ret, 0);
	} else {
		EXPECT_EQ(ret, 0);
	}
}

TEST_F(mdwe, mprotect_WRITE_EXEC)
{
	int ret;

	self->p = mmap(NULL, self->size, PROT_WRITE, self->flags, 0, 0);
	ASSERT_NE(self->p, MAP_FAILED);

	ret = mprotect(self->p, self->size, PROT_WRITE | PROT_EXEC);
	if (variant->enabled) {
		EXPECT_LT(ret, 0);
	} else {
		EXPECT_EQ(ret, 0);
	}
}

TEST_F(mdwe, mmap_FIXED)
{
	void *p;

	self->p = mmap(NULL, self->size, PROT_READ, self->flags, 0, 0);
	ASSERT_NE(self->p, MAP_FAILED);

	p = mmap(self->p + self->size, self->size, PROT_READ | PROT_EXEC,
		 self->flags | MAP_FIXED, 0, 0);
	if (variant->enabled) {
		EXPECT_EQ(p, MAP_FAILED);
	} else {
		EXPECT_EQ(p, self->p);
	}
}

TEST_F(mdwe, arm64_BTI)
{
	int ret;

#ifdef __aarch64__
	if (!(getauxval(AT_HWCAP2) & HWCAP2_BTI))
#endif
		SKIP(return, "HWCAP2_BTI not supported");

	self->p = mmap(NULL, self->size, PROT_EXEC, self->flags, 0, 0);
	ASSERT_NE(self->p, MAP_FAILED);

	ret = mprotect(self->p, self->size, PROT_EXEC | PROT_BTI);
	EXPECT_EQ(ret, 0);
}

TEST_HARNESS_MAIN
