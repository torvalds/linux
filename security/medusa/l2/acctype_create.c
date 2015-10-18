#include <linux/security.h>
#include <linux/medusa/l3/registry.h>
#include <linux/dcache.h>
#include <linux/limits.h>
#include <linux/init.h>
#include <linux/fs.h>

#include "kobject_process.h"
#include "kobject_file.h"
#include <linux/medusa/l1/file_handlers.h>

/* let's define the 'create' access type, with subj=task and obj=inode */

struct create_access {
	MEDUSA_ACCESS_HEADER;
	char filename[NAME_MAX+1];
	int mode;
};

MED_ATTRS(create_access) {
	MED_ATTR_RO (create_access, filename, "filename", MED_STRING),
	MED_ATTR_RO (create_access, mode, "mode", MED_BITMAP),
	MED_ATTR_END
};

MED_ACCTYPE(create_access, "create", process_kobject, "process",
		file_kobject, "file");

int __init create_acctype_init(void) {
	MED_REGISTER_ACCTYPE(create_access, MEDUSA_ACCTYPE_TRIGGEREDATOBJECT);
	return 0;
}

static medusa_answer_t medusa_do_create(struct dentry * parent, struct dentry *dentry, int mode);
medusa_answer_t medusa_create(struct dentry *dentry, int mode)
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
	if (MEDUSA_MONITORED_ACCESS_O(create_access, &inode_security(ndparent.dentry->d_inode)))
		retval = medusa_do_create(ndparent.dentry, ndupper.dentry, mode);
	else
		retval = MED_OK;
	medusa_put_upper_and_parent(&ndupper, &ndparent);
	return retval;
}

/* XXX Don't try to inline this. GCC tries to be too smart about stack. */
static medusa_answer_t medusa_do_create(struct dentry * parent, struct dentry *dentry, int mode)
{
	struct create_access access;
	struct process_kobject process;
	struct file_kobject file;
	medusa_answer_t retval;

	file_kobj_dentry2string(dentry, access.filename);
	access.mode = mode;
	process_kern2kobj(&process, current);
	file_kern2kobj(&file, parent->d_inode);
	file_kobj_live_add(parent->d_inode);
	retval = MED_DECIDE(create_access, &access, &process, &file);
	if (retval == MED_ERR)
		retval = MED_OK;
	file_kobj_live_remove(parent->d_inode);
	return retval;
}
__initcall(create_acctype_init);
