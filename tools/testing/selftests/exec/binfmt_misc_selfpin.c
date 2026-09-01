// SPDX-License-Identifier: GPL-2.0
/*
 * An 'F' entry keeps its interpreter open for as long as the entry exists,
 * and the entry only goes away when the binfmt_misc superblock is destroyed.
 * An interpreter that lives on a mount which in turn keeps that superblock
 * alive therefore pins the instance that owns it, and nothing can break the
 * cycle. Check the two ways userspace could arrange for that: an interpreter
 * on the binfmt_misc instance itself, and one on a filesystem stacked on it.
 *
 * Runs unprivileged in a user namespace; binfmt_misc is FS_USERNS_MOUNT.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include "../filesystems/utils.h"
#include "kselftest_harness.h"

#define MNT		"/tmp/binfmt_selfpin"
#define BACKING		"/tmp/binfmt_selfpin_back"
#define LOWER		BACKING "/lower"
#define MERGED		"/tmp/binfmt_selfpin_merged"

#define MAGIC		"\\xde\\xad"
#define RULE(interp)	":selfpin:M::" MAGIC "::" interp ":F"
/* Not on the instance, and unlike /bin/true it always exists. */
#define INTERP		"/proc/self/exe"

#define OPTS_MAX	(3 * PATH_MAX + 64)

static int ensure_dir(const char *path)
{
	if (mkdir(path, 0755) && errno != EEXIST)
		return -1;
	return 0;
}

/* Write @rule to this instance's register file, preserving write(2)'s errno. */
static int register_at(struct __test_metadata *_metadata, const char *rule)
{
	int fd, saved;
	ssize_t n;

	fd = open(MNT "/register", O_WRONLY);
	ASSERT_GE(fd, 0);
	n = write(fd, rule, strlen(rule));
	saved = errno;
	close(fd);
	errno = saved;
	return n < 0 ? -1 : 0;
}

/*
 * Mount an overlay over @lower using a private upper/work pair, so the two
 * mounts this test performs cannot interfere with each other and neither
 * overlaps the lower layer.
 */
static int mount_overlay(const char *lower, int nr)
{
	char opts[OPTS_MAX], upper[PATH_MAX], work[PATH_MAX];

	snprintf(upper, sizeof(upper), "%s/upper%d", BACKING, nr);
	snprintf(work, sizeof(work), "%s/work%d", BACKING, nr);
	if (mkdir(upper, 0755) || mkdir(work, 0755))
		return -1;

	snprintf(opts, sizeof(opts), "lowerdir=%s,upperdir=%s,workdir=%s",
		 lower, upper, work);
	return mount("ovl", MERGED, "overlay", 0, opts);
}

FIXTURE(selfpin) {
};

FIXTURE_SETUP(selfpin)
{
	/* setup_userns() exits rather than returns if this is not there. */
	if (access("/proc/self/ns/user", F_OK))
		SKIP(return, "kernel without user namespaces");
	ASSERT_EQ(setup_userns(), 0);

	ASSERT_EQ(ensure_dir(MNT), 0);
	if (mount("binfmt_misc", MNT, "binfmt_misc", 0, NULL)) {
		int saved = errno;

		/* Teardown doesn't run when setup skips, so clean up here. */
		rmdir(MNT);
		SKIP(return, "no binfmt_misc: %s", strerror(saved));
	}
}

FIXTURE_TEARDOWN(selfpin)
{
	/* The namespaces go with the process; just don't litter /tmp. */
	umount2(MERGED, MNT_DETACH);
	umount2(BACKING, MNT_DETACH);
	umount2(MNT, MNT_DETACH);
	rmdir(MERGED);
	rmdir(BACKING);
	rmdir(MNT);
}

/*
 * The instance's own files are regular files the mounter owns, so they can be
 * made executable. Opening one for exec still has to fail, otherwise the entry
 * pins the very superblock it lives in.
 */
TEST_F(selfpin, interpreter_on_the_instance)
{
	ASSERT_EQ(chmod(MNT "/status", 0755), 0);

	ASSERT_NE(register_at(_metadata, RULE(MNT "/status")), 0);
	EXPECT_EQ(errno, EACCES);
}

/* Same for an entry file rather than one of the control files. */
TEST_F(selfpin, interpreter_on_an_entry)
{
	ASSERT_EQ(register_at(_metadata, ":victim:M::" MAGIC "::" INTERP ":"), 0);
	ASSERT_EQ(chmod(MNT "/victim", 0755), 0);

	ASSERT_NE(register_at(_metadata, RULE(MNT "/victim")), 0);
	EXPECT_EQ(errno, EACCES);
}

/*
 * A stacking filesystem holds a private clone of each layer for its whole
 * lifetime, so an instance used as a layer can be pinned by an interpreter
 * that does not live on it at all. Refuse to be a layer.
 */
TEST_F(selfpin, refuses_to_be_stacked_on)
{
	ASSERT_EQ(ensure_dir(BACKING), 0);
	ASSERT_EQ(mount("tmpfs", BACKING, "tmpfs", 0, NULL), 0);
	ASSERT_EQ(mkdir(LOWER, 0755), 0);
	ASSERT_EQ(ensure_dir(MERGED), 0);

	/* Nothing to prove unless overlayfs works here at all. */
	if (mount_overlay(LOWER, 1)) {
		if (errno == ENODEV || errno == EPERM)
			SKIP(return, "no unprivileged overlayfs");
		SKIP(return, "overlayfs unusable here: %s", strerror(errno));
	}
	ASSERT_EQ(umount(MERGED), 0);

	EXPECT_NE(mount_overlay(MNT, 2), 0);
}

/* An ordinary interpreter still registers with 'F'. */
TEST_F(selfpin, ordinary_interpreter_still_works)
{
	EXPECT_EQ(register_at(_metadata, RULE(INTERP)), 0);
}

TEST_HARNESS_MAIN
