#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l1/task.h>
#include <linux/medusa/l1/ipc.h>
#include <linux/init.h>
#include <linux/mm.h>
#include "kobject_process.h"
#include "kobject_ipc_common.h"
#include "evtype_ipc.h"

struct ipc_msgrcv_access {
	MEDUSA_ACCESS_HEADER;
	long m_type;	/* message type;  see 'struct msg_msg' in include/linux/msg.h */
	size_t m_ts;	/* msg text size; see 'struct msg_msg' in include/linux/msg.h */
	/* TODO char m_text[???]; send also message text? */
	pid_t target; /* TODO: namespaces implementation??? */
	long type;
	int mode;
	unsigned int ipc_class;
};

MED_ATTRS(ipc_msgrcv_access) {
	MED_ATTR_RO (ipc_msgrcv_access, m_type, "m_type", MED_SIGNED),
	MED_ATTR_RO (ipc_msgrcv_access, m_ts, "m_ts", MED_UNSIGNED),
	MED_ATTR_RO (ipc_msgrcv_access, target, "target", MED_SIGNED),
	MED_ATTR_RO (ipc_msgrcv_access, type, "type", MED_SIGNED),
	MED_ATTR_RO (ipc_msgrcv_access, mode, "mode", MED_SIGNED),
	MED_ATTR_RO (ipc_msgrcv_access, ipc_class, "ipc_class", MED_UNSIGNED),
	MED_ATTR_END
};

MED_ACCTYPE(ipc_msgrcv_access, "ipc_msgrcv", process_kobject, "process", ipc_kobject, "object");

int __init ipc_acctype_msgrcv_init(void) {
	MED_REGISTER_ACCTYPE(ipc_msgrcv_access,MEDUSA_ACCTYPE_TRIGGEREDATSUBJECT);
	return 0;
}

/*
 * Check permission before a message @msg is removed from the message queue,
 * which kernel permission structure @ipcp is given. The @target task structure
 * contains a pointer to the process that will be receiving the message
 * (not equal to the current process when inline receives are being performed).
 * @ipcp contains kernel permission structure for message queue
 * @msg contains the message destination
 * @target contains the task structure for recipient process
 * @type contains the type of message requested
 * @mode contains the operational flags
 */
medusa_answer_t medusa_ipc_msgrcv(struct kern_ipc_perm *ipcp, struct msg_msg *msg, struct task_struct *target, long type, int mode)
{
	medusa_answer_t retval = MED_OK;
	struct ipc_msgrcv_access access;
	struct process_kobject process;
	struct ipc_kobject object;
	
	memset(&access, '\0', sizeof(struct ipc_msgrcv_access));
	/* process_kobject parent is zeroed by process_kern2kobj function */

	if (!MED_MAGIC_VALID(&task_security(current)) && process_kobj_validate_task(current) <= 0)
		goto out;
	if (!MED_MAGIC_VALID(ipc_security(ipcp)) && medusa_ipc_validate(ipcp) <= 0)
		goto out;

	if (MEDUSA_MONITORED_ACCESS_S(ipc_msgrcv_access, &task_security(current))) {
		access.m_type = msg->m_type;
		access.m_ts = msg->m_ts;
		access.type = type;
		access.mode = mode;
		access.target = target->pid;
		access.ipc_class = ipc_security(ipcp)->ipc_class;

		process_kern2kobj(&process, current);
		if (ipc_kern2kobj(&object, ipcp) == NULL)
			goto out;

		retval = MED_DECIDE(ipc_msgrcv_access, &access, &process, &object);
		if (retval == MED_ERR)
			retval = MED_OK;
	}
out:
	return retval;
}
__initcall(ipc_acctype_msgrcv_init);
