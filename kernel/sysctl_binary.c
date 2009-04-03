#include <linux/stat.h>
#include <linux/sysctl.h>
#include "../fs/xfs/linux-2.6/xfs_sysctl.h"
#include <linux/sunrpc/debug.h>
#include <linux/string.h>
#include <net/ip_vs.h>
#include <linux/syscalls.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include <linux/nsproxy.h>
#include <linux/pid_namespace.h>
#include <linux/file.h>
#include <linux/ctype.h>
#include <linux/smp_lock.h>

#ifdef CONFIG_SYSCTL_SYSCALL

/* Perform the actual read/write of a sysctl table entry. */
static int do_sysctl_strategy(struct ctl_table_root *root,
			struct ctl_table *table,
			void __user *oldval, size_t __user *oldlenp,
			void __user *newval, size_t newlen)
{
	int op = 0, rc;

	if (oldval)
		op |= MAY_READ;
	if (newval)
		op |= MAY_WRITE;
	if (sysctl_perm(root, table, op))
		return -EPERM;

	if (table->strategy) {
		rc = table->strategy(table, oldval, oldlenp, newval, newlen);
		if (rc < 0)
			return rc;
		if (rc > 0)
			return 0;
	}

	/* If there is no strategy routine, or if the strategy returns
	 * zero, proceed with automatic r/w */
	if (table->data && table->maxlen) {
		rc = sysctl_data(table, oldval, oldlenp, newval, newlen);
		if (rc < 0)
			return rc;
	}
	return 0;
}

static int parse_table(const int *name, int nlen,
		       void __user *oldval, size_t __user *oldlenp,
		       void __user *newval, size_t newlen,
		       struct ctl_table_root *root,
		       struct ctl_table *table)
{
	int n;
repeat:
	if (!nlen)
		return -ENOTDIR;
	n = *name;
	for ( ; table->ctl_name || table->procname; table++) {
		if (!table->ctl_name)
			continue;
		if (n == table->ctl_name) {
			int error;
			if (table->child) {
				if (sysctl_perm(root, table, MAY_EXEC))
					return -EPERM;
				name++;
				nlen--;
				table = table->child;
				goto repeat;
			}
			error = do_sysctl_strategy(root, table,
						   oldval, oldlenp,
						   newval, newlen);
			return error;
		}
	}
	return -ENOTDIR;
}

static ssize_t binary_sysctl(const int *name, int nlen,
	void __user *oldval, size_t __user *oldlenp,
	void __user *newval, size_t newlen)

{
	struct ctl_table_header *head;
	ssize_t error = -ENOTDIR;

	for (head = sysctl_head_next(NULL); head;
			head = sysctl_head_next(head)) {
		error = parse_table(name, nlen, oldval, oldlenp, 
					newval, newlen,
					head->root, head->ctl_table);
		if (error != -ENOTDIR) {
			sysctl_head_finish(head);
			break;
		}
	}
	return error;
}

#else /* CONFIG_SYSCTL_SYSCALL */

static ssize_t binary_sysctl(const int *ctl_name, int nlen,
	void __user *oldval, size_t __user *oldlenp,
	void __user *newval, size_t newlen)
{
	return -ENOSYS;
}

#endif /* CONFIG_SYSCTL_SYSCALL */

static void deprecated_sysctl_warning(const int *name, int nlen)
{
	static int msg_count;
	int i;

	/* Ignore accesses to kernel.version */
	if ((nlen == 2) && (name[0] == CTL_KERN) && (name[1] == KERN_VERSION))
		return;

	if (msg_count < 5) {
		msg_count++;
		printk(KERN_INFO
			"warning: process `%s' used the deprecated sysctl "
			"system call with ", current->comm);
		for (i = 0; i < nlen; i++)
			printk("%d.", name[i]);
		printk("\n");
	}
	return;
}

int do_sysctl(int __user *args_name, int nlen,
	void __user *oldval, size_t __user *oldlenp,
	void __user *newval, size_t newlen)
{
	int name[CTL_MAXNAME];
	size_t oldlen = 0;
	int i;

	if (nlen <= 0 || nlen >= CTL_MAXNAME)
		return -ENOTDIR;
	if (oldval && !oldlenp)
		return -EFAULT;
	if (oldlenp && get_user(oldlen, oldlenp))
		return -EFAULT;

	/* Read in the sysctl name for simplicity */
	for (i = 0; i < nlen; i++)
		if (get_user(name[i], args_name + i))
			return -EFAULT;

	deprecated_sysctl_warning(name, nlen);

	return binary_sysctl(name, nlen, oldval, oldlenp, newval, newlen);
}


SYSCALL_DEFINE1(sysctl, struct __sysctl_args __user *, args)
{
	struct __sysctl_args tmp;
	int error;

	if (copy_from_user(&tmp, args, sizeof(tmp)))
		return -EFAULT;

	lock_kernel();
	error = do_sysctl(tmp.name, tmp.nlen, tmp.oldval, tmp.oldlenp,
			  tmp.newval, tmp.newlen);
	unlock_kernel();

	return error;
}
