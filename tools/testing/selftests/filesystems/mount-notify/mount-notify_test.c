// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2025 Miklos Szeredi <miklos@szeredi.hu>

#define _GNU_SOURCE
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <linux/fanotify.h>
#include <unistd.h>
#include <sys/fanotify.h>
#include <sys/syscall.h>

#include "../../kselftest_harness.h"
#include "../statmount/statmount.h"

#ifndef FAN_MNT_ATTACH
struct fanotify_event_info_mnt {
	struct fanotify_event_info_header hdr;
	__u64 mnt_id;
};
#define FAN_MNT_ATTACH 0x01000000 /* Mount was attached */
#endif

#ifndef FAN_MNT_DETACH
#define FAN_MNT_DETACH 0x02000000 /* Mount was detached */
#endif

#ifndef FAN_REPORT_MNT
#define FAN_REPORT_MNT 0x00004000 /* Report mount events */
#endif

#ifndef FAN_MARK_MNTNS
#define FAN_MARK_MNTNS 0x00000110
#endif

static uint64_t get_mnt_id(struct __test_metadata *const _metadata,
			   const char *path)
{
	struct statx sx;

	ASSERT_EQ(statx(AT_FDCWD, path, 0, STATX_MNT_ID_UNIQUE, &sx), 0);
	ASSERT_TRUE(!!(sx.stx_mask & STATX_MNT_ID_UNIQUE));
	return sx.stx_mnt_id;
}

static const char root_mntpoint_templ[] = "/tmp/mount-notify_test_root.XXXXXX";

FIXTURE(fanotify) {
	int fan_fd;
	char buf[256];
	unsigned int rem;
	void *next;
	char root_mntpoint[sizeof(root_mntpoint_templ)];
	int orig_root;
	int ns_fd;
	uint64_t root_id;
};

FIXTURE_SETUP(fanotify)
{
	int ret;

	ASSERT_EQ(unshare(CLONE_NEWNS), 0);

	self->ns_fd = open("/proc/self/ns/mnt", O_RDONLY);
	ASSERT_GE(self->ns_fd, 0);

	ASSERT_EQ(mount("", "/", NULL, MS_REC|MS_PRIVATE, NULL), 0);

	strcpy(self->root_mntpoint, root_mntpoint_templ);
	ASSERT_NE(mkdtemp(self->root_mntpoint), NULL);

	self->orig_root = open("/", O_PATH | O_CLOEXEC);
	ASSERT_GE(self->orig_root, 0);

	ASSERT_EQ(mount("tmpfs", self->root_mntpoint, "tmpfs", 0, NULL), 0);

	ASSERT_EQ(chroot(self->root_mntpoint), 0);

	ASSERT_EQ(chdir("/"), 0);

	ASSERT_EQ(mkdir("a", 0700), 0);

	ASSERT_EQ(mkdir("b", 0700), 0);

	self->root_id = get_mnt_id(_metadata, "/");
	ASSERT_NE(self->root_id, 0);

	self->fan_fd = fanotify_init(FAN_REPORT_MNT, 0);
	ASSERT_GE(self->fan_fd, 0);

	ret = fanotify_mark(self->fan_fd, FAN_MARK_ADD | FAN_MARK_MNTNS,
			    FAN_MNT_ATTACH | FAN_MNT_DETACH, self->ns_fd, NULL);
	ASSERT_EQ(ret, 0);

	self->rem = 0;
}

FIXTURE_TEARDOWN(fanotify)
{
	ASSERT_EQ(self->rem, 0);
	close(self->fan_fd);

	ASSERT_EQ(fchdir(self->orig_root), 0);

	ASSERT_EQ(chroot("."), 0);

	EXPECT_EQ(umount2(self->root_mntpoint, MNT_DETACH), 0);
	EXPECT_EQ(chdir(self->root_mntpoint), 0);
	EXPECT_EQ(chdir("/"), 0);
	EXPECT_EQ(rmdir(self->root_mntpoint), 0);
}

static uint64_t expect_notify(struct __test_metadata *const _metadata,
			      FIXTURE_DATA(fanotify) *self,
			      uint64_t *mask)
{
	struct fanotify_event_metadata *meta;
	struct fanotify_event_info_mnt *mnt;
	unsigned int thislen;

	if (!self->rem) {
		ssize_t len = read(self->fan_fd, self->buf, sizeof(self->buf));
		ASSERT_GT(len, 0);

		self->rem = len;
		self->next = (void *) self->buf;
	}

	meta = self->next;
	ASSERT_TRUE(FAN_EVENT_OK(meta, self->rem));

	thislen = meta->event_len;
	self->rem -= thislen;
	self->next += thislen;

	*mask = meta->mask;
	thislen -= sizeof(*meta);

	mnt = ((void *) meta) + meta->event_len - thislen;

	ASSERT_EQ(thislen, sizeof(*mnt));

	return mnt->mnt_id;
}

static void expect_notify_n(struct __test_metadata *const _metadata,
				 FIXTURE_DATA(fanotify) *self,
				 unsigned int n, uint64_t mask[], uint64_t mnts[])
{
	unsigned int i;

	for (i = 0; i < n; i++)
		mnts[i] = expect_notify(_metadata, self, &mask[i]);
}

static uint64_t expect_notify_mask(struct __test_metadata *const _metadata,
				   FIXTURE_DATA(fanotify) *self,
				   uint64_t expect_mask)
{
	uint64_t mntid, mask;

	mntid = expect_notify(_metadata, self, &mask);
	ASSERT_EQ(expect_mask, mask);

	return mntid;
}


static void expect_notify_mask_n(struct __test_metadata *const _metadata,
				 FIXTURE_DATA(fanotify) *self,
				 uint64_t mask, unsigned int n, uint64_t mnts[])
{
	unsigned int i;

	for (i = 0; i < n; i++)
		mnts[i] = expect_notify_mask(_metadata, self, mask);
}

static void verify_mount_ids(struct __test_metadata *const _metadata,
			     const uint64_t list1[], const uint64_t list2[],
			     size_t num)
{
	unsigned int i, j;

	// Check that neither list has any duplicates
	for (i = 0; i < num; i++) {
		for (j = 0; j < num; j++) {
			if (i != j) {
				ASSERT_NE(list1[i], list1[j]);
				ASSERT_NE(list2[i], list2[j]);
			}
		}
	}
	// Check that all list1 memebers can be found in list2. Together with
	// the above it means that the list1 and list2 represent the same sets.
	for (i = 0; i < num; i++) {
		for (j = 0; j < num; j++) {
			if (list1[i] == list2[j])
				break;
		}
		ASSERT_NE(j, num);
	}
}

static void check_mounted(struct __test_metadata *const _metadata,
			  const uint64_t mnts[], size_t num)
{
	ssize_t ret;
	uint64_t *list;

	list = malloc((num + 1) * sizeof(list[0]));
	ASSERT_NE(list, NULL);

	ret = listmount(LSMT_ROOT, 0, 0, list, num + 1, 0);
	ASSERT_EQ(ret, num);

	verify_mount_ids(_metadata, mnts, list, num);

	free(list);
}

static void setup_mount_tree(struct __test_metadata *const _metadata,
			    int log2_num)
{
	int ret, i;

	ret = mount("", "/", NULL, MS_SHARED, NULL);
	ASSERT_EQ(ret, 0);

	for (i = 0; i < log2_num; i++) {
		ret = mount("/", "/", NULL, MS_BIND, NULL);
		ASSERT_EQ(ret, 0);
	}
}

TEST_F(fanotify, bind)
{
	int ret;
	uint64_t mnts[2] = { self->root_id };

	ret = mount("/", "/", NULL, MS_BIND, NULL);
	ASSERT_EQ(ret, 0);

	mnts[1] = expect_notify_mask(_metadata, self, FAN_MNT_ATTACH);
	ASSERT_NE(mnts[0], mnts[1]);

	check_mounted(_metadata, mnts, 2);

	// Cleanup
	uint64_t detach_id;
	ret = umount("/");
	ASSERT_EQ(ret, 0);

	detach_id = expect_notify_mask(_metadata, self, FAN_MNT_DETACH);
	ASSERT_EQ(detach_id, mnts[1]);

	check_mounted(_metadata, mnts, 1);
}

TEST_F(fanotify, move)
{
	int ret;
	uint64_t mnts[2] = { self->root_id };
	uint64_t move_id;

	ret = mount("/", "/a", NULL, MS_BIND, NULL);
	ASSERT_EQ(ret, 0);

	mnts[1] = expect_notify_mask(_metadata, self, FAN_MNT_ATTACH);
	ASSERT_NE(mnts[0], mnts[1]);

	check_mounted(_metadata, mnts, 2);

	ret = move_mount(AT_FDCWD, "/a", AT_FDCWD, "/b", 0);
	ASSERT_EQ(ret, 0);

	move_id = expect_notify_mask(_metadata, self, FAN_MNT_ATTACH | FAN_MNT_DETACH);
	ASSERT_EQ(move_id, mnts[1]);

	// Cleanup
	ret = umount("/b");
	ASSERT_EQ(ret, 0);

	check_mounted(_metadata, mnts, 1);
}

TEST_F(fanotify, propagate)
{
	const unsigned int log2_num = 4;
	const unsigned int num = (1 << log2_num);
	uint64_t mnts[num];

	setup_mount_tree(_metadata, log2_num);

	expect_notify_mask_n(_metadata, self, FAN_MNT_ATTACH, num - 1, mnts + 1);

	mnts[0] = self->root_id;
	check_mounted(_metadata, mnts, num);

	// Cleanup
	int ret;
	uint64_t mnts2[num];
	ret = umount2("/", MNT_DETACH);
	ASSERT_EQ(ret, 0);

	ret = mount("", "/", NULL, MS_PRIVATE, NULL);
	ASSERT_EQ(ret, 0);

	mnts2[0] = self->root_id;
	expect_notify_mask_n(_metadata, self, FAN_MNT_DETACH, num - 1, mnts2 + 1);
	verify_mount_ids(_metadata, mnts, mnts2, num);

	check_mounted(_metadata, mnts, 1);
}

TEST_F(fanotify, fsmount)
{
	int ret, fs, mnt;
	uint64_t mnts[2] = { self->root_id };

	fs = fsopen("tmpfs", 0);
	ASSERT_GE(fs, 0);

        ret = fsconfig(fs, FSCONFIG_CMD_CREATE, 0, 0, 0);
	ASSERT_EQ(ret, 0);

        mnt = fsmount(fs, 0, 0);
	ASSERT_GE(mnt, 0);

        close(fs);

	ret = move_mount(mnt, "", AT_FDCWD, "/a", MOVE_MOUNT_F_EMPTY_PATH);
	ASSERT_EQ(ret, 0);

        close(mnt);

	mnts[1] = expect_notify_mask(_metadata, self, FAN_MNT_ATTACH);
	ASSERT_NE(mnts[0], mnts[1]);

	check_mounted(_metadata, mnts, 2);

	// Cleanup
	uint64_t detach_id;
	ret = umount("/a");
	ASSERT_EQ(ret, 0);

	detach_id = expect_notify_mask(_metadata, self, FAN_MNT_DETACH);
	ASSERT_EQ(detach_id, mnts[1]);

	check_mounted(_metadata, mnts, 1);
}

TEST_F(fanotify, reparent)
{
	uint64_t mnts[6] = { self->root_id };
	uint64_t dmnts[3];
	uint64_t masks[3];
	unsigned int i;
	int ret;

	// Create setup with a[1] -> b[2] propagation
	ret = mount("/", "/a", NULL, MS_BIND, NULL);
	ASSERT_EQ(ret, 0);

	ret = mount("", "/a", NULL, MS_SHARED, NULL);
	ASSERT_EQ(ret, 0);

	ret = mount("/a", "/b", NULL, MS_BIND, NULL);
	ASSERT_EQ(ret, 0);

	ret = mount("", "/b", NULL, MS_SLAVE, NULL);
	ASSERT_EQ(ret, 0);

	expect_notify_mask_n(_metadata, self, FAN_MNT_ATTACH, 2, mnts + 1);

	check_mounted(_metadata, mnts, 3);

	// Mount on a[3], which is propagated to b[4]
	ret = mount("/", "/a", NULL, MS_BIND, NULL);
	ASSERT_EQ(ret, 0);

	expect_notify_mask_n(_metadata, self, FAN_MNT_ATTACH, 2, mnts + 3);

	check_mounted(_metadata, mnts, 5);

	// Mount on b[5], not propagated
	ret = mount("/", "/b", NULL, MS_BIND, NULL);
	ASSERT_EQ(ret, 0);

	mnts[5] = expect_notify_mask(_metadata, self, FAN_MNT_ATTACH);

	check_mounted(_metadata, mnts, 6);

	// Umount a[3], which is propagated to b[4], but not b[5]
	// This will result in b[5] "falling" on b[2]
	ret = umount("/a");
	ASSERT_EQ(ret, 0);

	expect_notify_n(_metadata, self, 3, masks, dmnts);
	verify_mount_ids(_metadata, mnts + 3, dmnts, 3);

	for (i = 0; i < 3; i++) {
		if (dmnts[i] == mnts[5]) {
			ASSERT_EQ(masks[i], FAN_MNT_ATTACH | FAN_MNT_DETACH);
		} else {
			ASSERT_EQ(masks[i], FAN_MNT_DETACH);
		}
	}

	mnts[3] = mnts[5];
	check_mounted(_metadata, mnts, 4);

	// Cleanup
	ret = umount("/b");
	ASSERT_EQ(ret, 0);

	ret = umount("/a");
	ASSERT_EQ(ret, 0);

	ret = umount("/b");
	ASSERT_EQ(ret, 0);

	expect_notify_mask_n(_metadata, self, FAN_MNT_DETACH, 3, dmnts);
	verify_mount_ids(_metadata, mnts + 1, dmnts, 3);

	check_mounted(_metadata, mnts, 1);
}

TEST_F(fanotify, rmdir)
{
	uint64_t mnts[3] = { self->root_id };
	int ret;

	ret = mount("/", "/a", NULL, MS_BIND, NULL);
	ASSERT_EQ(ret, 0);

	ret = mount("/", "/a/b", NULL, MS_BIND, NULL);
	ASSERT_EQ(ret, 0);

	expect_notify_mask_n(_metadata, self, FAN_MNT_ATTACH, 2, mnts + 1);

	check_mounted(_metadata, mnts, 3);

	ret = chdir("/a");
	ASSERT_EQ(ret, 0);

	ret = fork();
	ASSERT_GE(ret, 0);

	if (ret == 0) {
		chdir("/");
		unshare(CLONE_NEWNS);
		mount("", "/", NULL, MS_REC|MS_PRIVATE, NULL);
		umount2("/a", MNT_DETACH);
		// This triggers a detach in the other namespace
		rmdir("/a");
		exit(0);
	}
	wait(NULL);

	expect_notify_mask_n(_metadata, self, FAN_MNT_DETACH, 2, mnts + 1);
	check_mounted(_metadata, mnts, 1);

	// Cleanup
	ret = chdir("/");
	ASSERT_EQ(ret, 0);
}

TEST_F(fanotify, pivot_root)
{
	uint64_t mnts[3] = { self->root_id };
	uint64_t mnts2[3];
	int ret;

	ret = mount("tmpfs", "/a", "tmpfs", 0, NULL);
	ASSERT_EQ(ret, 0);

	mnts[2] = expect_notify_mask(_metadata, self, FAN_MNT_ATTACH);

	ret = mkdir("/a/new", 0700);
	ASSERT_EQ(ret, 0);

	ret = mkdir("/a/old", 0700);
	ASSERT_EQ(ret, 0);

	ret = mount("/a", "/a/new", NULL, MS_BIND, NULL);
	ASSERT_EQ(ret, 0);

	mnts[1] = expect_notify_mask(_metadata, self, FAN_MNT_ATTACH);
	check_mounted(_metadata, mnts, 3);

	ret = syscall(SYS_pivot_root, "/a/new", "/a/new/old");
	ASSERT_EQ(ret, 0);

	expect_notify_mask_n(_metadata, self, FAN_MNT_ATTACH | FAN_MNT_DETACH, 2, mnts2);
	verify_mount_ids(_metadata, mnts, mnts2, 2);
	check_mounted(_metadata, mnts, 3);

	// Cleanup
	ret = syscall(SYS_pivot_root, "/old", "/old/a/new");
	ASSERT_EQ(ret, 0);

	ret = umount("/a/new");
	ASSERT_EQ(ret, 0);

	ret = umount("/a");
	ASSERT_EQ(ret, 0);

	check_mounted(_metadata, mnts, 1);
}

TEST_HARNESS_MAIN
