
#include <linux/xattr.h>
#include <linux/binfmts.h>
#include <linux/lsm_hooks.h>
#include <linux/sysctl.h>
#include <linux/ptrace.h>
#include <linux/prctl.h>
#include <linux/ratelimit.h>
#include <linux/workqueue.h>
#include <linux/string_helpers.h>
#include <linux/task_work.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#include <linux/lsm_hooks.h>


/*
 * Perform a check of a program execution/map.
 *
 * Return 0 if it should be allowed, -EPERM on block.
 */
static int whitelist_bprm_check_security(struct linux_binprm *bprm)
{
       // The current task & the UID it is running as.
       const struct task_struct *task = current;
       kuid_t uid = task->cred->uid;

       // The target we're checking
       struct dentry *dentry = bprm->file->f_path.dentry;
       struct inode *inode = d_backing_inode(dentry);
       int size = 0;

       // Root can access everything.
       if ( uid.val == 0 )
          return 0;

       size = __vfs_getxattr(dentry, inode, "user.whitelisted", NULL, 0);
       if ( size >= 0 )
       {
           printk(KERN_INFO "whitelist LSM check of %s resulted in %d bytes from 'user.whitelisted' - permitting access for UID %d\n", bprm->filename, size, uid.val );
           return 0;
       }

       printk(KERN_INFO "whitelist LSM check of %s denying access for UID %d [ERRO:%d] \n", bprm->filename, uid.val, size );
       return -EPERM;
}

/*
 * The hooks we wish to be installed.
 */
static struct security_hook_list whitelist_hooks[] = {
	LSM_HOOK_INIT(bprm_check_security, whitelist_bprm_check_security),
};

/*
 * Initialize our module.
 */
void __init whitelist_add_hooks(void)
{
	/* register ourselves with the security framework */
	security_add_hooks(whitelist_hooks, ARRAY_SIZE(whitelist_hooks), "whitelist");
	printk(KERN_INFO "whitelist LSM initialized\n");
}
