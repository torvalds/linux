// SPDX-License-Identifier: GPL-2.0
/*
 * Test the static transparent flag 'T' of binfmt_misc. A magic-matched
 * binary is dispatched to an interpreter with the argument vector left
 * untouched, the binary passed through AT_EXECFD and mm->exe_file labeled
 * with the binary. The asserting interpreter (binfmt_transparent_interp)
 * verifies the constructed identity from inside the process and exits 0.
 *
 * Needs root for the registration; no bpf toolchain involved.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include "binfmt_misc_common.h"
#include "kselftest_harness.h"

#define MAGIC		"#TRANSPARENT-SELFTEST#"
#define TARGET_PATH	"/tmp/binfmt_transparent_target"
#define INTERP_PATH	"/tmp/binfmt_transparent_interp"
#define ENTRY		"test_transparent"
#define RULE(flags)	":" ENTRY ":M:0:" MAGIC "::" INTERP_PATH ":" flags

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

FIXTURE(transparent) {
};

FIXTURE_SETUP(transparent)
{
	char src[PATH_MAX];

	if (getuid() != 0)
		SKIP(return, "test must be run as root");
	if (!binfmt_misc_available())
		SKIP(return, "no binfmt_misc");

	ASSERT_EQ(artifact_path(src, sizeof(src), "binfmt_transparent_interp"), 0);
	ASSERT_EQ(copy_file(src, INTERP_PATH), 0);
	ASSERT_EQ(create_target(), 0);

	/* Skip the whole suite on a kernel that does not know 'T'. */
	if (binfmt_flag_supported('T')) {
		ASSERT_EQ(errno, EINVAL);
		SKIP(return, "kernel without the 'T' flag");
	}
}

FIXTURE_TEARDOWN(transparent)
{
	unregister(ENTRY);
	unlink(TARGET_PATH);
	unlink(INTERP_PATH);
}

/* Grammar sanity check: the same entry without 'T' has to register. */
TEST_F(transparent, plain_entry_registers)
{
	ASSERT_EQ(write_reg(RULE("")), 0);
}

/* 'T' preserves the whole argv, so combining it with 'P' is rejected. */
TEST_F(transparent, rejects_preserve_argv0)
{
	ASSERT_NE(write_reg(RULE("TP")), 0);
	EXPECT_EQ(errno, EINVAL);
}

/* The interpreter asserts the identity the kernel built for it. */
TEST_F(transparent, dispatch)
{
	ASSERT_EQ(write_reg(RULE("T")), 0);

	setenv("BINFMT_TEST_BINARY", TARGET_PATH, 1);
	setenv("BINFMT_TEST_ARGV0", PAYLOAD_ARGV0, 1);
	EXPECT_EQ(run_payload(TARGET_PATH), 0);
}

TEST_HARNESS_MAIN
