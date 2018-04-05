#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l1/task.h>
#include <linux/medusa/l1/ipc.h>
#include <linux/init.h>
#include <linux/mm.h>
#include "kobject_process.h"
#include "kobject_ipc_common.h"
#include "evtype_ipc.h"

struct ipc_associate_access {
	MEDUSA_ACCESS_HEADER;
	unsigned int ipc_class;
	int flag;
};

MED_ATTRS(ipc_associate_access) {
	MED_ATTR_RO (ipc_associate_access, flag, "flag", MED_SIGNED),
	MED_ATTR_RO (ipc_associate_access, ipc_class, "ipc_class", MED_UNSIGNED),
	MED_ATTR_END
};

MED_ACCTYPE(ipc_associate_access, "ipc_associate", process_kobject, "process", ipc_kobject, "object");

int __init ipc_acctype_associate_init(void) {
	MED_REGISTER_ACCTYPE(ipc_associate_access,MEDUSA_ACCTYPE_TRIGGEREDATSUBJECT);
	return 0;
}

medusa_answer_t medusa_ipc_associate(struct kern_ipc_perm *ipcp, int flag)
{
	medusa_answer_t retval = MED_OK;
	struct ipc_associate_access access;
	struct process_kobject process;
	struct ipc_kobject object;
	printk("ASSOCIATE HOOK\n");	
    memset(&access, '\0', sizeof(struct ipc_associate_access));
    /* process_kobject parent is zeroed by process_kern2kobj function */

	if (!MED_MAGIC_VALID(&task_security(current)) && process_kobj_validate_task(current) <= 0)
		goto out_err;
	if (!MED_MAGIC_VALID(ipc_security(ipcp)) && medusa_ipc_validate(ipcp) <= 0)
		goto out_err;

	if (!VS_INTERSECT(VSS(&task_security(current)),VS(ipc_security(ipcp))) ||
		!VS_INTERSECT(VSW(&task_security(current)),VS(ipc_security(ipcp)))
	)
		return MED_NO;
	
	if (MEDUSA_MONITORED_ACCESS_S(ipc_associate_access, &task_security(current))) {
		access.flag = flag;
		access.ipc_class = ipc_security(ipcp)->ipc_class;

		process_kern2kobj(&process, current);
		if(ipc_kern2kobj(&object, ipcp) != 0)
			goto out_err;

		retval = MED_DECIDE(ipc_associate_access, &access, &process, &object);
		if (retval == MED_ERR)
			retval = MED_OK;
	}
out_err:
	return retval;
}
__initcall(ipc_acctype_associate_init);
