#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l1/ipc.h>
#include "kobject_ipc.h"

/*
* sclass = MED_IPC_SEM, MED_IPC_MSG
*/
MED_ATTRS(ipc_kobject) {
	MED_ATTR		(ipc_kobject, sid, "sid", MED_UNSIGNED),
	MED_ATTR		(ipc_kobject, sclass, "sclass", MED_UNSIGNED),
	MED_ATTR_END
};


int ipc_kern2kobj(struct ipc_kobject * ipck, struct kern_ipc_perm * ipcp)
{
	struct medusa_l1_ipc_s* security_s;
	security_s = (struct medusa_l1_ipc_s*) ipcp->security;
	if(!security_s)
		return -1;
	ipck->sid = security_s->sid;
	ipck->sclass = security_s->sclass;
	return 0;
}

static struct medusa_kobject_s * ipc_fetch(struct medusa_kobject_s * kobj)
{
}

static medusa_answer_t ipc_update(struct medusa_kobject_s * kobj)
{
}

MED_KCLASS(ipc_kobject) {
	MEDUSA_KCLASS_HEADER(ipc_kobject),
	"ipc",
	NULL,		/* init kclass */
	NULL,		/* destroy kclass */
	ipc_fetch,
	ipc_update,
	NULL,		/* unmonitor */
};

void ipc_kobject_rmmod(void);

int __init ipc_kobject_init(void) {
	MED_REGISTER_KCLASS(ipc_kobject);
	return 0;
}

__initcall(ipc_kobject_init);
