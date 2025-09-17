/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __NET_PSP_HELPERS_H
#define __NET_PSP_HELPERS_H

#include <net/psp/types.h>

struct inet_timewait_sock;

/* Driver-facing API */
struct psp_dev *
psp_dev_create(struct net_device *netdev, struct psp_dev_ops *psd_ops,
	       struct psp_dev_caps *psd_caps, void *priv_ptr);
void psp_dev_unregister(struct psp_dev *psd);

/* Kernel-facing API */
static inline void psp_sk_assoc_free(struct sock *sk) { }
static inline void psp_twsk_assoc_free(struct inet_timewait_sock *tw) { }

#endif /* __NET_PSP_HELPERS_H */
