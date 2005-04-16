/* binfmt_elf_fdpic.c: FDPIC ELF binary format
 *
 * Copyright (C) 2003, 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * Derived from binfmt_elf.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>

#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/binfmts.h>
#include <linux/string.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/personality.h>
#include <linux/ptrace.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/elf.h>
#include <linux/elf-fdpic.h>
#include <linux/elfcore.h>

#include <asm/uaccess.h>
#include <asm/param.h>
#include <asm/pgalloc.h>

typedef char *elf_caddr_t;
#ifndef elf_addr_t
#define elf_addr_t unsigned long
#endif

#if 0
#define kdebug(fmt, ...) printk("FDPIC "fmt"\n" ,##__VA_ARGS__ )
#else
#define kdebug(fmt, ...) do {} while(0)
#endif

MODULE_LICENSE("GPL");

static int load_elf_fdpic_binary(struct linux_binprm *bprm, struct pt_regs *regs);
//static int load_elf_fdpic_library(struct file *);
static int elf_fdpic_fetch_phdrs(struct elf_fdpic_params *params, struct file *file);
static int elf_fdpic_map_file(struct elf_fdpic_params *params,
			      struct file *file,
			      struct mm_struct *mm,
			      const char *what);

static int create_elf_fdpic_tables(struct linux_binprm *bprm,
				   struct mm_struct *mm,
				   struct elf_fdpic_params *exec_params,
				   struct elf_fdpic_params *interp_params);

#ifndef CONFIG_MMU
static int elf_fdpic_transfer_args_to_stack(struct linux_binprm *bprm, unsigned long *_sp);
static int elf_fdpic_map_file_constdisp_on_uclinux(struct elf_fdpic_params *params,
						   struct file *file,
						   struct mm_struct *mm);
#endif

static int elf_fdpic_map_file_by_direct_mmap(struct elf_fdpic_params *params,
					     struct file *file,
					     struct mm_struct *mm);

static struct linux_binfmt elf_fdpic_format = {
	.module		= THIS_MODULE,
	.load_binary	= load_elf_fdpic_binary,
//	.load_shlib	= load_elf_fdpic_library,
//	.core_dump	= elf_fdpic_core_dump,
	.min_coredump	= ELF_EXEC_PAGESIZE,
};

static int __init init_elf_fdpic_binfmt(void)  { return register_binfmt(&elf_fdpic_format); }
static void __exit exit_elf_fdpic_binfmt(void) { unregister_binfmt(&elf_fdpic_format); }

module_init(init_elf_fdpic_binfmt)
module_exit(exit_elf_fdpic_binfmt)

static int is_elf_fdpic(struct elfhdr *hdr, struct file *file)
{
	if (memcmp(hdr->e_ident, ELFMAG, SELFMAG) != 0)
		return 0;
	if (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN)
		return 0;
	if (!elf_check_arch(hdr) || !elf_check_fdpic(hdr))
		return 0;
	if (!file->f_op || !file->f_op->mmap)
		return 0;
	return 1;
}

/*****************************************************************************/
/*
 * read the program headers table into memory
 */
static int elf_fdpic_fetch_phdrs(struct elf_fdpic_params *params, struct file *file)
{
	struct elf32_phdr *phdr;
	unsigned long size;
	int retval, loop;

	if (params->hdr.e_phentsize != sizeof(struct elf_phdr))
		return -ENOMEM;
	if (params->hdr.e_phnum > 65536U / sizeof(struct elf_phdr))
		return -ENOMEM;

	size = params->hdr.e_phnum * sizeof(struct elf_phdr);
	params->phdrs = kmalloc(size, GFP_KERNEL);
	if (!params->phdrs)
		return -ENOMEM;

	retval = kernel_read(file, params->hdr.e_phoff, (char *) params->phdrs, size);
	if (retval < 0)
		return retval;

	/* determine stack size for this binary */
	phdr = params->phdrs;
	for (loop = 0; loop < params->hdr.e_phnum; loop++, phdr++) {
		if (phdr->p_type != PT_GNU_STACK)
			continue;

		if (phdr->p_flags & PF_X)
			params->flags |= ELF_FDPIC_FLAG_EXEC_STACK;
		else
			params->flags |= ELF_FDPIC_FLAG_NOEXEC_STACK;

		params->stack_size = phdr->p_memsz;
		break;
	}

	return 0;
} /* end elf_fdpic_fetch_phdrs() */

/*****************************************************************************/
/*
 * load an fdpic binary into various bits of memory
 */
static int load_elf_fdpic_binary(struct linux_binprm *bprm, struct pt_regs *regs)
{
	struct elf_fdpic_params exec_params, interp_params;
	struct elf_phdr *phdr;
	unsigned long stack_size;
	struct file *interpreter = NULL; /* to shut gcc up */
	char *interpreter_name = NULL;
	int executable_stack;
	int retval, i;

	memset(&exec_params, 0, sizeof(exec_params));
	memset(&interp_params, 0, sizeof(interp_params));

	exec_params.hdr = *(struct elfhdr *) bprm->buf;
	exec_params.flags = ELF_FDPIC_FLAG_PRESENT | ELF_FDPIC_FLAG_EXECUTABLE;

	/* check that this is a binary we know how to deal with */
	retval = -ENOEXEC;
	if (!is_elf_fdpic(&exec_params.hdr, bprm->file))
		goto error;

	/* read the program header table */
	retval = elf_fdpic_fetch_phdrs(&exec_params, bprm->file);
	if (retval < 0)
		goto error;

	/* scan for a program header that specifies an interpreter */
	phdr = exec_params.phdrs;

	for (i = 0; i < exec_params.hdr.e_phnum; i++, phdr++) {
		switch (phdr->p_type) {
		case PT_INTERP:
			retval = -ENOMEM;
			if (phdr->p_filesz > PATH_MAX)
				goto error;
			retval = -ENOENT;
			if (phdr->p_filesz < 2)
				goto error;

			/* read the name of the interpreter into memory */
			interpreter_name = (char *) kmalloc(phdr->p_filesz, GFP_KERNEL);
			if (!interpreter_name)
				goto error;

			retval = kernel_read(bprm->file,
					     phdr->p_offset,
					     interpreter_name,
					     phdr->p_filesz);
			if (retval < 0)
				goto error;

			retval = -ENOENT;
			if (interpreter_name[phdr->p_filesz - 1] != '\0')
				goto error;

			kdebug("Using ELF interpreter %s", interpreter_name);

			/* replace the program with the interpreter */
			interpreter = open_exec(interpreter_name);
			retval = PTR_ERR(interpreter);
			if (IS_ERR(interpreter)) {
				interpreter = NULL;
				goto error;
			}

			retval = kernel_read(interpreter, 0, bprm->buf, BINPRM_BUF_SIZE);
			if (retval < 0)
				goto error;

			interp_params.hdr = *((struct elfhdr *) bprm->buf);
			break;

		case PT_LOAD:
#ifdef CONFIG_MMU
			if (exec_params.load_addr == 0)
				exec_params.load_addr = phdr->p_vaddr;
#endif
			break;
		}

	}

	if (elf_check_const_displacement(&exec_params.hdr))
		exec_params.flags |= ELF_FDPIC_FLAG_CONSTDISP;

	/* perform insanity checks on the interpreter */
	if (interpreter_name) {
		retval = -ELIBBAD;
		if (!is_elf_fdpic(&interp_params.hdr, interpreter))
			goto error;

		interp_params.flags = ELF_FDPIC_FLAG_PRESENT;

		/* read the interpreter's program header table */
		retval = elf_fdpic_fetch_phdrs(&interp_params, interpreter);
		if (retval < 0)
			goto error;
	}

	stack_size = exec_params.stack_size;
	if (stack_size < interp_params.stack_size)
		stack_size = interp_params.stack_size;

	if (exec_params.flags & ELF_FDPIC_FLAG_EXEC_STACK)
		executable_stack = EXSTACK_ENABLE_X;
	else if (exec_params.flags & ELF_FDPIC_FLAG_NOEXEC_STACK)
		executable_stack = EXSTACK_DISABLE_X;
	else if (interp_params.flags & ELF_FDPIC_FLAG_EXEC_STACK)
		executable_stack = EXSTACK_ENABLE_X;
	else if (interp_params.flags & ELF_FDPIC_FLAG_NOEXEC_STACK)
		executable_stack = EXSTACK_DISABLE_X;
	else
		executable_stack = EXSTACK_DEFAULT;

	retval = -ENOEXEC;
	if (stack_size == 0)
		goto error;

	if (elf_check_const_displacement(&interp_params.hdr))
		interp_params.flags |= ELF_FDPIC_FLAG_CONSTDISP;

	/* flush all traces of the currently running executable */
	retval = flush_old_exec(bprm);
	if (retval)
		goto error;

	/* there's now no turning back... the old userspace image is dead,
	 * defunct, deceased, etc. after this point we have to exit via
	 * error_kill */
	set_personality(PER_LINUX_FDPIC);
	set_binfmt(&elf_fdpic_format);

	current->mm->start_code = 0;
	current->mm->end_code = 0;
	current->mm->start_stack = 0;
	current->mm->start_data = 0;
	current->mm->end_data = 0;
	current->mm->context.exec_fdpic_loadmap = 0;
	current->mm->context.interp_fdpic_loadmap = 0;

	current->flags &= ~PF_FORKNOEXEC;

#ifdef CONFIG_MMU
	elf_fdpic_arch_lay_out_mm(&exec_params,
				  &interp_params,
				  &current->mm->start_stack,
				  &current->mm->start_brk);
#endif

	/* do this so that we can load the interpreter, if need be
	 * - we will change some of these later
	 */
	set_mm_counter(current->mm, rss, 0);

#ifdef CONFIG_MMU
	retval = setup_arg_pages(bprm, current->mm->start_stack, executable_stack);
	if (retval < 0) {
		send_sig(SIGKILL, current, 0);
		goto error_kill;
	}
#endif

	/* load the executable and interpreter into memory */
	retval = elf_fdpic_map_file(&exec_params, bprm->file, current->mm, "executable");
	if (retval < 0)
		goto error_kill;

	if (interpreter_name) {
		retval = elf_fdpic_map_file(&interp_params, interpreter,
					    current->mm, "interpreter");
		if (retval < 0) {
			printk(KERN_ERR "Unable to load interpreter\n");
			goto error_kill;
		}

		allow_write_access(interpreter);
		fput(interpreter);
		interpreter = NULL;
	}

#ifdef CONFIG_MMU
	if (!current->mm->start_brk)
		current->mm->start_brk = current->mm->end_data;

	current->mm->brk = current->mm->start_brk = PAGE_ALIGN(current->mm->start_brk);

#else
	/* create a stack and brk area big enough for everyone
	 * - the brk heap starts at the bottom and works up
	 * - the stack starts at the top and works down
	 */
	stack_size = (stack_size + PAGE_SIZE - 1) & PAGE_MASK;
	if (stack_size < PAGE_SIZE * 2)
		stack_size = PAGE_SIZE * 2;

	down_write(&current->mm->mmap_sem);
	current->mm->start_brk = do_mmap(NULL,
					 0,
					 stack_size,
					 PROT_READ | PROT_WRITE | PROT_EXEC,
					 MAP_PRIVATE | MAP_ANON | MAP_GROWSDOWN,
					 0);

	if (IS_ERR((void *) current->mm->start_brk)) {
		up_write(&current->mm->mmap_sem);
		retval = current->mm->start_brk;
		current->mm->start_brk = 0;
		goto error_kill;
	}

	if (do_mremap(current->mm->start_brk,
		      stack_size,
		      ksize((char *) current->mm->start_brk),
		      0, 0
		      ) == current->mm->start_brk
	    )
		stack_size = ksize((char *) current->mm->start_brk);
	up_write(&current->mm->mmap_sem);

	current->mm->brk = current->mm->start_brk;
	current->mm->context.end_brk = current->mm->start_brk;
	current->mm->context.end_brk += (stack_size > PAGE_SIZE) ? (stack_size - PAGE_SIZE) : 0;
	current->mm->start_stack = current->mm->start_brk + stack_size;
#endif

	compute_creds(bprm);
	current->flags &= ~PF_FORKNOEXEC;
	if (create_elf_fdpic_tables(bprm, current->mm, &exec_params, &interp_params) < 0)
		goto error_kill;

	kdebug("- start_code  %lx",	(long) current->mm->start_code);
	kdebug("- end_code    %lx",	(long) current->mm->end_code);
	kdebug("- start_data  %lx",	(long) current->mm->start_data);
	kdebug("- end_data    %lx",	(long) current->mm->end_data);
	kdebug("- start_brk   %lx",	(long) current->mm->start_brk);
	kdebug("- brk         %lx",	(long) current->mm->brk);
	kdebug("- start_stack %lx",	(long) current->mm->start_stack);

#ifdef ELF_FDPIC_PLAT_INIT
	/*
	 * The ABI may specify that certain registers be set up in special
	 * ways (on i386 %edx is the address of a DT_FINI function, for
	 * example.  This macro performs whatever initialization to
	 * the regs structure is required.
	 */
	ELF_FDPIC_PLAT_INIT(regs,
			    exec_params.map_addr,
			    interp_params.map_addr,
			    interp_params.dynamic_addr ?: exec_params.dynamic_addr
			    );
#endif

	/* everything is now ready... get the userspace context ready to roll */
	start_thread(regs,
		     interp_params.entry_addr ?: exec_params.entry_addr,
		     current->mm->start_stack);

	if (unlikely(current->ptrace & PT_PTRACED)) {
		if (current->ptrace & PT_TRACE_EXEC)
			ptrace_notify ((PTRACE_EVENT_EXEC << 8) | SIGTRAP);
		else
			send_sig(SIGTRAP, current, 0);
	}

	retval = 0;

error:
	if (interpreter) {
		allow_write_access(interpreter);
		fput(interpreter);
	}
	if (interpreter_name)
		kfree(interpreter_name);
	if (exec_params.phdrs)
		kfree(exec_params.phdrs);
	if (exec_params.loadmap)
		kfree(exec_params.loadmap);
	if (interp_params.phdrs)
		kfree(interp_params.phdrs);
	if (interp_params.loadmap)
		kfree(interp_params.loadmap);
	return retval;

	/* unrecoverable error - kill the process */
 error_kill:
	send_sig(SIGSEGV, current, 0);
	goto error;

} /* end load_elf_fdpic_binary() */

/*****************************************************************************/
/*
 * present useful information to the program
 */
static int create_elf_fdpic_tables(struct linux_binprm *bprm,
				   struct mm_struct *mm,
				   struct elf_fdpic_params *exec_params,
				   struct elf_fdpic_params *interp_params)
{
	unsigned long sp, csp, nitems;
	elf_caddr_t *argv, *envp;
	size_t platform_len = 0, len;
	char *k_platform, *u_platform, *p;
	long hwcap;
	int loop;

	/* we're going to shovel a whole load of stuff onto the stack */
#ifdef CONFIG_MMU
	sp = bprm->p;
#else
	sp = mm->start_stack;

	/* stack the program arguments and environment */
	if (elf_fdpic_transfer_args_to_stack(bprm, &sp) < 0)
		return -EFAULT;
#endif

	/* get hold of platform and hardware capabilities masks for the machine
	 * we are running on.  In some cases (Sparc), this info is impossible
	 * to get, in others (i386) it is merely difficult.
	 */
	hwcap = ELF_HWCAP;
	k_platform = ELF_PLATFORM;

	if (k_platform) {
		platform_len = strlen(k_platform) + 1;
		sp -= platform_len;
		if (__copy_to_user(u_platform, k_platform, platform_len) != 0)
			return -EFAULT;
	}

	u_platform = (char *) sp;

#if defined(__i386__) && defined(CONFIG_SMP)
	/* in some cases (e.g. Hyper-Threading), we want to avoid L1 evictions
	 * by the processes running on the same package. One thing we can do
	 * is to shuffle the initial stack for them.
	 *
	 * the conditionals here are unneeded, but kept in to make the
	 * code behaviour the same as pre change unless we have hyperthreaded
	 * processors. This keeps Mr Marcelo Person happier but should be
	 * removed for 2.5
	 */
	if (smp_num_siblings > 1)
		sp = sp - ((current->pid % 64) << 7);
#endif

	sp &= ~7UL;

	/* stack the load map(s) */
	len = sizeof(struct elf32_fdpic_loadmap);
	len += sizeof(struct elf32_fdpic_loadseg) * exec_params->loadmap->nsegs;
	sp = (sp - len) & ~7UL;
	exec_params->map_addr = sp;

	if (copy_to_user((void *) sp, exec_params->loadmap, len) != 0)
		return -EFAULT;

	current->mm->context.exec_fdpic_loadmap = (unsigned long) sp;

	if (interp_params->loadmap) {
		len = sizeof(struct elf32_fdpic_loadmap);
		len += sizeof(struct elf32_fdpic_loadseg) * interp_params->loadmap->nsegs;
		sp = (sp - len) & ~7UL;
		interp_params->map_addr = sp;

		if (copy_to_user((void *) sp, interp_params->loadmap, len) != 0)
			return -EFAULT;

		current->mm->context.interp_fdpic_loadmap = (unsigned long) sp;
	}

	/* force 16 byte _final_ alignment here for generality */
#define DLINFO_ITEMS 13

	nitems = 1 + DLINFO_ITEMS + (k_platform ? 1 : 0);
#ifdef DLINFO_ARCH_ITEMS
	nitems += DLINFO_ARCH_ITEMS;
#endif

	csp = sp;
	sp -= nitems * 2 * sizeof(unsigned long);
	sp -= (bprm->envc + 1) * sizeof(char *);	/* envv[] */
	sp -= (bprm->argc + 1) * sizeof(char *);	/* argv[] */
	sp -= 1 * sizeof(unsigned long);		/* argc */

	csp -= sp & 15UL;
	sp -= sp & 15UL;

	/* put the ELF interpreter info on the stack */
#define NEW_AUX_ENT(nr, id, val)						\
	do {									\
		struct { unsigned long _id, _val; } *ent = (void *) csp;	\
		__put_user((id), &ent[nr]._id);					\
		__put_user((val), &ent[nr]._val);				\
	} while (0)

	csp -= 2 * sizeof(unsigned long);
	NEW_AUX_ENT(0, AT_NULL, 0);
	if (k_platform) {
		csp -= 2 * sizeof(unsigned long);
		NEW_AUX_ENT(0, AT_PLATFORM, (elf_addr_t)(unsigned long) u_platform);
	}

	csp -= DLINFO_ITEMS * 2 * sizeof(unsigned long);
	NEW_AUX_ENT( 0, AT_HWCAP,		hwcap);
	NEW_AUX_ENT( 1, AT_PAGESZ,		PAGE_SIZE);
	NEW_AUX_ENT( 2, AT_CLKTCK,		CLOCKS_PER_SEC);
	NEW_AUX_ENT( 3, AT_PHDR,		exec_params->ph_addr);
	NEW_AUX_ENT( 4, AT_PHENT,		sizeof(struct elf_phdr));
	NEW_AUX_ENT( 5, AT_PHNUM,		exec_params->hdr.e_phnum);
	NEW_AUX_ENT( 6,	AT_BASE,		interp_params->elfhdr_addr);
	NEW_AUX_ENT( 7, AT_FLAGS,		0);
	NEW_AUX_ENT( 8, AT_ENTRY,		exec_params->entry_addr);
	NEW_AUX_ENT( 9, AT_UID,			(elf_addr_t) current->uid);
	NEW_AUX_ENT(10, AT_EUID,		(elf_addr_t) current->euid);
	NEW_AUX_ENT(11, AT_GID,			(elf_addr_t) current->gid);
	NEW_AUX_ENT(12, AT_EGID,		(elf_addr_t) current->egid);

#ifdef ARCH_DLINFO
	/* ARCH_DLINFO must come last so platform specific code can enforce
	 * special alignment requirements on the AUXV if necessary (eg. PPC).
	 */
	ARCH_DLINFO;
#endif
#undef NEW_AUX_ENT

	/* allocate room for argv[] and envv[] */
	csp -= (bprm->envc + 1) * sizeof(elf_caddr_t);
	envp = (elf_caddr_t *) csp;
	csp -= (bprm->argc + 1) * sizeof(elf_caddr_t);
	argv = (elf_caddr_t *) csp;

	/* stack argc */
	csp -= sizeof(unsigned long);
	__put_user(bprm->argc, (unsigned long *) csp);

	if (csp != sp)
		BUG();

	/* fill in the argv[] array */
#ifdef CONFIG_MMU
	current->mm->arg_start = bprm->p;
#else
	current->mm->arg_start = current->mm->start_stack - (MAX_ARG_PAGES * PAGE_SIZE - bprm->p);
#endif

	p = (char *) current->mm->arg_start;
	for (loop = bprm->argc; loop > 0; loop--) {
		__put_user((elf_caddr_t) p, argv++);
		len = strnlen_user(p, PAGE_SIZE * MAX_ARG_PAGES);
		if (!len || len > PAGE_SIZE * MAX_ARG_PAGES)
			return -EINVAL;
		p += len;
	}
	__put_user(NULL, argv);
	current->mm->arg_end = (unsigned long) p;

	/* fill in the envv[] array */
	current->mm->env_start = (unsigned long) p;
	for (loop = bprm->envc; loop > 0; loop--) {
		__put_user((elf_caddr_t)(unsigned long) p, envp++);
		len = strnlen_user(p, PAGE_SIZE * MAX_ARG_PAGES);
		if (!len || len > PAGE_SIZE * MAX_ARG_PAGES)
			return -EINVAL;
		p += len;
	}
	__put_user(NULL, envp);
	current->mm->env_end = (unsigned long) p;

	mm->start_stack = (unsigned long) sp;
	return 0;
} /* end create_elf_fdpic_tables() */

/*****************************************************************************/
/*
 * transfer the program arguments and environment from the holding pages onto
 * the stack
 */
#ifndef CONFIG_MMU
static int elf_fdpic_transfer_args_to_stack(struct linux_binprm *bprm, unsigned long *_sp)
{
	unsigned long index, stop, sp;
	char *src;
	int ret = 0;

	stop = bprm->p >> PAGE_SHIFT;
	sp = *_sp;

	for (index = MAX_ARG_PAGES - 1; index >= stop; index--) {
		src = kmap(bprm->page[index]);
		sp -= PAGE_SIZE;
		if (copy_to_user((void *) sp, src, PAGE_SIZE) != 0)
			ret = -EFAULT;
		kunmap(bprm->page[index]);
		if (ret < 0)
			goto out;
	}

	*_sp = (*_sp - (MAX_ARG_PAGES * PAGE_SIZE - bprm->p)) & ~15;

 out:
	return ret;
} /* end elf_fdpic_transfer_args_to_stack() */
#endif

/*****************************************************************************/
/*
 * load the appropriate binary image (executable or interpreter) into memory
 * - we assume no MMU is available
 * - if no other PIC bits are set in params->hdr->e_flags
 *   - we assume that the LOADable segments in the binary are independently relocatable
 *   - we assume R/O executable segments are shareable
 * - else
 *   - we assume the loadable parts of the image to require fixed displacement
 *   - the image is not shareable
 */
static int elf_fdpic_map_file(struct elf_fdpic_params *params,
			      struct file *file,
			      struct mm_struct *mm,
			      const char *what)
{
	struct elf32_fdpic_loadmap *loadmap;
#ifdef CONFIG_MMU
	struct elf32_fdpic_loadseg *mseg;
#endif
	struct elf32_fdpic_loadseg *seg;
	struct elf32_phdr *phdr;
	unsigned long load_addr, stop;
	unsigned nloads, tmp;
	size_t size;
	int loop, ret;

	/* allocate a load map table */
	nloads = 0;
	for (loop = 0; loop < params->hdr.e_phnum; loop++)
		if (params->phdrs[loop].p_type == PT_LOAD)
			nloads++;

	if (nloads == 0)
		return -ELIBBAD;

	size = sizeof(*loadmap) + nloads * sizeof(*seg);
	loadmap = kmalloc(size, GFP_KERNEL);
	if (!loadmap)
		return -ENOMEM;

	params->loadmap = loadmap;
	memset(loadmap, 0, size);

	loadmap->version = ELF32_FDPIC_LOADMAP_VERSION;
	loadmap->nsegs = nloads;

	load_addr = params->load_addr;
	seg = loadmap->segs;

	/* map the requested LOADs into the memory space */
	switch (params->flags & ELF_FDPIC_FLAG_ARRANGEMENT) {
	case ELF_FDPIC_FLAG_CONSTDISP:
	case ELF_FDPIC_FLAG_CONTIGUOUS:
#ifndef CONFIG_MMU
		ret = elf_fdpic_map_file_constdisp_on_uclinux(params, file, mm);
		if (ret < 0)
			return ret;
		break;
#endif
	default:
		ret = elf_fdpic_map_file_by_direct_mmap(params, file, mm);
		if (ret < 0)
			return ret;
		break;
	}

	/* map the entry point */
	if (params->hdr.e_entry) {
		seg = loadmap->segs;
		for (loop = loadmap->nsegs; loop > 0; loop--, seg++) {
			if (params->hdr.e_entry >= seg->p_vaddr &&
			    params->hdr.e_entry < seg->p_vaddr + seg->p_memsz
			    ) {
				params->entry_addr =
					(params->hdr.e_entry - seg->p_vaddr) + seg->addr;
				break;
			}
		}
	}

	/* determine where the program header table has wound up if mapped */
	stop = params->hdr.e_phoff + params->hdr.e_phnum * sizeof (struct elf_phdr);
	phdr = params->phdrs;

	for (loop = 0; loop < params->hdr.e_phnum; loop++, phdr++) {
		if (phdr->p_type != PT_LOAD)
			continue;

		if (phdr->p_offset > params->hdr.e_phoff ||
		    phdr->p_offset + phdr->p_filesz < stop)
			continue;

		seg = loadmap->segs;
		for (loop = loadmap->nsegs; loop > 0; loop--, seg++) {
			if (phdr->p_vaddr >= seg->p_vaddr &&
			    phdr->p_vaddr + phdr->p_filesz <= seg->p_vaddr + seg->p_memsz
			    ) {
				params->ph_addr = (phdr->p_vaddr - seg->p_vaddr) + seg->addr +
					params->hdr.e_phoff - phdr->p_offset;
				break;
			}
		}
		break;
	}

	/* determine where the dynamic section has wound up if there is one */
	phdr = params->phdrs;
	for (loop = 0; loop < params->hdr.e_phnum; loop++, phdr++) {
		if (phdr->p_type != PT_DYNAMIC)
			continue;

		seg = loadmap->segs;
		for (loop = loadmap->nsegs; loop > 0; loop--, seg++) {
			if (phdr->p_vaddr >= seg->p_vaddr &&
			    phdr->p_vaddr + phdr->p_memsz <= seg->p_vaddr + seg->p_memsz
			    ) {
				params->dynamic_addr = (phdr->p_vaddr - seg->p_vaddr) + seg->addr;

				/* check the dynamic section contains at least one item, and that
				 * the last item is a NULL entry */
				if (phdr->p_memsz == 0 ||
				    phdr->p_memsz % sizeof(Elf32_Dyn) != 0)
					goto dynamic_error;

				tmp = phdr->p_memsz / sizeof(Elf32_Dyn);
				if (((Elf32_Dyn *) params->dynamic_addr)[tmp - 1].d_tag != 0)
					goto dynamic_error;
				break;
			}
		}
		break;
	}

	/* now elide adjacent segments in the load map on MMU linux
	 * - on uClinux the holes between may actually be filled with system stuff or stuff from
	 *   other processes
	 */
#ifdef CONFIG_MMU
	nloads = loadmap->nsegs;
	mseg = loadmap->segs;
	seg = mseg + 1;
	for (loop = 1; loop < nloads; loop++) {
		/* see if we have a candidate for merging */
		if (seg->p_vaddr - mseg->p_vaddr == seg->addr - mseg->addr) {
			load_addr = PAGE_ALIGN(mseg->addr + mseg->p_memsz);
			if (load_addr == (seg->addr & PAGE_MASK)) {
				mseg->p_memsz += load_addr - (mseg->addr + mseg->p_memsz);
				mseg->p_memsz += seg->addr & ~PAGE_MASK;
				mseg->p_memsz += seg->p_memsz;
				loadmap->nsegs--;
				continue;
			}
		}

		mseg++;
		if (mseg != seg)
			*mseg = *seg;
	}
#endif

	kdebug("Mapped Object [%s]:", what);
	kdebug("- elfhdr   : %lx", params->elfhdr_addr);
	kdebug("- entry    : %lx", params->entry_addr);
	kdebug("- PHDR[]   : %lx", params->ph_addr);
	kdebug("- DYNAMIC[]: %lx", params->dynamic_addr);
	seg = loadmap->segs;
	for (loop = 0; loop < loadmap->nsegs; loop++, seg++)
		kdebug("- LOAD[%d] : %08x-%08x [va=%x ms=%x]",
		       loop,
		       seg->addr, seg->addr + seg->p_memsz - 1,
		       seg->p_vaddr, seg->p_memsz);

	return 0;

 dynamic_error:
	printk("ELF FDPIC %s with invalid DYNAMIC section (inode=%lu)\n",
	       what, file->f_dentry->d_inode->i_ino);
	return -ELIBBAD;
} /* end elf_fdpic_map_file() */

/*****************************************************************************/
/*
 * map a file with constant displacement under uClinux
 */
#ifndef CONFIG_MMU
static int elf_fdpic_map_file_constdisp_on_uclinux(struct elf_fdpic_params *params,
						   struct file *file,
						   struct mm_struct *mm)
{
	struct elf32_fdpic_loadseg *seg;
	struct elf32_phdr *phdr;
	unsigned long load_addr, base = ULONG_MAX, top = 0, maddr = 0, mflags;
	loff_t fpos;
	int loop, ret;

	load_addr = params->load_addr;
	seg = params->loadmap->segs;

	/* determine the bounds of the contiguous overall allocation we must make */
	phdr = params->phdrs;
	for (loop = 0; loop < params->hdr.e_phnum; loop++, phdr++) {
		if (params->phdrs[loop].p_type != PT_LOAD)
			continue;

		if (base > phdr->p_vaddr)
			base = phdr->p_vaddr;
		if (top < phdr->p_vaddr + phdr->p_memsz)
			top = phdr->p_vaddr + phdr->p_memsz;
	}

	/* allocate one big anon block for everything */
	mflags = MAP_PRIVATE;
	if (params->flags & ELF_FDPIC_FLAG_EXECUTABLE)
		mflags |= MAP_EXECUTABLE;

	down_write(&mm->mmap_sem);
	maddr = do_mmap(NULL, load_addr, top - base,
			PROT_READ | PROT_WRITE | PROT_EXEC, mflags, 0);
	up_write(&mm->mmap_sem);
	if (IS_ERR((void *) maddr))
		return (int) maddr;

	if (load_addr != 0)
		load_addr += PAGE_ALIGN(top - base);

	/* and then load the file segments into it */
	phdr = params->phdrs;
	for (loop = 0; loop < params->hdr.e_phnum; loop++, phdr++) {
		if (params->phdrs[loop].p_type != PT_LOAD)
			continue;

		fpos = phdr->p_offset;

		seg->addr = maddr + (phdr->p_vaddr - base);
		seg->p_vaddr = phdr->p_vaddr;
		seg->p_memsz = phdr->p_memsz;

		ret = file->f_op->read(file, (void *) seg->addr, phdr->p_filesz, &fpos);
		if (ret < 0)
			return ret;

		/* map the ELF header address if in this segment */
		if (phdr->p_offset == 0)
			params->elfhdr_addr = seg->addr;

		/* clear any space allocated but not loaded */
		if (phdr->p_filesz < phdr->p_memsz)
			clear_user((void *) (seg->addr + phdr->p_filesz),
				   phdr->p_memsz - phdr->p_filesz);

		if (mm) {
			if (phdr->p_flags & PF_X) {
				mm->start_code = seg->addr;
				mm->end_code = seg->addr + phdr->p_memsz;
			}
			else if (!mm->start_data) {
				mm->start_data = seg->addr;
#ifndef CONFIG_MMU
				mm->end_data = seg->addr + phdr->p_memsz;
#endif
			}

#ifdef CONFIG_MMU
			if (seg->addr + phdr->p_memsz > mm->end_data)
				mm->end_data = seg->addr + phdr->p_memsz;
#endif
		}

		seg++;
	}

	return 0;
} /* end elf_fdpic_map_file_constdisp_on_uclinux() */
#endif

/*****************************************************************************/
/*
 * map a binary by direct mmap() of the individual PT_LOAD segments
 */
static int elf_fdpic_map_file_by_direct_mmap(struct elf_fdpic_params *params,
					     struct file *file,
					     struct mm_struct *mm)
{
	struct elf32_fdpic_loadseg *seg;
	struct elf32_phdr *phdr;
	unsigned long load_addr, delta_vaddr;
	int loop, dvset;

	load_addr = params->load_addr;
	delta_vaddr = 0;
	dvset = 0;

	seg = params->loadmap->segs;

	/* deal with each load segment separately */
	phdr = params->phdrs;
	for (loop = 0; loop < params->hdr.e_phnum; loop++, phdr++) {
		unsigned long maddr, disp, excess, excess1;
		int prot = 0, flags;

		if (phdr->p_type != PT_LOAD)
			continue;

		kdebug("[LOAD] va=%lx of=%lx fs=%lx ms=%lx",
		       (unsigned long) phdr->p_vaddr,
		       (unsigned long) phdr->p_offset,
		       (unsigned long) phdr->p_filesz,
		       (unsigned long) phdr->p_memsz);

		/* determine the mapping parameters */
		if (phdr->p_flags & PF_R) prot |= PROT_READ;
		if (phdr->p_flags & PF_W) prot |= PROT_WRITE;
		if (phdr->p_flags & PF_X) prot |= PROT_EXEC;

		flags = MAP_PRIVATE | MAP_DENYWRITE;
		if (params->flags & ELF_FDPIC_FLAG_EXECUTABLE)
			flags |= MAP_EXECUTABLE;

		maddr = 0;

		switch (params->flags & ELF_FDPIC_FLAG_ARRANGEMENT) {
		case ELF_FDPIC_FLAG_INDEPENDENT:
			/* PT_LOADs are independently locatable */
			break;

		case ELF_FDPIC_FLAG_HONOURVADDR:
			/* the specified virtual address must be honoured */
			maddr = phdr->p_vaddr;
			flags |= MAP_FIXED;
			break;

		case ELF_FDPIC_FLAG_CONSTDISP:
			/* constant displacement
			 * - can be mapped anywhere, but must be mapped as a unit
			 */
			if (!dvset) {
				maddr = load_addr;
				delta_vaddr = phdr->p_vaddr;
				dvset = 1;
			}
			else {
				maddr = load_addr + phdr->p_vaddr - delta_vaddr;
				flags |= MAP_FIXED;
			}
			break;

		case ELF_FDPIC_FLAG_CONTIGUOUS:
			/* contiguity handled later */
			break;

		default:
			BUG();
		}

		maddr &= PAGE_MASK;

		/* create the mapping */
		disp = phdr->p_vaddr & ~PAGE_MASK;
		down_write(&mm->mmap_sem);
		maddr = do_mmap(file, maddr, phdr->p_memsz + disp, prot, flags,
				phdr->p_offset - disp);
		up_write(&mm->mmap_sem);

		kdebug("mmap[%d] <file> sz=%lx pr=%x fl=%x of=%lx --> %08lx",
		       loop, phdr->p_memsz + disp, prot, flags, phdr->p_offset - disp,
		       maddr);

		if (IS_ERR((void *) maddr))
			return (int) maddr;

		if ((params->flags & ELF_FDPIC_FLAG_ARRANGEMENT) == ELF_FDPIC_FLAG_CONTIGUOUS)
			load_addr += PAGE_ALIGN(phdr->p_memsz + disp);

		seg->addr = maddr + disp;
		seg->p_vaddr = phdr->p_vaddr;
		seg->p_memsz = phdr->p_memsz;

		/* map the ELF header address if in this segment */
		if (phdr->p_offset == 0)
			params->elfhdr_addr = seg->addr;

		/* clear the bit between beginning of mapping and beginning of PT_LOAD */
		if (prot & PROT_WRITE && disp > 0) {
			kdebug("clear[%d] ad=%lx sz=%lx", loop, maddr, disp);
			clear_user((void *) maddr, disp);
			maddr += disp;
		}

		/* clear any space allocated but not loaded
		 * - on uClinux we can just clear the lot
		 * - on MMU linux we'll get a SIGBUS beyond the last page
		 *   extant in the file
		 */
		excess = phdr->p_memsz - phdr->p_filesz;
		excess1 = PAGE_SIZE - ((maddr + phdr->p_filesz) & ~PAGE_MASK);

#ifdef CONFIG_MMU

		if (excess > excess1) {
			unsigned long xaddr = maddr + phdr->p_filesz + excess1;
			unsigned long xmaddr;

			flags |= MAP_FIXED | MAP_ANONYMOUS;
			down_write(&mm->mmap_sem);
			xmaddr = do_mmap(NULL, xaddr, excess - excess1, prot, flags, 0);
			up_write(&mm->mmap_sem);

			kdebug("mmap[%d] <anon>"
			       " ad=%lx sz=%lx pr=%x fl=%x of=0 --> %08lx",
			       loop, xaddr, excess - excess1, prot, flags, xmaddr);

			if (xmaddr != xaddr)
				return -ENOMEM;
		}

		if (prot & PROT_WRITE && excess1 > 0) {
			kdebug("clear[%d] ad=%lx sz=%lx",
			       loop, maddr + phdr->p_filesz, excess1);
			clear_user((void *) maddr + phdr->p_filesz, excess1);
		}

#else
		if (excess > 0) {
			kdebug("clear[%d] ad=%lx sz=%lx",
			       loop, maddr + phdr->p_filesz, excess);
			clear_user((void *) maddr + phdr->p_filesz, excess);
		}
#endif

		if (mm) {
			if (phdr->p_flags & PF_X) {
				mm->start_code = maddr;
				mm->end_code = maddr + phdr->p_memsz;
			}
			else if (!mm->start_data) {
				mm->start_data = maddr;
				mm->end_data = maddr + phdr->p_memsz;
			}
		}

		seg++;
	}

	return 0;
} /* end elf_fdpic_map_file_by_direct_mmap() */
