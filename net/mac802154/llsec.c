// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Fraunhofer ITWM
 *
 * Written by:
 * Phoebe Buckheister <phoebe.buckheister@itwm.fraunhofer.de>
 */

#include <linux/err.h>
#include <linux/bug.h>
#include <linux/completion.h>
#include <linux/ieee802154.h>
#include <linux/rculist.h>

#include <crypto/aead.h>
#include <crypto/skcipher.h>

#include "ieee802154_i.h"
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
		kfree_sensitive(msl);
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
		kfree_sensitive(key);
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
		if (IS_ERR(key->tfm[i]))
			goto err_tfm;
		if (crypto_aead_setkey(key->tfm[i], template->key,
				       IEEE802154_LLSEC_KEY_SIZE))
			goto err_tfm;
		if (crypto_aead_setauthsize(key->tfm[i], authsizes[i]))
			goto err_tfm;
	}

	key->tfm0 = crypto_alloc_sync_skcipher("ctr(aes)", 0, 0);
	if (IS_ERR(key->tfm0))
		goto err_tfm;

	if (crypto_sync_skcipher_setkey(key->tfm0, template->key,
				   IEEE802154_LLSEC_KEY_SIZE))
		goto err_tfm0;

	return key;

err_tfm0:
	crypto_free_sync_skcipher(key->tfm0);
err_tfm:
	for (i = 0; i < ARRAY_SIZE(key->tfm); i++)
		if (!IS_ERR_OR_NULL(key->tfm[i]))
			crypto_free_aead(key->tfm[i]);

	kfree_sensitive(key);
	return NULL;
}

static void llsec_key_release(struct kref *ref)
{
	struct mac802154_llsec_key *key;
	int i;

	key = container_of(ref, struct mac802154_llsec_key, ref);

	for (i = 0; i < ARRAY_SIZE(key->tfm); i++)
		crypto_free_aead(key->tfm[i]);

	crypto_free_sync_skcipher(key->tfm0);
	kfree_sensitive(key);
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
	case IEEE802154_SCF_KEY_INDEX:
		return true;
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
	kfree_sensitive(new);
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
			list_del_rcu(&pos->list);
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
	return ((__force u16)short_addr) << 16 | (__force u16)pan_id;
}

static u64 llsec_dev_hash_long(__le64 hwaddr)
{
	return (__force u64)hwaddr;
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
		kfree_sensitive(devkey);
	}

	kfree_sensitive(dev);
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
	list_del_rcu(&pos->dev.list);
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

static int llsec_recover_addr(struct mac802154_llsec *sec,
			      struct ieee802154_addr *addr)
{
	__le16 caddr = sec->params.coord_shortaddr;

	addr->pan_id = sec->params.pan_id;

	if (caddr == cpu_to_le16(IEEE802154_ADDR_BROADCAST)) {
		return -EINVAL;
	} else if (caddr == cpu_to_le16(IEEE802154_ADDR_UNDEF)) {
		addr->extended_addr = sec->params.coord_hwaddr;
		addr->mode = IEEE802154_ADDR_LONG;
	} else {
		addr->short_addr = sec->params.coord_shortaddr;
		addr->mode = IEEE802154_ADDR_SHORT;
	}

	return 0;
}

static struct mac802154_llsec_key*
llsec_lookup_key(struct mac802154_llsec *sec,
		 const struct ieee802154_hdr *hdr,
		 const struct ieee802154_addr *addr,
		 struct ieee802154_llsec_key_id *key_id)
{
	struct ieee802154_addr devaddr = *addr;
	u8 key_id_mode = hdr->sec.key_id_mode;
	struct ieee802154_llsec_key_entry *key_entry;
	struct mac802154_llsec_key *key;

	if (key_id_mode == IEEE802154_SCF_KEY_IMPLICIT &&
	    devaddr.mode == IEEE802154_ADDR_NONE) {
		if (hdr->fc.type == IEEE802154_FC_TYPE_BEACON) {
			devaddr.extended_addr = sec->params.coord_hwaddr;
			devaddr.mode = IEEE802154_ADDR_LONG;
		} else if (llsec_recover_addr(sec, &devaddr) < 0) {
			return NULL;
		}
	}

	list_for_each_entry_rcu(key_entry, &sec->table.keys, list) {
		const struct ieee802154_llsec_key_id *id = &key_entry->id;

		if (!(key_entry->key->frame_types & BIT(hdr->fc.type)))
			continue;

		if (id->mode != key_id_mode)
			continue;

		if (key_id_mode == IEEE802154_SCF_KEY_IMPLICIT) {
			if (ieee802154_addr_equal(&devaddr, &id->device_addr))
				goto found;
		} else {
			if (id->id != hdr->sec.key_id)
				continue;

			if ((key_id_mode == IEEE802154_SCF_KEY_INDEX) ||
			    (key_id_mode == IEEE802154_SCF_KEY_SHORT_INDEX &&
			     id->short_source == hdr->sec.short_src) ||
			    (key_id_mode == IEEE802154_SCF_KEY_HW_INDEX &&
			     id->extended_source == hdr->sec.extended_src))
				goto found;
		}
	}

	return NULL;

found:
	key = container_of(key_entry->key, struct mac802154_llsec_key, key);
	if (key_id)
		*key_id = key_entry->id;
	return llsec_key_get(key);
}

static void llsec_geniv(u8 iv[16], __le64 addr,
			const struct ieee802154_sechdr *sec)
{
	__be64 addr_bytes = (__force __be64) swab64((__force u64) addr);
	__be32 frame_counter = (__force __be32) swab32((__force u32) sec->frame_counter);

	iv[0] = 1; /* L' = L - 1 = 1 */
	memcpy(iv + 1, &addr_bytes, sizeof(addr_bytes));
	memcpy(iv + 9, &frame_counter, sizeof(frame_counter));
	iv[13] = sec->level;
	iv[14] = 0;
	iv[15] = 1;
}

static int
llsec_do_encrypt_unauth(struct sk_buff *skb, const struct mac802154_llsec *sec,
			const struct ieee802154_hdr *hdr,
			struct mac802154_llsec_key *key)
{
	u8 iv[16];
	struct scatterlist src;
	SYNC_SKCIPHER_REQUEST_ON_STACK(req, key->tfm0);
	int err, datalen;
	unsigned char *data;

	llsec_geniv(iv, sec->params.hwaddr, &hdr->sec);
	/* Compute data payload offset and data length */
	data = skb_mac_header(skb) + skb->mac_len;
	datalen = skb_tail_pointer(skb) - data;
	sg_init_one(&src, data, datalen);

	skcipher_request_set_sync_tfm(req, key->tfm0);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, &src, &src, datalen, iv);
	err = crypto_skcipher_encrypt(req);
	skcipher_request_zero(req);
	return err;
}

static struct crypto_aead*
llsec_tfm_by_len(struct mac802154_llsec_key *key, int authlen)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(key->tfm); i++)
		if (crypto_aead_authsize(key->tfm[i]) == authlen)
			return key->tfm[i];

	BUG();
}

static int
llsec_do_encrypt_auth(struct sk_buff *skb, const struct mac802154_llsec *sec,
		      const struct ieee802154_hdr *hdr,
		      struct mac802154_llsec_key *key)
{
	u8 iv[16];
	unsigned char *data;
	int authlen, assoclen, datalen, rc;
	struct scatterlist sg;
	struct aead_request *req;

	authlen = ieee802154_sechdr_authtag_len(&hdr->sec);
	llsec_geniv(iv, sec->params.hwaddr, &hdr->sec);

	req = aead_request_alloc(llsec_tfm_by_len(key, authlen), GFP_ATOMIC);
	if (!req)
		return -ENOMEM;

	assoclen = skb->mac_len;

	data = skb_mac_header(skb) + skb->mac_len;
	datalen = skb_tail_pointer(skb) - data;

	skb_put(skb, authlen);

	sg_init_one(&sg, skb_mac_header(skb), assoclen + datalen + authlen);

	if (!(hdr->sec.level & IEEE802154_SCF_SECLEVEL_ENC)) {
		assoclen += datalen;
		datalen = 0;
	}

	aead_request_set_callback(req, 0, NULL, NULL);
	aead_request_set_crypt(req, &sg, &sg, datalen, iv);
	aead_request_set_ad(req, assoclen);

	rc = crypto_aead_encrypt(req);

	kfree_sensitive(req);

	return rc;
}

static int llsec_do_encrypt(struct sk_buff *skb,
			    const struct mac802154_llsec *sec,
			    const struct ieee802154_hdr *hdr,
			    struct mac802154_llsec_key *key)
{
	if (hdr->sec.level == IEEE802154_SCF_SECLEVEL_ENC)
		return llsec_do_encrypt_unauth(skb, sec, hdr, key);
	else
		return llsec_do_encrypt_auth(skb, sec, hdr, key);
}

int mac802154_llsec_encrypt(struct mac802154_llsec *sec, struct sk_buff *skb)
{
	struct ieee802154_hdr hdr;
	int rc, authlen, hlen;
	struct mac802154_llsec_key *key;
	u32 frame_ctr;

	hlen = ieee802154_hdr_pull(skb, &hdr);

	/* TODO: control frames security support */
	if (hlen < 0 ||
	    (hdr.fc.type != IEEE802154_FC_TYPE_DATA &&
	     hdr.fc.type != IEEE802154_FC_TYPE_BEACON))
		return -EINVAL;

	if (!hdr.fc.security_enabled ||
	    (hdr.sec.level == IEEE802154_SCF_SECLEVEL_NONE)) {
		skb_push(skb, hlen);
		return 0;
	}

	authlen = ieee802154_sechdr_authtag_len(&hdr.sec);

	if (skb->len + hlen + authlen + IEEE802154_MFR_SIZE > IEEE802154_MTU)
		return -EMSGSIZE;

	rcu_read_lock();

	read_lock_bh(&sec->lock);

	if (!sec->params.enabled) {
		rc = -EINVAL;
		goto fail_read;
	}

	key = llsec_lookup_key(sec, &hdr, &hdr.dest, NULL);
	if (!key) {
		rc = -ENOKEY;
		goto fail_read;
	}

	read_unlock_bh(&sec->lock);

	write_lock_bh(&sec->lock);

	frame_ctr = be32_to_cpu(sec->params.frame_counter);
	hdr.sec.frame_counter = cpu_to_le32(frame_ctr);
	if (frame_ctr == 0xFFFFFFFF) {
		write_unlock_bh(&sec->lock);
		llsec_key_put(key);
		rc = -EOVERFLOW;
		goto fail;
	}

	sec->params.frame_counter = cpu_to_be32(frame_ctr + 1);

	write_unlock_bh(&sec->lock);

	rcu_read_unlock();

	skb->mac_len = ieee802154_hdr_push(skb, &hdr);
	skb_reset_mac_header(skb);

	rc = llsec_do_encrypt(skb, sec, &hdr, key);
	llsec_key_put(key);

	return rc;

fail_read:
	read_unlock_bh(&sec->lock);
fail:
	rcu_read_unlock();
	return rc;
}

static struct mac802154_llsec_device*
llsec_lookup_dev(struct mac802154_llsec *sec,
		 const struct ieee802154_addr *addr)
{
	struct ieee802154_addr devaddr = *addr;
	struct mac802154_llsec_device *dev = NULL;

	if (devaddr.mode == IEEE802154_ADDR_NONE &&
	    llsec_recover_addr(sec, &devaddr) < 0)
		return NULL;

	if (devaddr.mode == IEEE802154_ADDR_SHORT) {
		u32 key = llsec_dev_hash_short(devaddr.short_addr,
					       devaddr.pan_id);

		hash_for_each_possible_rcu(sec->devices_short, dev,
					   bucket_s, key) {
			if (dev->dev.pan_id == devaddr.pan_id &&
			    dev->dev.short_addr == devaddr.short_addr)
				return dev;
		}
	} else {
		u64 key = llsec_dev_hash_long(devaddr.extended_addr);

		hash_for_each_possible_rcu(sec->devices_hw, dev,
					   bucket_hw, key) {
			if (dev->dev.hwaddr == devaddr.extended_addr)
				return dev;
		}
	}

	return NULL;
}

static int
llsec_lookup_seclevel(const struct mac802154_llsec *sec,
		      u8 frame_type, u8 cmd_frame_id,
		      struct ieee802154_llsec_seclevel *rlevel)
{
	struct ieee802154_llsec_seclevel *level;

	list_for_each_entry_rcu(level, &sec->table.security_levels, list) {
		if (level->frame_type == frame_type &&
		    (frame_type != IEEE802154_FC_TYPE_MAC_CMD ||
		     level->cmd_frame_id == cmd_frame_id)) {
			*rlevel = *level;
			return 0;
		}
	}

	return -EINVAL;
}

static int
llsec_do_decrypt_unauth(struct sk_buff *skb, const struct mac802154_llsec *sec,
			const struct ieee802154_hdr *hdr,
			struct mac802154_llsec_key *key, __le64 dev_addr)
{
	u8 iv[16];
	unsigned char *data;
	int datalen;
	struct scatterlist src;
	SYNC_SKCIPHER_REQUEST_ON_STACK(req, key->tfm0);
	int err;

	llsec_geniv(iv, dev_addr, &hdr->sec);
	data = skb_mac_header(skb) + skb->mac_len;
	datalen = skb_tail_pointer(skb) - data;

	sg_init_one(&src, data, datalen);

	skcipher_request_set_sync_tfm(req, key->tfm0);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, &src, &src, datalen, iv);

	err = crypto_skcipher_decrypt(req);
	skcipher_request_zero(req);
	return err;
}

static int
llsec_do_decrypt_auth(struct sk_buff *skb, const struct mac802154_llsec *sec,
		      const struct ieee802154_hdr *hdr,
		      struct mac802154_llsec_key *key, __le64 dev_addr)
{
	u8 iv[16];
	unsigned char *data;
	int authlen, datalen, assoclen, rc;
	struct scatterlist sg;
	struct aead_request *req;

	authlen = ieee802154_sechdr_authtag_len(&hdr->sec);
	llsec_geniv(iv, dev_addr, &hdr->sec);

	req = aead_request_alloc(llsec_tfm_by_len(key, authlen), GFP_ATOMIC);
	if (!req)
		return -ENOMEM;

	assoclen = skb->mac_len;

	data = skb_mac_header(skb) + skb->mac_len;
	datalen = skb_tail_pointer(skb) - data;

	sg_init_one(&sg, skb_mac_header(skb), assoclen + datalen);

	if (!(hdr->sec.level & IEEE802154_SCF_SECLEVEL_ENC)) {
		assoclen += datalen - authlen;
		datalen = authlen;
	}

	aead_request_set_callback(req, 0, NULL, NULL);
	aead_request_set_crypt(req, &sg, &sg, datalen, iv);
	aead_request_set_ad(req, assoclen);

	rc = crypto_aead_decrypt(req);

	kfree_sensitive(req);
	skb_trim(skb, skb->len - authlen);

	return rc;
}

static int
llsec_do_decrypt(struct sk_buff *skb, const struct mac802154_llsec *sec,
		 const struct ieee802154_hdr *hdr,
		 struct mac802154_llsec_key *key, __le64 dev_addr)
{
	if (hdr->sec.level == IEEE802154_SCF_SECLEVEL_ENC)
		return llsec_do_decrypt_unauth(skb, sec, hdr, key, dev_addr);
	else
		return llsec_do_decrypt_auth(skb, sec, hdr, key, dev_addr);
}

static int
llsec_update_devkey_record(struct mac802154_llsec_device *dev,
			   const struct ieee802154_llsec_key_id *in_key)
{
	struct mac802154_llsec_device_key *devkey;

	devkey = llsec_devkey_find(dev, in_key);

	if (!devkey) {
		struct mac802154_llsec_device_key *next;

		next = kzalloc(sizeof(*devkey), GFP_ATOMIC);
		if (!next)
			return -ENOMEM;

		next->devkey.key_id = *in_key;

		spin_lock_bh(&dev->lock);

		devkey = llsec_devkey_find(dev, in_key);
		if (!devkey)
			list_add_rcu(&next->devkey.list, &dev->dev.keys);
		else
			kfree_sensitive(next);

		spin_unlock_bh(&dev->lock);
	}

	return 0;
}

static int
llsec_update_devkey_info(struct mac802154_llsec_device *dev,
			 const struct ieee802154_llsec_key_id *in_key,
			 u32 frame_counter)
{
	struct mac802154_llsec_device_key *devkey = NULL;

	if (dev->dev.key_mode == IEEE802154_LLSEC_DEVKEY_RESTRICT) {
		devkey = llsec_devkey_find(dev, in_key);
		if (!devkey)
			return -ENOENT;
	}

	if (dev->dev.key_mode == IEEE802154_LLSEC_DEVKEY_RECORD) {
		int rc = llsec_update_devkey_record(dev, in_key);

		if (rc < 0)
			return rc;
	}

	spin_lock_bh(&dev->lock);

	if ((!devkey && frame_counter < dev->dev.frame_counter) ||
	    (devkey && frame_counter < devkey->devkey.frame_counter)) {
		spin_unlock_bh(&dev->lock);
		return -EINVAL;
	}

	if (devkey)
		devkey->devkey.frame_counter = frame_counter + 1;
	else
		dev->dev.frame_counter = frame_counter + 1;

	spin_unlock_bh(&dev->lock);

	return 0;
}

int mac802154_llsec_decrypt(struct mac802154_llsec *sec, struct sk_buff *skb)
{
	struct ieee802154_hdr hdr;
	struct mac802154_llsec_key *key;
	struct ieee802154_llsec_key_id key_id;
	struct mac802154_llsec_device *dev;
	struct ieee802154_llsec_seclevel seclevel;
	int err;
	__le64 dev_addr;
	u32 frame_ctr;

	if (ieee802154_hdr_peek(skb, &hdr) < 0)
		return -EINVAL;
	if (!hdr.fc.security_enabled)
		return 0;
	if (hdr.fc.version == 0)
		return -EINVAL;

	read_lock_bh(&sec->lock);
	if (!sec->params.enabled) {
		read_unlock_bh(&sec->lock);
		return -EINVAL;
	}
	read_unlock_bh(&sec->lock);

	rcu_read_lock();

	key = llsec_lookup_key(sec, &hdr, &hdr.source, &key_id);
	if (!key) {
		err = -ENOKEY;
		goto fail;
	}

	dev = llsec_lookup_dev(sec, &hdr.source);
	if (!dev) {
		err = -EINVAL;
		goto fail_dev;
	}

	if (llsec_lookup_seclevel(sec, hdr.fc.type, 0, &seclevel) < 0) {
		err = -EINVAL;
		goto fail_dev;
	}

	if (!(seclevel.sec_levels & BIT(hdr.sec.level)) &&
	    (hdr.sec.level == 0 && seclevel.device_override &&
	     !dev->dev.seclevel_exempt)) {
		err = -EINVAL;
		goto fail_dev;
	}

	frame_ctr = le32_to_cpu(hdr.sec.frame_counter);

	if (frame_ctr == 0xffffffff) {
		err = -EOVERFLOW;
		goto fail_dev;
	}

	err = llsec_update_devkey_info(dev, &key_id, frame_ctr);
	if (err)
		goto fail_dev;

	dev_addr = dev->dev.hwaddr;

	rcu_read_unlock();

	err = llsec_do_decrypt(skb, sec, &hdr, key, dev_addr);
	llsec_key_put(key);
	return err;

fail_dev:
	llsec_key_put(key);
fail:
	rcu_read_unlock();
	return err;
}
