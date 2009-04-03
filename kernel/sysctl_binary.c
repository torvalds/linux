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

static int deprecated_sysctl_warning(struct __sysctl_args *args);

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

static int parse_table(int __user *name, int nlen,
		       void __user *oldval, size_t __user *oldlenp,
		       void __user *newval, size_t newlen,
		       struct ctl_table_root *root,
		       struct ctl_table *table)
{
	int n;
repeat:
	if (!nlen)
		return -ENOTDIR;
	if (get_user(n, name))
		return -EFAULT;
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

int do_sysctl(int __user *name, int nlen, void __user *oldval, size_t __user *oldlenp,
	       void __user *newval, size_t newlen)
{
	struct ctl_table_header *head;
	int error = -ENOTDIR;

	if (nlen <= 0 || nlen >= CTL_MAXNAME)
		return -ENOTDIR;
	if (oldval) {
		int old_len;
		if (!oldlenp || get_user(old_len, oldlenp))
			return -EFAULT;
	}

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

SYSCALL_DEFINE1(sysctl, struct __sysctl_args __user *, args)
{
	struct __sysctl_args tmp;
	int error;

	if (copy_from_user(&tmp, args, sizeof(tmp)))
		return -EFAULT;

	error = deprecated_sysctl_warning(&tmp);
	if (error)
		goto out;

	lock_kernel();
	error = do_sysctl(tmp.name, tmp.nlen, tmp.oldval, tmp.oldlenp,
			  tmp.newval, tmp.newlen);
	unlock_kernel();
out:
	return error;
}

#else /* CONFIG_SYSCTL_SYSCALL */

SYSCALL_DEFINE1(sysctl, struct __sysctl_args __user *, args)
{
	struct __sysctl_args tmp;
	int error;

	if (copy_from_user(&tmp, args, sizeof(tmp)))
		return -EFAULT;

	error = deprecated_sysctl_warning(&tmp);

	/* If no error reading the parameters then just -ENOSYS ... */
	if (!error)
		error = -ENOSYS;

	return error;
}

#endif /* CONFIG_SYSCTL_SYSCALL */

static int deprecated_sysctl_warning(struct __sysctl_args *args)
{
	static int msg_count;
	int name[CTL_MAXNAME];
	int i;

	/* Check args->nlen. */
	if (args->nlen < 0 || args->nlen > CTL_MAXNAME)
		return -ENOTDIR;

	/* Read in the sysctl name for better debug message logging */
	for (i = 0; i < args->nlen; i++)
		if (get_user(name[i], args->name + i))
			return -EFAULT;

	/* Ignore accesses to kernel.version */
	if ((args->nlen == 2) && (name[0] == CTL_KERN) && (name[1] == KERN_VERSION))
		return 0;

	if (msg_count < 5) {
		msg_count++;
		printk(KERN_INFO
			"warning: process `%s' used the deprecated sysctl "
			"system call with ", current->comm);
		for (i = 0; i < args->nlen; i++)
			printk("%d.", name[i]);
		printk("\n");
	}
	return 0;
}
