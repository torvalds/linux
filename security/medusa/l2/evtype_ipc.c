#include <linux/medusa/l3/registry.h>
#include "kobject_ipc_sem.h"
#include <linux/medusa/l1/ipc.h>
#include <linux/init.h>
#include <linux/mm.h>

/* let's define the 'fork' access type, with object=task and subject=task. */

struct ipc_access {
	MEDUSA_ACCESS_HEADER;
};

MED_ATTRS(ipc_access) {
	MED_ATTR_END
};

MED_ACCTYPE(ipc_access, "ipc", ipc_sem_kobject, "sender", ipc_sem_kobject, "sender");

int __init ipc_acctype_init(void) {
	MED_REGISTER_ACCTYPE(ipc_access,MEDUSA_ACCTYPE_TRIGGEREDATOBJECT);
	return 0;
}

medusa_answer_t medusa_ipc_perm(struct kern_ipc_perm *ipcp, u32 perms)
{
	medusa_answer_t retval = MED_OK;
	struct ipc_access access;
	struct ipc_sem_kobject sender;

        memset(&access, '\0', sizeof(struct ipc_access));
	INIT_MEDUSA_OBJECT_VARS((struct medusa_l1_ipc_s*)ipcp->security);
	INIT_MEDUSA_SUBJECT_VARS((struct medusa_l1_ipc_s*)ipcp->security);
	if (MEDUSA_MONITORED_ACCESS_O(ipc_access, (struct medusa_l1_ipc_s*)ipcp->security)) {
		ipc_sem_kern2kobj((struct medusa_kobject_s *)&sender, ipcp);
		printk("medusa_ipc_perm before decide\n");
		retval = MED_DECIDE(ipc_access, &access, &sender, &sender);
		printk("medusa_ipc_perm after decide\n");
		if (retval == MED_ERR)
			retval = MED_OK;
	}
	return retval;
}
__initcall(ipc_acctype_init);
