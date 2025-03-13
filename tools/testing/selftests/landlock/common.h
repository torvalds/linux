/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Landlock test helpers
 *
 * Copyright © 2017-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2019-2020 ANSSI
 * Copyright © 2021 Microsoft Corporation
 */

#include <arpa/inet.h>
#include <errno.h>
#include <linux/securebits.h>
#include <sys/capability.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../kselftest_harness.h"
#include "wrappers.h"

#define TMP_DIR "tmp"

#ifndef __maybe_unused
#define __maybe_unused __attribute__((__unused__))
#endif

/* TEST_F_FORK() should not be used for new tests. */
#define TEST_F_FORK(fixture_name, test_name) TEST_F(fixture_name, test_name)

static const char bin_sandbox_and_launch[] = "./sandbox-and-launch";
static const char bin_wait_pipe[] = "./wait-pipe";

static void _init_caps(struct __test_metadata *const _metadata, bool drop_all)
{
	cap_t cap_p;
	/* Only these three capabilities are useful for the tests. */
	const cap_value_t caps[] = {
		/* clang-format off */
		CAP_DAC_OVERRIDE,
		CAP_MKNOD,
		CAP_NET_ADMIN,
		CAP_NET_BIND_SERVICE,
		CAP_SYS_ADMIN,
		CAP_SYS_CHROOT,
		/* clang-format on */
	};
	const unsigned int noroot = SECBIT_NOROOT | SECBIT_NOROOT_LOCKED;

	if ((cap_get_secbits() & noroot) != noroot)
		EXPECT_EQ(0, cap_set_secbits(noroot));

	cap_p = cap_get_proc();
	EXPECT_NE(NULL, cap_p);
	EXPECT_NE(-1, cap_clear(cap_p));
	if (!drop_all) {
		EXPECT_NE(-1, cap_set_flag(cap_p, CAP_PERMITTED,
					   ARRAY_SIZE(caps), caps, CAP_SET));
	}

	/* Automatically resets ambient capabilities. */
	EXPECT_NE(-1, cap_set_proc(cap_p))
	{
		TH_LOG("Failed to set capabilities: %s", strerror(errno));
	}
	EXPECT_NE(-1, cap_free(cap_p));

	/* Quickly checks that ambient capabilities are cleared. */
	EXPECT_NE(-1, cap_get_ambient(caps[0]));
}

/* We cannot put such helpers in a library because of kselftest_harness.h . */
static void __maybe_unused disable_caps(struct __test_metadata *const _metadata)
{
	_init_caps(_metadata, false);
}

static void __maybe_unused drop_caps(struct __test_metadata *const _metadata)
{
	_init_caps(_metadata, true);
}

static void _change_cap(struct __test_metadata *const _metadata,
			const cap_flag_t flag, const cap_value_t cap,
			const cap_flag_value_t value)
{
	cap_t cap_p;

	cap_p = cap_get_proc();
	EXPECT_NE(NULL, cap_p);
	EXPECT_NE(-1, cap_set_flag(cap_p, flag, 1, &cap, value));
	EXPECT_NE(-1, cap_set_proc(cap_p))
	{
		TH_LOG("Failed to set capability %d: %s", cap, strerror(errno));
	}
	EXPECT_NE(-1, cap_free(cap_p));
}

static void __maybe_unused set_cap(struct __test_metadata *const _metadata,
				   const cap_value_t cap)
{
	_change_cap(_metadata, CAP_EFFECTIVE, cap, CAP_SET);
}

static void __maybe_unused clear_cap(struct __test_metadata *const _metadata,
				     const cap_value_t cap)
{
	_change_cap(_metadata, CAP_EFFECTIVE, cap, CAP_CLEAR);
}

static void __maybe_unused
set_ambient_cap(struct __test_metadata *const _metadata, const cap_value_t cap)
{
	_change_cap(_metadata, CAP_INHERITABLE, cap, CAP_SET);

	EXPECT_NE(-1, cap_set_ambient(cap, CAP_SET))
	{
		TH_LOG("Failed to set ambient capability %d: %s", cap,
		       strerror(errno));
	}
}

static void __maybe_unused clear_ambient_cap(
	struct __test_metadata *const _metadata, const cap_value_t cap)
{
	EXPECT_EQ(1, cap_get_ambient(cap));
	_change_cap(_metadata, CAP_INHERITABLE, cap, CAP_CLEAR);
	EXPECT_EQ(0, cap_get_ambient(cap));
}

/* Receives an FD from a UNIX socket. Returns the received FD, or -errno. */
static int __maybe_unused recv_fd(int usock)
{
	int fd_rx;
	union {
		/* Aligned ancillary data buffer. */
		char buf[CMSG_SPACE(sizeof(fd_rx))];
		struct cmsghdr _align;
	} cmsg_rx = {};
	char data = '\0';
	struct iovec io = {
		.iov_base = &data,
		.iov_len = sizeof(data),
	};
	struct msghdr msg = {
		.msg_iov = &io,
		.msg_iovlen = 1,
		.msg_control = &cmsg_rx.buf,
		.msg_controllen = sizeof(cmsg_rx.buf),
	};
	struct cmsghdr *cmsg;
	int res;

	res = recvmsg(usock, &msg, MSG_CMSG_CLOEXEC);
	if (res < 0)
		return -errno;

	cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg->cmsg_len != CMSG_LEN(sizeof(fd_rx)))
		return -EIO;

	memcpy(&fd_rx, CMSG_DATA(cmsg), sizeof(fd_rx));
	return fd_rx;
}

/* Sends an FD on a UNIX socket. Returns 0 on success or -errno. */
static int __maybe_unused send_fd(int usock, int fd_tx)
{
	union {
		/* Aligned ancillary data buffer. */
		char buf[CMSG_SPACE(sizeof(fd_tx))];
		struct cmsghdr _align;
	} cmsg_tx = {};
	char data_tx = '.';
	struct iovec io = {
		.iov_base = &data_tx,
		.iov_len = sizeof(data_tx),
	};
	struct msghdr msg = {
		.msg_iov = &io,
		.msg_iovlen = 1,
		.msg_control = &cmsg_tx.buf,
		.msg_controllen = sizeof(cmsg_tx.buf),
	};
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);

	cmsg->cmsg_len = CMSG_LEN(sizeof(fd_tx));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	memcpy(CMSG_DATA(cmsg), &fd_tx, sizeof(fd_tx));

	if (sendmsg(usock, &msg, 0) < 0)
		return -errno;
	return 0;
}

static void __maybe_unused
enforce_ruleset(struct __test_metadata *const _metadata, const int ruleset_fd)
{
	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));
	ASSERT_EQ(0, landlock_restrict_self(ruleset_fd, 0))
	{
		TH_LOG("Failed to enforce ruleset: %s", strerror(errno));
	}
}

struct protocol_variant {
	int domain;
	int type;
	int protocol;
};

struct service_fixture {
	struct protocol_variant protocol;
	/* port is also stored in ipv4_addr.sin_port or ipv6_addr.sin6_port */
	unsigned short port;
	union {
		struct sockaddr_in ipv4_addr;
		struct sockaddr_in6 ipv6_addr;
		struct {
			struct sockaddr_un unix_addr;
			socklen_t unix_addr_len;
		};
	};
};

static void __maybe_unused set_unix_address(struct service_fixture *const srv,
					    const unsigned short index)
{
	srv->unix_addr.sun_family = AF_UNIX;
	sprintf(srv->unix_addr.sun_path,
		"_selftests-landlock-abstract-unix-tid%d-index%d", sys_gettid(),
		index);
	srv->unix_addr_len = SUN_LEN(&srv->unix_addr);
	srv->unix_addr.sun_path[0] = '\0';
}
