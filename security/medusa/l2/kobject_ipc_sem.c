#include <linux/sem.h>
#include "kobject_ipc_common.h"

MED_ATTRS(ipc_sem_kobject) {
	MED_ATTR_RO		(ipc_sem_kobject, ipc_class, "ipc_class", MED_UNSIGNED),
	MED_ATTR_RO		(ipc_sem_kobject, sem_nsems, "sem_nsems", MED_SIGNED),
	MED_ATTR_IPC	(ipc_sem_kobject),
	MED_ATTR_END
};

static struct ipc_sem_kobject storage;

/**
 * ipc_sem_kern2kobj - convert function from kernel structure to kobject
 * @ipcp - pointer to kernel structure used to get data
 * Return: void pointer to sem_kobject structure or NULL on error
 */
void * ipc_sem_kern2kobj(struct kern_ipc_perm * ipcp)
{
	struct medusa_l1_ipc_s* security_s;
	struct sem_array * sem_array;

	security_s = (struct medusa_l1_ipc_s*) ipcp->security;
	sem_array = container_of(ipcp, struct sem_array, sem_perm);
	
    memset(&storage, '\0', sizeof(struct ipc_sem_kobject));
	
	if(!security_s)
		return NULL;
	
	storage.ipc_class = security_s->ipc_class;
	storage.sem_nsems = sem_array->sem_nsems;
	
	COPY_WRITE_IPC_VARS(&(storage.ipc_perm), ipcp);
	COPY_READ_IPC_VARS(&(storage.ipc_perm), ipcp);
	COPY_MEDUSA_OBJECT_VARS(&storage, security_s);
	return (void *)&storage;
}

medusa_answer_t ipc_sem_kobj2kern(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp)
{
	struct medusa_l1_ipc_s* security_s;
	struct ipc_sem_kobject * ipck_sem;

	ipck_sem = (struct ipc_sem_kobject *)ipck;
	security_s = (struct medusa_l1_ipc_s*) ipcp->security;

	COPY_WRITE_IPC_VARS(ipcp, &ipck_sem->ipc_perm);
	COPY_MEDUSA_OBJECT_VARS(security_s, ipck_sem);
	MED_MAGIC_VALIDATE(security_s);
	return MED_OK;
}

static struct medusa_kobject_s * ipc_sem_fetch(struct medusa_kobject_s * kobj)
{
	struct ipc_sem_kobject * ipc_kobj;
	struct medusa_kobject_s * new_kobj;
	ipc_kobj = (struct ipc_sem_kobject *)kobj;
	new_kobj = (struct medusa_kobject_s *)ipc_fetch(ipc_kobj->ipc_perm.id, ipc_kobj->ipc_class, ipc_sem_kern2kobj);
	return new_kobj;
}

static medusa_answer_t ipc_sem_update(struct medusa_kobject_s * kobj)
{
	struct ipc_sem_kobject * ipc_kobj;
	medusa_answer_t answer;
	ipc_kobj = (struct ipc_sem_kobject *)kobj;
	answer = ipc_update(ipc_kobj->ipc_perm.id, ipc_kobj->ipc_class, kobj, ipc_sem_kobj2kern);
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
