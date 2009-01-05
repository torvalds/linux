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

static struct ccid_operations *ccids[] = {
	&ccid2_ops,
#ifdef CONFIG_IP_DCCP_CCID3
	&ccid3_ops,
#endif
};

static struct ccid_operations *ccid_by_number(const u8 id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ccids); i++)
		if (ccids[i]->ccid_id == id)
			return ccids[i];
	return NULL;
}

/* check that up to @array_len members in @ccid_array are supported */
bool ccid_support_check(u8 const *ccid_array, u8 array_len)
{
	while (array_len > 0)
		if (ccid_by_number(ccid_array[--array_len]) == NULL)
			return false;
	return true;
}

/**
 * ccid_get_builtin_ccids  -  Populate a list of built-in CCIDs
 * @ccid_array: pointer to copy into
 * @array_len: value to return length into
 * This function allocates memory - caller must see that it is freed after use.
 */
int ccid_get_builtin_ccids(u8 **ccid_array, u8 *array_len)
{
	*ccid_array = kmalloc(ARRAY_SIZE(ccids), gfp_any());
	if (*ccid_array == NULL)
		return -ENOBUFS;

	for (*array_len = 0; *array_len < ARRAY_SIZE(ccids); *array_len += 1)
		(*ccid_array)[*array_len] = ccids[*array_len]->ccid_id;
	return 0;
}

int ccid_getsockopt_builtin_ccids(struct sock *sk, int len,
				  char __user *optval, int __user *optlen)
{
	u8 *ccid_array, array_len;
	int err = 0;

	if (len < ARRAY_SIZE(ccids))
		return -EINVAL;

	if (ccid_get_builtin_ccids(&ccid_array, &array_len))
		return -ENOBUFS;

	if (put_user(array_len, optlen) ||
	    copy_to_user(optval, ccid_array, array_len))
		err = -EFAULT;

	kfree(ccid_array);
	return err;
}

#ifdef ___OLD_INTERFACE_TO_BE_REMOVED___
static u8 builtin_ccids[] = {
	DCCPC_CCID2,		/* CCID2 is supported by default */
#if defined(CONFIG_IP_DCCP_CCID3) || defined(CONFIG_IP_DCCP_CCID3_MODULE)
	DCCPC_CCID3,
#endif
};

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
#endif /* ___OLD_INTERFACE_TO_BE_REMOVED___ */

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

#ifdef ___OLD_INTERFACE_TO_BE_REMOVED___
/* check that up to @array_len members in @ccid_array are supported */
bool ccid_support_check(u8 const *ccid_array, u8 array_len)
{
	u8 i, j, found;

	for (i = 0, found = 0; i < array_len; i++, found = 0) {
		for (j = 0; !found && j < ARRAY_SIZE(builtin_ccids); j++)
			found = (ccid_array[i] == builtin_ccids[j]);
		if (!found)
			return false;
	}
	return true;
}

/**
 * ccid_get_builtin_ccids  -  Provide copy of `builtin' CCID array
 * @ccid_array: pointer to copy into
 * @array_len: value to return length into
 * This function allocates memory - caller must see that it is freed after use.
 */
int ccid_get_builtin_ccids(u8 **ccid_array, u8 *array_len)
{
	*ccid_array = kmemdup(builtin_ccids, sizeof(builtin_ccids), gfp_any());
	if (*ccid_array == NULL)
		return -ENOBUFS;
	*array_len = ARRAY_SIZE(builtin_ccids);
	return 0;
}

int ccid_getsockopt_builtin_ccids(struct sock *sk, int len,
				    char __user *optval, int __user *optlen)
{
	if (len < sizeof(builtin_ccids))
		return -EINVAL;

	if (put_user(sizeof(builtin_ccids), optlen) ||
	    copy_to_user(optval, builtin_ccids, sizeof(builtin_ccids)))
		return -EFAULT;
	return 0;
}
#endif /* ___OLD_INTERFACE_TO_BE_REMOVED___ */

static int ccid_activate(struct ccid_operations *ccid_ops)
{
	int err = -ENOBUFS;

	ccid_ops->ccid_hc_rx_slab =
			ccid_kmem_cache_create(ccid_ops->ccid_hc_rx_obj_size,
					       "ccid%u_hc_rx_sock",
					       ccid_ops->ccid_id);
	if (ccid_ops->ccid_hc_rx_slab == NULL)
		goto out;

	ccid_ops->ccid_hc_tx_slab =
			ccid_kmem_cache_create(ccid_ops->ccid_hc_tx_obj_size,
					       "ccid%u_hc_tx_sock",
					       ccid_ops->ccid_id);
	if (ccid_ops->ccid_hc_tx_slab == NULL)
		goto out_free_rx_slab;

	pr_info("CCID: Activated CCID %d (%s)\n",
		ccid_ops->ccid_id, ccid_ops->ccid_name);
	err = 0;
out:
	return err;
out_free_rx_slab:
	ccid_kmem_cache_destroy(ccid_ops->ccid_hc_rx_slab);
	ccid_ops->ccid_hc_rx_slab = NULL;
	goto out;
}

static void ccid_deactivate(struct ccid_operations *ccid_ops)
{
	ccid_kmem_cache_destroy(ccid_ops->ccid_hc_tx_slab);
	ccid_ops->ccid_hc_tx_slab = NULL;
	ccid_kmem_cache_destroy(ccid_ops->ccid_hc_rx_slab);
	ccid_ops->ccid_hc_rx_slab = NULL;

	pr_info("CCID: Deactivated CCID %d (%s)\n",
		ccid_ops->ccid_id, ccid_ops->ccid_name);
}

struct ccid *ccid_new(unsigned char id, struct sock *sk, int rx, gfp_t gfp)
{
	struct ccid_operations *ccid_ops = ccid_by_number(id);
	struct ccid *ccid = NULL;

	if (ccid_ops == NULL)
		goto out;

	ccid = kmem_cache_alloc(rx ? ccid_ops->ccid_hc_rx_slab :
				     ccid_ops->ccid_hc_tx_slab, gfp);
	if (ccid == NULL)
		goto out;
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
out_free_ccid:
	kmem_cache_free(rx ? ccid_ops->ccid_hc_rx_slab :
			ccid_ops->ccid_hc_tx_slab, ccid);
	ccid = NULL;
	goto out;
}

EXPORT_SYMBOL_GPL(ccid_new);

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

int __init ccid_initialize_builtins(void)
{
	int i, err;

	for (i = 0; i < ARRAY_SIZE(ccids); i++) {
		err = ccid_activate(ccids[i]);
		if (err)
			goto unwind_registrations;
	}
	return 0;

unwind_registrations:
	while(--i >= 0)
		ccid_deactivate(ccids[i]);
	return err;
}

void ccid_cleanup_builtins(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ccids); i++)
		ccid_deactivate(ccids[i]);
}
