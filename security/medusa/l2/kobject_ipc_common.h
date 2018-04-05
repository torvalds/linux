#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/medusa/l3/kobject.h>
#include <linux/medusa/l3/model.h>
#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l3/constants.h>
#include <linux/medusa/l1/ipc.h>

/**
 * ipc_sem_kobject - kobject for System V sempahores 
 */
struct ipc_sem_kobject {	
	unsigned int ipc_class;
	int sem_nsems;
	MEDUSA_IPC_VARS;
	MEDUSA_OBJECT_VARS;
};
extern MED_DECLARE_KCLASSOF(ipc_sem_kobject);

/**
 * ipc_shm_kobject - kobject for System V shared memory 
 */
struct ipc_shm_kobject {	
	unsigned int ipc_class;
	MEDUSA_IPC_VARS;
	MEDUSA_OBJECT_VARS;
};
extern MED_DECLARE_KCLASSOF(ipc_shm_kobject);

/**
 * ipc_msg_kobject - kobject for System V message queues 
 */
struct ipc_msg_kobject {	
	unsigned int ipc_class;
	MEDUSA_IPC_VARS;
	MEDUSA_OBJECT_VARS;
};
extern MED_DECLARE_KCLASSOF(ipc_msg_kobject);

#define max_simple(max1, max2) ((max1 > max2) ? (max1) : (max2))

/**
 * ipc_kobject - kobject structure used for 
 * semaphores, message queues and shared memory kobject defined above
 * DON'T have update/fetch function
 * for simplicity authorization server can update/fetch just concrete kobjects
 * @data - byte array used to store data of concrete kobject
 * e.g. ipc_sem_kobject defined in kobject_ipc_sem.h
 */
struct ipc_kobject {	
	unsigned char data[max_simple(max_simple(sizeof(struct ipc_sem_kobject), sizeof(struct ipc_shm_kobject)), sizeof(struct ipc_msg_kobject))];	
	MEDUSA_OBJECT_VARS;
};
extern MED_DECLARE_KCLASSOF(ipc_kobject);

int ipc_kern2kobj(struct ipc_kobject * ipck, struct kern_ipc_perm * ipcp);

void * ipc_sem_kern2kobj(struct kern_ipc_perm * ipcp);
void * ipc_msg_kern2kobj(struct kern_ipc_perm * ipcp);
void * ipc_shm_kern2kobj(struct kern_ipc_perm * ipcp);

medusa_answer_t ipc_shm_kobj2kern(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp);
medusa_answer_t ipc_msg_kobj2kern(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp);
medusa_answer_t ipc_sem_kobj2kern(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp);

void * ipc_fetch(unsigned int id, unsigned int ipc_class, void * (*ipc_kern2kobj)(struct kern_ipc_perm *));
medusa_answer_t ipc_update(unsigned int id, unsigned int ipc_class, struct medusa_kobject_s * kobj, int (*ipc_kobj2kern)(struct medusa_kobject_s *, struct kern_ipc_perm *));
