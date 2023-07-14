/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * s390 specific definitions for NOLIBC
 */

#ifndef _NOLIBC_ARCH_S390_H
#define _NOLIBC_ARCH_S390_H
#include <asm/signal.h>
#include <asm/unistd.h>

#include "compiler.h"

/* The struct returned by the stat() syscall, equivalent to stat64(). The
 * syscall returns 116 bytes and stops in the middle of __unused.
 */

struct sys_stat_struct {
	unsigned long	st_dev;
	unsigned long	st_ino;
	unsigned long	st_nlink;
	unsigned int	st_mode;
	unsigned int	st_uid;
	unsigned int	st_gid;
	unsigned int	__pad1;
	unsigned long	st_rdev;
	unsigned long	st_size;
	unsigned long	st_atime;
	unsigned long	st_atime_nsec;
	unsigned long	st_mtime;
	unsigned long	st_mtime_nsec;
	unsigned long	st_ctime;
	unsigned long	st_ctime_nsec;
	unsigned long	st_blksize;
	long		st_blocks;
	unsigned long	__unused[3];
};

/* Syscalls for s390:
 *   - registers are 64-bit
 *   - syscall number is passed in r1
 *   - arguments are in r2-r7
 *   - the system call is performed by calling the svc instruction
 *   - syscall return value is in r2
 *   - r1 and r2 are clobbered, others are preserved.
 *
 * Link s390 ABI: https://github.com/IBM/s390x-abi
 *
 */

#define my_syscall0(num)						\
({									\
	register long _num __asm__ ("1") = (num);			\
	register long _rc __asm__ ("2");				\
									\
	__asm__  volatile (						\
		"svc 0\n"						\
		: "=d"(_rc)						\
		: "d"(_num)						\
		: "memory", "cc"					\
		);							\
	_rc;								\
})

#define my_syscall1(num, arg1)						\
({									\
	register long _num __asm__ ("1") = (num);			\
	register long _arg1 __asm__ ("2") = (long)(arg1);		\
									\
	__asm__  volatile (						\
		"svc 0\n"						\
		: "+d"(_arg1)						\
		: "d"(_num)						\
		: "memory", "cc"					\
		);							\
	_arg1;								\
})

#define my_syscall2(num, arg1, arg2)					\
({									\
	register long _num __asm__ ("1") = (num);			\
	register long _arg1 __asm__ ("2") = (long)(arg1);		\
	register long _arg2 __asm__ ("3") = (long)(arg2);		\
									\
	__asm__  volatile (						\
		"svc 0\n"						\
		: "+d"(_arg1)						\
		: "d"(_arg2), "d"(_num)					\
		: "memory", "cc"					\
		);							\
	_arg1;								\
})

#define my_syscall3(num, arg1, arg2, arg3)				\
({									\
	register long _num __asm__ ("1") = (num);			\
	register long _arg1 __asm__ ("2") = (long)(arg1);		\
	register long _arg2 __asm__ ("3") = (long)(arg2);		\
	register long _arg3 __asm__ ("4") = (long)(arg3);		\
									\
	__asm__  volatile (						\
		"svc 0\n"						\
		: "+d"(_arg1)						\
		: "d"(_arg2), "d"(_arg3), "d"(_num)			\
		: "memory", "cc"					\
		);							\
	_arg1;								\
})

#define my_syscall4(num, arg1, arg2, arg3, arg4)			\
({									\
	register long _num __asm__ ("1") = (num);			\
	register long _arg1 __asm__ ("2") = (long)(arg1);		\
	register long _arg2 __asm__ ("3") = (long)(arg2);		\
	register long _arg3 __asm__ ("4") = (long)(arg3);		\
	register long _arg4 __asm__ ("5") = (long)(arg4);		\
									\
	__asm__  volatile (						\
		"svc 0\n"						\
		: "+d"(_arg1)						\
		: "d"(_arg2), "d"(_arg3), "d"(_arg4), "d"(_num)		\
		: "memory", "cc"					\
		);							\
	_arg1;								\
})

#define my_syscall5(num, arg1, arg2, arg3, arg4, arg5)			\
({									\
	register long _num __asm__ ("1") = (num);			\
	register long _arg1 __asm__ ("2") = (long)(arg1);		\
	register long _arg2 __asm__ ("3") = (long)(arg2);		\
	register long _arg3 __asm__ ("4") = (long)(arg3);		\
	register long _arg4 __asm__ ("5") = (long)(arg4);		\
	register long _arg5 __asm__ ("6") = (long)(arg5);		\
									\
	__asm__  volatile (						\
		"svc 0\n"						\
		: "+d"(_arg1)						\
		: "d"(_arg2), "d"(_arg3), "d"(_arg4), "d"(_arg5),	\
		  "d"(_num)						\
		: "memory", "cc"					\
		);							\
	_arg1;								\
})

#define my_syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6)		\
({									\
	register long _num __asm__ ("1") = (num);			\
	register long _arg1 __asm__ ("2") = (long)(arg1);		\
	register long _arg2 __asm__ ("3") = (long)(arg2);		\
	register long _arg3 __asm__ ("4") = (long)(arg3);		\
	register long _arg4 __asm__ ("5") = (long)(arg4);		\
	register long _arg5 __asm__ ("6") = (long)(arg5);		\
	register long _arg6 __asm__ ("7") = (long)(arg6);		\
									\
	__asm__  volatile (						\
		"svc 0\n"						\
		: "+d"(_arg1)						\
		: "d"(_arg2), "d"(_arg3), "d"(_arg4), "d"(_arg5),	\
		  "d"(_arg6), "d"(_num)					\
		: "memory", "cc"					\
		);							\
	_arg1;								\
})

char **environ __attribute__((weak));
const unsigned long *_auxv __attribute__((weak));

/* startup code */
void __attribute__((weak,noreturn,optimize("omit-frame-pointer"))) __no_stack_protector _start(void)
{
	__asm__ volatile (
		"lg	%r2,0(%r15)\n"		/* argument count */
		"la	%r3,8(%r15)\n"		/* argument pointers */

		"xgr	%r0,%r0\n"		/* r0 will be our NULL value */
		/* search for envp */
		"lgr	%r4,%r3\n"		/* start at argv */
		"0:\n"
		"clg	%r0,0(%r4)\n"		/* entry zero? */
		"la	%r4,8(%r4)\n"		/* advance pointer */
		"jnz	0b\n"			/* no -> test next pointer */
						/* yes -> r4 now contains start of envp */
		"larl	%r1,environ\n"
		"stg	%r4,0(%r1)\n"

		/* search for auxv */
		"lgr	%r5,%r4\n"		/* start at envp */
		"1:\n"
		"clg	%r0,0(%r5)\n"		/* entry zero? */
		"la	%r5,8(%r5)\n"		/* advance pointer */
		"jnz	1b\n"			/* no -> test next pointer */
		"larl	%r1,_auxv\n"		/* yes -> store value in _auxv */
		"stg	%r5,0(%r1)\n"

		"aghi	%r15,-160\n"		/* allocate new stackframe */
		"xc	0(8,%r15),0(%r15)\n"	/* clear backchain */
		"brasl	%r14,main\n"		/* ret value of main is arg to exit */
		"lghi	%r1,1\n"		/* __NR_exit */
		"svc	0\n"
	);
	__builtin_unreachable();
}

struct s390_mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

static __attribute__((unused))
void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd,
	       off_t offset)
{
	struct s390_mmap_arg_struct args = {
		.addr = (unsigned long)addr,
		.len = (unsigned long)length,
		.prot = prot,
		.flags = flags,
		.fd = fd,
		.offset = (unsigned long)offset
	};

	return (void *)my_syscall1(__NR_mmap, &args);
}
#define sys_mmap sys_mmap

static __attribute__((unused))
pid_t sys_fork(void)
{
	return my_syscall5(__NR_clone, 0, SIGCHLD, 0, 0, 0);
}
#define sys_fork sys_fork

#endif /* _NOLIBC_ARCH_S390_H */
