// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "kselftest_harness.h"

#ifndef SO_RIGHTS_NOTRUNC
#define SO_RIGHTS_NOTRUNC 85
#endif

#define NR_FILES 2

/* Per-file content, so a received fd can be matched to the file sent */
#define SECRET(n) "secret %d", (n)

/* Indices into the socketpair */
#define SK_SENDER 0
#define SK_RECEIVER 1

FIXTURE(scm_rights_denial_bpf)
{
	struct bpf_object *obj;
	struct bpf_link *link;
	int map_fd;
	int sk[2];
	int files[NR_FILES];
	__u64 inos[NR_FILES];
	char paths[NR_FILES][64];
};

FIXTURE_VARIANT(scm_rights_denial_bpf)
{
	int sock_type;
};

FIXTURE_VARIANT_ADD(scm_rights_denial_bpf, stream)
{
	.sock_type = SOCK_STREAM,
};

FIXTURE_VARIANT_ADD(scm_rights_denial_bpf, dgram)
{
	.sock_type = SOCK_DGRAM,
};

FIXTURE_VARIANT_ADD(scm_rights_denial_bpf, seqpacket)
{
	.sock_type = SOCK_SEQPACKET,
};

FIXTURE_SETUP(scm_rights_denial_bpf)
{
	struct bpf_program *prog;
	char lsms[256] = {};
	int i, fd;

	if (geteuid() != 0)
		SKIP(return, "requires root");

	fd = open("/sys/kernel/security/lsm", O_RDONLY);
	ASSERT_GE(fd, 0);
	ASSERT_LT(0, read(fd, lsms, sizeof(lsms) - 1));
	close(fd);

	if (!strstr(lsms, "bpf"))
		SKIP(return, "BPF LSM not active (boot with lsm=...,bpf)");

	self->obj = bpf_object__open_file("scm_rights_denial_lsm.bpf.o", NULL);
	ASSERT_NE(NULL, self->obj);
	ASSERT_EQ(0, bpf_object__load(self->obj));

	prog = bpf_object__find_program_by_name(self->obj, "scm_rights_deny");
	ASSERT_NE(NULL, prog);

	self->link = bpf_program__attach_lsm(prog);
	ASSERT_NE(NULL, self->link);

	self->map_fd = bpf_object__find_map_fd_by_name(self->obj,
						       "denied_inodes");
	ASSERT_GE(self->map_fd, 0);

	ASSERT_EQ(0, socketpair(AF_UNIX, variant->sock_type, 0, self->sk));

	for (i = 0; i < NR_FILES; i++) {
		struct stat st;

		snprintf(self->paths[i], sizeof(self->paths[i]),
			 "/tmp/scm_rights_denial_bpf.%d.XXXXXX", i);
		self->files[i] = mkstemp(self->paths[i]);
		ASSERT_GE(self->files[i], 0);

		ASSERT_LT(0, dprintf(self->files[i], SECRET(i)));

		ASSERT_EQ(0, fstat(self->files[i], &st));
		self->inos[i] = st.st_ino;
	}
}

FIXTURE_TEARDOWN(scm_rights_denial_bpf)
{
	bpf_link__destroy(self->link);
	bpf_object__close(self->obj);

	for (int i = 0; i < NR_FILES; i++) {
		if (self->files[i] >= 0) {
			close(self->files[i]);
			unlink(self->paths[i]);
		}
	}

	close(self->sk[SK_SENDER]);
	close(self->sk[SK_RECEIVER]);
}

static int deny_inode(int map_fd, __u64 ino)
{
	__u32 tgid = getpid();

	return bpf_map_update_elem(map_fd, &ino, &tgid, BPF_ANY);
}

static int set_notrunc(int sk)
{
	int one = 1;

	return setsockopt(sk, SOL_SOCKET, SO_RIGHTS_NOTRUNC,
			  &one, sizeof(one));
}

static int send_fds(int sk, int *fds, int n)
{
	char ctrl[CMSG_SPACE(NR_FILES * sizeof(int))] = {};
	char data = 'x';
	struct iovec iov = {
		.iov_base = &data,
		.iov_len = sizeof(data),
	};
	struct msghdr msg = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = ctrl,
		.msg_controllen = CMSG_SPACE(n * sizeof(int)),
	};
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	int ret;

	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(n * sizeof(int));
	memcpy(CMSG_DATA(cmsg), fds, n * sizeof(int));

	ret = sendmsg(sk, &msg, 0);
	if (ret != 1)
		return -1;

	return 0;
}

static int recv_fd_slots(int sk, int *slots, int *msg_flags)
{
	int nr_slots;
	char ctrl[CMSG_SPACE(NR_FILES * sizeof(int))];
	char data;
	struct iovec iov = {
		.iov_base = &data,
		.iov_len = sizeof(data),
	};
	struct msghdr msg = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = ctrl,
		.msg_controllen = sizeof(ctrl),
	};
	struct cmsghdr *cmsg;

	if (recvmsg(sk, &msg, 0) < 0)
		return -1;

	*msg_flags = msg.msg_flags;

	cmsg = CMSG_FIRSTHDR(&msg);
	if (!cmsg)
		return 0;

	nr_slots = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
	memcpy(slots, CMSG_DATA(cmsg), nr_slots * sizeof(int));

	return nr_slots;
}

/* Prove a received fd works by reading back the file's content. */
static int check_secret(int fd, int idx)
{
	char want[32], got[32] = {};

	snprintf(want, sizeof(want), SECRET(idx));
	if (pread(fd, got, sizeof(got) - 1, 0) < 0)
		return -1;

	return strcmp(want, got);
}

TEST_F(scm_rights_denial_bpf, all_allowed)
{
	int slots[NR_FILES], nr_slots, flags;

	ASSERT_EQ(0, set_notrunc(self->sk[SK_RECEIVER]));
	ASSERT_EQ(0, send_fds(self->sk[SK_SENDER], self->files, NR_FILES));
	nr_slots = recv_fd_slots(self->sk[SK_RECEIVER], slots, &flags);

	ASSERT_EQ(NR_FILES, nr_slots);
	EXPECT_EQ(0, flags & MSG_CTRUNC);

	for (int i = 0; i < nr_slots; i++) {
		ASSERT_GE(slots[i], 0);
		EXPECT_EQ(0, check_secret(slots[i], i));
		close(slots[i]);
	}
}

TEST_F(scm_rights_denial_bpf, first_denied)
{
	int slots[NR_FILES], nr_slots, flags;

	ASSERT_EQ(0, deny_inode(self->map_fd, self->inos[0]));

	ASSERT_EQ(0, set_notrunc(self->sk[SK_RECEIVER]));
	ASSERT_EQ(0, send_fds(self->sk[SK_SENDER], self->files, NR_FILES));
	nr_slots = recv_fd_slots(self->sk[SK_RECEIVER], slots, &flags);

	ASSERT_EQ(NR_FILES, nr_slots);
	EXPECT_EQ(0, flags & MSG_CTRUNC);
	
	EXPECT_EQ(-EPERM, slots[0]);
	for (int i = 1; i < nr_slots; i++) {
		ASSERT_GE(slots[i], 0);
		EXPECT_EQ(0, check_secret(slots[i], i));
		close(slots[i]);
	}
}

TEST_F(scm_rights_denial_bpf, all_denied)
{
	int slots[NR_FILES], nr_slots, flags, i;

	for (i = 0; i < NR_FILES; i++)
		ASSERT_EQ(0, deny_inode(self->map_fd, self->inos[i]));

	ASSERT_EQ(0, set_notrunc(self->sk[SK_RECEIVER]));
	ASSERT_EQ(0, send_fds(self->sk[SK_SENDER], self->files, NR_FILES));
	nr_slots = recv_fd_slots(self->sk[SK_RECEIVER], slots, &flags);

	ASSERT_EQ(NR_FILES, nr_slots);
	EXPECT_EQ(0, flags & MSG_CTRUNC);

	for (i = 0; i < nr_slots; i++)
		EXPECT_EQ(-EPERM, slots[i]);
}

TEST_F(scm_rights_denial_bpf, denied_without_notrunc)
{
	int slots[NR_FILES], nr_slots, flags;

	/*
	 * Baseline behaviour without SO_RIGHTS_NOTRUNC: the fd array is
	 * truncated at the first denied fd and MSG_CTRUNC is set.
	 */
	ASSERT_EQ(0, deny_inode(self->map_fd, self->inos[1]));

	ASSERT_EQ(0, send_fds(self->sk[SK_SENDER], self->files, NR_FILES));
	nr_slots = recv_fd_slots(self->sk[SK_RECEIVER], slots, &flags);

	ASSERT_EQ(1, nr_slots);
	EXPECT_NE(0, flags & MSG_CTRUNC);

	ASSERT_GE(slots[0], 0);
	EXPECT_EQ(0, check_secret(slots[0], 0));
	close(slots[0]);
}

TEST_HARNESS_MAIN
