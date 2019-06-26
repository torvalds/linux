/*
 *  linux/fs/binfmt_aout.c
 *
 *  Copyright (C) 1991, 1992, 1996  Linus Torvalds
 */

#include <linux/module.h>

#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/a.out.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/init.h>
#include <linux/coredump.h>
#include <linux/slab.h>
#include <linux/sched/task_stack.h>

#include <linux/uaccess.h>
#include <asm/cacheflush.h>

static int load_aout_binary(struct linux_binprm *);
static int load_aout_library(struct file*);

static struct linux_binfmt aout_format = {
	.module		= THIS_MODULE,
	.load_binary	= load_aout_binary,
	.load_shlib	= load_aout_library,
};

#define BAD_ADDR(x)	((unsigned long)(x) >= TASK_SIZE)

static int set_brk(unsigned long start, unsigned long end)
{
	start = PAGE_ALIGN(start);
	end = PAGE_ALIGN(end);
	if (end > start)
		return vm_brk(start, end - start);
	return 0;
}

/*
 * create_aout_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */
static unsigned long __user *create_aout_tables(char __user *p, struct linux_binprm * bprm)
{
	char __user * __user *argv;
	char __user * __user *envp;
	unsigned long __user *sp;
	int argc = bprm->argc;
	int envc = bprm->envc;

	sp = (void __user *)((-(unsigned long)sizeof(char *)) & (unsigned long) p);
#ifdef __alpha__
/* whee.. test-programs are so much fun. */
	put_user(0, --sp);
	put_user(0, --sp);
	if (bprm->loader) {
		put_user(0, --sp);
		put_user(1003, --sp);
		put_user(bprm->loader, --sp);
		put_user(1002, --sp);
	}
	put_user(bprm->exec, --sp);
	put_user(1001, --sp);
#endif
	sp -= envc+1;
	envp = (char __user * __user *) sp;
	sp -= argc+1;
	argv = (char __user * __user *) sp;
#ifndef __alpha__
	put_user((unsigned long) envp,--sp);
	put_user((unsigned long) argv,--sp);
#endif
	put_user(argc,--sp);
	current->mm->arg_start = (unsigned long) p;
	while (argc-->0) {
		char c;
		put_user(p,argv++);
		do {
			get_user(c,p++);
		} while (c);
	}
	put_user(NULL,argv);
	current->mm->arg_end = current->mm->env_start = (unsigned long) p;
	while (envc-->0) {
		char c;
		put_user(p,envp++);
		do {
			get_user(c,p++);
		} while (c);
	}
	put_user(NULL,envp);
	current->mm->env_end = (unsigned long) p;
	return sp;
}

/*
 * These are the functions used to load a.out style executables and shared
 * libraries.  There is no binary dependent code anywhere else.
 */

static int load_aout_binary(struct linux_binprm * bprm)
{
	struct pt_regs *regs = current_pt_regs();
	struct exec ex;
	unsigned long error;
	unsigned long fd_offset;
	unsigned long rlim;
	int retval;

	ex = *((struct exec *) bprm->buf);		/* exec-header */
	if ((N_MAGIC(ex) != ZMAGIC && N_MAGIC(ex) != OMAGIC &&
	     N_MAGIC(ex) != QMAGIC && N_MAGIC(ex) != NMAGIC) ||
	    N_TRSIZE(ex) || N_DRSIZE(ex) ||
	    i_size_read(file_inode(bprm->file)) < ex.a_text+ex.a_data+N_SYMSIZE(ex)+N_TXTOFF(ex)) {
		return -ENOEXEC;
	}

	/*
	 * Requires a mmap handler. This prevents people from using a.out
	 * as part of an exploit attack against /proc-related vulnerabilities.
	 */
	if (!bprm->file->f_op->mmap)
		return -ENOEXEC;

	fd_offset = N_TXTOFF(ex);

	/* Check initial limits. This avoids letting people circumvent
	 * size limits imposed on them by creating programs with large
	 * arrays in the data or bss.
	 */
	rlim = rlimit(RLIMIT_DATA);
	if (rlim >= RLIM_INFINITY)
		rlim = ~0;
	if (ex.a_data + ex.a_bss > rlim)
		return -ENOMEM;

	/* Flush all traces of the currently running executable */
	retval = flush_old_exec(bprm);
	if (retval)
		return retval;

	/* OK, This is the point of no return */
#ifdef __alpha__
	SET_AOUT_PERSONALITY(bprm, ex);
#else
	set_personality(PER_LINUX);
#endif
	setup_new_exec(bprm);

	current->mm->end_code = ex.a_text +
		(current->mm->start_code = N_TXTADDR(ex));
	current->mm->end_data = ex.a_data +
		(current->mm->start_data = N_DATADDR(ex));
	current->mm->brk = ex.a_bss +
		(current->mm->start_brk = N_BSSADDR(ex));

	retval = setup_arg_pages(bprm, STACK_TOP, EXSTACK_DEFAULT);
	if (retval < 0)
		return retval;

	install_exec_creds(bprm);

	if (N_MAGIC(ex) == OMAGIC) {
		unsigned long text_addr, map_size;
		loff_t pos;

		text_addr = N_TXTADDR(ex);

#ifdef __alpha__
		pos = fd_offset;
		map_size = ex.a_text+ex.a_data + PAGE_SIZE - 1;
#else
		pos = 32;
		map_size = ex.a_text+ex.a_data;
#endif
		error = vm_brk(text_addr & PAGE_MASK, map_size);
		if (error)
			return error;

		error = read_code(bprm->file, text_addr, pos,
				  ex.a_text+ex.a_data);
		if ((signed long)error < 0)
			return error;
	} else {
		if ((ex.a_text & 0xfff || ex.a_data & 0xfff) &&
		    (N_MAGIC(ex) != NMAGIC) && printk_ratelimit())
		{
			printk(KERN_NOTICE "executable not page aligned\n");
		}

		if ((fd_offset & ~PAGE_MASK) != 0 && printk_ratelimit())
		{
			printk(KERN_WARNING 
			       "fd_offset is not page aligned. Please convert program: %pD\n",
			       bprm->file);
		}

		if (!bprm->file->f_op->mmap||((fd_offset & ~PAGE_MASK) != 0)) {
			error = vm_brk(N_TXTADDR(ex), ex.a_text+ex.a_data);
			if (error)
				return error;

			read_code(bprm->file, N_TXTADDR(ex), fd_offset,
				  ex.a_text + ex.a_data);
			goto beyond_if;
		}

		error = vm_mmap(bprm->file, N_TXTADDR(ex), ex.a_text,
			PROT_READ | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_EXECUTABLE,
			fd_offset);

		if (error != N_TXTADDR(ex))
			return error;

		error = vm_mmap(bprm->file, N_DATADDR(ex), ex.a_data,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_EXECUTABLE,
				fd_offset + ex.a_text);
		if (error != N_DATADDR(ex))
			return error;
	}
beyond_if:
	set_binfmt(&aout_format);

	retval = set_brk(current->mm->start_brk, current->mm->brk);
	if (retval < 0)
		return retval;

	current->mm->start_stack =
		(unsigned long) create_aout_tables((char __user *) bprm->p, bprm);
#ifdef __alpha__
	regs->gp = ex.a_gpvalue;
#endif
	finalize_exec(bprm);
	start_thread(regs, ex.a_entry, current->mm->start_stack);
	return 0;
}

static int load_aout_library(struct file *file)
{
	struct inode * inode;
	unsigned long bss, start_addr, len;
	unsigned long error;
	int retval;
	struct exec ex;
	loff_t pos = 0;

	inode = file_inode(file);

	retval = -ENOEXEC;
	error = kernel_read(file, &ex, sizeof(ex), &pos);
	if (error != sizeof(ex))
		goto out;

	/* We come in here for the regular a.out style of shared libraries */
	if ((N_MAGIC(ex) != ZMAGIC && N_MAGIC(ex) != QMAGIC) || N_TRSIZE(ex) ||
	    N_DRSIZE(ex) || ((ex.a_entry & 0xfff) && N_MAGIC(ex) == ZMAGIC) ||
	    i_size_read(inode) < ex.a_text+ex.a_data+N_SYMSIZE(ex)+N_TXTOFF(ex)) {
		goto out;
	}

	/*
	 * Requires a mmap handler. This prevents people from using a.out
	 * as part of an exploit attack against /proc-related vulnerabilities.
	 */
	if (!file->f_op->mmap)
		goto out;

	if (N_FLAGS(ex))
		goto out;

	/* For  QMAGIC, the starting address is 0x20 into the page.  We mask
	   this off to get the starting address for the page */

	start_addr =  ex.a_entry & 0xfffff000;

	if ((N_TXTOFF(ex) & ~PAGE_MASK) != 0) {
		if (printk_ratelimit())
		{
			printk(KERN_WARNING 
			       "N_TXTOFF is not page aligned. Please convert library: %pD\n",
			       file);
		}
		retval = vm_brk(start_addr, ex.a_text + ex.a_data + ex.a_bss);
		if (retval)
			goto out;

		read_code(file, start_addr, N_TXTOFF(ex),
			  ex.a_text + ex.a_data);
		retval = 0;
		goto out;
	}
	/* Now use mmap to map the library into memory. */
	error = vm_mmap(file, start_addr, ex.a_text + ex.a_data,
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE,
			N_TXTOFF(ex));
	retval = error;
	if (error != start_addr)
		goto out;

	len = PAGE_ALIGN(ex.a_text + ex.a_data);
	bss = ex.a_text + ex.a_data + ex.a_bss;
	if (bss > len) {
		retval = vm_brk(start_addr + len, bss - len);
		if (retval)
			goto out;
	}
	retval = 0;
out:
	return retval;
}

static int __init init_aout_binfmt(void)
{
	register_binfmt(&aout_format);
	return 0;
}

static void __exit exit_aout_binfmt(void)
{
	unregister_binfmt(&aout_format);
}

core_initcall(init_aout_binfmt);
module_exit(exit_aout_binfmt);
MODULE_LICENSE("GPL");
