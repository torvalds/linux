#ifndef _LINUX_BINFMTS_H
#define _LINUX_BINFMTS_H

#include <linux/capability.h>

struct pt_regs;

/*
 * These are the maximum length and maximum number of strings passed to the
 * execve() system call.  MAX_ARG_STRLEN is essentially random but serves to
 * prevent the kernel from being unduly impacted by misaddressed pointers.
 * MAX_ARG_STRINGS is chosen to fit in a signed 32-bit integer.
 */
#define MAX_ARG_STRLEN (PAGE_SIZE * 32)
#define MAX_ARG_STRINGS 0x7FFFFFFF

/* sizeof(linux_binprm->buf) */
#define BINPRM_BUF_SIZE 128

#ifdef __KERNEL__

#define CORENAME_MAX_SIZE 128

/*
 * This structure is used to hold the arguments that are used when loading binaries.
 */
struct linux_binprm{
	char buf[BINPRM_BUF_SIZE];
#ifdef CONFIG_MMU
	struct vm_area_struct *vma;
#else
# define MAX_ARG_PAGES	32
	struct page *page[MAX_ARG_PAGES];
#endif
	struct mm_struct *mm;
	unsigned long p; /* current top of mem */
	unsigned int sh_bang:1,
		     misc_bang:1;
	struct file * file;
	int e_uid, e_gid;
	kernel_cap_t cap_inheritable, cap_permitted;
	bool cap_effective;
	void *security;
	int argc, envc;
	char * filename;	/* Name of binary as seen by procps */
	char * interp;		/* Name of the binary really executed. Most
				   of the time same as filename, but could be
				   different for binfmt_{misc,script} */
	unsigned interp_flags;
	unsigned interp_data;
	unsigned long loader, exec;
};

#define BINPRM_FLAGS_ENFORCE_NONDUMP_BIT 0
#define BINPRM_FLAGS_ENFORCE_NONDUMP (1 << BINPRM_FLAGS_ENFORCE_NONDUMP_BIT)

/* fd of the binary should be passed to the interpreter */
#define BINPRM_FLAGS_EXECFD_BIT 1
#define BINPRM_FLAGS_EXECFD (1 << BINPRM_FLAGS_EXECFD_BIT)


/*
 * This structure defines the functions that are used to load the binary formats that
 * linux accepts.
 */
struct linux_binfmt {
	struct list_head lh;
	struct module *module;
	int (*load_binary)(struct linux_binprm *, struct  pt_regs * regs);
	int (*load_shlib)(struct file *);
	int (*core_dump)(long signr, struct pt_regs *regs, struct file *file, unsigned long limit);
	unsigned long min_coredump;	/* minimal dump size */
	int hasvdso;
};

extern int register_binfmt(struct linux_binfmt *);
extern void unregister_binfmt(struct linux_binfmt *);

extern int prepare_binprm(struct linux_binprm *);
extern int __must_check remove_arg_zero(struct linux_binprm *);
extern int search_binary_handler(struct linux_binprm *,struct pt_regs *);
extern int flush_old_exec(struct linux_binprm * bprm);

extern int suid_dumpable;
#define SUID_DUMP_DISABLE	0	/* No setuid dumping */
#define SUID_DUMP_USER		1	/* Dump as user of process */
#define SUID_DUMP_ROOT		2	/* Dump as root */

/* Stack area protections */
#define EXSTACK_DEFAULT   0	/* Whatever the arch defaults to */
#define EXSTACK_DISABLE_X 1	/* Disable executable stacks */
#define EXSTACK_ENABLE_X  2	/* Enable executable stacks */

extern int setup_arg_pages(struct linux_binprm * bprm,
			   unsigned long stack_top,
			   int executable_stack);
extern int bprm_mm_init(struct linux_binprm *bprm);
extern int copy_strings_kernel(int argc,char ** argv,struct linux_binprm *bprm);
extern void compute_creds(struct linux_binprm *binprm);
extern int do_coredump(long signr, int exit_code, struct pt_regs * regs);
extern int set_binfmt(struct linux_binfmt *new);
extern void free_bprm(struct linux_binprm *);

#endif /* __KERNEL__ */
#endif /* _LINUX_BINFMTS_H */
