/* multipath.c: IPV4 multipath algorithm support.
 *
 * Copyright (C) 2004, 2005 Einar Lueck <elueck@de.ibm.com>
 * Copyright (C) 2005 David S. Miller <davem@davemloft.net>
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>

#include <net/ip_mp_alg.h>

static DEFINE_SPINLOCK(alg_table_lock);
struct ip_mp_alg_ops *ip_mp_alg_table[IP_MP_ALG_MAX + 1];

int multipath_alg_register(struct ip_mp_alg_ops *ops, enum ip_mp_alg n)
{
	struct ip_mp_alg_ops **slot;
	int err;

	if (n < IP_MP_ALG_NONE || n > IP_MP_ALG_MAX ||
	    !ops->mp_alg_select_route)
		return -EINVAL;

	spin_lock(&alg_table_lock);
	slot = &ip_mp_alg_table[n];
	if (*slot != NULL) {
		err = -EBUSY;
	} else {
		*slot = ops;
		err = 0;
	}
	spin_unlock(&alg_table_lock);

	return err;
}
EXPORT_SYMBOL(multipath_alg_register);

void multipath_alg_unregister(struct ip_mp_alg_ops *ops, enum ip_mp_alg n)
{
	struct ip_mp_alg_ops **slot;

	if (n < IP_MP_ALG_NONE || n > IP_MP_ALG_MAX)
		return;

	spin_lock(&alg_table_lock);
	slot = &ip_mp_alg_table[n];
	if (*slot == ops)
		*slot = NULL;
	spin_unlock(&alg_table_lock);

	synchronize_net();
}
EXPORT_SYMBOL(multipath_alg_unregister);
