#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l1/ipc.h>
#include <linux/init.h>
#include <linux/mm.h>
#include "kobject_ipc_common.h"
#include "kobject_ipc_sem.h"

struct ipc_event {
	MEDUSA_ACCESS_HEADER;
};

MED_ATTRS(ipc_event) {
	MED_ATTR_END
};

MED_EVTYPE(ipc_event, "ipc", ipc_sem_kobject, "sender", ipc_sem_kobject, "sender");

int __init ipc_evtype_init(void) {
	MED_REGISTER_EVTYPE(ipc_event, MEDUSA_EVTYPE_NOTTRIGGERED);
	return 0;
}

medusa_answer_t medusa_ipc_validate(struct kern_ipc_perm *ipcp)
	medusa_answer_t retval = MED_OK;
	struct ipc_event event;
	struct ipc_sem_kobject sender;

	memset(&event, '\0', sizeof(struct ipc_event));
	INIT_MEDUSA_OBJECT_VARS((struct medusa_l1_ipc_s*)ipcp->security);
	INIT_MEDUSA_SUBJECT_VARS((struct medusa_l1_ipc_s*)ipcp->security);
	
	ipc_sem_kern2kobj((struct medusa_kobject_s *)&sender, ipcp);
	retval = MED_DECIDE(ipc_event, &event, &sender, &sender);
	if (retval == MED_ERR)
		retval = MED_OK;
	
	return retval;
}
__initcall(ipc_evtype_init);
