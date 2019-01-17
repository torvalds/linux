/*
 * Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPLv2 license.
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

#include <errno.h>
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
#include <linux/elf.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/times.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <unistd.h>
#include <sys/syscall.h>
#include <poll.h>

#include "../kselftest_harness.h"

#ifndef PR_SET_PTRACER
# define PR_SET_PTRACER 0x59616d61
#endif

#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS 38
#define PR_GET_NO_NEW_PRIVS 39
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
#define SECCOMP_RET_ERRNO	 0x00050000U /* returns an errno */
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
# elif defined(__hppa__)
#  define __NR_seccomp 338
# elif defined(__powerpc__)
#  define __NR_seccomp 358
# elif defined(__s390__)
#  define __NR_seccomp 348
# else
#  warning "seccomp syscall number unknown for this architecture"
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

#ifndef SECCOMP_GET_NOTIF_SIZES
#define SECCOMP_GET_NOTIF_SIZES 3
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

#define SECCOMP_RET_USER_NOTIF 0x7fc00000U

#define SECCOMP_IOC_MAGIC		'!'
#define SECCOMP_IO(nr)			_IO(SECCOMP_IOC_MAGIC, nr)
#define SECCOMP_IOR(nr, type)		_IOR(SECCOMP_IOC_MAGIC, nr, type)
#define SECCOMP_IOW(nr, type)		_IOW(SECCOMP_IOC_MAGIC, nr, type)
#define SECCOMP_IOWR(nr, type)		_IOWR(SECCOMP_IOC_MAGIC, nr, type)

/* Flags for seccomp notification fd ioctl. */
#define SECCOMP_IOCTL_NOTIF_RECV	SECCOMP_IOWR(0, struct seccomp_notif)
#define SECCOMP_IOCTL_NOTIF_SEND	SECCOMP_IOWR(1,	\
						struct seccomp_notif_resp)
#define SECCOMP_IOCTL_NOTIF_ID_VALID	SECCOMP_IOR(2, __u64)

struct seccomp_notif {
	__u64 id;
	__u32 pid;
	__u32 flags;
	struct seccomp_data data;
};

struct seccomp_notif_resp {
	__u64 id;
	__s64 val;
	__s32 error;
	__u32 flags;
};

struct seccomp_notif_sizes {
	__u16 seccomp_notif;
	__u16 seccomp_notif_resp;
	__u16 seccomp_data;
};
#endif

#ifndef seccomp
int seccomp(unsigned int op, unsigned int flags, void *args)
{
	errno = 0;
	return syscall(__NR_seccomp, op, flags, args);
}
#endif

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define syscall_arg(_n) (offsetof(struct seccomp_data, args[_n]))
#elif __BYTE_ORDER == __BIG_ENDIAN
#define syscall_arg(_n) (offsetof(struct seccomp_data, args[_n]) + sizeof(__u32))
#else
#error "wut? Unknown __BYTE_ORDER?!"
#endif

#define SIBLING_EXIT_UNKILLED	0xbadbeef
#define SIBLING_EXIT_FAILURE	0xbadface
#define SIBLING_EXIT_NEWPRIVS	0xbadfeed

TEST(mode_strict_support)
{
	long ret;

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_STRICT, NULL, NULL, NULL);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does not support CONFIG_SECCOMP");
	}
	syscall(__NR_exit, 0);
}

TEST_SIGNAL(mode_strict_cannot_call_prctl, SIGKILL)
{
	long ret;

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_STRICT, NULL, NULL, NULL);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does not support CONFIG_SECCOMP");
	}
	syscall(__NR_prctl, PR_SET_SECCOMP, SECCOMP_MODE_FILTER,
		NULL, NULL, NULL);
	EXPECT_FALSE(true) {
		TH_LOG("Unreachable!");
	}
}

/* Note! This doesn't test no new privs behavior */
TEST(no_new_privs_support)
{
	long ret;

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	EXPECT_EQ(0, ret) {
		TH_LOG("Kernel does not support PR_SET_NO_NEW_PRIVS!");
	}
}

/* Tests kernel support by checking for a copy_from_user() fault on NULL. */
TEST(mode_filter_support)
{
	long ret;

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, NULL, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does not support PR_SET_NO_NEW_PRIVS!");
	}
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, NULL, NULL, NULL);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EFAULT, errno) {
		TH_LOG("Kernel does not support CONFIG_SECCOMP_FILTER!");
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

	ret = prctl(PR_GET_NO_NEW_PRIVS, 0, NULL, 0, 0);
	ASSERT_LE(0, ret) {
		TH_LOG("Expected 0 or unsupported for NO_NEW_PRIVS");
	}
	errno = 0;
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
	/* Succeeds with CAP_SYS_ADMIN, fails without */
	/* TODO(wad) check caps not euid */
	if (geteuid()) {
		EXPECT_EQ(-1, ret);
		EXPECT_EQ(EACCES, errno);
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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

TEST(mode_filter_cannot_move_to_strict)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_STRICT, NULL, 0, 0);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EINVAL, errno);
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EINVAL, errno);
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	ASSERT_EQ(0, ret);

	/* getppid() should succeed and be logged (no check for logging) */
	EXPECT_EQ(parent, syscall(__NR_getppid));
}

TEST_SIGNAL(unknown_ret_is_kill_inside, SIGSYS)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_RET|BPF_K, 0x10000000U),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	ASSERT_EQ(0, ret);
	EXPECT_EQ(0, syscall(__NR_getpid)) {
		TH_LOG("getpid() shouldn't ever return");
	}
}

/* return code >= 0x80000000 is unused. */
TEST_SIGNAL(unknown_ret_is_kill_above_allow, SIGSYS)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_RET|BPF_K, 0x90000000U),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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
		/* Only both with lower 32-bit for now. */
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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
	int sysno = __NR_mmap;
#else
	int sysno = __NR_mmap2;
#endif
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, sysno, 1, 0),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
		/* Only both with lower 32-bit for now. */
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	ASSERT_EQ(0, ret);

	fd = open("/dev/zero", O_RDONLY);
	ASSERT_NE(-1, fd);

	EXPECT_EQ(parent, syscall(__NR_getppid));
	map1 = (void *)syscall(sysno,
		NULL, page_size, PROT_READ, MAP_PRIVATE, fd, page_size);
	EXPECT_NE(MAP_FAILED, map1);
	/* mmap2() should never return. */
	map2 = (void *)syscall(sysno,
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

/* Prepare a thread that will kill itself or both of us. */
void kill_thread_or_group(struct __test_metadata *_metadata, bool kill_process)
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
	struct sock_filter filter_process[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_prctl, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_KILL_PROCESS),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog_process = {
		.len = (unsigned short)ARRAY_SIZE(filter_process),
		.filter = filter_process,
	};

	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
		TH_LOG("Kernel does not support PR_SET_NO_NEW_PRIVS!");
	}

	ASSERT_EQ(0, seccomp(SECCOMP_SET_MODE_FILTER, 0,
			     kill_process ? &prog_process : &prog_thread));

	/*
	 * Add the KILL_THREAD rule again to make sure that the KILL_PROCESS
	 * flag cannot be downgraded by a new filter.
	 */
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
	 * If we get here, only the spawned thread died. Let the parent know
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
		kill_thread_or_group(_metadata, false);
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
		kill_thread_or_group(_metadata, true);
		_exit(38);
	}

	ASSERT_EQ(child_pid, waitpid(child_pid, &status, 0));

	/* If the entire process was killed, we'll see SIGSYS. */
	ASSERT_TRUE(WIFSIGNALED(status));
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EINVAL, errno);
}

#define ERRNO_FILTER(name, errno)					\
	struct sock_filter _read_filter_##name[] = {			\
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,				\
			offsetof(struct seccomp_data, nr)),		\
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_read, 0, 1),	\
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ERRNO | errno),	\
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),		\
	};								\
	struct sock_fprog prog_##name = {				\
		.len = (unsigned short)ARRAY_SIZE(_read_filter_##name),	\
		.filter = _read_filter_##name,				\
	}

/* Make sure basic errno values are correctly passed through a filter. */
TEST(ERRNO_valid)
{
	ERRNO_FILTER(valid, E2BIG);
	long ret;
	pid_t parent = getppid();

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog_valid);
	ASSERT_EQ(0, ret);

	EXPECT_EQ(parent, syscall(__NR_getppid));
	EXPECT_EQ(-1, read(0, NULL, 0));
	EXPECT_EQ(E2BIG, errno);
}

/* Make sure an errno of zero is correctly handled by the arch code. */
TEST(ERRNO_zero)
{
	ERRNO_FILTER(zero, 0);
	long ret;
	pid_t parent = getppid();

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog_zero);
	ASSERT_EQ(0, ret);

	EXPECT_EQ(parent, syscall(__NR_getppid));
	/* "errno" of 0 is ok. */
	EXPECT_EQ(0, read(0, NULL, 0));
}

/*
 * The SECCOMP_RET_DATA mask is 16 bits wide, but errno is smaller.
 * This tests that the errno value gets capped correctly, fixed by
 * 580c57f10768 ("seccomp: cap SECCOMP_RET_ERRNO data to MAX_ERRNO").
 */
TEST(ERRNO_capped)
{
	ERRNO_FILTER(capped, 4096);
	long ret;
	pid_t parent = getppid();

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog_capped);
	ASSERT_EQ(0, ret);

	EXPECT_EQ(parent, syscall(__NR_getppid));
	EXPECT_EQ(-1, read(0, NULL, 0));
	EXPECT_EQ(4095, errno);
}

/*
 * Filters are processed in reverse order: last applied is executed first.
 * Since only the SECCOMP_RET_ACTION mask is tested for return values, the
 * SECCOMP_RET_DATA mask results will follow the most recently applied
 * matching filter return (and not the lowest or highest value).
 */
TEST(ERRNO_order)
{
	ERRNO_FILTER(first,  11);
	ERRNO_FILTER(second, 13);
	ERRNO_FILTER(third,  12);
	long ret;
	pid_t parent = getppid();

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog_first);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog_second);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog_third);
	ASSERT_EQ(0, ret);

	EXPECT_EQ(parent, syscall(__NR_getppid));
	EXPECT_EQ(-1, read(0, NULL, 0));
	EXPECT_EQ(12, errno);
}

FIXTURE_DATA(TRAP) {
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->prog);
	ASSERT_EQ(0, ret);
	syscall(__NR_getpid);
}

/* Ensure that SIGSYS overrides SIG_IGN */
TEST_F_SIGNAL(TRAP, ign, SIGSYS)
{
	long ret;

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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
	/* Make sure arch is non-zero. */
	EXPECT_NE(0, sigsys->_arch);
	EXPECT_NE(0, (unsigned long)sigsys->_call_addr);
}

FIXTURE_DATA(precedence) {
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
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ERRNO),
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
	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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
	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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
	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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
	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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
	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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

TEST_F(precedence, errno_is_third)
{
	pid_t parent;
	long ret;

	parent = getppid();
	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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

TEST_F(precedence, errno_is_third_in_any_order)
{
	pid_t parent;
	long ret;

	parent = getppid();
	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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
	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->allow);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->log);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->trace);
	ASSERT_EQ(0, ret);
	/* Should work just fine. */
	EXPECT_EQ(parent, syscall(__NR_getppid));
	/* No ptracer */
	EXPECT_EQ(-1, syscall(__NR_getpid));
}

TEST_F(precedence, trace_is_fourth_in_any_order)
{
	pid_t parent;
	long ret;

	parent = getppid();
	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->trace);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->allow);
	ASSERT_EQ(0, ret);
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->log);
	ASSERT_EQ(0, ret);
	/* Should work just fine. */
	EXPECT_EQ(parent, syscall(__NR_getppid));
	/* No ptracer */
	EXPECT_EQ(-1, syscall(__NR_getpid));
}

TEST_F(precedence, log_is_fifth)
{
	pid_t mypid, parent;
	long ret;

	mypid = getpid();
	parent = getppid();
	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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
	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
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

#define IS_SECCOMP_EVENT(status) ((status >> 16) == PTRACE_EVENT_SECCOMP)
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

	errno = 0;
	while (ret == -1 && errno != EINVAL)
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
		if (WIFSIGNALED(status) || WIFEXITED(status))
			/* Child is dead. Time to go. */
			return;

		/* Check if this is a seccomp event. */
		ASSERT_EQ(!ptrace_syscall, IS_SECCOMP_EVENT(status));

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
	 * Registers are not touched to try to keep this relatively arch
	 * agnostic.
	 */
	ret = ptrace(PTRACE_POKEDATA, tracee, info->poke_addr, 0x1001);
	EXPECT_EQ(0, ret);
}

FIXTURE_DATA(TRACE_poke) {
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->prog, 0, 0);
	ASSERT_EQ(0, ret);

	EXPECT_EQ(0, self->poked);
	ret = read(-1, NULL, 0);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(0x1001, self->poked);
}

TEST_F(TRACE_poke, getpid_runs_normally)
{
	long ret;

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->prog, 0, 0);
	ASSERT_EQ(0, ret);

	EXPECT_EQ(0, self->poked);
	EXPECT_NE(0, syscall(__NR_getpid));
	EXPECT_EQ(0, self->poked);
}

#if defined(__x86_64__)
# define ARCH_REGS	struct user_regs_struct
# define SYSCALL_NUM	orig_rax
# define SYSCALL_RET	rax
#elif defined(__i386__)
# define ARCH_REGS	struct user_regs_struct
# define SYSCALL_NUM	orig_eax
# define SYSCALL_RET	eax
#elif defined(__arm__)
# define ARCH_REGS	struct pt_regs
# define SYSCALL_NUM	ARM_r7
# define SYSCALL_RET	ARM_r0
#elif defined(__aarch64__)
# define ARCH_REGS	struct user_pt_regs
# define SYSCALL_NUM	regs[8]
# define SYSCALL_RET	regs[0]
#elif defined(__hppa__)
# define ARCH_REGS	struct user_regs_struct
# define SYSCALL_NUM	gr[20]
# define SYSCALL_RET	gr[28]
#elif defined(__powerpc__)
# define ARCH_REGS	struct pt_regs
# define SYSCALL_NUM	gpr[0]
# define SYSCALL_RET	gpr[3]
#elif defined(__s390__)
# define ARCH_REGS     s390_regs
# define SYSCALL_NUM   gprs[2]
# define SYSCALL_RET   gprs[2]
#elif defined(__mips__)
# define ARCH_REGS	struct pt_regs
# define SYSCALL_NUM	regs[2]
# define SYSCALL_SYSCALL_NUM regs[4]
# define SYSCALL_RET	regs[2]
# define SYSCALL_NUM_RET_SHARE_REG
#else
# error "Do not know how to find your architecture's registers and syscalls"
#endif

/* When the syscall return can't be changed, stub out the tests for it. */
#ifdef SYSCALL_NUM_RET_SHARE_REG
# define EXPECT_SYSCALL_RETURN(val, action)	EXPECT_EQ(-1, action)
#else
# define EXPECT_SYSCALL_RETURN(val, action)	EXPECT_EQ(val, action)
#endif

/* Use PTRACE_GETREGS and PTRACE_SETREGS when available. This is useful for
 * architectures without HAVE_ARCH_TRACEHOOK (e.g. User-mode Linux).
 */
#if defined(__x86_64__) || defined(__i386__) || defined(__mips__)
#define HAVE_GETREGS
#endif

/* Architecture-specific syscall fetching routine. */
int get_syscall(struct __test_metadata *_metadata, pid_t tracee)
{
	ARCH_REGS regs;
#ifdef HAVE_GETREGS
	EXPECT_EQ(0, ptrace(PTRACE_GETREGS, tracee, 0, &regs)) {
		TH_LOG("PTRACE_GETREGS failed");
		return -1;
	}
#else
	struct iovec iov;

	iov.iov_base = &regs;
	iov.iov_len = sizeof(regs);
	EXPECT_EQ(0, ptrace(PTRACE_GETREGSET, tracee, NT_PRSTATUS, &iov)) {
		TH_LOG("PTRACE_GETREGSET failed");
		return -1;
	}
#endif

#if defined(__mips__)
	if (regs.SYSCALL_NUM == __NR_O32_Linux)
		return regs.SYSCALL_SYSCALL_NUM;
#endif
	return regs.SYSCALL_NUM;
}

/* Architecture-specific syscall changing routine. */
void change_syscall(struct __test_metadata *_metadata,
		    pid_t tracee, int syscall)
{
	int ret;
	ARCH_REGS regs;
#ifdef HAVE_GETREGS
	ret = ptrace(PTRACE_GETREGS, tracee, 0, &regs);
#else
	struct iovec iov;
	iov.iov_base = &regs;
	iov.iov_len = sizeof(regs);
	ret = ptrace(PTRACE_GETREGSET, tracee, NT_PRSTATUS, &iov);
#endif
	EXPECT_EQ(0, ret) {}

#if defined(__x86_64__) || defined(__i386__) || defined(__powerpc__) || \
    defined(__s390__) || defined(__hppa__)
	{
		regs.SYSCALL_NUM = syscall;
	}
#elif defined(__mips__)
	{
		if (regs.SYSCALL_NUM == __NR_O32_Linux)
			regs.SYSCALL_SYSCALL_NUM = syscall;
		else
			regs.SYSCALL_NUM = syscall;
	}

#elif defined(__arm__)
# ifndef PTRACE_SET_SYSCALL
#  define PTRACE_SET_SYSCALL   23
# endif
	{
		ret = ptrace(PTRACE_SET_SYSCALL, tracee, NULL, syscall);
		EXPECT_EQ(0, ret);
	}

#elif defined(__aarch64__)
# ifndef NT_ARM_SYSTEM_CALL
#  define NT_ARM_SYSTEM_CALL 0x404
# endif
	{
		iov.iov_base = &syscall;
		iov.iov_len = sizeof(syscall);
		ret = ptrace(PTRACE_SETREGSET, tracee, NT_ARM_SYSTEM_CALL,
			     &iov);
		EXPECT_EQ(0, ret);
	}

#else
	ASSERT_EQ(1, 0) {
		TH_LOG("How is the syscall changed on this architecture?");
	}
#endif

	/* If syscall is skipped, change return value. */
	if (syscall == -1)
#ifdef SYSCALL_NUM_RET_SHARE_REG
		TH_LOG("Can't modify syscall return on this architecture");
#else
		regs.SYSCALL_RET = EPERM;
#endif

#ifdef HAVE_GETREGS
	ret = ptrace(PTRACE_SETREGS, tracee, 0, &regs);
#else
	iov.iov_base = &regs;
	iov.iov_len = sizeof(regs);
	ret = ptrace(PTRACE_SETREGSET, tracee, NT_PRSTATUS, &iov);
#endif
	EXPECT_EQ(0, ret);
}

void tracer_syscall(struct __test_metadata *_metadata, pid_t tracee,
		    int status, void *args)
{
	int ret;
	unsigned long msg;

	/* Make sure we got the right message. */
	ret = ptrace(PTRACE_GETEVENTMSG, tracee, NULL, &msg);
	EXPECT_EQ(0, ret);

	/* Validate and take action on expected syscalls. */
	switch (msg) {
	case 0x1002:
		/* change getpid to getppid. */
		EXPECT_EQ(__NR_getpid, get_syscall(_metadata, tracee));
		change_syscall(_metadata, tracee, __NR_getppid);
		break;
	case 0x1003:
		/* skip gettid. */
		EXPECT_EQ(__NR_gettid, get_syscall(_metadata, tracee));
		change_syscall(_metadata, tracee, -1);
		break;
	case 0x1004:
		/* do nothing (allow getppid) */
		EXPECT_EQ(__NR_getppid, get_syscall(_metadata, tracee));
		break;
	default:
		EXPECT_EQ(0, msg) {
			TH_LOG("Unknown PTRACE_GETEVENTMSG: 0x%lx", msg);
			kill(tracee, SIGKILL);
		}
	}

}

void tracer_ptrace(struct __test_metadata *_metadata, pid_t tracee,
		   int status, void *args)
{
	int ret, nr;
	unsigned long msg;
	static bool entry;

	/* Make sure we got an empty message. */
	ret = ptrace(PTRACE_GETEVENTMSG, tracee, NULL, &msg);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(0, msg);

	/* The only way to tell PTRACE_SYSCALL entry/exit is by counting. */
	entry = !entry;
	if (!entry)
		return;

	nr = get_syscall(_metadata, tracee);

	if (nr == __NR_getpid)
		change_syscall(_metadata, tracee, __NR_getppid);
	if (nr == __NR_openat)
		change_syscall(_metadata, tracee, -1);
}

FIXTURE_DATA(TRACE_syscall) {
	struct sock_fprog prog;
	pid_t tracer, mytid, mypid, parent;
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
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_getppid, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_TRACE | 0x1004),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};

	memset(&self->prog, 0, sizeof(self->prog));
	self->prog.filter = malloc(sizeof(filter));
	ASSERT_NE(NULL, self->prog.filter);
	memcpy(self->prog.filter, filter, sizeof(filter));
	self->prog.len = (unsigned short)ARRAY_SIZE(filter);

	/* Prepare some testable syscall results. */
	self->mytid = syscall(__NR_gettid);
	ASSERT_GT(self->mytid, 0);
	ASSERT_NE(self->mytid, 1) {
		TH_LOG("Running this test as init is not supported. :)");
	}

	self->mypid = getpid();
	ASSERT_GT(self->mypid, 0);
	ASSERT_EQ(self->mytid, self->mypid);

	self->parent = getppid();
	ASSERT_GT(self->parent, 0);
	ASSERT_NE(self->parent, self->mypid);

	/* Launch tracer. */
	self->tracer = setup_trace_fixture(_metadata, tracer_syscall, NULL,
					   false);
}

FIXTURE_TEARDOWN(TRACE_syscall)
{
	teardown_trace_fixture(_metadata, self->tracer);
	if (self->prog.filter)
		free(self->prog.filter);
}

TEST_F(TRACE_syscall, ptrace_syscall_redirected)
{
	/* Swap SECCOMP_RET_TRACE tracer for PTRACE_SYSCALL tracer. */
	teardown_trace_fixture(_metadata, self->tracer);
	self->tracer = setup_trace_fixture(_metadata, tracer_ptrace, NULL,
					   true);

	/* Tracer will redirect getpid to getppid. */
	EXPECT_NE(self->mypid, syscall(__NR_getpid));
}

TEST_F(TRACE_syscall, ptrace_syscall_dropped)
{
	/* Swap SECCOMP_RET_TRACE tracer for PTRACE_SYSCALL tracer. */
	teardown_trace_fixture(_metadata, self->tracer);
	self->tracer = setup_trace_fixture(_metadata, tracer_ptrace, NULL,
					   true);

	/* Tracer should skip the open syscall, resulting in EPERM. */
	EXPECT_SYSCALL_RETURN(EPERM, syscall(__NR_openat));
}

TEST_F(TRACE_syscall, syscall_allowed)
{
	long ret;

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->prog, 0, 0);
	ASSERT_EQ(0, ret);

	/* getppid works as expected (no changes). */
	EXPECT_EQ(self->parent, syscall(__NR_getppid));
	EXPECT_NE(self->mypid, syscall(__NR_getppid));
}

TEST_F(TRACE_syscall, syscall_redirected)
{
	long ret;

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->prog, 0, 0);
	ASSERT_EQ(0, ret);

	/* getpid has been redirected to getppid as expected. */
	EXPECT_EQ(self->parent, syscall(__NR_getpid));
	EXPECT_NE(self->mypid, syscall(__NR_getpid));
}

TEST_F(TRACE_syscall, syscall_dropped)
{
	long ret;

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->prog, 0, 0);
	ASSERT_EQ(0, ret);

	/* gettid has been skipped and an altered return value stored. */
	EXPECT_SYSCALL_RETURN(EPERM, syscall(__NR_gettid));
	EXPECT_NE(self->mytid, syscall(__NR_gettid));
}

TEST_F(TRACE_syscall, skip_after_RET_TRACE)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_getppid, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ERRNO | EPERM),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	/* Install fixture filter. */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->prog, 0, 0);
	ASSERT_EQ(0, ret);

	/* Install "errno on getppid" filter. */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
	ASSERT_EQ(0, ret);

	/* Tracer will redirect getpid to getppid, and we should see EPERM. */
	errno = 0;
	EXPECT_EQ(-1, syscall(__NR_getpid));
	EXPECT_EQ(EPERM, errno);
}

TEST_F_SIGNAL(TRACE_syscall, kill_after_RET_TRACE, SIGSYS)
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	/* Install fixture filter. */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &self->prog, 0, 0);
	ASSERT_EQ(0, ret);

	/* Install "death on getppid" filter. */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
	ASSERT_EQ(0, ret);

	/* Tracer will redirect getpid to getppid, and we should die. */
	EXPECT_NE(self->mypid, syscall(__NR_getpid));
}

TEST_F(TRACE_syscall, skip_after_ptrace)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_getppid, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ERRNO | EPERM),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};
	long ret;

	/* Swap SECCOMP_RET_TRACE tracer for PTRACE_SYSCALL tracer. */
	teardown_trace_fixture(_metadata, self->tracer);
	self->tracer = setup_trace_fixture(_metadata, tracer_ptrace, NULL,
					   true);

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	/* Install "errno on getppid" filter. */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
	ASSERT_EQ(0, ret);

	/* Tracer will redirect getpid to getppid, and we should see EPERM. */
	EXPECT_EQ(-1, syscall(__NR_getpid));
	EXPECT_EQ(EPERM, errno);
}

TEST_F_SIGNAL(TRACE_syscall, kill_after_ptrace, SIGSYS)
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

	/* Swap SECCOMP_RET_TRACE tracer for PTRACE_SYSCALL tracer. */
	teardown_trace_fixture(_metadata, self->tracer);
	self->tracer = setup_trace_fixture(_metadata, tracer_ptrace, NULL,
					   true);

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	/* Install "death on getppid" filter. */
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does not support PR_SET_NO_NEW_PRIVS!");
	}

	/* Reject insane operation. */
	ret = seccomp(-1, 0, &prog);
	ASSERT_NE(ENOSYS, errno) {
		TH_LOG("Kernel does not support seccomp syscall!");
	}
	EXPECT_EQ(EINVAL, errno) {
		TH_LOG("Did not reject crazy op value!");
	}

	/* Reject strict with flags or pointer. */
	ret = seccomp(SECCOMP_SET_MODE_STRICT, -1, NULL);
	EXPECT_EQ(EINVAL, errno) {
		TH_LOG("Did not reject mode strict with flags!");
	}
	ret = seccomp(SECCOMP_SET_MODE_STRICT, 0, &prog);
	EXPECT_EQ(EINVAL, errno) {
		TH_LOG("Did not reject mode strict with uargs!");
	}

	/* Reject insane args for filter. */
	ret = seccomp(SECCOMP_SET_MODE_FILTER, -1, &prog);
	EXPECT_EQ(EINVAL, errno) {
		TH_LOG("Did not reject crazy filter flags!");
	}
	ret = seccomp(SECCOMP_SET_MODE_FILTER, 0, NULL);
	EXPECT_EQ(EFAULT, errno) {
		TH_LOG("Did not reject NULL filter!");
	}

	ret = seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog);
	EXPECT_EQ(0, errno) {
		TH_LOG("Kernel does not support SECCOMP_SET_MODE_FILTER: %s",
			strerror(errno));
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, NULL, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does not support PR_SET_NO_NEW_PRIVS!");
	}

	ret = seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog);
	ASSERT_NE(ENOSYS, errno) {
		TH_LOG("Kernel does not support seccomp syscall!");
	}
	EXPECT_EQ(0, ret) {
		TH_LOG("Could not install filter!");
	}

	/* Make sure neither entry point will switch to strict. */
	ret = prctl(PR_SET_SECCOMP, SECCOMP_MODE_STRICT, 0, 0, 0);
	EXPECT_EQ(EINVAL, errno) {
		TH_LOG("Switched to mode strict!");
	}

	ret = seccomp(SECCOMP_SET_MODE_STRICT, 0, NULL);
	EXPECT_EQ(EINVAL, errno) {
		TH_LOG("Switched to mode strict!");
	}
}

/*
 * Test detection of known and unknown filter flags. Userspace needs to be able
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
				 SECCOMP_FILTER_FLAG_NEW_LISTENER };
	unsigned int flag, all_flags;
	int i;
	long ret;

	/* Test detection of known-good filter flags */
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
		ASSERT_NE(ENOSYS, errno) {
			TH_LOG("Kernel does not support seccomp syscall!");
		}
		EXPECT_EQ(-1, ret);
		EXPECT_EQ(EFAULT, errno) {
			TH_LOG("Failed to detect that a known-good filter flag (0x%X) is supported!",
			       flag);
		}

		all_flags |= flag;
	}

	/* Test detection of all known-good filter flags */
	ret = seccomp(SECCOMP_SET_MODE_FILTER, all_flags, NULL);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EFAULT, errno) {
		TH_LOG("Failed to detect that all known-good filter flags (0x%X) are supported!",
		       all_flags);
	}

	/* Test detection of an unknown filter flag */
	flag = -1;
	ret = seccomp(SECCOMP_SET_MODE_FILTER, flag, NULL);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EINVAL, errno) {
		TH_LOG("Failed to detect that an unknown filter flag (0x%X) is unsupported!",
		       flag);
	}

	/*
	 * Test detection of an unknown filter flag that may simply need to be
	 * added to this test
	 */
	flag = flags[ARRAY_SIZE(flags) - 1] << 1;
	ret = seccomp(SECCOMP_SET_MODE_FILTER, flag, NULL);
	EXPECT_EQ(-1, ret);
	EXPECT_EQ(EINVAL, errno) {
		TH_LOG("Failed to detect that an unknown filter flag (0x%X) is unsupported! Does a new flag need to be added to this test?",
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, NULL, 0, 0);
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does not support PR_SET_NO_NEW_PRIVS!");
	}

	ret = seccomp(SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC,
		      &prog);
	ASSERT_NE(ENOSYS, errno) {
		TH_LOG("Kernel does not support seccomp syscall!");
	}
	EXPECT_EQ(0, ret) {
		TH_LOG("Could not install initial filter with TSYNC!");
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
 * To avoid joining joined threads (which is not allowed by Bionic),
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

FIXTURE_DATA(TSYNC) {
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
	/* Return outside of started so parent notices failures. */
	if (ret) {
		pthread_mutex_unlock(me->mutex);
		return (void *)SIBLING_EXIT_FAILURE;
	}
	do {
		pthread_cond_wait(me->cond, me->mutex);
		me->num_waits = me->num_waits - 1;
	} while (me->num_waits);
	pthread_mutex_unlock(me->mutex);

	ret = prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0);
	if (!ret)
		return (void *)SIBLING_EXIT_NEWPRIVS;
	read(0, NULL, 0);
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
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ERRNO | EINVAL),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};

	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
		TH_LOG("Kernel does not support PR_SET_NO_NEW_PRIVS!");
	}

	/* Check prctl failure detection by requesting sib 0 diverge. */
	ret = seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog);
	ASSERT_NE(ENOSYS, errno) {
		TH_LOG("Kernel does not support seccomp syscall!");
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
		TH_LOG("cond broadcast non-zero");
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

	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
		TH_LOG("Kernel does not support PR_SET_NO_NEW_PRIVS!");
	}

	ret = seccomp(SECCOMP_SET_MODE_FILTER, 0, &self->root_prog);
	ASSERT_NE(ENOSYS, errno) {
		TH_LOG("Kernel does not support seccomp syscall!");
	}
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does not support SECCOMP_SET_MODE_FILTER!");
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
		TH_LOG("cond broadcast non-zero");
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

	/* Tell the siblings to test no policy */
	pthread_mutex_lock(&self->mutex);
	ASSERT_EQ(0, pthread_cond_broadcast(&self->cond)) {
		TH_LOG("cond broadcast non-zero");
	}
	pthread_mutex_unlock(&self->mutex);

	/* Ensure they are both upset about lacking nnp. */
	PTHREAD_JOIN(self->sibling[0].tid, &status);
	EXPECT_EQ(SIBLING_EXIT_NEWPRIVS, (long)status);
	PTHREAD_JOIN(self->sibling[1].tid, &status);
	EXPECT_EQ(SIBLING_EXIT_NEWPRIVS, (long)status);
}

TEST_F(TSYNC, two_siblings_with_no_filter)
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

	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
		TH_LOG("Kernel does not support PR_SET_NO_NEW_PRIVS!");
	}

	ret = seccomp(SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC,
		      &self->apply_prog);
	ASSERT_NE(ENOSYS, errno) {
		TH_LOG("Kernel does not support seccomp syscall!");
	}
	ASSERT_EQ(0, ret) {
		TH_LOG("Could install filter on all threads!");
	}

	/* Tell the siblings to test the policy */
	pthread_mutex_lock(&self->mutex);
	ASSERT_EQ(0, pthread_cond_broadcast(&self->cond)) {
		TH_LOG("cond broadcast non-zero");
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

	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
		TH_LOG("Kernel does not support PR_SET_NO_NEW_PRIVS!");
	}

	ret = seccomp(SECCOMP_SET_MODE_FILTER, 0, &self->root_prog);
	ASSERT_NE(ENOSYS, errno) {
		TH_LOG("Kernel does not support seccomp syscall!");
	}
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does not support SECCOMP_SET_MODE_FILTER!");
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
		TH_LOG("Did not fail on diverged sibling.");
	}

	/* Wake the threads */
	pthread_mutex_lock(&self->mutex);
	ASSERT_EQ(0, pthread_cond_broadcast(&self->cond)) {
		TH_LOG("cond broadcast non-zero");
	}
	pthread_mutex_unlock(&self->mutex);

	/* Ensure they are both unkilled. */
	PTHREAD_JOIN(self->sibling[0].tid, &status);
	EXPECT_EQ(SIBLING_EXIT_UNKILLED, (long)status);
	PTHREAD_JOIN(self->sibling[1].tid, &status);
	EXPECT_EQ(SIBLING_EXIT_UNKILLED, (long)status);
}

TEST_F(TSYNC, two_siblings_not_under_filter)
{
	long ret, sib;
	void *status;

	ASSERT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
		TH_LOG("Kernel does not support PR_SET_NO_NEW_PRIVS!");
	}

	/*
	 * Sibling 0 will have its own seccomp policy
	 * and Sibling 1 will not be under seccomp at
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
	ASSERT_NE(ENOSYS, errno) {
		TH_LOG("Kernel does not support seccomp syscall!");
	}
	ASSERT_EQ(0, ret) {
		TH_LOG("Kernel does not support SECCOMP_SET_MODE_FILTER!");
	}

	ret = seccomp(SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC,
		      &self->apply_prog);
	ASSERT_EQ(ret, self->sibling[0].system_tid) {
		TH_LOG("Did not fail on diverged sibling.");
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
		TH_LOG("cond broadcast non-zero");
	}
	pthread_mutex_unlock(&self->mutex);
	PTHREAD_JOIN(self->sibling[sib].tid, &status);
	EXPECT_EQ(SIBLING_EXIT_UNKILLED, (long)status);
	/* Poll for actual task death. pthread_join doesn't guarantee it. */
	while (!kill(self->sibling[sib].system_tid, 0))
		sleep(0.1);
	/* Switch to the remaining sibling */
	sib = !sib;

	ret = seccomp(SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC,
		      &self->apply_prog);
	ASSERT_EQ(0, ret) {
		TH_LOG("Expected the remaining sibling to sync");
	};

	pthread_mutex_lock(&self->mutex);

	/* If remaining sibling didn't have a chance to wake up during
	 * the first broadcast, manually reduce the num_waits now.
	 */
	if (self->sibling[sib].num_waits > 1)
		self->sibling[sib].num_waits = 1;
	ASSERT_EQ(0, pthread_cond_broadcast(&self->cond)) {
		TH_LOG("cond broadcast non-zero");
	}
	pthread_mutex_unlock(&self->mutex);
	PTHREAD_JOIN(self->sibling[sib].tid, &status);
	EXPECT_EQ(0, (long)status);
	/* Poll for actual task death. pthread_join doesn't guarantee it. */
	while (!kill(self->sibling[sib].system_tid, 0))
		sleep(0.1);

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
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_sigreturn, 6, 0),
#endif
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_read, 5, 0),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_exit, 4, 0),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_rt_sigreturn, 3, 0),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_nanosleep, 4, 0),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_restart_syscall, 4, 0),

		/* Allow __NR_write for easy logging. */
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, __NR_write, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_KILL),
		/* The nanosleep jump target. */
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
		/* Child uses EXPECT not ASSERT to deliver status correctly. */
		char buf = ' ';
		struct timespec timeout = { };

		/* Attach parent as tracer and stop. */
		EXPECT_EQ(0, ptrace(PTRACE_TRACEME));
		EXPECT_EQ(0, raise(SIGSTOP));

		EXPECT_EQ(0, close(pipefd[1]));

		EXPECT_EQ(0, prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
			TH_LOG("Kernel does not support PR_SET_NO_NEW_PRIVS!");
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

		/* Start nanosleep to be interrupted. */
		timeout.tv_sec = 1;
		errno = 0;
		EXPECT_EQ(0, nanosleep(&timeout, NULL)) {
			TH_LOG("Call to nanosleep() failed (errno %d)", errno);
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

	/* Wait for nanosleep() to start. */
	ASSERT_EQ(child_pid, waitpid(child_pid, &status, 0));
	ASSERT_EQ(true, WIFSTOPPED(status));
	ASSERT_EQ(SIGTRAP, WSTOPSIG(status));
	ASSERT_EQ(PTRACE_EVENT_SECCOMP, (status >> 16));
	ASSERT_EQ(0, ptrace(PTRACE_GETEVENTMSG, child_pid, NULL, &msg));
	ASSERT_EQ(0x100, msg);
	EXPECT_EQ(__NR_nanosleep, get_syscall(_metadata, child_pid));

	/* Might as well check siginfo for sanity while we're here. */
	ASSERT_EQ(0, ptrace(PTRACE_GETSIGINFO, child_pid, NULL, &info));
	ASSERT_EQ(SIGTRAP, info.si_signo);
	ASSERT_EQ(SIGTRAP | (PTRACE_EVENT_SECCOMP << 8), info.si_code);
	EXPECT_EQ(0, info.si_errno);
	EXPECT_EQ(getuid(), info.si_uid);
	/* Verify signal delivery came from child (seccomp-triggered). */
	EXPECT_EQ(child_pid, info.si_pid);

	/* Interrupt nanosleep with SIGSTOP (which we'll need to handle). */
	ASSERT_EQ(0, kill(child_pid, SIGSTOP));
	ASSERT_EQ(0, ptrace(PTRACE_CONT, child_pid, NULL, 0));
	ASSERT_EQ(child_pid, waitpid(child_pid, &status, 0));
	ASSERT_EQ(true, WIFSTOPPED(status));
	ASSERT_EQ(SIGSTOP, WSTOPSIG(status));
	ASSERT_EQ(0, ptrace(PTRACE_GETSIGINFO, child_pid, NULL, &info));
	/*
	 * There is no siginfo on SIGSTOP any more, so we can't verify
	 * signal delivery came from parent now (getpid() == info.si_pid).
	 * https://lkml.kernel.org/r/CAGXu5jJaZAOzP1qFz66tYrtbuywqb+UN2SOA1VLHpCCOiYvYeg@mail.gmail.com
	 * At least verify the SIGSTOP via PTRACE_GETSIGINFO.
	 */
	EXPECT_EQ(SIGSTOP, info.si_signo);

	/* Restart nanosleep with SIGCONT, which triggers restart_syscall. */
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
	 * - native ARM registers do NOT expose true syscall.
	 * - compat ARM registers on ARM64 DO expose true syscall.
	 */
	ASSERT_EQ(0, uname(&utsbuf));
	if (strncmp(utsbuf.machine, "arm", 3) == 0) {
		EXPECT_EQ(__NR_nanosleep, ret);
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

	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	ASSERT_EQ(0, ret);

	/* Verify that the FILTER_FLAG_LOG flag isn't accepted in strict mode */
	ret = seccomp(SECCOMP_SET_MODE_STRICT, SECCOMP_FILTER_FLAG_LOG,
		      &allow_prog);
	ASSERT_NE(ENOSYS, errno) {
		TH_LOG("Kernel does not support seccomp syscall!");
	}
	EXPECT_NE(0, ret) {
		TH_LOG("Kernel accepted FILTER_FLAG_LOG flag in strict mode!");
	}
	EXPECT_EQ(EINVAL, errno) {
		TH_LOG("Kernel returned unexpected errno for FILTER_FLAG_LOG flag in strict mode!");
	}

	/* Verify that a simple, permissive filter can be added with no flags */
	ret = seccomp(SECCOMP_SET_MODE_FILTER, 0, &allow_prog);
	EXPECT_EQ(0, ret);

	/* See if the same filter can be added with the FILTER_FLAG_LOG flag */
	ret = seccomp(SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_LOG,
		      &allow_prog);
	ASSERT_NE(EINVAL, errno) {
		TH_LOG("Kernel does not support the FILTER_FLAG_LOG flag!");
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
			    SECCOMP_RET_ERRNO, SECCOMP_RET_TRACE,
			    SECCOMP_RET_LOG,   SECCOMP_RET_ALLOW };
	__u32 unknown_action = 0x10000000U;
	int i;
	long ret;

	ret = seccomp(SECCOMP_GET_ACTION_AVAIL, 0, &actions[0]);
	ASSERT_NE(ENOSYS, errno) {
		TH_LOG("Kernel does not support seccomp syscall!");
	}
	ASSERT_NE(EINVAL, errno) {
		TH_LOG("Kernel does not support SECCOMP_GET_ACTION_AVAIL operation!");
	}
	EXPECT_EQ(ret, 0);

	for (i = 0; i < ARRAY_SIZE(actions); i++) {
		ret = seccomp(SECCOMP_GET_ACTION_AVAIL, 0, &actions[i]);
		EXPECT_EQ(ret, 0) {
			TH_LOG("Expected action (0x%X) not available!",
			       actions[i]);
		}
	}

	/* Check that an unknown action is handled properly (EOPNOTSUPP) */
	ret = seccomp(SECCOMP_GET_ACTION_AVAIL, 0, &unknown_action);
	EXPECT_EQ(ret, -1);
	EXPECT_EQ(errno, EOPNOTSUPP);
}

TEST(get_metadata)
{
	pid_t pid;
	int pipefd[2];
	char buf;
	struct seccomp_metadata md;
	long ret;

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
		ASSERT_EQ(0, seccomp(SECCOMP_SET_MODE_FILTER,
				     SECCOMP_FILTER_FLAG_LOG, &prog));
		ASSERT_EQ(0, seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog));

		ASSERT_EQ(0, close(pipefd[0]));
		ASSERT_EQ(1, write(pipefd[1], "1", 1));
		ASSERT_EQ(0, close(pipefd[1]));

		while (1)
			sleep(100);
	}

	ASSERT_EQ(0, close(pipefd[1]));
	ASSERT_EQ(1, read(pipefd[0], &buf, 1));

	ASSERT_EQ(0, ptrace(PTRACE_ATTACH, pid));
	ASSERT_EQ(pid, waitpid(pid, NULL, 0));

	/* Past here must not use ASSERT or child process is never killed. */

	md.filter_off = 0;
	errno = 0;
	ret = ptrace(PTRACE_SECCOMP_GET_METADATA, pid, sizeof(md), &md);
	EXPECT_EQ(sizeof(md), ret) {
		if (errno == EINVAL)
			XFAIL(goto skip, "Kernel does not support PTRACE_SECCOMP_GET_METADATA (missing CONFIG_CHECKPOINT_RESTORE?)");
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

static int user_trap_syscall(int nr, unsigned int flags)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD+BPF_W+BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, nr, 0, 1),
		BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_USER_NOTIF),
		BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),
	};

	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};

	return seccomp(SECCOMP_SET_MODE_FILTER, flags, &prog);
}

#define USER_NOTIF_MAGIC 116983961184613L
TEST(user_notification_basic)
{
	pid_t pid;
	long ret;
	int status, listener;
	struct seccomp_notif req = {};
	struct seccomp_notif_resp resp = {};
	struct pollfd pollfd;

	struct sock_filter filter[] = {
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};

	pid = fork();
	ASSERT_GE(pid, 0);

	/* Check that we get -ENOSYS with no listener attached */
	if (pid == 0) {
		if (user_trap_syscall(__NR_getpid, 0) < 0)
			exit(1);
		ret = syscall(__NR_getpid);
		exit(ret >= 0 || errno != ENOSYS);
	}

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));

	/* Add some no-op filters so for grins. */
	EXPECT_EQ(seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog), 0);
	EXPECT_EQ(seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog), 0);
	EXPECT_EQ(seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog), 0);
	EXPECT_EQ(seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog), 0);

	/* Check that the basic notification machinery works */
	listener = user_trap_syscall(__NR_getpid,
				     SECCOMP_FILTER_FLAG_NEW_LISTENER);
	ASSERT_GE(listener, 0);

	/* Installing a second listener in the chain should EBUSY */
	EXPECT_EQ(user_trap_syscall(__NR_getpid,
				    SECCOMP_FILTER_FLAG_NEW_LISTENER),
		  -1);
	EXPECT_EQ(errno, EBUSY);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ret = syscall(__NR_getpid);
		exit(ret != USER_NOTIF_MAGIC);
	}

	pollfd.fd = listener;
	pollfd.events = POLLIN | POLLOUT;

	EXPECT_GT(poll(&pollfd, 1, -1), 0);
	EXPECT_EQ(pollfd.revents, POLLIN);

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_NOTIF_RECV, &req), 0);

	pollfd.fd = listener;
	pollfd.events = POLLIN | POLLOUT;

	EXPECT_GT(poll(&pollfd, 1, -1), 0);
	EXPECT_EQ(pollfd.revents, POLLOUT);

	EXPECT_EQ(req.data.nr,  __NR_getpid);

	resp.id = req.id;
	resp.error = 0;
	resp.val = USER_NOTIF_MAGIC;

	/* check that we make sure flags == 0 */
	resp.flags = 1;
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_NOTIF_SEND, &resp), -1);
	EXPECT_EQ(errno, EINVAL);

	resp.flags = 0;
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_NOTIF_SEND, &resp), 0);

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(user_notification_kill_in_middle)
{
	pid_t pid;
	long ret;
	int listener;
	struct seccomp_notif req = {};
	struct seccomp_notif_resp resp = {};

	listener = user_trap_syscall(__NR_getpid,
				     SECCOMP_FILTER_FLAG_NEW_LISTENER);
	ASSERT_GE(listener, 0);

	/*
	 * Check that nothing bad happens when we kill the task in the middle
	 * of a syscall.
	 */
	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ret = syscall(__NR_getpid);
		exit(ret != USER_NOTIF_MAGIC);
	}

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_NOTIF_RECV, &req), 0);
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_NOTIF_ID_VALID, &req.id), 0);

	EXPECT_EQ(kill(pid, SIGKILL), 0);
	EXPECT_EQ(waitpid(pid, NULL, 0), pid);

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_NOTIF_ID_VALID, &req.id), -1);

	resp.id = req.id;
	ret = ioctl(listener, SECCOMP_IOCTL_NOTIF_SEND, &resp);
	EXPECT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOENT);
}

static int handled = -1;

static void signal_handler(int signal)
{
	if (write(handled, "c", 1) != 1)
		perror("write from signal");
}

TEST(user_notification_signal)
{
	pid_t pid;
	long ret;
	int status, listener, sk_pair[2];
	struct seccomp_notif req = {};
	struct seccomp_notif_resp resp = {};
	char c;

	ASSERT_EQ(socketpair(PF_LOCAL, SOCK_SEQPACKET, 0, sk_pair), 0);

	listener = user_trap_syscall(__NR_gettid,
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
		 * to rely on a signal that has not yet been handled. Let's at
		 * least check that the error code gets propagated through, and
		 * hope that it doesn't break when there is actually a signal :)
		 */
		ret = syscall(__NR_gettid);
		exit(!(ret == -1 && errno == 512));
	}

	close(sk_pair[1]);

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_NOTIF_RECV, &req), 0);

	EXPECT_EQ(kill(pid, SIGUSR1), 0);

	/*
	 * Make sure the signal really is delivered, which means we're not
	 * stuck in the user notification code any more and the notification
	 * should be dead.
	 */
	EXPECT_EQ(read(sk_pair[0], &c, 1), 1);

	resp.id = req.id;
	resp.error = -EPERM;
	resp.val = 0;

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_NOTIF_SEND, &resp), -1);
	EXPECT_EQ(errno, ENOENT);

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_NOTIF_RECV, &req), 0);

	resp.id = req.id;
	resp.error = -512; /* -ERESTARTSYS */
	resp.val = 0;

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_NOTIF_SEND, &resp), 0);

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(user_notification_closed_listener)
{
	pid_t pid;
	long ret;
	int status, listener;

	listener = user_trap_syscall(__NR_getpid,
				     SECCOMP_FILTER_FLAG_NEW_LISTENER);
	ASSERT_GE(listener, 0);

	/*
	 * Check that we get an ENOSYS when the listener is closed.
	 */
	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0) {
		close(listener);
		ret = syscall(__NR_getpid);
		exit(ret != -1 && errno != ENOSYS);
	}

	close(listener);

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
}

/*
 * Check that a pid in a child namespace still shows up as valid in ours.
 */
TEST(user_notification_child_pid_ns)
{
	pid_t pid;
	int status, listener;
	struct seccomp_notif req = {};
	struct seccomp_notif_resp resp = {};

	ASSERT_EQ(unshare(CLONE_NEWPID), 0);

	listener = user_trap_syscall(__NR_getpid, SECCOMP_FILTER_FLAG_NEW_LISTENER);
	ASSERT_GE(listener, 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0)
		exit(syscall(__NR_getpid) != USER_NOTIF_MAGIC);

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_NOTIF_RECV, &req), 0);
	EXPECT_EQ(req.pid, pid);

	resp.id = req.id;
	resp.error = 0;
	resp.val = USER_NOTIF_MAGIC;

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_NOTIF_SEND, &resp), 0);

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
	close(listener);
}

/*
 * Check that a pid in a sibling (i.e. unrelated) namespace shows up as 0, i.e.
 * invalid.
 */
TEST(user_notification_sibling_pid_ns)
{
	pid_t pid, pid2;
	int status, listener;
	struct seccomp_notif req = {};
	struct seccomp_notif_resp resp = {};

	listener = user_trap_syscall(__NR_getpid, SECCOMP_FILTER_FLAG_NEW_LISTENER);
	ASSERT_GE(listener, 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ASSERT_EQ(unshare(CLONE_NEWPID), 0);

		pid2 = fork();
		ASSERT_GE(pid2, 0);

		if (pid2 == 0)
			exit(syscall(__NR_getpid) != USER_NOTIF_MAGIC);

		EXPECT_EQ(waitpid(pid2, &status, 0), pid2);
		EXPECT_EQ(true, WIFEXITED(status));
		EXPECT_EQ(0, WEXITSTATUS(status));
		exit(WEXITSTATUS(status));
	}

	/* Create the sibling ns, and sibling in it. */
	EXPECT_EQ(unshare(CLONE_NEWPID), 0);
	EXPECT_EQ(errno, 0);

	pid2 = fork();
	EXPECT_GE(pid2, 0);

	if (pid2 == 0) {
		ASSERT_EQ(ioctl(listener, SECCOMP_IOCTL_NOTIF_RECV, &req), 0);
		/*
		 * The pid should be 0, i.e. the task is in some namespace that
		 * we can't "see".
		 */
		ASSERT_EQ(req.pid, 0);

		resp.id = req.id;
		resp.error = 0;
		resp.val = USER_NOTIF_MAGIC;

		ASSERT_EQ(ioctl(listener, SECCOMP_IOCTL_NOTIF_SEND, &resp), 0);
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

TEST(user_notification_fault_recv)
{
	pid_t pid;
	int status, listener;
	struct seccomp_notif req = {};
	struct seccomp_notif_resp resp = {};

	listener = user_trap_syscall(__NR_getpid, SECCOMP_FILTER_FLAG_NEW_LISTENER);
	ASSERT_GE(listener, 0);

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0)
		exit(syscall(__NR_getpid) != USER_NOTIF_MAGIC);

	/* Do a bad recv() */
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_NOTIF_RECV, NULL), -1);
	EXPECT_EQ(errno, EFAULT);

	/* We should still be able to receive this notification, though. */
	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_NOTIF_RECV, &req), 0);
	EXPECT_EQ(req.pid, pid);

	resp.id = req.id;
	resp.error = 0;
	resp.val = USER_NOTIF_MAGIC;

	EXPECT_EQ(ioctl(listener, SECCOMP_IOCTL_NOTIF_SEND, &resp), 0);

	EXPECT_EQ(waitpid(pid, &status, 0), pid);
	EXPECT_EQ(true, WIFEXITED(status));
	EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(seccomp_get_notif_sizes)
{
	struct seccomp_notif_sizes sizes;

	ASSERT_EQ(seccomp(SECCOMP_GET_NOTIF_SIZES, 0, &sizes), 0);
	EXPECT_EQ(sizes.seccomp_notif, sizeof(struct seccomp_notif));
	EXPECT_EQ(sizes.seccomp_notif_resp, sizeof(struct seccomp_notif_resp));
}

/*
 * TODO:
 * - add microbenchmarks
 * - expand NNP testing
 * - better arch-specific TRACE and TRAP handlers.
 * - endianness checking when appropriate
 * - 64-bit arg prodding
 * - arch value testing (x86 modes especially)
 * - verify that FILTER_FLAG_LOG filters generate log messages
 * - verify that RET_LOG generates log messages
 * - ...
 */

TEST_HARNESS_MAIN
