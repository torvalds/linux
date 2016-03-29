/*
 * Copyright (C) 2014 Fraunhofer ITWM
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Written by:
 * Phoebe Buckheister <phoebe.buckheister@itwm.fraunhofer.de>
 */

#ifndef MAC802154_LLSEC_H
#define MAC802154_LLSEC_H

#include <linux/slab.h>
#include <linux/hashtable.h>
#include <linux/kref.h>
#include <linux/spinlock.h>
#include <net/af_ieee802154.h>
#include <net/ieee802154_netdev.h>

struct mac802154_llsec_key {
	struct ieee802154_llsec_key key;

	/* one tfm for each authsize (4/8/16) */
	struct crypto_aead *tfm[3];
	struct crypto_skcipher *tfm0;

	struct kref ref;
};

struct mac802154_llsec_device_key {
	struct ieee802154_llsec_device_key devkey;

	struct rcu_head rcu;
};

struct mac802154_llsec_device {
	struct ieee802154_llsec_device dev;

	struct hlist_node bucket_s;
	struct hlist_node bucket_hw;

	/* protects dev.frame_counter and the elements of dev.keys */
	spinlock_t lock;

	struct rcu_head rcu;
};

struct mac802154_llsec_seclevel {
	struct ieee802154_llsec_seclevel level;

	struct rcu_head rcu;
};

struct mac802154_llsec {
	struct ieee802154_llsec_params params;
	struct ieee802154_llsec_table table;

	DECLARE_HASHTABLE(devices_short, 6);
	DECLARE_HASHTABLE(devices_hw, 6);

	/* protects params, all other fields are fine with RCU */
	rwlock_t lock;
};

void mac802154_llsec_init(struct mac802154_llsec *sec);
void mac802154_llsec_destroy(struct mac802154_llsec *sec);

int mac802154_llsec_get_params(struct mac802154_llsec *sec,
			       struct ieee802154_llsec_params *params);
int mac802154_llsec_set_params(struct mac802154_llsec *sec,
			       const struct ieee802154_llsec_params *params,
			       int changed);

int mac802154_llsec_key_add(struct mac802154_llsec *sec,
			    const struct ieee802154_llsec_key_id *id,
			    const struct ieee802154_llsec_key *key);
int mac802154_llsec_key_del(struct mac802154_llsec *sec,
			    const struct ieee802154_llsec_key_id *key);

int mac802154_llsec_dev_add(struct mac802154_llsec *sec,
			    const struct ieee802154_llsec_device *dev);
int mac802154_llsec_dev_del(struct mac802154_llsec *sec,
			    __le64 device_addr);

int mac802154_llsec_devkey_add(struct mac802154_llsec *sec,
			       __le64 dev_addr,
			       const struct ieee802154_llsec_device_key *key);
int mac802154_llsec_devkey_del(struct mac802154_llsec *sec,
			       __le64 dev_addr,
			       const struct ieee802154_llsec_device_key *key);

int mac802154_llsec_seclevel_add(struct mac802154_llsec *sec,
				 const struct ieee802154_llsec_seclevel *sl);
int mac802154_llsec_seclevel_del(struct mac802154_llsec *sec,
				 const struct ieee802154_llsec_seclevel *sl);

int mac802154_llsec_encrypt(struct mac802154_llsec *sec, struct sk_buff *skb);
int mac802154_llsec_decrypt(struct mac802154_llsec *sec, struct sk_buff *skb);

#endif /* MAC802154_LLSEC_H */
