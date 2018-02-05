#include <linux/spinlock.h>
#include <linux/nsproxy.h>
#include <linux/ipc_namespace.h>
#include <linux/medusa/l1/ipc.h>
#include "../../../ipc/util.h" //TODO
#include "ipc_utils.h"

//from shm.c,sem.c,msg.c not present in header files
#define shm_ids(ns)	((ns)->ids[IPC_SHM_IDS])
#define sem_ids(ns)	((ns)->ids[IPC_SEM_IDS])
#define msg_ids(ns)	((ns)->ids[IPC_MSG_IDS])


struct kern_ipc_perm * medusa_ipc_info_lock(int id, unsigned int ipc_class)
{
	struct ipc_namespace *ns;
	struct kern_ipc_perm *ipcp;
	struct ipc_ids *ids;
	
	if (id < 0)
	{
		printk("kern_ipc_perm FAILED 0\n");
		return -EINVAL;		
	}
	printk("MEDUSAAAA: id: %d, class: %d\n", id, ipc_class);
	ns = current->nsproxy->ipc_ns;

	switch(ipc_class){
		case MED_IPC_SEM: {
			ids = &sem_ids(ns);
			break;
		}
		case MED_IPC_MSG: {
			ids = &msg_ids(ns);
			break;
		}
		case MED_IPC_SHM: {
			ids = &shm_ids(ns);
			break;
		}
		default: {
			printk("kern_ipc_perm FAILED 1\n");
			return -EINVAL;		
		}
	}

	printk("kern_ipc_perm - before rcu read lock\n");
	rcu_read_lock();
	printk("kern_ipc_perm - before obtain\n");
	ipcp = ipc_obtain_object_check(ids, id);
	printk("kern_ipc_perm - after ipc obtain\n");

	if (IS_ERR(ipcp))
	{
		printk("kern_ipc_perm FAILED 2\n");
		goto out_err;
	}
	if (!ipcp)
	{
		printk("kern_ipc_perm FAILED 3 - it is null probably\n");
	}

	printk("MEDUSAAAA: id from object: %d\n", ipcp->id);

	printk("kern_ipc_perm SUCCESS  - before locks\n");
	rcu_read_unlock();
	printk("kern_ipc_perm SUCCESS - after locks\n");
	return ipcp;
out_err:
	printk("kern_ipc_perm FAILED\n");
	rcu_read_unlock();
	return NULL;
}
