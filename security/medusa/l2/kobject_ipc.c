#include <linux/msg.h>
#include "../../../ipc/util.h" //TODO
#include "ipc_utils.h"
#include "kobject_ipc.h"

static struct ipc_kobject storage;

#define COPY_WRITE_IPC_VARS(to,from) \
	do { \
		(to)->uid = (from)->uid; \
		(to)->gid = (from)->gid; \
		(to)->mode = (from)->mode; \
	} while(0);

#define COPY_READ_IPC_VARS(to, from) \
	do { \
		(to)->deleted = (from)->deleted; \
		(to)->id = (from)->id; \
		(to)->key = (from)->key; \
		(to)->cuid = (from)->cuid; \
		(to)->cgid = (from)->cgid; \
		(to)->seq = (from)->seq; \
		(to)->refcount = (from)->refcount; \
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
	MED_ATTR_RO	(ipc_kobject, ipc_perm.refcount, "refcount", MED_UNSIGNED),
	MED_ATTR_OBJECT (ipc_kobject),
	MED_ATTR_END
};

/**
 * ipc_kern2kobj - convert function from kernel structure to kobject
 * @ipc_kobj - pointer to ipc_kobject where data will be stored 
 * @ipcp - pointer to kernel structure used to get data
 * Return: pointer to ipc_kobject with data on success, NULL on error
 *
 * This routine expects existing Medusa ipcp security struct.
 * For validity of an IPC object, it must be always called after ipc_getref(),
 * before ipc_putref() functions.
 */
struct ipc_kobject *ipc_kern2kobj(struct ipc_kobject * ipc_kobj, struct kern_ipc_perm * ipcp)
{
	memset(ipc_kobj, '\0', sizeof(struct ipc_kobject));

	rcu_read_lock();
	ipc_kobj->ipc_class = ipc_security(ipcp)->ipc_class;
	COPY_WRITE_IPC_VARS(&(ipc_kobj->ipc_perm), ipcp);
	COPY_READ_IPC_VARS(&(ipc_kobj->ipc_perm), ipcp);
	COPY_MEDUSA_OBJECT_VARS(ipc_kobj, ipc_security(ipcp));
	rcu_read_unlock();

	/* due to ipc_getref() refcount of an IPC object is increased by 1 */
	refcount_dec(&ipc_kobj->ipc_perm.refcount);

	return ipc_kobj;
}

/**
 * TODO TODO TODO
 * write races to IPC object! need ipc_trylock_object()
 */
medusa_answer_t ipc_kobj2kern(struct ipc_kobject *ipc_obj, struct kern_ipc_perm *ipcp)
{
	struct medusa_l1_ipc_s *security_s = (struct medusa_l1_ipc_s*)ipcp->security;
	if (!security_s)
		return MED_ERR;

	rcu_read_lock();
	COPY_WRITE_IPC_VARS(ipcp, &(ipc_obj->ipc_perm));
	COPY_MEDUSA_OBJECT_VARS(ipc_security(ipcp), ipc_obj);
	MED_MAGIC_VALIDATE(ipc_security(ipcp));
	rcu_read_unlock();
	
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
	// update kernel structure	
	retval = ipc_kobj2kern(ipc_kobj, ipcp);

	ipc_unlock_object(ipcp);
	//ipc_rcu_putref(ipcp, ipc_rcu_free);	
out_rcu_unlock:
	rcu_read_unlock();
out_err:
	return retval;
}

/**
 * Increase references to an IPC object given by its @ipcp kern_ipc_perm
 * structure. If @unlock is True, also unlock this IPC object. At the
 * end release RCU read lock.
 * @ipcp - pointer to kern_ipc_perm struct of relevant IPC object
 * @unlock - if True, unlock relevant IPC object
 * Return: -EIDRM if IPC object is marked to deletion, 0 otherwise
 */
inline int ipc_getref(struct kern_ipc_perm *ipcp, bool unlock) {
	/* increase references to IPC object; check races with IPC_RMID */
	if (unlikely(!ipc_rcu_getref(ipcp)))
		return -EIDRM;
	if (unlock)
		ipc_unlock_object(ipcp);
	rcu_read_unlock();
	return 0;
}

/**
 * Decrease references to an IPC object given by his @ipcp kern_ipc_perm
 * structure. If @lock is True, returns with locked IPC object. Takes
 * RCU read lock.
 * @ipcp - pointer to kern_ipc_perm struct of relevant IPC object
 * @lock - if True, returns with locked IPC object
 * Return: -EIDRM if IPC object is marked to deletion, 0 otherwise
 */
inline int ipc_putref(struct kern_ipc_perm *ipcp, bool lock) {
	int retval = 0;
	rcu_read_lock();
	/* ipc_valid_object() must be called with lock */
	ipc_lock_object(ipcp);
	/* decrease references to IPC object */
	ipc_rcu_putref(ipcp, ipcp->rcu_free);
	/* check validity of IPC object due to races with IPC_RMID */
	if (unlikely(!ipc_valid_object(ipcp)))
		retval = -EIDRM;
	if (!lock)
		ipc_unlock_object(ipcp);
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
