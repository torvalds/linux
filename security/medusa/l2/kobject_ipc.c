#include <linux/msg.h>
#include "../../../ipc/util.h" //TODO
#include "kobject_ipc.h"

/* from ipc/shm.c, ipc/sem.c, ipc/msg.c */
#define shm_ids(ns)	((ns)->ids[IPC_SHM_IDS])
#define sem_ids(ns)	((ns)->ids[IPC_SEM_IDS])
#define msg_ids(ns)	((ns)->ids[IPC_MSG_IDS])

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
 * medusa_get_ipc_ids - retrieve ipc_ids structure from namespace by @ipc_class
 * @ipc_class is type of ipc mechanism; defined values see in l1/ipc.h
 * return: ipc_ids struct or NULL if @ipc_class is unknown
 */
static inline struct ipc_ids *medusa_get_ipc_ids(unsigned int ipc_class)
{
	struct ipc_namespace *ns;

	/*
	 * TODO: get_ipc_ns(current->...)
	 * see: ipc/util.c sysvipc_proc_open line 880...
	 */
	ns = current->nsproxy->ipc_ns;

	switch (ipc_class) {
	case MED_IPC_SEM:
		return &sem_ids(ns);
	case MED_IPC_SHM:
		return &shm_ids(ns);
	case MED_IPC_MSG:
		return &msg_ids(ns);
	}
	return NULL;
}

/**
 * ipc_kern2kobj - convert function from kernel structure to kobject
 * @ipc_kobj - pointer to ipc_kobject where data will be stored 
 * @ipcp - pointer to kernel structure used to get data
 * @dec_refcount - if set, decrement IPC object's refcount in returned @ipc_kobj
 * Return: pointer to @ipc_kobj with data on success, NULL on error (@ipc_kobj
 *	or @ipcp->security NULL)
 *
 * This routine expects existing Medusa ipcp security struct.
 * For validity of an IPC object, it must be always called after ipc_getref(),
 * before ipc_putref() functions.
 */
inline struct ipc_kobject *ipc_kern2kobj(struct ipc_kobject * ipc_kobj, struct kern_ipc_perm * ipcp, bool dec_refcount)
{
	if (unlikely(!ipc_kobj || !ipc_security(ipcp))) {
		med_pr_err("ERROR: NULL pointer: ipc_kern2kobj: ipc_kobj=%p or ipcp=%p", \
			ipc_kobj, ipcp);
		return NULL;
	}

	memset(ipc_kobj, '\0', sizeof(struct ipc_kobject));

	rcu_read_lock();
	ipc_kobj->ipc_class = ipc_security(ipcp)->ipc_class;
	COPY_WRITE_IPC_VARS(&(ipc_kobj->ipc_perm), ipcp);
	COPY_READ_IPC_VARS(&(ipc_kobj->ipc_perm), ipcp);
	COPY_MEDUSA_OBJECT_VARS(ipc_kobj, ipc_security(ipcp));
	rcu_read_unlock();

	/*
	 * Due to ipc_getref() refcount of an IPC object is increased by 1,
	 * so if @dec_refcount is true, we decrement it. Of course, not in
	 * ipcp->refcount but in its copy in returned @ipc_kobj.
	 *
	 * Almost always is ipc_kern2kobj() called with @dec_refcount true;
	 * only in case of ipc_kobj operation fetch() not.
	 */
	if (likely(dec_refcount))
		refcount_dec(&ipc_kobj->ipc_perm.refcount);

	return ipc_kobj;
}

/**
 * ipc_kobj2kern - convert function from kobject to kernel structure
 * @ipc_kobj - pointer to ipc_kobject used to get data
 * @ipcp - pointer to kernel structure where data will be stored
 * Return: MED_ERR on error (@ipc_kobj or @ipcp->security NULL), MED_OK otherwise
 *
 * This routine expects existing Medusa @ipcp security struct.
 * Due to write acces to an IPC object, this function must be called
 * within an RCU update section with ipcp->lock held.
 *
 * For now, it is called only from ipc_kobject update() operation.
 */
static inline medusa_answer_t ipc_kobj2kern(struct ipc_kobject *ipc_kobj, struct kern_ipc_perm *ipcp)
{
	if (unlikely(!ipc_kobj || !ipc_security(ipcp))) {
		med_pr_err("ERROR: NULL pointer: ipc_kobj2kern: ipc_kobj=%p or ipcp=%p", \
			ipc_kobj, ipcp);
		return MED_ERR;
	}

	COPY_WRITE_IPC_VARS(ipcp, &(ipc_kobj->ipc_perm));
	COPY_MEDUSA_OBJECT_VARS(ipc_security(ipcp), ipc_kobj);
	MED_MAGIC_VALIDATE(ipc_security(ipcp));
	
	return MED_OK;
}

/**
 * ipc_fetch - fetch data related to requested IPC object by authorisation server
 * @kobj - pointer to ipc_kobject which one holds identification of the requested
 *	IPC object
 * Return: pointer to new ipc_kobject with related IPC object data on success, NULL
 *	on error (@kobj is NULL, not existing IPC object)
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

	/*
	 * Call inside RCU critical section, IPC object is not locked
	 * on return of ipc_obtain_object_check().
	 */
	ipcp = ipc_obtain_object_check(ids, ipc_kobj->ipc_perm.id);
	if(IS_ERR(ipcp) || !ipcp)
		goto out_rcu_unlock;

	/*
	 * IPC object can be marked to deletion (races with IPC_RMID)
	 * but it's OK; we are in RCU, so IPC struct is not deleted yet.
	 *
	 * Third argument is false - do not decrement IPC object refcount
	 * in returned ipc_kobj.
	 */
	new_kobj = ipc_kern2kobj(&storage, ipcp, false);

out_rcu_unlock:
	rcu_read_unlock();
out_err:
	return (struct medusa_kobject_s *)new_kobj;
}

/**
 * ipc_update - update kernel-side data related to related IPC object @kobj
 * @kobj - pointer to ipc_kobject which one holds identification of therelated
 *	IPC object
 * Return: MED_OK if successfull, MED_ERR otherwise
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

	/*
	 * Call inside RCU critical section, IPC object is not locked
	 * on return of ipc_obtain_object_check().
	 */
	ipcp = ipc_obtain_object_check(ids, ipc_kobj->ipc_perm.id);
	if(IS_ERR(ipcp) || !ipcp)
		goto out_rcu_unlock;

	/*
	 * IPC object can be marked to deletion (races with IPC_RMID)
	 * but it's OK; we are in RCU, so IPC struct is not deleted yet.
	 * Obtaining ipcp->lock we check races.
	 */
	ipc_lock_object(ipcp);
	if (!ipc_valid_object(ipcp))
		goto out_ipc_unlock;

	/* IPC object is valid, so update kernel structure */
	retval = ipc_kobj2kern(ipc_kobj, ipcp);

out_ipc_unlock:
	ipc_unlock_object(ipcp);
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
