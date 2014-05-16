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

#include <linux/err.h>
#include <linux/bug.h>
#include <linux/completion.h>
#include <net/ieee802154.h>

#include "mac802154.h"
#include "llsec.h"

static void llsec_key_put(struct mac802154_llsec_key *key);
static bool llsec_key_id_equal(const struct ieee802154_llsec_key_id *a,
			       const struct ieee802154_llsec_key_id *b);

static void llsec_dev_free(struct mac802154_llsec_device *dev);

void mac802154_llsec_init(struct mac802154_llsec *sec)
{
	memset(sec, 0, sizeof(*sec));

	memset(&sec->params.default_key_source, 0xFF, IEEE802154_ADDR_LEN);

	INIT_LIST_HEAD(&sec->table.security_levels);
	INIT_LIST_HEAD(&sec->table.devices);
	INIT_LIST_HEAD(&sec->table.keys);
	hash_init(sec->devices_short);
	hash_init(sec->devices_hw);
	rwlock_init(&sec->lock);
}

void mac802154_llsec_destroy(struct mac802154_llsec *sec)
{
	struct ieee802154_llsec_seclevel *sl, *sn;
	struct ieee802154_llsec_device *dev, *dn;
	struct ieee802154_llsec_key_entry *key, *kn;

	list_for_each_entry_safe(sl, sn, &sec->table.security_levels, list) {
		struct mac802154_llsec_seclevel *msl;

		msl = container_of(sl, struct mac802154_llsec_seclevel, level);
		list_del(&sl->list);
		kfree(msl);
	}

	list_for_each_entry_safe(dev, dn, &sec->table.devices, list) {
		struct mac802154_llsec_device *mdev;

		mdev = container_of(dev, struct mac802154_llsec_device, dev);
		list_del(&dev->list);
		llsec_dev_free(mdev);
	}

	list_for_each_entry_safe(key, kn, &sec->table.keys, list) {
		struct mac802154_llsec_key *mkey;

		mkey = container_of(key->key, struct mac802154_llsec_key, key);
		list_del(&key->list);
		llsec_key_put(mkey);
		kfree(key);
	}
}



int mac802154_llsec_get_params(struct mac802154_llsec *sec,
			       struct ieee802154_llsec_params *params)
{
	read_lock_bh(&sec->lock);
	*params = sec->params;
	read_unlock_bh(&sec->lock);

	return 0;
}

int mac802154_llsec_set_params(struct mac802154_llsec *sec,
			       const struct ieee802154_llsec_params *params,
			       int changed)
{
	write_lock_bh(&sec->lock);

	if (changed & IEEE802154_LLSEC_PARAM_ENABLED)
		sec->params.enabled = params->enabled;
	if (changed & IEEE802154_LLSEC_PARAM_FRAME_COUNTER)
		sec->params.frame_counter = params->frame_counter;
	if (changed & IEEE802154_LLSEC_PARAM_OUT_LEVEL)
		sec->params.out_level = params->out_level;
	if (changed & IEEE802154_LLSEC_PARAM_OUT_KEY)
		sec->params.out_key = params->out_key;
	if (changed & IEEE802154_LLSEC_PARAM_KEY_SOURCE)
		sec->params.default_key_source = params->default_key_source;
	if (changed & IEEE802154_LLSEC_PARAM_PAN_ID)
		sec->params.pan_id = params->pan_id;
	if (changed & IEEE802154_LLSEC_PARAM_HWADDR)
		sec->params.hwaddr = params->hwaddr;
	if (changed & IEEE802154_LLSEC_PARAM_COORD_HWADDR)
		sec->params.coord_hwaddr = params->coord_hwaddr;
	if (changed & IEEE802154_LLSEC_PARAM_COORD_SHORTADDR)
		sec->params.coord_shortaddr = params->coord_shortaddr;

	write_unlock_bh(&sec->lock);

	return 0;
}



static struct mac802154_llsec_key*
llsec_key_alloc(const struct ieee802154_llsec_key *template)
{
	const int authsizes[3] = { 4, 8, 16 };
	struct mac802154_llsec_key *key;
	int i;

	key = kzalloc(sizeof(*key), GFP_KERNEL);
	if (!key)
		return NULL;

	kref_init(&key->ref);
	key->key = *template;

	BUILD_BUG_ON(ARRAY_SIZE(authsizes) != ARRAY_SIZE(key->tfm));

	for (i = 0; i < ARRAY_SIZE(key->tfm); i++) {
		key->tfm[i] = crypto_alloc_aead("ccm(aes)", 0,
						CRYPTO_ALG_ASYNC);
		if (!key->tfm[i])
			goto err_tfm;
		if (crypto_aead_setkey(key->tfm[i], template->key,
				       IEEE802154_LLSEC_KEY_SIZE))
			goto err_tfm;
		if (crypto_aead_setauthsize(key->tfm[i], authsizes[i]))
			goto err_tfm;
	}

	key->tfm0 = crypto_alloc_blkcipher("ctr(aes)", 0, CRYPTO_ALG_ASYNC);
	if (!key->tfm0)
		goto err_tfm;

	if (crypto_blkcipher_setkey(key->tfm0, template->key,
				    IEEE802154_LLSEC_KEY_SIZE))
		goto err_tfm0;

	return key;

err_tfm0:
	crypto_free_blkcipher(key->tfm0);
err_tfm:
	for (i = 0; i < ARRAY_SIZE(key->tfm); i++)
		if (key->tfm[i])
			crypto_free_aead(key->tfm[i]);

	kfree(key);
	return NULL;
}

static void llsec_key_release(struct kref *ref)
{
	struct mac802154_llsec_key *key;
	int i;

	key = container_of(ref, struct mac802154_llsec_key, ref);

	for (i = 0; i < ARRAY_SIZE(key->tfm); i++)
		crypto_free_aead(key->tfm[i]);

	crypto_free_blkcipher(key->tfm0);
	kfree(key);
}

static struct mac802154_llsec_key*
llsec_key_get(struct mac802154_llsec_key *key)
{
	kref_get(&key->ref);
	return key;
}

static void llsec_key_put(struct mac802154_llsec_key *key)
{
	kref_put(&key->ref, llsec_key_release);
}

static bool llsec_key_id_equal(const struct ieee802154_llsec_key_id *a,
			       const struct ieee802154_llsec_key_id *b)
{
	if (a->mode != b->mode)
		return false;

	if (a->mode == IEEE802154_SCF_KEY_IMPLICIT)
		return ieee802154_addr_equal(&a->device_addr, &b->device_addr);

	if (a->id != b->id)
		return false;

	switch (a->mode) {
	case IEEE802154_SCF_KEY_SHORT_INDEX:
		return a->short_source == b->short_source;
	case IEEE802154_SCF_KEY_HW_INDEX:
		return a->extended_source == b->extended_source;
	}

	return false;
}

int mac802154_llsec_key_add(struct mac802154_llsec *sec,
			    const struct ieee802154_llsec_key_id *id,
			    const struct ieee802154_llsec_key *key)
{
	struct mac802154_llsec_key *mkey = NULL;
	struct ieee802154_llsec_key_entry *pos, *new;

	if (!(key->frame_types & (1 << IEEE802154_FC_TYPE_MAC_CMD)) &&
	    key->cmd_frame_ids)
		return -EINVAL;

	list_for_each_entry(pos, &sec->table.keys, list) {
		if (llsec_key_id_equal(&pos->id, id))
			return -EEXIST;

		if (memcmp(pos->key->key, key->key,
			   IEEE802154_LLSEC_KEY_SIZE))
			continue;

		mkey = container_of(pos->key, struct mac802154_llsec_key, key);

		/* Don't allow multiple instances of the same AES key to have
		 * different allowed frame types/command frame ids, as this is
		 * not possible in the 802.15.4 PIB.
		 */
		if (pos->key->frame_types != key->frame_types ||
		    pos->key->cmd_frame_ids != key->cmd_frame_ids)
			return -EEXIST;

		break;
	}

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	if (!mkey)
		mkey = llsec_key_alloc(key);
	else
		mkey = llsec_key_get(mkey);

	if (!mkey)
		goto fail;

	new->id = *id;
	new->key = &mkey->key;

	list_add_rcu(&new->list, &sec->table.keys);

	return 0;

fail:
	kfree(new);
	return -ENOMEM;
}

int mac802154_llsec_key_del(struct mac802154_llsec *sec,
			    const struct ieee802154_llsec_key_id *key)
{
	struct ieee802154_llsec_key_entry *pos;

	list_for_each_entry(pos, &sec->table.keys, list) {
		struct mac802154_llsec_key *mkey;

		mkey = container_of(pos->key, struct mac802154_llsec_key, key);

		if (llsec_key_id_equal(&pos->id, key)) {
			llsec_key_put(mkey);
			return 0;
		}
	}

	return -ENOENT;
}



static bool llsec_dev_use_shortaddr(__le16 short_addr)
{
	return short_addr != cpu_to_le16(IEEE802154_ADDR_UNDEF) &&
		short_addr != cpu_to_le16(0xffff);
}

static u32 llsec_dev_hash_short(__le16 short_addr, __le16 pan_id)
{
	return ((__force u16) short_addr) << 16 | (__force u16) pan_id;
}

static u64 llsec_dev_hash_long(__le64 hwaddr)
{
	return (__force u64) hwaddr;
}

static struct mac802154_llsec_device*
llsec_dev_find_short(struct mac802154_llsec *sec, __le16 short_addr,
		     __le16 pan_id)
{
	struct mac802154_llsec_device *dev;
	u32 key = llsec_dev_hash_short(short_addr, pan_id);

	hash_for_each_possible_rcu(sec->devices_short, dev, bucket_s, key) {
		if (dev->dev.short_addr == short_addr &&
		    dev->dev.pan_id == pan_id)
			return dev;
	}

	return NULL;
}

static struct mac802154_llsec_device*
llsec_dev_find_long(struct mac802154_llsec *sec, __le64 hwaddr)
{
	struct mac802154_llsec_device *dev;
	u64 key = llsec_dev_hash_long(hwaddr);

	hash_for_each_possible_rcu(sec->devices_hw, dev, bucket_hw, key) {
		if (dev->dev.hwaddr == hwaddr)
			return dev;
	}

	return NULL;
}

static void llsec_dev_free(struct mac802154_llsec_device *dev)
{
	struct ieee802154_llsec_device_key *pos, *pn;
	struct mac802154_llsec_device_key *devkey;

	list_for_each_entry_safe(pos, pn, &dev->dev.keys, list) {
		devkey = container_of(pos, struct mac802154_llsec_device_key,
				      devkey);

		list_del(&pos->list);
		kfree(devkey);
	}

	kfree(dev);
}

int mac802154_llsec_dev_add(struct mac802154_llsec *sec,
			    const struct ieee802154_llsec_device *dev)
{
	struct mac802154_llsec_device *entry;
	u32 skey = llsec_dev_hash_short(dev->short_addr, dev->pan_id);
	u64 hwkey = llsec_dev_hash_long(dev->hwaddr);

	BUILD_BUG_ON(sizeof(hwkey) != IEEE802154_ADDR_LEN);

	if ((llsec_dev_use_shortaddr(dev->short_addr) &&
	     llsec_dev_find_short(sec, dev->short_addr, dev->pan_id)) ||
	     llsec_dev_find_long(sec, dev->hwaddr))
		return -EEXIST;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->dev = *dev;
	spin_lock_init(&entry->lock);
	INIT_LIST_HEAD(&entry->dev.keys);

	if (llsec_dev_use_shortaddr(dev->short_addr))
		hash_add_rcu(sec->devices_short, &entry->bucket_s, skey);
	else
		INIT_HLIST_NODE(&entry->bucket_s);

	hash_add_rcu(sec->devices_hw, &entry->bucket_hw, hwkey);
	list_add_tail_rcu(&entry->dev.list, &sec->table.devices);

	return 0;
}

static void llsec_dev_free_rcu(struct rcu_head *rcu)
{
	llsec_dev_free(container_of(rcu, struct mac802154_llsec_device, rcu));
}

int mac802154_llsec_dev_del(struct mac802154_llsec *sec, __le64 device_addr)
{
	struct mac802154_llsec_device *pos;

	pos = llsec_dev_find_long(sec, device_addr);
	if (!pos)
		return -ENOENT;

	hash_del_rcu(&pos->bucket_s);
	hash_del_rcu(&pos->bucket_hw);
	call_rcu(&pos->rcu, llsec_dev_free_rcu);

	return 0;
}



static struct mac802154_llsec_device_key*
llsec_devkey_find(struct mac802154_llsec_device *dev,
		  const struct ieee802154_llsec_key_id *key)
{
	struct ieee802154_llsec_device_key *devkey;

	list_for_each_entry_rcu(devkey, &dev->dev.keys, list) {
		if (!llsec_key_id_equal(key, &devkey->key_id))
			continue;

		return container_of(devkey, struct mac802154_llsec_device_key,
				    devkey);
	}

	return NULL;
}

int mac802154_llsec_devkey_add(struct mac802154_llsec *sec,
			       __le64 dev_addr,
			       const struct ieee802154_llsec_device_key *key)
{
	struct mac802154_llsec_device *dev;
	struct mac802154_llsec_device_key *devkey;

	dev = llsec_dev_find_long(sec, dev_addr);

	if (!dev)
		return -ENOENT;

	if (llsec_devkey_find(dev, &key->key_id))
		return -EEXIST;

	devkey = kmalloc(sizeof(*devkey), GFP_KERNEL);
	if (!devkey)
		return -ENOMEM;

	devkey->devkey = *key;
	list_add_tail_rcu(&devkey->devkey.list, &dev->dev.keys);
	return 0;
}

int mac802154_llsec_devkey_del(struct mac802154_llsec *sec,
			       __le64 dev_addr,
			       const struct ieee802154_llsec_device_key *key)
{
	struct mac802154_llsec_device *dev;
	struct mac802154_llsec_device_key *devkey;

	dev = llsec_dev_find_long(sec, dev_addr);

	if (!dev)
		return -ENOENT;

	devkey = llsec_devkey_find(dev, &key->key_id);
	if (!devkey)
		return -ENOENT;

	list_del_rcu(&devkey->devkey.list);
	kfree_rcu(devkey, rcu);
	return 0;
}



static struct mac802154_llsec_seclevel*
llsec_find_seclevel(const struct mac802154_llsec *sec,
		    const struct ieee802154_llsec_seclevel *sl)
{
	struct ieee802154_llsec_seclevel *pos;

	list_for_each_entry(pos, &sec->table.security_levels, list) {
		if (pos->frame_type != sl->frame_type ||
		    (pos->frame_type == IEEE802154_FC_TYPE_MAC_CMD &&
		     pos->cmd_frame_id != sl->cmd_frame_id) ||
		    pos->device_override != sl->device_override ||
		    pos->sec_levels != sl->sec_levels)
			continue;

		return container_of(pos, struct mac802154_llsec_seclevel,
				    level);
	}

	return NULL;
}

int mac802154_llsec_seclevel_add(struct mac802154_llsec *sec,
				 const struct ieee802154_llsec_seclevel *sl)
{
	struct mac802154_llsec_seclevel *entry;

	if (llsec_find_seclevel(sec, sl))
		return -EEXIST;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->level = *sl;

	list_add_tail_rcu(&entry->level.list, &sec->table.security_levels);

	return 0;
}

int mac802154_llsec_seclevel_del(struct mac802154_llsec *sec,
				 const struct ieee802154_llsec_seclevel *sl)
{
	struct mac802154_llsec_seclevel *pos;

	pos = llsec_find_seclevel(sec, sl);
	if (!pos)
		return -ENOENT;

	list_del_rcu(&pos->level.list);
	kfree_rcu(pos, rcu);

	return 0;
}
