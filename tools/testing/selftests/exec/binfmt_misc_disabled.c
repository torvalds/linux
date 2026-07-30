// SPDX-License-Identifier: GPL-2.0
/*
 * Test the 'D' (register disabled) flag of binfmt_misc. An entry
 * registered with it exists but cannot be matched until userspace enables
 * it, which splits a registration into create and activate.
 *
 * Needs root for the registration; no bpf toolchain involved.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include "binfmt_misc_common.h"
#include "kselftest_harness.h"

#define MAGIC		"#DISABLED-SELFTEST#"
#define TARGET_PATH	"/tmp/binfmt_disabled_target"
#define INTERP_PATH	"/tmp/binfmt_disabled_interp.sh"
#define ENTRY		"test_disabled"
#define RULE(flags)	":" ENTRY ":M:0:" MAGIC "::" INTERP_PATH ":" flags

/* The interpreter exits with a code the harness can recognise. */
#define EXIT_INTERP	7

/* The target only has to carry the magic; it is never actually loaded. */
static int create_target(void)
{
	char buf[128] = MAGIC "\n";
	int fd;

	unlink(TARGET_PATH);
	fd = open(TARGET_PATH, O_WRONLY | O_CREAT | O_EXCL, 0755);
	if (fd < 0)
		return -1;
	if (write(fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static int create_interp(void)
{
	char buf[64];
	int fd;

	unlink(INTERP_PATH);
	fd = open(INTERP_PATH, O_WRONLY | O_CREAT | O_EXCL, 0755);
	if (fd < 0)
		return -1;
	snprintf(buf, sizeof(buf), "#!/bin/sh\nexit %d\n", EXIT_INTERP);
	if (write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf)) {
		close(fd);
		return -1;
	}
	return close(fd);
}

FIXTURE(disabled) {
};

FIXTURE_SETUP(disabled)
{
	if (getuid() != 0)
		SKIP(return, "test must be run as root");
	if (!binfmt_misc_available())
		SKIP(return, "no binfmt_misc");

	/* Skip the whole suite on a kernel that does not know 'D'. */
	if (!binfmt_flag_supported('D')) {
		ASSERT_EQ(errno, EINVAL);
		SKIP(return, "kernel without the 'D' flag");
	}

	ASSERT_EQ(create_interp(), 0);
	ASSERT_EQ(create_target(), 0);
}

FIXTURE_TEARDOWN(disabled)
{
	unregister(ENTRY);
	unlink(TARGET_PATH);
	unlink(INTERP_PATH);
}

/* The entry exists but does not dispatch until it is enabled. */
TEST_F(disabled, inert_until_enabled)
{
	ASSERT_EQ(write_reg(RULE("D")), 0);
	EXPECT_TRUE(entry_shows(ENTRY, "disabled"));

	/* Nothing matches it, so no binary format claims the target. */
	EXPECT_EQ(run_payload(TARGET_PATH), RUN_ENOEXEC);

	ASSERT_EQ(entry_command(ENTRY, "1\n"), 0);
	EXPECT_TRUE(entry_shows(ENTRY, "enabled"));
	EXPECT_EQ(run_payload(TARGET_PATH), EXIT_INTERP);
}

/* Without 'D' an entry is matchable the moment it is registered. */
TEST_F(disabled, enabled_without_the_flag)
{
	ASSERT_EQ(write_reg(RULE("")), 0);
	EXPECT_TRUE(entry_shows(ENTRY, "enabled"));
	EXPECT_EQ(run_payload(TARGET_PATH), EXIT_INTERP);
}

/* 'D' is spent on the registration: the entry does not report it back. */
TEST_F(disabled, flag_not_reported)
{
	ASSERT_EQ(write_reg(RULE("D")), 0);
	EXPECT_FALSE(entry_shows(ENTRY, "flags: D"));
	EXPECT_TRUE(entry_shows(ENTRY, "flags: "));
}

/* A disabled entry can be disabled and enabled like any other. */
TEST_F(disabled, toggles_like_any_entry)
{
	ASSERT_EQ(write_reg(RULE("D")), 0);

	ASSERT_EQ(entry_command(ENTRY, "1\n"), 0);
	ASSERT_EQ(run_payload(TARGET_PATH), EXIT_INTERP);
	ASSERT_EQ(entry_command(ENTRY, "0\n"), 0);
	EXPECT_EQ(run_payload(TARGET_PATH), RUN_ENOEXEC);
	ASSERT_EQ(entry_command(ENTRY, "1\n"), 0);
	EXPECT_EQ(run_payload(TARGET_PATH), EXIT_INTERP);
}

/* 'D' composes with the invocation flags a static entry can carry. */
TEST_F(disabled, composes_with_invocation_flags)
{
	ASSERT_EQ(write_reg(RULE("PD")), 0);
	EXPECT_TRUE(entry_shows(ENTRY, "disabled"));
	EXPECT_TRUE(entry_shows(ENTRY, "flags: P"));
}

/* '-1' to the status file sweeps a staged entry with everything else. */
TEST_F(disabled, removed_by_remove_all)
{
	int fd;

	ASSERT_EQ(write_reg(RULE("D")), 0);
	EXPECT_TRUE(entry_shows(ENTRY, "disabled"));

	fd = open(BINFMT_DIR "/status", O_WRONLY | O_CLOEXEC);
	ASSERT_GE(fd, 0);
	ASSERT_EQ(write(fd, "-1", 2), 2);
	close(fd);

	EXPECT_NE(access(BINFMT_DIR "/" ENTRY, F_OK), 0);
}

/* A file handle held across a removal cannot resurrect the entry. */
TEST_F(disabled, no_resurrection_after_remove)
{
	int fd;

	ASSERT_EQ(write_reg(RULE("D")), 0);
	fd = open(BINFMT_DIR "/" ENTRY, O_WRONLY | O_CLOEXEC);
	ASSERT_GE(fd, 0);

	ASSERT_EQ(write(fd, "-1", 2), 2);
	EXPECT_NE(access(BINFMT_DIR "/" ENTRY, F_OK), 0);

	/* Accepted like any toggle of a removed entry, but publishes nothing. */
	EXPECT_EQ(write(fd, "1", 1), 1);
	EXPECT_EQ(run_payload(TARGET_PATH), RUN_ENOEXEC);
	close(fd);
}

TEST_HARNESS_MAIN
