#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l1/ipc.h>
#include "../../../ipc/util.h" //TODO
#include "kobject_ipc_common.h"
#include "ipc_utils.h"

MED_ATTRS(ipc_kobject) {
	MED_ATTR		(ipc_kobject, data, "data", MED_BYTES),
	MED_ATTR_END
};

static struct medusa_kobject_s storage;

int ipc_kern2kobj(struct ipc_kobject * ipck, struct kern_ipc_perm * ipcp)
{
	struct medusa_l1_ipc_s* security_s;
	struct medusa_kobject_s concrete_ipc_object;
	unsigned int ipc_class;

	security_s = (struct medusa_l1_ipc_s*) ipcp->security;
	ipc_class = security_s->ipc_class;
	printk("kern2kobj: IPC_CLASS:%d", ipc_class);
	switch(ipc_class){
		case MED_IPC_SEM: 
			ipc_sem_kern2kobj(&concrete_ipc_object, ipcp);
			printk("before memcpy sem concrete: %p, size: %d\n", concrete_ipc_object, sizeof(struct ipc_sem_kobject));
			//memcpy(ipck->data, (unsigned char *)&concrete_ipc_object, sizeof(struct ipc_sem_kobject));
			break;
		case MED_IPC_MSG: 
			ipc_msg_kern2kobj(&concrete_ipc_object, ipcp);
			printk("before memcpy msg\n");
			//memcpy(ipck->data, (unsigned char *)&concrete_ipc_object, sizeof(struct ipc_msg_kobject));
			break;
		case MED_IPC_SHM: 
			ipc_shm_kern2kobj(&concrete_ipc_object, ipcp);
			printk("before memcpy shm 2\n");
			printk("before memcpy shm concrete: %p, size: %d\n", concrete_ipc_object, sizeof(struct ipc_shm_kobject));
			//memcpy(ipck->data, (unsigned char *)&concrete_ipc_object, sizeof(struct ipc_shm_kobject));
			break;
		default: {
			printk("Unkown ipc_class\n");
			return -1;		
		}
	}
	printk("ipc_kern2kobj end return 0\n");
	return 0;
}

struct medusa_kobject_s * ipc_fetch(unsigned int id, unsigned int ipc_class, int (*ipc_kern2kobj)(struct medusa_kobject_s *, struct kern_ipc_perm *))
{
	struct kern_ipc_perm *ipcp;
	struct ipc_ids *ids;

	ids = medusa_get_ipc_ids(ipc_class);
	if(!ids)
		goto out_err;

	rcu_read_lock();

	ipcp = ipc_obtain_object_check(ids, id);
	if(IS_ERR(ipcp) || !ipcp)
		goto out_err_unlock;

	if(ipc_kern2kobj(&storage, ipcp) == 0){
		rcu_read_unlock();
		return &storage;
	}
out_err_unlock:
	rcu_read_unlock();
out_err:
	return NULL;
}

medusa_answer_t ipc_update(unsigned int id, unsigned int ipc_class, struct medusa_kobject_s * kobj, int (*ipc_kobj2kern)(struct medusa_kobject_s *, struct kern_ipc_perm *))
{
	struct kern_ipc_perm *ipcp;
	struct ipc_ids *ids;
	int retval;
	
	ids = medusa_get_ipc_ids(ipc_class);
	if(!ids)
		goto out_err;
	//down_write(&(ids->rwsem));
	
	rcu_read_lock();

	ipcp = ipc_obtain_object_check(ids, id);
	if(IS_ERR(ipcp) || !ipcp)
		goto out_err_unlock;
	
	//ipc_lock_object(ipcp);
	retval = ipc_kobj2kern(kobj, ipcp);
	//ipc_unlock_object(ipcp);
	//up_write(&(ids->rwsem));
	rcu_read_unlock();
	return retval;
out_err_unlock:
	//up_write(&(ids->rwsem));
	rcu_read_unlock();
out_err:
	return MED_ERR;
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
