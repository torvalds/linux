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
#include "ccids/lib/tfrc.h"

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

static struct kmem_cache *ccid_kmem_cache_create(int obj_size, char *slab_name_fmt, const char *fmt,...)
{
	struct kmem_cache *slab;
	va_list args;

	va_start(args, fmt);
	vsnprintf(slab_name_fmt, CCID_SLAB_NAME_LENGTH, fmt, args);
	va_end(args);

	slab = kmem_cache_create(slab_name_fmt, sizeof(struct ccid) + obj_size, 0,
				 SLAB_HWCACHE_ALIGN, NULL);
	return slab;
}

static void ccid_kmem_cache_destroy(struct kmem_cache *slab)
{
	if (slab != NULL)
		kmem_cache_destroy(slab);
}

static int ccid_activate(struct ccid_operations *ccid_ops)
{
	int err = -ENOBUFS;

	ccid_ops->ccid_hc_rx_slab =
			ccid_kmem_cache_create(ccid_ops->ccid_hc_rx_obj_size,
					       ccid_ops->ccid_hc_rx_slab_name,
					       "ccid%u_hc_rx_sock",
					       ccid_ops->ccid_id);
	if (ccid_ops->ccid_hc_rx_slab == NULL)
		goto out;

	ccid_ops->ccid_hc_tx_slab =
			ccid_kmem_cache_create(ccid_ops->ccid_hc_tx_obj_size,
					       ccid_ops->ccid_hc_tx_slab_name,
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

struct ccid *ccid_new(const u8 id, struct sock *sk, bool rx)
{
	struct ccid_operations *ccid_ops = ccid_by_number(id);
	struct ccid *ccid = NULL;

	if (ccid_ops == NULL)
		goto out;

	ccid = kmem_cache_alloc(rx ? ccid_ops->ccid_hc_rx_slab :
				     ccid_ops->ccid_hc_tx_slab, gfp_any());
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

void ccid_hc_rx_delete(struct ccid *ccid, struct sock *sk)
{
	if (ccid != NULL) {
		if (ccid->ccid_ops->ccid_hc_rx_exit != NULL)
			ccid->ccid_ops->ccid_hc_rx_exit(sk);
		kmem_cache_free(ccid->ccid_ops->ccid_hc_rx_slab, ccid);
	}
}

void ccid_hc_tx_delete(struct ccid *ccid, struct sock *sk)
{
	if (ccid != NULL) {
		if (ccid->ccid_ops->ccid_hc_tx_exit != NULL)
			ccid->ccid_ops->ccid_hc_tx_exit(sk);
		kmem_cache_free(ccid->ccid_ops->ccid_hc_tx_slab, ccid);
	}
}

int __init ccid_initialize_builtins(void)
{
	int i, err = tfrc_lib_init();

	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(ccids); i++) {
		err = ccid_activate(ccids[i]);
		if (err)
			goto unwind_registrations;
	}
	return 0;

unwind_registrations:
	while(--i >= 0)
		ccid_deactivate(ccids[i]);
	tfrc_lib_exit();
	return err;
}

void ccid_cleanup_builtins(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ccids); i++)
		ccid_deactivate(ccids[i]);
	tfrc_lib_exit();
}
