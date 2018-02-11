#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l1/ipc.h>
#include <linux/syscalls.h>
#include <linux/sem.h>
#include "kobject_ipc_common.h"
#include "kobject_ipc_sem.h"

/*
* ipc_class = MED_IPC_SEM, MED_IPC_MSG, MED_IPC_SHM look l1/ipc.h
*/
MED_ATTRS(ipc_sem_kobject) {
	MED_ATTR_RO		(ipc_sem_kobject, ipc_class, "ipc_class", MED_UNSIGNED),
	MED_ATTR_RO		(ipc_sem_kobject, id, "id", MED_SIGNED),
	MED_ATTR		(ipc_sem_kobject, uid, "uid", MED_UNSIGNED),
	MED_ATTR		(ipc_sem_kobject, gid, "gid", MED_UNSIGNED),
	MED_ATTR_RO		(ipc_sem_kobject, sem_nsems, "sem_nsems", MED_SIGNED),
	MED_ATTR_END
};


int ipc_sem_kern2kobj(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp)
{
	struct medusa_l1_ipc_s* security_s;
	struct ipc_sem_kobject * ipck_sem;
	struct sem_array * sem_array;

	security_s = (struct medusa_l1_ipc_s*) ipcp->security;
	ipck_sem = (struct ipc_sem_kobject *)ipck;
	sem_array = container_of(ipcp, struct sem_array, sem_perm);
	
        memset(ipck_sem, '\0', sizeof(struct ipc_sem_kobject));
	
	if(!security_s)
		return -1;
	
	ipck_sem->id = ipcp->id;
	ipck_sem->ipc_class = security_s->ipc_class;
	ipck_sem->uid = ipcp->uid;
	ipck_sem->gid = ipcp->gid;
	ipck_sem->sem_nsems = sem_array->sem_nsems;
	COPY_MEDUSA_SUBJECT_VARS(ipck_sem, security_s);
	COPY_MEDUSA_OBJECT_VARS(ipck_sem, security_s);
	return 0;
}

medusa_answer_t ipc_sem_kobj2kern(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp)
{
	struct medusa_l1_ipc_s* security_s;
	struct ipc_sem_kobject * ipck_sem;

	ipck_sem = (struct ipc_sem_kobject *)ipck;
	security_s = (struct medusa_l1_ipc_s*) ipcp->security;

	ipcp->uid = ipck_sem->uid;
	ipcp->gid = ipck_sem->gid;

	COPY_MEDUSA_SUBJECT_VARS(security_s, ipck_sem);
	COPY_MEDUSA_OBJECT_VARS(security_s, ipck_sem);
	return MED_OK;
}

static struct medusa_kobject_s * ipc_sem_fetch(struct medusa_kobject_s * kobj)
{
	struct ipc_sem_kobject * ipc_kobj;
	struct medusa_kobject_s * new_kobj;
	ipc_kobj = (struct ipc_sem_kobject *)kobj;
	new_kobj = ipc_fetch(ipc_kobj->id, ipc_kobj->ipc_class, ipc_sem_kern2kobj);
	return new_kobj;
}

static medusa_answer_t ipc_sem_update(struct medusa_kobject_s * kobj)
{
	struct ipc_sem_kobject * ipc_kobj;
	medusa_answer_t answer;
	ipc_kobj = (struct ipc_sem_kobject *)kobj;
	answer = ipc_update(ipc_kobj->id, ipc_kobj->ipc_class, kobj, ipc_sem_kobj2kern);
	return answer;
}

MED_KCLASS(ipc_sem_kobject) {
	MEDUSA_KCLASS_HEADER(ipc_sem_kobject),
	"ipc_sem",
	NULL,		/* init kclass */
	NULL,		/* destroy kclass */
	ipc_sem_fetch,
	ipc_sem_update,
	NULL,		/* unmonitor */
};

void ipc_sem_kobject_rmmod(void);

int __init ipc_sem_kobject_init(void) {
	MED_REGISTER_KCLASS(ipc_sem_kobject);
	return 0;
}

__initcall(ipc_sem_kobject_init);
