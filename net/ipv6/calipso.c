/*
 * CALIPSO - Common Architecture Label IPv6 Security Option
 *
 * This is an implementation of the CALIPSO protocol as specified in
 * RFC 5570.
 *
 * Authors: Paul Moore <paul.moore@hp.com>
 *          Huw Davies <huw@codeweavers.com>
 *
 */

/* (c) Copyright Hewlett-Packard Development Company, L.P., 2006, 2008
 * (c) Copyright Huw Davies <huw@codeweavers.com>, 2015
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY;  without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;  if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/jhash.h>
#include <linux/audit.h>
#include <linux/slab.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/tcp.h>
#include <net/netlabel.h>
#include <net/calipso.h>
#include <linux/atomic.h>
#include <linux/bug.h>
#include <asm/unaligned.h>
#include <linux/crc-ccitt.h>

/* Maximium size of the calipso option including
 * the two-byte TLV header.
 */
#define CALIPSO_OPT_LEN_MAX (2 + 252)

/* Size of the minimum calipso option including
 * the two-byte TLV header.
 */
#define CALIPSO_HDR_LEN (2 + 8)

/* Maximium size of the calipso option including
 * the two-byte TLV header and upto 3 bytes of
 * leading pad and 7 bytes of trailing pad.
 */
#define CALIPSO_OPT_LEN_MAX_WITH_PAD (3 + CALIPSO_OPT_LEN_MAX + 7)

 /* Maximium size of u32 aligned buffer required to hold calipso
  * option.  Max of 3 initial pad bytes starting from buffer + 3.
  * i.e. the worst case is when the previous tlv finishes on 4n + 3.
  */
#define CALIPSO_MAX_BUFFER (6 + CALIPSO_OPT_LEN_MAX)

/* List of available DOI definitions */
static DEFINE_SPINLOCK(calipso_doi_list_lock);
static LIST_HEAD(calipso_doi_list);

/* Label mapping cache */
int calipso_cache_enabled = 1;
int calipso_cache_bucketsize = 10;
#define CALIPSO_CACHE_BUCKETBITS     7
#define CALIPSO_CACHE_BUCKETS        BIT(CALIPSO_CACHE_BUCKETBITS)
#define CALIPSO_CACHE_REORDERLIMIT   10
struct calipso_map_cache_bkt {
	spinlock_t lock;
	u32 size;
	struct list_head list;
};

struct calipso_map_cache_entry {
	u32 hash;
	unsigned char *key;
	size_t key_len;

	struct netlbl_lsm_cache *lsm_data;

	u32 activity;
	struct list_head list;
};

static struct calipso_map_cache_bkt *calipso_cache;

/* Label Mapping Cache Functions
 */

/**
 * calipso_cache_entry_free - Frees a cache entry
 * @entry: the entry to free
 *
 * Description:
 * This function frees the memory associated with a cache entry including the
 * LSM cache data if there are no longer any users, i.e. reference count == 0.
 *
 */
static void calipso_cache_entry_free(struct calipso_map_cache_entry *entry)
{
	if (entry->lsm_data)
		netlbl_secattr_cache_free(entry->lsm_data);
	kfree(entry->key);
	kfree(entry);
}

/**
 * calipso_map_cache_hash - Hashing function for the CALIPSO cache
 * @key: the hash key
 * @key_len: the length of the key in bytes
 *
 * Description:
 * The CALIPSO tag hashing function.  Returns a 32-bit hash value.
 *
 */
static u32 calipso_map_cache_hash(const unsigned char *key, u32 key_len)
{
	return jhash(key, key_len, 0);
}

/**
 * calipso_cache_init - Initialize the CALIPSO cache
 *
 * Description:
 * Initializes the CALIPSO label mapping cache, this function should be called
 * before any of the other functions defined in this file.  Returns zero on
 * success, negative values on error.
 *
 */
static int __init calipso_cache_init(void)
{
	u32 iter;

	calipso_cache = kcalloc(CALIPSO_CACHE_BUCKETS,
				sizeof(struct calipso_map_cache_bkt),
				GFP_KERNEL);
	if (!calipso_cache)
		return -ENOMEM;

	for (iter = 0; iter < CALIPSO_CACHE_BUCKETS; iter++) {
		spin_lock_init(&calipso_cache[iter].lock);
		calipso_cache[iter].size = 0;
		INIT_LIST_HEAD(&calipso_cache[iter].list);
	}

	return 0;
}

/**
 * calipso_cache_invalidate - Invalidates the current CALIPSO cache
 *
 * Description:
 * Invalidates and frees any entries in the CALIPSO cache.  Returns zero on
 * success and negative values on failure.
 *
 */
static void calipso_cache_invalidate(void)
{
	struct calipso_map_cache_entry *entry, *tmp_entry;
	u32 iter;

	for (iter = 0; iter < CALIPSO_CACHE_BUCKETS; iter++) {
		spin_lock_bh(&calipso_cache[iter].lock);
		list_for_each_entry_safe(entry,
					 tmp_entry,
					 &calipso_cache[iter].list, list) {
			list_del(&entry->list);
			calipso_cache_entry_free(entry);
		}
		calipso_cache[iter].size = 0;
		spin_unlock_bh(&calipso_cache[iter].lock);
	}
}

/**
 * calipso_cache_check - Check the CALIPSO cache for a label mapping
 * @key: the buffer to check
 * @key_len: buffer length in bytes
 * @secattr: the security attribute struct to use
 *
 * Description:
 * This function checks the cache to see if a label mapping already exists for
 * the given key.  If there is a match then the cache is adjusted and the
 * @secattr struct is populated with the correct LSM security attributes.  The
 * cache is adjusted in the following manner if the entry is not already the
 * first in the cache bucket:
 *
 *  1. The cache entry's activity counter is incremented
 *  2. The previous (higher ranking) entry's activity counter is decremented
 *  3. If the difference between the two activity counters is geater than
 *     CALIPSO_CACHE_REORDERLIMIT the two entries are swapped
 *
 * Returns zero on success, -ENOENT for a cache miss, and other negative values
 * on error.
 *
 */
static int calipso_cache_check(const unsigned char *key,
			       u32 key_len,
			       struct netlbl_lsm_secattr *secattr)
{
	u32 bkt;
	struct calipso_map_cache_entry *entry;
	struct calipso_map_cache_entry *prev_entry = NULL;
	u32 hash;

	if (!calipso_cache_enabled)
		return -ENOENT;

	hash = calipso_map_cache_hash(key, key_len);
	bkt = hash & (CALIPSO_CACHE_BUCKETS - 1);
	spin_lock_bh(&calipso_cache[bkt].lock);
	list_for_each_entry(entry, &calipso_cache[bkt].list, list) {
		if (entry->hash == hash &&
		    entry->key_len == key_len &&
		    memcmp(entry->key, key, key_len) == 0) {
			entry->activity += 1;
			atomic_inc(&entry->lsm_data->refcount);
			secattr->cache = entry->lsm_data;
			secattr->flags |= NETLBL_SECATTR_CACHE;
			secattr->type = NETLBL_NLTYPE_CALIPSO;
			if (!prev_entry) {
				spin_unlock_bh(&calipso_cache[bkt].lock);
				return 0;
			}

			if (prev_entry->activity > 0)
				prev_entry->activity -= 1;
			if (entry->activity > prev_entry->activity &&
			    entry->activity - prev_entry->activity >
			    CALIPSO_CACHE_REORDERLIMIT) {
				__list_del(entry->list.prev, entry->list.next);
				__list_add(&entry->list,
					   prev_entry->list.prev,
					   &prev_entry->list);
			}

			spin_unlock_bh(&calipso_cache[bkt].lock);
			return 0;
		}
		prev_entry = entry;
	}
	spin_unlock_bh(&calipso_cache[bkt].lock);

	return -ENOENT;
}

/**
 * calipso_cache_add - Add an entry to the CALIPSO cache
 * @calipso_ptr: the CALIPSO option
 * @secattr: the packet's security attributes
 *
 * Description:
 * Add a new entry into the CALIPSO label mapping cache.  Add the new entry to
 * head of the cache bucket's list, if the cache bucket is out of room remove
 * the last entry in the list first.  It is important to note that there is
 * currently no checking for duplicate keys.  Returns zero on success,
 * negative values on failure.  The key stored starts at calipso_ptr + 2,
 * i.e. the type and length bytes are not stored, this corresponds to
 * calipso_ptr[1] bytes of data.
 *
 */
static int calipso_cache_add(const unsigned char *calipso_ptr,
			     const struct netlbl_lsm_secattr *secattr)
{
	int ret_val = -EPERM;
	u32 bkt;
	struct calipso_map_cache_entry *entry = NULL;
	struct calipso_map_cache_entry *old_entry = NULL;
	u32 calipso_ptr_len;

	if (!calipso_cache_enabled || calipso_cache_bucketsize <= 0)
		return 0;

	calipso_ptr_len = calipso_ptr[1];

	entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return -ENOMEM;
	entry->key = kmemdup(calipso_ptr + 2, calipso_ptr_len, GFP_ATOMIC);
	if (!entry->key) {
		ret_val = -ENOMEM;
		goto cache_add_failure;
	}
	entry->key_len = calipso_ptr_len;
	entry->hash = calipso_map_cache_hash(calipso_ptr, calipso_ptr_len);
	atomic_inc(&secattr->cache->refcount);
	entry->lsm_data = secattr->cache;

	bkt = entry->hash & (CALIPSO_CACHE_BUCKETS - 1);
	spin_lock_bh(&calipso_cache[bkt].lock);
	if (calipso_cache[bkt].size < calipso_cache_bucketsize) {
		list_add(&entry->list, &calipso_cache[bkt].list);
		calipso_cache[bkt].size += 1;
	} else {
		old_entry = list_entry(calipso_cache[bkt].list.prev,
				       struct calipso_map_cache_entry, list);
		list_del(&old_entry->list);
		list_add(&entry->list, &calipso_cache[bkt].list);
		calipso_cache_entry_free(old_entry);
	}
	spin_unlock_bh(&calipso_cache[bkt].lock);

	return 0;

cache_add_failure:
	if (entry)
		calipso_cache_entry_free(entry);
	return ret_val;
}

/* DOI List Functions
 */

/**
 * calipso_doi_search - Searches for a DOI definition
 * @doi: the DOI to search for
 *
 * Description:
 * Search the DOI definition list for a DOI definition with a DOI value that
 * matches @doi.  The caller is responsible for calling rcu_read_[un]lock().
 * Returns a pointer to the DOI definition on success and NULL on failure.
 */
static struct calipso_doi *calipso_doi_search(u32 doi)
{
	struct calipso_doi *iter;

	list_for_each_entry_rcu(iter, &calipso_doi_list, list)
		if (iter->doi == doi && atomic_read(&iter->refcount))
			return iter;
	return NULL;
}

/**
 * calipso_doi_add - Add a new DOI to the CALIPSO protocol engine
 * @doi_def: the DOI structure
 * @audit_info: NetLabel audit information
 *
 * Description:
 * The caller defines a new DOI for use by the CALIPSO engine and calls this
 * function to add it to the list of acceptable domains.  The caller must
 * ensure that the mapping table specified in @doi_def->map meets all of the
 * requirements of the mapping type (see calipso.h for details).  Returns
 * zero on success and non-zero on failure.
 *
 */
static int calipso_doi_add(struct calipso_doi *doi_def,
			   struct netlbl_audit *audit_info)
{
	int ret_val = -EINVAL;
	u32 doi;
	u32 doi_type;
	struct audit_buffer *audit_buf;

	doi = doi_def->doi;
	doi_type = doi_def->type;

	if (doi_def->doi == CALIPSO_DOI_UNKNOWN)
		goto doi_add_return;

	atomic_set(&doi_def->refcount, 1);

	spin_lock(&calipso_doi_list_lock);
	if (calipso_doi_search(doi_def->doi)) {
		spin_unlock(&calipso_doi_list_lock);
		ret_val = -EEXIST;
		goto doi_add_return;
	}
	list_add_tail_rcu(&doi_def->list, &calipso_doi_list);
	spin_unlock(&calipso_doi_list_lock);
	ret_val = 0;

doi_add_return:
	audit_buf = netlbl_audit_start(AUDIT_MAC_CALIPSO_ADD, audit_info);
	if (audit_buf) {
		const char *type_str;

		switch (doi_type) {
		case CALIPSO_MAP_PASS:
			type_str = "pass";
			break;
		default:
			type_str = "(unknown)";
		}
		audit_log_format(audit_buf,
				 " calipso_doi=%u calipso_type=%s res=%u",
				 doi, type_str, ret_val == 0 ? 1 : 0);
		audit_log_end(audit_buf);
	}

	return ret_val;
}

/**
 * calipso_doi_free - Frees a DOI definition
 * @doi_def: the DOI definition
 *
 * Description:
 * This function frees all of the memory associated with a DOI definition.
 *
 */
static void calipso_doi_free(struct calipso_doi *doi_def)
{
	kfree(doi_def);
}

/**
 * calipso_doi_free_rcu - Frees a DOI definition via the RCU pointer
 * @entry: the entry's RCU field
 *
 * Description:
 * This function is designed to be used as a callback to the call_rcu()
 * function so that the memory allocated to the DOI definition can be released
 * safely.
 *
 */
static void calipso_doi_free_rcu(struct rcu_head *entry)
{
	struct calipso_doi *doi_def;

	doi_def = container_of(entry, struct calipso_doi, rcu);
	calipso_doi_free(doi_def);
}

/**
 * calipso_doi_remove - Remove an existing DOI from the CALIPSO protocol engine
 * @doi: the DOI value
 * @audit_secid: the LSM secid to use in the audit message
 *
 * Description:
 * Removes a DOI definition from the CALIPSO engine.  The NetLabel routines will
 * be called to release their own LSM domain mappings as well as our own
 * domain list.  Returns zero on success and negative values on failure.
 *
 */
static int calipso_doi_remove(u32 doi, struct netlbl_audit *audit_info)
{
	int ret_val;
	struct calipso_doi *doi_def;
	struct audit_buffer *audit_buf;

	spin_lock(&calipso_doi_list_lock);
	doi_def = calipso_doi_search(doi);
	if (!doi_def) {
		spin_unlock(&calipso_doi_list_lock);
		ret_val = -ENOENT;
		goto doi_remove_return;
	}
	if (!atomic_dec_and_test(&doi_def->refcount)) {
		spin_unlock(&calipso_doi_list_lock);
		ret_val = -EBUSY;
		goto doi_remove_return;
	}
	list_del_rcu(&doi_def->list);
	spin_unlock(&calipso_doi_list_lock);

	call_rcu(&doi_def->rcu, calipso_doi_free_rcu);
	ret_val = 0;

doi_remove_return:
	audit_buf = netlbl_audit_start(AUDIT_MAC_CALIPSO_DEL, audit_info);
	if (audit_buf) {
		audit_log_format(audit_buf,
				 " calipso_doi=%u res=%u",
				 doi, ret_val == 0 ? 1 : 0);
		audit_log_end(audit_buf);
	}

	return ret_val;
}

/**
 * calipso_doi_getdef - Returns a reference to a valid DOI definition
 * @doi: the DOI value
 *
 * Description:
 * Searches for a valid DOI definition and if one is found it is returned to
 * the caller.  Otherwise NULL is returned.  The caller must ensure that
 * calipso_doi_putdef() is called when the caller is done.
 *
 */
static struct calipso_doi *calipso_doi_getdef(u32 doi)
{
	struct calipso_doi *doi_def;

	rcu_read_lock();
	doi_def = calipso_doi_search(doi);
	if (!doi_def)
		goto doi_getdef_return;
	if (!atomic_inc_not_zero(&doi_def->refcount))
		doi_def = NULL;

doi_getdef_return:
	rcu_read_unlock();
	return doi_def;
}

/**
 * calipso_doi_putdef - Releases a reference for the given DOI definition
 * @doi_def: the DOI definition
 *
 * Description:
 * Releases a DOI definition reference obtained from calipso_doi_getdef().
 *
 */
static void calipso_doi_putdef(struct calipso_doi *doi_def)
{
	if (!doi_def)
		return;

	if (!atomic_dec_and_test(&doi_def->refcount))
		return;
	spin_lock(&calipso_doi_list_lock);
	list_del_rcu(&doi_def->list);
	spin_unlock(&calipso_doi_list_lock);

	call_rcu(&doi_def->rcu, calipso_doi_free_rcu);
}

/**
 * calipso_doi_walk - Iterate through the DOI definitions
 * @skip_cnt: skip past this number of DOI definitions, updated
 * @callback: callback for each DOI definition
 * @cb_arg: argument for the callback function
 *
 * Description:
 * Iterate over the DOI definition list, skipping the first @skip_cnt entries.
 * For each entry call @callback, if @callback returns a negative value stop
 * 'walking' through the list and return.  Updates the value in @skip_cnt upon
 * return.  Returns zero on success, negative values on failure.
 *
 */
static int calipso_doi_walk(u32 *skip_cnt,
			    int (*callback)(struct calipso_doi *doi_def,
					    void *arg),
			    void *cb_arg)
{
	int ret_val = -ENOENT;
	u32 doi_cnt = 0;
	struct calipso_doi *iter_doi;

	rcu_read_lock();
	list_for_each_entry_rcu(iter_doi, &calipso_doi_list, list)
		if (atomic_read(&iter_doi->refcount) > 0) {
			if (doi_cnt++ < *skip_cnt)
				continue;
			ret_val = callback(iter_doi, cb_arg);
			if (ret_val < 0) {
				doi_cnt--;
				goto doi_walk_return;
			}
		}

doi_walk_return:
	rcu_read_unlock();
	*skip_cnt = doi_cnt;
	return ret_val;
}

/**
 * calipso_validate - Validate a CALIPSO option
 * @skb: the packet
 * @option: the start of the option
 *
 * Description:
 * This routine is called to validate a CALIPSO option.
 * If the option is valid then %true is returned, otherwise
 * %false is returned.
 *
 * The caller should have already checked that the length of the
 * option (including the TLV header) is >= 10 and that the catmap
 * length is consistent with the option length.
 *
 * We leave checks on the level and categories to the socket layer.
 */
bool calipso_validate(const struct sk_buff *skb, const unsigned char *option)
{
	struct calipso_doi *doi_def;
	bool ret_val;
	u16 crc, len = option[1] + 2;
	static const u8 zero[2];

	/* The original CRC runs over the option including the TLV header
	 * with the CRC-16 field (at offset 8) zeroed out. */
	crc = crc_ccitt(0xffff, option, 8);
	crc = crc_ccitt(crc, zero, sizeof(zero));
	if (len > 10)
		crc = crc_ccitt(crc, option + 10, len - 10);
	crc = ~crc;
	if (option[8] != (crc & 0xff) || option[9] != ((crc >> 8) & 0xff))
		return false;

	rcu_read_lock();
	doi_def = calipso_doi_search(get_unaligned_be32(option + 2));
	ret_val = !!doi_def;
	rcu_read_unlock();

	return ret_val;
}

/**
 * calipso_map_cat_hton - Perform a category mapping from host to network
 * @doi_def: the DOI definition
 * @secattr: the security attributes
 * @net_cat: the zero'd out category bitmap in network/CALIPSO format
 * @net_cat_len: the length of the CALIPSO bitmap in bytes
 *
 * Description:
 * Perform a label mapping to translate a local MLS category bitmap to the
 * correct CALIPSO bitmap using the given DOI definition.  Returns the minimum
 * size in bytes of the network bitmap on success, negative values otherwise.
 *
 */
static int calipso_map_cat_hton(const struct calipso_doi *doi_def,
				const struct netlbl_lsm_secattr *secattr,
				unsigned char *net_cat,
				u32 net_cat_len)
{
	int spot = -1;
	u32 net_spot_max = 0;
	u32 net_clen_bits = net_cat_len * 8;

	for (;;) {
		spot = netlbl_catmap_walk(secattr->attr.mls.cat,
					  spot + 1);
		if (spot < 0)
			break;
		if (spot >= net_clen_bits)
			return -ENOSPC;
		netlbl_bitmap_setbit(net_cat, spot, 1);

		if (spot > net_spot_max)
			net_spot_max = spot;
	}

	return (net_spot_max / 32 + 1) * 4;
}

/**
 * calipso_map_cat_ntoh - Perform a category mapping from network to host
 * @doi_def: the DOI definition
 * @net_cat: the category bitmap in network/CALIPSO format
 * @net_cat_len: the length of the CALIPSO bitmap in bytes
 * @secattr: the security attributes
 *
 * Description:
 * Perform a label mapping to translate a CALIPSO bitmap to the correct local
 * MLS category bitmap using the given DOI definition.  Returns zero on
 * success, negative values on failure.
 *
 */
static int calipso_map_cat_ntoh(const struct calipso_doi *doi_def,
				const unsigned char *net_cat,
				u32 net_cat_len,
				struct netlbl_lsm_secattr *secattr)
{
	int ret_val;
	int spot = -1;
	u32 net_clen_bits = net_cat_len * 8;

	for (;;) {
		spot = netlbl_bitmap_walk(net_cat,
					  net_clen_bits,
					  spot + 1,
					  1);
		if (spot < 0) {
			if (spot == -2)
				return -EFAULT;
			return 0;
		}

		ret_val = netlbl_catmap_setbit(&secattr->attr.mls.cat,
					       spot,
					       GFP_ATOMIC);
		if (ret_val != 0)
			return ret_val;
	}

	return -EINVAL;
}

/**
 * calipso_pad_write - Writes pad bytes in TLV format
 * @buf: the buffer
 * @offset: offset from start of buffer to write padding
 * @count: number of pad bytes to write
 *
 * Description:
 * Write @count bytes of TLV padding into @buffer starting at offset @offset.
 * @count should be less than 8 - see RFC 4942.
 *
 */
static int calipso_pad_write(unsigned char *buf, unsigned int offset,
			     unsigned int count)
{
	if (WARN_ON_ONCE(count >= 8))
		return -EINVAL;

	switch (count) {
	case 0:
		break;
	case 1:
		buf[offset] = IPV6_TLV_PAD1;
		break;
	default:
		buf[offset] = IPV6_TLV_PADN;
		buf[offset + 1] = count - 2;
		if (count > 2)
			memset(buf + offset + 2, 0, count - 2);
		break;
	}
	return 0;
}

/**
 * calipso_genopt - Generate a CALIPSO option
 * @buf: the option buffer
 * @start: offset from which to write
 * @buf_len: the size of opt_buf
 * @doi_def: the CALIPSO DOI to use
 * @secattr: the security attributes
 *
 * Description:
 * Generate a CALIPSO option using the DOI definition and security attributes
 * passed to the function. This also generates upto three bytes of leading
 * padding that ensures that the option is 4n + 2 aligned.  It returns the
 * number of bytes written (including any initial padding).
 */
static int calipso_genopt(unsigned char *buf, u32 start, u32 buf_len,
			  const struct calipso_doi *doi_def,
			  const struct netlbl_lsm_secattr *secattr)
{
	int ret_val;
	u32 len, pad;
	u16 crc;
	static const unsigned char padding[4] = {2, 1, 0, 3};
	unsigned char *calipso;

	/* CALIPSO has 4n + 2 alignment */
	pad = padding[start & 3];
	if (buf_len <= start + pad + CALIPSO_HDR_LEN)
		return -ENOSPC;

	if ((secattr->flags & NETLBL_SECATTR_MLS_LVL) == 0)
		return -EPERM;

	len = CALIPSO_HDR_LEN;

	if (secattr->flags & NETLBL_SECATTR_MLS_CAT) {
		ret_val = calipso_map_cat_hton(doi_def,
					       secattr,
					       buf + start + pad + len,
					       buf_len - start - pad - len);
		if (ret_val < 0)
			return ret_val;
		len += ret_val;
	}

	calipso_pad_write(buf, start, pad);
	calipso = buf + start + pad;

	calipso[0] = IPV6_TLV_CALIPSO;
	calipso[1] = len - 2;
	*(__be32 *)(calipso + 2) = htonl(doi_def->doi);
	calipso[6] = (len - CALIPSO_HDR_LEN) / 4;
	calipso[7] = secattr->attr.mls.lvl,
	crc = ~crc_ccitt(0xffff, calipso, len);
	calipso[8] = crc & 0xff;
	calipso[9] = (crc >> 8) & 0xff;
	return pad + len;
}

/* Hop-by-hop hdr helper functions
 */

/**
 * calipso_opt_update - Replaces socket's hop options with a new set
 * @sk: the socket
 * @hop: new hop options
 *
 * Description:
 * Replaces @sk's hop options with @hop.  @hop may be NULL to leave
 * the socket with no hop options.
 *
 */
static int calipso_opt_update(struct sock *sk, struct ipv6_opt_hdr *hop)
{
	struct ipv6_txoptions *old = txopt_get(inet6_sk(sk)), *txopts;

	txopts = ipv6_renew_options_kern(sk, old, IPV6_HOPOPTS,
					 hop, hop ? ipv6_optlen(hop) : 0);
	txopt_put(old);
	if (IS_ERR(txopts))
		return PTR_ERR(txopts);

	txopts = ipv6_update_options(sk, txopts);
	if (txopts) {
		atomic_sub(txopts->tot_len, &sk->sk_omem_alloc);
		txopt_put(txopts);
	}

	return 0;
}

/**
 * calipso_tlv_len - Returns the length of the TLV
 * @opt: the option header
 * @offset: offset of the TLV within the header
 *
 * Description:
 * Returns the length of the TLV option at offset @offset within
 * the option header @opt.  Checks that the entire TLV fits inside
 * the option header, returns a negative value if this is not the case.
 */
static int calipso_tlv_len(struct ipv6_opt_hdr *opt, unsigned int offset)
{
	unsigned char *tlv = (unsigned char *)opt;
	unsigned int opt_len = ipv6_optlen(opt), tlv_len;

	if (offset < sizeof(*opt) || offset >= opt_len)
		return -EINVAL;
	if (tlv[offset] == IPV6_TLV_PAD1)
		return 1;
	if (offset + 1 >= opt_len)
		return -EINVAL;
	tlv_len = tlv[offset + 1] + 2;
	if (offset + tlv_len > opt_len)
		return -EINVAL;
	return tlv_len;
}

/**
 * calipso_opt_find - Finds the CALIPSO option in an IPv6 hop options header
 * @hop: the hop options header
 * @start: on return holds the offset of any leading padding
 * @end: on return holds the offset of the first non-pad TLV after CALIPSO
 *
 * Description:
 * Finds the space occupied by a CALIPSO option (including any leading and
 * trailing padding).
 *
 * If a CALIPSO option exists set @start and @end to the
 * offsets within @hop of the start of padding before the first
 * CALIPSO option and the end of padding after the first CALIPSO
 * option.  In this case the function returns 0.
 *
 * In the absence of a CALIPSO option, @start and @end will be
 * set to the start and end of any trailing padding in the header.
 * This is useful when appending a new option, as the caller may want
 * to overwrite some of this padding.  In this case the function will
 * return -ENOENT.
 */
static int calipso_opt_find(struct ipv6_opt_hdr *hop, unsigned int *start,
			    unsigned int *end)
{
	int ret_val = -ENOENT, tlv_len;
	unsigned int opt_len, offset, offset_s = 0, offset_e = 0;
	unsigned char *opt = (unsigned char *)hop;

	opt_len = ipv6_optlen(hop);
	offset = sizeof(*hop);

	while (offset < opt_len) {
		tlv_len = calipso_tlv_len(hop, offset);
		if (tlv_len < 0)
			return tlv_len;

		switch (opt[offset]) {
		case IPV6_TLV_PAD1:
		case IPV6_TLV_PADN:
			if (offset_e)
				offset_e = offset;
			break;
		case IPV6_TLV_CALIPSO:
			ret_val = 0;
			offset_e = offset;
			break;
		default:
			if (offset_e == 0)
				offset_s = offset;
			else
				goto out;
		}
		offset += tlv_len;
	}

out:
	if (offset_s)
		*start = offset_s + calipso_tlv_len(hop, offset_s);
	else
		*start = sizeof(*hop);
	if (offset_e)
		*end = offset_e + calipso_tlv_len(hop, offset_e);
	else
		*end = opt_len;

	return ret_val;
}

/**
 * calipso_opt_insert - Inserts a CALIPSO option into an IPv6 hop opt hdr
 * @hop: the original hop options header
 * @doi_def: the CALIPSO DOI to use
 * @secattr: the specific security attributes of the socket
 *
 * Description:
 * Creates a new hop options header based on @hop with a
 * CALIPSO option added to it.  If @hop already contains a CALIPSO
 * option this is overwritten, otherwise the new option is appended
 * after any existing options.  If @hop is NULL then the new header
 * will contain just the CALIPSO option and any needed padding.
 *
 */
static struct ipv6_opt_hdr *
calipso_opt_insert(struct ipv6_opt_hdr *hop,
		   const struct calipso_doi *doi_def,
		   const struct netlbl_lsm_secattr *secattr)
{
	unsigned int start, end, buf_len, pad, hop_len;
	struct ipv6_opt_hdr *new;
	int ret_val;

	if (hop) {
		hop_len = ipv6_optlen(hop);
		ret_val = calipso_opt_find(hop, &start, &end);
		if (ret_val && ret_val != -ENOENT)
			return ERR_PTR(ret_val);
	} else {
		hop_len = 0;
		start = sizeof(*hop);
		end = 0;
	}

	buf_len = hop_len + start - end + CALIPSO_OPT_LEN_MAX_WITH_PAD;
	new = kzalloc(buf_len, GFP_ATOMIC);
	if (!new)
		return ERR_PTR(-ENOMEM);

	if (start > sizeof(*hop))
		memcpy(new, hop, start);
	ret_val = calipso_genopt((unsigned char *)new, start, buf_len, doi_def,
				 secattr);
	if (ret_val < 0) {
		kfree(new);
		return ERR_PTR(ret_val);
	}

	buf_len = start + ret_val;
	/* At this point buf_len aligns to 4n, so (buf_len & 4) pads to 8n */
	pad = ((buf_len & 4) + (end & 7)) & 7;
	calipso_pad_write((unsigned char *)new, buf_len, pad);
	buf_len += pad;

	if (end != hop_len) {
		memcpy((char *)new + buf_len, (char *)hop + end, hop_len - end);
		buf_len += hop_len - end;
	}
	new->nexthdr = 0;
	new->hdrlen = buf_len / 8 - 1;

	return new;
}

/**
 * calipso_opt_del - Removes the CALIPSO option from an option header
 * @hop: the original header
 * @new: the new header
 *
 * Description:
 * Creates a new header based on @hop without any CALIPSO option.  If @hop
 * doesn't contain a CALIPSO option it returns -ENOENT.  If @hop contains
 * no other non-padding options, it returns zero with @new set to NULL.
 * Otherwise it returns zero, creates a new header without the CALIPSO
 * option (and removing as much padding as possible) and returns with
 * @new set to that header.
 *
 */
static int calipso_opt_del(struct ipv6_opt_hdr *hop,
			   struct ipv6_opt_hdr **new)
{
	int ret_val;
	unsigned int start, end, delta, pad, hop_len;

	ret_val = calipso_opt_find(hop, &start, &end);
	if (ret_val)
		return ret_val;

	hop_len = ipv6_optlen(hop);
	if (start == sizeof(*hop) && end == hop_len) {
		/* There's no other option in the header so return NULL */
		*new = NULL;
		return 0;
	}

	delta = (end - start) & ~7;
	*new = kzalloc(hop_len - delta, GFP_ATOMIC);
	if (!*new)
		return -ENOMEM;

	memcpy(*new, hop, start);
	(*new)->hdrlen -= delta / 8;
	pad = (end - start) & 7;
	calipso_pad_write((unsigned char *)*new, start, pad);
	if (end != hop_len)
		memcpy((char *)*new + start + pad, (char *)hop + end,
		       hop_len - end);

	return 0;
}

/**
 * calipso_opt_getattr - Get the security attributes from a memory block
 * @calipso: the CALIPSO option
 * @secattr: the security attributes
 *
 * Description:
 * Inspect @calipso and return the security attributes in @secattr.
 * Returns zero on success and negative values on failure.
 *
 */
static int calipso_opt_getattr(const unsigned char *calipso,
			       struct netlbl_lsm_secattr *secattr)
{
	int ret_val = -ENOMSG;
	u32 doi, len = calipso[1], cat_len = calipso[6] * 4;
	struct calipso_doi *doi_def;

	if (cat_len + 8 > len)
		return -EINVAL;

	if (calipso_cache_check(calipso + 2, calipso[1], secattr) == 0)
		return 0;

	doi = get_unaligned_be32(calipso + 2);
	rcu_read_lock();
	doi_def = calipso_doi_search(doi);
	if (!doi_def)
		goto getattr_return;

	secattr->attr.mls.lvl = calipso[7];
	secattr->flags |= NETLBL_SECATTR_MLS_LVL;

	if (cat_len) {
		ret_val = calipso_map_cat_ntoh(doi_def,
					       calipso + 10,
					       cat_len,
					       secattr);
		if (ret_val != 0) {
			netlbl_catmap_free(secattr->attr.mls.cat);
			goto getattr_return;
		}

		secattr->flags |= NETLBL_SECATTR_MLS_CAT;
	}

	secattr->type = NETLBL_NLTYPE_CALIPSO;

getattr_return:
	rcu_read_unlock();
	return ret_val;
}

/* sock functions.
 */

/**
 * calipso_sock_getattr - Get the security attributes from a sock
 * @sk: the sock
 * @secattr: the security attributes
 *
 * Description:
 * Query @sk to see if there is a CALIPSO option attached to the sock and if
 * there is return the CALIPSO security attributes in @secattr.  This function
 * requires that @sk be locked, or privately held, but it does not do any
 * locking itself.  Returns zero on success and negative values on failure.
 *
 */
static int calipso_sock_getattr(struct sock *sk,
				struct netlbl_lsm_secattr *secattr)
{
	struct ipv6_opt_hdr *hop;
	int opt_len, len, ret_val = -ENOMSG, offset;
	unsigned char *opt;
	struct ipv6_txoptions *txopts = txopt_get(inet6_sk(sk));

	if (!txopts || !txopts->hopopt)
		goto done;

	hop = txopts->hopopt;
	opt = (unsigned char *)hop;
	opt_len = ipv6_optlen(hop);
	offset = sizeof(*hop);
	while (offset < opt_len) {
		len = calipso_tlv_len(hop, offset);
		if (len < 0) {
			ret_val = len;
			goto done;
		}
		switch (opt[offset]) {
		case IPV6_TLV_CALIPSO:
			if (len < CALIPSO_HDR_LEN)
				ret_val = -EINVAL;
			else
				ret_val = calipso_opt_getattr(&opt[offset],
							      secattr);
			goto done;
		default:
			offset += len;
			break;
		}
	}
done:
	txopt_put(txopts);
	return ret_val;
}

/**
 * calipso_sock_setattr - Add a CALIPSO option to a socket
 * @sk: the socket
 * @doi_def: the CALIPSO DOI to use
 * @secattr: the specific security attributes of the socket
 *
 * Description:
 * Set the CALIPSO option on the given socket using the DOI definition and
 * security attributes passed to the function.  This function requires
 * exclusive access to @sk, which means it either needs to be in the
 * process of being created or locked.  Returns zero on success and negative
 * values on failure.
 *
 */
static int calipso_sock_setattr(struct sock *sk,
				const struct calipso_doi *doi_def,
				const struct netlbl_lsm_secattr *secattr)
{
	int ret_val;
	struct ipv6_opt_hdr *old, *new;
	struct ipv6_txoptions *txopts = txopt_get(inet6_sk(sk));

	old = NULL;
	if (txopts)
		old = txopts->hopopt;

	new = calipso_opt_insert(old, doi_def, secattr);
	txopt_put(txopts);
	if (IS_ERR(new))
		return PTR_ERR(new);

	ret_val = calipso_opt_update(sk, new);

	kfree(new);
	return ret_val;
}

/**
 * calipso_sock_delattr - Delete the CALIPSO option from a socket
 * @sk: the socket
 *
 * Description:
 * Removes the CALIPSO option from a socket, if present.
 *
 */
static void calipso_sock_delattr(struct sock *sk)
{
	struct ipv6_opt_hdr *new_hop;
	struct ipv6_txoptions *txopts = txopt_get(inet6_sk(sk));

	if (!txopts || !txopts->hopopt)
		goto done;

	if (calipso_opt_del(txopts->hopopt, &new_hop))
		goto done;

	calipso_opt_update(sk, new_hop);
	kfree(new_hop);

done:
	txopt_put(txopts);
}

/* request sock functions.
 */

/**
 * calipso_req_setattr - Add a CALIPSO option to a connection request socket
 * @req: the connection request socket
 * @doi_def: the CALIPSO DOI to use
 * @secattr: the specific security attributes of the socket
 *
 * Description:
 * Set the CALIPSO option on the given socket using the DOI definition and
 * security attributes passed to the function.  Returns zero on success and
 * negative values on failure.
 *
 */
static int calipso_req_setattr(struct request_sock *req,
			       const struct calipso_doi *doi_def,
			       const struct netlbl_lsm_secattr *secattr)
{
	struct ipv6_txoptions *txopts;
	struct inet_request_sock *req_inet = inet_rsk(req);
	struct ipv6_opt_hdr *old, *new;
	struct sock *sk = sk_to_full_sk(req_to_sk(req));

	if (req_inet->ipv6_opt && req_inet->ipv6_opt->hopopt)
		old = req_inet->ipv6_opt->hopopt;
	else
		old = NULL;

	new = calipso_opt_insert(old, doi_def, secattr);
	if (IS_ERR(new))
		return PTR_ERR(new);

	txopts = ipv6_renew_options_kern(sk, req_inet->ipv6_opt, IPV6_HOPOPTS,
					 new, new ? ipv6_optlen(new) : 0);

	kfree(new);

	if (IS_ERR(txopts))
		return PTR_ERR(txopts);

	txopts = xchg(&req_inet->ipv6_opt, txopts);
	if (txopts) {
		atomic_sub(txopts->tot_len, &sk->sk_omem_alloc);
		txopt_put(txopts);
	}

	return 0;
}

/**
 * calipso_req_delattr - Delete the CALIPSO option from a request socket
 * @reg: the request socket
 *
 * Description:
 * Removes the CALIPSO option from a request socket, if present.
 *
 */
static void calipso_req_delattr(struct request_sock *req)
{
	struct inet_request_sock *req_inet = inet_rsk(req);
	struct ipv6_opt_hdr *new;
	struct ipv6_txoptions *txopts;
	struct sock *sk = sk_to_full_sk(req_to_sk(req));

	if (!req_inet->ipv6_opt || !req_inet->ipv6_opt->hopopt)
		return;

	if (calipso_opt_del(req_inet->ipv6_opt->hopopt, &new))
		return; /* Nothing to do */

	txopts = ipv6_renew_options_kern(sk, req_inet->ipv6_opt, IPV6_HOPOPTS,
					 new, new ? ipv6_optlen(new) : 0);

	if (!IS_ERR(txopts)) {
		txopts = xchg(&req_inet->ipv6_opt, txopts);
		if (txopts) {
			atomic_sub(txopts->tot_len, &sk->sk_omem_alloc);
			txopt_put(txopts);
		}
	}
	kfree(new);
}

/* skbuff functions.
 */

/**
 * calipso_skbuff_optptr - Find the CALIPSO option in the packet
 * @skb: the packet
 *
 * Description:
 * Parse the packet's IP header looking for a CALIPSO option.  Returns a pointer
 * to the start of the CALIPSO option on success, NULL if one if not found.
 *
 */
static unsigned char *calipso_skbuff_optptr(const struct sk_buff *skb)
{
	const struct ipv6hdr *ip6_hdr = ipv6_hdr(skb);
	int offset;

	if (ip6_hdr->nexthdr != NEXTHDR_HOP)
		return NULL;

	offset = ipv6_find_tlv(skb, sizeof(*ip6_hdr), IPV6_TLV_CALIPSO);
	if (offset >= 0)
		return (unsigned char *)ip6_hdr + offset;

	return NULL;
}

/**
 * calipso_skbuff_setattr - Set the CALIPSO option on a packet
 * @skb: the packet
 * @doi_def: the CALIPSO DOI to use
 * @secattr: the security attributes
 *
 * Description:
 * Set the CALIPSO option on the given packet based on the security attributes.
 * Returns a pointer to the IP header on success and NULL on failure.
 *
 */
static int calipso_skbuff_setattr(struct sk_buff *skb,
				  const struct calipso_doi *doi_def,
				  const struct netlbl_lsm_secattr *secattr)
{
	int ret_val;
	struct ipv6hdr *ip6_hdr;
	struct ipv6_opt_hdr *hop;
	unsigned char buf[CALIPSO_MAX_BUFFER];
	int len_delta, new_end, pad, payload;
	unsigned int start, end;

	ip6_hdr = ipv6_hdr(skb);
	if (ip6_hdr->nexthdr == NEXTHDR_HOP) {
		hop = (struct ipv6_opt_hdr *)(ip6_hdr + 1);
		ret_val = calipso_opt_find(hop, &start, &end);
		if (ret_val && ret_val != -ENOENT)
			return ret_val;
	} else {
		start = 0;
		end = 0;
	}

	memset(buf, 0, sizeof(buf));
	ret_val = calipso_genopt(buf, start & 3, sizeof(buf), doi_def, secattr);
	if (ret_val < 0)
		return ret_val;

	new_end = start + ret_val;
	/* At this point new_end aligns to 4n, so (new_end & 4) pads to 8n */
	pad = ((new_end & 4) + (end & 7)) & 7;
	len_delta = new_end - (int)end + pad;
	ret_val = skb_cow(skb, skb_headroom(skb) + len_delta);
	if (ret_val < 0)
		return ret_val;

	ip6_hdr = ipv6_hdr(skb); /* Reset as skb_cow() may have moved it */

	if (len_delta) {
		if (len_delta > 0)
			skb_push(skb, len_delta);
		else
			skb_pull(skb, -len_delta);
		memmove((char *)ip6_hdr - len_delta, ip6_hdr,
			sizeof(*ip6_hdr) + start);
		skb_reset_network_header(skb);
		ip6_hdr = ipv6_hdr(skb);
		payload = ntohs(ip6_hdr->payload_len);
		ip6_hdr->payload_len = htons(payload + len_delta);
	}

	hop = (struct ipv6_opt_hdr *)(ip6_hdr + 1);
	if (start == 0) {
		struct ipv6_opt_hdr *new_hop = (struct ipv6_opt_hdr *)buf;

		new_hop->nexthdr = ip6_hdr->nexthdr;
		new_hop->hdrlen = len_delta / 8 - 1;
		ip6_hdr->nexthdr = NEXTHDR_HOP;
	} else {
		hop->hdrlen += len_delta / 8;
	}
	memcpy((char *)hop + start, buf + (start & 3), new_end - start);
	calipso_pad_write((unsigned char *)hop, new_end, pad);

	return 0;
}

/**
 * calipso_skbuff_delattr - Delete any CALIPSO options from a packet
 * @skb: the packet
 *
 * Description:
 * Removes any and all CALIPSO options from the given packet.  Returns zero on
 * success, negative values on failure.
 *
 */
static int calipso_skbuff_delattr(struct sk_buff *skb)
{
	int ret_val;
	struct ipv6hdr *ip6_hdr;
	struct ipv6_opt_hdr *old_hop;
	u32 old_hop_len, start = 0, end = 0, delta, size, pad;

	if (!calipso_skbuff_optptr(skb))
		return 0;

	/* since we are changing the packet we should make a copy */
	ret_val = skb_cow(skb, skb_headroom(skb));
	if (ret_val < 0)
		return ret_val;

	ip6_hdr = ipv6_hdr(skb);
	old_hop = (struct ipv6_opt_hdr *)(ip6_hdr + 1);
	old_hop_len = ipv6_optlen(old_hop);

	ret_val = calipso_opt_find(old_hop, &start, &end);
	if (ret_val)
		return ret_val;

	if (start == sizeof(*old_hop) && end == old_hop_len) {
		/* There's no other option in the header so we delete
		 * the whole thing. */
		delta = old_hop_len;
		size = sizeof(*ip6_hdr);
		ip6_hdr->nexthdr = old_hop->nexthdr;
	} else {
		delta = (end - start) & ~7;
		if (delta)
			old_hop->hdrlen -= delta / 8;
		pad = (end - start) & 7;
		size = sizeof(*ip6_hdr) + start + pad;
		calipso_pad_write((unsigned char *)old_hop, start, pad);
	}

	if (delta) {
		skb_pull(skb, delta);
		memmove((char *)ip6_hdr + delta, ip6_hdr, size);
		skb_reset_network_header(skb);
	}

	return 0;
}

static const struct netlbl_calipso_ops ops = {
	.doi_add          = calipso_doi_add,
	.doi_free         = calipso_doi_free,
	.doi_remove       = calipso_doi_remove,
	.doi_getdef       = calipso_doi_getdef,
	.doi_putdef       = calipso_doi_putdef,
	.doi_walk         = calipso_doi_walk,
	.sock_getattr     = calipso_sock_getattr,
	.sock_setattr     = calipso_sock_setattr,
	.sock_delattr     = calipso_sock_delattr,
	.req_setattr      = calipso_req_setattr,
	.req_delattr      = calipso_req_delattr,
	.opt_getattr      = calipso_opt_getattr,
	.skbuff_optptr    = calipso_skbuff_optptr,
	.skbuff_setattr   = calipso_skbuff_setattr,
	.skbuff_delattr   = calipso_skbuff_delattr,
	.cache_invalidate = calipso_cache_invalidate,
	.cache_add        = calipso_cache_add
};

/**
 * calipso_init - Initialize the CALIPSO module
 *
 * Description:
 * Initialize the CALIPSO module and prepare it for use.  Returns zero on
 * success and negative values on failure.
 *
 */
int __init calipso_init(void)
{
	int ret_val;

	ret_val = calipso_cache_init();
	if (!ret_val)
		netlbl_calipso_ops_register(&ops);
	return ret_val;
}

void calipso_exit(void)
{
	netlbl_calipso_ops_register(NULL);
	calipso_cache_invalidate();
	kfree(calipso_cache);
}
