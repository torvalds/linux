#include <linux/medusa/l3/registry.h>
#include <linux/dcache.h>
#include <linux/limits.h>
#include <linux/init.h>

#include "kobject_process.h"
#include "kobject_file.h"
#include <linux/medusa/l1/file_handlers.h>

/* let's define the 'notify_change' access type, with subj=task and obj=inode */
/* todo: rename this to chmod or chattr or whatever */

struct notify_change_access {
	MEDUSA_ACCESS_HEADER;
	char filename[NAME_MAX+1];
	struct iattr attr;
	/* TODO: add few attributes here */
};

MED_ATTRS(notify_change_access) {
	MED_ATTR_RO (notify_change_access, filename, "filename", MED_STRING),
	MED_ATTR_RO(notify_change_access, attr.ia_valid, "valid", MED_UNSIGNED),
	MED_ATTR(notify_change_access, attr.ia_mode, "mode", MED_BITMAP),
	MED_ATTR(notify_change_access, attr.ia_uid, "uid", MED_SIGNED),
	MED_ATTR(notify_change_access, attr.ia_gid, "gid", MED_SIGNED),
	MED_ATTR_RO(notify_change_access, attr.ia_size, "size", MED_UNSIGNED),
	MED_ATTR(notify_change_access, attr.ia_atime, "atime", MED_UNSIGNED),
	MED_ATTR(notify_change_access, attr.ia_mtime, "mtime", MED_UNSIGNED),
	MED_ATTR(notify_change_access, attr.ia_ctime, "ctime", MED_UNSIGNED),
	//MED_ATTR_RO(notify_change_access, attr.ia_attr_flags, "attr_flags", MED_BITMAP),
	MED_ATTR_END
};

MED_ACCTYPE(notify_change_access, "notify_change", process_kobject, "process",
		file_kobject, "file");

int __init notify_change_acctype_init(void) {
	MED_REGISTER_ACCTYPE(notify_change_access, MEDUSA_ACCTYPE_TRIGGEREDATOBJECT);
	return 0;
}

static medusa_answer_t medusa_do_notify_change(struct dentry *dentry, struct iattr * attr);
medusa_answer_t medusa_notify_change(struct dentry *dentry, struct iattr * attr)
{
	if (!dentry || IS_ERR(dentry) || dentry->d_inode == NULL)
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
	if (!attr)
		return MED_OK;
	if (MEDUSA_MONITORED_ACCESS_O(notify_change_access, &inode_security(dentry->d_inode)))
		return medusa_do_notify_change(dentry, attr);
	return MED_OK;
}

/* XXX Don't try to inline this. GCC tries to be too smart about stack. */
static medusa_answer_t medusa_do_notify_change(struct dentry * dentry, struct iattr * attr)
{
	struct notify_change_access access;
	struct process_kobject process;
	struct file_kobject file;
	medusa_answer_t retval;

	file_kobj_dentry2string(dentry, access.filename);
	access.attr.ia_valid = attr->ia_valid;
	access.attr.ia_mode = attr->ia_mode;
	access.attr.ia_uid = attr->ia_uid;
	access.attr.ia_gid = attr->ia_gid;
	access.attr.ia_size = attr->ia_size;
	access.attr.ia_atime = attr->ia_atime;
	access.attr.ia_mtime = attr->ia_mtime;
	access.attr.ia_ctime = attr->ia_ctime;
	//access.attr.ia_attr_flags = attr->ia_attr_flags;
	process_kern2kobj(&process, current);
	file_kern2kobj(&file, dentry->d_inode);
	file_kobj_live_add(dentry->d_inode);
	retval = MED_DECIDE(notify_change_access, &access, &process, &file);
	file_kobj_live_remove(dentry->d_inode);
	if (retval != MED_ERR)
		return retval;
	return MED_OK;
}
__initcall(notify_change_acctype_init);
