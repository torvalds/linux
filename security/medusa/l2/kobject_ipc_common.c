#include <linux/msg.h>
#include "../../../ipc/util.h" //TODO
#include "ipc_utils.h"
#include "kobject_ipc_common.h"

MED_ATTRS(ipc_kobject) {
	MED_ATTR		(ipc_kobject, data, "data", MED_BYTES),
	MED_ATTR_END
};


/**
 * ipc_kern2kobj - convert function from kernel structure to kobject
 * @ipck - pointer to ipc_kobject where data will be stored 
 * @ipcp - pointer to kernel structure used to get data
 * Return: 0 on success, -1 on error
 */
int ipc_kern2kobj(struct ipc_kobject * ipck, struct kern_ipc_perm * ipcp)
{
	struct medusa_l1_ipc_s* security_s;
	unsigned int ipc_class;

	security_s = ipc_security(ipcp);
	ipc_class = security_s->ipc_class;
	switch(ipc_class){
		case MED_IPC_SEM: {
			struct ipc_sem_kobject *new_kobj;
			new_kobj = (struct ipc_sem_kobject *)ipc_sem_kern2kobj(ipcp);
			memcpy(ipck->data, (unsigned char *)new_kobj, sizeof(struct ipc_sem_kobject));
			break;
		}
		case MED_IPC_MSG:{
			struct ipc_msg_kobject *new_kobj;
			new_kobj = (struct ipc_msg_kobject *)ipc_msg_kern2kobj(ipcp);
			memcpy(ipck->data, (unsigned char *)new_kobj, sizeof(struct ipc_msg_kobject));
			break;
		}
		case MED_IPC_SHM:{
			struct ipc_shm_kobject *new_kobj;
			new_kobj = (struct ipc_shm_kobject *)ipc_shm_kern2kobj(ipcp);
			memcpy(ipck->data, (unsigned char *)new_kobj, sizeof(struct ipc_shm_kobject));
			break;
		}
		default:
			printk("Unkown ipc_class\n");
			return -1;		
	}
	return 0;
}

/**
 * ipc_fetch - common logic for fetching data from kernel
 * used by concrete fetch methods for example ipc_sem_fetch 
 * @id - id of ipc mechanism
 * @ipc_class - type of ipc mechanism define in l1/ipc.h
 * @ipc_concrete_kern2kobj - pointer to suitable convert function
 * Return: void pointer to memory area where kobject is stored or NULL on error
 */
void * ipc_fetch(unsigned int id, unsigned int ipc_class, void * (*ipc_concrete_kern2kobj)(struct kern_ipc_perm *))
{
	struct kern_ipc_perm *ipcp;
	struct ipc_ids *ids;
	void *new_kobj = NULL;

	ids = medusa_get_ipc_ids(ipc_class);
	if(!ids)
		goto out_err;

	rcu_read_lock();

	ipcp = ipc_obtain_object_check(ids, id);
	if(IS_ERR(ipcp) || !ipcp)
		goto out_unlock0;

	new_kobj = ipc_concrete_kern2kobj(ipcp);
out_unlock0:
	rcu_read_unlock();
out_err:
	return new_kobj;
}

/**
 * ipc_update - common logic for updating data in kernel by kobject data
 * used by concrete update methods for example ipc_sem_update 
 * @id - id of ipc mechanism
 * @ipc_class - type of ipc mechanism define in l1/ipc.h
 * @kobj - kobject which defines data to update
 * @ipc_concrete_kern2kobj - pointer to suitable convert function
 * Return: void pointer to memory area where kobject is stored or NULL on error
 */
medusa_answer_t ipc_update(unsigned int id, unsigned int ipc_class, struct medusa_kobject_s * kobj, int (*ipc_kobj2kern)(struct medusa_kobject_s *, struct kern_ipc_perm *))
{
	struct medusa_l1_ipc_s* security_s;
	struct kern_ipc_perm *ipcp;
	struct ipc_ids *ids;
	int retval = MED_ERR;
	
	ids = medusa_get_ipc_ids(ipc_class);
	if(!ids)
		goto out_err;
	
	
	rcu_read_lock();

	//Call inside RCU critical section
	//Object is not locked on exit
	ipcp = ipc_obtain_object_check(ids, id);
	if(IS_ERR(ipcp) || !ipcp)
		goto out_unlock0;

	if (!ipc_rcu_getref(ipcp)) {
		goto out_unlock0;
	}

	security_s = ipc_security(ipcp);

	ipc_lock_object(ipcp);

	//this update kernel structure	
	retval = ipc_kobj2kern(kobj, ipcp);

	ipc_unlock_object(ipcp);
	ipc_rcu_putref(ipcp, ipc_rcu_free);	
out_unlock0:
	rcu_read_unlock();
out_err:
	return retval;
}

MED_KCLASS(ipc_kobject) {
	MEDUSA_KCLASS_HEADER(ipc_kobject),
	"ipc",
	NULL,		/* init kclass */
	NULL,		/* destroy kclass */
	NULL,
	NULL,
	NULL,		/* unmonitor */
};

void ipc_kobject_rmmod(void);

int __init ipc_kobject_init(void) {
	MED_REGISTER_KCLASS(ipc_kobject);
	return 0;
}

__initcall(ipc_kobject_init);
