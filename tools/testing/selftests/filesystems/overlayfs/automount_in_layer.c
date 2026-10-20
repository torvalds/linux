// SPDX-License-Identifier: GPL-2.0
/*
 * A layer of an overlay is a private clone of the mount it was given and
 * belongs to no mount namespace. fanotify hands out descriptors on it. An
 * automount triggered through one has no namespace to go into: the open has
 * to fail, not oops with namespace_sem held.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/magic.h>
#include <sys/fanotify.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/vfs.h>

#include "../../kselftest_harness.h"

#define DIR_LEN		64
#define PATH_LEN	192

static bool have_fs(const char *name)
{
	char line[128];
	bool found = false;
	FILE *f;

	f = fopen("/proc/filesystems", "re");
	if (!f)
		return false;
	while (fgets(line, sizeof(line), f)) {
		char *nl = strchr(line, '\n');
		char *tab = strchr(line, '\t');

		if (nl)
			*nl = 0;
		if (tab && !strcmp(tab + 1, name))
			found = true;
	}
	fclose(f);
	return found;
}

static int mnt_id_of(int fd)
{
	char path[64], buf[4096], *p;
	ssize_t n;
	int info;

	snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", fd);
	info = open(path, O_RDONLY | O_CLOEXEC);
	if (info < 0)
		return -1;
	n = read(info, buf, sizeof(buf) - 1);
	close(info);
	if (n <= 0)
		return -1;
	buf[n] = 0;
	p = strstr(buf, "mnt_id:");
	return p ? atoi(p + strlen("mnt_id:")) : -1;
}

static bool on_debugfs(int fd)
{
	struct statfs sf;

	return !fstatfs(fd, &sf) && sf.f_type == DEBUGFS_MAGIC;
}

FIXTURE(layer) {
	char base[DIR_LEN];
	int fan;
	int evfd;	/* on the layer clone of the lower debugfs */
};

FIXTURE_SETUP(layer)
{
	char lower[PATH_LEN], other[PATH_LEN], ovl[PATH_LEN], opts[2 * PATH_LEN + 16];
	struct fanotify_event_metadata *ev;
	struct pollfd pfd;
	char buf[4096];
	int lfd, lower_id;
	ssize_t n;
	DIR *d;

	self->fan = -1;
	self->evfd = -1;
	if (geteuid())
		SKIP(return, "test requires root");
	if (!have_fs("debugfs") || !have_fs("tracefs"))
		SKIP(return, "test requires debugfs with the tracefs automount");
	if (!have_fs("overlay"))
		SKIP(return, "test requires overlayfs");

	snprintf(self->base, sizeof(self->base), "/tmp/layer.XXXXXX");
	ASSERT_NE(mkdtemp(self->base), NULL);
	ASSERT_EQ(unshare(CLONE_NEWNS), 0);
	ASSERT_EQ(mount("", "/", NULL, MS_REC | MS_PRIVATE, NULL), 0);
	ASSERT_EQ(mount("tmpfs", self->base, "tmpfs", 0, "mode=0755"), 0);
	snprintf(lower, sizeof(lower), "%s/lower", self->base);
	snprintf(other, sizeof(other), "%s/other", self->base);
	snprintf(ovl, sizeof(ovl), "%s/ovl", self->base);
	ASSERT_EQ(mkdir(lower, 0755), 0);
	ASSERT_EQ(mkdir(other, 0755), 0);
	ASSERT_EQ(mkdir(ovl, 0755), 0);
	ASSERT_EQ(mount("debugfs", lower, "debugfs", 0, NULL), 0);
	snprintf(opts, sizeof(opts), "lowerdir=%s:%s", lower, other);
	ASSERT_EQ(mount("overlay", ovl, "overlay", MS_RDONLY, opts), 0);

	self->fan = fanotify_init(FAN_CLASS_NOTIF | FAN_NONBLOCK | FAN_CLOEXEC,
				  O_RDONLY | O_CLOEXEC);
	ASSERT_GE(self->fan, 0);
	ASSERT_EQ(fanotify_mark(self->fan, FAN_MARK_ADD | FAN_MARK_FILESYSTEM,
				FAN_OPEN | FAN_ONDIR, AT_FDCWD, lower), 0);
	lfd = open(lower, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	ASSERT_GE(lfd, 0);
	lower_id = mnt_id_of(lfd);
	close(lfd);

	/* the overlay opens its lower directory through the layer clone */
	d = opendir(ovl);
	ASSERT_NE(d, NULL);
	closedir(d);
	pfd.fd = self->fan;
	pfd.events = POLLIN;
	ASSERT_EQ(poll(&pfd, 1, 5000), 1);
	n = read(self->fan, buf, sizeof(buf));
	ASSERT_GT(n, 0);
	for (ev = (void *)buf; FAN_EVENT_OK(ev, n); ev = FAN_EVENT_NEXT(ev, n)) {
		if (ev->fd < 0)
			continue;
		if (self->evfd < 0 && on_debugfs(ev->fd) &&
		    mnt_id_of(ev->fd) != lower_id)
			self->evfd = ev->fd;
		else
			close(ev->fd);
	}
	ASSERT_GE(self->evfd, 0)
		TH_LOG("no event on the layer clone");
}

FIXTURE_TEARDOWN(layer)
{
	if (self->evfd >= 0)
		close(self->evfd);
	if (self->fan >= 0)
		close(self->fan);
	umount2(self->base, MNT_DETACH);
	rmdir(self->base);
}

TEST_F(layer, automount_below_the_clone_is_refused)
{
	struct stat st;
	int fd;

	/* the automount point is there */
	ASSERT_EQ(fstatat(self->evfd, "tracing", &st, AT_NO_AUTOMOUNT), 0);
	/* the clone is in no namespace, so the automount has nowhere to go */
	fd = openat(self->evfd, "tracing", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	EXPECT_LT(fd, 0);
	EXPECT_EQ(errno, EINVAL);
	if (fd >= 0)
		close(fd);
}

TEST_HARNESS_MAIN
