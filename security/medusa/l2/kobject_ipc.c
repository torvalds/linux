#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l1/ipc.h>
#include <linux/syscalls.h>
#include "../../../ipc/util.h" //TODO
#include "ipc_utils.h"
#include "kobject_ipc.h"

/*
* ipc_class = MED_IPC_SEM, MED_IPC_MSG, MED_IPC_SHM look l1/ipc.h
*/
MED_ATTRS(ipc_kobject) {
	MED_ATTR_RO		(ipc_kobject, ipc_class, "ipc_class", MED_UNSIGNED),
	MED_ATTR_RO		(ipc_kobject, id, "id", MED_SIGNED),
	MED_ATTR_END
};


int ipc_kern2kobj(struct ipc_kobject * ipck, struct kern_ipc_perm * ipcp)
{
	struct medusa_l1_ipc_s* security_s;
	security_s = (struct medusa_l1_ipc_s*) ipcp->security;
        memset(ipck, '\0', sizeof(struct ipc_kobject));
	
	if(!security_s)
		return -1;
	
	ipck->id = ipcp->id;
	ipck->ipc_class = security_s->ipc_class;
	COPY_MEDUSA_SUBJECT_VARS(ipck, security_s);
	COPY_MEDUSA_OBJECT_VARS(ipck, security_s);
	return 0;
}

medusa_answer_t ipc_kobj2kern(struct ipc_kobject * ipck, struct kern_ipc_perm * ipcp)
{
/*	struct medusa_l1_ipc_s* security_s;
	security_s = (struct medusa_l1_ipc_s*) ipcp->security;
	
	

	security_s->ipc_class = ipck->ipc_class;

	COPY_MEDUSA_SUBJECT_VARS(ipck, security_s);
	COPY_MEDUSA_OBJECT_VARS(ipck, security_s);
*/
	return MED_OK;
}

static struct ipc_kobject storage;

static struct medusa_kobject_s * ipc_fetch(struct medusa_kobject_s * kobj)
{
	struct ipc_kobject * ipc_kobj;
	struct kern_ipc_perm *ipcp;
	struct ipc_ids *ids;
	ipc_kobj = (struct ipc_kobject *)kobj;
	
	ids = medusa_get_ipc_ids(ipc_kobj->ipc_class);
	if(!ids)
		goto out_err;

	rcu_read_lock();

	ipcp = medusa_get_ipc_perm(ipc_kobj->id, ids);
	if(!ipcp)
		goto out_err_unlock;

	if(ipc_kern2kobj(&storage, ipcp) == 0){
		rcu_read_unlock();
		return (struct medusa_kobject_s *)&storage;
	}
out_err_unlock:
	rcu_read_unlock();
out_err:
	return (struct medusa_kobject_s *)kobj;
}

static medusa_answer_t ipc_update(struct medusa_kobject_s * kobj)
{
	struct ipc_kobject * ipc_kobj;
	struct kern_ipc_perm *ipcp;
	struct ipc_ids *ids;
	int retval;
	ipc_kobj = (struct ipc_kobject *)kobj;
	
	ids = medusa_get_ipc_ids(ipc_kobj->ipc_class);
	if(!ids)
		goto out_err;
	
	down_write(&(ids->rwsem));
	rcu_read_lock();

	ipcp = medusa_get_ipc_perm(ipc_kobj->id, ids);
	if(!ipcp)
		goto out_err_unlock;
	
	ipc_lock_object(ipcp);
	retval = ipc_kobj2kern(ipc_kobj, ipcp);
	ipc_unlock_object(ipcp);
	up_write(&(ids->rwsem));
	rcu_read_unlock();
	return retval;
out_err_unlock:
	up_write(&(ids->rwsem));
	rcu_read_unlock();
out_err:
	return MED_ERR;
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
