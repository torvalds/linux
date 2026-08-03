// SPDX-License-Identifier: GPL-2.0
/*
 * A pre-opened interpreter - what 'F' gives a static entry and what a 'B'
 * entry binds - keeps a file open for as long as the entry lives, so it pins
 * the mount it came from. It costs no file descriptor, and binfmt_misc is
 * FS_USERNS_MOUNT, so an unprivileged user namespace can create them without
 * bound. Check that UCOUNT_BINFMT_MISC_INTERPRETERS bounds it, that an entry
 * that pre-opens nothing is not charged, that removing an entry gives the
 * charge back, and that nesting a user namespace does not evade it.
 *
 * Runs unprivileged in a user namespace.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../filesystems/utils.h"
#include "kselftest_harness.h"

#define MNT		"/tmp/binfmt_interplimit"
#define NESTED_MNT	"/tmp/binfmt_interplimit_nested"
#define LIMIT_SYSCTL	"/proc/sys/user/max_binfmt_misc_interpreters"

#define MAGIC		"\\xde\\xad"
/* Not on the instance, and unlike /bin/true it always exists. */
#define INTERP		"/proc/self/exe"

/* Small enough to fill by hand, big enough that a refund is visible. */
#define LIMIT		4

/* What UCOUNT_ENTRY() lets a namespace raise its own limit to. */
#define LIMIT_MAX	"2147483647"

static int ensure_dir(const char *path)
{
	if (mkdir(path, 0755) && errno != EEXIST)
		return -1;
	return 0;
}

/* Write @val to @path, preserving write(2)'s errno for the caller. */
static int write_keep_errno(const char *path, const char *val)
{
	int fd, saved;
	ssize_t n;

	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	n = write(fd, val, strlen(val));
	saved = errno;
	close(fd);
	errno = saved;
	return n < 0 ? -1 : 0;
}

static int set_limit(const char *val)
{
	return write_keep_errno(LIMIT_SYSCTL, val);
}

static int register_at(const char *mnt, const char *rule)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/register", mnt);
	return write_keep_errno(path, rule);
}

/* An 'F' entry: one interpreter pre-opened at registration, one charge. */
static int register_fixed(const char *mnt, const char *name)
{
	char rule[PATH_MAX];

	snprintf(rule, sizeof(rule), ":%s:M::" MAGIC "::" INTERP ":F", name);
	return register_at(mnt, rule);
}

/* The same entry without 'F': the interpreter is opened per exec instead. */
static int register_plain(const char *mnt, const char *name)
{
	char rule[PATH_MAX];

	snprintf(rule, sizeof(rule), ":%s:M::" MAGIC "::" INTERP ":", name);
	return register_at(mnt, rule);
}

static int remove_entry(const char *mnt, const char *name)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", mnt, name);
	return write_keep_errno(path, "-1\n");
}

static bool entry_exists(const char *mnt, const char *name)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", mnt, name);
	return access(path, F_OK) == 0;
}

/* Register @n 'F' entries, each with a name of its own. */
static int fill_budget(const char *mnt, unsigned int n)
{
	char name[32];
	unsigned int i;

	for (i = 0; i < n; i++) {
		snprintf(name, sizeof(name), "fixed%u", i);
		if (register_fixed(mnt, name))
			return -1;
	}
	return 0;
}

FIXTURE(interp_limit) {
};

FIXTURE_SETUP(interp_limit)
{
	/* setup_userns() exits rather than returns if this is not there. */
	if (access("/proc/self/ns/user", F_OK))
		SKIP(return, "kernel without user namespaces");
	ASSERT_EQ(setup_userns(), 0);

	/* CAP_SYS_RESOURCE in this namespace is what makes it writable. */
	if (set_limit(LIMIT_MAX)) {
		if (errno == ENOENT)
			SKIP(return, "kernel without " LIMIT_SYSCTL);
		SKIP(return, "cannot set the limit: %s", strerror(errno));
	}

	ASSERT_EQ(ensure_dir(MNT), 0);
	if (mount("binfmt_misc", MNT, "binfmt_misc", 0, NULL)) {
		int saved = errno;

		/* Teardown doesn't run when setup skips, so clean up here. */
		rmdir(MNT);
		SKIP(return, "no binfmt_misc: %s", strerror(saved));
	}
}

FIXTURE_TEARDOWN(interp_limit)
{
	/* The namespaces go with the process; just don't litter /tmp. */
	umount2(NESTED_MNT, MNT_DETACH);
	umount2(MNT, MNT_DETACH);
	rmdir(NESTED_MNT);
	rmdir(MNT);
}

/* Every pre-opened interpreter is charged, and the budget is a hard stop. */
TEST_F(interp_limit, fixed_interpreters_are_charged)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "%u", LIMIT);
	ASSERT_EQ(set_limit(buf), 0);

	ASSERT_EQ(fill_budget(MNT, LIMIT), 0);

	EXPECT_NE(register_fixed(MNT, "over"), 0);
	EXPECT_EQ(errno, ENOSPC);

	/* A refused registration leaves nothing behind. */
	EXPECT_FALSE(entry_exists(MNT, "over"));
}

/* An entry that pre-opens nothing pins nothing, so it is not charged. */
TEST_F(interp_limit, plain_entries_are_not_charged)
{
	ASSERT_EQ(set_limit("0"), 0);

	EXPECT_EQ(register_plain(MNT, "plain"), 0);
	EXPECT_TRUE(entry_exists(MNT, "plain"));

	/* ... while the same entry with 'F' has nothing to spend. */
	EXPECT_NE(register_fixed(MNT, "fixed"), 0);
	EXPECT_EQ(errno, ENOSPC);
}

/* Removing an entry closes its interpreters and gives the charge back. */
TEST_F(interp_limit, removal_refunds_the_charge)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "%u", LIMIT);
	ASSERT_EQ(set_limit(buf), 0);

	ASSERT_EQ(fill_budget(MNT, LIMIT), 0);
	ASSERT_NE(register_fixed(MNT, "over"), 0);

	ASSERT_EQ(remove_entry(MNT, "fixed0"), 0);
	EXPECT_EQ(register_fixed(MNT, "over"), 0);
}

/*
 * The charge walks the ancestors, so a namespace cannot buy itself budget by
 * nesting: it may raise only its own limit, and the parent it was created
 * from is charged for every binding made below it.
 */
TEST_F(interp_limit, nesting_does_not_evade_it)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "%u", LIMIT);
	ASSERT_EQ(set_limit(buf), 0);
	ASSERT_EQ(fill_budget(MNT, LIMIT), 0);

	ASSERT_EQ(setup_userns(), 0);
	ASSERT_EQ(set_limit(LIMIT_MAX), 0);

	ASSERT_EQ(ensure_dir(NESTED_MNT), 0);
	ASSERT_EQ(mount("binfmt_misc", NESTED_MNT, "binfmt_misc", 0, NULL), 0);

	/* A fresh instance with an unlimited budget of its own, and yet: */
	EXPECT_NE(register_fixed(NESTED_MNT, "nested"), 0);
	EXPECT_EQ(errno, ENOSPC);

	/* The nested instance works for anything that pins no file. */
	EXPECT_EQ(register_plain(NESTED_MNT, "nested_plain"), 0);
}

TEST_HARNESS_MAIN
