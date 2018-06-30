#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l1/ipc.h>
#include <linux/init.h>
#include <linux/mm.h>
#include "evtype_ipc.h"
#include "kobject_ipc_common.h"

struct ipc_event {
	MEDUSA_ACCESS_HEADER;
	unsigned int ipc_class;
};

MED_ATTRS(ipc_event) {
	MED_ATTR_RO (ipc_event, ipc_class, "ipc_class", MED_UNSIGNED),
	MED_ATTR_END
};

MED_EVTYPE(ipc_event, "getipc", ipc_kobject, "sender", ipc_kobject, "sender");

int __init ipc_evtype_init(void) {
	MED_REGISTER_EVTYPE(ipc_event, MEDUSA_EVTYPE_NOTTRIGGERED);
	return 0;
}

/*
 * This routine expects the existing, but !MED_MAGIC_VALID Medusa ipcp security struct!
 */
int medusa_ipc_validate(struct kern_ipc_perm *ipcp) {
	medusa_answer_t retval;
	struct ipc_event event;
	struct ipc_kobject sender;

	memset(&event, '\0', sizeof(struct ipc_event));
	INIT_MEDUSA_OBJECT_VARS(ipc_security(ipcp));
	ipc_kern2kobj(&sender, ipcp);
	retval = MED_DECIDE(ipc_event, &event, &sender, &sender);
	if (retval != MED_ERR)
		return 1;
	return MED_ERR;
}

__initcall(ipc_evtype_init);
