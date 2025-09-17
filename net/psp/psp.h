/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __PSP_PSP_H
#define __PSP_PSP_H

#include <linux/list.h>
#include <linux/mutex.h>
#include <net/netns/generic.h>
#include <net/psp.h>
#include <net/sock.h>

extern struct xarray psp_devs;
extern struct mutex psp_devs_lock;

void psp_dev_destroy(struct psp_dev *psd);
int psp_dev_check_access(struct psp_dev *psd, struct net *net);

void psp_nl_notify_dev(struct psp_dev *psd, u32 cmd);

static inline void psp_dev_get(struct psp_dev *psd)
{
	refcount_inc(&psd->refcnt);
}

static inline void psp_dev_put(struct psp_dev *psd)
{
	if (refcount_dec_and_test(&psd->refcnt))
		psp_dev_destroy(psd);
}

#endif /* __PSP_PSP_H */
