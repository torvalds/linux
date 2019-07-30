#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l1/task.h>
#include <linux/medusa/l1/ipc.h>
#include <linux/init.h>
#include <linux/mm.h>
#include "kobject_process.h"
#include "kobject_ipc.h"

struct ipc_semop_access {
	MEDUSA_ACCESS_HEADER;
	/* sem_num, sem_op and sem_flg are from sembuf struct definition;
	   see include/uapi/linux/sem.h */
	unsigned int sem_num;	/* semaphore index in array */
	int sem_op;		/* semaphore operation */
	int sem_flg;		/* operation flags */
	unsigned int nsops;	/* number of operations to perform */
	int alter;		/* indicates whether changes on semaphore array are to be made */
	unsigned int ipc_class;
};

MED_ATTRS(ipc_semop_access) {
	MED_ATTR_RO (ipc_semop_access, sem_num, "sem_num", MED_UNSIGNED),
	MED_ATTR_RO (ipc_semop_access, sem_op, "sem_op", MED_SIGNED),
	MED_ATTR_RO (ipc_semop_access, sem_flg, "sem_flg", MED_SIGNED),
	MED_ATTR_RO (ipc_semop_access, nsops, "nsops", MED_UNSIGNED),
	MED_ATTR_RO (ipc_semop_access, alter, "alter", MED_SIGNED),
	MED_ATTR_RO (ipc_semop_access, ipc_class, "ipc_class", MED_UNSIGNED),
	MED_ATTR_END
};

MED_ACCTYPE(ipc_semop_access, "ipc_semop", process_kobject, "process", ipc_kobject, "object");

int __init ipc_acctype_semop_init(void) {
	MED_REGISTER_ACCTYPE(ipc_semop_access,MEDUSA_ACCTYPE_TRIGGEREDATOBJECT);
	return 0;
}

/*
 * Check permissions before performing operations on members of the semaphore set
 * @ipcp contains semaphore ipc_perm structure
 * @sops contains the operation to perform
 * @nsops contains the number of operations to perform
 * @alter contains the flag indicating whether changes are to be made;
 *	if @alter flag is nonzero, the semaphore set may be modified
 *
 * This routine is called only by do_semtimedop() (ipc/sem.c) in RCU read-side.
 *
 * security_sem_semop()
 *  |
 *  |<-- do_semtimedop()
 */
medusa_answer_t medusa_ipc_semop(struct kern_ipc_perm *ipcp, struct sembuf *sops, unsigned nsops, int alter)
{
	medusa_answer_t retval = MED_OK;
	struct ipc_semop_access access;
	struct process_kobject process;
	struct ipc_kobject object;

	/* second argument false: don't need to unlock IPC object */
	if (unlikely(ipc_getref(ipcp, false)))
		/* for now, we don't support error codes */
		return MED_NO;

	if (!MED_MAGIC_VALID(&task_security(current)) && process_kobj_validate_task(current) <= 0)
		goto out;
	if (!MED_MAGIC_VALID(ipc_security(ipcp)) && ipc_kobj_validate_ipcp(ipcp) <= 0)
		goto out;

	if (MEDUSA_MONITORED_ACCESS_S(ipc_semop_access, &task_security(current))) {
		process_kern2kobj(&process, current);
		/* 3-th argument is true: decrement IPC object's refcount in returned object */
		if (ipc_kern2kobj(&object, ipcp, true) == NULL)
			goto out;

		memset(&access, '\0', sizeof(struct ipc_semop_access));
		access.sem_op = sops->sem_op;
		access.sem_num = sops->sem_num;
		access.sem_flg = sops->sem_flg;
		access.nsops = nsops;
		access.alter = alter;
		access.ipc_class = object.ipc_class;

		retval = MED_DECIDE(ipc_semop_access, &access, &process, &object);
		if (retval == MED_ERR)
			retval = MED_OK;
	}
out:
	/* second argument false: don't need to lock IPC object */
	if (unlikely(ipc_putref(ipcp, false)))
		/* for now, we don't support error codes */
		retval = MED_NO;
	return retval;
}
__initcall(ipc_acctype_semop_init);
