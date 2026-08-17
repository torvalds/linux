// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include <linux/mount.h>
#include <linux/types.h>

#include "kselftest_harness.h"
#include "../wrappers.h"
#include "../utils.h"

/*
 * An idmapping that maps the mount-visible id range [0, ID_RANGE) onto the
 * host/overlay-final id range [ID_HOST, ID_HOST + ID_RANGE).  Through such an
 * idmapped overlay mount, an overlay-final id of ID_HOST + n is reported as n,
 * and an id of n requested through the mount is stored as ID_HOST + n.
 */
#define ID_NS	 0
#define ID_HOST	 10000
#define ID_RANGE 10000

/*
 * For the composition test the lower layer's on-disk ids live in a
 * separate range and are mapped by an idmapped lower layer onto the
 * overlay-final range [ID_HOST, ID_HOST + ID_RANGE).
 */
#define LAYER_HOST 20000

#ifndef MOUNT_ATTR_IDMAP
#define MOUNT_ATTR_IDMAP 0x00100000
#endif

#ifndef __NR_mount_setattr
#define __NR_mount_setattr 442
#endif

static inline int sys_mount_setattr(int dfd, const char *path,
				    unsigned int flags,
				    struct mount_attr *attr, size_t size)
{
	return syscall(__NR_mount_setattr, dfd, path, flags, attr, size);
}

static bool ovl_supported(void)
{
	int fd = sys_fsopen("overlay", 0);

	if (fd < 0)
		return false;
	close(fd);
	return true;
}

/* base/{l,u,w} owned by ID_HOST so they map to ID_NS through the idmap. */
static int setup_layers(const char *base)
{
	static const char *sub[] = { "", "/l", "/u", "/w" };
	char path[PATH_MAX];

	for (size_t i = 0; i < ARRAY_SIZE(sub); i++) {
		snprintf(path, sizeof(path), "%s%s", base, sub[i]);
		if (mkdir(path, 0755) && errno != EEXIST)
			return -1;
		if (i && chown(path, ID_HOST, ID_HOST))
			return -1;
	}
	return 0;
}

static int ovl_mount(const char *base, bool nfs_export)
{
	char lower[PATH_MAX], upper[PATH_MAX], work[PATH_MAX];
	int fsfd, ovl;

	snprintf(lower, sizeof(lower), "%s/l", base);
	snprintf(upper, sizeof(upper), "%s/u", base);
	snprintf(work, sizeof(work), "%s/w", base);

	fsfd = sys_fsopen("overlay", 0);
	if (fsfd < 0)
		return -1;

	if (sys_fsconfig(fsfd, FSCONFIG_SET_STRING, "source", "test", 0) ||
	    sys_fsconfig(fsfd, FSCONFIG_SET_STRING, "lowerdir", lower, 0) ||
	    sys_fsconfig(fsfd, FSCONFIG_SET_STRING, "upperdir", upper, 0) ||
	    sys_fsconfig(fsfd, FSCONFIG_SET_STRING, "workdir", work, 0))
		goto err;
	if (nfs_export &&
	    (sys_fsconfig(fsfd, FSCONFIG_SET_STRING, "index", "on", 0) ||
	     sys_fsconfig(fsfd, FSCONFIG_SET_STRING, "nfs_export", "on", 0)))
		goto err;
	if (sys_fsconfig(fsfd, FSCONFIG_CMD_CREATE, NULL, NULL, 0))
		goto err;

	ovl = sys_fsmount(fsfd, 0, 0);
	close(fsfd);
	return ovl;
err:
	close(fsfd);
	return -1;
}

/* Idmap the (still detached, not yet visible) overlay mount @mfd. */
static int ovl_idmap(int mfd)
{
	struct mount_attr attr = {
		.attr_set = MOUNT_ATTR_IDMAP,
	};
	int ret, userns_fd;

	/*
	 * get_userns_fd(fs_id, mount_id, range): a file whose filesystem id
	 * is fs_id + n is shown through the idmapped mount as mount_id + n.
	 * Here the overlay-final (fs side) range is [ID_HOST, ..) and the
	 * caller-visible (mount side) range is [ID_NS, ..).
	 */
	userns_fd = get_userns_fd(ID_HOST, ID_NS, ID_RANGE);
	if (userns_fd < 0)
		return -1;

	attr.userns_fd = userns_fd;
	ret = sys_mount_setattr(mfd, "", AT_EMPTY_PATH, &attr, sizeof(attr));
	close(userns_fd);
	return ret;
}

/* Clone @path into a detached, idmapped mount usable as an overlay layer. */
static int idmapped_layer_fd(const char *path, int nsid, int hostid, int range)
{
	struct mount_attr attr = {
		.attr_set = MOUNT_ATTR_IDMAP,
	};
	int fd_tree, userns_fd;

	fd_tree = sys_open_tree(AT_FDCWD, path,
			       OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
	if (fd_tree < 0)
		return -1;
	userns_fd = get_userns_fd(nsid, hostid, range);
	if (userns_fd < 0) {
		close(fd_tree);
		return -1;
	}
	attr.userns_fd = userns_fd;
	if (sys_mount_setattr(fd_tree, "", AT_EMPTY_PATH, &attr,
			      sizeof(attr))) {
		close(userns_fd);
		close(fd_tree);
		return -1;
	}
	close(userns_fd);
	return fd_tree;
}

/* Overlay with a layer passed by fd (idmapped) plus a plain upper/work. */
static int ovl_mount_lower_fd(const char *upper, const char *work, int fd_lower)
{
	int fsfd, ovl;

	fsfd = sys_fsopen("overlay", 0);
	if (fsfd < 0)
		return -1;

	if (sys_fsconfig(fsfd, FSCONFIG_SET_STRING, "source", "test", 0) ||
	    sys_fsconfig(fsfd, FSCONFIG_SET_STRING, "upperdir", upper, 0) ||
	    sys_fsconfig(fsfd, FSCONFIG_SET_STRING, "workdir", work, 0) ||
	    sys_fsconfig(fsfd, FSCONFIG_SET_FD, "lowerdir+", NULL, fd_lower) ||
	    sys_fsconfig(fsfd, FSCONFIG_CMD_CREATE, NULL, NULL, 0))
		goto err;

	ovl = sys_fsmount(fsfd, 0, 0);
	close(fsfd);
	return ovl;
err:
	close(fsfd);
	return -1;
}

/*
 * Mount an overlay inside user namespace @u1 (so the overlay sb's s_user_ns is
 * not the initial namespace) and idmap that overlay mount with @u2.  Runs in a
 * child that joins @u1; returns 0 on success.
 */
static int userns_overlay_child(int u1)
{
	struct mount_attr attr = {
		.attr_set = MOUNT_ATTR_IDMAP,
	};
	struct stat st;
	int ovl, u2;

	/* Become root in the overlay sb's user namespace u1. */
	if (!switch_userns(u1, 0, 0, false))
		return fprintf(stderr, "userns: switch_userns: %m\n"), -1;
	if (unshare(CLONE_NEWNS) ||
	    sys_mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL))
		return fprintf(stderr, "userns: unshare/slave: %m\n"), -1;
	if (sys_mount("tmpfs", "/tmp", "tmpfs", 0, NULL))
		return fprintf(stderr, "userns: mount tmpfs: %m\n"), -1;
	if (setup_layers("/tmp/ovl"))
		return fprintf(stderr, "userns: setup_layers: %m\n"), -1;
	if (mknod("/tmp/ovl/l/file", S_IFREG | 0644, 0) ||
	    chown("/tmp/ovl/l/file", ID_HOST + 5, ID_HOST + 5))
		return fprintf(stderr, "userns: lower file: %m\n"), -1;

	ovl = ovl_mount("/tmp/ovl", false);
	if (ovl < 0)
		return fprintf(stderr, "userns: ovl_mount: %m\n"), -1;

	/*
	 * mount_setattr() requires CAP_SYS_ADMIN over the idmap user
	 * namespace, so it must be a child of u1.  Create it now, from
	 * inside u1.
	 */
	u2 = get_userns_fd(ID_HOST, ID_NS, ID_RANGE);
	if (u2 < 0)
		return fprintf(stderr, "userns: get_userns_fd: %m\n"), -1;
	attr.userns_fd = u2;
	if (sys_mount_setattr(ovl, "", AT_EMPTY_PATH, &attr, sizeof(attr)))
		return fprintf(stderr, "userns: mount_setattr: %m\n"), -1;
	close(u2);

	if (fstatat(ovl, "file", &st, 0))
		return fprintf(stderr, "userns: fstatat: %m\n"), -1;
	if (st.st_uid != ID_NS + 5 || st.st_gid != ID_NS + 5) {
		fprintf(stderr, "userns: got %u:%u expected %u:%u\n",
			st.st_uid, st.st_gid, ID_NS + 5, ID_NS + 5);
		return -1;
	}
	return 0;
}

FIXTURE(idmapped_overlay) {
	char base[64];
};

FIXTURE_SETUP(idmapped_overlay)
{
	/* Private mount namespace so test mounts need no cleanup. */
	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_EQ(sys_mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL), 0);

	/* tmpfs for the layers so we can chown them to arbitrary ids. */
	ASSERT_EQ(sys_mount("tmpfs", "/tmp", "tmpfs", 0, NULL), 0);

	snprintf(self->base, sizeof(self->base), "/tmp/ovl");
	ASSERT_EQ(setup_layers(self->base), 0);
}

FIXTURE_TEARDOWN(idmapped_overlay)
{
}

/* A file owned by ID_HOST + 5 is reported as ID_NS + 5 through the idmap. */
TEST_F(idmapped_overlay, getattr)
{
	char path[PATH_MAX];
	struct stat st;
	int ovl;

	if (!ovl_supported())
		SKIP(return, "overlayfs not supported");

	snprintf(path, sizeof(path), "%s/l/file", self->base);
	ASSERT_EQ(mknod(path, S_IFREG | 0644, 0), 0);
	ASSERT_EQ(chown(path, ID_HOST + 5, ID_HOST + 5), 0);

	ovl = ovl_mount(self->base, false);
	ASSERT_GE(ovl, 0);
	ASSERT_EQ(ovl_idmap(ovl), 0);

	ASSERT_EQ(fstatat(ovl, "file", &st, 0), 0);
	EXPECT_EQ(st.st_uid, ID_NS + 5);
	EXPECT_EQ(st.st_gid, ID_NS + 5);

	EXPECT_EQ(close(ovl), 0);
}

/*
 * Every creation path initializes the new owner through the mount idmap:
 * created as caller id ID_NS, stored on the upper layer as overlay-final
 * ID_HOST.  Covers ovl_create() (regular file), ovl_mkdir(), ovl_mknod()
 * and ovl_symlink() (which share ovl_create_object()), plus the separate
 * ovl_tmpfile() path.
 */
TEST_F(idmapped_overlay, create)
{
	static const char *names[] = { "reg", "dir", "fifo", "lnk" };
	char path[PATH_MAX];
	struct stat st;
	int ovl, fd;

	if (!ovl_supported())
		SKIP(return, "overlayfs not supported");

	ovl = ovl_mount(self->base, false);
	ASSERT_GE(ovl, 0);
	ASSERT_EQ(ovl_idmap(ovl), 0);

	/* One object per creation operation, all as caller id ID_NS. */
	fd = openat(ovl, "reg", O_CREAT | O_WRONLY | O_EXCL, 0644);
	ASSERT_GE(fd, 0);
	EXPECT_EQ(close(fd), 0);
	ASSERT_EQ(mkdirat(ovl, "dir", 0755), 0);
	ASSERT_EQ(mknodat(ovl, "fifo", S_IFIFO | 0644, 0), 0);
	ASSERT_EQ(symlinkat("target", ovl, "lnk"), 0);

	for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
		/* Reported as ID_NS through the idmapped mount ... */
		ASSERT_EQ(fstatat(ovl, names[i], &st, AT_SYMLINK_NOFOLLOW), 0);
		EXPECT_EQ(st.st_uid, ID_NS);
		EXPECT_EQ(st.st_gid, ID_NS);
		/* ... and stored as ID_HOST on the upper layer. */
		snprintf(path, sizeof(path), "%s/u/%s", self->base, names[i]);
		ASSERT_EQ(lstat(path, &st), 0);
		EXPECT_EQ(st.st_uid, ID_HOST);
		EXPECT_EQ(st.st_gid, ID_HOST);
	}

	/* O_TMPFILE goes through the separate ovl_tmpfile() path. */
	fd = openat(ovl, ".", O_TMPFILE | O_WRONLY, 0644);
	ASSERT_GE(fd, 0);
	/* Inside the mount: caller id ID_NS. */
	ASSERT_EQ(fstat(fd, &st), 0);
	EXPECT_EQ(st.st_uid, ID_NS);
	EXPECT_EQ(st.st_gid, ID_NS);
	/* Link it in so the upper backing file can be inspected too. */
	ASSERT_EQ(linkat(fd, "", ovl, "tmp", AT_EMPTY_PATH), 0);
	EXPECT_EQ(close(fd), 0);
	snprintf(path, sizeof(path), "%s/u/tmp", self->base);
	ASSERT_EQ(lstat(path, &st), 0);
	EXPECT_EQ(st.st_uid, ID_HOST);
	EXPECT_EQ(st.st_gid, ID_HOST);

	EXPECT_EQ(close(ovl), 0);
}

/* chown through the idmapped mount round-trips: ID_NS + 5 <-> ID_HOST + 5. */
TEST_F(idmapped_overlay, chown)
{
	char path[PATH_MAX];
	struct stat st;
	int ovl, fd;

	if (!ovl_supported())
		SKIP(return, "overlayfs not supported");

	ovl = ovl_mount(self->base, false);
	ASSERT_GE(ovl, 0);
	ASSERT_EQ(ovl_idmap(ovl), 0);

	fd = openat(ovl, "f", O_CREAT | O_WRONLY | O_EXCL, 0644);
	ASSERT_GE(fd, 0);
	EXPECT_EQ(close(fd), 0);

	ASSERT_EQ(fchownat(ovl, "f", ID_NS + 5, ID_NS + 5, 0), 0);

	ASSERT_EQ(fstatat(ovl, "f", &st, 0), 0);
	EXPECT_EQ(st.st_uid, ID_NS + 5);
	EXPECT_EQ(st.st_gid, ID_NS + 5);

	snprintf(path, sizeof(path), "%s/u/f", self->base);
	ASSERT_EQ(stat(path, &st), 0);
	EXPECT_EQ(st.st_uid, ID_HOST + 5);
	EXPECT_EQ(st.st_gid, ID_HOST + 5);

	EXPECT_EQ(close(ovl), 0);
}

/*
 * Composition: an idmapped lower layer underneath an idmapped overlay mount.
 * An on-disk id is mapped by the layer idmap into the overlay-final range and
 * then by the mount idmap into the caller's range:
 *
 *   on-disk LAYER_HOST+7  --layer-->  ID_HOST+7  --mount-->  ID_NS+7
 */
TEST_F(idmapped_overlay, composition)
{
	char lower[PATH_MAX], upper[PATH_MAX], work[PATH_MAX], path[PATH_MAX];
	struct stat st;
	int ovl, fd_lower;

	if (!ovl_supported())
		SKIP(return, "overlayfs not supported");

	snprintf(lower, sizeof(lower), "%s/l", self->base);
	snprintf(upper, sizeof(upper), "%s/u", self->base);
	snprintf(work, sizeof(work), "%s/w", self->base);

	/* Put the lower layer's ids in the on-disk [LAYER_HOST, ..) range. */
	ASSERT_EQ(chown(lower, LAYER_HOST, LAYER_HOST), 0);
	snprintf(path, sizeof(path), "%s/l/file", self->base);
	ASSERT_EQ(mknod(path, S_IFREG | 0644, 0), 0);
	ASSERT_EQ(chown(path, LAYER_HOST + 7, LAYER_HOST + 7), 0);

	/* Idmapped lower: on-disk LAYER_HOST <-> overlay-final ID_HOST. */
	fd_lower = idmapped_layer_fd(lower, LAYER_HOST, ID_HOST, ID_RANGE);
	ASSERT_GE(fd_lower, 0);

	ovl = ovl_mount_lower_fd(upper, work, fd_lower);
	ASSERT_GE(ovl, 0);
	EXPECT_EQ(close(fd_lower), 0);

	/* Idmap the overlay mount: overlay-final ID_HOST <-> caller ID_NS. */
	ASSERT_EQ(ovl_idmap(ovl), 0);

	ASSERT_EQ(fstatat(ovl, "file", &st, 0), 0);
	EXPECT_EQ(st.st_uid, ID_NS + 7);
	EXPECT_EQ(st.st_gid, ID_NS + 7);

	EXPECT_EQ(close(ovl), 0);
}

/* An idmapped overlay mount whose sb lives inside a user namespace. */
TEST_F(idmapped_overlay, userns)
{
	int u1;
	pid_t pid;

	if (!ovl_supported())
		SKIP(return, "overlayfs not supported");

	/* u1 backs the overlay sb: identity-mapped, but not the init ns. */
	u1 = get_userns_fd(0, 0, 65536);
	if (u1 < 0)
		SKIP(return, "user namespaces not available");

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0) {
		int ret = userns_overlay_child(u1);

		_exit(ret ? EXIT_FAILURE : EXIT_SUCCESS);
	}
	EXPECT_EQ(wait_for_pid(pid), 0);

	EXPECT_EQ(close(u1), 0);
}

/*
 * An nfs_export overlay can be idmapped, and decodable file handles round-trip
 * through the idmapped mount with correctly mapped ownership.  Overlay file
 * handles encode object identity, not ownership, so the mount idmap does not
 * affect them; it only maps the owner reported once a handle is reopened.
 */
TEST_F(idmapped_overlay, nfs_export_handles)
{
	char path[PATH_MAX], mnt[128];
	union {
		struct file_handle fh;
		char buf[sizeof(struct file_handle) + MAX_HANDLE_SZ];
	} fhu;
	struct file_handle *fh = &fhu.fh;
	struct stat st;
	int ovl, mfd, fd, mount_id;

	if (!ovl_supported())
		SKIP(return, "overlayfs not supported");

	snprintf(path, sizeof(path), "%s/l/file", self->base);
	ASSERT_EQ(mknod(path, S_IFREG | 0644, 0), 0);
	ASSERT_EQ(chown(path, ID_HOST + 7, ID_HOST + 7), 0);

	/* nfs_export=on gives decodable overlay file handles. */
	ovl = ovl_mount(self->base, true);
	if (ovl < 0)
		SKIP(return, "overlayfs nfs_export not supported");
	ASSERT_EQ(ovl_idmap(ovl), 0);

	/* Attach the idmapped mount so handles can be resolved against it. */
	snprintf(mnt, sizeof(mnt), "%s/mnt", self->base);
	ASSERT_EQ(mkdir(mnt, 0755), 0);
	ASSERT_EQ(sys_move_mount(ovl, "", AT_FDCWD, mnt,
				 MOVE_MOUNT_F_EMPTY_PATH), 0);

	snprintf(path, sizeof(path), "%s/file", mnt);
	fh->handle_bytes = MAX_HANDLE_SZ;
	ASSERT_EQ(name_to_handle_at(AT_FDCWD, path, fh, &mount_id, 0), 0);

	mfd = open(mnt, O_RDONLY | O_DIRECTORY);
	ASSERT_GE(mfd, 0);
	fd = open_by_handle_at(mfd, fh, O_RDONLY);
	EXPECT_EQ(close(mfd), 0);
	ASSERT_GE(fd, 0);

	ASSERT_EQ(fstat(fd, &st), 0);
	EXPECT_EQ(st.st_uid, ID_NS + 7);
	EXPECT_EQ(st.st_gid, ID_NS + 7);

	EXPECT_EQ(close(fd), 0);
	EXPECT_EQ(close(ovl), 0);
}

TEST_HARNESS_MAIN
