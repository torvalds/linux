#include <linux/medusa/l3/registry.h>
#include <linux/dcache.h>
#include <linux/limits.h>
#include <linux/init.h>
#include <linux/mm.h>

#include "kobject_process.h"
#include "kobject_file.h"
#include <linux/medusa/l1/file_handlers.h>

/* let's define the 'rename' access type, with subj=task and obj=inode */

struct rename_access {
	MEDUSA_ACCESS_HEADER;
	char filename[NAME_MAX+1];
	char newname[NAME_MAX+1];
};

MED_ATTRS(rename_access) {
	MED_ATTR_RO (rename_access, filename, "filename", MED_STRING),
	MED_ATTR_RO (rename_access, newname, "newname", MED_STRING),
	MED_ATTR_END
};

MED_ACCTYPE(rename_access, "rename", process_kobject, "process",
		file_kobject, "file");

int __init rename_acctype_init(void) {
	MED_REGISTER_ACCTYPE(rename_access, MEDUSA_ACCTYPE_TRIGGEREDATOBJECT);
	return 0;
}

static medusa_answer_t medusa_do_rename(struct dentry *dentry, const char * newname);
//medusa_answer_t medusa_rename(struct dentry *old_dentry, struct dentry *new_dentry)
medusa_answer_t medusa_rename(struct dentry *old_dentry, const char *newname)
{
	medusa_answer_t r;

	if (!old_dentry || IS_ERR(old_dentry) || old_dentry->d_inode == NULL)
		return MED_OK;

	if (!MED_MAGIC_VALID(&task_security(current)) &&
		process_kobj_validate_task(current) <= 0)
		return MED_OK;

	if (!MED_MAGIC_VALID(&inode_security(old_dentry->d_inode)) &&
			file_kobj_validate_dentry(old_dentry,NULL) <= 0) {
		return MED_OK;
	}
    //!VS_INTERSECT(VSS(&task_security(current)),VS(&inode_security(new_dentry->d_inode))) ||
    //!VS_INTERSECT(VSW(&task_security(current)),VS(&inode_security(new_dentry->d_inode)))
	if (!VS_INTERSECT(VSS(&task_security(current)),VS(&inode_security(old_dentry->d_inode))) ||
		!VS_INTERSECT(VSW(&task_security(current)),VS(&inode_security(old_dentry->d_inode))) 
	)
		return MED_NO;
#warning FIXME - add target directory checking

	r = MED_OK;
	if (MEDUSA_MONITORED_ACCESS_O(rename_access, &inode_security(old_dentry->d_inode)))
		r=medusa_do_rename(old_dentry, newname);
		//r=medusa_do_rename(old_dentry, new_dentry->d_name.name);
	MED_MAGIC_INVALIDATE(&inode_security(old_dentry->d_inode));
	return r;
}

/* XXX Don't try to inline this. GCC tries to be too smart about stack. */
static medusa_answer_t medusa_do_rename(struct dentry *dentry, const char * newname)
{
	struct rename_access access;
	struct process_kobject process;
	struct file_kobject file;
	medusa_answer_t retval;
        int newnamelen;

        memset(&access, '\0', sizeof(struct rename_access));
        /* process_kobject process is zeroed by process_kern2kobj function */
        /* file_kobject file is zeroed by file_kern2kobj function */

	file_kobj_dentry2string(dentry, access.filename);
        newnamelen = strlen(newname);
        if (newnamelen > NAME_MAX)
                newnamelen = NAME_MAX;
	memcpy(access.newname, newname, newnamelen);
	access.newname[newnamelen] = '\0';
	process_kern2kobj(&process, current);
	file_kern2kobj(&file, dentry->d_inode);
	file_kobj_live_add(dentry->d_inode);
	retval = MED_DECIDE(rename_access, &access, &process, &file);
	file_kobj_live_remove(dentry->d_inode);
	if (retval != MED_ERR)
		return retval;
	return MED_OK;
}
__initcall(rename_acctype_init);
