// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>

#include <sys/resource.h>
#include <sys/prctl.h>

/* Avoid any inconsistencies */
#define TH_LOG_STREAM stdout

#include "../kselftest_harness.h"

static void test_helper(struct __test_metadata *_metadata)
{
	ASSERT_EQ(0, 0);
}

TEST(standalone_pass) {
	TH_LOG("before");
	ASSERT_EQ(0, 0);
	EXPECT_EQ(0, 0);
	test_helper(_metadata);
	TH_LOG("after");
}

TEST(standalone_fail) {
	TH_LOG("before");
	EXPECT_EQ(0, 0);
	EXPECT_EQ(0, 1);
	ASSERT_EQ(0, 1);
	TH_LOG("after");
}

TEST_SIGNAL(signal_pass, SIGUSR1) {
	TH_LOG("before");
	ASSERT_EQ(0, 0);
	TH_LOG("after");
	kill(getpid(), SIGUSR1);
}

TEST_SIGNAL(signal_fail, SIGUSR1) {
	TH_LOG("before");
	ASSERT_EQ(0, 1);
	TH_LOG("after");
	kill(getpid(), SIGUSR1);
}

FIXTURE(fixture) {
	pid_t testpid;
};

FIXTURE_SETUP(fixture) {
	TH_LOG("setup");
	self->testpid = getpid();
}

FIXTURE_TEARDOWN(fixture) {
	TH_LOG("teardown same-process=%d", self->testpid == getpid());
}

TEST_F(fixture, pass) {
	TH_LOG("before");
	ASSERT_EQ(0, 0);
	test_helper(_metadata);
	standalone_pass(_metadata);
	TH_LOG("after");
}

TEST_F(fixture, fail) {
	TH_LOG("before");
	ASSERT_EQ(0, 1);
	fixture_pass(_metadata, self, variant);
	TH_LOG("after");
}

TEST_F_TIMEOUT(fixture, timeout, 1) {
	TH_LOG("before");
	sleep(2);
	TH_LOG("after");
}

FIXTURE(fixture_parent) {
	pid_t testpid;
};

FIXTURE_SETUP(fixture_parent) {
	TH_LOG("setup");
	self->testpid = getpid();
}

FIXTURE_TEARDOWN_PARENT(fixture_parent) {
	TH_LOG("teardown same-process=%d", self->testpid == getpid());
}

TEST_F(fixture_parent, pass) {
	TH_LOG("before");
	ASSERT_EQ(0, 0);
	TH_LOG("after");
}

FIXTURE(fixture_setup_failure) {
	pid_t testpid;
};

FIXTURE_SETUP(fixture_setup_failure) {
	TH_LOG("setup");
	self->testpid = getpid();
	ASSERT_EQ(0, 1);
}

FIXTURE_TEARDOWN(fixture_setup_failure) {
	TH_LOG("teardown same-process=%d", self->testpid == getpid());
}

TEST_F(fixture_setup_failure, pass) {
	TH_LOG("before");
	ASSERT_EQ(0, 0);
	TH_LOG("after");
}

int main(int argc, char **argv)
{
	/*
	 * The harness uses abort() to signal assertion failures, which triggers coredumps.
	 * This may be useful to debug real failures but not for this selftest, disable them.
	 */
	struct rlimit rlimit = {
		.rlim_cur = 0,
		.rlim_max = 0,
	};

	prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
	setrlimit(RLIMIT_CORE, &rlimit);

	return test_harness_run(argc, argv);
}
