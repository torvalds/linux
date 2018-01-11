#include <linux/medusa/l3/registry.h>
#include "kobject_ipc.h"
#include <linux/medusa/l1/ipc.h>
#include <linux/init.h>
#include <linux/mm.h>

/* let's define the 'fork' access type, with object=task and subject=task. */

struct ipc_access {
	MEDUSA_ACCESS_HEADER;
	//u32 perms;
};

MED_ATTRS(ipc_access) {
	//MED_ATTR_RO (ipc_access, perms, "perms", MED_UNSIGNED),
	MED_ATTR_END
};

MED_ACCTYPE(ipc_access, "ipc", ipc_kobject, "sender", ipc_kobject, "sender");

int __init ipc_acctype_init(void) {
	MED_REGISTER_ACCTYPE(ipc_access,MEDUSA_ACCTYPE_TRIGGEREDATOBJECT);
	return 0;
}

medusa_answer_t medusa_ipc_perm(struct kern_ipc_perm *ipcp, u32 perms)
{
	medusa_answer_t retval = MED_OK;
	struct ipc_access access;
	struct ipc_kobject sender;

        memset(&access, '\0', sizeof(struct ipc_access));
	INIT_MEDUSA_OBJECT_VARS((struct medusa_l1_ipc_s*)ipcp->security);
	INIT_MEDUSA_SUBJECT_VARS((struct medusa_l1_ipc_s*)ipcp->security);
	if (MEDUSA_MONITORED_ACCESS_O(ipc_access, (struct medusa_l1_ipc_s*)ipcp->security)) {
		//access.perms = perms;
		ipc_kern2kobj(&sender, ipcp);
		retval = MED_DECIDE(ipc_access, &access, &sender, &sender);
		if (retval == MED_ERR)
			retval = MED_OK;
	}
	return retval;
}
__initcall(ipc_acctype_init);
