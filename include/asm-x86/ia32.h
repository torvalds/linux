#ifndef _ASM_X86_64_IA32_H
#define _ASM_X86_64_IA32_H


#ifdef CONFIG_IA32_EMULATION

#include <linux/compat.h>

/*
 * 32 bit structures for IA32 support.
 */

#include <asm/sigcontext32.h>

/* signal.h */
struct sigaction32 {
       unsigned int  sa_handler;	/* Really a pointer, but need to deal 
					     with 32 bits */
       unsigned int sa_flags;
       unsigned int sa_restorer;	/* Another 32 bit pointer */
       compat_sigset_t sa_mask;		/* A 32 bit mask */
};

struct old_sigaction32 {
       unsigned int  sa_handler;	/* Really a pointer, but need to deal 
					     with 32 bits */
       compat_old_sigset_t sa_mask;		/* A 32 bit mask */
       unsigned int sa_flags;
       unsigned int sa_restorer;	/* Another 32 bit pointer */
};

typedef struct sigaltstack_ia32 {
	unsigned int	ss_sp;
	int		ss_flags;
	unsigned int	ss_size;
} stack_ia32_t;

struct ucontext_ia32 {
	unsigned int	  uc_flags;
	unsigned int 	  uc_link;
	stack_ia32_t	  uc_stack;
	struct sigcontext_ia32 uc_mcontext;
	compat_sigset_t	  uc_sigmask;	/* mask last for extensibility */
};

/* This matches struct stat64 in glibc2.2, hence the absolutely
 * insane amounts of padding around dev_t's.
 */
struct stat64 {
	unsigned long long	st_dev;
	unsigned char		__pad0[4];

#define STAT64_HAS_BROKEN_ST_INO	1
	unsigned int		__st_ino;

	unsigned int		st_mode;
	unsigned int		st_nlink;

	unsigned int		st_uid;
	unsigned int		st_gid;

	unsigned long long	st_rdev;
	unsigned char		__pad3[4];

	long long		st_size;
	unsigned int		st_blksize;

	long long		st_blocks;/* Number 512-byte blocks allocated. */

	unsigned 		st_atime;
	unsigned 		st_atime_nsec;
	unsigned 		st_mtime;
	unsigned 		st_mtime_nsec;
	unsigned 		st_ctime;
	unsigned 		st_ctime_nsec;

	unsigned long long	st_ino;
} __attribute__((packed));

typedef struct compat_siginfo{
	int si_signo;
	int si_errno;
	int si_code;

	union {
		int _pad[((128/sizeof(int)) - 3)];

		/* kill() */
		struct {
			unsigned int _pid;	/* sender's pid */
			unsigned int _uid;	/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			compat_timer_t _tid;	/* timer id */
			int _overrun;		/* overrun count */
			compat_sigval_t _sigval;	/* same as below */
			int _sys_private;	/* not to be passed to user */
			int _overrun_incr;	/* amount to add to overrun */
		} _timer;

		/* POSIX.1b signals */
		struct {
			unsigned int _pid;	/* sender's pid */
			unsigned int _uid;	/* sender's uid */
			compat_sigval_t _sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			unsigned int _pid;	/* which child */
			unsigned int _uid;	/* sender's uid */
			int _status;		/* exit code */
			compat_clock_t _utime;
			compat_clock_t _stime;
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS */
		struct {
			unsigned int _addr;	/* faulting insn/memory ref. */
		} _sigfault;

		/* SIGPOLL */
		struct {
			int _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;
	} _sifields;
} compat_siginfo_t;

struct sigframe32
{
        u32 pretcode;
        int sig;
        struct sigcontext_ia32 sc;
        struct _fpstate_ia32 fpstate;
        unsigned int extramask[_COMPAT_NSIG_WORDS-1];
};

struct rt_sigframe32
{
        u32 pretcode;
        int sig;
        u32 pinfo;
        u32 puc;
        compat_siginfo_t info;
        struct ucontext_ia32 uc;
        struct _fpstate_ia32 fpstate;
};

struct ustat32 {
	__u32	f_tfree;
	compat_ino_t		f_tinode;
	char			f_fname[6];
	char			f_fpack[6];
};

#define IA32_STACK_TOP IA32_PAGE_OFFSET

#ifdef __KERNEL__
struct linux_binprm;
extern int ia32_setup_arg_pages(struct linux_binprm *bprm,
				unsigned long stack_top, int exec_stack);
struct mm_struct;
extern void ia32_pick_mmap_layout(struct mm_struct *mm);

#endif

#endif /* !CONFIG_IA32_SUPPORT */
 
#endif 
