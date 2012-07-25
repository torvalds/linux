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

#include <net/nfc/nfc.h>
#include <net/sock.h>

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

#ifdef CONFIG_NFC_LLCP

void nfc_llcp_mac_is_down(struct nfc_dev *dev);
void nfc_llcp_mac_is_up(struct nfc_dev *dev, u32 target_idx,
			u8 comm_mode, u8 rf_mode);
int nfc_llcp_register_device(struct nfc_dev *dev);
void nfc_llcp_unregister_device(struct nfc_dev *dev);
int nfc_llcp_set_remote_gb(struct nfc_dev *dev, u8 *gb, u8 gb_len);
u8 *nfc_llcp_general_bytes(struct nfc_dev *dev, size_t *general_bytes_len);
int __init nfc_llcp_init(void);
void nfc_llcp_exit(void);

#else

static inline void nfc_llcp_mac_is_down(struct nfc_dev *dev)
{
}

static inline void nfc_llcp_mac_is_up(struct nfc_dev *dev, u32 target_idx,
				      u8 comm_mode, u8 rf_mode)
{
}

static inline int nfc_llcp_register_device(struct nfc_dev *dev)
{
	return 0;
}

static inline void nfc_llcp_unregister_device(struct nfc_dev *dev)
{
}

static inline int nfc_llcp_set_remote_gb(struct nfc_dev *dev,
					 u8 *gb, u8 gb_len)
{
	return 0;
}

static inline u8 *nfc_llcp_general_bytes(struct nfc_dev *dev, size_t *gb_len)
{
	*gb_len = 0;
	return NULL;
}

static inline int nfc_llcp_init(void)
{
	return 0;
}

static inline void nfc_llcp_exit(void)
{
}

#endif

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
int nfc_genl_target_lost(struct nfc_dev *dev, u32 target_idx);

int nfc_genl_device_added(struct nfc_dev *dev);
int nfc_genl_device_removed(struct nfc_dev *dev);

int nfc_genl_dep_link_up_event(struct nfc_dev *dev, u32 target_idx,
			       u8 comm_mode, u8 rf_mode);
int nfc_genl_dep_link_down_event(struct nfc_dev *dev);

struct nfc_dev *nfc_get_device(unsigned int idx);

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

int nfc_dev_up(struct nfc_dev *dev);

int nfc_dev_down(struct nfc_dev *dev);

int nfc_start_poll(struct nfc_dev *dev, u32 protocols);

int nfc_stop_poll(struct nfc_dev *dev);

int nfc_dep_link_up(struct nfc_dev *dev, int target_idx, u8 comm_mode);

int nfc_dep_link_down(struct nfc_dev *dev);

int nfc_activate_target(struct nfc_dev *dev, u32 target_idx, u32 protocol);

int nfc_deactivate_target(struct nfc_dev *dev, u32 target_idx);

int nfc_data_exchange(struct nfc_dev *dev, u32 target_idx, struct sk_buff *skb,
		      data_exchange_cb_t cb, void *cb_context);

#endif /* __LOCAL_NFC_H */
