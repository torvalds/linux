// SPDX-License-Identifier: GPL-2.0

#include <linux/mutex.h>
#include <linux/xarray.h>
#include <net/page_pool/types.h>

#include "page_pool_priv.h"

static DEFINE_XARRAY_FLAGS(page_pools, XA_FLAGS_ALLOC1);
static DEFINE_MUTEX(page_pools_lock);

int page_pool_list(struct page_pool *pool)
{
	static u32 id_alloc_next;
	int err;

	mutex_lock(&page_pools_lock);
	err = xa_alloc_cyclic(&page_pools, &pool->user.id, pool, xa_limit_32b,
			      &id_alloc_next, GFP_KERNEL);
	if (err < 0)
		goto err_unlock;

	mutex_unlock(&page_pools_lock);
	return 0;

err_unlock:
	mutex_unlock(&page_pools_lock);
	return err;
}

void page_pool_unlist(struct page_pool *pool)
{
	mutex_lock(&page_pools_lock);
	xa_erase(&page_pools, pool->user.id);
	mutex_unlock(&page_pools_lock);
}
