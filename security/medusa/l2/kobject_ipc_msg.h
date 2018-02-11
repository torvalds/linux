#include <linux/sched.h>
#include <linux/medusa/l3/registry.h>

struct ipc_msg_kobject {	
	unsigned int ipc_class;
	int id;
	kuid_t uid;
	kgid_t gid;
	MEDUSA_OBJECT_VARS;
	MEDUSA_SUBJECT_VARS;
};
extern MED_DECLARE_KCLASSOF(ipc_msg_kobject);


int ipc_msg_kern2kobj(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp);
medusa_answer_t ipc_msg_kobj2kern(struct medusa_kobject_s * ipck, struct kern_ipc_perm * ipcp);
