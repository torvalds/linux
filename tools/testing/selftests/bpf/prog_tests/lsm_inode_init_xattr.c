// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <test_progs.h>

#include "lsm_inode_init_xattr.skel.h"
#include "lsm_inode_init_xattr_budget.skel.h"
#include "lsm_inode_init_xattr_value.skel.h"

#define INIT_XATTRS_MAX		2
#define VALUE_SIZE_MAX		(64 * 1024)
#define TESTDIR			"/tmp/test_progs_lsm_init_xattr"
#define RAMFSDIR		"/tmp/test_progs_lsm_init_xattr_ramfs"

static bool testdir_mounted;
static bool ramfsdir_mounted;

static int testdir_setup(void)
{
	if (mkdir(TESTDIR, 0755) && errno != EEXIST)
		return -errno;
	if (mount("tmpfs", TESTDIR, "tmpfs", 0, NULL))
		return -errno;
	testdir_mounted = true;
	if (mkdir(RAMFSDIR, 0755) && errno != EEXIST)
		return -errno;
	if (mount("ramfs", RAMFSDIR, "ramfs", 0, NULL))
		return -errno;
	ramfsdir_mounted = true;
	return 0;
}

static void testdir_cleanup(void)
{
	if (ramfsdir_mounted)
		umount(RAMFSDIR);
	rmdir(RAMFSDIR);
	if (testdir_mounted)
		umount(TESTDIR);
	rmdir(TESTDIR);
}

static bool lsm_is_active(const char *name)
{
	char buf[512], *tok;
	int fd, len;

	fd = open("/sys/kernel/security/lsm", O_RDONLY);
	if (fd < 0)
		return true;
	len = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (len <= 0)
		return true;
	buf[len] = '\0';
	for (tok = strtok(buf, ",\n"); tok; tok = strtok(NULL, ",\n"))
		if (!strcmp(tok, name))
			return true;
	return false;
}

static int read_label(const char *path, const char *name, char *buf, size_t sz)
{
	int ret = getxattr(path, name, buf, sz);

	return ret < 0 ? -errno : ret;
}

static void assert_label(const char *path, const char *name, const char *want)
{
	char buf[64] = {};
	int ret;

	ret = read_label(path, name, buf, sizeof(buf));
	if (!ASSERT_EQ(ret, (int)strlen(want) + 1, name))
		return;
	ASSERT_STREQ(buf, want, name);
}

static struct lsm_inode_init_xattr *policy_attach(void)
{
	struct lsm_inode_init_xattr *skel;

	skel = lsm_inode_init_xattr__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return NULL;

	skel->bss->monitored_pid = getpid();
	if (!ASSERT_OK(lsm_inode_init_xattr__attach(skel), "skel_attach")) {
		lsm_inode_init_xattr__destroy(skel);
		return NULL;
	}
	return skel;
}

static void test_init_labels(void)
{
	struct lsm_inode_init_xattr *skel;
	const char *file = TESTDIR "/file";
	const char *subdir = TESTDIR "/subdir";
	int fd = -1;

	skel = policy_attach();
	if (!skel)
		return;

	fd = open(file, O_CREAT | O_RDWR, 0644);
	if (!ASSERT_GE(fd, 0, "create_file"))
		goto out;

	if (!ASSERT_TRUE(skel->bss->hook_ran, "hook_ran"))
		goto out;

	ASSERT_OK(skel->data->zone_err, "zone_err");
	ASSERT_OK(skel->data->origin_err, "origin_err");
	assert_label(file, "security.bpf.zone", "default");
	assert_label(file, "security.bpf.origin", "created");

	if (!ASSERT_OK(mkdir(subdir, 0755), "mkdir"))
		goto out;
	assert_label(subdir, "security.bpf.zone", "default");
	assert_label(subdir, "security.bpf.origin", "created");
	rmdir(subdir);
out:
	if (fd >= 0)
		close(fd);
	remove(file);
	lsm_inode_init_xattr__destroy(skel);
}

static void test_inherit_from_parent(void)
{
	const char *zonedir = TESTDIR "/zoned";
	const char *file = TESTDIR "/zoned/file";
	struct lsm_inode_init_xattr *skel;
	int dirfd = -1, fd = -1, err;
	struct {
		char	v[32];
		__u32	len;
	} label = {};

	if (!ASSERT_OK(mkdir(zonedir, 0755), "mkdir_zoned"))
		return;

	skel = policy_attach();
	if (!skel)
		goto out_dir;

	dirfd = open(zonedir, O_RDONLY | O_DIRECTORY);
	if (!ASSERT_GE(dirfd, 0, "open_zoned"))
		goto out;

	strncpy(label.v, "restricted", sizeof(label.v) - 1);
	label.len = strlen("restricted") + 1;

	err = bpf_map_update_elem(bpf_map__fd(skel->maps.inode_zone), &dirfd,
				  &label, BPF_ANY);
	if (!ASSERT_OK(err, "seed_parent_zone"))
		goto out;

	fd = open(file, O_CREAT | O_RDWR, 0644);
	if (!ASSERT_GE(fd, 0, "create_file"))
		goto out;

	if (!ASSERT_TRUE(skel->bss->hook_ran, "hook_ran"))
		goto out;

	ASSERT_EQ(skel->bss->inherited, 1, "inherited");
	ASSERT_OK(skel->data->zone_err, "zone_err");
	assert_label(file, "security.bpf.zone", "restricted");
	assert_label(file, "security.bpf.origin", "created");
out:
	if (fd >= 0)
		close(fd);
	if (dirfd >= 0)
		close(dirfd);
	remove(file);
	lsm_inode_init_xattr__destroy(skel);
out_dir:
	rmdir(zonedir);
}

static void test_refused_claims(void)
{
	const char *file = TESTDIR "/refused";
	struct lsm_inode_init_xattr *skel;
	char buf[64];
	int fd = -1;

	skel = policy_attach();
	if (!skel)
		return;

	fd = open(file, O_CREAT | O_RDWR, 0644);
	if (!ASSERT_GE(fd, 0, "create_file"))
		goto out;

	if (!ASSERT_TRUE(skel->bss->hook_ran, "hook_ran"))
		goto out;

	ASSERT_EQ(skel->data->overflow_err, -ENOSPC, "overflow_err");
	ASSERT_EQ(read_label(file, "security.bpf.overflow", buf, sizeof(buf)),
		  -ENODATA, "overflow_absent");

	ASSERT_EQ(skel->data->toolong_err, -EINVAL, "toolong_err");

	ASSERT_EQ(skel->data->selinux_err, -EPERM, "selinux_err");
	ASSERT_EQ(skel->data->user_err, -EPERM, "user_err");

	if (!lsm_is_active("selinux"))
		ASSERT_EQ(read_label(file, "security.selinux", buf, sizeof(buf)),
			  -ENODATA, "selinux_absent");
	ASSERT_EQ(read_label(file, "user.zone", buf, sizeof(buf)),
		  -ENODATA, "user_absent");
out:
	if (fd >= 0)
		close(fd);
	remove(file);
	lsm_inode_init_xattr__destroy(skel);
}

static void test_null_xattrs(void)
{
	const char *file = RAMFSDIR "/file";
	struct lsm_inode_init_xattr *skel;
	int fd = -1;

	skel = policy_attach();
	if (!skel)
		return;

	fd = open(file, O_CREAT | O_RDWR, 0644);
	if (!ASSERT_GE(fd, 0, "create_file"))
		goto out;

	if (!ASSERT_TRUE(skel->bss->hook_ran, "hook_ran"))
		goto out;
	ASSERT_EQ(skel->data->zone_err, -EOPNOTSUPP, "zone_err");
out:
	if (fd >= 0)
		close(fd);
	remove(file);
	lsm_inode_init_xattr__destroy(skel);
}

static void test_shared_budget(void)
{
	struct lsm_inode_init_xattr_budget *skel[INIT_XATTRS_MAX + 1] = {};
	struct bpf_link *link[INIT_XATTRS_MAX + 1] = {};
	const char *file = TESTDIR "/budget";
	int claimed = 0, refused = 0;
	int i, fd = -1;

	for (i = 0; i <= INIT_XATTRS_MAX; i++) {
		skel[i] = lsm_inode_init_xattr_budget__open();
		if (!ASSERT_OK_PTR(skel[i], "skel_open"))
			goto out;

		snprintf(skel[i]->rodata->xattr_name,
			 sizeof(skel[i]->rodata->xattr_name),
			 "security.bpf.slot%d", i);

		if (!ASSERT_OK(lsm_inode_init_xattr_budget__load(skel[i]),
			       "skel_load"))
			goto out;

		skel[i]->bss->monitored_pid = getpid();

		link[i] = bpf_program__attach_lsm(skel[i]->progs.claim_one);
		if (!ASSERT_OK_PTR(link[i], "attach"))
			goto out;
	}

	fd = open(file, O_CREAT | O_RDWR, 0644);
	if (!ASSERT_GE(fd, 0, "create_file"))
		goto out;

	for (i = 0; i <= INIT_XATTRS_MAX; i++) {
		int err = skel[i]->data->claim_err;

		ASSERT_TRUE(skel[i]->bss->hook_ran, "hook_ran");
		if (!err)
			claimed++;
		else if (ASSERT_EQ(err, -ENOSPC, "claim_err"))
			refused++;
	}

	ASSERT_EQ(claimed, INIT_XATTRS_MAX, "claimed");
	ASSERT_EQ(refused, 1, "refused");
out:
	if (fd >= 0)
		close(fd);
	remove(file);
	for (i = 0; i <= INIT_XATTRS_MAX; i++) {
		bpf_link__destroy(link[i]);
		lsm_inode_init_xattr_budget__destroy(skel[i]);
	}
}

static int claim_value(const char *file, __u32 len, __u64 flags,
		       int hook_retval, int *open_errno)
{
	struct lsm_inode_init_xattr_value *skel;
	struct bpf_link *link = NULL;
	int err = 1, fd;

	skel = lsm_inode_init_xattr_value__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return 1;

	skel->rodata->value_len = len;
	skel->rodata->dynptr_flags = flags;
	skel->rodata->hook_retval = hook_retval;

	if (!ASSERT_OK(lsm_inode_init_xattr_value__load(skel), "skel_load"))
		goto out;

	skel->bss->monitored_pid = getpid();

	link = bpf_program__attach_lsm(skel->progs.claim_value);
	if (!ASSERT_OK_PTR(link, "attach"))
		goto out;

	fd = open(file, O_CREAT | O_RDWR | O_EXCL, 0644);
	*open_errno = fd < 0 ? errno : 0;
	if (fd >= 0)
		close(fd);

	if (ASSERT_TRUE(skel->bss->hook_ran, "hook_ran"))
		err = skel->data->claim_err;
out:
	bpf_link__destroy(link);
	lsm_inode_init_xattr_value__destroy(skel);
	return err;
}

static void test_value_shapes(void)
{
	const char *file = TESTDIR "/value";
	char buf[64];
	int err, open_errno;

	remove(file);

	err = claim_value(file, 0, 0, 0, &open_errno);
	ASSERT_OK(err, "empty_value_err");
	ASSERT_OK(open_errno, "empty_value_open");
	ASSERT_EQ(read_label(file, "security.bpf.value", buf, sizeof(buf)),
		  0, "empty_value_len");
	remove(file);

	err = claim_value(file, sizeof(buf), 1, 0, &open_errno);
	ASSERT_EQ(err, -EINVAL, "bad_dynptr_err");
	ASSERT_EQ(read_label(file, "security.bpf.value", buf, sizeof(buf)),
		  -ENODATA, "bad_dynptr_absent");
	remove(file);

	err = claim_value(file, VALUE_SIZE_MAX + 1, 0, 0, &open_errno);
	ASSERT_EQ(err, -E2BIG, "oversized_err");
	ASSERT_EQ(read_label(file, "security.bpf.value", buf, sizeof(buf)),
		  -ENODATA, "oversized_absent");
	remove(file);

	err = claim_value(file, 8, 0, -EPERM, &open_errno);
	ASSERT_OK(err, "denied_claim_err");
	ASSERT_EQ(open_errno, EPERM, "denied_open");
	ASSERT_EQ(read_label(file, "security.bpf.value", buf, sizeof(buf)),
		  -ENOENT, "denied_absent");
	remove(file);
}

void test_lsm_inode_init_xattr(void)
{
	if (!ASSERT_OK(testdir_setup(), "testdir_setup"))
		goto out;

	if (test__start_subtest("init_labels"))
		test_init_labels();
	if (test__start_subtest("inherit_from_parent"))
		test_inherit_from_parent();
	if (test__start_subtest("refused_claims"))
		test_refused_claims();
	if (test__start_subtest("null_xattrs"))
		test_null_xattrs();
	if (test__start_subtest("shared_budget"))
		test_shared_budget();
	if (test__start_subtest("value_shapes"))
		test_value_shapes();
out:
	testdir_cleanup();
}
