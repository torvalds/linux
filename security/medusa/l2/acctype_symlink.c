#include <linux/medusa/l3/registry.h>
#include <linux/dcache.h>
#include <linux/limits.h>
#include <linux/init.h>

#include "kobject_process.h"
#include "kobject_file.h"
#include <linux/medusa/l1/file_handlers.h>

/* let's define the 'symlink' access type, with subj=task and obj=inode */

struct symlink_access {
	MEDUSA_ACCESS_HEADER;
	char filename[NAME_MAX+1];
	char oldname[NAME_MAX+1]; /* hope we will fit in the stack. the string won't fit.. of course. */
};

MED_ATTRS(symlink_access) {
	MED_ATTR_RO (symlink_access, filename, "filename", MED_STRING),
	MED_ATTR_RO (symlink_access, oldname, "oldname", MED_STRING),
	MED_ATTR_END
};

MED_ACCTYPE(symlink_access, "symlink", process_kobject, "process",
		file_kobject, "file");

int __init symlink_acctype_init(void) {
	MED_REGISTER_ACCTYPE(symlink_access, MEDUSA_ACCTYPE_TRIGGEREDATOBJECT);
	return 0;
}

static medusa_answer_t medusa_do_symlink(struct dentry * parent, struct dentry *dentry, const char * oldname);
medusa_answer_t medusa_symlink(struct dentry *dentry, const char * oldname)
{
	struct path ndcurrent, ndupper, ndparent;
	medusa_answer_t retval;

	if (!dentry || IS_ERR(dentry))
		return MED_OK;

	if (!MED_MAGIC_VALID(&task_security(current)) &&
		process_kobj_validate_task(current) <= 0)
		return MED_OK;

	ndcurrent.dentry = dentry;
	ndcurrent.mnt = NULL;
	medusa_get_upper_and_parent(&ndcurrent,&ndupper,&ndparent);
	
	file_kobj_validate_dentry(ndparent.dentry,ndparent.mnt);

	if (!MED_MAGIC_VALID(&inode_security(ndparent.dentry->d_inode)) &&
			file_kobj_validate_dentry(ndparent.dentry,ndparent.mnt) <= 0) {
		medusa_put_upper_and_parent(&ndupper, &ndparent);
		return MED_OK;
	}
	if (!VS_INTERSECT(VSS(&task_security(current)),VS(&inode_security(ndparent.dentry->d_inode))) ||
		!VS_INTERSECT(VSW(&task_security(current)),VS(&inode_security(ndparent.dentry->d_inode)))
	) {
		medusa_put_upper_and_parent(&ndupper, &ndparent);
		return MED_NO;
	}
	if (MEDUSA_MONITORED_ACCESS_O(symlink_access, &inode_security(ndparent.dentry->d_inode)))
		retval = medusa_do_symlink(ndparent.dentry, ndupper.dentry, oldname);
	else
		retval = MED_OK;
	medusa_put_upper_and_parent(&ndupper, &ndparent);
	return retval;
}

/* XXX Don't try to inline this. GCC tries to be too smart about stack. */
static medusa_answer_t medusa_do_symlink(struct dentry * parent, struct dentry *dentry, const char * oldname)
{
	struct symlink_access access;
	struct process_kobject process;
	struct file_kobject file;
	medusa_answer_t retval;

	file_kobj_dentry2string(dentry, access.filename);
	memcpy(access.oldname, oldname, sizeof(access.oldname)-1);
	access.oldname[sizeof(access.oldname)-1] = '\0';
	process_kern2kobj(&process, current);
	file_kern2kobj(&file, parent->d_inode);
	file_kobj_live_add(parent->d_inode);
	retval = MED_DECIDE(symlink_access, &access, &process, &file);
	file_kobj_live_remove(parent->d_inode);
	if (retval != MED_ERR)
		return retval;
	return MED_OK;
}
__initcall(symlink_acctype_init);
