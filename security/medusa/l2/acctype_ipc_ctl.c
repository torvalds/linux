#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l1/task.h>
#include <linux/medusa/l1/ipc.h>
#include <linux/init.h>
#include <linux/mm.h>
#include "kobject_process.h"
#include "kobject_ipc_common.h"
#include "evtype_ipc.h"

struct ipc_ctl_access {
	MEDUSA_ACCESS_HEADER;
	unsigned int ipc_class;
	int cmd;
};

MED_ATTRS(ipc_ctl_access) {
	MED_ATTR_RO (ipc_ctl_access, cmd, "cmd", MED_SIGNED),
	MED_ATTR_RO (ipc_ctl_access, ipc_class, "ipc_class", MED_UNSIGNED),
	MED_ATTR_END
};

MED_ACCTYPE(ipc_ctl_access, "ipc_ctl", process_kobject, "process", ipc_kobject, "object");

int __init ipc_acctype_ctl_init(void) {
	MED_REGISTER_ACCTYPE(ipc_ctl_access,MEDUSA_ACCTYPE_TRIGGEREDATSUBJECT);
	return 0;
}

/*
 * Check permission when an IPC object (semaphore, message queue, shared memory)
 * operation specified by @cmd is to be performed on the IPC object which
 * kernel ipc permission @ipcp is given. The @ipcp may be NULL, e.g. for
 * IPC_INFO or MSG_INFO or SEM_INFO or SHM_INFO @cmd value.
 * @ipcp contains kernel IPC permission of the related IPC object
 * @cmd contains the operation to be performed
 */
medusa_answer_t medusa_ipc_ctl(struct kern_ipc_perm *ipcp, int cmd)
{
	medusa_answer_t retval = MED_OK;
	struct ipc_ctl_access access;
	struct process_kobject process;
	struct ipc_kobject object, *object_p = NULL;

	memset(&access, '\0', sizeof(struct ipc_ctl_access));
	/* process_kobject parent is zeroed by process_kern2kobj function */

	if (!MED_MAGIC_VALID(&task_security(current)) && process_kobj_validate_task(current) <= 0)
		goto out;

	/* 'ipcp' is NULL in case of 'cmd': IPC_INFO, MSG_INFO, SEM_INFO, SHM_INFO */
	if(likely(ipcp)) {
		object_p = &object;
		if (!MED_MAGIC_VALID(ipc_security(ipcp)) && medusa_ipc_validate(ipcp) <= 0)
			goto out;
	}

	if (MEDUSA_MONITORED_ACCESS_S(ipc_ctl_access, &task_security(current))) {
		access.cmd = cmd;
		access.ipc_class = MED_IPC_UNDEFINED;

		process_kern2kobj(&process, current);
		if (likely(object_p)) {
			access.ipc_class = ipc_security(ipcp)->ipc_class;
			if (ipc_kern2kobj(object_p, ipcp) <= 0)
				goto out;
		}

		/* in case of NULL 'ipcp', 'object_p' is NULL too */
		retval = MED_DECIDE(ipc_ctl_access, &access, &process, object_p);
		if (retval == MED_ERR)
			retval = MED_OK;
	}
out:
	return retval;
}
__initcall(ipc_acctype_ctl_init);
