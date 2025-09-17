// SPDX-License-Identifier: GPL-2.0-only

#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/xarray.h>
#include <net/net_namespace.h>
#include <net/psp.h>

#include "psp.h"
#include "psp-nl-gen.h"

DEFINE_XARRAY_ALLOC1(psp_devs);
struct mutex psp_devs_lock;

/**
 * DOC: PSP locking
 *
 * psp_devs_lock protects the psp_devs xarray.
 * Ordering is take the psp_devs_lock and then the instance lock.
 * Each instance is protected by RCU, and has a refcount.
 * When driver unregisters the instance gets flushed, but struct sticks around.
 */

/**
 * psp_dev_check_access() - check if user in a given net ns can access PSP dev
 * @psd:	PSP device structure user is trying to access
 * @net:	net namespace user is in
 *
 * Return: 0 if PSP device should be visible in @net, errno otherwise.
 */
int psp_dev_check_access(struct psp_dev *psd, struct net *net)
{
	if (dev_net(psd->main_netdev) == net)
		return 0;
	return -ENOENT;
}

/**
 * psp_dev_create() - create and register PSP device
 * @netdev:	main netdevice
 * @psd_ops:	driver callbacks
 * @psd_caps:	device capabilities
 * @priv_ptr:	back-pointer to driver private data
 *
 * Return: pointer to allocated PSP device, or ERR_PTR.
 */
struct psp_dev *
psp_dev_create(struct net_device *netdev,
	       struct psp_dev_ops *psd_ops, struct psp_dev_caps *psd_caps,
	       void *priv_ptr)
{
	struct psp_dev *psd;
	static u32 last_id;
	int err;

	if (WARN_ON(!psd_caps->versions ||
		    !psd_ops->set_config))
		return ERR_PTR(-EINVAL);

	psd = kzalloc(sizeof(*psd), GFP_KERNEL);
	if (!psd)
		return ERR_PTR(-ENOMEM);

	psd->main_netdev = netdev;
	psd->ops = psd_ops;
	psd->caps = psd_caps;
	psd->drv_priv = priv_ptr;

	mutex_init(&psd->lock);
	refcount_set(&psd->refcnt, 1);

	mutex_lock(&psp_devs_lock);
	err = xa_alloc_cyclic(&psp_devs, &psd->id, psd, xa_limit_16b,
			      &last_id, GFP_KERNEL);
	if (err) {
		mutex_unlock(&psp_devs_lock);
		kfree(psd);
		return ERR_PTR(err);
	}
	mutex_lock(&psd->lock);
	mutex_unlock(&psp_devs_lock);

	psp_nl_notify_dev(psd, PSP_CMD_DEV_ADD_NTF);

	rcu_assign_pointer(netdev->psp_dev, psd);

	mutex_unlock(&psd->lock);

	return psd;
}
EXPORT_SYMBOL(psp_dev_create);

void psp_dev_destroy(struct psp_dev *psd)
{
	mutex_lock(&psp_devs_lock);
	xa_erase(&psp_devs, psd->id);
	mutex_unlock(&psp_devs_lock);

	mutex_destroy(&psd->lock);
	kfree_rcu(psd, rcu);
}

/**
 * psp_dev_unregister() - unregister PSP device
 * @psd:	PSP device structure
 */
void psp_dev_unregister(struct psp_dev *psd)
{
	mutex_lock(&psp_devs_lock);
	mutex_lock(&psd->lock);

	psp_nl_notify_dev(psd, PSP_CMD_DEV_DEL_NTF);

	/* Wait until psp_dev_destroy() to call xa_erase() to prevent a
	 * different psd from being added to the xarray with this id, while
	 * there are still references to this psd being held.
	 */
	xa_store(&psp_devs, psd->id, NULL, GFP_KERNEL);
	mutex_unlock(&psp_devs_lock);

	rcu_assign_pointer(psd->main_netdev->psp_dev, NULL);

	psd->ops = NULL;
	psd->drv_priv = NULL;

	mutex_unlock(&psd->lock);

	psp_dev_put(psd);
}
EXPORT_SYMBOL(psp_dev_unregister);

static int __init psp_init(void)
{
	mutex_init(&psp_devs_lock);

	return genl_register_family(&psp_nl_family);
}

subsys_initcall(psp_init);
