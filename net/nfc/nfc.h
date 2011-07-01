/*
 * Copyright (C) 2011 Instituto Nokia de Tecnologia
 *
 * Authors:
 *    Lauro Ramos Venancio <lauro.venancio@openbossa.org>
 *    Aloisio Almeida Jr <aloisio.almeida@openbossa.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __LOCAL_NFC_H
#define __LOCAL_NFC_H

#include <net/nfc.h>
#include <net/sock.h>

__attribute__((format (printf, 2, 3)))
int nfc_printk(const char *level, const char *fmt, ...);

#define nfc_info(fmt, arg...) nfc_printk(KERN_INFO, fmt, ##arg)
#define nfc_err(fmt, arg...) nfc_printk(KERN_ERR, fmt, ##arg)
#define nfc_dbg(fmt, arg...) pr_debug(fmt "\n", ##arg)

struct nfc_protocol {
	int id;
	struct proto *proto;
	struct module *owner;
	int (*create)(struct net *net, struct socket *sock,
			const struct nfc_protocol *nfc_proto);
};

struct nfc_rawsock {
	struct sock sk;
	struct nfc_dev *dev;
	u32 target_idx;
	struct work_struct tx_work;
	bool tx_work_scheduled;
};
#define nfc_rawsock(sk) ((struct nfc_rawsock *) sk)
#define to_rawsock_sk(_tx_work) \
	((struct sock *) container_of(_tx_work, struct nfc_rawsock, tx_work))

int __init rawsock_init(void);
void rawsock_exit(void);

int __init af_nfc_init(void);
void af_nfc_exit(void);
int nfc_proto_register(const struct nfc_protocol *nfc_proto);
void nfc_proto_unregister(const struct nfc_protocol *nfc_proto);

extern int nfc_devlist_generation;
extern struct mutex nfc_devlist_mutex;

int __init nfc_genl_init(void);
void nfc_genl_exit(void);

void nfc_genl_data_init(struct nfc_genl_data *genl_data);
void nfc_genl_data_exit(struct nfc_genl_data *genl_data);

int nfc_genl_targets_found(struct nfc_dev *dev);

int nfc_genl_device_added(struct nfc_dev *dev);
int nfc_genl_device_removed(struct nfc_dev *dev);

struct nfc_dev *nfc_get_device(unsigned idx);

static inline void nfc_put_device(struct nfc_dev *dev)
{
	put_device(&dev->dev);
}

static inline void nfc_device_iter_init(struct class_dev_iter *iter)
{
	class_dev_iter_init(iter, &nfc_class, NULL, NULL);
}

static inline struct nfc_dev *nfc_device_iter_next(struct class_dev_iter *iter)
{
	struct device *d = class_dev_iter_next(iter);
	if (!d)
		return NULL;

	return to_nfc_dev(d);
}

static inline void nfc_device_iter_exit(struct class_dev_iter *iter)
{
	class_dev_iter_exit(iter);
}

int nfc_start_poll(struct nfc_dev *dev, u32 protocols);

int nfc_stop_poll(struct nfc_dev *dev);

int nfc_activate_target(struct nfc_dev *dev, u32 target_idx, u32 protocol);

int nfc_deactivate_target(struct nfc_dev *dev, u32 target_idx);

int nfc_data_exchange(struct nfc_dev *dev, u32 target_idx,
					struct sk_buff *skb,
					data_exchange_cb_t cb,
					void *cb_context);

#endif /* __LOCAL_NFC_H */
