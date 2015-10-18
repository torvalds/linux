#include <linux/medusa/l3/registry.h>
#include <linux/dcache.h>
#include <linux/limits.h>
#include <linux/init.h>

#include "kobject_process.h"
#include "kobject_file.h"
#include <linux/medusa/l1/file_handlers.h>

/* let's define the 'truncate' access type, with subj=task and obj=inode */

struct truncate_access {
	MEDUSA_ACCESS_HEADER;
	char filename[NAME_MAX+1];
	loff_t length;
};

MED_ATTRS(truncate_access) {
	MED_ATTR_RO (truncate_access, filename, "filename", MED_STRING),
	MED_ATTR_RO (truncate_access, length, "length", MED_UNSIGNED),
	MED_ATTR_END
};

MED_ACCTYPE(truncate_access, "truncate", process_kobject, "process",
		file_kobject, "file");

int __init truncate_acctype_init(void) {
	MED_REGISTER_ACCTYPE(truncate_access, MEDUSA_ACCTYPE_TRIGGEREDATOBJECT);
	return 0;
}

static medusa_answer_t medusa_do_truncate(struct dentry *dentry, unsigned long length);
medusa_answer_t medusa_truncate(struct dentry *dentry, unsigned long length)
{
	if (!dentry || IS_ERR(dentry) || !dentry->d_inode)
		return MED_OK;
	if (!MED_MAGIC_VALID(&task_security(current)) &&
		process_kobj_validate_task(current) <= 0)
		return MED_OK;

	if (!MED_MAGIC_VALID(&inode_security(dentry->d_inode)) &&
			file_kobj_validate_dentry(dentry,NULL) <= 0)
		return MED_OK;
	if (!VS_INTERSECT(VSS(&task_security(current)),VS(&inode_security(dentry->d_inode))) ||
		!VS_INTERSECT(VSW(&task_security(current)),VS(&inode_security(dentry->d_inode)))
	)
		return MED_NO;
	if (MEDUSA_MONITORED_ACCESS_O(truncate_access, &inode_security(dentry->d_inode)))
		return medusa_do_truncate(dentry, length);
	return MED_OK;
}

/* XXX Don't try to inline this. GCC tries to be too smart about stack. */
static medusa_answer_t medusa_do_truncate(struct dentry *dentry, unsigned long length)
{
	struct truncate_access access;
	struct process_kobject process;
	struct file_kobject file;
	medusa_answer_t retval;

	file_kobj_dentry2string(dentry, access.filename);
	access.length = length;
	process_kern2kobj(&process, current);
	file_kern2kobj(&file, dentry->d_inode);
	file_kobj_live_add(dentry->d_inode);
	retval = MED_DECIDE(truncate_access, &access, &process, &file);
	file_kobj_live_remove(dentry->d_inode);
	if (retval != MED_ERR)
		return retval;
	return MED_OK;
}
__initcall(truncate_acctype_init);
