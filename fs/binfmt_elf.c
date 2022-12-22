// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/fs/binfmt_elf.c
 *
 * These are the functions used to load ELF format executables as used
 * on SVr4 machines.  Information on the format may be found in the book
 * "UNIX SYSTEM V RELEASE 4 Programmers Guide: Ansi C and Programming Support
 * Tools".
 *
 * Copyright 1993, 1994: Eric Youngdale (ericy@cais.com).
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/log2.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/binfmts.h>
#include <linux/string.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/personality.h>
#include <linux/elfcore.h>
#include <linux/init.h>
#include <linux/highuid.h>
#include <linux/compiler.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/security.h>
#include <linux/random.h>
#include <linux/elf.h>
#include <linux/elf-randomize.h>
#include <linux/utsname.h>
#include <linux/coredump.h>
#include <linux/sched.h>
#include <linux/sched/coredump.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/cputime.h>
#include <linux/sizes.h>
#include <linux/types.h>
#include <linux/cred.h>
#include <linux/dax.h>
#include <linux/uaccess.h>
#include <asm/param.h>
#include <asm/page.h>

#ifndef ELF_COMPAT
#define ELF_COMPAT 0
#endif

#ifndef user_long_t
#define user_long_t long
#endif
#ifndef user_siginfo_t
#define user_siginfo_t siginfo_t
#endif

/* That's for binfmt_elf_fdpic to deal with */
#ifndef elf_check_fdpic
#define elf_check_fdpic(ex) false
#endif

static int load_elf_binary(struct linux_binprm *bprm);

#ifdef CONFIG_USELIB
static int load_elf_library(struct file *);
#else
#define load_elf_library NULL
#endif

/*
 * If we don't support core dumping, then supply a NULL so we
 * don't even try.
 */
#ifdef CONFIG_ELF_CORE
static int elf_core_dump(struct coredump_params *cprm);
#else
#define elf_core_dump	NULL
#endif

#if ELF_EXEC_PAGESIZE > PAGE_SIZE
#define ELF_MIN_ALIGN	ELF_EXEC_PAGESIZE
#else
#define ELF_MIN_ALIGN	PAGE_SIZE
#endif

#ifndef ELF_CORE_EFLAGS
#define ELF_CORE_EFLAGS	0
#endif

#define ELF_PAGESTART(_v) ((_v) & ~(int)(ELF_MIN_ALIGN-1))
#define ELF_PAGEOFFSET(_v) ((_v) & (ELF_MIN_ALIGN-1))
#define ELF_PAGEALIGN(_v) (((_v) + ELF_MIN_ALIGN - 1) & ~(ELF_MIN_ALIGN - 1))

static struct linux_binfmt elf_format = {
	.module		= THIS_MODULE,
	.load_binary	= load_elf_binary,
	.load_shlib	= load_elf_library,
#ifdef CONFIG_COREDUMP
	.core_dump	= elf_core_dump,
	.min_coredump	= ELF_EXEC_PAGESIZE,
#endif
};

#define BAD_ADDR(x) (unlikely((unsigned long)(x) >= TASK_SIZE))

static int set_brk(unsigned long start, unsigned long end, int prot)
{
	start = ELF_PAGEALIGN(start);
	end = ELF_PAGEALIGN(end);
	if (end > start) {
		/*
		 * Map the last of the bss segment.
		 * If the header is requesting these pages to be
		 * executable, honour that (ppc32 needs this).
		 */
		int error = vm_brk_flags(start, end - start,
				prot & PROT_EXEC ? VM_EXEC : 0);
		if (error)
			return error;
	}
	current->mm->start_brk = current->mm->brk = end;
	return 0;
}

/* We need to explicitly zero any fractional pages
   after the data section (i.e. bss).  This would
   contain the junk from the file that should not
   be in memory
 */
static int padzero(unsigned long elf_bss)
{
	unsigned long nbyte;

	nbyte = ELF_PAGEOFFSET(elf_bss);
	if (nbyte) {
		nbyte = ELF_MIN_ALIGN - nbyte;
		if (clear_user((void __user *) elf_bss, nbyte))
			return -EFAULT;
	}
	return 0;
}

/* Let's use some macros to make this stack manipulation a little clearer */
#ifdef CONFIG_STACK_GROWSUP
#define STACK_ADD(sp, items) ((elf_addr_t __user *)(sp) + (items))
#define STACK_ROUND(sp, items) \
	((15 + (unsigned long) ((sp) + (items))) &~ 15UL)
#define STACK_ALLOC(sp, len) ({ \
	elf_addr_t __user *old_sp = (elf_addr_t __user *)sp; sp += len; \
	old_sp; })
#else
#define STACK_ADD(sp, items) ((elf_addr_t __user *)(sp) - (items))
#define STACK_ROUND(sp, items) \
	(((unsigned long) (sp - items)) &~ 15UL)
#define STACK_ALLOC(sp, len) (sp -= len)
#endif

#ifndef ELF_BASE_PLATFORM
/*
 * AT_BASE_PLATFORM indicates the "real" hardware/microarchitecture.
 * If the arch defines ELF_BASE_PLATFORM (in asm/elf.h), the value
 * will be copied to the user stack in the same manner as AT_PLATFORM.
 */
#define ELF_BASE_PLATFORM NULL
#endif

static int
create_elf_tables(struct linux_binprm *bprm, const struct elfhdr *exec,
		unsigned long interp_load_addr,
		unsigned long e_entry, unsigned long phdr_addr)
{
	struct mm_struct *mm = current->mm;
	unsigned long p = bprm->p;
	int argc = bprm->argc;
	int envc = bprm->envc;
	elf_addr_t __user *sp;
	elf_addr_t __user *u_platform;
	elf_addr_t __user *u_base_platform;
	elf_addr_t __user *u_rand_bytes;
	const char *k_platform = ELF_PLATFORM;
	const char *k_base_platform = ELF_BASE_PLATFORM;
	unsigned char k_rand_bytes[16];
	int items;
	elf_addr_t *elf_info;
	elf_addr_t flags = 0;
	int ei_index;
	const struct cred *cred = current_cred();
	struct vm_area_struct *vma;

	/*
	 * In some cases (e.g. Hyper-Threading), we want to avoid L1
	 * evictions by the processes running on the same package. One
	 * thing we can do is to shuffle the initial stack for them.
	 */

	p = arch_align_stack(p);

	/*
	 * If this architecture has a platform capability string, copy it
	 * to userspace.  In some cases (Sparc), this info is impossible
	 * for userspace to get any other way, in others (i386) it is
	 * merely difficult.
	 */
	u_platform = NULL;
	if (k_platform) {
		size_t len = strlen(k_platform) + 1;

		u_platform = (elf_addr_t __user *)STACK_ALLOC(p, len);
		if (copy_to_user(u_platform, k_platform, len))
			return -EFAULT;
	}

	/*
	 * If this architecture has a "base" platform capability
	 * string, copy it to userspace.
	 */
	u_base_platform = NULL;
	if (k_base_platform) {
		size_t len = strlen(k_base_platform) + 1;

		u_base_platform = (elf_addr_t __user *)STACK_ALLOC(p, len);
		if (copy_to_user(u_base_platform, k_base_platform, len))
			return -EFAULT;
	}

	/*
	 * Generate 16 random bytes for userspace PRNG seeding.
	 */
	get_random_bytes(k_rand_bytes, sizeof(k_rand_bytes));
	u_rand_bytes = (elf_addr_t __user *)
		       STACK_ALLOC(p, sizeof(k_rand_bytes));
	if (copy_to_user(u_rand_bytes, k_rand_bytes, sizeof(k_rand_bytes)))
		return -EFAULT;

	/* Create the ELF interpreter info */
	elf_info = (elf_addr_t *)mm->saved_auxv;
	/* update AT_VECTOR_SIZE_BASE if the number of NEW_AUX_ENT() changes */
#define NEW_AUX_ENT(id, val) \
	do { \
		*elf_info++ = id; \
		*elf_info++ = val; \
	} while (0)

#ifdef ARCH_DLINFO
	/*
	 * ARCH_DLINFO must come first so PPC can do its special alignment of
	 * AUXV.
	 * update AT_VECTOR_SIZE_ARCH if the number of NEW_AUX_ENT() in
	 * ARCH_DLINFO changes
	 */
	ARCH_DLINFO;
#endif
	NEW_AUX_ENT(AT_HWCAP, ELF_HWCAP);
	NEW_AUX_ENT(AT_PAGESZ, ELF_EXEC_PAGESIZE);
	NEW_AUX_ENT(AT_CLKTCK, CLOCKS_PER_SEC);
	NEW_AUX_ENT(AT_PHDR, phdr_addr);
	NEW_AUX_ENT(AT_PHENT, sizeof(struct elf_phdr));
	NEW_AUX_ENT(AT_PHNUM, exec->e_phnum);
	NEW_AUX_ENT(AT_BASE, interp_load_addr);
	if (bprm->interp_flags & BINPRM_FLAGS_PRESERVE_ARGV0)
		flags |= AT_FLAGS_PRESERVE_ARGV0;
	NEW_AUX_ENT(AT_FLAGS, flags);
	NEW_AUX_ENT(AT_ENTRY, e_entry);
	NEW_AUX_ENT(AT_UID, from_kuid_munged(cred->user_ns, cred->uid));
	NEW_AUX_ENT(AT_EUID, from_kuid_munged(cred->user_ns, cred->euid));
	NEW_AUX_ENT(AT_GID, from_kgid_munged(cred->user_ns, cred->gid));
	NEW_AUX_ENT(AT_EGID, from_kgid_munged(cred->user_ns, cred->egid));
	NEW_AUX_ENT(AT_SECURE, bprm->secureexec);
	NEW_AUX_ENT(AT_RANDOM, (elf_addr_t)(unsigned long)u_rand_bytes);
#ifdef ELF_HWCAP2
	NEW_AUX_ENT(AT_HWCAP2, ELF_HWCAP2);
#endif
	NEW_AUX_ENT(AT_EXECFN, bprm->exec);
	if (k_platform) {
		NEW_AUX_ENT(AT_PLATFORM,
			    (elf_addr_t)(unsigned long)u_platform);
	}
	if (k_base_platform) {
		NEW_AUX_ENT(AT_BASE_PLATFORM,
			    (elf_addr_t)(unsigned long)u_base_platform);
	}
	if (bprm->have_execfd) {
		NEW_AUX_ENT(AT_EXECFD, bprm->execfd);
	}
#undef NEW_AUX_ENT
	/* AT_NULL is zero; clear the rest too */
	memset(elf_info, 0, (char *)mm->saved_auxv +
			sizeof(mm->saved_auxv) - (char *)elf_info);

	/* And advance past the AT_NULL entry.  */
	elf_info += 2;

	ei_index = elf_info - (elf_addr_t *)mm->saved_auxv;
	sp = STACK_ADD(p, ei_index);

	items = (argc + 1) + (envc + 1) + 1;
	bprm->p = STACK_ROUND(sp, items);

	/* Point sp at the lowest address on the stack */
#ifdef CONFIG_STACK_GROWSUP
	sp = (elf_addr_t __user *)bprm->p - items - ei_index;
	bprm->exec = (unsigned long)sp; /* XXX: PARISC HACK */
#else
	sp = (elf_addr_t __user *)bprm->p;
#endif


	/*
	 * Grow the stack manually; some architectures have a limit on how
	 * far ahead a user-space access may be in order to grow the stack.
	 */
	if (mmap_read_lock_killable(mm))
		return -EINTR;
	vma = find_extend_vma(mm, bprm->p);
	mmap_read_unlock(mm);
	if (!vma)
		return -EFAULT;

	/* Now, let's put argc (and argv, envp if appropriate) on the stack */
	if (put_user(argc, sp++))
		return -EFAULT;

	/* Populate list of argv pointers back to argv strings. */
	p = mm->arg_end = mm->arg_start;
	while (argc-- > 0) {
		size_t len;
		if (put_user((elf_addr_t)p, sp++))
			return -EFAULT;
		len = strnlen_user((void __user *)p, MAX_ARG_STRLEN);
		if (!len || len > MAX_ARG_STRLEN)
			return -EINVAL;
		p += len;
	}
	if (put_user(0, sp++))
		return -EFAULT;
	mm->arg_end = p;

	/* Populate list of envp pointers back to envp strings. */
	mm->env_end = mm->env_start = p;
	while (envc-- > 0) {
		size_t len;
		if (put_user((elf_addr_t)p, sp++))
			return -EFAULT;
		len = strnlen_user((void __user *)p, MAX_ARG_STRLEN);
		if (!len || len > MAX_ARG_STRLEN)
			return -EINVAL;
		p += len;
	}
	if (put_user(0, sp++))
		return -EFAULT;
	mm->env_end = p;

	/* Put the elf_info on the stack in the right place.  */
	if (copy_to_user(sp, mm->saved_auxv, ei_index * sizeof(elf_addr_t)))
		return -EFAULT;
	return 0;
}

static unsigned long elf_map(struct file *filep, unsigned long addr,
		const struct elf_phdr *eppnt, int prot, int type,
		unsigned long total_size)
{
	unsigned long map_addr;
	unsigned long size = eppnt->p_filesz + ELF_PAGEOFFSET(eppnt->p_vaddr);
	unsigned long off = eppnt->p_offset - ELF_PAGEOFFSET(eppnt->p_vaddr);
	addr = ELF_PAGESTART(addr);
	size = ELF_PAGEALIGN(size);

	/* mmap() will return -EINVAL if given a zero size, but a
	 * segment with zero filesize is perfectly valid */
	if (!size)
		return addr;

	/*
	* total_size is the size of the ELF (interpreter) image.
	* The _first_ mmap needs to know the full size, otherwise
	* randomization might put this image into an overlapping
	* position with the ELF binary image. (since size < total_size)
	* So we first map the 'big' image - and unmap the remainder at
	* the end. (which unmap is needed for ELF images with holes.)
	*/
	if (total_size) {
		total_size = ELF_PAGEALIGN(total_size);
		map_addr = vm_mmap(filep, addr, total_size, prot, type, off);
		if (!BAD_ADDR(map_addr))
			vm_munmap(map_addr+size, total_size-size);
	} else
		map_addr = vm_mmap(filep, addr, size, prot, type, off);

	if ((type & MAP_FIXED_NOREPLACE) &&
	    PTR_ERR((void *)map_addr) == -EEXIST)
		pr_info("%d (%s): Uhuuh, elf segment at %px requested but the memory is mapped already\n",
			task_pid_nr(current), current->comm, (void *)addr);

	return(map_addr);
}

static unsigned long total_mapping_size(const struct elf_phdr *phdr, int nr)
{
	elf_addr_t min_addr = -1;
	elf_addr_t max_addr = 0;
	bool pt_load = false;
	int i;

	for (i = 0; i < nr; i++) {
		if (phdr[i].p_type == PT_LOAD) {
			min_addr = min(min_addr, ELF_PAGESTART(phdr[i].p_vaddr));
			max_addr = max(max_addr, phdr[i].p_vaddr + phdr[i].p_memsz);
			pt_load = true;
		}
	}
	return pt_load ? (max_addr - min_addr) : 0;
}

static int elf_read(struct file *file, void *buf, size_t len, loff_t pos)
{
	ssize_t rv;

	rv = kernel_read(file, buf, len, &pos);
	if (unlikely(rv != len)) {
		return (rv < 0) ? rv : -EIO;
	}
	return 0;
}

static unsigned long maximum_alignment(struct elf_phdr *cmds, int nr)
{
	unsigned long alignment = 0;
	int i;

	for (i = 0; i < nr; i++) {
		if (cmds[i].p_type == PT_LOAD) {
			unsigned long p_align = cmds[i].p_align;

			/* skip non-power of two alignments as invalid */
			if (!is_power_of_2(p_align))
				continue;
			alignment = max(alignment, p_align);
		}
	}

	/* ensure we align to at least one page */
	return ELF_PAGEALIGN(alignment);
}

/**
 * load_elf_phdrs() - load ELF program headers
 * @elf_ex:   ELF header of the binary whose program headers should be loaded
 * @elf_file: the opened ELF binary file
 *
 * Loads ELF program headers from the binary file elf_file, which has the ELF
 * header pointed to by elf_ex, into a newly allocated array. The caller is
 * responsible for freeing the allocated data. Returns NULL upon failure.
 */
static struct elf_phdr *load_elf_phdrs(const struct elfhdr *elf_ex,
				       struct file *elf_file)
{
	struct elf_phdr *elf_phdata = NULL;
	int retval = -1;
	unsigned int size;

	/*
	 * If the size of this structure has changed, then punt, since
	 * we will be doing the wrong thing.
	 */
	if (elf_ex->e_phentsize != sizeof(struct elf_phdr))
		goto out;

	/* Sanity check the number of program headers... */
	/* ...and their total size. */
	size = sizeof(struct elf_phdr) * elf_ex->e_phnum;
	if (size == 0 || size > 65536 || size > ELF_MIN_ALIGN)
		goto out;

	elf_phdata = kmalloc(size, GFP_KERNEL);
	if (!elf_phdata)
		goto out;

	/* Read in the program headers */
	retval = elf_read(elf_file, elf_phdata, size, elf_ex->e_phoff);

out:
	if (retval) {
		kfree(elf_phdata);
		elf_phdata = NULL;
	}
	return elf_phdata;
}

#ifndef CONFIG_ARCH_BINFMT_ELF_STATE

/**
 * struct arch_elf_state - arch-specific ELF loading state
 *
 * This structure is used to preserve architecture specific data during
 * the loading of an ELF file, throughout the checking of architecture
 * specific ELF headers & through to the point where the ELF load is
 * known to be proceeding (ie. SET_PERSONALITY).
 *
 * This implementation is a dummy for architectures which require no
 * specific state.
 */
struct arch_elf_state {
};

#define INIT_ARCH_ELF_STATE {}

/**
 * arch_elf_pt_proc() - check a PT_LOPROC..PT_HIPROC ELF program header
 * @ehdr:	The main ELF header
 * @phdr:	The program header to check
 * @elf:	The open ELF file
 * @is_interp:	True if the phdr is from the interpreter of the ELF being
 *		loaded, else false.
 * @state:	Architecture-specific state preserved throughout the process
 *		of loading the ELF.
 *
 * Inspects the program header phdr to validate its correctness and/or
 * suitability for the system. Called once per ELF program header in the
 * range PT_LOPROC to PT_HIPROC, for both the ELF being loaded and its
 * interpreter.
 *
 * Return: Zero to proceed with the ELF load, non-zero to fail the ELF load
 *         with that return code.
 */
static inline int arch_elf_pt_proc(struct elfhdr *ehdr,
				   struct elf_phdr *phdr,
				   struct file *elf, bool is_interp,
				   struct arch_elf_state *state)
{
	/* Dummy implementation, always proceed */
	return 0;
}

/**
 * arch_check_elf() - check an ELF executable
 * @ehdr:	The main ELF header
 * @has_interp:	True if the ELF has an interpreter, else false.
 * @interp_ehdr: The interpreter's ELF header
 * @state:	Architecture-specific state preserved throughout the process
 *		of loading the ELF.
 *
 * Provides a final opportunity for architecture code to reject the loading
 * of the ELF & cause an exec syscall to return an error. This is called after
 * all program headers to be checked by arch_elf_pt_proc have been.
 *
 * Return: Zero to proceed with the ELF load, non-zero to fail the ELF load
 *         with that return code.
 */
static inline int arch_check_elf(struct elfhdr *ehdr, bool has_interp,
				 struct elfhdr *interp_ehdr,
				 struct arch_elf_state *state)
{
	/* Dummy implementation, always proceed */
	return 0;
}

#endif /* !CONFIG_ARCH_BINFMT_ELF_STATE */

static inline int make_prot(u32 p_flags, struct arch_elf_state *arch_state,
			    bool has_interp, bool is_interp)
{
	int prot = 0;

	if (p_flags & PF_R)
		prot |= PROT_READ;
	if (p_flags & PF_W)
		prot |= PROT_WRITE;
	if (p_flags & PF_X)
		prot |= PROT_EXEC;

	return arch_elf_adjust_prot(prot, arch_state, has_interp, is_interp);
}

/* This is much more generalized than the library routine read function,
   so we keep this separate.  Technically the library read function
   is only provided so that we can read a.out libraries that have
   an ELF header */

static unsigned long load_elf_interp(struct elfhdr *interp_elf_ex,
		struct file *interpreter,
		unsigned long no_base, struct elf_phdr *interp_elf_phdata,
		struct arch_elf_state *arch_state)
{
	struct elf_phdr *eppnt;
	unsigned long load_addr = 0;
	int load_addr_set = 0;
	unsigned long last_bss = 0, elf_bss = 0;
	int bss_prot = 0;
	unsigned long error = ~0UL;
	unsigned long total_size;
	int i;

	/* First of all, some simple consistency checks */
	if (interp_elf_ex->e_type != ET_EXEC &&
	    interp_elf_ex->e_type != ET_DYN)
		goto out;
	if (!elf_check_arch(interp_elf_ex) ||
	    elf_check_fdpic(interp_elf_ex))
		goto out;
	if (!interpreter->f_op->mmap)
		goto out;

	total_size = total_mapping_size(interp_elf_phdata,
					interp_elf_ex->e_phnum);
	if (!total_size) {
		error = -EINVAL;
		goto out;
	}

	eppnt = interp_elf_phdata;
	for (i = 0; i < interp_elf_ex->e_phnum; i++, eppnt++) {
		if (eppnt->p_type == PT_LOAD) {
			int elf_type = MAP_PRIVATE;
			int elf_prot = make_prot(eppnt->p_flags, arch_state,
						 true, true);
			unsigned long vaddr = 0;
			unsigned long k, map_addr;

			vaddr = eppnt->p_vaddr;
			if (interp_elf_ex->e_type == ET_EXEC || load_addr_set)
				elf_type |= MAP_FIXED;
			else if (no_base && interp_elf_ex->e_type == ET_DYN)
				load_addr = -vaddr;

			map_addr = elf_map(interpreter, load_addr + vaddr,
					eppnt, elf_prot, elf_type, total_size);
			total_size = 0;
			error = map_addr;
			if (BAD_ADDR(map_addr))
				goto out;

			if (!load_addr_set &&
			    interp_elf_ex->e_type == ET_DYN) {
				load_addr = map_addr - ELF_PAGESTART(vaddr);
				load_addr_set = 1;
			}

			/*
			 * Check to see if the section's size will overflow the
			 * allowed task size. Note that p_filesz must always be
			 * <= p_memsize so it's only necessary to check p_memsz.
			 */
			k = load_addr + eppnt->p_vaddr;
			if (BAD_ADDR(k) ||
			    eppnt->p_filesz > eppnt->p_memsz ||
			    eppnt->p_memsz > TASK_SIZE ||
			    TASK_SIZE - eppnt->p_memsz < k) {
				error = -ENOMEM;
				goto out;
			}

			/*
			 * Find the end of the file mapping for this phdr, and
			 * keep track of the largest address we see for this.
			 */
			k = load_addr + eppnt->p_vaddr + eppnt->p_filesz;
			if (k > elf_bss)
				elf_bss = k;

			/*
			 * Do the same thing for the memory mapping - between
			 * elf_bss and last_bss is the bss section.
			 */
			k = load_addr + eppnt->p_vaddr + eppnt->p_memsz;
			if (k > last_bss) {
				last_bss = k;
				bss_prot = elf_prot;
			}
		}
	}

	/*
	 * Now fill out the bss section: first pad the last page from
	 * the file up to the page boundary, and zero it from elf_bss
	 * up to the end of the page.
	 */
	if (padzero(elf_bss)) {
		error = -EFAULT;
		goto out;
	}
	/*
	 * Next, align both the file and mem bss up to the page size,
	 * since this is where elf_bss was just zeroed up to, and where
	 * last_bss will end after the vm_brk_flags() below.
	 */
	elf_bss = ELF_PAGEALIGN(elf_bss);
	last_bss = ELF_PAGEALIGN(last_bss);
	/* Finally, if there is still more bss to allocate, do it. */
	if (last_bss > elf_bss) {
		error = vm_brk_flags(elf_bss, last_bss - elf_bss,
				bss_prot & PROT_EXEC ? VM_EXEC : 0);
		if (error)
			goto out;
	}

	error = load_addr;
out:
	return error;
}

/*
 * These are the functions used to load ELF style executables and shared
 * libraries.  There is no binary dependent code anywhere else.
 */

static int parse_elf_property(const char *data, size_t *off, size_t datasz,
			      struct arch_elf_state *arch,
			      bool have_prev_type, u32 *prev_type)
{
	size_t o, step;
	const struct gnu_property *pr;
	int ret;

	if (*off == datasz)
		return -ENOENT;

	if (WARN_ON_ONCE(*off > datasz || *off % ELF_GNU_PROPERTY_ALIGN))
		return -EIO;
	o = *off;
	datasz -= *off;

	if (datasz < sizeof(*pr))
		return -ENOEXEC;
	pr = (const struct gnu_property *)(data + o);
	o += sizeof(*pr);
	datasz -= sizeof(*pr);

	if (pr->pr_datasz > datasz)
		return -ENOEXEC;

	WARN_ON_ONCE(o % ELF_GNU_PROPERTY_ALIGN);
	step = round_up(pr->pr_datasz, ELF_GNU_PROPERTY_ALIGN);
	if (step > datasz)
		return -ENOEXEC;

	/* Properties are supposed to be unique and sorted on pr_type: */
	if (have_prev_type && pr->pr_type <= *prev_type)
		return -ENOEXEC;
	*prev_type = pr->pr_type;

	ret = arch_parse_elf_property(pr->pr_type, data + o,
				      pr->pr_datasz, ELF_COMPAT, arch);
	if (ret)
		return ret;

	*off = o + step;
	return 0;
}

#define NOTE_DATA_SZ SZ_1K
#define GNU_PROPERTY_TYPE_0_NAME "GNU"
#define NOTE_NAME_SZ (sizeof(GNU_PROPERTY_TYPE_0_NAME))

static int parse_elf_properties(struct file *f, const struct elf_phdr *phdr,
				struct arch_elf_state *arch)
{
	union {
		struct elf_note nhdr;
		char data[NOTE_DATA_SZ];
	} note;
	loff_t pos;
	ssize_t n;
	size_t off, datasz;
	int ret;
	bool have_prev_type;
	u32 prev_type;

	if (!IS_ENABLED(CONFIG_ARCH_USE_GNU_PROPERTY) || !phdr)
		return 0;

	/* load_elf_binary() shouldn't call us unless this is true... */
	if (WARN_ON_ONCE(phdr->p_type != PT_GNU_PROPERTY))
		return -ENOEXEC;

	/* If the properties are crazy large, that's too bad (for now): */
	if (phdr->p_filesz > sizeof(note))
		return -ENOEXEC;

	pos = phdr->p_offset;
	n = kernel_read(f, &note, phdr->p_filesz, &pos);

	BUILD_BUG_ON(sizeof(note) < sizeof(note.nhdr) + NOTE_NAME_SZ);
	if (n < 0 || n < sizeof(note.nhdr) + NOTE_NAME_SZ)
		return -EIO;

	if (note.nhdr.n_type != NT_GNU_PROPERTY_TYPE_0 ||
	    note.nhdr.n_namesz != NOTE_NAME_SZ ||
	    strncmp(note.data + sizeof(note.nhdr),
		    GNU_PROPERTY_TYPE_0_NAME, n - sizeof(note.nhdr)))
		return -ENOEXEC;

	off = round_up(sizeof(note.nhdr) + NOTE_NAME_SZ,
		       ELF_GNU_PROPERTY_ALIGN);
	if (off > n)
		return -ENOEXEC;

	if (note.nhdr.n_descsz > n - off)
		return -ENOEXEC;
	datasz = off + note.nhdr.n_descsz;

	have_prev_type = false;
	do {
		ret = parse_elf_property(note.data, &off, datasz, arch,
					 have_prev_type, &prev_type);
		have_prev_type = true;
	} while (!ret);

	return ret == -ENOENT ? 0 : ret;
}

static int load_elf_binary(struct linux_binprm *bprm)
{
	struct file *interpreter = NULL; /* to shut gcc up */
	unsigned long load_bias = 0, phdr_addr = 0;
	int first_pt_load = 1;
	unsigned long error;
	struct elf_phdr *elf_ppnt, *elf_phdata, *interp_elf_phdata = NULL;
	struct elf_phdr *elf_property_phdata = NULL;
	unsigned long elf_bss, elf_brk;
	int bss_prot = 0;
	int retval, i;
	unsigned long elf_entry;
	unsigned long e_entry;
	unsigned long interp_load_addr = 0;
	unsigned long start_code, end_code, start_data, end_data;
	unsigned long reloc_func_desc __maybe_unused = 0;
	int executable_stack = EXSTACK_DEFAULT;
	struct elfhdr *elf_ex = (struct elfhdr *)bprm->buf;
	struct elfhdr *interp_elf_ex = NULL;
	struct arch_elf_state arch_state = INIT_ARCH_ELF_STATE;
	struct mm_struct *mm;
	struct pt_regs *regs;

	retval = -ENOEXEC;
	/* First of all, some simple consistency checks */
	if (memcmp(elf_ex->e_ident, ELFMAG, SELFMAG) != 0)
		goto out;

	if (elf_ex->e_type != ET_EXEC && elf_ex->e_type != ET_DYN)
		goto out;
	if (!elf_check_arch(elf_ex))
		goto out;
	if (elf_check_fdpic(elf_ex))
		goto out;
	if (!bprm->file->f_op->mmap)
		goto out;

	elf_phdata = load_elf_phdrs(elf_ex, bprm->file);
	if (!elf_phdata)
		goto out;

	elf_ppnt = elf_phdata;
	for (i = 0; i < elf_ex->e_phnum; i++, elf_ppnt++) {
		char *elf_interpreter;

		if (elf_ppnt->p_type == PT_GNU_PROPERTY) {
			elf_property_phdata = elf_ppnt;
			continue;
		}

		if (elf_ppnt->p_type != PT_INTERP)
			continue;

		/*
		 * This is the program interpreter used for shared libraries -
		 * for now assume that this is an a.out format binary.
		 */
		retval = -ENOEXEC;
		if (elf_ppnt->p_filesz > PATH_MAX || elf_ppnt->p_filesz < 2)
			goto out_free_ph;

		retval = -ENOMEM;
		elf_interpreter = kmalloc(elf_ppnt->p_filesz, GFP_KERNEL);
		if (!elf_interpreter)
			goto out_free_ph;

		retval = elf_read(bprm->file, elf_interpreter, elf_ppnt->p_filesz,
				  elf_ppnt->p_offset);
		if (retval < 0)
			goto out_free_interp;
		/* make sure path is NULL terminated */
		retval = -ENOEXEC;
		if (elf_interpreter[elf_ppnt->p_filesz - 1] != '\0')
			goto out_free_interp;

		interpreter = open_exec(elf_interpreter);
		kfree(elf_interpreter);
		retval = PTR_ERR(interpreter);
		if (IS_ERR(interpreter))
			goto out_free_ph;

		/*
		 * If the binary is not readable then enforce mm->dumpable = 0
		 * regardless of the interpreter's permissions.
		 */
		would_dump(bprm, interpreter);

		interp_elf_ex = kmalloc(sizeof(*interp_elf_ex), GFP_KERNEL);
		if (!interp_elf_ex) {
			retval = -ENOMEM;
			goto out_free_file;
		}

		/* Get the exec headers */
		retval = elf_read(interpreter, interp_elf_ex,
				  sizeof(*interp_elf_ex), 0);
		if (retval < 0)
			goto out_free_dentry;

		break;

out_free_interp:
		kfree(elf_interpreter);
		goto out_free_ph;
	}

	elf_ppnt = elf_phdata;
	for (i = 0; i < elf_ex->e_phnum; i++, elf_ppnt++)
		switch (elf_ppnt->p_type) {
		case PT_GNU_STACK:
			if (elf_ppnt->p_flags & PF_X)
				executable_stack = EXSTACK_ENABLE_X;
			else
				executable_stack = EXSTACK_DISABLE_X;
			break;

		case PT_LOPROC ... PT_HIPROC:
			retval = arch_elf_pt_proc(elf_ex, elf_ppnt,
						  bprm->file, false,
						  &arch_state);
			if (retval)
				goto out_free_dentry;
			break;
		}

	/* Some simple consistency checks for the interpreter */
	if (interpreter) {
		retval = -ELIBBAD;
		/* Not an ELF interpreter */
		if (memcmp(interp_elf_ex->e_ident, ELFMAG, SELFMAG) != 0)
			goto out_free_dentry;
		/* Verify the interpreter has a valid arch */
		if (!elf_check_arch(interp_elf_ex) ||
		    elf_check_fdpic(interp_elf_ex))
			goto out_free_dentry;

		/* Load the interpreter program headers */
		interp_elf_phdata = load_elf_phdrs(interp_elf_ex,
						   interpreter);
		if (!interp_elf_phdata)
			goto out_free_dentry;

		/* Pass PT_LOPROC..PT_HIPROC headers to arch code */
		elf_property_phdata = NULL;
		elf_ppnt = interp_elf_phdata;
		for (i = 0; i < interp_elf_ex->e_phnum; i++, elf_ppnt++)
			switch (elf_ppnt->p_type) {
			case PT_GNU_PROPERTY:
				elf_property_phdata = elf_ppnt;
				break;

			case PT_LOPROC ... PT_HIPROC:
				retval = arch_elf_pt_proc(interp_elf_ex,
							  elf_ppnt, interpreter,
							  true, &arch_state);
				if (retval)
					goto out_free_dentry;
				break;
			}
	}

	retval = parse_elf_properties(interpreter ?: bprm->file,
				      elf_property_phdata, &arch_state);
	if (retval)
		goto out_free_dentry;

	/*
	 * Allow arch code to reject the ELF at this point, whilst it's
	 * still possible to return an error to the code that invoked
	 * the exec syscall.
	 */
	retval = arch_check_elf(elf_ex,
				!!interpreter, interp_elf_ex,
				&arch_state);
	if (retval)
		goto out_free_dentry;

	/* Flush all traces of the currently running executable */
	retval = begin_new_exec(bprm);
	if (retval)
		goto out_free_dentry;

	/* Do this immediately, since STACK_TOP as used in setup_arg_pages
	   may depend on the personality.  */
	SET_PERSONALITY2(*elf_ex, &arch_state);
	if (elf_read_implies_exec(*elf_ex, executable_stack))
		current->personality |= READ_IMPLIES_EXEC;

	if (!(current->personality & ADDR_NO_RANDOMIZE) && randomize_va_space)
		current->flags |= PF_RANDOMIZE;

	setup_new_exec(bprm);

	/* Do this so that we can load the interpreter, if need be.  We will
	   change some of these later */
	retval = setup_arg_pages(bprm, randomize_stack_top(STACK_TOP),
				 executable_stack);
	if (retval < 0)
		goto out_free_dentry;

	elf_bss = 0;
	elf_brk = 0;

	start_code = ~0UL;
	end_code = 0;
	start_data = 0;
	end_data = 0;

	/* Now we do a little grungy work by mmapping the ELF image into
	   the correct location in memory. */
	for(i = 0, elf_ppnt = elf_phdata;
	    i < elf_ex->e_phnum; i++, elf_ppnt++) {
		int elf_prot, elf_flags;
		unsigned long k, vaddr;
		unsigned long total_size = 0;
		unsigned long alignment;

		if (elf_ppnt->p_type != PT_LOAD)
			continue;

		if (unlikely (elf_brk > elf_bss)) {
			unsigned long nbyte;

			/* There was a PT_LOAD segment with p_memsz > p_filesz
			   before this one. Map anonymous pages, if needed,
			   and clear the area.  */
			retval = set_brk(elf_bss + load_bias,
					 elf_brk + load_bias,
					 bss_prot);
			if (retval)
				goto out_free_dentry;
			nbyte = ELF_PAGEOFFSET(elf_bss);
			if (nbyte) {
				nbyte = ELF_MIN_ALIGN - nbyte;
				if (nbyte > elf_brk - elf_bss)
					nbyte = elf_brk - elf_bss;
				if (clear_user((void __user *)elf_bss +
							load_bias, nbyte)) {
					/*
					 * This bss-zeroing can fail if the ELF
					 * file specifies odd protections. So
					 * we don't check the return value
					 */
				}
			}
		}

		elf_prot = make_prot(elf_ppnt->p_flags, &arch_state,
				     !!interpreter, false);

		elf_flags = MAP_PRIVATE;

		vaddr = elf_ppnt->p_vaddr;
		/*
		 * The first time through the loop, first_pt_load is true:
		 * layout will be calculated. Once set, use MAP_FIXED since
		 * we know we've already safely mapped the entire region with
		 * MAP_FIXED_NOREPLACE in the once-per-binary logic following.
		 */
		if (!first_pt_load) {
			elf_flags |= MAP_FIXED;
		} else if (elf_ex->e_type == ET_EXEC) {
			/*
			 * This logic is run once for the first LOAD Program
			 * Header for ET_EXEC binaries. No special handling
			 * is needed.
			 */
			elf_flags |= MAP_FIXED_NOREPLACE;
		} else if (elf_ex->e_type == ET_DYN) {
			/*
			 * This logic is run once for the first LOAD Program
			 * Header for ET_DYN binaries to calculate the
			 * randomization (load_bias) for all the LOAD
			 * Program Headers.
			 *
			 * There are effectively two types of ET_DYN
			 * binaries: programs (i.e. PIE: ET_DYN with INTERP)
			 * and loaders (ET_DYN without INTERP, since they
			 * _are_ the ELF interpreter). The loaders must
			 * be loaded away from programs since the program
			 * may otherwise collide with the loader (especially
			 * for ET_EXEC which does not have a randomized
			 * position). For example to handle invocations of
			 * "./ld.so someprog" to test out a new version of
			 * the loader, the subsequent program that the
			 * loader loads must avoid the loader itself, so
			 * they cannot share the same load range. Sufficient
			 * room for the brk must be allocated with the
			 * loader as well, since brk must be available with
			 * the loader.
			 *
			 * Therefore, programs are loaded offset from
			 * ELF_ET_DYN_BASE and loaders are loaded into the
			 * independently randomized mmap region (0 load_bias
			 * without MAP_FIXED nor MAP_FIXED_NOREPLACE).
			 */
			if (interpreter) {
				load_bias = ELF_ET_DYN_BASE;
				if (current->flags & PF_RANDOMIZE)
					load_bias += arch_mmap_rnd();
				alignment = maximum_alignment(elf_phdata, elf_ex->e_phnum);
				if (alignment)
					load_bias &= ~(alignment - 1);
				elf_flags |= MAP_FIXED_NOREPLACE;
			} else
				load_bias = 0;

			/*
			 * Since load_bias is used for all subsequent loading
			 * calculations, we must lower it by the first vaddr
			 * so that the remaining calculations based on the
			 * ELF vaddrs will be correctly offset. The result
			 * is then page aligned.
			 */
			load_bias = ELF_PAGESTART(load_bias - vaddr);

			/*
			 * Calculate the entire size of the ELF mapping
			 * (total_size), used for the initial mapping,
			 * due to load_addr_set which is set to true later
			 * once the initial mapping is performed.
			 *
			 * Note that this is only sensible when the LOAD
			 * segments are contiguous (or overlapping). If
			 * used for LOADs that are far apart, this would
			 * cause the holes between LOADs to be mapped,
			 * running the risk of having the mapping fail,
			 * as it would be larger than the ELF file itself.
			 *
			 * As a result, only ET_DYN does this, since
			 * some ET_EXEC (e.g. ia64) may have large virtual
			 * memory holes between LOADs.
			 *
			 */
			total_size = total_mapping_size(elf_phdata,
							elf_ex->e_phnum);
			if (!total_size) {
				retval = -EINVAL;
				goto out_free_dentry;
			}
		}

		error = elf_map(bprm->file, load_bias + vaddr, elf_ppnt,
				elf_prot, elf_flags, total_size);
		if (BAD_ADDR(error)) {
			retval = IS_ERR_VALUE(error) ?
				PTR_ERR((void*)error) : -EINVAL;
			goto out_free_dentry;
		}

		if (first_pt_load) {
			first_pt_load = 0;
			if (elf_ex->e_type == ET_DYN) {
				load_bias += error -
				             ELF_PAGESTART(load_bias + vaddr);
				reloc_func_desc = load_bias;
			}
		}

		/*
		 * Figure out which segment in the file contains the Program
		 * Header table, and map to the associated memory address.
		 */
		if (elf_ppnt->p_offset <= elf_ex->e_phoff &&
		    elf_ex->e_phoff < elf_ppnt->p_offset + elf_ppnt->p_filesz) {
			phdr_addr = elf_ex->e_phoff - elf_ppnt->p_offset +
				    elf_ppnt->p_vaddr;
		}

		k = elf_ppnt->p_vaddr;
		if ((elf_ppnt->p_flags & PF_X) && k < start_code)
			start_code = k;
		if (start_data < k)
			start_data = k;

		/*
		 * Check to see if the section's size will overflow the
		 * allowed task size. Note that p_filesz must always be
		 * <= p_memsz so it is only necessary to check p_memsz.
		 */
		if (BAD_ADDR(k) || elf_ppnt->p_filesz > elf_ppnt->p_memsz ||
		    elf_ppnt->p_memsz > TASK_SIZE ||
		    TASK_SIZE - elf_ppnt->p_memsz < k) {
			/* set_brk can never work. Avoid overflows. */
			retval = -EINVAL;
			goto out_free_dentry;
		}

		k = elf_ppnt->p_vaddr + elf_ppnt->p_filesz;

		if (k > elf_bss)
			elf_bss = k;
		if ((elf_ppnt->p_flags & PF_X) && end_code < k)
			end_code = k;
		if (end_data < k)
			end_data = k;
		k = elf_ppnt->p_vaddr + elf_ppnt->p_memsz;
		if (k > elf_brk) {
			bss_prot = elf_prot;
			elf_brk = k;
		}
	}

	e_entry = elf_ex->e_entry + load_bias;
	phdr_addr += load_bias;
	elf_bss += load_bias;
	elf_brk += load_bias;
	start_code += load_bias;
	end_code += load_bias;
	start_data += load_bias;
	end_data += load_bias;

	/* Calling set_brk effectively mmaps the pages that we need
	 * for the bss and break sections.  We must do this before
	 * mapping in the interpreter, to make sure it doesn't wind
	 * up getting placed where the bss needs to go.
	 */
	retval = set_brk(elf_bss, elf_brk, bss_prot);
	if (retval)
		goto out_free_dentry;
	if (likely(elf_bss != elf_brk) && unlikely(padzero(elf_bss))) {
		retval = -EFAULT; /* Nobody gets to see this, but.. */
		goto out_free_dentry;
	}

	if (interpreter) {
		elf_entry = load_elf_interp(interp_elf_ex,
					    interpreter,
					    load_bias, interp_elf_phdata,
					    &arch_state);
		if (!IS_ERR_VALUE(elf_entry)) {
			/*
			 * load_elf_interp() returns relocation
			 * adjustment
			 */
			interp_load_addr = elf_entry;
			elf_entry += interp_elf_ex->e_entry;
		}
		if (BAD_ADDR(elf_entry)) {
			retval = IS_ERR_VALUE(elf_entry) ?
					(int)elf_entry : -EINVAL;
			goto out_free_dentry;
		}
		reloc_func_desc = interp_load_addr;

		allow_write_access(interpreter);
		fput(interpreter);

		kfree(interp_elf_ex);
		kfree(interp_elf_phdata);
	} else {
		elf_entry = e_entry;
		if (BAD_ADDR(elf_entry)) {
			retval = -EINVAL;
			goto out_free_dentry;
		}
	}

	kfree(elf_phdata);

	set_binfmt(&elf_format);

#ifdef ARCH_HAS_SETUP_ADDITIONAL_PAGES
	retval = ARCH_SETUP_ADDITIONAL_PAGES(bprm, elf_ex, !!interpreter);
	if (retval < 0)
		goto out;
#endif /* ARCH_HAS_SETUP_ADDITIONAL_PAGES */

	retval = create_elf_tables(bprm, elf_ex, interp_load_addr,
				   e_entry, phdr_addr);
	if (retval < 0)
		goto out;

	mm = current->mm;
	mm->end_code = end_code;
	mm->start_code = start_code;
	mm->start_data = start_data;
	mm->end_data = end_data;
	mm->start_stack = bprm->p;

	if ((current->flags & PF_RANDOMIZE) && (randomize_va_space > 1)) {
		/*
		 * For architectures with ELF randomization, when executing
		 * a loader directly (i.e. no interpreter listed in ELF
		 * headers), move the brk area out of the mmap region
		 * (since it grows up, and may collide early with the stack
		 * growing down), and into the unused ELF_ET_DYN_BASE region.
		 */
		if (IS_ENABLED(CONFIG_ARCH_HAS_ELF_RANDOMIZE) &&
		    elf_ex->e_type == ET_DYN && !interpreter) {
			mm->brk = mm->start_brk = ELF_ET_DYN_BASE;
		}

		mm->brk = mm->start_brk = arch_randomize_brk(mm);
#ifdef compat_brk_randomized
		current->brk_randomized = 1;
#endif
	}

	if (current->personality & MMAP_PAGE_ZERO) {
		/* Why this, you ask???  Well SVr4 maps page 0 as read-only,
		   and some applications "depend" upon this behavior.
		   Since we do not have the power to recompile these, we
		   emulate the SVr4 behavior. Sigh. */
		error = vm_mmap(NULL, 0, PAGE_SIZE, PROT_READ | PROT_EXEC,
				MAP_FIXED | MAP_PRIVATE, 0);
	}

	regs = current_pt_regs();
#ifdef ELF_PLAT_INIT
	/*
	 * The ABI may specify that certain registers be set up in special
	 * ways (on i386 %edx is the address of a DT_FINI function, for
	 * example.  In addition, it may also specify (eg, PowerPC64 ELF)
	 * that the e_entry field is the address of the function descriptor
	 * for the startup routine, rather than the address of the startup
	 * routine itself.  This macro performs whatever initialization to
	 * the regs structure is required as well as any relocations to the
	 * function descriptor entries when executing dynamically links apps.
	 */
	ELF_PLAT_INIT(regs, reloc_func_desc);
#endif

	finalize_exec(bprm);
	START_THREAD(elf_ex, regs, elf_entry, bprm->p);
	retval = 0;
out:
	return retval;

	/* error cleanup */
out_free_dentry:
	kfree(interp_elf_ex);
	kfree(interp_elf_phdata);
out_free_file:
	allow_write_access(interpreter);
	if (interpreter)
		fput(interpreter);
out_free_ph:
	kfree(elf_phdata);
	goto out;
}

#ifdef CONFIG_USELIB
/* This is really simpleminded and specialized - we are loading an
   a.out library that is given an ELF header. */
static int load_elf_library(struct file *file)
{
	struct elf_phdr *elf_phdata;
	struct elf_phdr *eppnt;
	unsigned long elf_bss, bss, len;
	int retval, error, i, j;
	struct elfhdr elf_ex;

	error = -ENOEXEC;
	retval = elf_read(file, &elf_ex, sizeof(elf_ex), 0);
	if (retval < 0)
		goto out;

	if (memcmp(elf_ex.e_ident, ELFMAG, SELFMAG) != 0)
		goto out;

	/* First of all, some simple consistency checks */
	if (elf_ex.e_type != ET_EXEC || elf_ex.e_phnum > 2 ||
	    !elf_check_arch(&elf_ex) || !file->f_op->mmap)
		goto out;
	if (elf_check_fdpic(&elf_ex))
		goto out;

	/* Now read in all of the header information */

	j = sizeof(struct elf_phdr) * elf_ex.e_phnum;
	/* j < ELF_MIN_ALIGN because elf_ex.e_phnum <= 2 */

	error = -ENOMEM;
	elf_phdata = kmalloc(j, GFP_KERNEL);
	if (!elf_phdata)
		goto out;

	eppnt = elf_phdata;
	error = -ENOEXEC;
	retval = elf_read(file, eppnt, j, elf_ex.e_phoff);
	if (retval < 0)
		goto out_free_ph;

	for (j = 0, i = 0; i<elf_ex.e_phnum; i++)
		if ((eppnt + i)->p_type == PT_LOAD)
			j++;
	if (j != 1)
		goto out_free_ph;

	while (eppnt->p_type != PT_LOAD)
		eppnt++;

	/* Now use mmap to map the library into memory. */
	error = vm_mmap(file,
			ELF_PAGESTART(eppnt->p_vaddr),
			(eppnt->p_filesz +
			 ELF_PAGEOFFSET(eppnt->p_vaddr)),
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_FIXED_NOREPLACE | MAP_PRIVATE,
			(eppnt->p_offset -
			 ELF_PAGEOFFSET(eppnt->p_vaddr)));
	if (error != ELF_PAGESTART(eppnt->p_vaddr))
		goto out_free_ph;

	elf_bss = eppnt->p_vaddr + eppnt->p_filesz;
	if (padzero(elf_bss)) {
		error = -EFAULT;
		goto out_free_ph;
	}

	len = ELF_PAGEALIGN(eppnt->p_filesz + eppnt->p_vaddr);
	bss = ELF_PAGEALIGN(eppnt->p_memsz + eppnt->p_vaddr);
	if (bss > len) {
		error = vm_brk(len, bss - len);
		if (error)
			goto out_free_ph;
	}
	error = 0;

out_free_ph:
	kfree(elf_phdata);
out:
	return error;
}
#endif /* #ifdef CONFIG_USELIB */

#ifdef CONFIG_ELF_CORE
/*
 * ELF core dumper
 *
 * Modelled on fs/exec.c:aout_core_dump()
 * Jeremy Fitzhardinge <jeremy@sw.oz.au>
 */

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

static int writenote(struct memelfnote *men, struct coredump_params *cprm)
{
	struct elf_note en;
	en.n_namesz = strlen(men->name) + 1;
	en.n_descsz = men->datasz;
	en.n_type = men->type;

	return dump_emit(cprm, &en, sizeof(en)) &&
	    dump_emit(cprm, men->name, en.n_namesz) && dump_align(cprm, 4) &&
	    dump_emit(cprm, men->data, men->datasz) && dump_align(cprm, 4);
}

static void fill_elf_header(struct elfhdr *elf, int segs,
			    u16 machine, u32 flags)
{
	memset(elf, 0, sizeof(*elf));

	memcpy(elf->e_ident, ELFMAG, SELFMAG);
	elf->e_ident[EI_CLASS] = ELF_CLASS;
	elf->e_ident[EI_DATA] = ELF_DATA;
	elf->e_ident[EI_VERSION] = EV_CURRENT;
	elf->e_ident[EI_OSABI] = ELF_OSABI;

	elf->e_type = ET_CORE;
	elf->e_machine = machine;
	elf->e_version = EV_CURRENT;
	elf->e_phoff = sizeof(struct elfhdr);
	elf->e_flags = flags;
	elf->e_ehsize = sizeof(struct elfhdr);
	elf->e_phentsize = sizeof(struct elf_phdr);
	elf->e_phnum = segs;
}

static void fill_elf_note_phdr(struct elf_phdr *phdr, int sz, loff_t offset)
{
	phdr->p_type = PT_NOTE;
	phdr->p_offset = offset;
	phdr->p_vaddr = 0;
	phdr->p_paddr = 0;
	phdr->p_filesz = sz;
	phdr->p_memsz = 0;
	phdr->p_flags = 0;
	phdr->p_align = 0;
}

static void fill_note(struct memelfnote *note, const char *name, int type,
		unsigned int sz, void *data)
{
	note->name = name;
	note->type = type;
	note->datasz = sz;
	note->data = data;
}

/*
 * fill up all the fields in prstatus from the given task struct, except
 * registers which need to be filled up separately.
 */
static void fill_prstatus(struct elf_prstatus_common *prstatus,
		struct task_struct *p, long signr)
{
	prstatus->pr_info.si_signo = prstatus->pr_cursig = signr;
	prstatus->pr_sigpend = p->pending.signal.sig[0];
	prstatus->pr_sighold = p->blocked.sig[0];
	rcu_read_lock();
	prstatus->pr_ppid = task_pid_vnr(rcu_dereference(p->real_parent));
	rcu_read_unlock();
	prstatus->pr_pid = task_pid_vnr(p);
	prstatus->pr_pgrp = task_pgrp_vnr(p);
	prstatus->pr_sid = task_session_vnr(p);
	if (thread_group_leader(p)) {
		struct task_cputime cputime;

		/*
		 * This is the record for the group leader.  It shows the
		 * group-wide total, not its individual thread total.
		 */
		thread_group_cputime(p, &cputime);
		prstatus->pr_utime = ns_to_kernel_old_timeval(cputime.utime);
		prstatus->pr_stime = ns_to_kernel_old_timeval(cputime.stime);
	} else {
		u64 utime, stime;

		task_cputime(p, &utime, &stime);
		prstatus->pr_utime = ns_to_kernel_old_timeval(utime);
		prstatus->pr_stime = ns_to_kernel_old_timeval(stime);
	}

	prstatus->pr_cutime = ns_to_kernel_old_timeval(p->signal->cutime);
	prstatus->pr_cstime = ns_to_kernel_old_timeval(p->signal->cstime);
}

static int fill_psinfo(struct elf_prpsinfo *psinfo, struct task_struct *p,
		       struct mm_struct *mm)
{
	const struct cred *cred;
	unsigned int i, len;
	unsigned int state;

	/* first copy the parameters from user space */
	memset(psinfo, 0, sizeof(struct elf_prpsinfo));

	len = mm->arg_end - mm->arg_start;
	if (len >= ELF_PRARGSZ)
		len = ELF_PRARGSZ-1;
	if (copy_from_user(&psinfo->pr_psargs,
		           (const char __user *)mm->arg_start, len))
		return -EFAULT;
	for(i = 0; i < len; i++)
		if (psinfo->pr_psargs[i] == 0)
			psinfo->pr_psargs[i] = ' ';
	psinfo->pr_psargs[len] = 0;

	rcu_read_lock();
	psinfo->pr_ppid = task_pid_vnr(rcu_dereference(p->real_parent));
	rcu_read_unlock();
	psinfo->pr_pid = task_pid_vnr(p);
	psinfo->pr_pgrp = task_pgrp_vnr(p);
	psinfo->pr_sid = task_session_vnr(p);

	state = READ_ONCE(p->__state);
	i = state ? ffz(~state) + 1 : 0;
	psinfo->pr_state = i;
	psinfo->pr_sname = (i > 5) ? '.' : "RSDTZW"[i];
	psinfo->pr_zomb = psinfo->pr_sname == 'Z';
	psinfo->pr_nice = task_nice(p);
	psinfo->pr_flag = p->flags;
	rcu_read_lock();
	cred = __task_cred(p);
	SET_UID(psinfo->pr_uid, from_kuid_munged(cred->user_ns, cred->uid));
	SET_GID(psinfo->pr_gid, from_kgid_munged(cred->user_ns, cred->gid));
	rcu_read_unlock();
	get_task_comm(psinfo->pr_fname, p);

	return 0;
}

static void fill_auxv_note(struct memelfnote *note, struct mm_struct *mm)
{
	elf_addr_t *auxv = (elf_addr_t *) mm->saved_auxv;
	int i = 0;
	do
		i += 2;
	while (auxv[i - 2] != AT_NULL);
	fill_note(note, "CORE", NT_AUXV, i * sizeof(elf_addr_t), auxv);
}

static void fill_siginfo_note(struct memelfnote *note, user_siginfo_t *csigdata,
		const kernel_siginfo_t *siginfo)
{
	copy_siginfo_to_external(csigdata, siginfo);
	fill_note(note, "CORE", NT_SIGINFO, sizeof(*csigdata), csigdata);
}

#define MAX_FILE_NOTE_SIZE (4*1024*1024)
/*
 * Format of NT_FILE note:
 *
 * long count     -- how many files are mapped
 * long page_size -- units for file_ofs
 * array of [COUNT] elements of
 *   long start
 *   long end
 *   long file_ofs
 * followed by COUNT filenames in ASCII: "FILE1" NUL "FILE2" NUL...
 */
static int fill_files_note(struct memelfnote *note, struct coredump_params *cprm)
{
	unsigned count, size, names_ofs, remaining, n;
	user_long_t *data;
	user_long_t *start_end_ofs;
	char *name_base, *name_curpos;
	int i;

	/* *Estimated* file count and total data size needed */
	count = cprm->vma_count;
	if (count > UINT_MAX / 64)
		return -EINVAL;
	size = count * 64;

	names_ofs = (2 + 3 * count) * sizeof(data[0]);
 alloc:
	if (size >= MAX_FILE_NOTE_SIZE) /* paranoia check */
		return -EINVAL;
	size = round_up(size, PAGE_SIZE);
	/*
	 * "size" can be 0 here legitimately.
	 * Let it ENOMEM and omit NT_FILE section which will be empty anyway.
	 */
	data = kvmalloc(size, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(data))
		return -ENOMEM;

	start_end_ofs = data + 2;
	name_base = name_curpos = ((char *)data) + names_ofs;
	remaining = size - names_ofs;
	count = 0;
	for (i = 0; i < cprm->vma_count; i++) {
		struct core_vma_metadata *m = &cprm->vma_meta[i];
		struct file *file;
		const char *filename;

		file = m->file;
		if (!file)
			continue;
		filename = file_path(file, name_curpos, remaining);
		if (IS_ERR(filename)) {
			if (PTR_ERR(filename) == -ENAMETOOLONG) {
				kvfree(data);
				size = size * 5 / 4;
				goto alloc;
			}
			continue;
		}

		/* file_path() fills at the end, move name down */
		/* n = strlen(filename) + 1: */
		n = (name_curpos + remaining) - filename;
		remaining = filename - name_curpos;
		memmove(name_curpos, filename, n);
		name_curpos += n;

		*start_end_ofs++ = m->start;
		*start_end_ofs++ = m->end;
		*start_end_ofs++ = m->pgoff;
		count++;
	}

	/* Now we know exact count of files, can store it */
	data[0] = count;
	data[1] = PAGE_SIZE;
	/*
	 * Count usually is less than mm->map_count,
	 * we need to move filenames down.
	 */
	n = cprm->vma_count - count;
	if (n != 0) {
		unsigned shift_bytes = n * 3 * sizeof(data[0]);
		memmove(name_base - shift_bytes, name_base,
			name_curpos - name_base);
		name_curpos -= shift_bytes;
	}

	size = name_curpos - (char *)data;
	fill_note(note, "CORE", NT_FILE, size, data);
	return 0;
}

#include <linux/regset.h>

struct elf_thread_core_info {
	struct elf_thread_core_info *next;
	struct task_struct *task;
	struct elf_prstatus prstatus;
	struct memelfnote notes[];
};

struct elf_note_info {
	struct elf_thread_core_info *thread;
	struct memelfnote psinfo;
	struct memelfnote signote;
	struct memelfnote auxv;
	struct memelfnote files;
	user_siginfo_t csigdata;
	size_t size;
	int thread_notes;
};

#ifdef CORE_DUMP_USE_REGSET
/*
 * When a regset has a writeback hook, we call it on each thread before
 * dumping user memory.  On register window machines, this makes sure the
 * user memory backing the register data is up to date before we read it.
 */
static void do_thread_regset_writeback(struct task_struct *task,
				       const struct user_regset *regset)
{
	if (regset->writeback)
		regset->writeback(task, regset, 1);
}

#ifndef PRSTATUS_SIZE
#define PRSTATUS_SIZE sizeof(struct elf_prstatus)
#endif

#ifndef SET_PR_FPVALID
#define SET_PR_FPVALID(S) ((S)->pr_fpvalid = 1)
#endif

static int fill_thread_core_info(struct elf_thread_core_info *t,
				 const struct user_regset_view *view,
				 long signr, struct elf_note_info *info)
{
	unsigned int note_iter, view_iter;

	/*
	 * NT_PRSTATUS is the one special case, because the regset data
	 * goes into the pr_reg field inside the note contents, rather
	 * than being the whole note contents.  We fill the reset in here.
	 * We assume that regset 0 is NT_PRSTATUS.
	 */
	fill_prstatus(&t->prstatus.common, t->task, signr);
	regset_get(t->task, &view->regsets[0],
		   sizeof(t->prstatus.pr_reg), &t->prstatus.pr_reg);

	fill_note(&t->notes[0], "CORE", NT_PRSTATUS,
		  PRSTATUS_SIZE, &t->prstatus);
	info->size += notesize(&t->notes[0]);

	do_thread_regset_writeback(t->task, &view->regsets[0]);

	/*
	 * Each other regset might generate a note too.  For each regset
	 * that has no core_note_type or is inactive, skip it.
	 */
	note_iter = 1;
	for (view_iter = 1; view_iter < view->n; ++view_iter) {
		const struct user_regset *regset = &view->regsets[view_iter];
		int note_type = regset->core_note_type;
		bool is_fpreg = note_type == NT_PRFPREG;
		void *data;
		int ret;

		do_thread_regset_writeback(t->task, regset);
		if (!note_type) // not for coredumps
			continue;
		if (regset->active && regset->active(t->task, regset) <= 0)
			continue;

		ret = regset_get_alloc(t->task, regset, ~0U, &data);
		if (ret < 0)
			continue;

		if (WARN_ON_ONCE(note_iter >= info->thread_notes))
			break;

		if (is_fpreg)
			SET_PR_FPVALID(&t->prstatus);

		fill_note(&t->notes[note_iter], is_fpreg ? "CORE" : "LINUX",
			  note_type, ret, data);

		info->size += notesize(&t->notes[note_iter]);
		note_iter++;
	}

	return 1;
}
#else
static int fill_thread_core_info(struct elf_thread_core_info *t,
				 const struct user_regset_view *view,
				 long signr, struct elf_note_info *info)
{
	struct task_struct *p = t->task;
	elf_fpregset_t *fpu;

	fill_prstatus(&t->prstatus.common, p, signr);
	elf_core_copy_task_regs(p, &t->prstatus.pr_reg);

	fill_note(&t->notes[0], "CORE", NT_PRSTATUS, sizeof(t->prstatus),
		  &(t->prstatus));
	info->size += notesize(&t->notes[0]);

	fpu = kzalloc(sizeof(elf_fpregset_t), GFP_KERNEL);
	if (!fpu || !elf_core_copy_task_fpregs(p, fpu)) {
		kfree(fpu);
		return 1;
	}

	t->prstatus.pr_fpvalid = 1;
	fill_note(&t->notes[1], "CORE", NT_PRFPREG, sizeof(*fpu), fpu);
	info->size += notesize(&t->notes[1]);

	return 1;
}
#endif

static int fill_note_info(struct elfhdr *elf, int phdrs,
			  struct elf_note_info *info,
			  struct coredump_params *cprm)
{
	struct task_struct *dump_task = current;
	const struct user_regset_view *view;
	struct elf_thread_core_info *t;
	struct elf_prpsinfo *psinfo;
	struct core_thread *ct;

	psinfo = kmalloc(sizeof(*psinfo), GFP_KERNEL);
	if (!psinfo)
		return 0;
	fill_note(&info->psinfo, "CORE", NT_PRPSINFO, sizeof(*psinfo), psinfo);

#ifdef CORE_DUMP_USE_REGSET
	view = task_user_regset_view(dump_task);

	/*
	 * Figure out how many notes we're going to need for each thread.
	 */
	info->thread_notes = 0;
	for (int i = 0; i < view->n; ++i)
		if (view->regsets[i].core_note_type != 0)
			++info->thread_notes;

	/*
	 * Sanity check.  We rely on regset 0 being in NT_PRSTATUS,
	 * since it is our one special case.
	 */
	if (unlikely(info->thread_notes == 0) ||
	    unlikely(view->regsets[0].core_note_type != NT_PRSTATUS)) {
		WARN_ON(1);
		return 0;
	}

	/*
	 * Initialize the ELF file header.
	 */
	fill_elf_header(elf, phdrs,
			view->e_machine, view->e_flags);
#else
	view = NULL;
	info->thread_notes = 2;
	fill_elf_header(elf, phdrs, ELF_ARCH, ELF_CORE_EFLAGS);
#endif

	/*
	 * Allocate a structure for each thread.
	 */
	info->thread = kzalloc(offsetof(struct elf_thread_core_info,
				     notes[info->thread_notes]),
			    GFP_KERNEL);
	if (unlikely(!info->thread))
		return 0;

	info->thread->task = dump_task;
	for (ct = dump_task->signal->core_state->dumper.next; ct; ct = ct->next) {
		t = kzalloc(offsetof(struct elf_thread_core_info,
				     notes[info->thread_notes]),
			    GFP_KERNEL);
		if (unlikely(!t))
			return 0;

		t->task = ct->task;
		t->next = info->thread->next;
		info->thread->next = t;
	}

	/*
	 * Now fill in each thread's information.
	 */
	for (t = info->thread; t != NULL; t = t->next)
		if (!fill_thread_core_info(t, view, cprm->siginfo->si_signo, info))
			return 0;

	/*
	 * Fill in the two process-wide notes.
	 */
	fill_psinfo(psinfo, dump_task->group_leader, dump_task->mm);
	info->size += notesize(&info->psinfo);

	fill_siginfo_note(&info->signote, &info->csigdata, cprm->siginfo);
	info->size += notesize(&info->signote);

	fill_auxv_note(&info->auxv, current->mm);
	info->size += notesize(&info->auxv);

	if (fill_files_note(&info->files, cprm) == 0)
		info->size += notesize(&info->files);

	return 1;
}

/*
 * Write all the notes for each thread.  When writing the first thread, the
 * process-wide notes are interleaved after the first thread-specific note.
 */
static int write_note_info(struct elf_note_info *info,
			   struct coredump_params *cprm)
{
	bool first = true;
	struct elf_thread_core_info *t = info->thread;

	do {
		int i;

		if (!writenote(&t->notes[0], cprm))
			return 0;

		if (first && !writenote(&info->psinfo, cprm))
			return 0;
		if (first && !writenote(&info->signote, cprm))
			return 0;
		if (first && !writenote(&info->auxv, cprm))
			return 0;
		if (first && info->files.data &&
				!writenote(&info->files, cprm))
			return 0;

		for (i = 1; i < info->thread_notes; ++i)
			if (t->notes[i].data &&
			    !writenote(&t->notes[i], cprm))
				return 0;

		first = false;
		t = t->next;
	} while (t);

	return 1;
}

static void free_note_info(struct elf_note_info *info)
{
	struct elf_thread_core_info *threads = info->thread;
	while (threads) {
		unsigned int i;
		struct elf_thread_core_info *t = threads;
		threads = t->next;
		WARN_ON(t->notes[0].data && t->notes[0].data != &t->prstatus);
		for (i = 1; i < info->thread_notes; ++i)
			kfree(t->notes[i].data);
		kfree(t);
	}
	kfree(info->psinfo.data);
	kvfree(info->files.data);
}

static void fill_extnum_info(struct elfhdr *elf, struct elf_shdr *shdr4extnum,
			     elf_addr_t e_shoff, int segs)
{
	elf->e_shoff = e_shoff;
	elf->e_shentsize = sizeof(*shdr4extnum);
	elf->e_shnum = 1;
	elf->e_shstrndx = SHN_UNDEF;

	memset(shdr4extnum, 0, sizeof(*shdr4extnum));

	shdr4extnum->sh_type = SHT_NULL;
	shdr4extnum->sh_size = elf->e_shnum;
	shdr4extnum->sh_link = elf->e_shstrndx;
	shdr4extnum->sh_info = segs;
}

/*
 * Actual dumper
 *
 * This is a two-pass process; first we find the offsets of the bits,
 * and then they are actually written out.  If we run out of core limit
 * we just truncate.
 */
static int elf_core_dump(struct coredump_params *cprm)
{
	int has_dumped = 0;
	int segs, i;
	struct elfhdr elf;
	loff_t offset = 0, dataoff;
	struct elf_note_info info = { };
	struct elf_phdr *phdr4note = NULL;
	struct elf_shdr *shdr4extnum = NULL;
	Elf_Half e_phnum;
	elf_addr_t e_shoff;

	/*
	 * The number of segs are recored into ELF header as 16bit value.
	 * Please check DEFAULT_MAX_MAP_COUNT definition when you modify here.
	 */
	segs = cprm->vma_count + elf_core_extra_phdrs(cprm);

	/* for notes section */
	segs++;

	/* If segs > PN_XNUM(0xffff), then e_phnum overflows. To avoid
	 * this, kernel supports extended numbering. Have a look at
	 * include/linux/elf.h for further information. */
	e_phnum = segs > PN_XNUM ? PN_XNUM : segs;

	/*
	 * Collect all the non-memory information about the process for the
	 * notes.  This also sets up the file header.
	 */
	if (!fill_note_info(&elf, e_phnum, &info, cprm))
		goto end_coredump;

	has_dumped = 1;

	offset += sizeof(elf);				/* Elf header */
	offset += segs * sizeof(struct elf_phdr);	/* Program headers */

	/* Write notes phdr entry */
	{
		size_t sz = info.size;

		/* For cell spufs */
		sz += elf_coredump_extra_notes_size();

		phdr4note = kmalloc(sizeof(*phdr4note), GFP_KERNEL);
		if (!phdr4note)
			goto end_coredump;

		fill_elf_note_phdr(phdr4note, sz, offset);
		offset += sz;
	}

	dataoff = offset = roundup(offset, ELF_EXEC_PAGESIZE);

	offset += cprm->vma_data_size;
	offset += elf_core_extra_data_size(cprm);
	e_shoff = offset;

	if (e_phnum == PN_XNUM) {
		shdr4extnum = kmalloc(sizeof(*shdr4extnum), GFP_KERNEL);
		if (!shdr4extnum)
			goto end_coredump;
		fill_extnum_info(&elf, shdr4extnum, e_shoff, segs);
	}

	offset = dataoff;

	if (!dump_emit(cprm, &elf, sizeof(elf)))
		goto end_coredump;

	if (!dump_emit(cprm, phdr4note, sizeof(*phdr4note)))
		goto end_coredump;

	/* Write program headers for segments dump */
	for (i = 0; i < cprm->vma_count; i++) {
		struct core_vma_metadata *meta = cprm->vma_meta + i;
		struct elf_phdr phdr;

		phdr.p_type = PT_LOAD;
		phdr.p_offset = offset;
		phdr.p_vaddr = meta->start;
		phdr.p_paddr = 0;
		phdr.p_filesz = meta->dump_size;
		phdr.p_memsz = meta->end - meta->start;
		offset += phdr.p_filesz;
		phdr.p_flags = 0;
		if (meta->flags & VM_READ)
			phdr.p_flags |= PF_R;
		if (meta->flags & VM_WRITE)
			phdr.p_flags |= PF_W;
		if (meta->flags & VM_EXEC)
			phdr.p_flags |= PF_X;
		phdr.p_align = ELF_EXEC_PAGESIZE;

		if (!dump_emit(cprm, &phdr, sizeof(phdr)))
			goto end_coredump;
	}

	if (!elf_core_write_extra_phdrs(cprm, offset))
		goto end_coredump;

	/* write out the notes section */
	if (!write_note_info(&info, cprm))
		goto end_coredump;

	/* For cell spufs */
	if (elf_coredump_extra_notes_write(cprm))
		goto end_coredump;

	/* Align to page */
	dump_skip_to(cprm, dataoff);

	for (i = 0; i < cprm->vma_count; i++) {
		struct core_vma_metadata *meta = cprm->vma_meta + i;

		if (!dump_user_range(cprm, meta->start, meta->dump_size))
			goto end_coredump;
	}

	if (!elf_core_write_extra_data(cprm))
		goto end_coredump;

	if (e_phnum == PN_XNUM) {
		if (!dump_emit(cprm, shdr4extnum, sizeof(*shdr4extnum)))
			goto end_coredump;
	}

end_coredump:
	free_note_info(&info);
	kfree(shdr4extnum);
	kfree(phdr4note);
	return has_dumped;
}

#endif		/* CONFIG_ELF_CORE */

static int __init init_elf_binfmt(void)
{
	register_binfmt(&elf_format);
	return 0;
}

static void __exit exit_elf_binfmt(void)
{
	/* Remove the COFF and ELF loaders. */
	unregister_binfmt(&elf_format);
}

core_initcall(init_elf_binfmt);
module_exit(exit_elf_binfmt);
MODULE_LICENSE("GPL");

#ifdef CONFIG_BINFMT_ELF_KUNIT_TEST
#include "binfmt_elf_test.c"
#endif
