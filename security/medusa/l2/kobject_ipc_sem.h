#include <linux/sched.h>
#include <linux/medusa/l3/registry.h>

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


int ipc_sem_kern2kobj(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp);
medusa_answer_t ipc_sem_kobj2kern(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp);
