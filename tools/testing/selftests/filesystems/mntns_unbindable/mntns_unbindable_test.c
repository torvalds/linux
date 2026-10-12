// SPDX-License-Identifier: GPL-2.0
/*
 * An unbindable mount stays unbindable in a cloned mount namespace.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../kselftest_harness.h"

#ifndef OPEN_TREE_CLONE
#define OPEN_TREE_CLONE		1
#endif
#ifndef OPEN_TREE_CLOEXEC
#define OPEN_TREE_CLOEXEC	O_CLOEXEC
#endif

static int sys_open_tree(int dfd, const char *filename, unsigned int flags)
{
	return syscall(__NR_open_tree, dfd, filename, flags);
}

/* Child exit codes. */
enum {
	CHILD_OK,		/* the operation failed with EINVAL as it must */
	CHILD_ALLOWED,		/* the operation succeeded: the flag was lost */
	CHILD_UNSHARE,		/* unshare(CLONE_NEWNS) failed */
	CHILD_ERRNO,		/* the operation failed with some other errno */
	CHILD_MOUNTINFO,	/* the mount was not found in mountinfo */
};

FIXTURE(mntns_unbindable)
{
	char base[64];
	char src[80];
	char dst[80];
	bool mounted;
};

FIXTURE_SETUP(mntns_unbindable)
{
	self->mounted = false;

	if (geteuid() != 0)
		SKIP(return, "test requires CAP_SYS_ADMIN");

	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_EQ(mount("", "/", NULL, MS_REC | MS_PRIVATE, NULL), 0);

	snprintf(self->base, sizeof(self->base), "/tmp/mntns_unbindable.XXXXXX");
	ASSERT_NE(mkdtemp(self->base), NULL);
	ASSERT_EQ(mount("tmpfs", self->base, "tmpfs", 0, NULL), 0);
	self->mounted = true;

	snprintf(self->src, sizeof(self->src), "%s/src", self->base);
	snprintf(self->dst, sizeof(self->dst), "%s/dst", self->base);
	ASSERT_EQ(mkdir(self->src, 0755), 0);
	ASSERT_EQ(mkdir(self->dst, 0755), 0);

	ASSERT_EQ(mount("tmpfs", self->src, "tmpfs", 0, NULL), 0);
	ASSERT_EQ(mount(NULL, self->src, NULL, MS_UNBINDABLE, NULL), 0);
}

FIXTURE_TEARDOWN(mntns_unbindable)
{
	if (self->mounted)
		umount2(self->base, MNT_DETACH);
	rmdir(self->base);
}

static int classify(int ret, int err)
{
	if (ret >= 0)
		return CHILD_ALLOWED;
	return err == EINVAL ? CHILD_OK : CHILD_ERRNO;
}

/* Is the mount on @mountpoint marked unbindable in /proc/self/mountinfo? */
static int mountinfo_unbindable(const char *mountpoint)
{
	char line[4096];
	FILE *f;
	int ret = CHILD_MOUNTINFO;

	f = fopen("/proc/self/mountinfo", "re");
	if (!f)
		return CHILD_ERRNO;

	while (fgets(line, sizeof(line), f)) {
		char *fields[6], *p = line, *opt;
		int i;

		for (i = 0; i < 6; i++) {
			fields[i] = strsep(&p, " ");
			if (!fields[i])
				break;
		}
		if (i < 6 || strcmp(fields[4], mountpoint))
			continue;

		/* the optional fields, up to the "-" separator */
		ret = CHILD_ALLOWED;
		while ((opt = strsep(&p, " ")) && strcmp(opt, "-")) {
			if (!strcmp(opt, "unbindable"))
				ret = CHILD_OK;
		}
		break;
	}
	fclose(f);
	return ret;
}

static int run_in_child(int (*fn)(const char *src, const char *dst),
			const char *src, const char *dst)
{
	int status;
	pid_t pid;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
		_exit(fn(src, dst));
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

static int bind_after_clone(const char *src, const char *dst)
{
	int ret;

	if (unshare(CLONE_NEWNS))
		return CHILD_UNSHARE;
	ret = mount(src, dst, NULL, MS_BIND, NULL);
	return classify(ret, errno);
}

static int rbind_after_clone(const char *src, const char *dst)
{
	int ret;

	if (unshare(CLONE_NEWNS))
		return CHILD_UNSHARE;
	ret = mount(src, dst, NULL, MS_BIND | MS_REC, NULL);
	return classify(ret, errno);
}

static int open_tree_after_clone(const char *src, const char *dst)
{
	int ret;

	if (unshare(CLONE_NEWNS))
		return CHILD_UNSHARE;
	ret = sys_open_tree(AT_FDCWD, src, OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
	return classify(ret, errno);
}

static int mountinfo_after_clone(const char *src, const char *dst)
{
	if (unshare(CLONE_NEWNS))
		return CHILD_UNSHARE;
	return mountinfo_unbindable(src);
}

static int bind_after_two_clones(const char *src, const char *dst)
{
	int ret;

	if (unshare(CLONE_NEWNS))
		return CHILD_UNSHARE;
	if (unshare(CLONE_NEWNS))
		return CHILD_UNSHARE;
	ret = mount(src, dst, NULL, MS_BIND, NULL);
	return classify(ret, errno);
}

/* The namespace the mount was made unbindable in. */
TEST_F(mntns_unbindable, refuses_bind)
{
	int ret = mount(self->src, self->dst, NULL, MS_BIND, NULL);

	ASSERT_EQ(classify(ret, errno), CHILD_OK);
	ASSERT_EQ(mountinfo_unbindable(self->src), CHILD_OK);
}

/* A copy of the namespace must not turn the mount bindable. */
TEST_F(mntns_unbindable, refuses_bind_after_clone)
{
	ASSERT_EQ(run_in_child(bind_after_clone, self->src, self->dst), CHILD_OK)
		TH_LOG("bind of an unbindable mount allowed in a copied mount namespace");
}

TEST_F(mntns_unbindable, refuses_rbind_after_clone)
{
	ASSERT_EQ(run_in_child(rbind_after_clone, self->src, self->dst), CHILD_OK)
		TH_LOG("rbind of an unbindable mount allowed in a copied mount namespace");
}

TEST_F(mntns_unbindable, refuses_open_tree_after_clone)
{
	ASSERT_EQ(run_in_child(open_tree_after_clone, self->src, self->dst), CHILD_OK)
		TH_LOG("OPEN_TREE_CLONE of an unbindable mount allowed in a copied mount namespace");
}

TEST_F(mntns_unbindable, mountinfo_after_clone)
{
	ASSERT_EQ(run_in_child(mountinfo_after_clone, self->src, self->dst), CHILD_OK)
		TH_LOG("mountinfo does not show the mount as unbindable in a copied mount namespace");
}

TEST_F(mntns_unbindable, refuses_bind_after_two_clones)
{
	ASSERT_EQ(run_in_child(bind_after_two_clones, self->src, self->dst), CHILD_OK)
		TH_LOG("bind of an unbindable mount allowed two mount namespace copies down");
}

TEST_HARNESS_MAIN
