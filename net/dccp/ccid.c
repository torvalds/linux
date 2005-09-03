/*
 *  net/dccp/ccid.c
 *
 *  An implementation of the DCCP protocol
 *  Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *  CCID infrastructure
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 */

#include "ccid.h"

static struct ccid *ccids[CCID_MAX];
#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT)
static atomic_t ccids_lockct = ATOMIC_INIT(0);
static DEFINE_SPINLOCK(ccids_lock);

/*
 * The strategy is: modifications ccids vector are short, do not sleep and
 * veeery rare, but read access should be free of any exclusive locks.
 */
static void ccids_write_lock(void)
{
	spin_lock(&ccids_lock);
	while (atomic_read(&ccids_lockct) != 0) {
		spin_unlock(&ccids_lock);
		yield();
		spin_lock(&ccids_lock);
	}
}

static inline void ccids_write_unlock(void)
{
	spin_unlock(&ccids_lock);
}

static inline void ccids_read_lock(void)
{
	atomic_inc(&ccids_lockct);
	spin_unlock_wait(&ccids_lock);
}

static inline void ccids_read_unlock(void)
{
	atomic_dec(&ccids_lockct);
}

#else
#define ccids_write_lock() do { } while(0)
#define ccids_write_unlock() do { } while(0)
#define ccids_read_lock() do { } while(0)
#define ccids_read_unlock() do { } while(0)
#endif

int ccid_register(struct ccid *ccid)
{
	int err;

	if (ccid->ccid_init == NULL)
		return -1;

	ccids_write_lock();
	err = -EEXIST;
	if (ccids[ccid->ccid_id] == NULL) {
		ccids[ccid->ccid_id] = ccid;
		err = 0;
	}
	ccids_write_unlock();
	if (err == 0)
		pr_info("CCID: Registered CCID %d (%s)\n",
			ccid->ccid_id, ccid->ccid_name);
	return err;
}

EXPORT_SYMBOL_GPL(ccid_register);

int ccid_unregister(struct ccid *ccid)
{
	ccids_write_lock();
	ccids[ccid->ccid_id] = NULL;
	ccids_write_unlock();
	pr_info("CCID: Unregistered CCID %d (%s)\n",
		ccid->ccid_id, ccid->ccid_name);
	return 0;
}

EXPORT_SYMBOL_GPL(ccid_unregister);

struct ccid *ccid_init(unsigned char id, struct sock *sk)
{
	struct ccid *ccid;

#ifdef CONFIG_KMOD
	if (ccids[id] == NULL)
		request_module("net-dccp-ccid-%d", id);
#endif
	ccids_read_lock();

	ccid = ccids[id];
	if (ccid == NULL)
		goto out;

	if (!try_module_get(ccid->ccid_owner))
		goto out_err;

	if (ccid->ccid_init(sk) != 0)
		goto out_module_put;
out:
	ccids_read_unlock();
	return ccid;
out_module_put:
	module_put(ccid->ccid_owner);
out_err:
	ccid = NULL;
	goto out;
}

EXPORT_SYMBOL_GPL(ccid_init);

void ccid_exit(struct ccid *ccid, struct sock *sk)
{
	if (ccid == NULL)
		return;

	ccids_read_lock();

	if (ccids[ccid->ccid_id] != NULL) {
		if (ccid->ccid_exit != NULL)
			ccid->ccid_exit(sk);
		module_put(ccid->ccid_owner);
	}

	ccids_read_unlock();
}

EXPORT_SYMBOL_GPL(ccid_exit);
