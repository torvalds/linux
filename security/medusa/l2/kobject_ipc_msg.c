#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l1/ipc.h>
#include <linux/syscalls.h>
#include <linux/msg.h>
#include "kobject_ipc_common.h"
#include "kobject_ipc_msg.h"

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


int ipc_msg_kern2kobj(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp)
{
	struct medusa_l1_ipc_s* security_s;
	struct ipc_msg_kobject * ipck_msg;
	struct msg_queue * msg_queue;

	security_s = (struct medusa_l1_ipc_s*) ipcp->security;
	ipck_msg = (struct ipc_msg_kobject *)ipck;
	msg_queue = container_of(ipcp, struct msg_queue, q_perm);
	
        memset(ipck_msg, '\0', sizeof(struct ipc_msg_kobject));
	
	if(!security_s)
		return -1;
	
	ipck_msg->id = ipcp->id;
	ipck_msg->ipc_class = security_s->ipc_class;
	ipck_msg->uid = ipcp->uid;
	ipck_msg->gid = ipcp->gid;
	
	COPY_MEDUSA_SUBJECT_VARS(ipck_msg, security_s);
	COPY_MEDUSA_OBJECT_VARS(ipck_msg, security_s);
	return 0;
}

medusa_answer_t ipc_msg_kobj2kern(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp)
{
	struct medusa_l1_ipc_s* security_s;
	struct ipc_msg_kobject * ipck_msg;

	ipck_msg = (struct ipc_msg_kobject *)ipck;
	security_s = (struct medusa_l1_ipc_s*) ipcp->security;

	ipcp->uid = ipck_msg->uid;
	ipcp->gid = ipck_msg->gid;

	COPY_MEDUSA_SUBJECT_VARS(security_s, ipck_msg);
	COPY_MEDUSA_OBJECT_VARS(security_s, ipck_msg);
	return MED_OK;
}

static struct medusa_kobject_s * ipc_msg_fetch(struct medusa_kobject_s * kobj)
{
	struct ipc_msg_kobject * ipc_kobj;
	struct medusa_kobject_s * new_kobj;
	ipc_kobj = (struct ipc_msg_kobject *)kobj;
	new_kobj = ipc_fetch(ipc_kobj->id, ipc_kobj->ipc_class, ipc_msg_kern2kobj);
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
