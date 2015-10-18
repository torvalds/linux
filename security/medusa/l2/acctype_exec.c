#include <linux/medusa/l3/registry.h>
#include <linux/dcache.h>
#include <linux/init.h>

#include "kobject_process.h"
#include "kobject_file.h"
#include <linux/medusa/l1/file_handlers.h>

/* let's define the 'exec' access type, with subj=task and obj=inode */

/* in fact, there are 2 of them. They're exactly the same, and differ
 * only in the place where they are triggered.
 */

struct exec_faccess {
	MEDUSA_ACCESS_HEADER;
	char filename[NAME_MAX+1];
};
struct exec_paccess {
	MEDUSA_ACCESS_HEADER;
	char filename[NAME_MAX+1];
};

MED_ATTRS(exec_faccess) {
	MED_ATTR_RO (exec_faccess, filename, "filename", MED_STRING),
	MED_ATTR_END
};
MED_ATTRS(exec_paccess) {
	MED_ATTR_RO (exec_paccess, filename, "filename", MED_STRING),
	MED_ATTR_END
};

MED_ACCTYPE(exec_faccess, "fexec", process_kobject, "process",
		file_kobject, "file");
MED_ACCTYPE(exec_paccess, "pexec", process_kobject, "process",
		file_kobject, "file");

int __init exec_acctype_init(void) {
	MED_REGISTER_ACCTYPE(exec_faccess, MEDUSA_ACCTYPE_TRIGGEREDATOBJECT);
	MED_REGISTER_ACCTYPE(exec_paccess, MEDUSA_ACCTYPE_TRIGGEREDATSUBJECT);
	return 0;
}

static medusa_answer_t medusa_do_fexec(struct dentry * dentry);
static medusa_answer_t medusa_do_pexec(struct dentry * dentry);
medusa_answer_t medusa_exec(struct dentry ** dentryp)
{
	medusa_answer_t retval;

	if (!*dentryp || IS_ERR(*dentryp) || !(*dentryp)->d_inode)
		return MED_OK;
	if (!MED_MAGIC_VALID(&task_security(current)) &&
		process_kobj_validate_task(current) <= 0)
		return MED_OK;

	if (!MED_MAGIC_VALID(&inode_security((*dentryp)->d_inode)) &&

			file_kobj_validate_dentry(*dentryp,NULL) <= 0)
		return MED_OK;
	if (!VS_INTERSECT(VSS(&task_security(current)),VS(&inode_security((*dentryp)->d_inode))) ||
		!VS_INTERSECT(VSR(&task_security(current)),VS(&inode_security((*dentryp)->d_inode)))
	)
		return MED_NO;
	if (MEDUSA_MONITORED_ACCESS_S(exec_paccess, &task_security(current))) {
		retval = medusa_do_pexec(*dentryp);
		if (retval == MED_NO)
			return retval;
	}
	if (MEDUSA_MONITORED_ACCESS_O(exec_faccess, &inode_security((*dentryp)->d_inode))) {
		retval = medusa_do_fexec(*dentryp);
		return retval;
	}
	return MED_OK;
}

/* XXX Don't try to inline this. GCC tries to be too smart about stack. */
static medusa_answer_t medusa_do_fexec(struct dentry * dentry)
{
	struct exec_faccess access;
	struct process_kobject process;
	struct file_kobject file;
	medusa_answer_t retval;

	file_kobj_dentry2string(dentry, access.filename);
	process_kern2kobj(&process, current);
	file_kern2kobj(&file, dentry->d_inode);
	file_kobj_live_add(dentry->d_inode);
	retval = MED_DECIDE(exec_faccess, &access, &process, &file);
	file_kobj_live_remove(dentry->d_inode);
	if (retval != MED_ERR)
		return retval;
	return MED_OK;
}
static medusa_answer_t medusa_do_pexec(struct dentry *dentry)
{
	struct exec_paccess access;
	struct process_kobject process;
	struct file_kobject file;
	medusa_answer_t retval;

	file_kobj_dentry2string(dentry, access.filename);
	process_kern2kobj(&process, current);
	file_kern2kobj(&file, dentry->d_inode);
	file_kobj_live_add(dentry->d_inode);
	retval = MED_DECIDE(exec_paccess, &access, &process, &file);
	file_kobj_live_remove(dentry->d_inode);
	if (retval == MED_ERR)
		retval = MED_OK;
	return retval;
}
int medusa_monitored_pexec(void)
{
	return MEDUSA_MONITORED_ACCESS_S(exec_paccess, &task_security(current));
}

void medusa_monitor_pexec(int flag)
{
	if (flag)
		MEDUSA_MONITOR_ACCESS_S(exec_paccess, &task_security(current));
	else
		MEDUSA_UNMONITOR_ACCESS_S(exec_paccess, &task_security(current));
}
__initcall(exec_acctype_init);
