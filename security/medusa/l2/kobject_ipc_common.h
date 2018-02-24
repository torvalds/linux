#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/medusa/l3/kobject.h>
#include <linux/medusa/l3/constants.h>

struct ipc_sem_kobject {	
	unsigned int ipc_class;
	int id;
	kuid_t uid;
	kgid_t gid;
	int sem_nsems;
	MEDUSA_OBJECT_VARS;
	MEDUSA_SUBJECT_VARS;
};
extern MED_DECLARE_KCLASSOF(ipc_sem_kobject);

struct ipc_shm_kobject {	
	unsigned int ipc_class;
	int id;
	kuid_t uid;
	kgid_t gid;
	MEDUSA_OBJECT_VARS;
	MEDUSA_SUBJECT_VARS;
};
extern MED_DECLARE_KCLASSOF(ipc_shm_kobject);

struct ipc_msg_kobject {	
	unsigned int ipc_class;
	int id;
	kuid_t uid;
	kgid_t gid;
	MEDUSA_OBJECT_VARS;
	MEDUSA_SUBJECT_VARS;
};
extern MED_DECLARE_KCLASSOF(ipc_msg_kobject);

struct ipc_kobject {	
	unsigned char data[sizeof(struct ipc_sem_kobject)];	
	MEDUSA_OBJECT_VARS;
	MEDUSA_SUBJECT_VARS;
};
extern MED_DECLARE_KCLASSOF(ipc_kobject);

int ipc_kern2kobj(struct ipc_kobject * ipck, struct kern_ipc_perm * ipcp);

int ipc_sem_kern2kobj(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp);
int ipc_msg_kern2kobj(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp);
int ipc_shm_kern2kobj(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp);

medusa_answer_t ipc_shm_kobj2kern(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp);
medusa_answer_t ipc_msg_kobj2kern(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp);
medusa_answer_t ipc_sem_kobj2kern(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp);

struct medusa_kobject_s * ipc_fetch(unsigned int id, unsigned int ipc_class, int (*ipc_kern2kobj)(struct medusa_kobject_s *, struct kern_ipc_perm *));
medusa_answer_t ipc_update(unsigned int id, unsigned int ipc_class, struct medusa_kobject_s * kobj, int (*ipc_kobj2kern)(struct medusa_kobject_s *, struct kern_ipc_perm *));
