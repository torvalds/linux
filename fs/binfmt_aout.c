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
#include <linux/slab.h>
#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>

static int load_aout_binary(struct linux_binprm *, struct pt_regs * regs);
static int load_aout_library(struct file*);
static int aout_core_dump(long signr, struct pt_regs *regs, struct file *file, unsigned long limit);

static struct linux_binfmt aout_format = {
	.module		= THIS_MODULE,
	.load_binary	= load_aout_binary,
	.load_shlib	= load_aout_library,
	.core_dump	= aout_core_dump,
	.min_coredump	= PAGE_SIZE
};

#define BAD_ADDR(x)	((unsigned long)(x) >= TASK_SIZE)

static int set_brk(unsigned long start, unsigned long end)
{
	start = PAGE_ALIGN(start);
	end = PAGE_ALIGN(end);
	if (end > start) {
		unsigned long addr;
		down_write(&current->mm->mmap_sem);
		addr = do_brk(start, end - start);
		up_write(&current->mm->mmap_sem);
		if (BAD_ADDR(addr))
			return addr;
	}
	return 0;
}

/*
 * These are the only things you should do on a core-file: use only these
 * macros to write out all the necessary info.
 */

static int dump_write(struct file *file, const void *addr, int nr)
{
	return file->f_op->write(file, addr, nr, &file->f_pos) == nr;
}

#define DUMP_WRITE(addr, nr)	\
	if (!dump_write(file, (void *)(addr), (nr))) \
		goto end_coredump;

#define DUMP_SEEK(offset) \
if (file->f_op->llseek) { \
	if (file->f_op->llseek(file,(offset),0) != (offset)) \
 		goto end_coredump; \
} else file->f_pos = (offset)

/*
 * Routine writes a core dump image in the current directory.
 * Currently only a stub-function.
 *
 * Note that setuid/setgid files won't make a core-dump if the uid/gid
 * changed due to the set[u|g]id. It's enforced by the "current->mm->dumpable"
 * field, which also makes sure the core-dumps won't be recursive if the
 * dumping of the process results in another error..
 */

static int aout_core_dump(long signr, struct pt_regs *regs, struct file *file, unsigned long limit)
{
	mm_segment_t fs;
	int has_dumped = 0;
	unsigned long dump_start, dump_size;
	struct user dump;
#if defined(__alpha__)
#       define START_DATA(u)	(u.start_data)
#elif defined(__arm__)
#	define START_DATA(u)	((u.u_tsize << PAGE_SHIFT) + u.start_code)
#elif defined(__sparc__)
#       define START_DATA(u)    (u.u_tsize)
#elif defined(__i386__) || defined(__mc68000__) || defined(__arch_um__)
#       define START_DATA(u)	(u.u_tsize << PAGE_SHIFT)
#endif
#ifdef __sparc__
#       define START_STACK(u)   ((regs->u_regs[UREG_FP]) & ~(PAGE_SIZE - 1))
#else
#       define START_STACK(u)   (u.start_stack)
#endif

	fs = get_fs();
	set_fs(KERNEL_DS);
	has_dumped = 1;
	current->flags |= PF_DUMPCORE;
       	strncpy(dump.u_comm, current->comm, sizeof(dump.u_comm));
#ifndef __sparc__
	dump.u_ar0 = offsetof(struct user, regs);
#endif
	dump.signal = signr;
	dump_thread(regs, &dump);

/* If the size of the dump file exceeds the rlimit, then see what would happen
   if we wrote the stack, but not the data area.  */
#ifdef __sparc__
	if ((dump.u_dsize + dump.u_ssize) > limit)
		dump.u_dsize = 0;
#else
	if ((dump.u_dsize + dump.u_ssize+1) * PAGE_SIZE > limit)
		dump.u_dsize = 0;
#endif

/* Make sure we have enough room to write the stack and data areas. */
#ifdef __sparc__
	if (dump.u_ssize > limit)
		dump.u_ssize = 0;
#else
	if ((dump.u_ssize + 1) * PAGE_SIZE > limit)
		dump.u_ssize = 0;
#endif

/* make sure we actually have a data and stack area to dump */
	set_fs(USER_DS);
#ifdef __sparc__
	if (!access_ok(VERIFY_READ, (void __user *)START_DATA(dump), dump.u_dsize))
		dump.u_dsize = 0;
	if (!access_ok(VERIFY_READ, (void __user *)START_STACK(dump), dump.u_ssize))
		dump.u_ssize = 0;
#else
	if (!access_ok(VERIFY_READ, (void __user *)START_DATA(dump), dump.u_dsize << PAGE_SHIFT))
		dump.u_dsize = 0;
	if (!access_ok(VERIFY_READ, (void __user *)START_STACK(dump), dump.u_ssize << PAGE_SHIFT))
		dump.u_ssize = 0;
#endif

	set_fs(KERNEL_DS);
/* struct user */
	DUMP_WRITE(&dump,sizeof(dump));
/* Now dump all of the user data.  Include malloced stuff as well */
#ifndef __sparc__
	DUMP_SEEK(PAGE_SIZE);
#endif
/* now we start writing out the user space info */
	set_fs(USER_DS);
/* Dump the data area */
	if (dump.u_dsize != 0) {
		dump_start = START_DATA(dump);
#ifdef __sparc__
		dump_size = dump.u_dsize;
#else
		dump_size = dump.u_dsize << PAGE_SHIFT;
#endif
		DUMP_WRITE(dump_start,dump_size);
	}
/* Now prepare to dump the stack area */
	if (dump.u_ssize != 0) {
		dump_start = START_STACK(dump);
#ifdef __sparc__
		dump_size = dump.u_ssize;
#else
		dump_size = dump.u_ssize << PAGE_SHIFT;
#endif
		DUMP_WRITE(dump_start,dump_size);
	}
/* Finally dump the task struct.  Not be used by gdb, but could be useful */
	set_fs(KERNEL_DS);
	DUMP_WRITE(current,sizeof(*current));
end_coredump:
	set_fs(fs);
	return has_dumped;
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
#ifdef __sparc__
	/* This imposes the proper stack alignment for a new process. */
	sp = (void __user *) (((unsigned long) sp) & ~7);
	if ((envc+argc+3)&1) --sp;
#endif
#ifdef __alpha__
/* whee.. test-programs are so much fun. */
	put_user(0, --sp);
	put_user(0, --sp);
	if (bprm->loader) {
		put_user(0, --sp);
		put_user(0x3eb, --sp);
		put_user(bprm->loader, --sp);
		put_user(0x3ea, --sp);
	}
	put_user(bprm->exec, --sp);
	put_user(0x3e9, --sp);
#endif
	sp -= envc+1;
	envp = (char __user * __user *) sp;
	sp -= argc+1;
	argv = (char __user * __user *) sp;
#if defined(__i386__) || defined(__mc68000__) || defined(__arm__) || defined(__arch_um__)
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

static int load_aout_binary(struct linux_binprm * bprm, struct pt_regs * regs)
{
	struct exec ex;
	unsigned long error;
	unsigned long fd_offset;
	unsigned long rlim;
	int retval;

	ex = *((struct exec *) bprm->buf);		/* exec-header */
	if ((N_MAGIC(ex) != ZMAGIC && N_MAGIC(ex) != OMAGIC &&
	     N_MAGIC(ex) != QMAGIC && N_MAGIC(ex) != NMAGIC) ||
	    N_TRSIZE(ex) || N_DRSIZE(ex) ||
	    i_size_read(bprm->file->f_path.dentry->d_inode) < ex.a_text+ex.a_data+N_SYMSIZE(ex)+N_TXTOFF(ex)) {
		return -ENOEXEC;
	}

	/*
	 * Requires a mmap handler. This prevents people from using a.out
	 * as part of an exploit attack against /proc-related vulnerabilities.
	 */
	if (!bprm->file->f_op || !bprm->file->f_op->mmap)
		return -ENOEXEC;

	fd_offset = N_TXTOFF(ex);

	/* Check initial limits. This avoids letting people circumvent
	 * size limits imposed on them by creating programs with large
	 * arrays in the data or bss.
	 */
	rlim = current->signal->rlim[RLIMIT_DATA].rlim_cur;
	if (rlim >= RLIM_INFINITY)
		rlim = ~0;
	if (ex.a_data + ex.a_bss > rlim)
		return -ENOMEM;

	/* Flush all traces of the currently running executable */
	retval = flush_old_exec(bprm);
	if (retval)
		return retval;

	/* OK, This is the point of no return */
#if defined(__alpha__)
	SET_AOUT_PERSONALITY(bprm, ex);
#elif defined(__sparc__)
	set_personality(PER_SUNOS);
#if !defined(__sparc_v9__)
	memcpy(&current->thread.core_exec, &ex, sizeof(struct exec));
#endif
#else
	set_personality(PER_LINUX);
#endif

	current->mm->end_code = ex.a_text +
		(current->mm->start_code = N_TXTADDR(ex));
	current->mm->end_data = ex.a_data +
		(current->mm->start_data = N_DATADDR(ex));
	current->mm->brk = ex.a_bss +
		(current->mm->start_brk = N_BSSADDR(ex));
	current->mm->free_area_cache = current->mm->mmap_base;
	current->mm->cached_hole_size = 0;

	compute_creds(bprm);
 	current->flags &= ~PF_FORKNOEXEC;
#ifdef __sparc__
	if (N_MAGIC(ex) == NMAGIC) {
		loff_t pos = fd_offset;
		/* Fuck me plenty... */
		/* <AOL></AOL> */
		down_write(&current->mm->mmap_sem);	
		error = do_brk(N_TXTADDR(ex), ex.a_text);
		up_write(&current->mm->mmap_sem);
		bprm->file->f_op->read(bprm->file, (char *) N_TXTADDR(ex),
			  ex.a_text, &pos);
		down_write(&current->mm->mmap_sem);
		error = do_brk(N_DATADDR(ex), ex.a_data);
		up_write(&current->mm->mmap_sem);
		bprm->file->f_op->read(bprm->file, (char *) N_DATADDR(ex),
			  ex.a_data, &pos);
		goto beyond_if;
	}
#endif

	if (N_MAGIC(ex) == OMAGIC) {
		unsigned long text_addr, map_size;
		loff_t pos;

		text_addr = N_TXTADDR(ex);

#if defined(__alpha__) || defined(__sparc__)
		pos = fd_offset;
		map_size = ex.a_text+ex.a_data + PAGE_SIZE - 1;
#else
		pos = 32;
		map_size = ex.a_text+ex.a_data;
#endif
		down_write(&current->mm->mmap_sem);
		error = do_brk(text_addr & PAGE_MASK, map_size);
		up_write(&current->mm->mmap_sem);
		if (error != (text_addr & PAGE_MASK)) {
			send_sig(SIGKILL, current, 0);
			return error;
		}

		error = bprm->file->f_op->read(bprm->file,
			  (char __user *)text_addr,
			  ex.a_text+ex.a_data, &pos);
		if ((signed long)error < 0) {
			send_sig(SIGKILL, current, 0);
			return error;
		}
			 
		flush_icache_range(text_addr, text_addr+ex.a_text+ex.a_data);
	} else {
		static unsigned long error_time, error_time2;
		if ((ex.a_text & 0xfff || ex.a_data & 0xfff) &&
		    (N_MAGIC(ex) != NMAGIC) && (jiffies-error_time2) > 5*HZ)
		{
			printk(KERN_NOTICE "executable not page aligned\n");
			error_time2 = jiffies;
		}

		if ((fd_offset & ~PAGE_MASK) != 0 &&
		    (jiffies-error_time) > 5*HZ)
		{
			printk(KERN_WARNING 
			       "fd_offset is not page aligned. Please convert program: %s\n",
			       bprm->file->f_path.dentry->d_name.name);
			error_time = jiffies;
		}

		if (!bprm->file->f_op->mmap||((fd_offset & ~PAGE_MASK) != 0)) {
			loff_t pos = fd_offset;
			down_write(&current->mm->mmap_sem);
			do_brk(N_TXTADDR(ex), ex.a_text+ex.a_data);
			up_write(&current->mm->mmap_sem);
			bprm->file->f_op->read(bprm->file,
					(char __user *)N_TXTADDR(ex),
					ex.a_text+ex.a_data, &pos);
			flush_icache_range((unsigned long) N_TXTADDR(ex),
					   (unsigned long) N_TXTADDR(ex) +
					   ex.a_text+ex.a_data);
			goto beyond_if;
		}

		down_write(&current->mm->mmap_sem);
		error = do_mmap(bprm->file, N_TXTADDR(ex), ex.a_text,
			PROT_READ | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_EXECUTABLE,
			fd_offset);
		up_write(&current->mm->mmap_sem);

		if (error != N_TXTADDR(ex)) {
			send_sig(SIGKILL, current, 0);
			return error;
		}

		down_write(&current->mm->mmap_sem);
 		error = do_mmap(bprm->file, N_DATADDR(ex), ex.a_data,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_EXECUTABLE,
				fd_offset + ex.a_text);
		up_write(&current->mm->mmap_sem);
		if (error != N_DATADDR(ex)) {
			send_sig(SIGKILL, current, 0);
			return error;
		}
	}
beyond_if:
	set_binfmt(&aout_format);

	retval = set_brk(current->mm->start_brk, current->mm->brk);
	if (retval < 0) {
		send_sig(SIGKILL, current, 0);
		return retval;
	}

	retval = setup_arg_pages(bprm, STACK_TOP, EXSTACK_DEFAULT);
	if (retval < 0) { 
		/* Someone check-me: is this error path enough? */ 
		send_sig(SIGKILL, current, 0); 
		return retval;
	}

	current->mm->start_stack =
		(unsigned long) create_aout_tables((char __user *) bprm->p, bprm);
#ifdef __alpha__
	regs->gp = ex.a_gpvalue;
#endif
	start_thread(regs, ex.a_entry, current->mm->start_stack);
	if (unlikely(current->ptrace & PT_PTRACED)) {
		if (current->ptrace & PT_TRACE_EXEC)
			ptrace_notify ((PTRACE_EVENT_EXEC << 8) | SIGTRAP);
		else
			send_sig(SIGTRAP, current, 0);
	}
	return 0;
}

static int load_aout_library(struct file *file)
{
	struct inode * inode;
	unsigned long bss, start_addr, len;
	unsigned long error;
	int retval;
	struct exec ex;

	inode = file->f_path.dentry->d_inode;

	retval = -ENOEXEC;
	error = kernel_read(file, 0, (char *) &ex, sizeof(ex));
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
	if (!file->f_op || !file->f_op->mmap)
		goto out;

	if (N_FLAGS(ex))
		goto out;

	/* For  QMAGIC, the starting address is 0x20 into the page.  We mask
	   this off to get the starting address for the page */

	start_addr =  ex.a_entry & 0xfffff000;

	if ((N_TXTOFF(ex) & ~PAGE_MASK) != 0) {
		static unsigned long error_time;
		loff_t pos = N_TXTOFF(ex);

		if ((jiffies-error_time) > 5*HZ)
		{
			printk(KERN_WARNING 
			       "N_TXTOFF is not page aligned. Please convert library: %s\n",
			       file->f_path.dentry->d_name.name);
			error_time = jiffies;
		}
		down_write(&current->mm->mmap_sem);
		do_brk(start_addr, ex.a_text + ex.a_data + ex.a_bss);
		up_write(&current->mm->mmap_sem);
		
		file->f_op->read(file, (char __user *)start_addr,
			ex.a_text + ex.a_data, &pos);
		flush_icache_range((unsigned long) start_addr,
				   (unsigned long) start_addr + ex.a_text + ex.a_data);

		retval = 0;
		goto out;
	}
	/* Now use mmap to map the library into memory. */
	down_write(&current->mm->mmap_sem);
	error = do_mmap(file, start_addr, ex.a_text + ex.a_data,
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE,
			N_TXTOFF(ex));
	up_write(&current->mm->mmap_sem);
	retval = error;
	if (error != start_addr)
		goto out;

	len = PAGE_ALIGN(ex.a_text + ex.a_data);
	bss = ex.a_text + ex.a_data + ex.a_bss;
	if (bss > len) {
		down_write(&current->mm->mmap_sem);
		error = do_brk(start_addr + len, bss - len);
		up_write(&current->mm->mmap_sem);
		retval = error;
		if (error != start_addr + len)
			goto out;
	}
	retval = 0;
out:
	return retval;
}

static int __init init_aout_binfmt(void)
{
	return register_binfmt(&aout_format);
}

static void __exit exit_aout_binfmt(void)
{
	unregister_binfmt(&aout_format);
}

core_initcall(init_aout_binfmt);
module_exit(exit_aout_binfmt);
MODULE_LICENSE("GPL");
