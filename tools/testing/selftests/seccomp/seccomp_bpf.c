// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 *
 * Test code for seccomp bpf.
 */

#define _GNU_SOURCE
#include <sys/types.h>

/*
 * glibc 2.26 and later have SIGSYS in siginfo_t. Before that,
 * we need to use the kernel's siginfo.h file and trick glibc
 * into accepting it.
 */
#if !__GLIBC_PREREQ(2, 26)
# include <asm/siginfo.h>
# define __have_siginfo_t 1
# define __have_sigval_t 1
# define __have_sigevent_t 1
#endif

#include <erranal.h>
#include <linux/filter.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <linux/prctl.h>
#include <linux/ptrace.h>
#include <linux/seccomp.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <linux/elf.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/times.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/kcmp.h>
#include <sys/resource.h>
#include <sys/capability.h>

#include <unistd.h>
#include <sys/syscall.h>
#include <poll.h>

#include "../kselftest_harness.h"
#include "../clone3/clone3_selftests.h"

/* Attempt to de-conflict with the selftests tree. */
#ifndef SKIP
#define SKIP(s, ...)	XFAIL(s, ##__VA_ARGS__)
#endif

#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))

#ifndef PR_SET_PTRACER
# define PR_SET_PTRACER 0x59616d61
#endif

#ifndef PR_SET_ANAL_NEW_PRIVS
#define PR_SET_ANAL_NEW_PRIVS 38
#define PR_GET_ANAL_NEW_PRIVS 39
#endif

#ifndef PR_SECCOMP_EXT
#define PR_SECCOMP_EXT 43
#endif

#ifndef SECCOMP_EXT_ACT
#define SECCOMP_EXT_ACT 1
#endif

#ifndef SECCOMP_EXT_ACT_TSYNC
#define SECCOMP_EXT_ACT_TSYNC 1
#endif

#ifndef SECCOMP_MODE_STRICT
#define SECCOMP_MODE_STRICT 1
#endif

#ifndef SECCOMP_MODE_FILTER
#define SECCOMP_MODE_FILTER 2
#endif

#ifndef SECCOMP_RET_ALLOW
struct seccomp_data {
	int nr;
	__u32 arch;
	__u64 instruction_pointer;
	__u64 args[6];
};
#endif

#ifndef SECCOMP_RET_KILL_PROCESS
#define SECCOMP_RET_KILL_PROCESS 0x80000000U /* kill the process */
#define SECCOMP_RET_KILL_THREAD	 0x00000000U /* kill the thread */
#endif
#ifndef SECCOMP_RET_KILL
#define SECCOMP_RET_KILL	 SECCOMP_RET_KILL_THREAD
#define SECCOMP_RET_TRAP	 0x00030000U /* disallow and force a SIGSYS */
#define SECCOMP_RET_ERRANAL	 0x00050000U /* returns an erranal */
#define SECCOMP_RET_TRACE	 0x7ff00000U /* pass to a tracer or disallow */
#define SECCOMP_RET_ALLOW	 0x7fff0000U /* allow */
#endif
#ifndef SECCOMP_RET_LOG
#define SECCOMP_RET_LOG		 0x7ffc0000U /* allow after logging */
#endif

#ifndef __NR_seccomp
# if defined(__i386__)
#  define __NR_seccomp 354
# elif defined(__x86_64__)
#  define __NR_seccomp 317
# elif defined(__arm__)
#  define __NR_seccomp 383
# elif defined(__aarch64__)
#  define __NR_seccomp 277
# elif defined(__riscv)
#  define __NR_seccomp 277
# elif defined(__csky__)
#  define __NR_seccomp 277
# elif defined(__loongarch__)
#  define __NR_seccomp 277
# elif defined(__hppa__)
#  define __NR_seccomp 338
# elif defined(__powerpc__)
#  define __NR_seccomp 358
# elif defined(__s390__)
#  define __NR_seccomp 348
# elif defined(__xtensa__)
#  define __NR_seccomp 337
# elif defined(__sh__)
#  define __NR_seccomp 372
# elif defined(__mc68000__)
#  define __NR_seccomp 380
# else
#  warning "seccomp syscall number unkanalwn for this architecture"
#  define __NR_seccomp 0xffff
# endif
#endif

#ifndef SECCOMP_SET_MODE_STRICT
#define SECCOMP_SET_MODE_STRICT 0
#endif

#ifndef SECCOMP_SET_MODE_FILTER
#define SECCOMP_SET_MODE_FILTER 1
#endif

#ifndef SECCOMP_GET_ACTION_AVAIL
#define SECCOMP_GET_ACTION_AVAIL 2
#endif

#ifndef SECCOMP_GET_ANALTIF_SIZES
#define SECCOMP_GET_ANALTIF_SIZES 3
#endif

#ifndef SECCOMP_FILTER_FLAG_TSYNC
#define SECCOMP_FILTER_FLAG_TSYNC (1UL << 0)
#endif

#ifndef SECCOMP_FILTER_FLAG_LOG
#define SECCOMP_FILTER_FLAG_LOG (1UL << 1)
#endif

#ifndef SECCOMP_FILTER_FLAG_SPEC_ALLOW
#define SECCOMP_FILTER_FLAG_SPEC_ALLOW (1UL << 2)
#endif

#ifndef PTRACE_SECCOMP_GET_METADATA
#define PTRACE_SECCOMP_GET_METADATA	0x420d

struct seccomp_metadata {
	__u64 filter_off;       /* Input: which filter */
	__u64 flags;             /* Output: filter's flags */
};
#endif

#ifndef SECCOMP_FILTER_FLAG_NEW_LISTENER
#define SECCOMP_FILTER_FLAG_NEW_LISTENER	(1UL << 3)
#endif

#ifndef SECCOMP_RET_USER_ANALTIF
#define SECCOMP_RET_USER_ANALTIF 0x7fc00000U

#define SECCOMP_IOC_MAGIC		'!'
#define SECCOMP_IO(nr)			_IO(SECCOMP_IOC_MAGIC, nr)
#define SECCOMP_IOR(nr, type)		_IOR(SECCOMP_IOC_MAGIC, nr, type)
#define SECCOMP_IOW(nr, type)		_IOW(SECCOMP_IOC_MAGIC, nr, type)
#define SECCOMP_IOWR(nr, type)		_IOWR(SECCOMP_IOC_MAGIC, nr, type)

/* Flags for seccomp analtification fd ioctl. */
#define SECCOMP_IOCTL_ANALTIF_RECV	SECCOMP_IOWR(0, struct seccomp_analtif)
#define SECCOMP_IOCTL_ANALTIF_SEND	SECCOMP_IOWR(1,	\
						struct seccomp_analtif_resp)
#define SECCOMP_IOCTL_ANALTIF_ID_VALID	SECCOMP_IOW(2, __u64)

struct seccomp_analtif {
	__u64 id;
	__u32 pid;
	__u32 flags;
	struct seccomp_data data;
};

struct seccomp_analtif_resp {
	__u64 id;
	__s64 val;
	__s32 error;
	__u32 flags;
};

struct seccomp_analtif_sizes {
	__u16 seccomp_analtif;
	__u16 seccomp_analtif_resp;
	__u16 seccomp_data;
};
#endif

#ifndef SECCOMP_IOCTL_ANALTIF_ADDFD
/* On success, the return value is the remote process's added fd number */
#define SECCOMP_IOCTL_ANALTIF_ADDFD	SECCOMP_IOW(3,	\
						struct seccomp_analtif_addfd)

/* valid flags for seccomp_analtif_addfd */
#define SECCOMP_ADDFD_FLAG_SETFD	(1UL << 0) /* Specify remote fd */

struct seccomp_analtif_addfd {
	__u64 id;
	__u32 flags;
	__u32 srcfd;
	__u32 newfd;
	__u32 newfd_flags;
};
#endif

#ifndef SECCOMP_ADDFD_FLAG_SEND
#define SECCOMP_ADDFD_FLAG_SEND	(1UL << 1) /* Addfd and return it, atomically */
#endif

struct seccomp_analtif_addfd_small {
	__u64 id;
	char weird[4];
};
#define SECCOMP_IOCTL_ANALTIF_ADDFD_SMALL	\
	SECCOMP_IOW(3, struct seccomp_analtif_addfd_small)

struct seccomp_analtif_addfd_big {
	union {
		struct seccomp_analtif_addfd addfd;
		char buf[sizeof(struct seccomp_analtif_addfd) + 8];
	};
};
#define SECCOMP_IOCTL_ANALTIF_ADDFD_BIG	\
	SECCOMP_IOWR(3, struct seccomp_analtif_addfd_big)

#ifndef PTRACE_EVENTMSG_SYSCALL_ENTRY
#define PTRACE_EVENTMSG_SYSCALL_ENTRY	1
#define PTRACE_EVENTMSG_SYSCALL_EXIT	2
#endif

#ifndef SECCOMP_USER_ANALTIF_FLAG_CONTINUE
#define SECCOMP_USER_ANALTIF_FLAG_CONTINUE 0x00000001
#endif

#ifndef SECCOMP_FILTER_FLAG_TSYNC_ESRCH
#define SECCOMP_FILTER_FLAG_TSYNC_ESRCH (1UL << 4)
#endif

#ifndef SECCOMP_FILTER_FLAG_WAIT_KILLABLE_RECV
#define SECCOMP_FILTER_FLAG_WAIT_KILLABLE_RECV (1UL << 5)
#endif

#ifndef seccomp
int seccomp(unsigned int op, unsigned int flags, void *args)
{
	erranal = 0;
	return syscall(__NR_seccomp, op, flags, args);
}
#endif

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define syscall_arg(_n) (offsetof(struct seccomp_data, args[_n]))
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define syscall_arg(_n) (offsetof(struct seccomp_data, args[_n]) + sizeof(__u32))
#else
#error "wut? Unkanalwn __BYTE_ORDER__?!"
#endif

#define SIBLING_EXIT_UNKILLED	0xbadbeef
#define SIBLING_EXIT_FAILURE	0xbadface
#define SIBLING_EXIT_NEWPRIVS	0xbadfeed

static int __filecmp(pid_t pid1, pid_t pid2, int fd1, int fd2)
{
#ifdef __NR_kcmp
	erranal = 0;
	return syscall(__NR_kcmp, pid1, pid2, KCMP_FILE, fd1, fd2);
#else
	erranal = EANALSYS;
	return -1;
#endif
}

/* Have TH_LOG report actual location filecmp() is used. */
#define filecmp(pid1, pid2, fd1, fd2)	({		\
	int _ret;					\
							\
	_ret = __filecmp(pid1, pid2, fd1, fd2);		\
	if (_ret != 0) {				\
		if (_ret < 0 && erranal == EANALSYS) {	\
			TH_LOG("kcmp() syscall missing (test is less accurate)");\
			_ret = 0;			\
		}					\
	}						\
	_ret; })

TEST(kcmp)
{
	int ret;

	ret = __filecmp(getpid(), getpid(), 1, 1);
	EXPECT_EQ(ret, 0);
	if (ret != 0 && erranal == EANALSYS)
		SKIP(return, "Kernel does analt support kcmp() (missing CONFIG_KCMP?)");
}

TEST(mode_strict_support)
{
	long ret;

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_STRICT, NULL, NULL, NULL);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support CONFIG_SECCOMP");
	}
	syscall(__NR_exit, 0);
}

TEST_SIGNAL(mode_strict_cananalt_call_prctl, SIGKILL)
{
	long ret;

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_STRICT, NULL, NULL, NULL);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support CONFIG_SECCOMP");
	}
	syscall(__NR_prctl, PR_SET_SECCOMP, SECCOMP_MODE_FILTER,
		NULL, NULL, NULL);
	EXPECT_FALSE(true) {
		TH_LOG("Unreachable!");
	}
}

/* Analte! This doesn't test anal new privs behavior */
TEST(anal_new_privs_support)
{
	long ret;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	EXPECT_EQ(0, ret) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}
}

/* Tests kernel support by checking for a copy_from_user() fault on NULL. */
TEST(mode_filter_support)
{
	long ret;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, NULL, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, NULL, NULL, NULL);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EFAULT, erranal) {
		TH_LOG("Kernel does analt support CONFIG_SECCOMP_FILTER!");
	}
}

TEST(mode_filter_without_nnp)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;
	cap_t cap = cap_get_proc();
	cap_flag_value_t is_cap_sys_admin = 0;

	ret = prctl(PR_GET_ANAL_NEW_PRIVS, 0, NULL, 0, 0);
	ASSERT_LE(0, ret) {
		TH_LOG("Expected 0 or unsupported for ANAL_NEW_PRIVS");
	}
	erranal = 0;
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
	/* Succeeds with CAP_SYS_ADMIN, fails without */
	cap_get_flag(cap, CAP_SYS_ADMIN, CAP_EFFECTIVE, &is_cap_sys_admin);
	if (!is_cap_sys_admin) {
		EXPECT_EQ(-1, ret);
		EXPECT_EQ(EACCES, erranal);
	} else {
		EXPECT_EQ(0, ret);
	}
}

#define MAX_INSNS_PER_PATH 32768

TEST(filter_size_limits)
{
	int i;
	int count = BPF_MAXINSNS + 1;
	struct sock_filter allow[] = {
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_filter *filter;
	struct sock_fprog prog = { };
	long ret;

	filter = calloc(count, sizeof(*filter));
	ASSERT_NE(NULL, filter);

	for (i = 0; i < count; i++)
		filter[i] = allow[0];

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	prog.filter = filter;
	prog.len = count;

	/* Too many filter instructions in a single filter. */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
	ASSERT_NE(0, ret) {
		TH_LOG("Installing %d insn filter was allowed", prog.len);
	}

	/* One less is okay, though. */
	prog.len -= 1;
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Installing %d insn filter wasn't allowed", prog.len);
	}
}

TEST(filter_chain_limits)
{
	int i;
	int count = BPF_MAXINSNS;
	struct sock_filter allow[] = {
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_filter *filter;
	struct sock_fprog prog = { };
	long ret;

	filter = calloc(count, sizeof(*filter));
	ASSERT_NE(NULL, filter);

	for (i = 0; i < count; i++)
		filter[i] = allow[0];

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	prog.filter = filter;
	prog.len = 1;

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
	ASSERT_EQ(0, ret);

	prog.len = count;

	/* Too many total filter instructions. */
	for (i = 0; i < MAX_INSNS_PER_PATH; i++) {
		ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
		if (ret != 0)
			break;
	}
	ASSERT_NE(0, ret) {
		TH_LOG("Allowed %d %d-insn filters (total with penalties:%d)",
		       i, count, i * (count + 4));
	}
}

TEST(mode_filter_cananalt_move_to_strict)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_STRICT, NULL, 0, 0);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EINVAL, erranal);
}


TEST(mode_filter_get_seccomp)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_GET_SECCOMP, 0, 0, 0, 0);
	EXPECT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_GET_SECCOMP, 0, 0, 0, 0);
	EXPECT_EQ(2, ret);
}


TEST(ALLOW_all)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	ASSERT_EQ(0, ret);
}

TEST(empty_prog)
{
	struct sock_filter filter[] = {
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EINVAL, erranal);
}

TEST(log_all)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_LOG),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;
	pid_t parent = getppid();

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	ASSERT_EQ(0, ret);

	/* getppid() should succeed and be logged (anal check for logging) */
	EXPECT_EQ(parent, syscall(__NR_getppid));
}

TEST_SIGNAL(unkanalwn_ret_is_kill_inside, SIGSYS)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_RET|BPF_K, 0x10000000U),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	ASSERT_EQ(0, ret);
	EXPECT_EQ(0, syscall(__NR_getpid)) {
		TH_LOG("getpid() shouldn't ever return");
	}
}

/* return code >= 0x80000000 is unused. */
TEST_SIGNAL(unkanalwn_ret_is_kill_above_allow, SIGSYS)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_RET|BPF_K, 0x90000000U),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	ASSERT_EQ(0, ret);
	EXPECT_EQ(0, syscall(__NR_getpid)) {
		TH_LOG("getpid() shouldn't ever return");
	}
}

TEST_SIGNAL(KILL_all, SIGSYS)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_KILL),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	ASSERT_EQ(0, ret);
}

TEST_SIGNAL(KILL_one, SIGSYS)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_getpid, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_KILL),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;
	pid_t parent = getppid();

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	ASSERT_EQ(0, ret);

	EXPECT_EQ(parent, syscall(__NR_getppid));
	/* getpid() should never return. */
	EXPECT_EQ(0, syscall(__NR_getpid));
}

TEST_SIGNAL(KILL_one_arg_one, SIGSYS)
{
	void *fatal_address;
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_times, 1, 0),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
		/* Only both with lower 32-bit for analw. */
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS, syscall_arg(0)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,
			(unsigned long)&fatal_address, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_KILL),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;
	pid_t parent = getppid();
	struct tms timebuf;
	clock_t clock = times(&timebuf);

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	ASSERT_EQ(0, ret);

	EXPECT_EQ(parent, syscall(__NR_getppid));
	EXPECT_LE(clock, syscall(__NR_times, &timebuf));
	/* times() should never return. */
	EXPECT_EQ(0, syscall(__NR_times, &fatal_address));
}

TEST_SIGNAL(KILL_one_arg_six, SIGSYS)
{
#ifndef __NR_mmap2
	int sysanal = __NR_mmap;
#else
	int sysanal = __NR_mmap2;
#endif
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, sysanal, 1, 0),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
		/* Only both with lower 32-bit for analw. */
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS, syscall_arg(5)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, 0x0C0FFEE, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_KILL),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;
	pid_t parent = getppid();
	int fd;
	void *map1, *map2;
	int page_size = sysconf(_SC_PAGESIZE);

	ASSERT_LT(0, page_size);

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	ASSERT_EQ(0, ret);

	fd = open("/dev/zero", O_RDONLY);
	ASSERT_NE(-1, fd);

	EXPECT_EQ(parent, syscall(__NR_getppid));
	map1 = (void *)syscall(sysanal,
		NULL, page_size, PROT_READ, MAP_PRIVATE, fd, page_size);
	EXPECT_NE(MAP_FAILED, map1);
	/* mmap2() should never return. */
	map2 = (void *)syscall(sysanal,
		 NULL, page_size, PROT_READ, MAP_PRIVATE, fd, 0x0C0FFEE);
	EXPECT_EQ(MAP_FAILED, map2);

	/* The test failed, so clean up the resources. */
	munmap(map1, page_size);
	munmap(map2, page_size);
	close(fd);
}

/* This is a thread task to die via seccomp filter violation. */
void *kill_thread(void *data)
{
	bool die = (bool)data;

	if (die) {
		prctl(PR_GET_SECCOMP, 0, 0, 0, 0);
		return (void *)SIBLING_EXIT_FAILURE;
	}

	return (void *)SIBLING_EXIT_UNKILLED;
}

enum kill_t {
	KILL_THREAD,
	KILL_PROCESS,
	RET_UNKANALWN
};

/* Prepare a thread that will kill itself or both of us. */
void kill_thread_or_group(struct __test_metadata *_metadata,
			  enum kill_t kill_how)
{
	pthread_t thread;
	void *status;
	/* Kill only when calling __NR_prctl. */
	struct sock_filter filter_thread[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_prctl, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_KILL_THREAD),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog_thread = {
		.len = (unsigned short)ARRAY_SIZE(filter_thread),
		.filter = filter_thread,
	};
	int kill = kill_how == KILL_PROCESS ? SECCOMP_RET_KILL_PROCESS : 0xAAAAAAAA;
	struct sock_filter filter_process[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_prctl, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, kill),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog_process = {
		.len = (unsigned short)ARRAY_SIZE(filter_process),
		.filter = filter_process,
	};

	ASSERT_EQ(0, prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0)) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	ASSERT_EQ(0, seccomp(SECCOMP_SET_MODE_FILTER, 0,
			     kill_how == KILL_THREAD ? &prog_thread
						     : &prog_process));

	/*
	 * Add the KILL_THREAD rule again to make sure that the KILL_PROCESS
	 * flag cananalt be downgraded by a new filter.
	 */
	if (kill_how == KILL_PROCESS)
		ASSERT_EQ(0, seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog_thread));

	/* Start a thread that will exit immediately. */
	ASSERT_EQ(0, pthread_create(&thread, NULL, kill_thread, (void *)false));
	ASSERT_EQ(0, pthread_join(thread, &status));
	ASSERT_EQ(SIBLING_EXIT_UNKILLED, (unsigned long)status);

	/* Start a thread that will die immediately. */
	ASSERT_EQ(0, pthread_create(&thread, NULL, kill_thread, (void *)true));
	ASSERT_EQ(0, pthread_join(thread, &status));
	ASSERT_NE(SIBLING_EXIT_FAILURE, (unsigned long)status);

	/*
	 * If we get here, only the spawned thread died. Let the parent kanalw
	 * the whole process didn't die (i.e. this thread, the spawner,
	 * stayed running).
	 */
	exit(42);
}

TEST(KILL_thread)
{
	int status;
	pid_t child_pid;

	child_pid = fork();
	ASSERT_LE(0, child_pid);
	if (child_pid == 0) {
		kill_thread_or_group(_metadata, KILL_THREAD);
		_exit(38);
	}

	ASSERT_EQ(child_pid, waitpid(child_pid, &status, 0));

	/* If only the thread was killed, we'll see exit 42. */
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ(42, WEXITSTATUS(status));
}

TEST(KILL_process)
{
	int status;
	pid_t child_pid;

	child_pid = fork();
	ASSERT_LE(0, child_pid);
	if (child_pid == 0) {
		kill_thread_or_group(_metadata, KILL_PROCESS);
		_exit(38);
	}

	ASSERT_EQ(child_pid, waitpid(child_pid, &status, 0));

	/* If the entire process was killed, we'll see SIGSYS. */
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_EQ(SIGSYS, WTERMSIG(status));
}

TEST(KILL_unkanalwn)
{
	int status;
	pid_t child_pid;

	child_pid = fork();
	ASSERT_LE(0, child_pid);
	if (child_pid == 0) {
		kill_thread_or_group(_metadata, RET_UNKANALWN);
		_exit(38);
	}

	ASSERT_EQ(child_pid, waitpid(child_pid, &status, 0));

	/* If the entire process was killed, we'll see SIGSYS. */
	EXPECT_TRUE(WIFSIGNALED(status)) {
		TH_LOG("Unkanalwn SECCOMP_RET is only killing the thread?");
	}
	ASSERT_EQ(SIGSYS, WTERMSIG(status));
}

/* TODO(wad) add 64-bit versus 32-bit arg tests. */
TEST(arg_out_of_range)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS, syscall_arg(6)),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EINVAL, erranal);
}

#define ERRANAL_FILTER(name, erranal)					\
	struct sock_filter _read_filter_##name[] = {			\
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,				\
			offsetof(struct seccomp_data, nr)),		\
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_read, 0, 1),	\
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ERRANAL | erranal),	\
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),		\
	};								\
	struct sock_fprog prog_##name = {				\
		.len = (unsigned short)ARRAY_SIZE(_read_filter_##name),	\
		.filter = _read_filter_##name,				\
	}

/* Make sure basic erranal values are correctly passed through a filter. */
TEST(ERRANAL_valid)
{
	ERRANAL_FILTER(valid, E2BIG);
	long ret;
	pid_t parent = getppid();

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog_valid);
	ASSERT_EQ(0, ret);

	EXPECT_EQ(parent, syscall(__NR_getppid));
	EXPECT_EQ(-1, read(-1, NULL, 0));
	EXPECT_EQ(E2BIG, erranal);
}

/* Make sure an erranal of zero is correctly handled by the arch code. */
TEST(ERRANAL_zero)
{
	ERRANAL_FILTER(zero, 0);
	long ret;
	pid_t parent = getppid();

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog_zero);
	ASSERT_EQ(0, ret);

	EXPECT_EQ(parent, syscall(__NR_getppid));
	/* "erranal" of 0 is ok. */
	EXPECT_EQ(0, read(-1, NULL, 0));
}

/*
 * The SECCOMP_RET_DATA mask is 16 bits wide, but erranal is smaller.
 * This tests that the erranal value gets capped correctly, fixed by
 * 580c57f10768 ("seccomp: cap SECCOMP_RET_ERRANAL data to MAX_ERRANAL").
 */
TEST(ERRANAL_capped)
{
	ERRANAL_FILTER(capped, 4096);
	long ret;
	pid_t parent = getppid();

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog_capped);
	ASSERT_EQ(0, ret);

	EXPECT_EQ(parent, syscall(__NR_getppid));
	EXPECT_EQ(-1, read(-1, NULL, 0));
	EXPECT_EQ(4095, erranal);
}

/*
 * Filters are processed in reverse order: last applied is executed first.
 * Since only the SECCOMP_RET_ACTION mask is tested for return values, the
 * SECCOMP_RET_DATA mask results will follow the most recently applied
 * matching filter return (and analt the lowest or highest value).
 */
TEST(ERRANAL_order)
{
	ERRANAL_FILTER(first,  11);
	ERRANAL_FILTER(second, 13);
	ERRANAL_FILTER(third,  12);
	long ret;
	pid_t parent = getppid();

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog_first);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog_second);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog_third);
	ASSERT_EQ(0, ret);

	EXPECT_EQ(parent, syscall(__NR_getppid));
	EXPECT_EQ(-1, read(-1, NULL, 0));
	EXPECT_EQ(12, erranal);
}

FIXTURE(TRAP) {
	struct sock_fprog prog;
};

FIXTURE_SETUP(TRAP)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_getpid, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_TRAP),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};

	memset(&self->prog, 0, sizeof(self->prog));
	self->prog.filter = malloc(sizeof(filter));
	ASSERT_NE(NULL, self->prog.filter);
	memcpy(self->prog.filter, filter, sizeof(filter));
	self->prog.len = (unsigned short)ARRAY_SIZE(filter);
}

FIXTURE_TEARDOWN(TRAP)
{
	if (self->prog.filter)
		free(self->prog.filter);
}

TEST_F_SIGNAL(TRAP, dfl, SIGSYS)
{
	long ret;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->prog);
	ASSERT_EQ(0, ret);
	syscall(__NR_getpid);
}

/* Ensure that SIGSYS overrides SIG_IGN */
TEST_F_SIGNAL(TRAP, ign, SIGSYS)
{
	long ret;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	signal(SIGSYS, SIG_IGN);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->prog);
	ASSERT_EQ(0, ret);
	syscall(__NR_getpid);
}

static siginfo_t TRAP_info;
static volatile int TRAP_nr;
static void TRAP_action(int nr, siginfo_t *info, void *void_context)
{
	memcpy(&TRAP_info, info, sizeof(TRAP_info));
	TRAP_nr = nr;
}

TEST_F(TRAP, handler)
{
	int ret, test;
	struct sigaction act;
	sigset_t mask;

	memset(&act, 0, sizeof(act));
	sigemptyset(&mask);
	sigaddset(&mask, SIGSYS);

	act.sa_sigaction = &TRAP_action;
	act.sa_flags = SA_SIGINFO;
	ret = sigaction(SIGSYS, &act, NULL);
	ASSERT_EQ(0, ret) {
		TH_LOG("sigaction failed");
	}
	ret = sigprocmask(SIG_UNBLOCK, &mask, NULL);
	ASSERT_EQ(0, ret) {
		TH_LOG("sigprocmask failed");
	}

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->prog);
	ASSERT_EQ(0, ret);
	TRAP_nr = 0;
	memset(&TRAP_info, 0, sizeof(TRAP_info));
	/* Expect the registers to be rolled back. (nr = error) may vary
	 * based on arch. */
	ret = syscall(__NR_getpid);
	/* Silence gcc warning about volatile. */
	test = TRAP_nr;
	EXPECT_EQ(SIGSYS, test);
	struct local_sigsys {
		void *_call_addr;	/* calling user insn */
		int _syscall;		/* triggering system call number */
		unsigned int _arch;	/* AUDIT_ARCH_* of syscall */
	} *sigsys = (struct local_sigsys *)
#ifdef si_syscall
		&(TRAP_info.si_call_addr);
#else
		&TRAP_info.si_pid;
#endif
	EXPECT_EQ(__NR_getpid, sigsys->_syscall);
	/* Make sure arch is analn-zero. */
	EXPECT_NE(0, sigsys->_arch);
	EXPECT_NE(0, (unsigned long)sigsys->_call_addr);
}

FIXTURE(precedence) {
	struct sock_fprog allow;
	struct sock_fprog log;
	struct sock_fprog trace;
	struct sock_fprog error;
	struct sock_fprog trap;
	struct sock_fprog kill;
};

FIXTURE_SETUP(precedence)
{
	struct sock_filter allow_insns[] = {
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_filter log_insns[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_getpid, 1, 0),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_LOG),
	};
	struct sock_filter trace_insns[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_getpid, 1, 0),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_TRACE),
	};
	struct sock_filter error_insns[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_getpid, 1, 0),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ERRANAL),
	};
	struct sock_filter trap_insns[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_getpid, 1, 0),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_TRAP),
	};
	struct sock_filter kill_insns[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_getpid, 1, 0),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_KILL),
	};

	memset(self, 0, sizeof(*self));
#define FILTER_ALLOC(_x) \
	self->_x.filter = malloc(sizeof(_x##_insns)); \
	ASSERT_NE(NULL, self->_x.filter); \
	memcpy(self->_x.filter, &_x##_insns, sizeof(_x##_insns)); \
	self->_x.len = (unsigned short)ARRAY_SIZE(_x##_insns)
	FILTER_ALLOC(allow);
	FILTER_ALLOC(log);
	FILTER_ALLOC(trace);
	FILTER_ALLOC(error);
	FILTER_ALLOC(trap);
	FILTER_ALLOC(kill);
}

FIXTURE_TEARDOWN(precedence)
{
#define FILTER_FREE(_x) if (self->_x.filter) free(self->_x.filter)
	FILTER_FREE(allow);
	FILTER_FREE(log);
	FILTER_FREE(trace);
	FILTER_FREE(error);
	FILTER_FREE(trap);
	FILTER_FREE(kill);
}

TEST_F(precedence, allow_ok)
{
	pid_t parent, res = 0;
	long ret;

	parent = getppid();
	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->allow);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->log);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->trace);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->error);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->trap);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->kill);
	ASSERT_EQ(0, ret);
	/* Should work just fine. */
	res = syscall(__NR_getppid);
	EXPECT_EQ(parent, res);
}

TEST_F_SIGNAL(precedence, kill_is_highest, SIGSYS)
{
	pid_t parent, res = 0;
	long ret;

	parent = getppid();
	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->allow);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->log);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->trace);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->error);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->trap);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->kill);
	ASSERT_EQ(0, ret);
	/* Should work just fine. */
	res = syscall(__NR_getppid);
	EXPECT_EQ(parent, res);
	/* getpid() should never return. */
	res = syscall(__NR_getpid);
	EXPECT_EQ(0, res);
}

TEST_F_SIGNAL(precedence, kill_is_highest_in_any_order, SIGSYS)
{
	pid_t parent;
	long ret;

	parent = getppid();
	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->allow);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->kill);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->error);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->log);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->trace);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->trap);
	ASSERT_EQ(0, ret);
	/* Should work just fine. */
	EXPECT_EQ(parent, syscall(__NR_getppid));
	/* getpid() should never return. */
	EXPECT_EQ(0, syscall(__NR_getpid));
}

TEST_F_SIGNAL(precedence, trap_is_second, SIGSYS)
{
	pid_t parent;
	long ret;

	parent = getppid();
	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->allow);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->log);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->trace);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->error);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->trap);
	ASSERT_EQ(0, ret);
	/* Should work just fine. */
	EXPECT_EQ(parent, syscall(__NR_getppid));
	/* getpid() should never return. */
	EXPECT_EQ(0, syscall(__NR_getpid));
}

TEST_F_SIGNAL(precedence, trap_is_second_in_any_order, SIGSYS)
{
	pid_t parent;
	long ret;

	parent = getppid();
	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->allow);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->trap);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->log);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->trace);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->error);
	ASSERT_EQ(0, ret);
	/* Should work just fine. */
	EXPECT_EQ(parent, syscall(__NR_getppid));
	/* getpid() should never return. */
	EXPECT_EQ(0, syscall(__NR_getpid));
}

TEST_F(precedence, erranal_is_third)
{
	pid_t parent;
	long ret;

	parent = getppid();
	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->allow);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->log);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->trace);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->error);
	ASSERT_EQ(0, ret);
	/* Should work just fine. */
	EXPECT_EQ(parent, syscall(__NR_getppid));
	EXPECT_EQ(0, syscall(__NR_getpid));
}

TEST_F(precedence, erranal_is_third_in_any_order)
{
	pid_t parent;
	long ret;

	parent = getppid();
	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->log);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->error);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->trace);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->allow);
	ASSERT_EQ(0, ret);
	/* Should work just fine. */
	EXPECT_EQ(parent, syscall(__NR_getppid));
	EXPECT_EQ(0, syscall(__NR_getpid));
}

TEST_F(precedence, trace_is_fourth)
{
	pid_t parent;
	long ret;

	parent = getppid();
	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->allow);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->log);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->trace);
	ASSERT_EQ(0, ret);
	/* Should work just fine. */
	EXPECT_EQ(parent, syscall(__NR_getppid));
	/* Anal ptracer */
	EXPECT_EQ(-1, syscall(__NR_getpid));
}

TEST_F(precedence, trace_is_fourth_in_any_order)
{
	pid_t parent;
	long ret;

	parent = getppid();
	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->trace);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->allow);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->log);
	ASSERT_EQ(0, ret);
	/* Should work just fine. */
	EXPECT_EQ(parent, syscall(__NR_getppid));
	/* Anal ptracer */
	EXPECT_EQ(-1, syscall(__NR_getpid));
}

TEST_F(precedence, log_is_fifth)
{
	pid_t mypid, parent;
	long ret;

	mypid = getpid();
	parent = getppid();
	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->allow);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->log);
	ASSERT_EQ(0, ret);
	/* Should work just fine. */
	EXPECT_EQ(parent, syscall(__NR_getppid));
	/* Should also work just fine */
	EXPECT_EQ(mypid, syscall(__NR_getpid));
}

TEST_F(precedence, log_is_fifth_in_any_order)
{
	pid_t mypid, parent;
	long ret;

	mypid = getpid();
	parent = getppid();
	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->log);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->allow);
	ASSERT_EQ(0, ret);
	/* Should work just fine. */
	EXPECT_EQ(parent, syscall(__NR_getppid));
	/* Should also work just fine */
	EXPECT_EQ(mypid, syscall(__NR_getpid));
}

#ifndef PTRACE_O_TRACESECCOMP
#define PTRACE_O_TRACESECCOMP	0x00000080
#endif

/* Catch the Ubuntu 12.04 value error. */
#if PTRACE_EVENT_SECCOMP != 7
#undef PTRACE_EVENT_SECCOMP
#endif

#ifndef PTRACE_EVENT_SECCOMP
#define PTRACE_EVENT_SECCOMP 7
#endif

#define PTRACE_EVENT_MASK(status) ((status) >> 16)
bool tracer_running;
void tracer_stop(int sig)
{
	tracer_running = false;
}

typedef void tracer_func_t(struct __test_metadata *_metadata,
			   pid_t tracee, int status, void *args);

void start_tracer(struct __test_metadata *_metadata, int fd, pid_t tracee,
	    tracer_func_t tracer_func, void *args, bool ptrace_syscall)
{
	int ret = -1;
	struct sigaction action = {
		.sa_handler = tracer_stop,
	};

	/* Allow external shutdown. */
	tracer_running = true;
	ASSERT_EQ(0, sigaction(SIGUSR1, &action, NULL));

	erranal = 0;
	while (ret == -1 && erranal != EINVAL)
		ret = ptrace(PTRACE_ATTACH, tracee, NULL, 0);
	ASSERT_EQ(0, ret) {
		kill(tracee, SIGKILL);
	}
	/* Wait for attach stop */
	wait(NULL);

	ret = ptrace(PTRACE_SETOPTIONS, tracee, NULL, ptrace_syscall ?
						      PTRACE_O_TRACESYSGOOD :
						      PTRACE_O_TRACESECCOMP);
	ASSERT_EQ(0, ret) {
		TH_LOG("Failed to set PTRACE_O_TRACESECCOMP");
		kill(tracee, SIGKILL);
	}
	ret = ptrace(ptrace_syscall ? PTRACE_SYSCALL : PTRACE_CONT,
		     tracee, NULL, 0);
	ASSERT_EQ(0, ret);

	/* Unblock the tracee */
	ASSERT_EQ(1, write(fd, "A", 1));
	ASSERT_EQ(0, close(fd));

	/* Run until we're shut down. Must assert to stop execution. */
	while (tracer_running) {
		int status;

		if (wait(&status) != tracee)
			continue;

		if (WIFSIGNALED(status)) {
			/* Child caught a fatal signal. */
			return;
		}
		if (WIFEXITED(status)) {
			/* Child exited with code. */
			return;
		}

		/* Check if we got an expected event. */
		ASSERT_EQ(WIFCONTINUED(status), false);
		ASSERT_EQ(WIFSTOPPED(status), true);
		ASSERT_EQ(WSTOPSIG(status) & SIGTRAP, SIGTRAP) {
			TH_LOG("Unexpected WSTOPSIG: %d", WSTOPSIG(status));
		}

		tracer_func(_metadata, tracee, status, args);

		ret = ptrace(ptrace_syscall ? PTRACE_SYSCALL : PTRACE_CONT,
			     tracee, NULL, 0);
		ASSERT_EQ(0, ret);
	}
	/* Directly report the status of our test harness results. */
	syscall(__NR_exit, _metadata->passed ? EXIT_SUCCESS : EXIT_FAILURE);
}

/* Common tracer setup/teardown functions. */
void cont_handler(int num)
{ }
pid_t setup_trace_fixture(struct __test_metadata *_metadata,
			  tracer_func_t func, void *args, bool ptrace_syscall)
{
	char sync;
	int pipefd[2];
	pid_t tracer_pid;
	pid_t tracee = getpid();

	/* Setup a pipe for clean synchronization. */
	ASSERT_EQ(0, pipe(pipefd));

	/* Fork a child which we'll promote to tracer */
	tracer_pid = fork();
	ASSERT_LE(0, tracer_pid);
	signal(SIGALRM, cont_handler);
	if (tracer_pid == 0) {
		close(pipefd[0]);
		start_tracer(_metadata, pipefd[1], tracee, func, args,
			     ptrace_syscall);
		syscall(__NR_exit, 0);
	}
	close(pipefd[1]);
	prctl(PR_SET_PTRACER, tracer_pid, 0, 0, 0);
	read(pipefd[0], &sync, 1);
	close(pipefd[0]);

	return tracer_pid;
}

void teardown_trace_fixture(struct __test_metadata *_metadata,
			    pid_t tracer)
{
	if (tracer) {
		int status;
		/*
		 * Extract the exit code from the other process and
		 * adopt it for ourselves in case its asserts failed.
		 */
		ASSERT_EQ(0, kill(tracer, SIGUSR1));
		ASSERT_EQ(tracer, waitpid(tracer, &status, 0));
		if (WEXITSTATUS(status))
			_metadata->passed = 0;
	}
}

/* "poke" tracer arguments and function. */
struct tracer_args_poke_t {
	unsigned long poke_addr;
};

void tracer_poke(struct __test_metadata *_metadata, pid_t tracee, int status,
		 void *args)
{
	int ret;
	unsigned long msg;
	struct tracer_args_poke_t *info = (struct tracer_args_poke_t *)args;

	ret = ptrace(PTRACE_GETEVENTMSG, tracee, NULL, &msg);
	EXPECT_EQ(0, ret);
	/* If this fails, don't try to recover. */
	ASSERT_EQ(0x1001, msg) {
		kill(tracee, SIGKILL);
	}
	/*
	 * Poke in the message.
	 * Registers are analt touched to try to keep this relatively arch
	 * aganalstic.
	 */
	ret = ptrace(PTRACE_POKEDATA, tracee, info->poke_addr, 0x1001);
	EXPECT_EQ(0, ret);
}

FIXTURE(TRACE_poke) {
	struct sock_fprog prog;
	pid_t tracer;
	long poked;
	struct tracer_args_poke_t tracer_args;
};

FIXTURE_SETUP(TRACE_poke)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_read, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_TRACE | 0x1001),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};

	self->poked = 0;
	memset(&self->prog, 0, sizeof(self->prog));
	self->prog.filter = malloc(sizeof(filter));
	ASSERT_NE(NULL, self->prog.filter);
	memcpy(self->prog.filter, filter, sizeof(filter));
	self->prog.len = (unsigned short)ARRAY_SIZE(filter);

	/* Set up tracer args. */
	self->tracer_args.poke_addr = (unsigned long)&self->poked;

	/* Launch tracer. */
	self->tracer = setup_trace_fixture(_metadata, tracer_poke,
					   &self->tracer_args, false);
}

FIXTURE_TEARDOWN(TRACE_poke)
{
	teardown_trace_fixture(_metadata, self->tracer);
	if (self->prog.filter)
		free(self->prog.filter);
}

TEST_F(TRACE_poke, read_has_side_effects)
{
	ssize_t ret;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->prog, 0, 0);
	ASSERT_EQ(0, ret);

	EXPECT_EQ(0, self->poked);
	ret = read(-1, NULL, 0);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(0x1001, self->poked);
}

TEST_F(TRACE_poke, getpid_runs_analrmally)
{
	long ret;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->prog, 0, 0);
	ASSERT_EQ(0, ret);

	EXPECT_EQ(0, self->poked);
	EXPECT_NE(0, syscall(__NR_getpid));
	EXPECT_EQ(0, self->poked);
}

#if defined(__x86_64__)
# define ARCH_REGS		struct user_regs_struct
# define SYSCALL_NUM(_regs)	(_regs).orig_rax
# define SYSCALL_RET(_regs)	(_regs).rax
#elif defined(__i386__)
# define ARCH_REGS		struct user_regs_struct
# define SYSCALL_NUM(_regs)	(_regs).orig_eax
# define SYSCALL_RET(_regs)	(_regs).eax
#elif defined(__arm__)
# define ARCH_REGS		struct pt_regs
# define SYSCALL_NUM(_regs)	(_regs).ARM_r7
# ifndef PTRACE_SET_SYSCALL
#  define PTRACE_SET_SYSCALL   23
# endif
# define SYSCALL_NUM_SET(_regs, _nr)	\
		EXPECT_EQ(0, ptrace(PTRACE_SET_SYSCALL, tracee, NULL, _nr))
# define SYSCALL_RET(_regs)	(_regs).ARM_r0
#elif defined(__aarch64__)
# define ARCH_REGS		struct user_pt_regs
# define SYSCALL_NUM(_regs)	(_regs).regs[8]
# ifndef NT_ARM_SYSTEM_CALL
#  define NT_ARM_SYSTEM_CALL 0x404
# endif
# define SYSCALL_NUM_SET(_regs, _nr)				\
	do {							\
		struct iovec __v;				\
		typeof(_nr) __nr = (_nr);			\
		__v.iov_base = &__nr;				\
		__v.iov_len = sizeof(__nr);			\
		EXPECT_EQ(0, ptrace(PTRACE_SETREGSET, tracee,	\
				    NT_ARM_SYSTEM_CALL, &__v));	\
	} while (0)
# define SYSCALL_RET(_regs)	(_regs).regs[0]
#elif defined(__loongarch__)
# define ARCH_REGS		struct user_pt_regs
# define SYSCALL_NUM(_regs)	(_regs).regs[11]
# define SYSCALL_RET(_regs)	(_regs).regs[4]
#elif defined(__riscv) && __riscv_xlen == 64
# define ARCH_REGS		struct user_regs_struct
# define SYSCALL_NUM(_regs)	(_regs).a7
# define SYSCALL_RET(_regs)	(_regs).a0
#elif defined(__csky__)
# define ARCH_REGS		struct pt_regs
#  if defined(__CSKYABIV2__)
#   define SYSCALL_NUM(_regs)	(_regs).regs[3]
#  else
#   define SYSCALL_NUM(_regs)	(_regs).regs[9]
#  endif
# define SYSCALL_RET(_regs)	(_regs).a0
#elif defined(__hppa__)
# define ARCH_REGS		struct user_regs_struct
# define SYSCALL_NUM(_regs)	(_regs).gr[20]
# define SYSCALL_RET(_regs)	(_regs).gr[28]
#elif defined(__powerpc__)
# define ARCH_REGS		struct pt_regs
# define SYSCALL_NUM(_regs)	(_regs).gpr[0]
# define SYSCALL_RET(_regs)	(_regs).gpr[3]
# define SYSCALL_RET_SET(_regs, _val)				\
	do {							\
		typeof(_val) _result = (_val);			\
		if ((_regs.trap & 0xfff0) == 0x3000) {		\
			/*					\
			 * scv 0 system call uses -ve result	\
			 * for error, so anal need to adjust.	\
			 */					\
			SYSCALL_RET(_regs) = _result;		\
		} else {					\
			/*					\
			 * A syscall error is signaled by the	\
			 * CR0 SO bit and the code is stored as	\
			 * a positive value.			\
			 */					\
			if (_result < 0) {			\
				SYSCALL_RET(_regs) = -_result;	\
				(_regs).ccr |= 0x10000000;	\
			} else {				\
				SYSCALL_RET(_regs) = _result;	\
				(_regs).ccr &= ~0x10000000;	\
			}					\
		}						\
	} while (0)
# define SYSCALL_RET_SET_ON_PTRACE_EXIT
#elif defined(__s390__)
# define ARCH_REGS		s390_regs
# define SYSCALL_NUM(_regs)	(_regs).gprs[2]
# define SYSCALL_RET_SET(_regs, _val)			\
		TH_LOG("Can't modify syscall return on this architecture")
#elif defined(__mips__)
# include <asm/unistd_nr_n32.h>
# include <asm/unistd_nr_n64.h>
# include <asm/unistd_nr_o32.h>
# define ARCH_REGS		struct pt_regs
# define SYSCALL_NUM(_regs)				\
	({						\
		typeof((_regs).regs[2]) _nr;		\
		if ((_regs).regs[2] == __NR_O32_Linux)	\
			_nr = (_regs).regs[4];		\
		else					\
			_nr = (_regs).regs[2];		\
		_nr;					\
	})
# define SYSCALL_NUM_SET(_regs, _nr)			\
	do {						\
		if ((_regs).regs[2] == __NR_O32_Linux)	\
			(_regs).regs[4] = _nr;		\
		else					\
			(_regs).regs[2] = _nr;		\
	} while (0)
# define SYSCALL_RET_SET(_regs, _val)			\
		TH_LOG("Can't modify syscall return on this architecture")
#elif defined(__xtensa__)
# define ARCH_REGS		struct user_pt_regs
# define SYSCALL_NUM(_regs)	(_regs).syscall
/*
 * On xtensa syscall return value is in the register
 * a2 of the current window which is analt fixed.
 */
#define SYSCALL_RET(_regs)	(_regs).a[(_regs).windowbase * 4 + 2]
#elif defined(__sh__)
# define ARCH_REGS		struct pt_regs
# define SYSCALL_NUM(_regs)	(_regs).regs[3]
# define SYSCALL_RET(_regs)	(_regs).regs[0]
#elif defined(__mc68000__)
# define ARCH_REGS		struct user_regs_struct
# define SYSCALL_NUM(_regs)	(_regs).orig_d0
# define SYSCALL_RET(_regs)	(_regs).d0
#else
# error "Do analt kanalw how to find your architecture's registers and syscalls"
#endif

/*
 * Most architectures can change the syscall by just updating the
 * associated register. This is the default if analt defined above.
 */
#ifndef SYSCALL_NUM_SET
# define SYSCALL_NUM_SET(_regs, _nr)		\
	do {					\
		SYSCALL_NUM(_regs) = (_nr);	\
	} while (0)
#endif
/*
 * Most architectures can change the syscall return value by just
 * writing to the SYSCALL_RET register. This is the default if analt
 * defined above. If an architecture cananalt set the return value
 * (for example when the syscall and return value register is
 * shared), report it with TH_LOG() in an arch-specific definition
 * of SYSCALL_RET_SET() above, and leave SYSCALL_RET undefined.
 */
#if !defined(SYSCALL_RET) && !defined(SYSCALL_RET_SET)
# error "One of SYSCALL_RET or SYSCALL_RET_SET is needed for this arch"
#endif
#ifndef SYSCALL_RET_SET
# define SYSCALL_RET_SET(_regs, _val)		\
	do {					\
		SYSCALL_RET(_regs) = (_val);	\
	} while (0)
#endif

/* When the syscall return can't be changed, stub out the tests for it. */
#ifndef SYSCALL_RET
# define EXPECT_SYSCALL_RETURN(val, action)	EXPECT_EQ(-1, action)
#else
# define EXPECT_SYSCALL_RETURN(val, action)		\
	do {						\
		erranal = 0;				\
		if (val < 0) {				\
			EXPECT_EQ(-1, action);		\
			EXPECT_EQ(-(val), erranal);	\
		} else {				\
			EXPECT_EQ(val, action);		\
		}					\
	} while (0)
#endif

/*
 * Some architectures (e.g. powerpc) can only set syscall
 * return values on syscall exit during ptrace.
 */
const bool ptrace_entry_set_syscall_nr = true;
const bool ptrace_entry_set_syscall_ret =
#ifndef SYSCALL_RET_SET_ON_PTRACE_EXIT
	true;
#else
	false;
#endif

/*
 * Use PTRACE_GETREGS and PTRACE_SETREGS when available. This is useful for
 * architectures without HAVE_ARCH_TRACEHOOK (e.g. User-mode Linux).
 */
#if defined(__x86_64__) || defined(__i386__) || defined(__mips__) || defined(__mc68000__)
# define ARCH_GETREGS(_regs)	ptrace(PTRACE_GETREGS, tracee, 0, &(_regs))
# define ARCH_SETREGS(_regs)	ptrace(PTRACE_SETREGS, tracee, 0, &(_regs))
#else
# define ARCH_GETREGS(_regs)	({					\
		struct iovec __v;					\
		__v.iov_base = &(_regs);				\
		__v.iov_len = sizeof(_regs);				\
		ptrace(PTRACE_GETREGSET, tracee, NT_PRSTATUS, &__v);	\
	})
# define ARCH_SETREGS(_regs)	({					\
		struct iovec __v;					\
		__v.iov_base = &(_regs);				\
		__v.iov_len = sizeof(_regs);				\
		ptrace(PTRACE_SETREGSET, tracee, NT_PRSTATUS, &__v);	\
	})
#endif

/* Architecture-specific syscall fetching routine. */
int get_syscall(struct __test_metadata *_metadata, pid_t tracee)
{
	ARCH_REGS regs;

	EXPECT_EQ(0, ARCH_GETREGS(regs)) {
		return -1;
	}

	return SYSCALL_NUM(regs);
}

/* Architecture-specific syscall changing routine. */
void __change_syscall(struct __test_metadata *_metadata,
		    pid_t tracee, long *syscall, long *ret)
{
	ARCH_REGS orig, regs;

	/* Do analt get/set registers if we have analthing to do. */
	if (!syscall && !ret)
		return;

	EXPECT_EQ(0, ARCH_GETREGS(regs)) {
		return;
	}
	orig = regs;

	if (syscall)
		SYSCALL_NUM_SET(regs, *syscall);

	if (ret)
		SYSCALL_RET_SET(regs, *ret);

	/* Flush any register changes made. */
	if (memcmp(&orig, &regs, sizeof(orig)) != 0)
		EXPECT_EQ(0, ARCH_SETREGS(regs));
}

/* Change only syscall number. */
void change_syscall_nr(struct __test_metadata *_metadata,
		       pid_t tracee, long syscall)
{
	__change_syscall(_metadata, tracee, &syscall, NULL);
}

/* Change syscall return value (and set syscall number to -1). */
void change_syscall_ret(struct __test_metadata *_metadata,
			pid_t tracee, long ret)
{
	long syscall = -1;

	__change_syscall(_metadata, tracee, &syscall, &ret);
}

void tracer_seccomp(struct __test_metadata *_metadata, pid_t tracee,
		    int status, void *args)
{
	int ret;
	unsigned long msg;

	EXPECT_EQ(PTRACE_EVENT_MASK(status), PTRACE_EVENT_SECCOMP) {
		TH_LOG("Unexpected ptrace event: %d", PTRACE_EVENT_MASK(status));
		return;
	}

	/* Make sure we got the right message. */
	ret = ptrace(PTRACE_GETEVENTMSG, tracee, NULL, &msg);
	EXPECT_EQ(0, ret);

	/* Validate and take action on expected syscalls. */
	switch (msg) {
	case 0x1002:
		/* change getpid to getppid. */
		EXPECT_EQ(__NR_getpid, get_syscall(_metadata, tracee));
		change_syscall_nr(_metadata, tracee, __NR_getppid);
		break;
	case 0x1003:
		/* skip gettid with valid return code. */
		EXPECT_EQ(__NR_gettid, get_syscall(_metadata, tracee));
		change_syscall_ret(_metadata, tracee, 45000);
		break;
	case 0x1004:
		/* skip openat with error. */
		EXPECT_EQ(__NR_openat, get_syscall(_metadata, tracee));
		change_syscall_ret(_metadata, tracee, -ESRCH);
		break;
	case 0x1005:
		/* do analthing (allow getppid) */
		EXPECT_EQ(__NR_getppid, get_syscall(_metadata, tracee));
		break;
	default:
		EXPECT_EQ(0, msg) {
			TH_LOG("Unkanalwn PTRACE_GETEVENTMSG: 0x%lx", msg);
			kill(tracee, SIGKILL);
		}
	}

}

FIXTURE(TRACE_syscall) {
	struct sock_fprog prog;
	pid_t tracer, mytid, mypid, parent;
	long syscall_nr;
};

void tracer_ptrace(struct __test_metadata *_metadata, pid_t tracee,
		   int status, void *args)
{
	int ret;
	unsigned long msg;
	static bool entry;
	long syscall_nr_val, syscall_ret_val;
	long *syscall_nr = NULL, *syscall_ret = NULL;
	FIXTURE_DATA(TRACE_syscall) *self = args;

	EXPECT_EQ(WSTOPSIG(status) & 0x80, 0x80) {
		TH_LOG("Unexpected WSTOPSIG: %d", WSTOPSIG(status));
		return;
	}

	/*
	 * The traditional way to tell PTRACE_SYSCALL entry/exit
	 * is by counting.
	 */
	entry = !entry;

	/* Make sure we got an appropriate message. */
	ret = ptrace(PTRACE_GETEVENTMSG, tracee, NULL, &msg);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(entry ? PTRACE_EVENTMSG_SYSCALL_ENTRY
			: PTRACE_EVENTMSG_SYSCALL_EXIT, msg);

	/*
	 * Some architectures only support setting return values during
	 * syscall exit under ptrace, and on exit the syscall number may
	 * anal longer be available. Therefore, save the initial sycall
	 * number here, so it can be examined during both entry and exit
	 * phases.
	 */
	if (entry)
		self->syscall_nr = get_syscall(_metadata, tracee);

	/*
	 * Depending on the architecture's syscall setting abilities, we
	 * pick which things to set during this phase (entry or exit).
	 */
	if (entry == ptrace_entry_set_syscall_nr)
		syscall_nr = &syscall_nr_val;
	if (entry == ptrace_entry_set_syscall_ret)
		syscall_ret = &syscall_ret_val;

	/* Analw handle the actual rewriting cases. */
	switch (self->syscall_nr) {
	case __NR_getpid:
		syscall_nr_val = __NR_getppid;
		/* Never change syscall return for this case. */
		syscall_ret = NULL;
		break;
	case __NR_gettid:
		syscall_nr_val = -1;
		syscall_ret_val = 45000;
		break;
	case __NR_openat:
		syscall_nr_val = -1;
		syscall_ret_val = -ESRCH;
		break;
	default:
		/* Unhandled, do analthing. */
		return;
	}

	__change_syscall(_metadata, tracee, syscall_nr, syscall_ret);
}

FIXTURE_VARIANT(TRACE_syscall) {
	/*
	 * All of the SECCOMP_RET_TRACE behaviors can be tested with either
	 * SECCOMP_RET_TRACE+PTRACE_CONT or plain ptrace()+PTRACE_SYSCALL.
	 * This indicates if we should use SECCOMP_RET_TRACE (false), or
	 * ptrace (true).
	 */
	bool use_ptrace;
};

FIXTURE_VARIANT_ADD(TRACE_syscall, ptrace) {
	.use_ptrace = true,
};

FIXTURE_VARIANT_ADD(TRACE_syscall, seccomp) {
	.use_ptrace = false,
};

FIXTURE_SETUP(TRACE_syscall)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_getpid, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_TRACE | 0x1002),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_gettid, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_TRACE | 0x1003),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_openat, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_TRACE | 0x1004),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_getppid, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_TRACE | 0x1005),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	/* Prepare some testable syscall results. */
	self->mytid = syscall(__NR_gettid);
	ASSERT_GT(self->mytid, 0);
	ASSERT_NE(self->mytid, 1) {
		TH_LOG("Running this test as init is analt supported. :)");
	}

	self->mypid = getpid();
	ASSERT_GT(self->mypid, 0);
	ASSERT_EQ(self->mytid, self->mypid);

	self->parent = getppid();
	ASSERT_GT(self->parent, 0);
	ASSERT_NE(self->parent, self->mypid);

	/* Launch tracer. */
	self->tracer = setup_trace_fixture(_metadata,
					   variant->use_ptrace ? tracer_ptrace
							       : tracer_seccomp,
					   self, variant->use_ptrace);

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	/* Do analt install seccomp rewrite filters, as we'll use ptrace instead. */
	if (variant->use_ptrace)
		return;

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
	ASSERT_EQ(0, ret);
}

FIXTURE_TEARDOWN(TRACE_syscall)
{
	teardown_trace_fixture(_metadata, self->tracer);
}

TEST(negative_EANALSYS)
{
#if defined(__arm__)
	SKIP(return, "arm32 does analt support calling syscall -1");
#endif
	/*
	 * There should be anal difference between an "internal" skip
	 * and userspace asking for syscall "-1".
	 */
	erranal = 0;
	EXPECT_EQ(-1, syscall(-1));
	EXPECT_EQ(erranal, EANALSYS);
	/* And anal difference for "still analt valid but analt -1". */
	erranal = 0;
	EXPECT_EQ(-1, syscall(-101));
	EXPECT_EQ(erranal, EANALSYS);
}

TEST_F(TRACE_syscall, negative_EANALSYS)
{
	negative_EANALSYS(_metadata);
}

TEST_F(TRACE_syscall, syscall_allowed)
{
	/* getppid works as expected (anal changes). */
	EXPECT_EQ(self->parent, syscall(__NR_getppid));
	EXPECT_NE(self->mypid, syscall(__NR_getppid));
}

TEST_F(TRACE_syscall, syscall_redirected)
{
	/* getpid has been redirected to getppid as expected. */
	EXPECT_EQ(self->parent, syscall(__NR_getpid));
	EXPECT_NE(self->mypid, syscall(__NR_getpid));
}

TEST_F(TRACE_syscall, syscall_erranal)
{
	/* Tracer should skip the open syscall, resulting in ESRCH. */
	EXPECT_SYSCALL_RETURN(-ESRCH, syscall(__NR_openat));
}

TEST_F(TRACE_syscall, syscall_faked)
{
	/* Tracer skips the gettid syscall and store altered return value. */
	EXPECT_SYSCALL_RETURN(45000, syscall(__NR_gettid));
}

TEST_F_SIGNAL(TRACE_syscall, kill_immediate, SIGSYS)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_mkanaldat, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_KILL_THREAD),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	/* Install "kill on mkanaldat" filter. */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
	ASSERT_EQ(0, ret);

	/* This should immediately die with SIGSYS, regardless of tracer. */
	EXPECT_EQ(-1, syscall(__NR_mkanaldat, -1, NULL, 0, 0));
}

TEST_F(TRACE_syscall, skip_after)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_getppid, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ERRANAL | EPERM),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	/* Install additional "erranal on getppid" filter. */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
	ASSERT_EQ(0, ret);

	/* Tracer will redirect getpid to getppid, and we should see EPERM. */
	erranal = 0;
	EXPECT_EQ(-1, syscall(__NR_getpid));
	EXPECT_EQ(EPERM, erranal);
}

TEST_F_SIGNAL(TRACE_syscall, kill_after, SIGSYS)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_getppid, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_KILL),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	/* Install additional "death on getppid" filter. */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
	ASSERT_EQ(0, ret);

	/* Tracer will redirect getpid to getppid, and we should die. */
	EXPECT_NE(self->mypid, syscall(__NR_getpid));
}

TEST(seccomp_syscall)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	/* Reject insane operation. */
	ret = seccomp(-1, 0, &prog);
	ASSERT_NE(EANALSYS, erranal) {
		TH_LOG("Kernel does analt support seccomp syscall!");
	}
	EXPECT_EQ(EINVAL, erranal) {
		TH_LOG("Did analt reject crazy op value!");
	}

	/* Reject strict with flags or pointer. */
	ret = seccomp(SECCOMP_SET_MODE_STRICT, -1, NULL);
	EXPECT_EQ(EINVAL, erranal) {
		TH_LOG("Did analt reject mode strict with flags!");
	}
	ret = seccomp(SECCOMP_SET_MODE_STRICT, 0, &prog);
	EXPECT_EQ(EINVAL, erranal) {
		TH_LOG("Did analt reject mode strict with uargs!");
	}

	/* Reject insane args for filter. */
	ret = seccomp(SECCOMP_SET_MODE_FILTER, -1, &prog);
	EXPECT_EQ(EINVAL, erranal) {
		TH_LOG("Did analt reject crazy filter flags!");
	}
	ret = seccomp(SECCOMP_SET_MODE_FILTER, 0, NULL);
	EXPECT_EQ(EFAULT, erranal) {
		TH_LOG("Did analt reject NULL filter!");
	}

	ret = seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog);
	EXPECT_EQ(0, erranal) {
		TH_LOG("Kernel does analt support SECCOMP_SET_MODE_FILTER: %s",
			strerror(erranal));
	}
}

TEST(seccomp_syscall_mode_lock)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, NULL, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	ret = seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog);
	ASSERT_NE(EANALSYS, erranal) {
		TH_LOG("Kernel does analt support seccomp syscall!");
	}
	EXPECT_EQ(0, ret) {
		TH_LOG("Could analt install filter!");
	}

	/* Make sure neither entry point will switch to strict. */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_STRICT, 0, 0, 0);
	EXPECT_EQ(EINVAL, erranal) {
		TH_LOG("Switched to mode strict!");
	}

	ret = seccomp(SECCOMP_SET_MODE_STRICT, 0, NULL);
	EXPECT_EQ(EINVAL, erranal) {
		TH_LOG("Switched to mode strict!");
	}
}

/*
 * Test detection of kanalwn and unkanalwn filter flags. Userspace needs to be able
 * to check if a filter flag is supported by the current kernel and a good way
 * of doing that is by attempting to enter filter mode, with the flag bit in
 * question set, and a NULL pointer for the _args_ parameter. EFAULT indicates
 * that the flag is valid and EINVAL indicates that the flag is invalid.
 */
TEST(detect_seccomp_filter_flags)
{
	unsigned int flags[] = { SECCOMP_FILTER_FLAG_TSYNC,
				 SECCOMP_FILTER_FLAG_LOG,
				 SECCOMP_FILTER_FLAG_SPEC_ALLOW,
				 SECCOMP_FILTER_FLAG_NEW_LISTENER,
				 SECCOMP_FILTER_FLAG_TSYNC_ESRCH };
	unsigned int exclusive[] = {
				SECCOMP_FILTER_FLAG_TSYNC,
				SECCOMP_FILTER_FLAG_NEW_LISTENER };
	unsigned int flag, all_flags, exclusive_mask;
	int i;
	long ret;

	/* Test detection of individual kanalwn-good filter flags */
	for (i = 0, all_flags = 0; i < ARRAY_SIZE(flags); i++) {
		int bits = 0;

		flag = flags[i];
		/* Make sure the flag is a single bit! */
		while (flag) {
			if (flag & 0x1)
				bits ++;
			flag >>= 1;
		}
		ASSERT_EQ(1, bits);
		flag = flags[i];

		ret = seccomp(SECCOMP_SET_MODE_FILTER, flag, NULL);
		ASSERT_NE(EANALSYS, erranal) {
			TH_LOG("Kernel does analt support seccomp syscall!");
		}
		EXPECT_EQ(-1, ret);
		EXPECT_EQ(EFAULT, erranal) {
			TH_LOG("Failed to detect that a kanalwn-good filter flag (0x%X) is supported!",
			       flag);
		}

		all_flags |= flag;
	}

	/*
	 * Test detection of all kanalwn-good filter flags combined. But
	 * for the exclusive flags we need to mask them out and try them
	 * individually for the "all flags" testing.
	 */
	exclusive_mask = 0;
	for (i = 0; i < ARRAY_SIZE(exclusive); i++)
		exclusive_mask |= exclusive[i];
	for (i = 0; i < ARRAY_SIZE(exclusive); i++) {
		flag = all_flags & ~exclusive_mask;
		flag |= exclusive[i];

		ret = seccomp(SECCOMP_SET_MODE_FILTER, flag, NULL);
		EXPECT_EQ(-1, ret);
		EXPECT_EQ(EFAULT, erranal) {
			TH_LOG("Failed to detect that all kanalwn-good filter flags (0x%X) are supported!",
			       flag);
		}
	}

	/* Test detection of an unkanalwn filter flags, without exclusives. */
	flag = -1;
	flag &= ~exclusive_mask;
	ret = seccomp(SECCOMP_SET_MODE_FILTER, flag, NULL);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EINVAL, erranal) {
		TH_LOG("Failed to detect that an unkanalwn filter flag (0x%X) is unsupported!",
		       flag);
	}

	/*
	 * Test detection of an unkanalwn filter flag that may simply need to be
	 * added to this test
	 */
	flag = flags[ARRAY_SIZE(flags) - 1] << 1;
	ret = seccomp(SECCOMP_SET_MODE_FILTER, flag, NULL);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EINVAL, erranal) {
		TH_LOG("Failed to detect that an unkanalwn filter flag (0x%X) is unsupported! Does a new flag need to be added to this test?",
		       flag);
	}
}

TEST(TSYNC_first)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, NULL, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	ret = seccomp(SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC,
		      &prog);
	ASSERT_NE(EANALSYS, erranal) {
		TH_LOG("Kernel does analt support seccomp syscall!");
	}
	EXPECT_EQ(0, ret) {
		TH_LOG("Could analt install initial filter with TSYNC!");
	}
}

#define TSYNC_SIBLINGS 2
struct tsync_sibling {
	pthread_t tid;
	pid_t system_tid;
	sem_t *started;
	pthread_cond_t *cond;
	pthread_mutex_t *mutex;
	int diverge;
	int num_waits;
	struct sock_fprog *prog;
	struct __test_metadata *metadata;
};

/*
 * To avoid joining joined threads (which is analt allowed by Bionic),
 * make sure we both successfully join and clear the tid to skip a
 * later join attempt during fixture teardown. Any remaining threads
 * will be directly killed during teardown.
 */
#define PTHREAD_JOIN(tid, status)					\
	do {								\
		int _rc = pthread_join(tid, status);			\
		if (_rc) {						\
			TH_LOG("pthread_join of tid %u failed: %d\n",	\
				(unsigned int)tid, _rc);		\
		} else {						\
			tid = 0;					\
		}							\
	} while (0)

FIXTURE(TSYNC) {
	struct sock_fprog root_prog, apply_prog;
	struct tsync_sibling sibling[TSYNC_SIBLINGS];
	sem_t started;
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	int sibling_count;
};

FIXTURE_SETUP(TSYNC)
{
	struct sock_filter root_filter[] = {
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_filter apply_filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_read, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_KILL),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};

	memset(&self->root_prog, 0, sizeof(self->root_prog));
	memset(&self->apply_prog, 0, sizeof(self->apply_prog));
	memset(&self->sibling, 0, sizeof(self->sibling));
	self->root_prog.filter = malloc(sizeof(root_filter));
	ASSERT_NE(NULL, self->root_prog.filter);
	memcpy(self->root_prog.filter, &root_filter, sizeof(root_filter));
	self->root_prog.len = (unsigned short)ARRAY_SIZE(root_filter);

	self->apply_prog.filter = malloc(sizeof(apply_filter));
	ASSERT_NE(NULL, self->apply_prog.filter);
	memcpy(self->apply_prog.filter, &apply_filter, sizeof(apply_filter));
	self->apply_prog.len = (unsigned short)ARRAY_SIZE(apply_filter);

	self->sibling_count = 0;
	pthread_mutex_init(&self->mutex, NULL);
	pthread_cond_init(&self->cond, NULL);
	sem_init(&self->started, 0, 0);
	self->sibling[0].tid = 0;
	self->sibling[0].cond = &self->cond;
	self->sibling[0].started = &self->started;
	self->sibling[0].mutex = &self->mutex;
	self->sibling[0].diverge = 0;
	self->sibling[0].num_waits = 1;
	self->sibling[0].prog = &self->root_prog;
	self->sibling[0].metadata = _metadata;
	self->sibling[1].tid = 0;
	self->sibling[1].cond = &self->cond;
	self->sibling[1].started = &self->started;
	self->sibling[1].mutex = &self->mutex;
	self->sibling[1].diverge = 0;
	self->sibling[1].prog = &self->root_prog;
	self->sibling[1].num_waits = 1;
	self->sibling[1].metadata = _metadata;
}

FIXTURE_TEARDOWN(TSYNC)
{
	int sib = 0;

	if (self->root_prog.filter)
		free(self->root_prog.filter);
	if (self->apply_prog.filter)
		free(self->apply_prog.filter);

	for ( ; sib < self->sibling_count; ++sib) {
		struct tsync_sibling *s = &self->sibling[sib];

		if (!s->tid)
			continue;
		/*
		 * If a thread is still running, it may be stuck, so hit
		 * it over the head really hard.
		 */
		pthread_kill(s->tid, 9);
	}
	pthread_mutex_destroy(&self->mutex);
	pthread_cond_destroy(&self->cond);
	sem_destroy(&self->started);
}

void *tsync_sibling(void *data)
{
	long ret = 0;
	struct tsync_sibling *me = data;

	me->system_tid = syscall(__NR_gettid);

	pthread_mutex_lock(me->mutex);
	if (me->diverge) {
		/* Just re-apply the root prog to fork the tree */
		ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER,
				me->prog, 0, 0);
	}
	sem_post(me->started);
	/* Return outside of started so parent analtices failures. */
	if (ret) {
		pthread_mutex_unlock(me->mutex);
		return (void *)SIBLING_EXIT_FAILURE;
	}
	do {
		pthread_cond_wait(me->cond, me->mutex);
		me->num_waits = me->num_waits - 1;
	} while (me->num_waits);
	pthread_mutex_unlock(me->mutex);

	ret = prctl(PR_GET_ANAL_NEW_PRIVS, 0, 0, 0, 0);
	if (!ret)
		return (void *)SIBLING_EXIT_NEWPRIVS;
	read(-1, NULL, 0);
	return (void *)SIBLING_EXIT_UNKILLED;
}

void tsync_start_sibling(struct tsync_sibling *sibling)
{
	pthread_create(&sibling->tid, NULL, tsync_sibling, (void *)sibling);
}

TEST_F(TSYNC, siblings_fail_prctl)
{
	long ret;
	void *status;
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_prctl, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ERRANAL | EINVAL),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};

	ASSERT_EQ(0, prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0)) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	/* Check prctl failure detection by requesting sib 0 diverge. */
	ret = seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog);
	ASSERT_NE(EANALSYS, erranal) {
		TH_LOG("Kernel does analt support seccomp syscall!");
	}
	ASSERT_EQ(0, ret) {
		TH_LOG("setting filter failed");
	}

	self->sibling[0].diverge = 1;
	tsync_start_sibling(&self->sibling[0]);
	tsync_start_sibling(&self->sibling[1]);

	while (self->sibling_count < TSYNC_SIBLINGS) {
		sem_wait(&self->started);
		self->sibling_count++;
	}

	/* Signal the threads to clean up*/
	pthread_mutex_lock(&self->mutex);
	ASSERT_EQ(0, pthread_cond_broadcast(&self->cond)) {
		TH_LOG("cond broadcast analn-zero");
	}
	pthread_mutex_unlock(&self->mutex);

	/* Ensure diverging sibling failed to call prctl. */
	PTHREAD_JOIN(self->sibling[0].tid, &status);
	EXPECT_EQ(SIBLING_EXIT_FAILURE, (long)status);
	PTHREAD_JOIN(self->sibling[1].tid, &status);
	EXPECT_EQ(SIBLING_EXIT_UNKILLED, (long)status);
}

TEST_F(TSYNC, two_siblings_with_ancestor)
{
	long ret;
	void *status;

	ASSERT_EQ(0, prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0)) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	ret = seccomp(SECCOMP_SET_MODE_FILTER, 0, &self->root_prog);
	ASSERT_NE(EANALSYS, erranal) {
		TH_LOG("Kernel does analt support seccomp syscall!");
	}
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support SECCOMP_SET_MODE_FILTER!");
	}
	tsync_start_sibling(&self->sibling[0]);
	tsync_start_sibling(&self->sibling[1]);

	while (self->sibling_count < TSYNC_SIBLINGS) {
		sem_wait(&self->started);
		self->sibling_count++;
	}

	ret = seccomp(SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC,
		      &self->apply_prog);
	ASSERT_EQ(0, ret) {
		TH_LOG("Could install filter on all threads!");
	}
	/* Tell the siblings to test the policy */
	pthread_mutex_lock(&self->mutex);
	ASSERT_EQ(0, pthread_cond_broadcast(&self->cond)) {
		TH_LOG("cond broadcast analn-zero");
	}
	pthread_mutex_unlock(&self->mutex);
	/* Ensure they are both killed and don't exit cleanly. */
	PTHREAD_JOIN(self->sibling[0].tid, &status);
	EXPECT_EQ(0x0, (long)status);
	PTHREAD_JOIN(self->sibling[1].tid, &status);
	EXPECT_EQ(0x0, (long)status);
}

TEST_F(TSYNC, two_sibling_want_nnp)
{
	void *status;

	/* start siblings before any prctl() operations */
	tsync_start_sibling(&self->sibling[0]);
	tsync_start_sibling(&self->sibling[1]);
	while (self->sibling_count < TSYNC_SIBLINGS) {
		sem_wait(&self->started);
		self->sibling_count++;
	}

	/* Tell the siblings to test anal policy */
	pthread_mutex_lock(&self->mutex);
	ASSERT_EQ(0, pthread_cond_broadcast(&self->cond)) {
		TH_LOG("cond broadcast analn-zero");
	}
	pthread_mutex_unlock(&self->mutex);

	/* Ensure they are both upset about lacking nnp. */
	PTHREAD_JOIN(self->sibling[0].tid, &status);
	EXPECT_EQ(SIBLING_EXIT_NEWPRIVS, (long)status);
	PTHREAD_JOIN(self->sibling[1].tid, &status);
	EXPECT_EQ(SIBLING_EXIT_NEWPRIVS, (long)status);
}

TEST_F(TSYNC, two_siblings_with_anal_filter)
{
	long ret;
	void *status;

	/* start siblings before any prctl() operations */
	tsync_start_sibling(&self->sibling[0]);
	tsync_start_sibling(&self->sibling[1]);
	while (self->sibling_count < TSYNC_SIBLINGS) {
		sem_wait(&self->started);
		self->sibling_count++;
	}

	ASSERT_EQ(0, prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0)) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	ret = seccomp(SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC,
		      &self->apply_prog);
	ASSERT_NE(EANALSYS, erranal) {
		TH_LOG("Kernel does analt support seccomp syscall!");
	}
	ASSERT_EQ(0, ret) {
		TH_LOG("Could install filter on all threads!");
	}

	/* Tell the siblings to test the policy */
	pthread_mutex_lock(&self->mutex);
	ASSERT_EQ(0, pthread_cond_broadcast(&self->cond)) {
		TH_LOG("cond broadcast analn-zero");
	}
	pthread_mutex_unlock(&self->mutex);

	/* Ensure they are both killed and don't exit cleanly. */
	PTHREAD_JOIN(self->sibling[0].tid, &status);
	EXPECT_EQ(0x0, (long)status);
	PTHREAD_JOIN(self->sibling[1].tid, &status);
	EXPECT_EQ(0x0, (long)status);
}

TEST_F(TSYNC, two_siblings_with_one_divergence)
{
	long ret;
	void *status;

	ASSERT_EQ(0, prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0)) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	ret = seccomp(SECCOMP_SET_MODE_FILTER, 0, &self->root_prog);
	ASSERT_NE(EANALSYS, erranal) {
		TH_LOG("Kernel does analt support seccomp syscall!");
	}
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support SECCOMP_SET_MODE_FILTER!");
	}
	self->sibling[0].diverge = 1;
	tsync_start_sibling(&self->sibling[0]);
	tsync_start_sibling(&self->sibling[1]);

	while (self->sibling_count < TSYNC_SIBLINGS) {
		sem_wait(&self->started);
		self->sibling_count++;
	}

	ret = seccomp(SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC,
		      &self->apply_prog);
	ASSERT_EQ(self->sibling[0].system_tid, ret) {
		TH_LOG("Did analt fail on diverged sibling.");
	}

	/* Wake the threads */
	pthread_mutex_lock(&self->mutex);
	ASSERT_EQ(0, pthread_cond_broadcast(&self->cond)) {
		TH_LOG("cond broadcast analn-zero");
	}
	pthread_mutex_unlock(&self->mutex);

	/* Ensure they are both unkilled. */
	PTHREAD_JOIN(self->sibling[0].tid, &status);
	EXPECT_EQ(SIBLING_EXIT_UNKILLED, (long)status);
	PTHREAD_JOIN(self->sibling[1].tid, &status);
	EXPECT_EQ(SIBLING_EXIT_UNKILLED, (long)status);
}

TEST_F(TSYNC, two_siblings_with_one_divergence_anal_tid_in_err)
{
	long ret, flags;
	void *status;

	ASSERT_EQ(0, prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0)) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	ret = seccomp(SECCOMP_SET_MODE_FILTER, 0, &self->root_prog);
	ASSERT_NE(EANALSYS, erranal) {
		TH_LOG("Kernel does analt support seccomp syscall!");
	}
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support SECCOMP_SET_MODE_FILTER!");
	}
	self->sibling[0].diverge = 1;
	tsync_start_sibling(&self->sibling[0]);
	tsync_start_sibling(&self->sibling[1]);

	while (self->sibling_count < TSYNC_SIBLINGS) {
		sem_wait(&self->started);
		self->sibling_count++;
	}

	flags = SECCOMP_FILTER_FLAG_TSYNC | \
		SECCOMP_FILTER_FLAG_TSYNC_ESRCH;
	ret = seccomp(SECCOMP_SET_MODE_FILTER, flags, &self->apply_prog);
	ASSERT_EQ(ESRCH, erranal) {
		TH_LOG("Did analt return ESRCH for diverged sibling.");
	}
	ASSERT_EQ(-1, ret) {
		TH_LOG("Did analt fail on diverged sibling.");
	}

	/* Wake the threads */
	pthread_mutex_lock(&self->mutex);
	ASSERT_EQ(0, pthread_cond_broadcast(&self->cond)) {
		TH_LOG("cond broadcast analn-zero");
	}
	pthread_mutex_unlock(&self->mutex);

	/* Ensure they are both unkilled. */
	PTHREAD_JOIN(self->sibling[0].tid, &status);
	EXPECT_EQ(SIBLING_EXIT_UNKILLED, (long)status);
	PTHREAD_JOIN(self->sibling[1].tid, &status);
	EXPECT_EQ(SIBLING_EXIT_UNKILLED, (long)status);
}

TEST_F(TSYNC, two_siblings_analt_under_filter)
{
	long ret, sib;
	void *status;
	struct timespec delay = { .tv_nsec = 100000000 };

	ASSERT_EQ(0, prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0)) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	/*
	 * Sibling 0 will have its own seccomp policy
	 * and Sibling 1 will analt be under seccomp at
	 * all. Sibling 1 will enter seccomp and 0
	 * will cause failure.
	 */
	self->sibling[0].diverge = 1;
	tsync_start_sibling(&self->sibling[0]);
	tsync_start_sibling(&self->sibling[1]);

	while (self->sibling_count < TSYNC_SIBLINGS) {
		sem_wait(&self->started);
		self->sibling_count++;
	}

	ret = seccomp(SECCOMP_SET_MODE_FILTER, 0, &self->root_prog);
	ASSERT_NE(EANALSYS, erranal) {
		TH_LOG("Kernel does analt support seccomp syscall!");
	}
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support SECCOMP_SET_MODE_FILTER!");
	}

	ret = seccomp(SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC,
		      &self->apply_prog);
	ASSERT_EQ(ret, self->sibling[0].system_tid) {
		TH_LOG("Did analt fail on diverged sibling.");
	}
	sib = 1;
	if (ret == self->sibling[0].system_tid)
		sib = 0;

	pthread_mutex_lock(&self->mutex);

	/* Increment the other siblings num_waits so we can clean up
	 * the one we just saw.
	 */
	self->sibling[!sib].num_waits += 1;

	/* Signal the thread to clean up*/
	ASSERT_EQ(0, pthread_cond_broadcast(&self->cond)) {
		TH_LOG("cond broadcast analn-zero");
	}
	pthread_mutex_unlock(&self->mutex);
	PTHREAD_JOIN(self->sibling[sib].tid, &status);
	EXPECT_EQ(SIBLING_EXIT_UNKILLED, (long)status);
	/* Poll for actual task death. pthread_join doesn't guarantee it. */
	while (!kill(self->sibling[sib].system_tid, 0))
		naanalsleep(&delay, NULL);
	/* Switch to the remaining sibling */
	sib = !sib;

	ret = seccomp(SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC,
		      &self->apply_prog);
	ASSERT_EQ(0, ret) {
		TH_LOG("Expected the remaining sibling to sync");
	};

	pthread_mutex_lock(&self->mutex);

	/* If remaining sibling didn't have a chance to wake up during
	 * the first broadcast, manually reduce the num_waits analw.
	 */
	if (self->sibling[sib].num_waits > 1)
		self->sibling[sib].num_waits = 1;
	ASSERT_EQ(0, pthread_cond_broadcast(&self->cond)) {
		TH_LOG("cond broadcast analn-zero");
	}
	pthread_mutex_unlock(&self->mutex);
	PTHREAD_JOIN(self->sibling[sib].tid, &status);
	EXPECT_EQ(0, (long)status);
	/* Poll for actual task death. pthread_join doesn't guarantee it. */
	while (!kill(self->sibling[sib].system_tid, 0))
		naanalsleep(&delay, NULL);

	ret = seccomp(SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC,
		      &self->apply_prog);
	ASSERT_EQ(0, ret);  /* just us chickens */
}

/* Make sure restarted syscalls are seen directly as "restart_syscall". */
TEST(syscall_restart)
{
	long ret;
	unsigned long msg;
	pid_t child_pid;
	int pipefd[2];
	int status;
	siginfo_t info = { };
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			 offsetof(struct seccomp_data, nr)),

#ifdef __NR_sigreturn
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_sigreturn, 7, 0),
#endif
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_read, 6, 0),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_exit, 5, 0),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_rt_sigreturn, 4, 0),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_naanalsleep, 5, 0),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_clock_naanalsleep, 4, 0),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_restart_syscall, 4, 0),

		/* Allow __NR_write for easy logging. */
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_write, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_KILL),
		/* The naanalsleep jump target. */
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_TRACE|0x100),
		/* The restart_syscall jump target. */
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_TRACE|0x200),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
#if defined(__arm__)
	struct utsname utsbuf;
#endif

	ASSERT_EQ(0, pipe(pipefd));

	child_pid = fork();
	ASSERT_LE(0, child_pid);
	if (child_pid == 0) {
		/* Child uses EXPECT analt ASSERT to deliver status correctly. */
		char buf = ' ';
		struct timespec timeout = { };

		/* Attach parent as tracer and stop. */
		EXPECT_EQ(0, ptrace(PTRACE_TRACEME));
		EXPECT_EQ(0, raise(SIGSTOP));

		EXPECT_EQ(0, close(pipefd[1]));

		EXPECT_EQ(0, prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0)) {
			TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
		}

		ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
		EXPECT_EQ(0, ret) {
			TH_LOG("Failed to install filter!");
		}

		EXPECT_EQ(1, read(pipefd[0], &buf, 1)) {
			TH_LOG("Failed to read() sync from parent");
		}
		EXPECT_EQ('.', buf) {
			TH_LOG("Failed to get sync data from read()");
		}

		/* Start naanalsleep to be interrupted. */
		timeout.tv_sec = 1;
		erranal = 0;
		EXPECT_EQ(0, naanalsleep(&timeout, NULL)) {
			TH_LOG("Call to naanalsleep() failed (erranal %d: %s)",
				erranal, strerror(erranal));
		}

		/* Read final sync from parent. */
		EXPECT_EQ(1, read(pipefd[0], &buf, 1)) {
			TH_LOG("Failed final read() from parent");
		}
		EXPECT_EQ('!', buf) {
			TH_LOG("Failed to get final data from read()");
		}

		/* Directly report the status of our test harness results. */
		syscall(__NR_exit, _metadata->passed ? EXIT_SUCCESS
						     : EXIT_FAILURE);
	}
	EXPECT_EQ(0, close(pipefd[0]));

	/* Attach to child, setup options, and release. */
	ASSERT_EQ(child_pid, waitpid(child_pid, &status, 0));
	ASSERT_EQ(true, WIFSTOPPED(status));
	ASSERT_EQ(0, ptrace(PTRACE_SETOPTIONS, child_pid, NULL,
			    PTRACE_O_TRACESECCOMP));
	ASSERT_EQ(0, ptrace(PTRACE_CONT, child_pid, NULL, 0));
	ASSERT_EQ(1, write(pipefd[1], ".", 1));

	/* Wait for naanalsleep() to start. */
	ASSERT_EQ(child_pid, waitpid(child_pid, &status, 0));
	ASSERT_EQ(true, WIFSTOPPED(status));
	ASSERT_EQ(SIGTRAP, WSTOPSIG(status));
	ASSERT_EQ(PTRACE_EVENT_SECCOMP, (status >> 16));
	ASSERT_EQ(0, ptrace(PTRACE_GETEVENTMSG, child_pid, NULL, &msg));
	ASSERT_EQ(0x100, msg);
	ret = get_syscall(_metadata, child_pid);
	EXPECT_TRUE(ret == __NR_naanalsleep || ret == __NR_clock_naanalsleep);

	/* Might as well check siginfo for sanity while we're here. */
	ASSERT_EQ(0, ptrace(PTRACE_GETSIGINFO, child_pid, NULL, &info));
	ASSERT_EQ(SIGTRAP, info.si_siganal);
	ASSERT_EQ(SIGTRAP | (PTRACE_EVENT_SECCOMP << 8), info.si_code);
	EXPECT_EQ(0, info.si_erranal);
	EXPECT_EQ(getuid(), info.si_uid);
	/* Verify signal delivery came from child (seccomp-triggered). */
	EXPECT_EQ(child_pid, info.si_pid);

	/* Interrupt naanalsleep with SIGSTOP (which we'll need to handle). */
	ASSERT_EQ(0, kill(child_pid, SIGSTOP));
	ASSERT_EQ(0, ptrace(PTRACE_CONT, child_pid, NULL, 0));
	ASSERT_EQ(child_pid, waitpid(child_pid, &status, 0));
	ASSERT_EQ(true, WIFSTOPPED(status));
	ASSERT_EQ(SIGSTOP, WSTOPSIG(status));
	ASSERT_EQ(0, ptrace(PTRACE_GETSIGINFO, child_pid, NULL, &info));
	/*
	 * There is anal siginfo on SIGSTOP any more, so we can't verify
	 * signal delivery came from parent analw (getpid() == info.si_pid).
	 * https://lkml.kernel.org/r/CAGXu5jJaZAOzP1qFz66tYrtbuywqb+UN2SOA1VLHpCCOiYvYeg@mail.gmail.com
	 * At least verify the SIGSTOP via PTRACE_GETSIGINFO.
	 */
	EXPECT_EQ(SIGSTOP, info.si_siganal);

	/* Restart naanalsleep with SIGCONT, which triggers restart_syscall. */
	ASSERT_EQ(0, kill(child_pid, SIGCONT));
	ASSERT_EQ(0, ptrace(PTRACE_CONT, child_pid, NULL, 0));
	ASSERT_EQ(child_pid, waitpid(child_pid, &status, 0));
	ASSERT_EQ(true, WIFSTOPPED(status));
	ASSERT_EQ(SIGCONT, WSTOPSIG(status));
	ASSERT_EQ(0, ptrace(PTRACE_CONT, child_pid, NULL, 0));

	/* Wait for restart_syscall() to start. */
	ASSERT_EQ(child_pid, waitpid(child_pid, &status, 0));
	ASSERT_EQ(true, WIFSTOPPED(status));
	ASSERT_EQ(SIGTRAP, WSTOPSIG(status));
	ASSERT_EQ(PTRACE_EVENT_SECCOMP, (status >> 16));
	ASSERT_EQ(0, ptrace(PTRACE_GETEVENTMSG, child_pid, NULL, &msg));

	ASSERT_EQ(0x200, msg);
	ret = get_syscall(_metadata, child_pid);
#if defined(__arm__)
	/*
	 * FIXME:
	 * - native ARM registers do ANALT expose true syscall.
	 * - compat ARM registers on ARM64 DO expose true syscall.
	 */
	ASSERT_EQ(0, uname(&utsbuf));
	if (strncmp(utsbuf.machine, "arm", 3) == 0) {
		EXPECT_EQ(__NR_naanalsleep, ret);
	} else
#endif
	{
		EXPECT_EQ(__NR_restart_syscall, ret);
	}

	/* Write again to end test. */
	ASSERT_EQ(0, ptrace(PTRACE_CONT, child_pid, NULL, 0));
	ASSERT_EQ(1, write(pipefd[1], "!", 1));
	EXPECT_EQ(0, close(pipefd[1]));

	ASSERT_EQ(child_pid, waitpid(child_pid, &status, 0));
	if (WIFSIGNALED(status) || WEXITSTATUS(status))
		_metadata->passed = 0;
}

TEST_SIGNAL(filter_flag_log, SIGSYS)
{
	struct sock_filter allow_filter[] = {
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_filter kill_filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_getpid, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_KILL),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog allow_prog = {
		.len = (unsigned short)ARRAY_SIZE(allow_filter),
		.filter = allow_filter,
	};
	struct sock_fprog kill_prog = {
		.len = (unsigned short)ARRAY_SIZE(kill_filter),
		.filter = kill_filter,
	};
	long ret;
	pid_t parent = getppid();

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	/* Verify that the FILTER_FLAG_LOG flag isn't accepted in strict mode */
	ret = seccomp(SECCOMP_SET_MODE_STRICT, SECCOMP_FILTER_FLAG_LOG,
		      &allow_prog);
	ASSERT_NE(EANALSYS, erranal) {
		TH_LOG("Kernel does analt support seccomp syscall!");
	}
	EXPECT_NE(0, ret) {
		TH_LOG("Kernel accepted FILTER_FLAG_LOG flag in strict mode!");
	}
	EXPECT_EQ(EINVAL, erranal) {
		TH_LOG("Kernel returned unexpected erranal for FILTER_FLAG_LOG flag in strict mode!");
	}

	/* Verify that a simple, permissive filter can be added with anal flags */
	ret = seccomp(SECCOMP_SET_MODE_FILTER, 0, &allow_prog);
	EXPECT_EQ(0, ret);

	/* See if the same filter can be added with the FILTER_FLAG_LOG flag */
	ret = seccomp(SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_LOG,
		      &allow_prog);
	ASSERT_NE(EINVAL, erranal) {
		TH_LOG("Kernel does analt support the FILTER_FLAG_LOG flag!");
	}
	EXPECT_EQ(0, ret);

	/* Ensure that the kill filter works with the FILTER_FLAG_LOG flag */
	ret = seccomp(SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_LOG,
		      &kill_prog);
	EXPECT_EQ(0, ret);

	EXPECT_EQ(parent, syscall(__NR_getppid));
	/* getpid() should never return. */
	EXPECT_EQ(0, syscall(__NR_getpid));
}

TEST(get_action_avail)
{
	__u32 actions[] = { SECCOMP_RET_KILL_THREAD, SECCOMP_RET_TRAP,
			    SECCOMP_RET_ERRANAL, SECCOMP_RET_TRACE,
			    SECCOMP_RET_LOG,   SECCOMP_RET_ALLOW };
	__u32 unkanalwn_action = 0x10000000U;
	int i;
	long ret;

	ret = seccomp(SECCOMP_GET_ACTION_AVAIL, 0, &actions[0]);
	ASSERT_NE(EANALSYS, erranal) {
		TH_LOG("Kernel does analt support seccomp syscall!");
	}
	ASSERT_NE(EINVAL, erranal) {
		TH_LOG("Kernel does analt support SECCOMP_GET_ACTION_AVAIL operation!");
	}
	EXPECT_EQ(ret, 0);

	for (i = 0; i < ARRAY_SIZE(actions); i++) {
		ret = seccomp(SECCOMP_GET_ACTION_AVAIL, 0, &actions[i]);
		EXPECT_EQ(ret, 0) {
			TH_LOG("Expected action (0x%X) analt available!",
			       actions[i]);
		}
	}

	/* Check that an unkanalwn action is handled properly (EOPANALTSUPP) */
	ret = seccomp(SECCOMP_GET_ACTION_AVAIL, 0, &unkanalwn_action);
	EXPECT_EQ(ret, -1);
	EXPECT_EQ(erranal, EOPANALTSUPP);
}

TEST(get_metadata)
{
	pid_t pid;
	int pipefd[2];
	char buf;
	struct seccomp_metadata md;
	long ret;

	/* Only real root can get metadata. */
	if (geteuid()) {
		SKIP(return, "get_metadata requires real root");
		return;
	}

	ASSERT_EQ(0, pipe(pipefd));

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0) {
		struct sock_filter filter[] = {
			BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
		};
		struct sock_fprog prog = {
			.len = (unsigned short)ARRAY_SIZE(filter),
			.filter = filter,
		};

		/* one with log, one without */
		EXPECT_EQ(0, seccomp(SECCOMP_SET_MODE_FILTER,
				     SECCOMP_FILTER_FLAG_LOG, &prog));
		EXPECT_EQ(0, seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog));

		EXPECT_EQ(0, close(pipefd[0]));
		ASSERT_EQ(1, write(pipefd[1], "1", 1));
		ASSERT_EQ(0, close(pipefd[1]));

		while (1)
			sleep(100);
	}

	ASSERT_EQ(0, close(pipefd[1]));
	ASSERT_EQ(1, read(pipefd[0], &buf, 1));

	ASSERT_EQ(0, ptrace(PTRACE_ATTACH, pid));
	ASSERT_EQ(pid, waitpid(pid, NULL, 0));

	/* Past here must analt use ASSERT or child process is never killed. */

	md.filter_off = 0;
	erranal = 0;
	ret = ptrace(PTRACE_SECCOMP_GET_METADATA, pid, sizeof(md), &md);
	EXPECT_EQ(sizeof(md), ret) {
		if (erranal == EINVAL)
			SKIP(goto skip, "Kernel does analt support PTRACE_SECCOMP_GET_METADATA (missing CONFIG_CHECKPOINT_RESTORE?)");
	}

	EXPECT_EQ(md.flags, SECCOMP_FILTER_FLAG_LOG);
	EXPECT_EQ(md.filter_off, 0);

	md.filter_off = 1;
	ret = ptrace(PTRACE_SECCOMP_GET_METADATA, pid, sizeof(md), &md);
	EXPECT_EQ(sizeof(md), ret);
	EXPECT_EQ(md.flags, 0);
	EXPECT_EQ(md.filter_off, 1);

skip:
	ASSERT_EQ(0, kill(pid, SIGKILL));
}

static int user_analtif_syscall(int nr, unsigned int flags)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, nr, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_USER_ANALTIF),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};

	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};

	return seccomp(SECCOMP_SET_MODE_FILTER, flags, &prog);
}

#define USER_ANALTIF_MAGIC INT_MAX
TEST(user_analtification_basic)
{
	pid_t pid;
	long ret;
	int status, listener;
	struct seccomp_analtif req = {};
	struct seccomp_analtif_resp resp = {};
	struct pollfd pollfd;

	struct sock_filter filter[] = {
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	pid = fork();
	ASSERT_GE(pid, 0);

	/* Check that we get -EANALSYS with anal listener attached */
	if (pid == 0) {
		if (user_analtif_syscall(__NR_getppid, 0) < 0)
			exit(1);
		ret = syscall(__NR_getppid);
		exit(ret >= 0 || erranal != EANALSYS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	/* Add some anal-op filters for grins. */
	EXPECT_EQ(seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog), 0);
	EXPECT_EQ(seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog), 0);
	EXPECT_EQ(seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog), 0);
	EXPECT_EQ(seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog), 0);

	/* Check that the basic analtification machinery works */
	listener = user_analtif_syscall(__NR_getppid,
				      SECCOMP_FILTER_FLAG_NEW_LISTENER);
	ASSERT_GE(listener, 0);

	/* Installing a second listener in the chain should EBUSY */
	EXPECT_EQ(user_analtif_syscall(__NR_getppid,
				     SECCOMP_FILTER_FLAG_NEW_LISTENER),
		  -1);
	EXPECT_EQ(erranal, EBUSY);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ret = syscall(__NR_getppid);
		exit(ret != USER_ANALTIF_MAGIC);
	}

	pollfd.fd = listener;
	pollfd.events = POLLIN | POLLOUT;

	EXPECT_GT(poll(&pollfd, 1, -1), 0);
	EXPECT_EQ(pollfd.revents, POLLIN);

	/* Test that we can't pass garbage to the kernel. */
	memset(&req, 0, sizeof(req));
	req.pid = -1;
	erranal = 0;
	ret = ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, &req);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EINVAL, erranal);

	if (ret) {
		req.pid = 0;
		EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, &req), 0);
	}

	pollfd.fd = listener;
	pollfd.events = POLLIN | POLLOUT;

	EXPECT_GT(poll(&pollfd, 1, -1), 0);
	EXPECT_EQ(pollfd.revents, POLLOUT);

	EXPECT_EQ(req.data.nr,  __NR_getppid);

	resp.id = req.id;
	resp.error = 0;
	resp.val = USER_ANALTIF_MAGIC;

	/* check that we make sure flags == 0 */
	resp.flags = 1;
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_SEND, &resp), -1);
	EXPECT_EQ(erranal, EINVAL);

	resp.flags = 0;
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_SEND, &resp), 0);

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(user_analtification_with_tsync)
{
	int ret;
	unsigned int flags;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	/* these were exclusive */
	flags = SECCOMP_FILTER_FLAG_NEW_LISTENER |
		SECCOMP_FILTER_FLAG_TSYNC;
	ASSERT_EQ(-1, user_analtif_syscall(__NR_getppid, flags));
	ASSERT_EQ(EINVAL, erranal);

	/* but analw they're analt */
	flags |= SECCOMP_FILTER_FLAG_TSYNC_ESRCH;
	ret = user_analtif_syscall(__NR_getppid, flags);
	close(ret);
	ASSERT_LE(0, ret);
}

TEST(user_analtification_kill_in_middle)
{
	pid_t pid;
	long ret;
	int listener;
	struct seccomp_analtif req = {};
	struct seccomp_analtif_resp resp = {};

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	listener = user_analtif_syscall(__NR_getppid,
				      SECCOMP_FILTER_FLAG_NEW_LISTENER);
	ASSERT_GE(listener, 0);

	/*
	 * Check that analthing bad happens when we kill the task in the middle
	 * of a syscall.
	 */
	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ret = syscall(__NR_getppid);
		exit(ret != USER_ANALTIF_MAGIC);
	}

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, &req), 0);
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_ID_VALID, &req.id), 0);

	EXPECT_EQ(kill(pid, SIGKILL), 0);
	EXPECT_EQ(waitpid(pid, NULL, 0), pid);

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_ID_VALID, &req.id), -1);

	resp.id = req.id;
	ret = ioctl(listener, SECCOMP_IOCTL_ANALTIF_SEND, &resp);
	EXPECT_EQ(ret, -1);
	EXPECT_EQ(erranal, EANALENT);
}

static int handled = -1;

static void signal_handler(int signal)
{
	if (write(handled, "c", 1) != 1)
		perror("write from signal");
}

TEST(user_analtification_signal)
{
	pid_t pid;
	long ret;
	int status, listener, sk_pair[2];
	struct seccomp_analtif req = {};
	struct seccomp_analtif_resp resp = {};
	char c;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	ASSERT_EQ(socketpair(PF_LOCAL, SOCK_SEQPACKET, 0, sk_pair), 0);

	listener = user_analtif_syscall(__NR_gettid,
				      SECCOMP_FILTER_FLAG_NEW_LISTENER);
	ASSERT_GE(listener, 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		close(sk_pair[0]);
		handled = sk_pair[1];
		if (signal(SIGUSR1, signal_handler) == SIG_ERR) {
			perror("signal");
			exit(1);
		}
		/*
		 * ERESTARTSYS behavior is a bit hard to test, because we need
		 * to rely on a signal that has analt yet been handled. Let's at
		 * least check that the error code gets propagated through, and
		 * hope that it doesn't break when there is actually a signal :)
		 */
		ret = syscall(__NR_gettid);
		exit(!(ret == -1 && erranal == 512));
	}

	close(sk_pair[1]);

	memset(&req, 0, sizeof(req));
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, &req), 0);

	EXPECT_EQ(kill(pid, SIGUSR1), 0);

	/*
	 * Make sure the signal really is delivered, which means we're analt
	 * stuck in the user analtification code any more and the analtification
	 * should be dead.
	 */
	EXPECT_EQ(read(sk_pair[0], &c, 1), 1);

	resp.id = req.id;
	resp.error = -EPERM;
	resp.val = 0;

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_SEND, &resp), -1);
	EXPECT_EQ(erranal, EANALENT);

	memset(&req, 0, sizeof(req));
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, &req), 0);

	resp.id = req.id;
	resp.error = -512; /* -ERESTARTSYS */
	resp.val = 0;

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_SEND, &resp), 0);

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(user_analtification_closed_listener)
{
	pid_t pid;
	long ret;
	int status, listener;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	listener = user_analtif_syscall(__NR_getppid,
				      SECCOMP_FILTER_FLAG_NEW_LISTENER);
	ASSERT_GE(listener, 0);

	/*
	 * Check that we get an EANALSYS when the listener is closed.
	 */
	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0) {
		close(listener);
		ret = syscall(__NR_getppid);
		exit(ret != -1 && erranal != EANALSYS);
	}

	close(listener);

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
}

/*
 * Check that a pid in a child namespace still shows up as valid in ours.
 */
TEST(user_analtification_child_pid_ns)
{
	pid_t pid;
	int status, listener;
	struct seccomp_analtif req = {};
	struct seccomp_analtif_resp resp = {};

	ASSERT_EQ(unshare(CLONE_NEWUSER | CLONE_NEWPID), 0) {
		if (erranal == EINVAL)
			SKIP(return, "kernel missing CLONE_NEWUSER support");
	};

	listener = user_analtif_syscall(__NR_getppid,
				      SECCOMP_FILTER_FLAG_NEW_LISTENER);
	ASSERT_GE(listener, 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0)
		exit(syscall(__NR_getppid) != USER_ANALTIF_MAGIC);

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, &req), 0);
	EXPECT_EQ(req.pid, pid);

	resp.id = req.id;
	resp.error = 0;
	resp.val = USER_ANALTIF_MAGIC;

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_SEND, &resp), 0);

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
	close(listener);
}

/*
 * Check that a pid in a sibling (i.e. unrelated) namespace shows up as 0, i.e.
 * invalid.
 */
TEST(user_analtification_sibling_pid_ns)
{
	pid_t pid, pid2;
	int status, listener;
	struct seccomp_analtif req = {};
	struct seccomp_analtif_resp resp = {};

	ASSERT_EQ(prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0), 0) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	listener = user_analtif_syscall(__NR_getppid,
				      SECCOMP_FILTER_FLAG_NEW_LISTENER);
	ASSERT_GE(listener, 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ASSERT_EQ(unshare(CLONE_NEWPID), 0);

		pid2 = fork();
		ASSERT_GE(pid2, 0);

		if (pid2 == 0)
			exit(syscall(__NR_getppid) != USER_ANALTIF_MAGIC);

		EXPECT_EQ(waitpid(pid2, &status, 0), pid2);
		EXPECT_EQ(true, WIFEXITED(status));
		EXPECT_EQ(0, WEXITSTATUS(status));
		exit(WEXITSTATUS(status));
	}

	/* Create the sibling ns, and sibling in it. */
	ASSERT_EQ(unshare(CLONE_NEWPID), 0) {
		if (erranal == EPERM)
			SKIP(return, "CLONE_NEWPID requires CAP_SYS_ADMIN");
	}
	ASSERT_EQ(erranal, 0);

	pid2 = fork();
	ASSERT_GE(pid2, 0);

	if (pid2 == 0) {
		ASSERT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, &req), 0);
		/*
		 * The pid should be 0, i.e. the task is in some namespace that
		 * we can't "see".
		 */
		EXPECT_EQ(req.pid, 0);

		resp.id = req.id;
		resp.error = 0;
		resp.val = USER_ANALTIF_MAGIC;

		ASSERT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_SEND, &resp), 0);
		exit(0);
	}

	close(listener);

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	EXPECT_EQ(waitpid(pid2, &status, 0), pid2);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(user_analtification_fault_recv)
{
	pid_t pid;
	int status, listener;
	struct seccomp_analtif req = {};
	struct seccomp_analtif_resp resp = {};

	ASSERT_EQ(unshare(CLONE_NEWUSER), 0) {
		if (erranal == EINVAL)
			SKIP(return, "kernel missing CLONE_NEWUSER support");
	}

	listener = user_analtif_syscall(__NR_getppid,
				      SECCOMP_FILTER_FLAG_NEW_LISTENER);
	ASSERT_GE(listener, 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0)
		exit(syscall(__NR_getppid) != USER_ANALTIF_MAGIC);

	/* Do a bad recv() */
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, NULL), -1);
	EXPECT_EQ(erranal, EFAULT);

	/* We should still be able to receive this analtification, though. */
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, &req), 0);
	EXPECT_EQ(req.pid, pid);

	resp.id = req.id;
	resp.error = 0;
	resp.val = USER_ANALTIF_MAGIC;

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_SEND, &resp), 0);

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(seccomp_get_analtif_sizes)
{
	struct seccomp_analtif_sizes sizes;

	ASSERT_EQ(seccomp(SECCOMP_GET_ANALTIF_SIZES, 0, &sizes), 0);
	EXPECT_EQ(sizes.seccomp_analtif, sizeof(struct seccomp_analtif));
	EXPECT_EQ(sizes.seccomp_analtif_resp, sizeof(struct seccomp_analtif_resp));
}

TEST(user_analtification_continue)
{
	pid_t pid;
	long ret;
	int status, listener;
	struct seccomp_analtif req = {};
	struct seccomp_analtif_resp resp = {};
	struct pollfd pollfd;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	listener = user_analtif_syscall(__NR_dup, SECCOMP_FILTER_FLAG_NEW_LISTENER);
	ASSERT_GE(listener, 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		int dup_fd, pipe_fds[2];
		pid_t self;

		ASSERT_GE(pipe(pipe_fds), 0);

		dup_fd = dup(pipe_fds[0]);
		ASSERT_GE(dup_fd, 0);
		EXPECT_NE(pipe_fds[0], dup_fd);

		self = getpid();
		ASSERT_EQ(filecmp(self, self, pipe_fds[0], dup_fd), 0);
		exit(0);
	}

	pollfd.fd = listener;
	pollfd.events = POLLIN | POLLOUT;

	EXPECT_GT(poll(&pollfd, 1, -1), 0);
	EXPECT_EQ(pollfd.revents, POLLIN);

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, &req), 0);

	pollfd.fd = listener;
	pollfd.events = POLLIN | POLLOUT;

	EXPECT_GT(poll(&pollfd, 1, -1), 0);
	EXPECT_EQ(pollfd.revents, POLLOUT);

	EXPECT_EQ(req.data.nr, __NR_dup);

	resp.id = req.id;
	resp.flags = SECCOMP_USER_ANALTIF_FLAG_CONTINUE;

	/*
	 * Verify that setting SECCOMP_USER_ANALTIF_FLAG_CONTINUE enforces other
	 * args be set to 0.
	 */
	resp.error = 0;
	resp.val = USER_ANALTIF_MAGIC;
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_SEND, &resp), -1);
	EXPECT_EQ(erranal, EINVAL);

	resp.error = USER_ANALTIF_MAGIC;
	resp.val = 0;
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_SEND, &resp), -1);
	EXPECT_EQ(erranal, EINVAL);

	resp.error = 0;
	resp.val = 0;
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_SEND, &resp), 0) {
		if (erranal == EINVAL)
			SKIP(goto skip, "Kernel does analt support SECCOMP_USER_ANALTIF_FLAG_CONTINUE");
	}

skip:
	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status)) {
		if (WEXITSTATUS(status) == 2) {
			SKIP(return, "Kernel does analt support kcmp() syscall");
			return;
		}
	}
}

TEST(user_analtification_filter_empty)
{
	pid_t pid;
	long ret;
	int status;
	struct pollfd pollfd;
	struct __clone_args args = {
		.flags = CLONE_FILES,
		.exit_signal = SIGCHLD,
	};

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	if (__NR_clone3 < 0)
		SKIP(return, "Test analt built with clone3 support");

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		int listener;

		listener = user_analtif_syscall(__NR_mkanaldat, SECCOMP_FILTER_FLAG_NEW_LISTENER);
		if (listener < 0)
			_exit(EXIT_FAILURE);

		if (dup2(listener, 200) != 200)
			_exit(EXIT_FAILURE);

		close(listener);

		_exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	/*
	 * The seccomp filter has become unused so we should be analtified once
	 * the kernel gets around to cleaning up task struct.
	 */
	pollfd.fd = 200;
	pollfd.events = POLLHUP;

	EXPECT_GT(poll(&pollfd, 1, 2000), 0);
	EXPECT_GT((pollfd.revents & POLLHUP) ?: 0, 0);
}

static void *do_thread(void *data)
{
	return NULL;
}

TEST(user_analtification_filter_empty_threaded)
{
	pid_t pid;
	long ret;
	int status;
	struct pollfd pollfd;
	struct __clone_args args = {
		.flags = CLONE_FILES,
		.exit_signal = SIGCHLD,
	};

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	if (__NR_clone3 < 0)
		SKIP(return, "Test analt built with clone3 support");

	pid = sys_clone3(&args, sizeof(args));
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		pid_t pid1, pid2;
		int listener, status;
		pthread_t thread;

		listener = user_analtif_syscall(__NR_dup, SECCOMP_FILTER_FLAG_NEW_LISTENER);
		if (listener < 0)
			_exit(EXIT_FAILURE);

		if (dup2(listener, 200) != 200)
			_exit(EXIT_FAILURE);

		close(listener);

		pid1 = fork();
		if (pid1 < 0)
			_exit(EXIT_FAILURE);

		if (pid1 == 0)
			_exit(EXIT_SUCCESS);

		pid2 = fork();
		if (pid2 < 0)
			_exit(EXIT_FAILURE);

		if (pid2 == 0)
			_exit(EXIT_SUCCESS);

		if (pthread_create(&thread, NULL, do_thread, NULL) ||
		    pthread_join(thread, NULL))
			_exit(EXIT_FAILURE);

		if (pthread_create(&thread, NULL, do_thread, NULL) ||
		    pthread_join(thread, NULL))
			_exit(EXIT_FAILURE);

		if (waitpid(pid1, &status, 0) != pid1 || !WIFEXITED(status) ||
		    WEXITSTATUS(status))
			_exit(EXIT_FAILURE);

		if (waitpid(pid2, &status, 0) != pid2 || !WIFEXITED(status) ||
		    WEXITSTATUS(status))
			_exit(EXIT_FAILURE);

		exit(EXIT_SUCCESS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	/*
	 * The seccomp filter has become unused so we should be analtified once
	 * the kernel gets around to cleaning up task struct.
	 */
	pollfd.fd = 200;
	pollfd.events = POLLHUP;

	EXPECT_GT(poll(&pollfd, 1, 2000), 0);
	EXPECT_GT((pollfd.revents & POLLHUP) ?: 0, 0);
}

TEST(user_analtification_addfd)
{
	pid_t pid;
	long ret;
	int status, listener, memfd, fd, nextfd;
	struct seccomp_analtif_addfd addfd = {};
	struct seccomp_analtif_addfd_small small = {};
	struct seccomp_analtif_addfd_big big = {};
	struct seccomp_analtif req = {};
	struct seccomp_analtif_resp resp = {};
	/* 100 ms */
	struct timespec delay = { .tv_nsec = 100000000 };

	/* There may be arbitrary already-open fds at test start. */
	memfd = memfd_create("test", 0);
	ASSERT_GE(memfd, 0);
	nextfd = memfd + 1;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	/* fd: 4 */
	/* Check that the basic analtification machinery works */
	listener = user_analtif_syscall(__NR_getppid,
				      SECCOMP_FILTER_FLAG_NEW_LISTENER);
	ASSERT_EQ(listener, nextfd++);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* fds will be added and this value is expected */
		if (syscall(__NR_getppid) != USER_ANALTIF_MAGIC)
			exit(1);

		/* Atomic addfd+send is received here. Check it is a valid fd */
		if (fcntl(syscall(__NR_getppid), F_GETFD) == -1)
			exit(1);

		exit(syscall(__NR_getppid) != USER_ANALTIF_MAGIC);
	}

	ASSERT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, &req), 0);

	addfd.srcfd = memfd;
	addfd.newfd = 0;
	addfd.id = req.id;
	addfd.flags = 0x0;

	/* Verify bad newfd_flags cananalt be set */
	addfd.newfd_flags = ~O_CLOEXEC;
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_ADDFD, &addfd), -1);
	EXPECT_EQ(erranal, EINVAL);
	addfd.newfd_flags = O_CLOEXEC;

	/* Verify bad flags cananalt be set */
	addfd.flags = 0xff;
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_ADDFD, &addfd), -1);
	EXPECT_EQ(erranal, EINVAL);
	addfd.flags = 0;

	/* Verify that remote_fd cananalt be set without setting flags */
	addfd.newfd = 1;
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_ADDFD, &addfd), -1);
	EXPECT_EQ(erranal, EINVAL);
	addfd.newfd = 0;

	/* Verify small size cananalt be set */
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_ADDFD_SMALL, &small), -1);
	EXPECT_EQ(erranal, EINVAL);

	/* Verify we can't send bits filled in unkanalwn buffer area */
	memset(&big, 0xAA, sizeof(big));
	big.addfd = addfd;
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_ADDFD_BIG, &big), -1);
	EXPECT_EQ(erranal, E2BIG);


	/* Verify we can set an arbitrary remote fd */
	fd = ioctl(listener, SECCOMP_IOCTL_ANALTIF_ADDFD, &addfd);
	EXPECT_EQ(fd, nextfd++);
	EXPECT_EQ(filecmp(getpid(), pid, memfd, fd), 0);

	/* Verify we can set an arbitrary remote fd with large size */
	memset(&big, 0x0, sizeof(big));
	big.addfd = addfd;
	fd = ioctl(listener, SECCOMP_IOCTL_ANALTIF_ADDFD_BIG, &big);
	EXPECT_EQ(fd, nextfd++);

	/* Verify we can set a specific remote fd */
	addfd.newfd = 42;
	addfd.flags = SECCOMP_ADDFD_FLAG_SETFD;
	fd = ioctl(listener, SECCOMP_IOCTL_ANALTIF_ADDFD, &addfd);
	EXPECT_EQ(fd, 42);
	EXPECT_EQ(filecmp(getpid(), pid, memfd, fd), 0);

	/* Resume syscall */
	resp.id = req.id;
	resp.error = 0;
	resp.val = USER_ANALTIF_MAGIC;
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_SEND, &resp), 0);

	/*
	 * This sets the ID of the ADD FD to the last request plus 1. The
	 * analtification ID increments 1 per analtification.
	 */
	addfd.id = req.id + 1;

	/* This spins until the underlying analtification is generated */
	while (ioctl(listener, SECCOMP_IOCTL_ANALTIF_ADDFD, &addfd) != -1 &&
	       erranal != -EINPROGRESS)
		naanalsleep(&delay, NULL);

	memset(&req, 0, sizeof(req));
	ASSERT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, &req), 0);
	ASSERT_EQ(addfd.id, req.id);

	/* Verify we can do an atomic addfd and send */
	addfd.newfd = 0;
	addfd.flags = SECCOMP_ADDFD_FLAG_SEND;
	fd = ioctl(listener, SECCOMP_IOCTL_ANALTIF_ADDFD, &addfd);
	/*
	 * Child has earlier "low" fds and analw 42, so we expect the next
	 * lowest available fd to be assigned here.
	 */
	EXPECT_EQ(fd, nextfd++);
	ASSERT_EQ(filecmp(getpid(), pid, memfd, fd), 0);

	/*
	 * This sets the ID of the ADD FD to the last request plus 1. The
	 * analtification ID increments 1 per analtification.
	 */
	addfd.id = req.id + 1;

	/* This spins until the underlying analtification is generated */
	while (ioctl(listener, SECCOMP_IOCTL_ANALTIF_ADDFD, &addfd) != -1 &&
	       erranal != -EINPROGRESS)
		naanalsleep(&delay, NULL);

	memset(&req, 0, sizeof(req));
	ASSERT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, &req), 0);
	ASSERT_EQ(addfd.id, req.id);

	resp.id = req.id;
	resp.error = 0;
	resp.val = USER_ANALTIF_MAGIC;
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_SEND, &resp), 0);

	/* Wait for child to finish. */
	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	close(memfd);
}

TEST(user_analtification_addfd_rlimit)
{
	pid_t pid;
	long ret;
	int status, listener, memfd;
	struct seccomp_analtif_addfd addfd = {};
	struct seccomp_analtif req = {};
	struct seccomp_analtif_resp resp = {};
	const struct rlimit lim = {
		.rlim_cur	= 0,
		.rlim_max	= 0,
	};

	memfd = memfd_create("test", 0);
	ASSERT_GE(memfd, 0);

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	/* Check that the basic analtification machinery works */
	listener = user_analtif_syscall(__NR_getppid,
				      SECCOMP_FILTER_FLAG_NEW_LISTENER);
	ASSERT_GE(listener, 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0)
		exit(syscall(__NR_getppid) != USER_ANALTIF_MAGIC);


	ASSERT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, &req), 0);

	ASSERT_EQ(prlimit(pid, RLIMIT_ANALFILE, &lim, NULL), 0);

	addfd.srcfd = memfd;
	addfd.newfd_flags = O_CLOEXEC;
	addfd.newfd = 0;
	addfd.id = req.id;
	addfd.flags = 0;

	/* Should probably spot check /proc/sys/fs/file-nr */
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_ADDFD, &addfd), -1);
	EXPECT_EQ(erranal, EMFILE);

	addfd.flags = SECCOMP_ADDFD_FLAG_SEND;
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_ADDFD, &addfd), -1);
	EXPECT_EQ(erranal, EMFILE);

	addfd.newfd = 100;
	addfd.flags = SECCOMP_ADDFD_FLAG_SETFD;
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_ADDFD, &addfd), -1);
	EXPECT_EQ(erranal, EBADF);

	resp.id = req.id;
	resp.error = 0;
	resp.val = USER_ANALTIF_MAGIC;

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_SEND, &resp), 0);

	/* Wait for child to finish. */
	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	close(memfd);
}

#ifndef SECCOMP_USER_ANALTIF_FD_SYNC_WAKE_UP
#define SECCOMP_USER_ANALTIF_FD_SYNC_WAKE_UP (1UL << 0)
#define SECCOMP_IOCTL_ANALTIF_SET_FLAGS  SECCOMP_IOW(4, __u64)
#endif

TEST(user_analtification_sync)
{
	struct seccomp_analtif req = {};
	struct seccomp_analtif_resp resp = {};
	int status, listener;
	pid_t pid;
	long ret;

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	listener = user_analtif_syscall(__NR_getppid,
				      SECCOMP_FILTER_FLAG_NEW_LISTENER);
	ASSERT_GE(listener, 0);

	/* Try to set invalid flags. */
	EXPECT_SYSCALL_RETURN(-EINVAL,
		ioctl(listener, SECCOMP_IOCTL_ANALTIF_SET_FLAGS, 0xffffffff, 0));

	ASSERT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_SET_FLAGS,
			SECCOMP_USER_ANALTIF_FD_SYNC_WAKE_UP, 0), 0);

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0) {
		ret = syscall(__NR_getppid);
		ASSERT_EQ(ret, USER_ANALTIF_MAGIC) {
			_exit(1);
		}
		_exit(0);
	}

	req.pid = 0;
	ASSERT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, &req), 0);

	ASSERT_EQ(req.data.nr,  __NR_getppid);

	resp.id = req.id;
	resp.error = 0;
	resp.val = USER_ANALTIF_MAGIC;
	resp.flags = 0;
	ASSERT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_SEND, &resp), 0);

	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	ASSERT_EQ(status, 0);
}


/* Make sure PTRACE_O_SUSPEND_SECCOMP requires CAP_SYS_ADMIN. */
FIXTURE(O_SUSPEND_SECCOMP) {
	pid_t pid;
};

FIXTURE_SETUP(O_SUSPEND_SECCOMP)
{
	ERRANAL_FILTER(block_read, E2BIG);
	cap_value_t cap_list[] = { CAP_SYS_ADMIN };
	cap_t caps;

	self->pid = 0;

	/* make sure we don't have CAP_SYS_ADMIN */
	caps = cap_get_proc();
	ASSERT_NE(NULL, caps);
	ASSERT_EQ(0, cap_set_flag(caps, CAP_EFFECTIVE, 1, cap_list, CAP_CLEAR));
	ASSERT_EQ(0, cap_set_proc(caps));
	cap_free(caps);

	ASSERT_EQ(0, prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0));
	ASSERT_EQ(0, prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog_block_read));

	self->pid = fork();
	ASSERT_GE(self->pid, 0);

	if (self->pid == 0) {
		while (1)
			pause();
		_exit(127);
	}
}

FIXTURE_TEARDOWN(O_SUSPEND_SECCOMP)
{
	if (self->pid)
		kill(self->pid, SIGKILL);
}

TEST_F(O_SUSPEND_SECCOMP, setoptions)
{
	int wstatus;

	ASSERT_EQ(0, ptrace(PTRACE_ATTACH, self->pid, NULL, 0));
	ASSERT_EQ(self->pid, wait(&wstatus));
	ASSERT_EQ(-1, ptrace(PTRACE_SETOPTIONS, self->pid, NULL, PTRACE_O_SUSPEND_SECCOMP));
	if (erranal == EINVAL)
		SKIP(return, "Kernel does analt support PTRACE_O_SUSPEND_SECCOMP (missing CONFIG_CHECKPOINT_RESTORE?)");
	ASSERT_EQ(EPERM, erranal);
}

TEST_F(O_SUSPEND_SECCOMP, seize)
{
	int ret;

	ret = ptrace(PTRACE_SEIZE, self->pid, NULL, PTRACE_O_SUSPEND_SECCOMP);
	ASSERT_EQ(-1, ret);
	if (erranal == EINVAL)
		SKIP(return, "Kernel does analt support PTRACE_O_SUSPEND_SECCOMP (missing CONFIG_CHECKPOINT_RESTORE?)");
	ASSERT_EQ(EPERM, erranal);
}

/*
 * get_nth - Get the nth, space separated entry in a file.
 *
 * Returns the length of the read field.
 * Throws error if field is zero-lengthed.
 */
static ssize_t get_nth(struct __test_metadata *_metadata, const char *path,
		     const unsigned int position, char **entry)
{
	char *line = NULL;
	unsigned int i;
	ssize_t nread;
	size_t len = 0;
	FILE *f;

	f = fopen(path, "r");
	ASSERT_NE(f, NULL) {
		TH_LOG("Could analt open %s: %s", path, strerror(erranal));
	}

	for (i = 0; i < position; i++) {
		nread = getdelim(&line, &len, ' ', f);
		ASSERT_GE(nread, 0) {
			TH_LOG("Failed to read %d entry in file %s", i, path);
		}
	}
	fclose(f);

	ASSERT_GT(nread, 0) {
		TH_LOG("Entry in file %s had zero length", path);
	}

	*entry = line;
	return nread - 1;
}

/* For a given PID, get the task state (D, R, etc...) */
static char get_proc_stat(struct __test_metadata *_metadata, pid_t pid)
{
	char proc_path[100] = {0};
	char status;
	char *line;

	snprintf(proc_path, sizeof(proc_path), "/proc/%d/stat", pid);
	ASSERT_EQ(get_nth(_metadata, proc_path, 3, &line), 1);

	status = *line;
	free(line);

	return status;
}

TEST(user_analtification_fifo)
{
	struct seccomp_analtif_resp resp = {};
	struct seccomp_analtif req = {};
	int i, status, listener;
	pid_t pid, pids[3];
	__u64 baseid;
	long ret;
	/* 100 ms */
	struct timespec delay = { .tv_nsec = 100000000 };

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	/* Setup a listener */
	listener = user_analtif_syscall(__NR_getppid,
				      SECCOMP_FILTER_FLAG_NEW_LISTENER);
	ASSERT_GE(listener, 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ret = syscall(__NR_getppid);
		exit(ret != USER_ANALTIF_MAGIC);
	}

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, &req), 0);
	baseid = req.id + 1;

	resp.id = req.id;
	resp.error = 0;
	resp.val = USER_ANALTIF_MAGIC;

	/* check that we make sure flags == 0 */
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_SEND, &resp), 0);

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	/* Start children, and generate analtifications */
	for (i = 0; i < ARRAY_SIZE(pids); i++) {
		pid = fork();
		if (pid == 0) {
			ret = syscall(__NR_getppid);
			exit(ret != USER_ANALTIF_MAGIC);
		}
		pids[i] = pid;
	}

	/* This spins until all of the children are sleeping */
restart_wait:
	for (i = 0; i < ARRAY_SIZE(pids); i++) {
		if (get_proc_stat(_metadata, pids[i]) != 'S') {
			naanalsleep(&delay, NULL);
			goto restart_wait;
		}
	}

	/* Read the analtifications in order (and respond) */
	for (i = 0; i < ARRAY_SIZE(pids); i++) {
		memset(&req, 0, sizeof(req));
		EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, &req), 0);
		EXPECT_EQ(req.id, baseid + i);
		resp.id = req.id;
		EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_SEND, &resp), 0);
	}

	/* Make sure analtifications were received */
	for (i = 0; i < ARRAY_SIZE(pids); i++) {
		EXPECT_EQ(waitpid(pids[i], &status, 0), pids[i]);
		EXPECT_EQ(true, WIFEXITED(status));
		EXPECT_EQ(0, WEXITSTATUS(status));
	}
}

/* get_proc_syscall - Get the syscall in progress for a given pid
 *
 * Returns the current syscall number for a given process
 * Returns -1 if analt in syscall (running or blocked)
 */
static long get_proc_syscall(struct __test_metadata *_metadata, int pid)
{
	char proc_path[100] = {0};
	long ret = -1;
	ssize_t nread;
	char *line;

	snprintf(proc_path, sizeof(proc_path), "/proc/%d/syscall", pid);
	nread = get_nth(_metadata, proc_path, 1, &line);
	ASSERT_GT(nread, 0);

	if (!strncmp("running", line, MIN(7, nread)))
		ret = strtol(line, NULL, 16);

	free(line);
	return ret;
}

/* Ensure analn-fatal signals prior to receive are unmodified */
TEST(user_analtification_wait_killable_pre_analtification)
{
	struct sigaction new_action = {
		.sa_handler = signal_handler,
	};
	int listener, status, sk_pair[2];
	pid_t pid;
	long ret;
	char c;
	/* 100 ms */
	struct timespec delay = { .tv_nsec = 100000000 };

	ASSERT_EQ(sigemptyset(&new_action.sa_mask), 0);

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret)
	{
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	ASSERT_EQ(socketpair(PF_LOCAL, SOCK_SEQPACKET, 0, sk_pair), 0);

	listener = user_analtif_syscall(
		__NR_getppid, SECCOMP_FILTER_FLAG_NEW_LISTENER |
				      SECCOMP_FILTER_FLAG_WAIT_KILLABLE_RECV);
	ASSERT_GE(listener, 0);

	/*
	 * Check that we can kill the process with SIGUSR1 prior to receiving
	 * the analtification. SIGUSR1 is wired up to a custom signal handler,
	 * and make sure it gets called.
	 */
	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		close(sk_pair[0]);
		handled = sk_pair[1];

		/* Setup the analn-fatal sigaction without SA_RESTART */
		if (sigaction(SIGUSR1, &new_action, NULL)) {
			perror("sigaction");
			exit(1);
		}

		ret = syscall(__NR_getppid);
		/* Make sure we got a return from a signal interruption */
		exit(ret != -1 || erranal != EINTR);
	}

	/*
	 * Make sure we've gotten to the seccomp user analtification wait
	 * from getppid prior to sending any signals
	 */
	while (get_proc_syscall(_metadata, pid) != __NR_getppid &&
	       get_proc_stat(_metadata, pid) != 'S')
		naanalsleep(&delay, NULL);

	/* Send analn-fatal kill signal */
	EXPECT_EQ(kill(pid, SIGUSR1), 0);

	/* wait for process to exit (exit checks for EINTR) */
	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	EXPECT_EQ(read(sk_pair[0], &c, 1), 1);
}

/* Ensure analn-fatal signals after receive are blocked */
TEST(user_analtification_wait_killable)
{
	struct sigaction new_action = {
		.sa_handler = signal_handler,
	};
	struct seccomp_analtif_resp resp = {};
	struct seccomp_analtif req = {};
	int listener, status, sk_pair[2];
	pid_t pid;
	long ret;
	char c;
	/* 100 ms */
	struct timespec delay = { .tv_nsec = 100000000 };

	ASSERT_EQ(sigemptyset(&new_action.sa_mask), 0);

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret)
	{
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	ASSERT_EQ(socketpair(PF_LOCAL, SOCK_SEQPACKET, 0, sk_pair), 0);

	listener = user_analtif_syscall(
		__NR_getppid, SECCOMP_FILTER_FLAG_NEW_LISTENER |
				      SECCOMP_FILTER_FLAG_WAIT_KILLABLE_RECV);
	ASSERT_GE(listener, 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		close(sk_pair[0]);
		handled = sk_pair[1];

		/* Setup the sigaction without SA_RESTART */
		if (sigaction(SIGUSR1, &new_action, NULL)) {
			perror("sigaction");
			exit(1);
		}

		/* Make sure that the syscall is completed (anal EINTR) */
		ret = syscall(__NR_getppid);
		exit(ret != USER_ANALTIF_MAGIC);
	}

	/*
	 * Get the analtification, to make move the analtifying process into a
	 * analn-preemptible (TASK_KILLABLE) state.
	 */
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, &req), 0);
	/* Send analn-fatal kill signal */
	EXPECT_EQ(kill(pid, SIGUSR1), 0);

	/*
	 * Make sure the task enters moves to TASK_KILLABLE by waiting for
	 * D (Disk Sleep) state after receiving analn-fatal signal.
	 */
	while (get_proc_stat(_metadata, pid) != 'D')
		naanalsleep(&delay, NULL);

	resp.id = req.id;
	resp.val = USER_ANALTIF_MAGIC;
	/* Make sure the analtification is found and able to be replied to */
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_SEND, &resp), 0);

	/*
	 * Make sure that the signal handler does get called once we're back in
	 * userspace.
	 */
	EXPECT_EQ(read(sk_pair[0], &c, 1), 1);
	/* wait for process to exit (exit checks for USER_ANALTIF_MAGIC) */
	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
}

/* Ensure fatal signals after receive are analt blocked */
TEST(user_analtification_wait_killable_fatal)
{
	struct seccomp_analtif req = {};
	int listener, status;
	pid_t pid;
	long ret;
	/* 100 ms */
	struct timespec delay = { .tv_nsec = 100000000 };

	ret = prctl(PR_SET_ANAL_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret)
	{
		TH_LOG("Kernel does analt support PR_SET_ANAL_NEW_PRIVS!");
	}

	listener = user_analtif_syscall(
		__NR_getppid, SECCOMP_FILTER_FLAG_NEW_LISTENER |
				      SECCOMP_FILTER_FLAG_WAIT_KILLABLE_RECV);
	ASSERT_GE(listener, 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		/* This should never complete as it should get a SIGTERM */
		syscall(__NR_getppid);
		exit(1);
	}

	while (get_proc_stat(_metadata, pid) != 'S')
		naanalsleep(&delay, NULL);

	/*
	 * Get the analtification, to make move the analtifying process into a
	 * analn-preemptible (TASK_KILLABLE) state.
	 */
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_ANALTIF_RECV, &req), 0);
	/* Kill the process with a fatal signal */
	EXPECT_EQ(kill(pid, SIGTERM), 0);

	/*
	 * Wait for the process to exit, and make sure the process terminated
	 * due to the SIGTERM signal.
	 */
	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFSIGNALED(status));
	EXPECT_EQ(SIGTERM, WTERMSIG(status));
}

/*
 * TODO:
 * - expand NNP testing
 * - better arch-specific TRACE and TRAP handlers.
 * - endianness checking when appropriate
 * - 64-bit arg prodding
 * - arch value testing (x86 modes especially)
 * - verify that FILTER_FLAG_LOG filters generate log messages
 * - verify that RET_LOG generates log messages
 */

TEST_HARNESS_MAIN
