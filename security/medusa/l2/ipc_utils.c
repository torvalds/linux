#include <linux/nsproxy.h>
#include <linux/security.h>
#include <linux/mm.h>
#include <linux/shm.h>
#include <linux/sem.h>
#include <linux/msg.h>
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
 * @ipc_class - type of ipc mechanism define in l1/ipc.h
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

void ipc_rcu_free(struct rcu_head *head)
{
	struct kern_ipc_perm *p = container_of(head, struct kern_ipc_perm, rcu);
	struct medusa_l1_ipc_s* security_s;
	unsigned int ipc_class;

	security_s = ipc_security(p);
	ipc_class = security_s->ipc_class;
	
	switch(ipc_class){
		case MED_IPC_SEM: {
			struct sem_array *sem = container_of(p, struct sem_array, sem_perm);
			security_sem_free(sem);
			kvfree(sem);
			break;
		}
		case MED_IPC_MSG: {
			struct msg_queue *msq = container_of(p, struct msg_queue, q_perm);
			security_msg_queue_free(msq);
			kvfree(msq);
			break;
		}
		case MED_IPC_SHM: {
			struct shmid_kernel *shm = container_of(p, struct shmid_kernel, shm_perm);
			security_shm_free(shm);
			kvfree(shm);
			break;
		}
		default: {
			printk("Unkown ipc_class\n");
		}
	}
	return;
}
