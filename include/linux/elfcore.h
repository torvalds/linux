#ifndef _LINUX_ELFCORE_H
#define _LINUX_ELFCORE_H

#include <linux/types.h>
#include <linux/signal.h>
#include <linux/time.h>
#ifdef __KERNEL__
#include <linux/user.h>
#endif
#include <linux/ptrace.h>
#include <linux/elf.h>
#include <linux/fs.h>

struct elf_siginfo
{
	int	si_signo;			/* signal number */
	int	si_code;			/* extra code */
	int	si_errno;			/* errno */
};

#ifdef __KERNEL__
#include <asm/elf.h>
#endif

#ifndef __KERNEL__
typedef elf_greg_t greg_t;
typedef elf_gregset_t gregset_t;
typedef elf_fpregset_t fpregset_t;
typedef elf_fpxregset_t fpxregset_t;
#define NGREG ELF_NGREG
#endif

/*
 * Definitions to generate Intel SVR4-like core files.
 * These mostly have the same names as the SVR4 types with "elf_"
 * tacked on the front to prevent clashes with linux definitions,
 * and the typedef forms have been avoided.  This is mostly like
 * the SVR4 structure, but more Linuxy, with things that Linux does
 * not support and which gdb doesn't really use excluded.
 * Fields present but not used are marked with "XXX".
 */
struct elf_prstatus
{
#if 0
	long	pr_flags;	/* XXX Process flags */
	short	pr_why;		/* XXX Reason for process halt */
	short	pr_what;	/* XXX More detailed reason */
#endif
	struct elf_siginfo pr_info;	/* Info associated with signal */
	short	pr_cursig;		/* Current signal */
	unsigned long pr_sigpend;	/* Set of pending signals */
	unsigned long pr_sighold;	/* Set of held signals */
#if 0
	struct sigaltstack pr_altstack;	/* Alternate stack info */
	struct sigaction pr_action;	/* Signal action for current sig */
#endif
	pid_t	pr_pid;
	pid_t	pr_ppid;
	pid_t	pr_pgrp;
	pid_t	pr_sid;
	struct timeval pr_utime;	/* User time */
	struct timeval pr_stime;	/* System time */
	struct timeval pr_cutime;	/* Cumulative user time */
	struct timeval pr_cstime;	/* Cumulative system time */
#if 0
	long	pr_instr;		/* Current instruction */
#endif
	elf_gregset_t pr_reg;	/* GP registers */
#ifdef CONFIG_BINFMT_ELF_FDPIC
	/* When using FDPIC, the loadmap addresses need to be communicated
	 * to GDB in order for GDB to do the necessary relocations.  The
	 * fields (below) used to communicate this information are placed
	 * immediately after ``pr_reg'', so that the loadmap addresses may
	 * be viewed as part of the register set if so desired.
	 */
	unsigned long pr_exec_fdpic_loadmap;
	unsigned long pr_interp_fdpic_loadmap;
#endif
	int pr_fpvalid;		/* True if math co-processor being used.  */
};

#define ELF_PRARGSZ	(80)	/* Number of chars for args */

struct elf_prpsinfo
{
	char	pr_state;	/* numeric process state */
	char	pr_sname;	/* char for pr_state */
	char	pr_zomb;	/* zombie */
	char	pr_nice;	/* nice val */
	unsigned long pr_flag;	/* flags */
	__kernel_uid_t	pr_uid;
	__kernel_gid_t	pr_gid;
	pid_t	pr_pid, pr_ppid, pr_pgrp, pr_sid;
	/* Lots missing */
	char	pr_fname[16];	/* filename of executable */
	char	pr_psargs[ELF_PRARGSZ];	/* initial part of arg list */
};

#ifndef __KERNEL__
typedef struct elf_prstatus prstatus_t;
typedef struct elf_prpsinfo prpsinfo_t;
#define PRARGSZ ELF_PRARGSZ 
#endif

#ifdef __KERNEL__
static inline void elf_core_copy_regs(elf_gregset_t *elfregs, struct pt_regs *regs)
{
#ifdef ELF_CORE_COPY_REGS
	ELF_CORE_COPY_REGS((*elfregs), regs)
#else
	BUG_ON(sizeof(*elfregs) != sizeof(*regs));
	*(struct pt_regs *)elfregs = *regs;
#endif
}

static inline void elf_core_copy_kernel_regs(elf_gregset_t *elfregs, struct pt_regs *regs)
{
#ifdef ELF_CORE_COPY_KERNEL_REGS
	ELF_CORE_COPY_KERNEL_REGS((*elfregs), regs);
#else
	elf_core_copy_regs(elfregs, regs);
#endif
}

static inline int elf_core_copy_task_regs(struct task_struct *t, elf_gregset_t* elfregs)
{
#if defined (ELF_CORE_COPY_TASK_REGS)
	return ELF_CORE_COPY_TASK_REGS(t, elfregs);
#elif defined (task_pt_regs)
	elf_core_copy_regs(elfregs, task_pt_regs(t));
#endif
	return 0;
}

extern int dump_fpu (struct pt_regs *, elf_fpregset_t *);

static inline int elf_core_copy_task_fpregs(struct task_struct *t, struct pt_regs *regs, elf_fpregset_t *fpu)
{
#ifdef ELF_CORE_COPY_FPREGS
	return ELF_CORE_COPY_FPREGS(t, fpu);
#else
	return dump_fpu(regs, fpu);
#endif
}

#ifdef ELF_CORE_COPY_XFPREGS
static inline int elf_core_copy_task_xfpregs(struct task_struct *t, elf_fpxregset_t *xfpu)
{
	return ELF_CORE_COPY_XFPREGS(t, xfpu);
}
#endif

/*
 * These functions parameterize elf_core_dump in fs/binfmt_elf.c to write out
 * extra segments containing the gate DSO contents.  Dumping its
 * contents makes post-mortem fully interpretable later without matching up
 * the same kernel and hardware config to see what PC values meant.
 * Dumping its extra ELF program headers includes all the other information
 * a debugger needs to easily find how the gate DSO was being used.
 */
extern Elf_Half elf_core_extra_phdrs(void);
extern int
elf_core_write_extra_phdrs(struct file *file, loff_t offset, size_t *size,
			   unsigned long limit);
extern int
elf_core_write_extra_data(struct file *file, size_t *size, unsigned long limit);
extern size_t elf_core_extra_data_size(void);

#endif /* __KERNEL__ */

#endif /* _LINUX_ELFCORE_H */
