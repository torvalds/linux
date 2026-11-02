// SPDX-License-Identifier: GPL-2.0
/*
 * Test that FUSE_SYNCFS is propagated to a userspace server only when the
 * server opts in with FUSE_HAS_SYNCFS *and* opened /dev/fuse with
 * CAP_SYS_ADMIN in the initial user namespace.
 *
 * Unlike the libfuse-based selftests, this talks the raw FUSE wire protocol
 * over /dev/fuse so it can (a) choose whether to advertise FUSE_HAS_SYNCFS in
 * the INIT reply and (b) observe directly whether a FUSE_SYNCFS opcode arrives.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <linux/capability.h>
#include <linux/fuse.h>

#include "../../kselftest.h"

#define FUSE_ROOT_ID 1

/*
 * Add or drop CAP_SYS_ADMIN in the effective set via raw capget/capset
 * (avoids a libcap dependency).  Used to construct a server that opens
 * /dev/fuse without the privilege FUSE_HAS_SYNCFS requires.
 */
static int set_sysadmin(int on)
{
	struct __user_cap_header_struct hdr = {
		.version = _LINUX_CAPABILITY_VERSION_3,
		.pid = 0,
	};
	struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3] = {};
	unsigned int idx = CAP_SYS_ADMIN >> 5;
	__u32 bit = 1U << (CAP_SYS_ADMIN & 31);

	if (syscall(SYS_capget, &hdr, data))
		return -1;
	if (on)
		data[idx].effective |= bit;
	else
		data[idx].effective &= ~bit;
	return syscall(SYS_capset, &hdr, data);
}

/*
 * eventfd the server child writes once when it receives FUSE_SYNCFS; the
 * parent poll()s it to observe (or rule out) propagation.
 */
static int syncfs_evfd;

static void reply(int fd, uint64_t unique, int error, void *data, size_t len)
{
	struct fuse_out_header oh = {
		.len = sizeof(oh) + len,
		.error = error,
		.unique = unique,
	};
	struct iovec iov[2] = {
		{ &oh, sizeof(oh) },
		{ data, len },
	};

	if (writev(fd, iov, data ? 2 : 1) < 0)
		ksft_print_msg("server writev failed: %s\n", strerror(errno));
}

static void fill_attr(struct fuse_attr *a, uint64_t ino, uint32_t mode,
		      uint32_t nlink)
{
	memset(a, 0, sizeof(*a));
	a->ino = ino;
	a->mode = mode;
	a->nlink = nlink;
	a->blksize = 4096;
}

/*
 * Minimal FUSE server.  Advertises FUSE_HAS_SYNCFS in its INIT reply iff
 * @advertise is set.  Signals syncfs_evfd when a FUSE_SYNCFS opcode arrives.
 */
#define SERVER_MAX_WRITE 65536
static void run_server(int fd, int advertise)
{
	/*
	 * The kernel rejects reads (EINVAL) whose buffer is smaller than
	 * max_write + header, so size generously for the max_write we
	 * advertise in the INIT reply below.
	 */
	static char buf[SERVER_MAX_WRITE + 4096];

	for (;;) {
		ssize_t n = read(fd, buf, sizeof(buf));
		struct fuse_in_header *ih = (void *)buf;

		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return;		/* device closed on unmount/abort */
		}
		if (n < (ssize_t)sizeof(*ih))
			continue;

		switch (ih->opcode) {
		case FUSE_INIT: {
			struct fuse_init_in *in = (void *)(ih + 1);
			struct fuse_init_out out = {0};
			uint64_t flags = FUSE_INIT_EXT;

			out.major = FUSE_KERNEL_VERSION;
			out.minor = FUSE_KERNEL_MINOR_VERSION;
			out.max_readahead = in->max_readahead;
			out.max_write = SERVER_MAX_WRITE;
			out.max_background = 16;
			out.congestion_threshold = 12;
			if (advertise)
				flags |= FUSE_HAS_SYNCFS;
			out.flags = flags;
			out.flags2 = flags >> 32;
			reply(fd, ih->unique, 0, &out, sizeof(out));
			break;
		}
		case FUSE_GETATTR: {
			struct fuse_attr_out out = {0};

			fill_attr(&out.attr, FUSE_ROOT_ID, S_IFDIR | 0755, 2);
			reply(fd, ih->unique, 0, &out, sizeof(out));
			break;
		}
		case FUSE_SYNCFS: {
			uint64_t one = 1;

			if (write(syncfs_evfd, &one, sizeof(one)) < 0)
				ksft_print_msg("server eventfd write failed: %s\n",
					       strerror(errno));
			reply(fd, ih->unique, 0, NULL, 0);
			break;
		}
		default:
			/*
			 * Anything else (e.g. OPENDIR from opening the mount
			 * root) is not needed to drive this test; -ENOSYS lets
			 * the kernel proceed.
			 */
			reply(fd, ih->unique, -ENOSYS, NULL, 0);
			break;
		}
	}
}

/*
 * Mount a fuse fs backed by a forked server, issue syncfs(), and report
 * whether the server observed FUSE_SYNCFS.  Returns 0 on success, -1 if the
 * environment could not support the test (caller should skip).
 *
 * If @unpriv_open is set, /dev/fuse is opened with CAP_SYS_ADMIN dropped
 * (regained only for the mount() syscall), so the device opener -- the
 * server principal FUSE_HAS_SYNCFS is gated on -- lacks the privilege even
 * though it remains in the initial user namespace.
 */
static int do_mount_and_syncfs(const char *mnt, int advertise, int unpriv_open,
			       int *seen)
{
	struct pollfd pfd = { .events = POLLIN };
	char opts[256];
	int fd, mfd = -1, i;
	pid_t pid;

	syncfs_evfd = eventfd(0, EFD_CLOEXEC);
	if (syncfs_evfd < 0)
		return -1;

	if (unpriv_open && set_sysadmin(0))
		goto out_evfd;
	fd = open("/dev/fuse", O_RDWR);
	if (unpriv_open && set_sysadmin(1)) {
		if (fd >= 0)
			close(fd);
		goto out_evfd;
	}
	if (fd < 0)
		goto out_evfd;

	mkdir(mnt, 0755);
	snprintf(opts, sizeof(opts),
		 "fd=%d,rootmode=40000,user_id=%d,group_id=%d",
		 fd, getuid(), getgid());

	if (mount("fuse", mnt, "fuse", 0, opts) < 0)
		goto out_fd;

	pid = fork();
	if (pid < 0)
		goto out_umount;
	if (pid == 0) {
		run_server(fd, advertise);
		_exit(0);
	}

	/*
	 * The parent does not service the fuse fd; the child does.  Close our
	 * copy so the kernel sees a single server, and so that if the child
	 * dies the connection aborts instead of hanging us forever.
	 */
	close(fd);

	/*
	 * mount() returns before the server has answered FUSE_INIT, so the
	 * first open() can race and fail with ENOTCONN; retry until the
	 * handshake settles.
	 */
	for (i = 0; i < 1000; i++) {
		mfd = open(mnt, O_RDONLY | O_DIRECTORY);
		if (mfd >= 0)
			break;
		usleep(1000);
	}
	if (mfd >= 0) {
		syncfs(mfd);
		close(mfd);
	}

	/*
	 * No waiting is needed: the server writes syncfs_evfd before it replies
	 * to FUSE_SYNCFS, and that reply is what unblocks the synchronous
	 * syncfs() above.  So once syncfs() has returned, the eventfd is already
	 * signalled if the opcode was propagated, and will never be otherwise.
	 * poll() with a zero timeout therefore decides both cases immediately.
	 */
	pfd.fd = syncfs_evfd;
	*seen = poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN);

	kill(pid, SIGKILL);
	waitpid(pid, NULL, 0);
	umount2(mnt, MNT_DETACH);
	close(syncfs_evfd);
	return 0;

out_umount:
	umount2(mnt, MNT_DETACH);
out_fd:
	close(fd);
out_evfd:
	close(syncfs_evfd);
	return -1;
}

int main(void)
{
	char mnt[] = "/tmp/fuse_syncfs_XXXXXX";
	int seen, ret;

	ksft_print_header();
	ksft_set_plan(3);

	/* Hard watchdog: never let a stuck syncfs hang the test runner. */
	signal(SIGALRM, SIG_DFL);
	alarm(60);

	if (geteuid() != 0)
		ksft_exit_skip("test requires root to mount fuse\n");

	if (!mkdtemp(mnt))
		ksft_exit_fail_msg("mkdtemp failed\n");

	/* T1: host-root mount, server opts in -> syncfs must reach server. */
	ret = do_mount_and_syncfs(mnt, 1, 0, &seen);
	if (ret < 0)
		ksft_test_result_skip("T1: could not mount fuse\n");
	else
		ksft_test_result(seen,
				 "T1 host-root + FUSE_HAS_SYNCFS: server receives FUSE_SYNCFS\n");

	/* T2: host-root mount, server does NOT opt in -> no FUSE_SYNCFS. */
	ret = do_mount_and_syncfs(mnt, 0, 0, &seen);
	if (ret < 0)
		ksft_test_result_skip("T2: could not mount fuse\n");
	else
		ksft_test_result(!seen,
				 "T2 host-root, no opt-in: server does NOT receive FUSE_SYNCFS\n");

	/*
	 * T3: server opts in but opened /dev/fuse without CAP_SYS_ADMIN while
	 * still in the initial user namespace -> kernel must withhold
	 * FUSE_SYNCFS.  This is the case that distinguishes gating on the
	 * server's privilege from gating on the mount's user namespace.
	 */
	ret = do_mount_and_syncfs(mnt, 1, 1, &seen);
	if (ret < 0)
		ksft_test_result_skip("T3: could not mount fuse unprivileged\n");
	else
		ksft_test_result(!seen,
				 "T3 init_userns, opener lacks CAP_SYS_ADMIN: FUSE_SYNCFS withheld\n");

	rmdir(mnt);
	ksft_finished();
}
