#include "../../../ipc/util.h" //TODO
#include "kobject_ipc_common.h"
#include "ipc_utils.h"

static struct medusa_kobject_s storage;

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
	printk("update 1\n");
	//down_write(&(ids->rwsem));
	
	printk("update 2\n");
	rcu_read_lock();

	printk("update 3\n");
	ipcp = ipc_obtain_object_check(ids, id);
	if(IS_ERR(ipcp) || !ipcp)
		goto out_err_unlock;
	
	printk("update 4\n");
	//ipc_lock_object(ipcp);
	printk("update 5\n");
	retval = ipc_kobj2kern(kobj, ipcp);
	printk("update 6\n");
	//ipc_unlock_object(ipcp);
	printk("update 7\n");
	//up_write(&(ids->rwsem));
	printk("update 8\n");
	rcu_read_unlock();
	printk("update 9\n");
	return retval;
out_err_unlock:
	//up_write(&(ids->rwsem));
	rcu_read_unlock();
out_err:
	return MED_ERR;
}
