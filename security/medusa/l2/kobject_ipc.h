#include <linux/medusa/l3/registry.h>

struct ipc_kobject {	
	unsigned int sid;
	unsigned int sclass;
	MEDUSA_OBJECT_VARS;
};
extern MED_DECLARE_KCLASSOF(ipc_kobject);

int ipc_kobj2kern(struct ipc_kobject * ipck, struct kern_ipc_perm * ipcp);
int ipc_kern2kobj(struct ipc_kobject * ipck, struct kern_ipc_perm * ipcp);
