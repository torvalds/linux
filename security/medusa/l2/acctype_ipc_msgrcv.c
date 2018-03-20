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
	unsigned int ipc_class;
	long type;
	int mode;
	pid_t recipient_pid;
};

MED_ATTRS(ipc_msgrcv_access) {
	MED_ATTR_RO (ipc_msgrcv_access, mode, "mode", MED_SIGNED),
	MED_ATTR_RO (ipc_msgrcv_access, type, "type", MED_SIGNED),
	MED_ATTR_RO (ipc_msgrcv_access, recipient_pid, "recipient_pid", MED_SIGNED),
	MED_ATTR_RO (ipc_msgrcv_access, ipc_class, "ipc_class", MED_UNSIGNED),
	MED_ATTR_END
};

MED_ACCTYPE(ipc_msgrcv_access, "ipc_msgrcv", process_kobject, "process", ipc_kobject, "object");

int __init ipc_acctype_msgrcv_init(void) {
	MED_REGISTER_ACCTYPE(ipc_msgrcv_access,MEDUSA_ACCTYPE_TRIGGEREDATSUBJECT);
	return 0;
}

medusa_answer_t medusa_ipc_msgrcv(struct kern_ipc_perm *ipcp, struct msg_msg *msg, struct task_struct *target, long type, int mode)
{
	medusa_answer_t retval = MED_OK;
	struct ipc_msgrcv_access access;
	struct process_kobject process;
	struct ipc_kobject object;
    memset(&access, '\0', sizeof(struct ipc_msgrcv_access));
    /* process_kobject parent is zeroed by process_kern2kobj function */

	if (!MED_MAGIC_VALID(&task_security(current)) && process_kobj_validate_task(current) <= 0)
		goto out_err;
	if (!MED_MAGIC_VALID(ipc_security(ipcp)) && medusa_ipc_validate(ipcp) <= 0)
		goto out_err;

	if (MEDUSA_MONITORED_ACCESS_S(ipc_msgrcv_access, &task_security(current))) {
		access.type = type;
		access.mode = mode;
		access.recipient_pid = target->pid;
		access.ipc_class = ipc_security(ipcp)->ipc_class;

		process_kern2kobj(&process, current);
		if(ipc_kern2kobj(&object, ipcp) != 0)
			goto out_err;

		retval = MED_DECIDE(ipc_msgrcv_access, &access, &process, &object);
		if (retval == MED_ERR)
			retval = MED_OK;
	}
out_err:
	return retval;
}
__initcall(ipc_acctype_msgrcv_init);
