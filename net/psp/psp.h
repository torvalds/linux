/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __PSP_PSP_H
#define __PSP_PSP_H

#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <net/netns/generic.h>
#include <net/psp.h>
#include <net/sock.h>

extern struct xarray psp_devs;
extern struct mutex psp_devs_lock;

void psp_dev_free(struct psp_dev *psd);
int psp_dev_check_access(struct psp_dev *psd, struct net *net);

void psp_nl_notify_dev(struct psp_dev *psd, u32 cmd);

struct psp_assoc *psp_assoc_create(struct psp_dev *psd);
struct psp_dev *psp_dev_get_for_sock(struct sock *sk);
void psp_dev_tx_key_del(struct psp_dev *psd, struct psp_assoc *pas);
int psp_sock_assoc_set_rx(struct sock *sk, struct psp_assoc *pas,
			  struct psp_key_parsed *key,
			  struct netlink_ext_ack *extack);
int psp_sock_assoc_set_tx(struct sock *sk, struct psp_dev *psd,
			  u32 version, struct psp_key_parsed *key,
			  struct netlink_ext_ack *extack);
void psp_assocs_key_rotated(struct psp_dev *psd);

static inline void psp_dev_get(struct psp_dev *psd)
{
	refcount_inc(&psd->refcnt);
}

static inline bool psp_dev_tryget(struct psp_dev *psd)
{
	return refcount_inc_not_zero(&psd->refcnt);
}

static inline void psp_dev_put(struct psp_dev *psd)
{
	if (refcount_dec_and_test(&psd->refcnt))
		psp_dev_free(psd);
}

static inline bool psp_dev_is_registered(struct psp_dev *psd)
{
	lockdep_assert_held(&psd->lock);
	return !!psd->ops;
}

#endif /* __PSP_PSP_H */
