#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l3/model.h>
#include <linux/init.h>

#include "kobject_process.h"
#include "kobject_file.h"
#include <linux/medusa/l1/file_handlers.h>

/* let's define the 'sexec' access type, with subj=task and obj=inode */

struct sexec_access {
	MEDUSA_ACCESS_HEADER;
	char filename[NAME_MAX+1];
	kernel_cap_t cap_effective;
	kernel_cap_t cap_inheritable;
	kernel_cap_t cap_permitted;
	kuid_t uid;
	kgid_t gid;
};

MED_ATTRS(sexec_access) {
	MED_ATTR_RO (sexec_access, cap_effective, "ecap", MED_BITMAP),
	MED_ATTR_RO (sexec_access, cap_inheritable, "icap", MED_BITMAP),
	MED_ATTR_RO (sexec_access, cap_permitted, "pcap", MED_BITMAP),
	MED_ATTR_RO (sexec_access, uid, "uid", MED_SIGNED),
	MED_ATTR_RO (sexec_access, gid, "gid", MED_SIGNED),
	MED_ATTR_END
};

MED_ACCTYPE(sexec_access, "sexec", process_kobject, "process",
		file_kobject, "file");

int __init sexec_acctype_init(void) {
	MED_REGISTER_ACCTYPE(sexec_access, MEDUSA_ACCTYPE_TRIGGEREDATSUBJECT);
	return 0;
}

/**
 * medusa_sexec - L1-called code to create access of type 'sexec'.
 * @inode: input inode for sexec() call
 * @mask: mask of access rights to validate
 *
 */
static medusa_answer_t medusa_do_sexec(struct linux_binprm * bprm);

#define DENTRY (bprm->file->f_path.dentry)

medusa_answer_t medusa_sexec(struct linux_binprm * bprm)
{
	medusa_answer_t retval = MED_OK;

	if (!MED_MAGIC_VALID(&task_security(current)) &&
		process_kobj_validate_task(current) <= 0)
		return MED_OK;

	if (!MED_MAGIC_VALID(&inode_security(DENTRY->d_inode)) &&
			file_kobj_validate_dentry(DENTRY,bprm->file->f_path.mnt) <= 0)
		return MED_OK;
	/* no sense in checking VS here */
	if (MEDUSA_MONITORED_ACCESS_S(sexec_access, &task_security(current)))
		retval = medusa_do_sexec(bprm);
	return retval;
}

static medusa_answer_t medusa_do_sexec(struct linux_binprm * bprm)
{
	struct sexec_access access;
	struct process_kobject process;
	struct file_kobject file;
	medusa_answer_t retval;

	file_kobj_dentry2string(DENTRY, access.filename);
	access.cap_effective = bprm->cred->cap_effective;
	access.cap_inheritable = bprm->cred->cap_inheritable;
	access.cap_permitted = bprm->cred->cap_permitted;
	access.uid = bprm->cred->euid;
	access.gid = bprm->cred->egid;
	process_kern2kobj(&process, current);
	file_kern2kobj(&file, DENTRY->d_inode);
	file_kobj_live_add(DENTRY->d_inode);
	retval = MED_DECIDE(sexec_access, &access, &process, &file);
	file_kobj_live_remove(DENTRY->d_inode);
	if (retval != MED_ERR)
		return retval;
	return MED_OK;
}
#undef DENTRY

__initcall(sexec_acctype_init);
