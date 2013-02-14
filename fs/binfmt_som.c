/*
 * linux/fs/binfmt_som.c
 *
 * These are the functions used to load SOM format executables as used
 * by HP-UX.  
 *
 * Copyright 1999 Matthew Wilcox <willy@bofh.ai>
 * based on binfmt_elf which is
 * Copyright 1993, 1994: Eric Youngdale (ericy@cais.com).
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
#include <linux/som.h>
#include <linux/string.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/shm.h>
#include <linux/personality.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>


#include <linux/elf.h>

static int load_som_binary(struct linux_binprm * bprm);
static int load_som_library(struct file *);

/*
 * If we don't support core dumping, then supply a NULL so we
 * don't even try.
 */
#if 0
static int som_core_dump(struct coredump_params *cprm);
#else
#define som_core_dump	NULL
#endif

#define SOM_PAGESTART(_v) ((_v) & ~(unsigned long)(SOM_PAGESIZE-1))
#define SOM_PAGEOFFSET(_v) ((_v) & (SOM_PAGESIZE-1))
#define SOM_PAGEALIGN(_v) (((_v) + SOM_PAGESIZE - 1) & ~(SOM_PAGESIZE - 1))

static struct linux_binfmt som_format = {
	.module		= THIS_MODULE,
	.load_binary	= load_som_binary,
	.load_shlib	= load_som_library,
	.core_dump	= som_core_dump,
	.min_coredump	= SOM_PAGESIZE
};

/*
 * create_som_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */
static void create_som_tables(struct linux_binprm *bprm)
{
	char **argv, **envp;
	int argc = bprm->argc;
	int envc = bprm->envc;
	unsigned long p;
	unsigned long *sp;

	/* Word-align the stack pointer */
	sp = (unsigned long *)((bprm->p + 3) & ~3);

	envp = (char **) sp;
	sp += envc + 1;
	argv = (char **) sp;
	sp += argc + 1;

	__put_user((unsigned long) envp,++sp);
	__put_user((unsigned long) argv,++sp);

	__put_user(argc, ++sp);

	bprm->p = (unsigned long) sp;

	p = current->mm->arg_start;
	while (argc-- > 0) {
		__put_user((char *)p,argv++);
		p += strlen_user((char *)p);
	}
	__put_user(NULL, argv);
	current->mm->arg_end = current->mm->env_start = p;
	while (envc-- > 0) {
		__put_user((char *)p,envp++);
		p += strlen_user((char *)p);
	}
	__put_user(NULL, envp);
	current->mm->env_end = p;
}

static int check_som_header(struct som_hdr *som_ex)
{
	int *buf = (int *)som_ex;
	int i, ck;

	if (som_ex->system_id != SOM_SID_PARISC_1_0 &&
	    som_ex->system_id != SOM_SID_PARISC_1_1 &&
	    som_ex->system_id != SOM_SID_PARISC_2_0)
		return -ENOEXEC;

	if (som_ex->a_magic != SOM_EXEC_NONSHARE &&
	    som_ex->a_magic != SOM_EXEC_SHARE &&
	    som_ex->a_magic != SOM_EXEC_DEMAND)
		return -ENOEXEC;

	if (som_ex->version_id != SOM_ID_OLD &&
	    som_ex->version_id != SOM_ID_NEW)
		return -ENOEXEC;

	ck = 0;
	for (i=0; i<32; i++)
		ck ^= buf[i];
	if (ck != 0)
		return -ENOEXEC;

	return 0;
}

static int map_som_binary(struct file *file,
		const struct som_exec_auxhdr *hpuxhdr)
{
	unsigned long code_start, code_size, data_start, data_size;
	unsigned long bss_start, som_brk;
	int retval;
	int prot = PROT_READ | PROT_EXEC;
	int flags = MAP_FIXED|MAP_PRIVATE|MAP_DENYWRITE|MAP_EXECUTABLE;

	mm_segment_t old_fs = get_fs();
	set_fs(get_ds());

	code_start = SOM_PAGESTART(hpuxhdr->exec_tmem);
	code_size = SOM_PAGEALIGN(hpuxhdr->exec_tsize);
	current->mm->start_code = code_start;
	current->mm->end_code = code_start + code_size;
	retval = vm_mmap(file, code_start, code_size, prot,
			flags, SOM_PAGESTART(hpuxhdr->exec_tfile));
	if (retval < 0 && retval > -1024)
		goto out;

	data_start = SOM_PAGESTART(hpuxhdr->exec_dmem);
	data_size = SOM_PAGEALIGN(hpuxhdr->exec_dsize);
	current->mm->start_data = data_start;
	current->mm->end_data = bss_start = data_start + data_size;
	retval = vm_mmap(file, data_start, data_size,
			prot | PROT_WRITE, flags,
			SOM_PAGESTART(hpuxhdr->exec_dfile));
	if (retval < 0 && retval > -1024)
		goto out;

	som_brk = bss_start + SOM_PAGEALIGN(hpuxhdr->exec_bsize);
	current->mm->start_brk = current->mm->brk = som_brk;
	retval = vm_mmap(NULL, bss_start, som_brk - bss_start,
			prot | PROT_WRITE, MAP_FIXED | MAP_PRIVATE, 0);
	if (retval > 0 || retval < -1024)
		retval = 0;
out:
	set_fs(old_fs);
	return retval;
}


/*
 * These are the functions used to load SOM executables and shared
 * libraries.  There is no binary dependent code anywhere else.
 */

static int
load_som_binary(struct linux_binprm * bprm)
{
	int retval;
	unsigned int size;
	unsigned long som_entry;
	struct som_hdr *som_ex;
	struct som_exec_auxhdr *hpuxhdr;
	struct pt_regs *regs = current_pt_regs();

	/* Get the exec-header */
	som_ex = (struct som_hdr *) bprm->buf;

	retval = check_som_header(som_ex);
	if (retval != 0)
		goto out;

	/* Now read in the auxiliary header information */

	retval = -ENOMEM;
	size = som_ex->aux_header_size;
	if (size > SOM_PAGESIZE)
		goto out;
	hpuxhdr = kmalloc(size, GFP_KERNEL);
	if (!hpuxhdr)
		goto out;

	retval = kernel_read(bprm->file, som_ex->aux_header_location,
			(char *) hpuxhdr, size);
	if (retval != size) {
		if (retval >= 0)
			retval = -EIO;
		goto out_free;
	}

	/* Flush all traces of the currently running executable */
	retval = flush_old_exec(bprm);
	if (retval)
		goto out_free;

	/* OK, This is the point of no return */
	current->personality = PER_HPUX;
	setup_new_exec(bprm);

	/* Set the task size for HP-UX processes such that
	 * the gateway page is outside the address space.
	 * This can be fixed later, but for now, this is much
	 * easier.
	 */

	current->thread.task_size = 0xc0000000;

	/* Set map base to allow enough room for hp-ux heap growth */

	current->thread.map_base = 0x80000000;

	retval = map_som_binary(bprm->file, hpuxhdr);
	if (retval < 0)
		goto out_free;

	som_entry = hpuxhdr->exec_entry;
	kfree(hpuxhdr);

	set_binfmt(&som_format);
	install_exec_creds(bprm);
	setup_arg_pages(bprm, STACK_TOP, EXSTACK_DEFAULT);

	create_som_tables(bprm);

	current->mm->start_stack = bprm->p;

#if 0
	printk("(start_brk) %08lx\n" , (unsigned long) current->mm->start_brk);
	printk("(end_code) %08lx\n" , (unsigned long) current->mm->end_code);
	printk("(start_code) %08lx\n" , (unsigned long) current->mm->start_code);
	printk("(end_data) %08lx\n" , (unsigned long) current->mm->end_data);
	printk("(start_stack) %08lx\n" , (unsigned long) current->mm->start_stack);
	printk("(brk) %08lx\n" , (unsigned long) current->mm->brk);
#endif

	map_hpux_gateway_page(current,current->mm);

	start_thread_som(regs, som_entry, bprm->p);
	return 0;

	/* error cleanup */
out_free:
	kfree(hpuxhdr);
out:
	return retval;
}

static int load_som_library(struct file *f)
{
/* No lib support in SOM yet.  gizza chance.. */
	return -ENOEXEC;
}
	/* Install the SOM loader.
	 * N.B. We *rely* on the table being the right size with the
	 * right number of free slots...
	 */

static int __init init_som_binfmt(void)
{
	register_binfmt(&som_format);
	return 0;
}

static void __exit exit_som_binfmt(void)
{
	/* Remove the SOM loader. */
	unregister_binfmt(&som_format);
}

core_initcall(init_som_binfmt);
module_exit(exit_som_binfmt);

MODULE_LICENSE("GPL");
