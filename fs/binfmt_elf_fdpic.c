/* binfmt_elf_fdpic.c: FDPIC ELF binary format
 *
 * Copyright (C) 2003, 2004, 2006 Red Hat, Inc. All Rights Reserved.
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
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/highuid.h>
#include <linux/personality.h>
#include <linux/ptrace.h>
#include <linux/init.h>
#include <linux/elf.h>
#include <linux/elf-fdpic.h>
#include <linux/elfcore.h>

#include <asm/uaccess.h>
#include <asm/param.h>
#include <asm/pgalloc.h>

typedef char *elf_caddr_t;

#if 0
#define kdebug(fmt, ...) printk("FDPIC "fmt"\n" ,##__VA_ARGS__ )
#else
#define kdebug(fmt, ...) do {} while(0)
#endif

#if 0
#define kdcore(fmt, ...) printk("FDPIC "fmt"\n" ,##__VA_ARGS__ )
#else
#define kdcore(fmt, ...) do {} while(0)
#endif

MODULE_LICENSE("GPL");

static int load_elf_fdpic_binary(struct linux_binprm *, struct pt_regs *);
static int elf_fdpic_fetch_phdrs(struct elf_fdpic_params *, struct file *);
static int elf_fdpic_map_file(struct elf_fdpic_params *, struct file *,
			      struct mm_struct *, const char *);

static int create_elf_fdpic_tables(struct linux_binprm *, struct mm_struct *,
				   struct elf_fdpic_params *,
				   struct elf_fdpic_params *);

#ifndef CONFIG_MMU
static int elf_fdpic_transfer_args_to_stack(struct linux_binprm *,
					    unsigned long *);
static int elf_fdpic_map_file_constdisp_on_uclinux(struct elf_fdpic_params *,
						   struct file *,
						   struct mm_struct *);
#endif

static int elf_fdpic_map_file_by_direct_mmap(struct elf_fdpic_params *,
					     struct file *, struct mm_struct *);

#if defined(USE_ELF_CORE_DUMP) && defined(CONFIG_ELF_CORE)
static int elf_fdpic_core_dump(long, struct pt_regs *, struct file *, unsigned long limit);
#endif

static struct linux_binfmt elf_fdpic_format = {
	.module		= THIS_MODULE,
	.load_binary	= load_elf_fdpic_binary,
#if defined(USE_ELF_CORE_DUMP) && defined(CONFIG_ELF_CORE)
	.core_dump	= elf_fdpic_core_dump,
#endif
	.min_coredump	= ELF_EXEC_PAGESIZE,
};

static int __init init_elf_fdpic_binfmt(void)
{
	return register_binfmt(&elf_fdpic_format);
}

static void __exit exit_elf_fdpic_binfmt(void)
{
	unregister_binfmt(&elf_fdpic_format);
}

core_initcall(init_elf_fdpic_binfmt);
module_exit(exit_elf_fdpic_binfmt);

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
static int elf_fdpic_fetch_phdrs(struct elf_fdpic_params *params,
				 struct file *file)
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

	retval = kernel_read(file, params->hdr.e_phoff,
			     (char *) params->phdrs, size);
	if (unlikely(retval != size))
		return retval < 0 ? retval : -ENOEXEC;

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
}

/*****************************************************************************/
/*
 * load an fdpic binary into various bits of memory
 */
static int load_elf_fdpic_binary(struct linux_binprm *bprm,
				 struct pt_regs *regs)
{
	struct elf_fdpic_params exec_params, interp_params;
	struct elf_phdr *phdr;
	unsigned long stack_size, entryaddr;
#ifndef CONFIG_MMU
	unsigned long fullsize;
#endif
#ifdef ELF_FDPIC_PLAT_INIT
	unsigned long dynaddr;
#endif
	struct file *interpreter = NULL; /* to shut gcc up */
	char *interpreter_name = NULL;
	int executable_stack;
	int retval, i;

	kdebug("____ LOAD %d ____", current->pid);

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
			interpreter_name = kmalloc(phdr->p_filesz, GFP_KERNEL);
			if (!interpreter_name)
				goto error;

			retval = kernel_read(bprm->file,
					     phdr->p_offset,
					     interpreter_name,
					     phdr->p_filesz);
			if (unlikely(retval != phdr->p_filesz)) {
				if (retval >= 0)
					retval = -ENOEXEC;
				goto error;
			}

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

			/*
			 * If the binary is not readable then enforce
			 * mm->dumpable = 0 regardless of the interpreter's
			 * permissions.
			 */
			if (file_permission(interpreter, MAY_READ) < 0)
				bprm->interp_flags |= BINPRM_FLAGS_ENFORCE_NONDUMP;

			retval = kernel_read(interpreter, 0, bprm->buf,
					     BINPRM_BUF_SIZE);
			if (unlikely(retval != BINPRM_BUF_SIZE)) {
				if (retval >= 0)
					retval = -ENOEXEC;
				goto error;
			}

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

	retval = setup_arg_pages(bprm, current->mm->start_stack,
				 executable_stack);
	if (retval < 0) {
		send_sig(SIGKILL, current, 0);
		goto error_kill;
	}
#endif

	/* load the executable and interpreter into memory */
	retval = elf_fdpic_map_file(&exec_params, bprm->file, current->mm,
				    "executable");
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

	current->mm->brk = current->mm->start_brk =
		PAGE_ALIGN(current->mm->start_brk);

#else
	/* create a stack and brk area big enough for everyone
	 * - the brk heap starts at the bottom and works up
	 * - the stack starts at the top and works down
	 */
	stack_size = (stack_size + PAGE_SIZE - 1) & PAGE_MASK;
	if (stack_size < PAGE_SIZE * 2)
		stack_size = PAGE_SIZE * 2;

	down_write(&current->mm->mmap_sem);
	current->mm->start_brk = do_mmap(NULL, 0, stack_size,
					 PROT_READ | PROT_WRITE | PROT_EXEC,
					 MAP_PRIVATE | MAP_ANONYMOUS | MAP_GROWSDOWN,
					 0);

	if (IS_ERR_VALUE(current->mm->start_brk)) {
		up_write(&current->mm->mmap_sem);
		retval = current->mm->start_brk;
		current->mm->start_brk = 0;
		goto error_kill;
	}

	/* expand the stack mapping to use up the entire allocation granule */
	fullsize = kobjsize((char *) current->mm->start_brk);
	if (!IS_ERR_VALUE(do_mremap(current->mm->start_brk, stack_size,
				    fullsize, 0, 0)))
		stack_size = fullsize;
	up_write(&current->mm->mmap_sem);

	current->mm->brk = current->mm->start_brk;
	current->mm->context.end_brk = current->mm->start_brk;
	current->mm->context.end_brk +=
		(stack_size > PAGE_SIZE) ? (stack_size - PAGE_SIZE) : 0;
	current->mm->start_stack = current->mm->start_brk + stack_size;
#endif

	compute_creds(bprm);
	current->flags &= ~PF_FORKNOEXEC;
	if (create_elf_fdpic_tables(bprm, current->mm,
				    &exec_params, &interp_params) < 0)
		goto error_kill;

	kdebug("- start_code  %lx", current->mm->start_code);
	kdebug("- end_code    %lx", current->mm->end_code);
	kdebug("- start_data  %lx", current->mm->start_data);
	kdebug("- end_data    %lx", current->mm->end_data);
	kdebug("- start_brk   %lx", current->mm->start_brk);
	kdebug("- brk         %lx", current->mm->brk);
	kdebug("- start_stack %lx", current->mm->start_stack);

#ifdef ELF_FDPIC_PLAT_INIT
	/*
	 * The ABI may specify that certain registers be set up in special
	 * ways (on i386 %edx is the address of a DT_FINI function, for
	 * example.  This macro performs whatever initialization to
	 * the regs structure is required.
	 */
	dynaddr = interp_params.dynamic_addr ?: exec_params.dynamic_addr;
	ELF_FDPIC_PLAT_INIT(regs, exec_params.map_addr, interp_params.map_addr,
			    dynaddr);
#endif

	/* everything is now ready... get the userspace context ready to roll */
	entryaddr = interp_params.entry_addr ?: exec_params.entry_addr;
	start_thread(regs, entryaddr, current->mm->start_stack);

	retval = 0;

error:
	if (interpreter) {
		allow_write_access(interpreter);
		fput(interpreter);
	}
	kfree(interpreter_name);
	kfree(exec_params.phdrs);
	kfree(exec_params.loadmap);
	kfree(interp_params.phdrs);
	kfree(interp_params.loadmap);
	return retval;

	/* unrecoverable error - kill the process */
error_kill:
	send_sig(SIGSEGV, current, 0);
	goto error;

}

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
	elf_caddr_t __user *argv, *envp;
	size_t platform_len = 0, len;
	char *k_platform;
	char __user *u_platform, *p;
	long hwcap;
	int loop;
	int nr;	/* reset for each csp adjustment */

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
	u_platform = NULL;

	if (k_platform) {
		platform_len = strlen(k_platform) + 1;
		sp -= platform_len;
		u_platform = (char __user *) sp;
		if (__copy_to_user(u_platform, k_platform, platform_len) != 0)
			return -EFAULT;
	}

#if defined(__i386__) && defined(CONFIG_SMP)
	/* in some cases (e.g. Hyper-Threading), we want to avoid L1 evictions
	 * by the processes running on the same package. One thing we can do is
	 * to shuffle the initial stack for them.
	 *
	 * the conditionals here are unneeded, but kept in to make the code
	 * behaviour the same as pre change unless we have hyperthreaded
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

	if (copy_to_user((void __user *) sp, exec_params->loadmap, len) != 0)
		return -EFAULT;

	current->mm->context.exec_fdpic_loadmap = (unsigned long) sp;

	if (interp_params->loadmap) {
		len = sizeof(struct elf32_fdpic_loadmap);
		len += sizeof(struct elf32_fdpic_loadseg) *
			interp_params->loadmap->nsegs;
		sp = (sp - len) & ~7UL;
		interp_params->map_addr = sp;

		if (copy_to_user((void __user *) sp, interp_params->loadmap,
				 len) != 0)
			return -EFAULT;

		current->mm->context.interp_fdpic_loadmap = (unsigned long) sp;
	}

	/* force 16 byte _final_ alignment here for generality */
#define DLINFO_ITEMS 13

	nitems = 1 + DLINFO_ITEMS + (k_platform ? 1 : 0) + AT_VECTOR_SIZE_ARCH;

	csp = sp;
	sp -= nitems * 2 * sizeof(unsigned long);
	sp -= (bprm->envc + 1) * sizeof(char *);	/* envv[] */
	sp -= (bprm->argc + 1) * sizeof(char *);	/* argv[] */
	sp -= 1 * sizeof(unsigned long);		/* argc */

	csp -= sp & 15UL;
	sp -= sp & 15UL;

	/* put the ELF interpreter info on the stack */
#define NEW_AUX_ENT(id, val)						\
	do {								\
		struct { unsigned long _id, _val; } __user *ent;	\
									\
		ent = (void __user *) csp;				\
		__put_user((id), &ent[nr]._id);				\
		__put_user((val), &ent[nr]._val);			\
		nr++;							\
	} while (0)

	nr = 0;
	csp -= 2 * sizeof(unsigned long);
	NEW_AUX_ENT(AT_NULL, 0);
	if (k_platform) {
		nr = 0;
		csp -= 2 * sizeof(unsigned long);
		NEW_AUX_ENT(AT_PLATFORM,
			    (elf_addr_t) (unsigned long) u_platform);
	}

	nr = 0;
	csp -= DLINFO_ITEMS * 2 * sizeof(unsigned long);
	NEW_AUX_ENT(AT_HWCAP,	hwcap);
	NEW_AUX_ENT(AT_PAGESZ,	PAGE_SIZE);
	NEW_AUX_ENT(AT_CLKTCK,	CLOCKS_PER_SEC);
	NEW_AUX_ENT(AT_PHDR,	exec_params->ph_addr);
	NEW_AUX_ENT(AT_PHENT,	sizeof(struct elf_phdr));
	NEW_AUX_ENT(AT_PHNUM,	exec_params->hdr.e_phnum);
	NEW_AUX_ENT(AT_BASE,	interp_params->elfhdr_addr);
	NEW_AUX_ENT(AT_FLAGS,	0);
	NEW_AUX_ENT(AT_ENTRY,	exec_params->entry_addr);
	NEW_AUX_ENT(AT_UID,	(elf_addr_t) current->uid);
	NEW_AUX_ENT(AT_EUID,	(elf_addr_t) current->euid);
	NEW_AUX_ENT(AT_GID,	(elf_addr_t) current->gid);
	NEW_AUX_ENT(AT_EGID,	(elf_addr_t) current->egid);

#ifdef ARCH_DLINFO
	nr = 0;
	csp -= AT_VECTOR_SIZE_ARCH * 2 * sizeof(unsigned long);

	/* ARCH_DLINFO must come last so platform specific code can enforce
	 * special alignment requirements on the AUXV if necessary (eg. PPC).
	 */
	ARCH_DLINFO;
#endif
#undef NEW_AUX_ENT

	/* allocate room for argv[] and envv[] */
	csp -= (bprm->envc + 1) * sizeof(elf_caddr_t);
	envp = (elf_caddr_t __user *) csp;
	csp -= (bprm->argc + 1) * sizeof(elf_caddr_t);
	argv = (elf_caddr_t __user *) csp;

	/* stack argc */
	csp -= sizeof(unsigned long);
	__put_user(bprm->argc, (unsigned long __user *) csp);

	BUG_ON(csp != sp);

	/* fill in the argv[] array */
#ifdef CONFIG_MMU
	current->mm->arg_start = bprm->p;
#else
	current->mm->arg_start = current->mm->start_stack -
		(MAX_ARG_PAGES * PAGE_SIZE - bprm->p);
#endif

	p = (char __user *) current->mm->arg_start;
	for (loop = bprm->argc; loop > 0; loop--) {
		__put_user((elf_caddr_t) p, argv++);
		len = strnlen_user(p, MAX_ARG_STRLEN);
		if (!len || len > MAX_ARG_STRLEN)
			return -EINVAL;
		p += len;
	}
	__put_user(NULL, argv);
	current->mm->arg_end = (unsigned long) p;

	/* fill in the envv[] array */
	current->mm->env_start = (unsigned long) p;
	for (loop = bprm->envc; loop > 0; loop--) {
		__put_user((elf_caddr_t)(unsigned long) p, envp++);
		len = strnlen_user(p, MAX_ARG_STRLEN);
		if (!len || len > MAX_ARG_STRLEN)
			return -EINVAL;
		p += len;
	}
	__put_user(NULL, envp);
	current->mm->env_end = (unsigned long) p;

	mm->start_stack = (unsigned long) sp;
	return 0;
}

/*****************************************************************************/
/*
 * transfer the program arguments and environment from the holding pages onto
 * the stack
 */
#ifndef CONFIG_MMU
static int elf_fdpic_transfer_args_to_stack(struct linux_binprm *bprm,
					    unsigned long *_sp)
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
}
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
	loadmap = kzalloc(size, GFP_KERNEL);
	if (!loadmap)
		return -ENOMEM;

	params->loadmap = loadmap;

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
			    params->hdr.e_entry < seg->p_vaddr + seg->p_memsz) {
				params->entry_addr =
					(params->hdr.e_entry - seg->p_vaddr) +
					seg->addr;
				break;
			}
		}
	}

	/* determine where the program header table has wound up if mapped */
	stop = params->hdr.e_phoff;
	stop += params->hdr.e_phnum * sizeof (struct elf_phdr);
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
			    phdr->p_vaddr + phdr->p_filesz <=
			    seg->p_vaddr + seg->p_memsz) {
				params->ph_addr =
					(phdr->p_vaddr - seg->p_vaddr) +
					seg->addr +
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
			    phdr->p_vaddr + phdr->p_memsz <=
			    seg->p_vaddr + seg->p_memsz) {
				params->dynamic_addr =
					(phdr->p_vaddr - seg->p_vaddr) +
					seg->addr;

				/* check the dynamic section contains at least
				 * one item, and that the last item is a NULL
				 * entry */
				if (phdr->p_memsz == 0 ||
				    phdr->p_memsz % sizeof(Elf32_Dyn) != 0)
					goto dynamic_error;

				tmp = phdr->p_memsz / sizeof(Elf32_Dyn);
				if (((Elf32_Dyn *)
				     params->dynamic_addr)[tmp - 1].d_tag != 0)
					goto dynamic_error;
				break;
			}
		}
		break;
	}

	/* now elide adjacent segments in the load map on MMU linux
	 * - on uClinux the holes between may actually be filled with system
	 *   stuff or stuff from other processes
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
				mseg->p_memsz +=
					load_addr -
					(mseg->addr + mseg->p_memsz);
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
	       what, file->f_path.dentry->d_inode->i_ino);
	return -ELIBBAD;
}

/*****************************************************************************/
/*
 * map a file with constant displacement under uClinux
 */
#ifndef CONFIG_MMU
static int elf_fdpic_map_file_constdisp_on_uclinux(
	struct elf_fdpic_params *params,
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

	/* determine the bounds of the contiguous overall allocation we must
	 * make */
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
	if (IS_ERR_VALUE(maddr))
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

		ret = file->f_op->read(file, (void *) seg->addr,
				       phdr->p_filesz, &fpos);
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
				if (!mm->start_code) {
					mm->start_code = seg->addr;
					mm->end_code = seg->addr +
						phdr->p_memsz;
				}
			} else if (!mm->start_data) {
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
}
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
			 * - can be mapped anywhere, but must be mapped as a
			 *   unit
			 */
			if (!dvset) {
				maddr = load_addr;
				delta_vaddr = phdr->p_vaddr;
				dvset = 1;
			} else {
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
		       loop, phdr->p_memsz + disp, prot, flags,
		       phdr->p_offset - disp, maddr);

		if (IS_ERR_VALUE(maddr))
			return (int) maddr;

		if ((params->flags & ELF_FDPIC_FLAG_ARRANGEMENT) ==
		    ELF_FDPIC_FLAG_CONTIGUOUS)
			load_addr += PAGE_ALIGN(phdr->p_memsz + disp);

		seg->addr = maddr + disp;
		seg->p_vaddr = phdr->p_vaddr;
		seg->p_memsz = phdr->p_memsz;

		/* map the ELF header address if in this segment */
		if (phdr->p_offset == 0)
			params->elfhdr_addr = seg->addr;

		/* clear the bit between beginning of mapping and beginning of
		 * PT_LOAD */
		if (prot & PROT_WRITE && disp > 0) {
			kdebug("clear[%d] ad=%lx sz=%lx", loop, maddr, disp);
			clear_user((void __user *) maddr, disp);
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
			xmaddr = do_mmap(NULL, xaddr, excess - excess1,
					 prot, flags, 0);
			up_write(&mm->mmap_sem);

			kdebug("mmap[%d] <anon>"
			       " ad=%lx sz=%lx pr=%x fl=%x of=0 --> %08lx",
			       loop, xaddr, excess - excess1, prot, flags,
			       xmaddr);

			if (xmaddr != xaddr)
				return -ENOMEM;
		}

		if (prot & PROT_WRITE && excess1 > 0) {
			kdebug("clear[%d] ad=%lx sz=%lx",
			       loop, maddr + phdr->p_filesz, excess1);
			clear_user((void __user *) maddr + phdr->p_filesz,
				   excess1);
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
				if (!mm->start_code) {
					mm->start_code = maddr;
					mm->end_code = maddr + phdr->p_memsz;
				}
			} else if (!mm->start_data) {
				mm->start_data = maddr;
				mm->end_data = maddr + phdr->p_memsz;
			}
		}

		seg++;
	}

	return 0;
}

/*****************************************************************************/
/*
 * ELF-FDPIC core dumper
 *
 * Modelled on fs/exec.c:aout_core_dump()
 * Jeremy Fitzhardinge <jeremy@sw.oz.au>
 *
 * Modelled on fs/binfmt_elf.c core dumper
 */
#if defined(USE_ELF_CORE_DUMP) && defined(CONFIG_ELF_CORE)

/*
 * These are the only things you should do on a core-file: use only these
 * functions to write out all the necessary info.
 */
static int dump_write(struct file *file, const void *addr, int nr)
{
	return file->f_op->write(file, addr, nr, &file->f_pos) == nr;
}

static int dump_seek(struct file *file, loff_t off)
{
	if (file->f_op->llseek) {
		if (file->f_op->llseek(file, off, SEEK_SET) != off)
			return 0;
	} else {
		file->f_pos = off;
	}
	return 1;
}

/*
 * Decide whether a segment is worth dumping; default is yes to be
 * sure (missing info is worse than too much; etc).
 * Personally I'd include everything, and use the coredump limit...
 *
 * I think we should skip something. But I am not sure how. H.J.
 */
static int maydump(struct vm_area_struct *vma, unsigned long mm_flags)
{
	int dump_ok;

	/* Do not dump I/O mapped devices or special mappings */
	if (vma->vm_flags & (VM_IO | VM_RESERVED)) {
		kdcore("%08lx: %08lx: no (IO)", vma->vm_start, vma->vm_flags);
		return 0;
	}

	/* If we may not read the contents, don't allow us to dump
	 * them either. "dump_write()" can't handle it anyway.
	 */
	if (!(vma->vm_flags & VM_READ)) {
		kdcore("%08lx: %08lx: no (!read)", vma->vm_start, vma->vm_flags);
		return 0;
	}

	/* By default, dump shared memory if mapped from an anonymous file. */
	if (vma->vm_flags & VM_SHARED) {
		if (vma->vm_file->f_path.dentry->d_inode->i_nlink == 0) {
			dump_ok = test_bit(MMF_DUMP_ANON_SHARED, &mm_flags);
			kdcore("%08lx: %08lx: %s (share)", vma->vm_start,
			       vma->vm_flags, dump_ok ? "yes" : "no");
			return dump_ok;
		}

		dump_ok = test_bit(MMF_DUMP_MAPPED_SHARED, &mm_flags);
		kdcore("%08lx: %08lx: %s (share)", vma->vm_start,
		       vma->vm_flags, dump_ok ? "yes" : "no");
		return dump_ok;
	}

#ifdef CONFIG_MMU
	/* By default, if it hasn't been written to, don't write it out */
	if (!vma->anon_vma) {
		dump_ok = test_bit(MMF_DUMP_MAPPED_PRIVATE, &mm_flags);
		kdcore("%08lx: %08lx: %s (!anon)", vma->vm_start,
		       vma->vm_flags, dump_ok ? "yes" : "no");
		return dump_ok;
	}
#endif

	dump_ok = test_bit(MMF_DUMP_ANON_PRIVATE, &mm_flags);
	kdcore("%08lx: %08lx: %s", vma->vm_start, vma->vm_flags,
	       dump_ok ? "yes" : "no");
	return dump_ok;
}

/* An ELF note in memory */
struct memelfnote
{
	const char *name;
	int type;
	unsigned int datasz;
	void *data;
};

static int notesize(struct memelfnote *en)
{
	int sz;

	sz = sizeof(struct elf_note);
	sz += roundup(strlen(en->name) + 1, 4);
	sz += roundup(en->datasz, 4);

	return sz;
}

/* #define DEBUG */

#define DUMP_WRITE(addr, nr)	\
	do { if (!dump_write(file, (addr), (nr))) return 0; } while(0)
#define DUMP_SEEK(off)	\
	do { if (!dump_seek(file, (off))) return 0; } while(0)

static int writenote(struct memelfnote *men, struct file *file)
{
	struct elf_note en;

	en.n_namesz = strlen(men->name) + 1;
	en.n_descsz = men->datasz;
	en.n_type = men->type;

	DUMP_WRITE(&en, sizeof(en));
	DUMP_WRITE(men->name, en.n_namesz);
	/* XXX - cast from long long to long to avoid need for libgcc.a */
	DUMP_SEEK(roundup((unsigned long)file->f_pos, 4));	/* XXX */
	DUMP_WRITE(men->data, men->datasz);
	DUMP_SEEK(roundup((unsigned long)file->f_pos, 4));	/* XXX */

	return 1;
}
#undef DUMP_WRITE
#undef DUMP_SEEK

#define DUMP_WRITE(addr, nr)	\
	if ((size += (nr)) > limit || !dump_write(file, (addr), (nr))) \
		goto end_coredump;
#define DUMP_SEEK(off)	\
	if (!dump_seek(file, (off))) \
		goto end_coredump;

static inline void fill_elf_fdpic_header(struct elfhdr *elf, int segs)
{
	memcpy(elf->e_ident, ELFMAG, SELFMAG);
	elf->e_ident[EI_CLASS] = ELF_CLASS;
	elf->e_ident[EI_DATA] = ELF_DATA;
	elf->e_ident[EI_VERSION] = EV_CURRENT;
	elf->e_ident[EI_OSABI] = ELF_OSABI;
	memset(elf->e_ident+EI_PAD, 0, EI_NIDENT-EI_PAD);

	elf->e_type = ET_CORE;
	elf->e_machine = ELF_ARCH;
	elf->e_version = EV_CURRENT;
	elf->e_entry = 0;
	elf->e_phoff = sizeof(struct elfhdr);
	elf->e_shoff = 0;
	elf->e_flags = ELF_FDPIC_CORE_EFLAGS;
	elf->e_ehsize = sizeof(struct elfhdr);
	elf->e_phentsize = sizeof(struct elf_phdr);
	elf->e_phnum = segs;
	elf->e_shentsize = 0;
	elf->e_shnum = 0;
	elf->e_shstrndx = 0;
	return;
}

static inline void fill_elf_note_phdr(struct elf_phdr *phdr, int sz, loff_t offset)
{
	phdr->p_type = PT_NOTE;
	phdr->p_offset = offset;
	phdr->p_vaddr = 0;
	phdr->p_paddr = 0;
	phdr->p_filesz = sz;
	phdr->p_memsz = 0;
	phdr->p_flags = 0;
	phdr->p_align = 0;
	return;
}

static inline void fill_note(struct memelfnote *note, const char *name, int type,
		unsigned int sz, void *data)
{
	note->name = name;
	note->type = type;
	note->datasz = sz;
	note->data = data;
	return;
}

/*
 * fill up all the fields in prstatus from the given task struct, except
 * registers which need to be filled up seperately.
 */
static void fill_prstatus(struct elf_prstatus *prstatus,
			  struct task_struct *p, long signr)
{
	prstatus->pr_info.si_signo = prstatus->pr_cursig = signr;
	prstatus->pr_sigpend = p->pending.signal.sig[0];
	prstatus->pr_sighold = p->blocked.sig[0];
	prstatus->pr_pid = task_pid_vnr(p);
	prstatus->pr_ppid = task_pid_vnr(p->parent);
	prstatus->pr_pgrp = task_pgrp_vnr(p);
	prstatus->pr_sid = task_session_vnr(p);
	if (thread_group_leader(p)) {
		/*
		 * This is the record for the group leader.  Add in the
		 * cumulative times of previous dead threads.  This total
		 * won't include the time of each live thread whose state
		 * is included in the core dump.  The final total reported
		 * to our parent process when it calls wait4 will include
		 * those sums as well as the little bit more time it takes
		 * this and each other thread to finish dying after the
		 * core dump synchronization phase.
		 */
		cputime_to_timeval(cputime_add(p->utime, p->signal->utime),
				   &prstatus->pr_utime);
		cputime_to_timeval(cputime_add(p->stime, p->signal->stime),
				   &prstatus->pr_stime);
	} else {
		cputime_to_timeval(p->utime, &prstatus->pr_utime);
		cputime_to_timeval(p->stime, &prstatus->pr_stime);
	}
	cputime_to_timeval(p->signal->cutime, &prstatus->pr_cutime);
	cputime_to_timeval(p->signal->cstime, &prstatus->pr_cstime);

	prstatus->pr_exec_fdpic_loadmap = p->mm->context.exec_fdpic_loadmap;
	prstatus->pr_interp_fdpic_loadmap = p->mm->context.interp_fdpic_loadmap;
}

static int fill_psinfo(struct elf_prpsinfo *psinfo, struct task_struct *p,
		       struct mm_struct *mm)
{
	unsigned int i, len;

	/* first copy the parameters from user space */
	memset(psinfo, 0, sizeof(struct elf_prpsinfo));

	len = mm->arg_end - mm->arg_start;
	if (len >= ELF_PRARGSZ)
		len = ELF_PRARGSZ - 1;
	if (copy_from_user(&psinfo->pr_psargs,
		           (const char __user *) mm->arg_start, len))
		return -EFAULT;
	for (i = 0; i < len; i++)
		if (psinfo->pr_psargs[i] == 0)
			psinfo->pr_psargs[i] = ' ';
	psinfo->pr_psargs[len] = 0;

	psinfo->pr_pid = task_pid_vnr(p);
	psinfo->pr_ppid = task_pid_vnr(p->parent);
	psinfo->pr_pgrp = task_pgrp_vnr(p);
	psinfo->pr_sid = task_session_vnr(p);

	i = p->state ? ffz(~p->state) + 1 : 0;
	psinfo->pr_state = i;
	psinfo->pr_sname = (i > 5) ? '.' : "RSDTZW"[i];
	psinfo->pr_zomb = psinfo->pr_sname == 'Z';
	psinfo->pr_nice = task_nice(p);
	psinfo->pr_flag = p->flags;
	SET_UID(psinfo->pr_uid, p->uid);
	SET_GID(psinfo->pr_gid, p->gid);
	strncpy(psinfo->pr_fname, p->comm, sizeof(psinfo->pr_fname));

	return 0;
}

/* Here is the structure in which status of each thread is captured. */
struct elf_thread_status
{
	struct list_head list;
	struct elf_prstatus prstatus;	/* NT_PRSTATUS */
	elf_fpregset_t fpu;		/* NT_PRFPREG */
	struct task_struct *thread;
#ifdef ELF_CORE_COPY_XFPREGS
	elf_fpxregset_t xfpu;		/* ELF_CORE_XFPREG_TYPE */
#endif
	struct memelfnote notes[3];
	int num_notes;
};

/*
 * In order to add the specific thread information for the elf file format,
 * we need to keep a linked list of every thread's pr_status and then create
 * a single section for them in the final core file.
 */
static int elf_dump_thread_status(long signr, struct elf_thread_status *t)
{
	struct task_struct *p = t->thread;
	int sz = 0;

	t->num_notes = 0;

	fill_prstatus(&t->prstatus, p, signr);
	elf_core_copy_task_regs(p, &t->prstatus.pr_reg);

	fill_note(&t->notes[0], "CORE", NT_PRSTATUS, sizeof(t->prstatus),
		  &t->prstatus);
	t->num_notes++;
	sz += notesize(&t->notes[0]);

	t->prstatus.pr_fpvalid = elf_core_copy_task_fpregs(p, NULL, &t->fpu);
	if (t->prstatus.pr_fpvalid) {
		fill_note(&t->notes[1], "CORE", NT_PRFPREG, sizeof(t->fpu),
			  &t->fpu);
		t->num_notes++;
		sz += notesize(&t->notes[1]);
	}

#ifdef ELF_CORE_COPY_XFPREGS
	if (elf_core_copy_task_xfpregs(p, &t->xfpu)) {
		fill_note(&t->notes[2], "LINUX", ELF_CORE_XFPREG_TYPE,
			  sizeof(t->xfpu), &t->xfpu);
		t->num_notes++;
		sz += notesize(&t->notes[2]);
	}
#endif
	return sz;
}

/*
 * dump the segments for an MMU process
 */
#ifdef CONFIG_MMU
static int elf_fdpic_dump_segments(struct file *file, size_t *size,
			   unsigned long *limit, unsigned long mm_flags)
{
	struct vm_area_struct *vma;

	for (vma = current->mm->mmap; vma; vma = vma->vm_next) {
		unsigned long addr;

		if (!maydump(vma, mm_flags))
			continue;

		for (addr = vma->vm_start;
		     addr < vma->vm_end;
		     addr += PAGE_SIZE
		     ) {
			struct vm_area_struct *vma;
			struct page *page;

			if (get_user_pages(current, current->mm, addr, 1, 0, 1,
					   &page, &vma) <= 0) {
				DUMP_SEEK(file->f_pos + PAGE_SIZE);
			}
			else if (page == ZERO_PAGE(0)) {
				page_cache_release(page);
				DUMP_SEEK(file->f_pos + PAGE_SIZE);
			}
			else {
				void *kaddr;

				flush_cache_page(vma, addr, page_to_pfn(page));
				kaddr = kmap(page);
				if ((*size += PAGE_SIZE) > *limit ||
				    !dump_write(file, kaddr, PAGE_SIZE)
				    ) {
					kunmap(page);
					page_cache_release(page);
					return -EIO;
				}
				kunmap(page);
				page_cache_release(page);
			}
		}
	}

	return 0;

end_coredump:
	return -EFBIG;
}
#endif

/*
 * dump the segments for a NOMMU process
 */
#ifndef CONFIG_MMU
static int elf_fdpic_dump_segments(struct file *file, size_t *size,
			   unsigned long *limit, unsigned long mm_flags)
{
	struct vm_list_struct *vml;

	for (vml = current->mm->context.vmlist; vml; vml = vml->next) {
	struct vm_area_struct *vma = vml->vma;

		if (!maydump(vma, mm_flags))
			continue;

		if ((*size += PAGE_SIZE) > *limit)
			return -EFBIG;

		if (!dump_write(file, (void *) vma->vm_start,
				vma->vm_end - vma->vm_start))
			return -EIO;
	}

	return 0;
}
#endif

/*
 * Actual dumper
 *
 * This is a two-pass process; first we find the offsets of the bits,
 * and then they are actually written out.  If we run out of core limit
 * we just truncate.
 */
static int elf_fdpic_core_dump(long signr, struct pt_regs *regs,
			       struct file *file, unsigned long limit)
{
#define	NUM_NOTES	6
	int has_dumped = 0;
	mm_segment_t fs;
	int segs;
	size_t size = 0;
	int i;
	struct vm_area_struct *vma;
	struct elfhdr *elf = NULL;
	loff_t offset = 0, dataoff;
	int numnote;
	struct memelfnote *notes = NULL;
	struct elf_prstatus *prstatus = NULL;	/* NT_PRSTATUS */
	struct elf_prpsinfo *psinfo = NULL;	/* NT_PRPSINFO */
 	LIST_HEAD(thread_list);
 	struct list_head *t;
	elf_fpregset_t *fpu = NULL;
#ifdef ELF_CORE_COPY_XFPREGS
	elf_fpxregset_t *xfpu = NULL;
#endif
	int thread_status_size = 0;
#ifndef CONFIG_MMU
	struct vm_list_struct *vml;
#endif
	elf_addr_t *auxv;
	unsigned long mm_flags;

	/*
	 * We no longer stop all VM operations.
	 *
	 * This is because those proceses that could possibly change map_count
	 * or the mmap / vma pages are now blocked in do_exit on current
	 * finishing this core dump.
	 *
	 * Only ptrace can touch these memory addresses, but it doesn't change
	 * the map_count or the pages allocated. So no possibility of crashing
	 * exists while dumping the mm->vm_next areas to the core file.
	 */

	/* alloc memory for large data structures: too large to be on stack */
	elf = kmalloc(sizeof(*elf), GFP_KERNEL);
	if (!elf)
		goto cleanup;
	prstatus = kzalloc(sizeof(*prstatus), GFP_KERNEL);
	if (!prstatus)
		goto cleanup;
	psinfo = kmalloc(sizeof(*psinfo), GFP_KERNEL);
	if (!psinfo)
		goto cleanup;
	notes = kmalloc(NUM_NOTES * sizeof(struct memelfnote), GFP_KERNEL);
	if (!notes)
		goto cleanup;
	fpu = kmalloc(sizeof(*fpu), GFP_KERNEL);
	if (!fpu)
		goto cleanup;
#ifdef ELF_CORE_COPY_XFPREGS
	xfpu = kmalloc(sizeof(*xfpu), GFP_KERNEL);
	if (!xfpu)
		goto cleanup;
#endif

	if (signr) {
		struct core_thread *ct;
		struct elf_thread_status *tmp;

		for (ct = current->mm->core_state->dumper.next;
						ct; ct = ct->next) {
			tmp = kzalloc(sizeof(*tmp), GFP_KERNEL);
			if (!tmp)
				goto cleanup;

			tmp->thread = ct->task;
			list_add(&tmp->list, &thread_list);
		}

		list_for_each(t, &thread_list) {
			struct elf_thread_status *tmp;
			int sz;

			tmp = list_entry(t, struct elf_thread_status, list);
			sz = elf_dump_thread_status(signr, tmp);
			thread_status_size += sz;
		}
	}

	/* now collect the dump for the current */
	fill_prstatus(prstatus, current, signr);
	elf_core_copy_regs(&prstatus->pr_reg, regs);

#ifdef CONFIG_MMU
	segs = current->mm->map_count;
#else
	segs = 0;
	for (vml = current->mm->context.vmlist; vml; vml = vml->next)
	    segs++;
#endif
#ifdef ELF_CORE_EXTRA_PHDRS
	segs += ELF_CORE_EXTRA_PHDRS;
#endif

	/* Set up header */
	fill_elf_fdpic_header(elf, segs + 1);	/* including notes section */

	has_dumped = 1;
	current->flags |= PF_DUMPCORE;

	/*
	 * Set up the notes in similar form to SVR4 core dumps made
	 * with info from their /proc.
	 */

	fill_note(notes + 0, "CORE", NT_PRSTATUS, sizeof(*prstatus), prstatus);
	fill_psinfo(psinfo, current->group_leader, current->mm);
	fill_note(notes + 1, "CORE", NT_PRPSINFO, sizeof(*psinfo), psinfo);

	numnote = 2;

	auxv = (elf_addr_t *) current->mm->saved_auxv;

	i = 0;
	do
		i += 2;
	while (auxv[i - 2] != AT_NULL);
	fill_note(&notes[numnote++], "CORE", NT_AUXV,
		  i * sizeof(elf_addr_t), auxv);

  	/* Try to dump the FPU. */
	if ((prstatus->pr_fpvalid =
	     elf_core_copy_task_fpregs(current, regs, fpu)))
		fill_note(notes + numnote++,
			  "CORE", NT_PRFPREG, sizeof(*fpu), fpu);
#ifdef ELF_CORE_COPY_XFPREGS
	if (elf_core_copy_task_xfpregs(current, xfpu))
		fill_note(notes + numnote++,
			  "LINUX", ELF_CORE_XFPREG_TYPE, sizeof(*xfpu), xfpu);
#endif

	fs = get_fs();
	set_fs(KERNEL_DS);

	DUMP_WRITE(elf, sizeof(*elf));
	offset += sizeof(*elf);				/* Elf header */
	offset += (segs+1) * sizeof(struct elf_phdr);	/* Program headers */

	/* Write notes phdr entry */
	{
		struct elf_phdr phdr;
		int sz = 0;

		for (i = 0; i < numnote; i++)
			sz += notesize(notes + i);

		sz += thread_status_size;

		fill_elf_note_phdr(&phdr, sz, offset);
		offset += sz;
		DUMP_WRITE(&phdr, sizeof(phdr));
	}

	/* Page-align dumped data */
	dataoff = offset = roundup(offset, ELF_EXEC_PAGESIZE);

	/*
	 * We must use the same mm->flags while dumping core to avoid
	 * inconsistency between the program headers and bodies, otherwise an
	 * unusable core file can be generated.
	 */
	mm_flags = current->mm->flags;

	/* write program headers for segments dump */
	for (
#ifdef CONFIG_MMU
		vma = current->mm->mmap; vma; vma = vma->vm_next
#else
			vml = current->mm->context.vmlist; vml; vml = vml->next
#endif
	     ) {
		struct elf_phdr phdr;
		size_t sz;

#ifndef CONFIG_MMU
		vma = vml->vma;
#endif

		sz = vma->vm_end - vma->vm_start;

		phdr.p_type = PT_LOAD;
		phdr.p_offset = offset;
		phdr.p_vaddr = vma->vm_start;
		phdr.p_paddr = 0;
		phdr.p_filesz = maydump(vma, mm_flags) ? sz : 0;
		phdr.p_memsz = sz;
		offset += phdr.p_filesz;
		phdr.p_flags = vma->vm_flags & VM_READ ? PF_R : 0;
		if (vma->vm_flags & VM_WRITE)
			phdr.p_flags |= PF_W;
		if (vma->vm_flags & VM_EXEC)
			phdr.p_flags |= PF_X;
		phdr.p_align = ELF_EXEC_PAGESIZE;

		DUMP_WRITE(&phdr, sizeof(phdr));
	}

#ifdef ELF_CORE_WRITE_EXTRA_PHDRS
	ELF_CORE_WRITE_EXTRA_PHDRS;
#endif

 	/* write out the notes section */
	for (i = 0; i < numnote; i++)
		if (!writenote(notes + i, file))
			goto end_coredump;

	/* write out the thread status notes section */
	list_for_each(t, &thread_list) {
		struct elf_thread_status *tmp =
				list_entry(t, struct elf_thread_status, list);

		for (i = 0; i < tmp->num_notes; i++)
			if (!writenote(&tmp->notes[i], file))
				goto end_coredump;
	}

	DUMP_SEEK(dataoff);

	if (elf_fdpic_dump_segments(file, &size, &limit, mm_flags) < 0)
		goto end_coredump;

#ifdef ELF_CORE_WRITE_EXTRA_DATA
	ELF_CORE_WRITE_EXTRA_DATA;
#endif

	if (file->f_pos != offset) {
		/* Sanity check */
		printk(KERN_WARNING
		       "elf_core_dump: file->f_pos (%lld) != offset (%lld)\n",
		       file->f_pos, offset);
	}

end_coredump:
	set_fs(fs);

cleanup:
	while (!list_empty(&thread_list)) {
		struct list_head *tmp = thread_list.next;
		list_del(tmp);
		kfree(list_entry(tmp, struct elf_thread_status, list));
	}

	kfree(elf);
	kfree(prstatus);
	kfree(psinfo);
	kfree(notes);
	kfree(fpu);
#ifdef ELF_CORE_COPY_XFPREGS
	kfree(xfpu);
#endif
	return has_dumped;
#undef NUM_NOTES
}

#endif		/* USE_ELF_CORE_DUMP */
