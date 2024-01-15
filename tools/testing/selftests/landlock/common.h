/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Landlock test helpers
 *
 * Copyright © 2017-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2019-2020 ANSSI
 * Copyright © 2021 Microsoft Corporation
 */

#include <errno.h>
#include <linux/landlock.h>
#include <sys/capability.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../kselftest_harness.h"

#ifndef __maybe_unused
#define __maybe_unused __attribute__((__unused__))
#endif

/*
 * TEST_F_FORK() is useful when a test drop privileges but the corresponding
 * FIXTURE_TEARDOWN() requires them (e.g. to remove files from a directory
 * where write actions are denied).  For convenience, FIXTURE_TEARDOWN() is
 * also called when the test failed, but not when FIXTURE_SETUP() failed.  For
 * this to be possible, we must not call abort() but instead exit smoothly
 * (hence the step print).
 */
/* clang-format off */
#define TEST_F_FORK(fixture_name, test_name) \
	static void fixture_name##_##test_name##_child( \
		struct __test_metadata *_metadata, \
		FIXTURE_DATA(fixture_name) *self, \
		const FIXTURE_VARIANT(fixture_name) *variant); \
	TEST_F(fixture_name, test_name) \
	{ \
		int status; \
		const pid_t child = fork(); \
		if (child < 0) \
			abort(); \
		if (child == 0) { \
			_metadata->no_print = 1; \
			fixture_name##_##test_name##_child(_metadata, self, variant); \
			if (_metadata->skip) \
				_exit(255); \
			if (_metadata->passed) \
				_exit(0); \
			_exit(_metadata->step); \
		} \
		if (child != waitpid(child, &status, 0)) \
			abort(); \
		if (WIFSIGNALED(status) || !WIFEXITED(status)) { \
			_metadata->passed = 0; \
			_metadata->step = 1; \
			return; \
		} \
		switch (WEXITSTATUS(status)) { \
		case 0: \
			_metadata->passed = 1; \
			break; \
		case 255: \
			_metadata->passed = 1; \
			_metadata->skip = 1; \
			break; \
		default: \
			_metadata->passed = 0; \
			_metadata->step = WEXITSTATUS(status); \
			break; \
		} \
	} \
	static void fixture_name##_##test_name##_child( \
		struct __test_metadata __attribute__((unused)) *_metadata, \
		FIXTURE_DATA(fixture_name) __attribute__((unused)) *self, \
		const FIXTURE_VARIANT(fixture_name) \
			__attribute__((unused)) *variant)
/* clang-format on */

#ifndef landlock_create_ruleset
static inline int
landlock_create_ruleset(const struct landlock_ruleset_attr *const attr,
			const size_t size, const __u32 flags)
{
	return syscall(__NR_landlock_create_ruleset, attr, size, flags);
}
#endif

#ifndef landlock_add_rule
static inline int landlock_add_rule(const int ruleset_fd,
				    const enum landlock_rule_type rule_type,
				    const void *const rule_attr,
				    const __u32 flags)
{
	return syscall(__NR_landlock_add_rule, ruleset_fd, rule_type, rule_attr,
		       flags);
}
#endif

#ifndef landlock_restrict_self
static inline int landlock_restrict_self(const int ruleset_fd,
					 const __u32 flags)
{
	return syscall(__NR_landlock_restrict_self, ruleset_fd, flags);
}
#endif

static void _init_caps(struct __test_metadata *const _metadata, bool drop_all)
{
	cap_t cap_p;
	/* Only these three capabilities are useful for the tests. */
	const cap_value_t caps[] = {
		/* clang-format off */
		CAP_DAC_OVERRIDE,
		CAP_MKNOD,
		CAP_SYS_ADMIN,
		CAP_SYS_CHROOT,
		CAP_NET_BIND_SERVICE,
		/* clang-format on */
	};

	cap_p = cap_get_proc();
	EXPECT_NE(NULL, cap_p)
	{
		TH_LOG("Failed to cap_get_proc: %s", strerror(errno));
	}
	EXPECT_NE(-1, cap_clear(cap_p))
	{
		TH_LOG("Failed to cap_clear: %s", strerror(errno));
	}
	if (!drop_all) {
		EXPECT_NE(-1, cap_set_flag(cap_p, CAP_PERMITTED,
					   ARRAY_SIZE(caps), caps, CAP_SET))
		{
			TH_LOG("Failed to cap_set_flag: %s", strerror(errno));
		}
	}
	EXPECT_NE(-1, cap_set_proc(cap_p))
	{
		TH_LOG("Failed to cap_set_proc: %s", strerror(errno));
	}
	EXPECT_NE(-1, cap_free(cap_p))
	{
		TH_LOG("Failed to cap_free: %s", strerror(errno));
	}
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

static void _effective_cap(struct __test_metadata *const _metadata,
			   const cap_value_t caps, const cap_flag_value_t value)
{
	cap_t cap_p;

	cap_p = cap_get_proc();
	EXPECT_NE(NULL, cap_p)
	{
		TH_LOG("Failed to cap_get_proc: %s", strerror(errno));
	}
	EXPECT_NE(-1, cap_set_flag(cap_p, CAP_EFFECTIVE, 1, &caps, value))
	{
		TH_LOG("Failed to cap_set_flag: %s", strerror(errno));
	}
	EXPECT_NE(-1, cap_set_proc(cap_p))
	{
		TH_LOG("Failed to cap_set_proc: %s", strerror(errno));
	}
	EXPECT_NE(-1, cap_free(cap_p))
	{
		TH_LOG("Failed to cap_free: %s", strerror(errno));
	}
}

static void __maybe_unused set_cap(struct __test_metadata *const _metadata,
				   const cap_value_t caps)
{
	_effective_cap(_metadata, caps, CAP_SET);
}

static void __maybe_unused clear_cap(struct __test_metadata *const _metadata,
				     const cap_value_t caps)
{
	_effective_cap(_metadata, caps, CAP_CLEAR);
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
