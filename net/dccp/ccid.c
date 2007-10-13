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

static struct ccid_operations *ccids[CCID_MAX];
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
	smp_mb__after_atomic_inc();
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

static struct kmem_cache *ccid_kmem_cache_create(int obj_size, const char *fmt,...)
{
	struct kmem_cache *slab;
	char slab_name_fmt[32], *slab_name;
	va_list args;

	va_start(args, fmt);
	vsnprintf(slab_name_fmt, sizeof(slab_name_fmt), fmt, args);
	va_end(args);

	slab_name = kstrdup(slab_name_fmt, GFP_KERNEL);
	if (slab_name == NULL)
		return NULL;
	slab = kmem_cache_create(slab_name, sizeof(struct ccid) + obj_size, 0,
				 SLAB_HWCACHE_ALIGN, NULL);
	if (slab == NULL)
		kfree(slab_name);
	return slab;
}

static void ccid_kmem_cache_destroy(struct kmem_cache *slab)
{
	if (slab != NULL) {
		const char *name = kmem_cache_name(slab);

		kmem_cache_destroy(slab);
		kfree(name);
	}
}

int ccid_register(struct ccid_operations *ccid_ops)
{
	int err = -ENOBUFS;

	ccid_ops->ccid_hc_rx_slab =
			ccid_kmem_cache_create(ccid_ops->ccid_hc_rx_obj_size,
					       "%s_hc_rx_sock",
					       ccid_ops->ccid_name);
	if (ccid_ops->ccid_hc_rx_slab == NULL)
		goto out;

	ccid_ops->ccid_hc_tx_slab =
			ccid_kmem_cache_create(ccid_ops->ccid_hc_tx_obj_size,
					       "%s_hc_tx_sock",
					       ccid_ops->ccid_name);
	if (ccid_ops->ccid_hc_tx_slab == NULL)
		goto out_free_rx_slab;

	ccids_write_lock();
	err = -EEXIST;
	if (ccids[ccid_ops->ccid_id] == NULL) {
		ccids[ccid_ops->ccid_id] = ccid_ops;
		err = 0;
	}
	ccids_write_unlock();
	if (err != 0)
		goto out_free_tx_slab;

	pr_info("CCID: Registered CCID %d (%s)\n",
		ccid_ops->ccid_id, ccid_ops->ccid_name);
out:
	return err;
out_free_tx_slab:
	ccid_kmem_cache_destroy(ccid_ops->ccid_hc_tx_slab);
	ccid_ops->ccid_hc_tx_slab = NULL;
	goto out;
out_free_rx_slab:
	ccid_kmem_cache_destroy(ccid_ops->ccid_hc_rx_slab);
	ccid_ops->ccid_hc_rx_slab = NULL;
	goto out;
}

EXPORT_SYMBOL_GPL(ccid_register);

int ccid_unregister(struct ccid_operations *ccid_ops)
{
	ccids_write_lock();
	ccids[ccid_ops->ccid_id] = NULL;
	ccids_write_unlock();

	ccid_kmem_cache_destroy(ccid_ops->ccid_hc_tx_slab);
	ccid_ops->ccid_hc_tx_slab = NULL;
	ccid_kmem_cache_destroy(ccid_ops->ccid_hc_rx_slab);
	ccid_ops->ccid_hc_rx_slab = NULL;

	pr_info("CCID: Unregistered CCID %d (%s)\n",
		ccid_ops->ccid_id, ccid_ops->ccid_name);
	return 0;
}

EXPORT_SYMBOL_GPL(ccid_unregister);

struct ccid *ccid_new(unsigned char id, struct sock *sk, int rx, gfp_t gfp)
{
	struct ccid_operations *ccid_ops;
	struct ccid *ccid = NULL;

	ccids_read_lock();
#ifdef CONFIG_KMOD
	if (ccids[id] == NULL) {
		/* We only try to load if in process context */
		ccids_read_unlock();
		if (gfp & GFP_ATOMIC)
			goto out;
		request_module("net-dccp-ccid-%d", id);
		ccids_read_lock();
	}
#endif
	ccid_ops = ccids[id];
	if (ccid_ops == NULL)
		goto out_unlock;

	if (!try_module_get(ccid_ops->ccid_owner))
		goto out_unlock;

	ccids_read_unlock();

	ccid = kmem_cache_alloc(rx ? ccid_ops->ccid_hc_rx_slab :
				     ccid_ops->ccid_hc_tx_slab, gfp);
	if (ccid == NULL)
		goto out_module_put;
	ccid->ccid_ops = ccid_ops;
	if (rx) {
		memset(ccid + 1, 0, ccid_ops->ccid_hc_rx_obj_size);
		if (ccid->ccid_ops->ccid_hc_rx_init != NULL &&
		    ccid->ccid_ops->ccid_hc_rx_init(ccid, sk) != 0)
			goto out_free_ccid;
	} else {
		memset(ccid + 1, 0, ccid_ops->ccid_hc_tx_obj_size);
		if (ccid->ccid_ops->ccid_hc_tx_init != NULL &&
		    ccid->ccid_ops->ccid_hc_tx_init(ccid, sk) != 0)
			goto out_free_ccid;
	}
out:
	return ccid;
out_unlock:
	ccids_read_unlock();
	goto out;
out_free_ccid:
	kmem_cache_free(rx ? ccid_ops->ccid_hc_rx_slab :
			ccid_ops->ccid_hc_tx_slab, ccid);
	ccid = NULL;
out_module_put:
	module_put(ccid_ops->ccid_owner);
	goto out;
}

EXPORT_SYMBOL_GPL(ccid_new);

struct ccid *ccid_hc_rx_new(unsigned char id, struct sock *sk, gfp_t gfp)
{
	return ccid_new(id, sk, 1, gfp);
}

EXPORT_SYMBOL_GPL(ccid_hc_rx_new);

struct ccid *ccid_hc_tx_new(unsigned char id,struct sock *sk,  gfp_t gfp)
{
	return ccid_new(id, sk, 0, gfp);
}

EXPORT_SYMBOL_GPL(ccid_hc_tx_new);

static void ccid_delete(struct ccid *ccid, struct sock *sk, int rx)
{
	struct ccid_operations *ccid_ops;

	if (ccid == NULL)
		return;

	ccid_ops = ccid->ccid_ops;
	if (rx) {
		if (ccid_ops->ccid_hc_rx_exit != NULL)
			ccid_ops->ccid_hc_rx_exit(sk);
		kmem_cache_free(ccid_ops->ccid_hc_rx_slab,  ccid);
	} else {
		if (ccid_ops->ccid_hc_tx_exit != NULL)
			ccid_ops->ccid_hc_tx_exit(sk);
		kmem_cache_free(ccid_ops->ccid_hc_tx_slab,  ccid);
	}
	ccids_read_lock();
	if (ccids[ccid_ops->ccid_id] != NULL)
		module_put(ccid_ops->ccid_owner);
	ccids_read_unlock();
}

void ccid_hc_rx_delete(struct ccid *ccid, struct sock *sk)
{
	ccid_delete(ccid, sk, 1);
}

EXPORT_SYMBOL_GPL(ccid_hc_rx_delete);

void ccid_hc_tx_delete(struct ccid *ccid, struct sock *sk)
{
	ccid_delete(ccid, sk, 0);
}

EXPORT_SYMBOL_GPL(ccid_hc_tx_delete);
