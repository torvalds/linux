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

/**
 * medusa_get_ipc_ids - retrieve ipc_ids structure from namespace
 * @ipc_class - type of ipc mechanizme define in l1/ipc.h
 * Return: ipc_ids or NULL if error
 */
struct ipc_ids * medusa_get_ipc_ids(unsigned int ipc_class)
{
	struct ipc_namespace *ns;
	struct ipc_ids *ids;
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
			printk("Unkown ipc_class\n");
			return NULL;		
		}
	}
	return ids;
}
