#include <linux/msg.h>
#include "kobject_ipc_common.h"

/*
* ipc_class = MED_IPC_SEM, MED_IPC_MSG, MED_IPC_SHM look l1/ipc.h
*/
MED_ATTRS(ipc_msg_kobject) {
	MED_ATTR_RO		(ipc_msg_kobject, ipc_class, "ipc_class", MED_UNSIGNED),
	MED_ATTR_RO		(ipc_msg_kobject, id, "id", MED_SIGNED),
	MED_ATTR		(ipc_msg_kobject, uid, "uid", MED_UNSIGNED),
	MED_ATTR		(ipc_msg_kobject, gid, "gid", MED_UNSIGNED),
	MED_ATTR_END
};

static struct ipc_msg_kobject storage;

void * ipc_msg_kern2kobj(struct kern_ipc_perm * ipcp)
{
	struct medusa_l1_ipc_s* security_s;
	struct msg_queue * msg_queue;

	security_s = (struct medusa_l1_ipc_s*) ipcp->security;
	msg_queue = container_of(ipcp, struct msg_queue, q_perm);
	
    memset(&storage, '\0', sizeof(struct ipc_msg_kobject));
	
	if(!security_s)
		return NULL;
	
	storage.id = ipcp->id;
	storage.ipc_class = security_s->ipc_class;
	storage.uid = ipcp->uid;
	storage.gid = ipcp->gid;
	
	COPY_MEDUSA_OBJECT_VARS(&storage, security_s);

	return (void *)&storage;
}

medusa_answer_t ipc_msg_kobj2kern(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp)
{
	struct medusa_l1_ipc_s* security_s;
	struct ipc_msg_kobject * ipck_msg;

	ipck_msg = (struct ipc_msg_kobject *)ipck;
	security_s = (struct medusa_l1_ipc_s*) ipcp->security;

	ipcp->uid = ipck_msg->uid;
	ipcp->gid = ipck_msg->gid;

	COPY_MEDUSA_OBJECT_VARS(security_s, ipck_msg);
	return MED_OK;
}

static struct medusa_kobject_s * ipc_msg_fetch(struct medusa_kobject_s * kobj)
{
	struct ipc_msg_kobject * ipc_kobj;
	struct medusa_kobject_s * new_kobj;
	ipc_kobj = (struct ipc_msg_kobject *)kobj;
	new_kobj = (struct medusa_kobject_s *)ipc_fetch(ipc_kobj->id, ipc_kobj->ipc_class, ipc_msg_kern2kobj);
	return new_kobj;
}

static medusa_answer_t ipc_msg_update(struct medusa_kobject_s * kobj)
{
	struct ipc_msg_kobject * ipc_kobj;
	medusa_answer_t answer;
	ipc_kobj = (struct ipc_msg_kobject *)kobj;
	answer = ipc_update(ipc_kobj->id, ipc_kobj->ipc_class, kobj, ipc_msg_kobj2kern);
	return answer;
}

MED_KCLASS(ipc_msg_kobject) {
	MEDUSA_KCLASS_HEADER(ipc_msg_kobject),
	"ipc_msg",
	NULL,		/* init kclass */
	NULL,		/* destroy kclass */
	ipc_msg_fetch,
	ipc_msg_update,
	NULL,		/* unmonitor */
};

void ipc_msg_kobject_rmmod(void);

int __init ipc_msg_kobject_init(void) {
	MED_REGISTER_KCLASS(ipc_msg_kobject);
	return 0;
}

__initcall(ipc_msg_kobject_init);
