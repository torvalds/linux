#include <linux/msg.h>
#include "../../../ipc/util.h" //TODO
#include "ipc_utils.h"
#include "kobject_ipc_common.h"

static struct ipc_kobject storage;

#define COPY_WRITE_IPC_VARS(from, to) \
	do { \
		(to)->uid = (from)->uid; \
		(to)->gid = (from)->gid; \
		(to)->mode = (from)->mode; \
	} while(0);

#define COPY_READ_IPC_VARS(from, to) \
	do { \
		(to)->deleted = (from)->deleted; \
		(to)->id = (from)->id; \
		(to)->key = (from)->key; \
		(to)->cuid = (from)->cuid; \
		(to)->cgid = (from)->cgid; \
		(to)->seq = (from)->seq; \
	} while(0);

MED_ATTRS(ipc_kobject) {
	MED_ATTR_RO	(ipc_kobject, ipc_class, "ipc_class", MED_UNSIGNED),
	MED_ATTR_RO	(ipc_kobject, ipc_perm.deleted, "deleted", MED_UNSIGNED),
	MED_ATTR_RO	(ipc_kobject, ipc_perm.id, "id", MED_SIGNED),
	MED_ATTR_RO	(ipc_kobject, ipc_perm.key, "key", MED_SIGNED),
	MED_ATTR	(ipc_kobject, ipc_perm.uid, "uid", MED_UNSIGNED),
	MED_ATTR	(ipc_kobject, ipc_perm.gid, "gid", MED_UNSIGNED),
	MED_ATTR_RO	(ipc_kobject, ipc_perm.cuid, "cuid", MED_UNSIGNED),
	MED_ATTR_RO	(ipc_kobject, ipc_perm.cgid, "cgid", MED_UNSIGNED),
	MED_ATTR	(ipc_kobject, ipc_perm.mode, "mode", MED_UNSIGNED),
	MED_ATTR_RO	(ipc_kobject, ipc_perm.seq, "seq", MED_UNSIGNED),
	MED_ATTR_OBJECT (ipc_kobject),
	MED_ATTR_END
};

/**
 * ipc_kern2kobj - convert function from kernel structure to kobject
 * @ipc_kobj - pointer to ipc_kobject where data will be stored 
 * @ipcp - pointer to kernel structure used to get data
 * Return: pointer to ipc_kobject with data on success, NULL on error
 *
 * This routine expects the existing Medusa ipcp security struct!
 */
struct ipc_kobject *ipc_kern2kobj(struct ipc_kobject * ipc_kobj, struct kern_ipc_perm * ipcp)
{
	memset(ipc_kobj, '\0', sizeof(struct ipc_kobject));
	ipc_kobj->ipc_class = ipc_security(ipcp)->ipc_class;

	COPY_WRITE_IPC_VARS(ipcp, &(ipc_kobj->ipc_perm));
	COPY_READ_IPC_VARS(ipcp, &(ipc_kobj->ipc_perm));
	COPY_MEDUSA_OBJECT_VARS(ipc_security(ipcp), ipc_kobj);

	return ipc_kobj;
}

/**
 * TODO TODO TODO
 */
medusa_answer_t ipc_kobj2kern(struct ipc_kobject *ipc_obj, struct kern_ipc_perm *ipcp)
{
	struct medusa_l1_ipc_s *security_s = (struct medusa_l1_ipc_s*)ipcp->security;
	if (!security_s)
		return MED_ERR;

	COPY_WRITE_IPC_VARS(&(ipc_obj->ipc_perm), ipcp);
	COPY_MEDUSA_OBJECT_VARS(ipc_obj, ipc_security(ipcp));
	MED_MAGIC_VALIDATE(ipc_security(ipcp));
	
	return MED_OK;
}

/**
 * ipc_fetch - common logic for fetching data from kernel
 * TODO TODO TODO
 */
struct medusa_kobject_s * ipc_fetch(struct medusa_kobject_s *kobj)
{
	struct ipc_kobject *ipc_kobj;
	struct ipc_kobject *new_kobj = NULL;
	struct kern_ipc_perm *ipcp;
	struct ipc_ids *ids;

	ipc_kobj = (struct ipc_kobject*)kobj;
	if (!ipc_kobj)
		goto out_err;

	ids = medusa_get_ipc_ids(ipc_kobj->ipc_class);
	if(!ids)
		goto out_err;

	rcu_read_lock();

	ipcp = ipc_obtain_object_check(ids, ipc_kobj->ipc_perm.id);
	if(IS_ERR(ipcp) || !ipcp)
		goto out_rcu_unlock;

	new_kobj = ipc_kern2kobj(&storage, ipcp);

out_rcu_unlock:
	rcu_read_unlock();
out_err:
	return (struct medusa_kobject_s *)new_kobj;
}

/**
 * ipc_update - common logic for updating data in kernel by kobject data
 * TODO TODO TODO
 * Return: void pointer to memory area where kobject is stored or NULL on error
 */
medusa_answer_t ipc_update(struct medusa_kobject_s * kobj)
{
	struct ipc_kobject *ipc_kobj;
	struct kern_ipc_perm *ipcp;
	struct ipc_ids *ids;
	medusa_answer_t retval = MED_ERR;

	ipc_kobj = (struct ipc_kobject *)kobj;
	if (!ipc_kobj)
		goto out_err;

	ids = medusa_get_ipc_ids(ipc_kobj->ipc_class);
	if(!ids)
		goto out_err;
	
	rcu_read_lock();

	// Call inside RCU critical section
	// Object is not locked on exit
	ipcp = ipc_obtain_object_check(ids, ipc_kobj->ipc_perm.id);
	if(IS_ERR(ipcp) || !ipcp)
		goto out_rcu_unlock;

	// TODO TODO TODO FIXME 
	//if (!ipc_rcu_getref(ipcp))
	//	goto out_rcu_unlock;
	ipc_lock_object(ipcp);
	printk("MEdusa update before kobj2kern\n");
	// update kernel structure	
	retval = ipc_kobj2kern(ipc_kobj, ipcp);

	printk("MEdusa update after kobj2kern %d\n", retval);
	ipc_unlock_object(ipcp);
	//ipc_rcu_putref(ipcp, ipc_rcu_free);	
out_rcu_unlock:
	rcu_read_unlock();
out_err:
	return retval;
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
