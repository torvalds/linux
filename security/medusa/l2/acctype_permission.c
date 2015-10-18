#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l3/model.h>

#include "kobject_process.h"
#include "kobject_file.h"
#include <linux/medusa/l1/file_handlers.h>

/* let's define the 'permission' access type, with subj=task and obj=inode */

struct permission_access {
	MEDUSA_ACCESS_HEADER;
	char filename[NAME_MAX+1];
	int mask;
};

MED_ATTRS(permission_access) {
	MED_ATTR_RO (permission_access, filename, "filename", MED_STRING),
	MED_ATTR_RO (permission_access, mask, "mask", MED_UNSIGNED),
	MED_ATTR_END
};

MED_ACCTYPE(permission_access, "permission", process_kobject, "process",
		file_kobject, "file");

int __init permission_acctype_init(void) {
	MED_REGISTER_ACCTYPE(permission_access, MEDUSA_ACCTYPE_TRIGGEREDATOBJECT);
	return 0;
}

/**
 * evocate_dentry - divine the dentry by inode.
 * @inode: the inode to find a dentry for.
 *
 * This is another masterpiece, just like the evocate_mnt routine,
 * and shouldn't ever have existed, if the Linux virtual filesystem was
 * consistent and used the same data structures across the code.
 */
static struct dentry * evocate_dentry(struct inode * inode)
{
	struct dentry * dentry;
	struct hlist_node * p;

	/* XXX: are we really supposed to grab a dcache lock here? */
	//spin_lock(&dcache_lock); JK
	spin_lock(&inode->i_lock); // Bugfix March 2015 JK
	list_for_each(p, (inode->i_dentry.first)) {
		if (d_count(list_entry(p, struct dentry, d_u.d_alias))) {
			dentry = dget(list_entry(p, struct dentry, d_u.d_alias));
			spin_unlock(&inode->i_lock); // Bugfix JK March 2015
			return dentry;
		}
	}
	spin_unlock(&inode->i_lock); // Bugfix JK March 2015
	//spin_unlock(&dcache_lock);
	return NULL;
}

medusa_answer_t medusa_do_permission(struct dentry * dentry, struct inode * inode, int mask);
/**
 * medusa_permission - L1-called code to create access of type 'permission'.
 * @inode: input inode for permission() call
 * @mask: mask of access rights to validate
 *
 */
medusa_answer_t medusa_permission(struct inode * inode, int mask)
{
	medusa_answer_t retval = MED_OK;
	struct dentry * dentry;

	if (!MED_MAGIC_VALID(&task_security(current)) &&
		process_kobj_validate_task(current) <= 0)
		return MED_OK;

	dentry = evocate_dentry(inode);
	if (!dentry || IS_ERR(dentry))
		return retval;
	if (!MED_MAGIC_VALID(&inode_security(inode)) &&
			file_kobj_validate_dentry(dentry,NULL) <= 0)
		goto out_dput;
	if (
		!VS_INTERSECT(VSS(&task_security(current)),VS(&inode_security(inode))) ||
		( (mask & (S_IRUGO | S_IXUGO)) &&
		  	!VS_INTERSECT(VSR(&task_security(current)),VS(&inode_security(inode))) ) ||
		( (mask & S_IWUGO) &&
		  	!VS_INTERSECT(VSW(&task_security(current)),VS(&inode_security(inode))) )
	   ) {
		retval = MED_NO;
		goto out_dput;
	}

	if (MEDUSA_MONITORED_ACCESS_O(permission_access, &inode_security(inode)))
		retval = medusa_do_permission(dentry, inode, mask);
out_dput:
	dput(dentry);
	return retval;
}

medusa_answer_t medusa_do_permission(struct dentry * dentry, struct inode * inode, int mask)
{
	struct permission_access access;
	struct process_kobject process;
	struct file_kobject file;
	medusa_answer_t retval;

	file_kobj_dentry2string(dentry, access.filename);
	access.mask = mask;
	process_kern2kobj(&process, current);
	file_kern2kobj(&file, inode);
	file_kobj_live_add(inode);
	retval = MED_DECIDE(permission_access, &access, &process, &file);
	file_kobj_live_remove(inode);
	if (retval != MED_ERR)
		return retval;
	return MED_OK;
}
__initcall(permission_acctype_init);
