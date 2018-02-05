#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l1/ipc.h>
#include <linux/syscalls.h>
#include "ipc_utils.h"
#include "kobject_ipc.h"

/*
* ipc_class = MED_IPC_SEM, MED_IPC_MSG, MED_IPC_SHM look l1/ipc.h
*/
MED_ATTRS(ipc_kobject) {
	MED_ATTR_RO		(ipc_kobject, ipc_class, "ipc_class", MED_UNSIGNED),
	MED_ATTR_RO		(ipc_kobject, id, "id", MED_SIGNED),
	MED_ATTR_END
};


int ipc_kern2kobj(struct ipc_kobject * ipck, struct kern_ipc_perm * ipcp)
{
	if(!ipcp){
		printk("IPCP is NULL\n");	
		return -1;
	}
	struct medusa_l1_ipc_s* security_s;
	security_s = (struct medusa_l1_ipc_s*) ipcp->security;
	printk("ipc kern2kobj 1\n");
        memset(ipck, '\0', sizeof(struct ipc_kobject));
	
	printk("ipc kern2kobj 2\n");
	if(!security_s)
		return -1;
	
	printk("ipc kern2kobj 3\n");
	ipck->id = ipcp->id;
	ipck->ipc_class = security_s->ipc_class;
	printk("ipc kern2kobj 4\n");
	COPY_MEDUSA_SUBJECT_VARS(ipck, security_s);
	COPY_MEDUSA_OBJECT_VARS(ipck, security_s);
	printk("ipc kern2kobj 5\n");

	return 0;
}

medusa_answer_t ipc_kobj2kern(struct ipc_kobject * ipck, struct kern_ipc_perm * ipcp)
{
	struct medusa_l1_ipc_s* security_s;
	security_s = (struct medusa_l1_ipc_s*) ipcp->security;
	security_s->ipc_class = ipck->ipc_class;

	COPY_MEDUSA_SUBJECT_VARS(ipck, security_s);
	COPY_MEDUSA_OBJECT_VARS(ipck, security_s);

	return MED_OK;
}

static struct ipc_kobject storage;

static struct medusa_kobject_s * ipc_fetch(struct medusa_kobject_s * kobj)
{
	struct ipc_kobject * ipc_kobj;
	struct kern_ipc_perm *ipcp;
	ipc_kobj = (struct ipc_kobject *)kobj;
	
	if (!ipc_kobj)
		goto out_err;

	ipcp = medusa_ipc_info_lock(ipc_kobj->id, ipc_kobj->ipc_class);
	if(!ipcp)
		goto out_err;

	printk("MEDUSAAAA: id from object 2: %d\n", ipcp->id);
	ipc_kern2kobj(&storage, ipcp);
	return (struct medusa_kobject_s *)&storage;
out_err:
	return (struct medusa_kobject_s *)kobj;
}

static medusa_answer_t ipc_update(struct medusa_kobject_s * kobj)
{
	/*
	struct task_struct * p;
	medusa_answer_t retval;

	p = ((struct ipc_kobject *)kobj);
	if (p) {
		retval = ipc_kobj2kern((struct process_kobject *)kobj, p);
		return retval;
	}
	*/
	return MED_OK;
}

MED_KCLASS(ipc_kobject) {
	MEDUSA_KCLASS_HEADER(ipc_kobject),
	"ipc",
	NULL,		/* init kclass */
	NULL,		/* destroy kclass */
	ipc_fetch,
	ipc_update,
	NULL,		/* unmonitor */
};

void ipc_kobject_rmmod(void);

int __init ipc_kobject_init(void) {
	MED_REGISTER_KCLASS(ipc_kobject);
	return 0;
}

__initcall(ipc_kobject_init);
