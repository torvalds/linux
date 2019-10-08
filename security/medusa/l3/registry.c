#include <linux/medusa/l3/arch.h>
#include <linux/medusa/l3/registry.h>
#include "l3_internals.h"

/* nesting as follows: registry_lock is outer, usecount_lock is inner. */

MED_LOCK_DATA(registry_lock); /* the linked list lock */
 static MED_LOCK_DATA(usecount_lock); /* the lock for modifying use-count */
  struct medusa_kclass_s * kclasses = NULL;
  struct medusa_evtype_s * evtypes = NULL;
  struct medusa_authserver_s * authserver = NULL;
 int medusa_authserver_magic = 1; /* the 'version' of authserver */
/* WARNING! medusa_authserver_magic is not locked, nor atomic type,
 * because we want to have as much portable (and easy and fast) code
 * as possible. thus we must change its value BEFORE modifying authserver,
 * and place some memory barrier between, or get lock there - the lock
 * hopefully contains some kind of such barrier ;).
 */

static int mystrcmp(char * p1, char * p2)
{
	while (*p1) {
		if (*p2 != *p1)
			return *p1 - *p2;
		p1++; p2++;
	}
	return -*p2;
}

/**
 * med_get_kclass - lock the kclass by incrementing its use-count.
 * @ptr: pointer to the kclass to lock
 *
 * This increments the use-count; works great even if you want to sleep.
 * when calling this function, the use-count must already be non-zero.
 */
medusa_answer_t med_get_kclass(struct medusa_kclass_s * ptr)
{
	MED_LOCK_W(usecount_lock);
	ptr->use_count++;
	MED_UNLOCK_W(usecount_lock);
	return MED_OK;
}

/**
 * med_put_kclass - unlock the kclass by decrementing its use-count.
 * @ptr: pointer to the kclass to unlock
 *
 * This decrements the use-count. Note that it does nothing special when
 * the use-count goes to zero. Someone still may find the kclass in the
 * linked list and claim it by using med_get_kclass.
 */
void med_put_kclass(struct medusa_kclass_s * ptr)
{
	MED_LOCK_W(usecount_lock);
	if (ptr->use_count) /* sanity check only */
		ptr->use_count--;
	MED_UNLOCK_W(usecount_lock);
}

/**
 * med_get_kclass_by_name - find a kclass and return get-kclassed refference.
 * @name: name of the kclass to find
 *
 * It may return NULL on failure; caller must verify this each time.
 */
struct medusa_kclass_s * med_get_kclass_by_name(char * name)
{
	struct medusa_kclass_s * tmp;

	MED_LOCK_R(registry_lock);
	for (tmp = kclasses; tmp; tmp = tmp->next)
		if (!mystrcmp(name, tmp->name)) {
			MED_LOCK_W(usecount_lock);
			tmp->use_count++;
			MED_UNLOCK_W(usecount_lock);
			break;
		}
	MED_UNLOCK_R(registry_lock);
	return tmp;
}

/**
 * med_get_kclass_by_cinfo - find a kclass and return get-kclassed refference.
 * @cinfo: cinfo of the kclass to find
 *
 * It may return NULL on failure; caller must verify this each time.
 */
struct medusa_kclass_s * med_get_kclass_by_cinfo(cinfo_t cinfo)
{
	struct medusa_kclass_s * tmp;

	MED_LOCK_R(registry_lock);
	for (tmp = kclasses; tmp; tmp = tmp->next)
		if (cinfo == tmp->cinfo) {
			MED_LOCK_W(usecount_lock);
			tmp->use_count++;
			MED_UNLOCK_W(usecount_lock);
			break;
		}
	MED_UNLOCK_R(registry_lock);
	return tmp;
}

/**
 * med_get_kclass_by_pointer - find a kclass and return get-kclassed refference.
 * @ptr: unsafe pointer to the kclass to find
 *
 * It may return NULL on failure; caller must verify this each time.
 */
struct medusa_kclass_s * med_get_kclass_by_pointer(struct medusa_kclass_s * ptr)
{
	struct medusa_kclass_s * tmp;

	MED_LOCK_R(registry_lock);
	for (tmp = kclasses; tmp; tmp = tmp->next)
		if (ptr == tmp) {
			MED_LOCK_W(usecount_lock);
			tmp->use_count++;
			MED_UNLOCK_W(usecount_lock);
			break;
		}
	MED_UNLOCK_R(registry_lock);
	return tmp;
}

/**
 * med_unlink_kclass - unlink the kclass from all L3 lists
 * @ptr: kclass to unlink
 *
 * This is called with use-count=0 to remove the kclass from L3
 * lists. It may be called with all kinds of locks held, and thus
 * it does not notify the authserver.
 *
 * That is: if the authserver really relies on the kclass, it should use
 * med_get_kclass() at the very beginning.
 *
 * If the use-count is nonzero, it fails gracefully. This allows use of
 * med_unlink_kclass as an atomic uninstallation check & unlink. Always
 * check the return value of this call.
 *
 * After returning from this function, some servers might still use
 * the kclass, but they must be able to give it up on del_kclass callback.
 * No new servers and/or event types will be able to attach to the kclass,
 * and it waits for its final deletion by med_unregister_kclass().
 *
 * callers, who call med_unlink_kclass and get MED_OK, should really call
 * med_unregister_kclass soon.
 */
 
medusa_answer_t med_unlink_kclass(struct medusa_kclass_s * ptr)
{
	int retval = MED_OK;
	struct medusa_kclass_s * tmp;

	MED_LOCK_W(registry_lock);
	MED_LOCK_R(usecount_lock);
	if (!ptr->use_count) {
		if (ptr == kclasses)
			kclasses = ptr->next;
		else
			for (tmp = kclasses; tmp; tmp = tmp->next)
				if (tmp->next == ptr) {
					tmp->next = tmp->next->next;
					break;
				}
		/* TODO: verify whether we found it! */
		ptr->next = NULL;
	} else
		retval = MED_ERR;
	MED_UNLOCK_R(usecount_lock);
	MED_UNLOCK_W(registry_lock);
	return retval;
}

/**
 * med_unregister_kclass - unregister the kclass.
 *
 * This is called after the usage-count has dropped to 0, and also
 * after someone has called med_unlink_kclass. Its whole purpose is to
 * notify few routines about disappearance of kclass. They must accept
 * it and stop using the kclass, because after returning from this
 * function, the k-kclass does not exist.
 *
 * The callbacks called from here may sleep.
 */
void med_unregister_kclass(struct medusa_kclass_s * ptr)
{
	med_pr_info("Unregistering kclass %s\n", ptr->name);
	MED_LOCK_R(registry_lock);
	MED_LOCK_R(usecount_lock);
	if (ptr->use_count || ptr->next) { /* useless sanity check */
		med_pr_crit("A fatal ERROR has occured; expect system crash. If you're removing a file-related kclass, press reset. Otherwise save now.\n");
		MED_UNLOCK_R(usecount_lock);
		MED_UNLOCK_R(registry_lock);
		return;
	}
	MED_UNLOCK_R(usecount_lock);
	MED_UNLOCK_R(registry_lock);
	if (authserver && authserver->del_kclass)
		authserver->del_kclass(ptr);
	/* FIXME: this isn't safe. add use-count to authserver too... */
}
 
/**
 * med_register_kclass - register a kclass of k-objects and notify the authserver
 * @ptr: pointer to the kclass to register
 *
 * The authserver call must be in lock or a semaphore - we promised
 * that in authserver.h. :)
 */
int med_register_kclass(struct medusa_kclass_s * ptr)
{
	struct medusa_kclass_s * p;

	ptr->name[MEDUSA_KCLASSNAME_MAX-1] = '\0';
	med_pr_info("Registering kclass %s\n", ptr->name);
	MED_LOCK_W(registry_lock);
	for (p=kclasses; p; p=p->next)
		if (ptr==p || !mystrcmp(p->name, ptr->name)) {
			med_pr_err("Error: such kclass already exists.\n");
			MED_UNLOCK_W(registry_lock);
			return -1;
		}
	/* we don't write-lock usecount_lock. That's OK, because noone is
	 * able to find the entry before it's in the linked list.
	 * we set use-count to 1, and decrement it soon hereafter.
	 */
	ptr->use_count = 1;
	ptr->next = kclasses;
	kclasses = ptr;
	MED_UNLOCK_W(registry_lock);
	if (authserver && authserver->add_kclass)
		authserver->add_kclass(ptr); /* TODO: some day, check the return value */
	med_put_kclass(ptr);
	return 0;
}

/**
 * med_register_evtype - register an event type and notify the authserver.
 * @ptr: pointer to the event type to register
 *
 * The event type must be prepared by l2 routines to contain pointers to
 * all related kclasses of k-objects.
 */
int med_register_evtype(struct medusa_evtype_s * ptr, int flags)
{
	struct medusa_evtype_s * p;

	ptr->name[MEDUSA_EVNAME_MAX-1] = '\0';
	ptr->arg_name[0][MEDUSA_ATTRNAME_MAX-1] = '\0';
	ptr->arg_name[1][MEDUSA_ATTRNAME_MAX-1] = '\0';
	/* TODO: check whether kclasses are registered, maybe register automagically */
	med_pr_info("Registering event type %s(%s:%s->%s:%s)\n", ptr->name,
		ptr->arg_name[0],ptr->arg_kclass[0]->name,
		ptr->arg_name[1],ptr->arg_kclass[1]->name
	);
	MED_LOCK_W(registry_lock);
	for (p=evtypes; p; p=p->next)
		if (!mystrcmp(p->name, ptr->name)) {
			MED_UNLOCK_W(registry_lock);
			med_pr_err("Error: such event type already exists.\n");
			return -1;
		}
	ptr->next = evtypes;
	ptr->bitnr = flags;

#define MASK (~(MEDUSA_EVTYPE_TRIGGEREDATOBJECT | MEDUSA_EVTYPE_TRIGGEREDATSUBJECT))
	if (flags != MEDUSA_EVTYPE_NOTTRIGGERED)
		for (p=evtypes; p; p ? (p=p->next) : (p=evtypes))
			if (p->bitnr != MEDUSA_EVTYPE_NOTTRIGGERED &&
				(p->bitnr & MASK) == (ptr->bitnr & MASK)) {

				ptr->bitnr++; /* TODO: check for the upper limit! */
				p = NULL;
				continue;
			}
#undef MASK
	evtypes = ptr;
	if (authserver && authserver->add_evtype)
		authserver->add_evtype(ptr); /* TODO: some day, check for response */
	MED_UNLOCK_W(registry_lock);
	return 0;
}

/**
 * med_unregister_evtype - unregister an event type and notify the authserver.
 * @ptr: pointer to the event type to unregister
 *
 */
void med_unregister_evtype(struct medusa_evtype_s * ptr)
{
	struct medusa_evtype_s * tmp;
	med_pr_info("Unregistering event type %s\n", ptr->name);
	MED_LOCK_W(registry_lock);
	if (ptr == evtypes)
		evtypes = ptr->next;
	else
		for (tmp = evtypes; tmp; tmp = tmp->next)
			if (tmp->next == ptr) {
				tmp->next = tmp->next->next;
				if (authserver && authserver->del_evtype)
					authserver->del_evtype(ptr);
				break;
			}
	/* TODO: verify whether we found it */
	MED_UNLOCK_W(registry_lock);
}

/**
 * med_register_authserver - register the authorization server
 * @ptr: pointer to the filled medusa_authserver_s structure
 *
 * This routine inserts the authorization server in the internal data
 * structures, sets the use-count to 1 (i.e. returns get-servered entry),
 * and announces all known classes to the server.
 */
int med_register_authserver(struct medusa_authserver_s * ptr)
{
	struct medusa_kclass_s * cp;
	struct medusa_evtype_s * ap;

	med_pr_info("Registering authentication server %s\n", ptr->name);
	MED_LOCK_W(registry_lock);
	if (authserver) {
		med_pr_err("Registration of auth. server '%s' failed: '%s' already present!\n", ptr->name, authserver->name);
		MED_UNLOCK_W(registry_lock);
		return -1;
	}
	/* we don't write-lock usecount_lock. That's OK, because noone is
	 * able to find the entry before it's in the linked list.
	 * we set use-count to 1, and somebody has to decrement it some day.
	 */
	ptr->use_count = 1;
	medusa_authserver_magic++;
	authserver = ptr;

	/* we must remain in write-lock here, to synchronize add_*
	 * events across our code.
	 */
	if (ptr->add_kclass)
		for (cp = kclasses; cp; cp = cp->next)
			ptr->add_kclass(cp); /* TODO: some day we might want to check the return value, to support specialized servers */
	if (ptr->add_evtype)
		for (ap = evtypes; ap; ap = ap->next)
			ptr->add_evtype(ap); /* TODO: the same for this */

	MED_UNLOCK_W(registry_lock);
	return 0;
}

/**
 * med_unregister_authserver - unlink the auth. server from L3.
 * @ptr: pointer to the server to unlink.
 *
 * This function is called by L4 code to unregister the auth. server.
 * After it has returned, no new questions will be placed to the server.
 * Note that some old questions might be pending, and after calling this,
 * it is wise to wait for close() callback to proceed with uninstallation.
 */
void med_unregister_authserver(struct medusa_authserver_s * ptr)
{
	med_pr_info("Unregistering authserver %s\n", ptr->name);
	MED_LOCK_W(registry_lock);
	/* the following code is a little bit useless, but we keep it here
	 * to allow multiple different authentication servers some day
	 */
	if (ptr != authserver) {
		MED_UNLOCK_W(registry_lock);
		return;
	}
	medusa_authserver_magic++;
	authserver = NULL;
	MED_UNLOCK_W(registry_lock);
	med_put_authserver(ptr);
}

/**
 * med_get_authserver - lock the authserver by increasing its use-count.
 *
 * This function gets one more refference to the authserver. Use it,
 * when you want to be sure the authserver won't vanish.
 */
struct medusa_authserver_s * med_get_authserver(void)
{
	MED_LOCK_W(usecount_lock);
	if (authserver) {
		authserver->use_count++;
		MED_UNLOCK_W(usecount_lock);
		return authserver;
	}
	MED_UNLOCK_W(usecount_lock);
	return NULL;
}

/**
 * med_put_authserver - release the authserver by decrementing its use-count
 * @ptr: a pointer to the authserver
 *
 * This is an opposite function to med_get_authserver. Please, try to call
 * this without any locks; the close() callback of L4 server, which may
 * eventually get called from here, may block. This might change, if
 * reasonable.
 */
void med_put_authserver(struct medusa_authserver_s * ptr)
{
	MED_LOCK_W(usecount_lock);
	if (ptr->use_count) /* sanity check only */
		ptr->use_count--;
	if (ptr->use_count) { /* fast path */
		MED_UNLOCK_W(usecount_lock);
		return;
	}
	MED_UNLOCK_W(usecount_lock);
	if (ptr->close)
		ptr->close();
}
