// SPDX-License-Identifier: GPL-2.0-only
#include <sys/prctl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <stdlib.h>

#include "../../kselftest_harness.h"
#include "v_helpers.h"

#define NEXT_PROGRAM "./vstate_exec_nolibc"

int test_and_compare_child(long provided, long expected, int inherit, int xtheadvector)
{
	int rc;

	rc = prctl(PR_RISCV_V_SET_CONTROL, provided);
	if (rc != 0) {
		printf("prctl with provided arg %lx failed with code %d\n",
		       provided, rc);
		return -1;
	}
	rc = launch_test(NEXT_PROGRAM, inherit, xtheadvector);
	if (rc != expected) {
		printf("Test failed, check %d != %ld\n", rc, expected);
		return -2;
	}
	return 0;
}

#define PR_RISCV_V_VSTATE_CTRL_CUR_SHIFT 0
#define PR_RISCV_V_VSTATE_CTRL_NEXT_SHIFT 2

TEST(get_control_no_v)
{
	long rc;

	if (is_vector_supported() || is_xtheadvector_supported())
		SKIP(return, "Test expects vector to be not supported");

	rc = prctl(PR_RISCV_V_GET_CONTROL);
	EXPECT_EQ(-1, rc)
	TH_LOG("GET_CONTROL should fail on kernel/hw without ZVE32X");
	EXPECT_EQ(EINVAL, errno)
	TH_LOG("GET_CONTROL should fail on kernel/hw without ZVE32X");
}

TEST(set_control_no_v)
{
	long rc;

	if (is_vector_supported() || is_xtheadvector_supported())
		SKIP(return, "Test expects vector to be not supported");

	rc = prctl(PR_RISCV_V_SET_CONTROL, PR_RISCV_V_VSTATE_CTRL_ON);
	EXPECT_EQ(-1, rc)
	TH_LOG("SET_CONTROL should fail on kernel/hw without ZVE32X");
	EXPECT_EQ(EINVAL, errno)
	TH_LOG("SET_CONTROL should fail on kernel/hw without ZVE32X");
}

TEST(vstate_on_current)
{
	long flag;
	long rc;

	if (!is_vector_supported() && !is_xtheadvector_supported())
		SKIP(return, "Vector not supported");

	flag = PR_RISCV_V_VSTATE_CTRL_ON;
	rc = prctl(PR_RISCV_V_SET_CONTROL, flag);
	EXPECT_EQ(0, rc) TH_LOG("Enabling V for current should always succeed");
}

TEST(vstate_off_eperm)
{
	long flag;
	long rc;

	if (!is_vector_supported() && !is_xtheadvector_supported())
		SKIP(return, "Vector not supported");

	flag = PR_RISCV_V_VSTATE_CTRL_OFF;
	rc = prctl(PR_RISCV_V_SET_CONTROL, flag);
	EXPECT_EQ(EPERM, errno)
	TH_LOG("Disabling V in current thread with V enabled must fail with EPERM(%d)", errno);
	EXPECT_EQ(-1, rc)
	TH_LOG("Disabling V in current thread with V enabled must fail with EPERM(%d)", errno);
}

TEST(vstate_on_no_nesting)
{
	long flag;
	int xtheadvector = 0;

	if (!is_vector_supported()) {
		if (is_xtheadvector_supported())
			xtheadvector = 1;
		else
			SKIP(return, "Vector not supported");
	}

	/* Turn on next's vector explicitly and test */
	flag = PR_RISCV_V_VSTATE_CTRL_ON << PR_RISCV_V_VSTATE_CTRL_NEXT_SHIFT;

	EXPECT_EQ(0, test_and_compare_child(flag, PR_RISCV_V_VSTATE_CTRL_ON, 0, xtheadvector));
}

TEST(vstate_off_nesting)
{
	long flag;
	int xtheadvector = 0;

	if (!is_vector_supported()) {
		if (is_xtheadvector_supported())
			xtheadvector = 1;
		else
			SKIP(return, "Vector not supported");
	}

	/* Turn off next's vector explicitly and test */
	flag = PR_RISCV_V_VSTATE_CTRL_OFF << PR_RISCV_V_VSTATE_CTRL_NEXT_SHIFT;

	EXPECT_EQ(0, test_and_compare_child(flag, PR_RISCV_V_VSTATE_CTRL_OFF, 1, xtheadvector));
}

TEST(vstate_on_inherit_no_nesting)
{
	long flag, expected;
	int xtheadvector = 0;

	if (!is_vector_supported()) {
		if (is_xtheadvector_supported())
			xtheadvector = 1;
		else
			SKIP(return, "Vector not supported");
	}

	/* Turn on next's vector explicitly and test no inherit */
	flag = PR_RISCV_V_VSTATE_CTRL_ON << PR_RISCV_V_VSTATE_CTRL_NEXT_SHIFT;
	flag |= PR_RISCV_V_VSTATE_CTRL_INHERIT;
	expected = flag | PR_RISCV_V_VSTATE_CTRL_ON;

	EXPECT_EQ(0, test_and_compare_child(flag, expected, 0, xtheadvector));
}

TEST(vstate_on_inherit)
{
	long flag, expected;
	int xtheadvector = 0;

	if (!is_vector_supported()) {
		if (is_xtheadvector_supported())
			xtheadvector = 1;
		else
			SKIP(return, "Vector not supported");
	}

	/* Turn on next's vector explicitly and test inherit */
	flag = PR_RISCV_V_VSTATE_CTRL_ON << PR_RISCV_V_VSTATE_CTRL_NEXT_SHIFT;
	flag |= PR_RISCV_V_VSTATE_CTRL_INHERIT;
	expected = flag | PR_RISCV_V_VSTATE_CTRL_ON;

	EXPECT_EQ(0, test_and_compare_child(flag, expected, 1, xtheadvector));
}

TEST(vstate_off_inherit_no_nesting)
{
	long flag, expected;
	int xtheadvector = 0;

	if (!is_vector_supported()) {
		if (is_xtheadvector_supported())
			xtheadvector = 1;
		else
			SKIP(return, "Vector not supported");
	}
	/* Turn off next's vector explicitly and test no inherit */
	flag = PR_RISCV_V_VSTATE_CTRL_OFF << PR_RISCV_V_VSTATE_CTRL_NEXT_SHIFT;
	flag |= PR_RISCV_V_VSTATE_CTRL_INHERIT;
	expected = flag | PR_RISCV_V_VSTATE_CTRL_OFF;

	EXPECT_EQ(0, test_and_compare_child(flag, expected, 0, xtheadvector));
}

TEST(vstate_off_inherit)
{
	long flag, expected;
	int xtheadvector = 0;

	if (!is_vector_supported()) {
		if (is_xtheadvector_supported())
			xtheadvector = 1;
		else
			SKIP(return, "Vector not supported");
	}

	/* Turn off next's vector explicitly and test inherit */
	flag = PR_RISCV_V_VSTATE_CTRL_OFF << PR_RISCV_V_VSTATE_CTRL_NEXT_SHIFT;
	flag |= PR_RISCV_V_VSTATE_CTRL_INHERIT;
	expected = flag | PR_RISCV_V_VSTATE_CTRL_OFF;

	EXPECT_EQ(0, test_and_compare_child(flag, expected, 1, xtheadvector));
}

/* arguments should fail with EINVAL */
TEST(inval_set_control_1)
{
	int rc;

	if (!is_vector_supported() && !is_xtheadvector_supported())
		SKIP(return, "Vector not supported");

	rc = prctl(PR_RISCV_V_SET_CONTROL, 0xff0);
	EXPECT_EQ(-1, rc);
	EXPECT_EQ(EINVAL, errno);
}

/* arguments should fail with EINVAL */
TEST(inval_set_control_2)
{
	int rc;

	if (!is_vector_supported() && !is_xtheadvector_supported())
		SKIP(return, "Vector not supported");

	rc = prctl(PR_RISCV_V_SET_CONTROL, 0x3);
	EXPECT_EQ(-1, rc);
	EXPECT_EQ(EINVAL, errno);
}

/* arguments should fail with EINVAL */
TEST(inval_set_control_3)
{
	int rc;

	if (!is_vector_supported() && !is_xtheadvector_supported())
		SKIP(return, "Vector not supported");

	rc = prctl(PR_RISCV_V_SET_CONTROL, 0xc);
	EXPECT_EQ(-1, rc);
	EXPECT_EQ(EINVAL, errno);
}

TEST_HARNESS_MAIN
