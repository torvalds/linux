#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l1/task.h>
#include <linux/medusa/l1/ipc.h>
#include <linux/init.h>
#include <linux/mm.h>
#include "kobject_process.h"
#include "kobject_ipc.h"

struct ipc_msgsnd_access {
	MEDUSA_ACCESS_HEADER;
	long m_type;	/* message type;  see 'struct msg_msg' in include/linux/msg.h */
	size_t m_ts;	/* msg text size; see 'struct msg_msg' in include/linux/msg.h */
	/* TODO char m_text[???]; send also message text? */
	int msgflg;	/* operational flags */
	unsigned int ipc_class;
};

MED_ATTRS(ipc_msgsnd_access) {
	MED_ATTR_RO (ipc_msgsnd_access, m_type, "m_type", MED_SIGNED),
	MED_ATTR_RO (ipc_msgsnd_access, m_ts, "m_ts", MED_UNSIGNED),
	MED_ATTR_RO (ipc_msgsnd_access, msgflg, "msgflg", MED_SIGNED),
	MED_ATTR_RO (ipc_msgsnd_access, ipc_class, "ipc_class", MED_UNSIGNED),
	MED_ATTR_END
};

MED_ACCTYPE(ipc_msgsnd_access, "ipc_msgsnd", process_kobject, "process", ipc_kobject, "object");

int __init ipc_acctype_msgsnd_init(void) {
	MED_REGISTER_ACCTYPE(ipc_msgsnd_access,MEDUSA_ACCTYPE_TRIGGEREDATOBJECT);
	return 0;
}

/*
 * Check permission before a message @msg is enqueued on the message queue for
 * which kernel ipc permission @ipcp is given.
 * @ipcp contains kernel ipc permissions for related message queue
 * @msg contains the message to be enqueued
 * @msgflg contains the operational flags
 *
 * This function is always called with rcu_read_lock() and ipcp->lock held.
 *
 * security_msg_queue_msgsnd()
 *  |
 *  |<-- do_msgsnd() (always get ipcp->lock)
 */
medusa_answer_t medusa_ipc_msgsnd(struct kern_ipc_perm *ipcp, struct msg_msg *msg, int msgflg)
{
	medusa_answer_t retval = MED_OK;
	struct ipc_msgsnd_access access;
	struct process_kobject process;
	struct ipc_kobject object;

	/* second argument true: returns with unlocked IPC object */
	if (unlikely(ipc_getref(ipcp, true)))
		/* for now, we don't support error codes */
		return MED_NO;

	if (!MED_MAGIC_VALID(&task_security(current)) && process_kobj_validate_task(current) <= 0)
		goto out;
	if (!MED_MAGIC_VALID(ipc_security(ipcp)) && ipc_kobj_validate_ipcp(ipcp) <= 0)
		goto out;

	if (MEDUSA_MONITORED_ACCESS_S(ipc_msgsnd_access, &task_security(current))) {
		process_kern2kobj(&process, current);
		/* 3-th argument is true: decrement IPC object's refcount in returned object */
		if (ipc_kern2kobj(&object, ipcp, true) == NULL)
			goto out;

		memset(&access, '\0', sizeof(struct ipc_msgsnd_access));
		access.m_type = msg->m_type;
		access.m_ts = msg->m_ts;
		access.msgflg = msgflg;
		access.ipc_class = object.ipc_class;

		retval = MED_DECIDE(ipc_msgsnd_access, &access, &process, &object);
		if (retval == MED_ERR)
			retval = MED_OK;
	}
out:
	/* second argument true: returns with locked IPC object */
	if (unlikely(ipc_putref(ipcp, true)))
		/* for now, we don't support error codes */
		retval = MED_NO;
	return retval;
}
__initcall(ipc_acctype_msgsnd_init);
