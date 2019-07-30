#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l1/task.h>
#include <linux/medusa/l1/ipc.h>
#include <linux/init.h>
#include <linux/mm.h>
#include "kobject_process.h"
#include "kobject_ipc.h"

struct ipc_ctl_access {
	MEDUSA_ACCESS_HEADER;
	unsigned int ipc_class;
	int cmd;	/* operation to be performed */
};

MED_ATTRS(ipc_ctl_access) {
	MED_ATTR_RO (ipc_ctl_access, cmd, "cmd", MED_SIGNED),
	MED_ATTR_RO (ipc_ctl_access, ipc_class, "ipc_class", MED_UNSIGNED),
	MED_ATTR_END
};

MED_ACCTYPE(ipc_ctl_access, "ipc_ctl", process_kobject, "process", ipc_kobject, "object");

int __init ipc_acctype_ctl_init(void) {
	MED_REGISTER_ACCTYPE(ipc_ctl_access,MEDUSA_ACCTYPE_TRIGGEREDATOBJECT);
	return 0;
}

/*
 * Check permission when an IPC object (semaphore, message queue, shared memory)
 * operation specified by @cmd is to be performed on the IPC object for which
 * kernel ipc permission @ipcp is given. The @ipcp may be NULL, e.g. for
 * IPC_INFO or MSG_INFO or SEM_INFO or SHM_INFO @cmd value.
 * @ipcp contains kernel IPC permission of the related IPC object
 * @cmd contains the operation to be performed
 *
 * This function is called with rcu_read_lock() held if @ipcp is not NULL.
 * So in this case it is necessary to release rcu lock because
 * this routine may wait while authorisation server is deciding.
 *
 * medusa_ipc_ctl()
 *  |
 *  |<-- medusa_l1_msg_queue_msgctl()
 *  |     |
 *  |     |<-- security_msg_queue_msgctl()
 *  |           |
 *  |           |<-- msgctl_stat()
 *  |           |<-- msgctl_down()
 *  |           |<-- msgctl_info() (@ipcp is NULL, no rcu_read_lock())
 *  |
 *  |<-- medusa_l1_sem_semctl()
 *  |     |
 *  |     |<-- security_sem_semctl()
 *  |           |
 *  |           |<-- semctl_stat()
 *  |           |<-- semctl_down()
 *  |           |<-- semctl_setval()
 *  |           |<-- semctl_main()
 *  |           |<-- semctl_info() (@ipcp is NULL, no rcu_read_lock())
 *  |
 *  |<-- medusa_l1_shm_shmctl()
 *        |
 *        |<-- security_shm_shmctl()
 *              |
 *              |<-- shmctl_stat()
 *              |<-- shmctl_down()
 *              |<-- shmctl_do_lock()
 *              |<-- shmctl_shm_info() (@ipcp is NULL, no rcu_read_lock())
 *              |<-- shmctl_ipc_info() (@ipcp is NULL, no rcu_read_lock())
 */
medusa_answer_t medusa_ipc_ctl(struct kern_ipc_perm *ipcp, int cmd)
{
	medusa_answer_t retval = MED_OK;
	struct ipc_ctl_access access;
	struct process_kobject process;
	struct ipc_kobject object, *object_p = NULL;

	/* 'ipcp' is NULL in case of 'cmd': IPC_INFO, MSG_INFO, SEM_INFO, SHM_INFO */
	if (likely(ipcp)) {
		/* second argument false: don't need to unlock IPC object */
		if (unlikely(ipc_getref(ipcp, false)))
			/* for now, we don't support error codes */
			return MED_NO;

		object_p = &object;
		if (!MED_MAGIC_VALID(ipc_security(ipcp)) && ipc_kobj_validate_ipcp(ipcp) <= 0)
			goto out;
	}

	if (!MED_MAGIC_VALID(&task_security(current)) && process_kobj_validate_task(current) <= 0)
		goto out;

	if (MEDUSA_MONITORED_ACCESS_S(ipc_ctl_access, &task_security(current))) {
		memset(&access, '\0', sizeof(struct ipc_ctl_access));
		access.cmd = cmd;
		access.ipc_class = MED_IPC_UNDEFINED;

		process_kern2kobj(&process, current);
		if (likely(object_p)) {
			/* 3-th argument is true: decrement IPC object's refcount in returned object */
			if (ipc_kern2kobj(object_p, ipcp, true) <= 0)
				goto out;
			access.ipc_class = object_p->ipc_class;
		}

		/* in case of NULL 'ipcp', 'object_p' is NULL too */
		retval = MED_DECIDE(ipc_ctl_access, &access, &process, object_p);
		if (retval == MED_ERR)
			retval = MED_OK;
	}
out:
	if (likely(ipcp)) {
		/* second argument false: don't need to lock IPC object */
		if (unlikely(ipc_putref(ipcp, false)))
			/* for now, we don't support error codes */
			retval = MED_NO;
	}
	return retval;
}
__initcall(ipc_acctype_ctl_init);
