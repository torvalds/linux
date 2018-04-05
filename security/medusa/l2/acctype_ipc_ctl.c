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

medusa_answer_t medusa_ipc_ctl(struct kern_ipc_perm *ipcp, int cmd)
{
	medusa_answer_t retval = MED_OK;
	struct ipc_ctl_access access;
	struct process_kobject process;
	struct ipc_kobject object;
	unsigned int info_flag = 0;
    memset(&access, '\0', sizeof(struct ipc_ctl_access));
    /* process_kobject parent is zeroed by process_kern2kobj function */

	if (!MED_MAGIC_VALID(&task_security(current)) && process_kobj_validate_task(current) <= 0)
		goto out_err;
	if(ipcp == NULL && (cmd == IPC_INFO || cmd == MSG_INFO || cmd == SEM_INFO || cmd == SHM_INFO)) {
		info_flag = 1;
	} 
	else {
		if (!MED_MAGIC_VALID(ipc_security(ipcp)) && medusa_ipc_validate(ipcp) <= 0)
			goto out_err;
	}
	if (MEDUSA_MONITORED_ACCESS_S(ipc_ctl_access, &task_security(current))) {
		access.cmd = cmd;
		process_kern2kobj(&process, current);
		if(info_flag == 0) {
			access.ipc_class = ipc_security(ipcp)->ipc_class;
			if(ipc_kern2kobj(&object, ipcp) != 0)
				goto out_err;
			retval = MED_DECIDE(ipc_ctl_access, &access, &process, &object);
		}else{
			access.ipc_class = MED_IPC_UNDEFINED;
			retval = MED_DECIDE(ipc_ctl_access, &access, &process, NULL);
		}

		if (retval == MED_ERR)
			retval = MED_OK;
	}
out_err:
	return retval;
}
__initcall(ipc_acctype_ctl_init);
