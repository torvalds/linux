#include <linux/medusa/l3/registry.h>
#include <linux/dcache.h>
#include <linux/limits.h>
#include <linux/init.h>
#include <linux/mm.h>

#include "kobject_process.h"
#include "kobject_file.h"
#include <linux/medusa/l1/file_handlers.h>

/* let's define the 'mkdir' access type, with subj=task and obj=inode */

struct mkdir_access {
	MEDUSA_ACCESS_HEADER;
	char filename[NAME_MAX+1];
	int mode;
};

MED_ATTRS(mkdir_access) {
	MED_ATTR_RO (mkdir_access, filename, "filename", MED_STRING),
	MED_ATTR_RO (mkdir_access, mode, "mode", MED_BITMAP),
	MED_ATTR_END
};

MED_ACCTYPE(mkdir_access, "mkdir", process_kobject, "process",
		file_kobject, "file");

int __init mkdir_acctype_init(void) {
	MED_REGISTER_ACCTYPE(mkdir_access, MEDUSA_ACCTYPE_TRIGGEREDATOBJECT);
	return 0;
}

static medusa_answer_t medusa_do_mkdir(struct dentry * parent, struct dentry *dentry, int mode);
medusa_answer_t medusa_mkdir(const struct path *parent, struct dentry *dentry, int mode)
{
	// struct path ndcurrent, ndupper, ndparent;
	medusa_answer_t retval;

	if (!dentry || IS_ERR(dentry))
		return MED_OK;
	if (!MED_MAGIC_VALID(&task_security(current)) &&
		process_kobj_validate_task(current) <= 0)
		return MED_OK;

	// ndcurrent.dentry = dentry;
	// ndcurrent.mnt = NULL;
	// medusa_get_upper_and_parent(&ndcurrent,&ndupper,&ndparent);

	if (!MED_MAGIC_VALID(&inode_security(parent->dentry->d_inode)) &&
			file_kobj_validate_dentry(parent->dentry,parent->mnt) <= 0) {
		// medusa_put_upper_and_parent(&ndupper, &ndparent);
		return MED_OK;
	}
	if (!VS_INTERSECT(VSS(&task_security(current)),VS(&inode_security(parent->dentry->d_inode))) ||
		!VS_INTERSECT(VSW(&task_security(current)),VS(&inode_security(parent->dentry->d_inode)))
	) {
		// medusa_put_upper_and_parent(&ndupper, &ndparent);
		return MED_NO;
	}
	if (MEDUSA_MONITORED_ACCESS_O(mkdir_access, &inode_security(parent->dentry->d_inode)))
		retval = medusa_do_mkdir(parent->dentry, dentry, mode);
	else
		retval = MED_OK;
	// medusa_put_upper_and_parent(&ndupper, &ndparent);
	return retval;
}

/* XXX Don't try to inline this. GCC tries to be too smart about stack. */
static medusa_answer_t medusa_do_mkdir(struct dentry * parent, struct dentry *dentry, int mode)
{
	struct mkdir_access access;
	struct process_kobject process;
	struct file_kobject file;
	medusa_answer_t retval;

        memset(&access, '\0', sizeof(struct mkdir_access));
        /* process_kobject process is zeroed by process_kern2kobj function */
        /* file_kobject file is zeroed by file_kern2kobj function */

	file_kobj_dentry2string(dentry, access.filename);
	access.mode = mode;
	process_kern2kobj(&process, current);
	file_kern2kobj(&file, parent->d_inode);
	file_kobj_live_add(parent->d_inode);
	retval = MED_DECIDE(mkdir_access, &access, &process, &file);
	file_kobj_live_remove(parent->d_inode);
	if (retval != MED_ERR)
		return retval;
	return MED_OK;
}
__initcall(mkdir_acctype_init);
