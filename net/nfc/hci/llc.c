// SPDX-License-Identifier: GPL-2.0-only
/*
 * Link Layer Control manager
 *
 * Copyright (C) 2012  Intel Corporation. All rights reserved.
 */

#include <net/nfc/llc.h>

#include "llc.h"

static LIST_HEAD(llc_engines);

int __init nfc_llc_init(void)
{
	int r;

	r = nfc_llc_nop_register();
	if (r)
		goto exit;

	r = nfc_llc_shdlc_register();
	if (r)
		goto exit;

	return 0;

exit:
	nfc_llc_exit();
	return r;
}

static void nfc_llc_del_engine(struct nfc_llc_engine *llc_engine)
{
	list_del(&llc_engine->entry);
	kfree_const(llc_engine->name);
	kfree(llc_engine);
}

void nfc_llc_exit(void)
{
	struct nfc_llc_engine *llc_engine, *n;

	list_for_each_entry_safe(llc_engine, n, &llc_engines, entry)
		nfc_llc_del_engine(llc_engine);
}

int nfc_llc_register(const char *name, const struct nfc_llc_ops *ops)
{
	struct nfc_llc_engine *llc_engine;

	llc_engine = kzalloc(sizeof(struct nfc_llc_engine), GFP_KERNEL);
	if (llc_engine == NULL)
		return -ENOMEM;

	llc_engine->name = kstrdup_const(name, GFP_KERNEL);
	if (llc_engine->name == NULL) {
		kfree(llc_engine);
		return -ENOMEM;
	}
	llc_engine->ops = ops;

	INIT_LIST_HEAD(&llc_engine->entry);
	list_add_tail(&llc_engine->entry, &llc_engines);

	return 0;
}

static struct nfc_llc_engine *nfc_llc_name_to_engine(const char *name)
{
	struct nfc_llc_engine *llc_engine;

	list_for_each_entry(llc_engine, &llc_engines, entry) {
		if (strcmp(llc_engine->name, name) == 0)
			return llc_engine;
	}

	return NULL;
}

void nfc_llc_unregister(const char *name)
{
	struct nfc_llc_engine *llc_engine;

	llc_engine = nfc_llc_name_to_engine(name);
	if (llc_engine == NULL)
		return;

	nfc_llc_del_engine(llc_engine);
}

struct nfc_llc *nfc_llc_allocate(const char *name, struct nfc_hci_dev *hdev,
				 xmit_to_drv_t xmit_to_drv,
				 rcv_to_hci_t rcv_to_hci, int tx_headroom,
				 int tx_tailroom, llc_failure_t llc_failure)
{
	struct nfc_llc_engine *llc_engine;
	struct nfc_llc *llc;

	llc_engine = nfc_llc_name_to_engine(name);
	if (llc_engine == NULL)
		return NULL;

	llc = kzalloc(sizeof(struct nfc_llc), GFP_KERNEL);
	if (llc == NULL)
		return NULL;

	llc->data = llc_engine->ops->init(hdev, xmit_to_drv, rcv_to_hci,
					  tx_headroom, tx_tailroom,
					  &llc->rx_headroom, &llc->rx_tailroom,
					  llc_failure);
	if (llc->data == NULL) {
		kfree(llc);
		return NULL;
	}
	llc->ops = llc_engine->ops;

	return llc;
}

void nfc_llc_free(struct nfc_llc *llc)
{
	llc->ops->deinit(llc);
	kfree(llc);
}

int nfc_llc_start(struct nfc_llc *llc)
{
	return llc->ops->start(llc);
}
EXPORT_SYMBOL(nfc_llc_start);

int nfc_llc_stop(struct nfc_llc *llc)
{
	return llc->ops->stop(llc);
}
EXPORT_SYMBOL(nfc_llc_stop);

void nfc_llc_rcv_from_drv(struct nfc_llc *llc, struct sk_buff *skb)
{
	llc->ops->rcv_from_drv(llc, skb);
}

int nfc_llc_xmit_from_hci(struct nfc_llc *llc, struct sk_buff *skb)
{
	return llc->ops->xmit_from_hci(llc, skb);
}

void *nfc_llc_get_data(struct nfc_llc *llc)
{
	return llc->data;
}
