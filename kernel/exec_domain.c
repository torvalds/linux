/*
 * Handling of different ABIs (personalities).
 *
 * We group personalities into execution domains which have their
 * own handlers for kernel entry points, signal mapping, etc...
 *
 * 2001-05-06	Complete rewrite,  Christoph Hellwig (hch@infradead.org)
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/module.h>
#include <linux/personality.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/syscalls.h>
#include <linux/sysctl.h>
#include <linux/types.h>
#include <linux/fs_struct.h>

static void default_handler(int, struct pt_regs *);
static unsigned long ident_map[32] = {
	0,	1,	2,	3,	4,	5,	6,	7,
	8,	9,	10,	11,	12,	13,	14,	15,
	16,	17,	18,	19,	20,	21,	22,	23,
	24,	25,	26,	27,	28,	29,	30,	31
};

struct exec_domain default_exec_domain = {
	.name		= "Linux",		/* name */
	.handler	= default_handler,	/* lcall7 causes a seg fault. */
	.pers_low	= 0,			/* PER_LINUX personality. */
	.pers_high	= 0,			/* PER_LINUX personality. */
	.signal_map	= ident_map,		/* Identity map signals. */
	.signal_invmap	= ident_map,		/*  - both ways. */
};


static void
default_handler(int segment, struct pt_regs *regp)
{
	set_personality(0);

	if (current_thread_info()->exec_domain->handler != default_handler)
		current_thread_info()->exec_domain->handler(segment, regp);
	else
		send_sig(SIGSEGV, current, 1);
}

int __set_personality(unsigned int personality)
{
	current->personality = personality;

	return 0;
}
EXPORT_SYMBOL(__set_personality);

#ifdef CONFIG_PROC_FS
static int execdomains_proc_show(struct seq_file *m, void *v)
{
	seq_puts(m, "0-0\tLinux           \t[kernel]\n");
	return 0;
}

static int execdomains_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, execdomains_proc_show, NULL);
}

static const struct file_operations execdomains_proc_fops = {
	.open		= execdomains_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_execdomains_init(void)
{
	proc_create("execdomains", 0, NULL, &execdomains_proc_fops);
	return 0;
}
module_init(proc_execdomains_init);
#endif

SYSCALL_DEFINE1(personality, unsigned int, personality)
{
	unsigned int old = current->personality;

	if (personality != 0xffffffff)
		set_personality(personality);

	return old;
}
