#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l1/task.h>
#include <linux/init.h>
#include <linux/mm.h>

/* let's define the 'fork' access type, with object=task and subject=task. */

struct ipc_perm_access {
	MEDUSA_ACCESS_HEADER;
	u32 perms;
};

MED_ATTRS(ipc_perm_access) {
	MED_ATTR_RO (ipc_perm_access, perms, "perms", MED_UNSIGNED),
	MED_ATTR_END
};


MED_ACCTYPE(ipc_perm_access, "ipc_sem_perm", process_kobject, "process", ipc_sem_kobject, "object");
MED_ACCTYPE(ipc_perm_access, "ipc_shm_perm", process_kobject, "process", ipc_shm_kobject, "object");
MED_ACCTYPE(ipc_perm_access, "ipc_msg_perm", process_kobject, "process", ipc_msg_kobject, "object");

int __init ipc_acctype_init(void) {
	MED_REGISTER_ACCTYPE(ipc_perm_access,MEDUSA_ACCTYPE_TRIGGEREDATSUBJECT);
	return 0;
}

medusa_answer_t medusa_ipc_permission(struct kern_ipc_perm *ipcp, u32 perms)
{
	medusa_answer_t retval = MED_OK;
	struct ipc_perm_access access;
	struct process_kobject process;

    memset(&access, '\0', sizeof(struct ipc_perm_access));
    /* process_kobject parent is zeroed by process_kern2kobj function */

	if (!MED_MAGIC_VALID(&task_security(current)) &&
		process_kobj_validate_task(current) <= 0)
		return MED_OK;

	if (MEDUSA_MONITORED_ACCESS_S(ipc_perm_access, &task_security(current))) {
		access.perms = perms;
		process_kern2kobj(&parent, current);
		ipc_sem_kern2kobj(&parent, current);
		retval = MED_DECIDE(ipc_perm_access, &access, &parent, &parent);
		if (retval == MED_ERR)
			retval = MED_OK;
	}
	return retval;
}
__initcall(fork_acctype_init);
