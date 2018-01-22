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

void medusa_ipc_info_unlock(struct kern_ipc_perm * ipcp)
{
	ipc_unlock(ipcp);
}

int medusa_ipc_info_lock(int id, unsigned int ipc_class, struct kern_ipc_perm * ipcp)
{
	struct ipc_namespace *ns;
	struct ipc_ids *ids;
	
	if (id < 0)
	{
		printk("kern_ipc_perm FAILED 0");
		goto out_err;		
	}
	printk("MEDUSAAAA: id: %d, class: %d", id, ipc_class);
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
			printk("kern_ipc_perm FAILED 1");
			goto out_err;		
		}
	}
	ipcp = ipc_lock(ids, id);

	if (IS_ERR(ipcp))
	{
		printk("kern_ipc_perm FAILED 2");
		goto out_err;
	}
	printk("kern_ipc_perm SUCCESS");
	return 0;

out_err:
	printk("kern_ipc_perm FAILED");
	return -EINVAL;
}
