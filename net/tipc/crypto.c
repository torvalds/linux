// SPDX-License-Identifier: GPL-2.0
/**
 * net/tipc/crypto.c: TIPC crypto for key handling & packet en/decryption
 *
 * Copyright (c) 2019, Ericsson AB
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <crypto/aead.h>
#include <crypto/aes.h>
#include "crypto.h"

#define TIPC_TX_PROBE_LIM	msecs_to_jiffies(1000) /* > 1s */
#define TIPC_TX_LASTING_LIM	msecs_to_jiffies(120000) /* 2 mins */
#define TIPC_RX_ACTIVE_LIM	msecs_to_jiffies(3000) /* 3s */
#define TIPC_RX_PASSIVE_LIM	msecs_to_jiffies(180000) /* 3 mins */
#define TIPC_MAX_TFMS_DEF	10
#define TIPC_MAX_TFMS_LIM	1000

/**
 * TIPC Key ids
 */
enum {
	KEY_UNUSED = 0,
	KEY_MIN,
	KEY_1 = KEY_MIN,
	KEY_2,
	KEY_3,
	KEY_MAX = KEY_3,
};

/**
 * TIPC Crypto statistics
 */
enum {
	STAT_OK,
	STAT_NOK,
	STAT_ASYNC,
	STAT_ASYNC_OK,
	STAT_ASYNC_NOK,
	STAT_BADKEYS, /* tx only */
	STAT_BADMSGS = STAT_BADKEYS, /* rx only */
	STAT_NOKEYS,
	STAT_SWITCHES,

	MAX_STATS,
};

/* TIPC crypto statistics' header */
static const char *hstats[MAX_STATS] = {"ok", "nok", "async", "async_ok",
					"async_nok", "badmsgs", "nokeys",
					"switches"};

/* Max TFMs number per key */
int sysctl_tipc_max_tfms __read_mostly = TIPC_MAX_TFMS_DEF;

/**
 * struct tipc_key - TIPC keys' status indicator
 *
 *         7     6     5     4     3     2     1     0
 *      +-----+-----+-----+-----+-----+-----+-----+-----+
 * key: | (reserved)|passive idx| active idx|pending idx|
 *      +-----+-----+-----+-----+-----+-----+-----+-----+
 */
struct tipc_key {
#define KEY_BITS (2)
#define KEY_MASK ((1 << KEY_BITS) - 1)
	union {
		struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
			u8 pending:2,
			   active:2,
			   passive:2, /* rx only */
			   reserved:2;
#elif defined(__BIG_ENDIAN_BITFIELD)
			u8 reserved:2,
			   passive:2, /* rx only */
			   active:2,
			   pending:2;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
		} __packed;
		u8 keys;
	};
};

/**
 * struct tipc_tfm - TIPC TFM structure to form a list of TFMs
 */
struct tipc_tfm {
	struct crypto_aead *tfm;
	struct list_head list;
};

/**
 * struct tipc_aead - TIPC AEAD key structure
 * @tfm_entry: per-cpu pointer to one entry in TFM list
 * @crypto: TIPC crypto owns this key
 * @cloned: reference to the source key in case cloning
 * @users: the number of the key users (TX/RX)
 * @salt: the key's SALT value
 * @authsize: authentication tag size (max = 16)
 * @mode: crypto mode is applied to the key
 * @hint[]: a hint for user key
 * @rcu: struct rcu_head
 * @seqno: the key seqno (cluster scope)
 * @refcnt: the key reference counter
 */
struct tipc_aead {
#define TIPC_AEAD_HINT_LEN (5)
	struct tipc_tfm * __percpu *tfm_entry;
	struct tipc_crypto *crypto;
	struct tipc_aead *cloned;
	atomic_t users;
	u32 salt;
	u8 authsize;
	u8 mode;
	char hint[TIPC_AEAD_HINT_LEN + 1];
	struct rcu_head rcu;

	atomic64_t seqno ____cacheline_aligned;
	refcount_t refcnt ____cacheline_aligned;

} ____cacheline_aligned;

/**
 * struct tipc_crypto_stats - TIPC Crypto statistics
 */
struct tipc_crypto_stats {
	unsigned int stat[MAX_STATS];
};

/**
 * struct tipc_crypto - TIPC TX/RX crypto structure
 * @net: struct net
 * @node: TIPC node (RX)
 * @aead: array of pointers to AEAD keys for encryption/decryption
 * @peer_rx_active: replicated peer RX active key index
 * @key: the key states
 * @working: the crypto is working or not
 * @stats: the crypto statistics
 * @sndnxt: the per-peer sndnxt (TX)
 * @timer1: general timer 1 (jiffies)
 * @timer2: general timer 1 (jiffies)
 * @lock: tipc_key lock
 */
struct tipc_crypto {
	struct net *net;
	struct tipc_node *node;
	struct tipc_aead __rcu *aead[KEY_MAX + 1]; /* key[0] is UNUSED */
	atomic_t peer_rx_active;
	struct tipc_key key;
	u8 working:1;
	struct tipc_crypto_stats __percpu *stats;

	atomic64_t sndnxt ____cacheline_aligned;
	unsigned long timer1;
	unsigned long timer2;
	spinlock_t lock; /* crypto lock */

} ____cacheline_aligned;

/* struct tipc_crypto_tx_ctx - TX context for callbacks */
struct tipc_crypto_tx_ctx {
	struct tipc_aead *aead;
	struct tipc_bearer *bearer;
	struct tipc_media_addr dst;
};

/* struct tipc_crypto_rx_ctx - RX context for callbacks */
struct tipc_crypto_rx_ctx {
	struct tipc_aead *aead;
	struct tipc_bearer *bearer;
};

static struct tipc_aead *tipc_aead_get(struct tipc_aead __rcu *aead);
static inline void tipc_aead_put(struct tipc_aead *aead);
static void tipc_aead_free(struct rcu_head *rp);
static int tipc_aead_users(struct tipc_aead __rcu *aead);
static void tipc_aead_users_inc(struct tipc_aead __rcu *aead, int lim);
static void tipc_aead_users_dec(struct tipc_aead __rcu *aead, int lim);
static void tipc_aead_users_set(struct tipc_aead __rcu *aead, int val);
static struct crypto_aead *tipc_aead_tfm_next(struct tipc_aead *aead);
static int tipc_aead_init(struct tipc_aead **aead, struct tipc_aead_key *ukey,
			  u8 mode);
static int tipc_aead_clone(struct tipc_aead **dst, struct tipc_aead *src);
static void *tipc_aead_mem_alloc(struct crypto_aead *tfm,
				 unsigned int crypto_ctx_size,
				 u8 **iv, struct aead_request **req,
				 struct scatterlist **sg, int nsg);
static int tipc_aead_encrypt(struct tipc_aead *aead, struct sk_buff *skb,
			     struct tipc_bearer *b,
			     struct tipc_media_addr *dst,
			     struct tipc_node *__dnode);
static void tipc_aead_encrypt_done(struct crypto_async_request *base, int err);
static int tipc_aead_decrypt(struct net *net, struct tipc_aead *aead,
			     struct sk_buff *skb, struct tipc_bearer *b);
static void tipc_aead_decrypt_done(struct crypto_async_request *base, int err);
static inline int tipc_ehdr_size(struct tipc_ehdr *ehdr);
static int tipc_ehdr_build(struct net *net, struct tipc_aead *aead,
			   u8 tx_key, struct sk_buff *skb,
			   struct tipc_crypto *__rx);
static inline void tipc_crypto_key_set_state(struct tipc_crypto *c,
					     u8 new_passive,
					     u8 new_active,
					     u8 new_pending);
static int tipc_crypto_key_attach(struct tipc_crypto *c,
				  struct tipc_aead *aead, u8 pos);
static bool tipc_crypto_key_try_align(struct tipc_crypto *rx, u8 new_pending);
static struct tipc_aead *tipc_crypto_key_pick_tx(struct tipc_crypto *tx,
						 struct tipc_crypto *rx,
						 struct sk_buff *skb);
static void tipc_crypto_key_synch(struct tipc_crypto *rx, u8 new_rx_active,
				  struct tipc_msg *hdr);
static int tipc_crypto_key_revoke(struct net *net, u8 tx_key);
static void tipc_crypto_rcv_complete(struct net *net, struct tipc_aead *aead,
				     struct tipc_bearer *b,
				     struct sk_buff **skb, int err);
static void tipc_crypto_do_cmd(struct net *net, int cmd);
static char *tipc_crypto_key_dump(struct tipc_crypto *c, char *buf);
#ifdef TIPC_CRYPTO_DEBUG
static char *tipc_key_change_dump(struct tipc_key old, struct tipc_key new,
				  char *buf);
#endif

#define key_next(cur) ((cur) % KEY_MAX + 1)

#define tipc_aead_rcu_ptr(rcu_ptr, lock)				\
	rcu_dereference_protected((rcu_ptr), lockdep_is_held(lock))

#define tipc_aead_rcu_replace(rcu_ptr, ptr, lock)			\
do {									\
	typeof(rcu_ptr) __tmp = rcu_dereference_protected((rcu_ptr),	\
						lockdep_is_held(lock));	\
	rcu_assign_pointer((rcu_ptr), (ptr));				\
	tipc_aead_put(__tmp);						\
} while (0)

#define tipc_crypto_key_detach(rcu_ptr, lock)				\
	tipc_aead_rcu_replace((rcu_ptr), NULL, lock)

/**
 * tipc_aead_key_validate - Validate a AEAD user key
 */
int tipc_aead_key_validate(struct tipc_aead_key *ukey)
{
	int keylen;

	/* Check if algorithm exists */
	if (unlikely(!crypto_has_alg(ukey->alg_name, 0, 0))) {
		pr_info("Not found cipher: \"%s\"!\n", ukey->alg_name);
		return -ENODEV;
	}

	/* Currently, we only support the "gcm(aes)" cipher algorithm */
	if (strcmp(ukey->alg_name, "gcm(aes)"))
		return -ENOTSUPP;

	/* Check if key size is correct */
	keylen = ukey->keylen - TIPC_AES_GCM_SALT_SIZE;
	if (unlikely(keylen != TIPC_AES_GCM_KEY_SIZE_128 &&
		     keylen != TIPC_AES_GCM_KEY_SIZE_192 &&
		     keylen != TIPC_AES_GCM_KEY_SIZE_256))
		return -EINVAL;

	return 0;
}

static struct tipc_aead *tipc_aead_get(struct tipc_aead __rcu *aead)
{
	struct tipc_aead *tmp;

	rcu_read_lock();
	tmp = rcu_dereference(aead);
	if (unlikely(!tmp || !refcount_inc_not_zero(&tmp->refcnt)))
		tmp = NULL;
	rcu_read_unlock();

	return tmp;
}

static inline void tipc_aead_put(struct tipc_aead *aead)
{
	if (aead && refcount_dec_and_test(&aead->refcnt))
		call_rcu(&aead->rcu, tipc_aead_free);
}

/**
 * tipc_aead_free - Release AEAD key incl. all the TFMs in the list
 * @rp: rcu head pointer
 */
static void tipc_aead_free(struct rcu_head *rp)
{
	struct tipc_aead *aead = container_of(rp, struct tipc_aead, rcu);
	struct tipc_tfm *tfm_entry, *head, *tmp;

	if (aead->cloned) {
		tipc_aead_put(aead->cloned);
	} else {
		head = *this_cpu_ptr(aead->tfm_entry);
		list_for_each_entry_safe(tfm_entry, tmp, &head->list, list) {
			crypto_free_aead(tfm_entry->tfm);
			list_del(&tfm_entry->list);
			kfree(tfm_entry);
		}
		/* Free the head */
		crypto_free_aead(head->tfm);
		list_del(&head->list);
		kfree(head);
	}
	free_percpu(aead->tfm_entry);
	kfree(aead);
}

static int tipc_aead_users(struct tipc_aead __rcu *aead)
{
	struct tipc_aead *tmp;
	int users = 0;

	rcu_read_lock();
	tmp = rcu_dereference(aead);
	if (tmp)
		users = atomic_read(&tmp->users);
	rcu_read_unlock();

	return users;
}

static void tipc_aead_users_inc(struct tipc_aead __rcu *aead, int lim)
{
	struct tipc_aead *tmp;

	rcu_read_lock();
	tmp = rcu_dereference(aead);
	if (tmp)
		atomic_add_unless(&tmp->users, 1, lim);
	rcu_read_unlock();
}

static void tipc_aead_users_dec(struct tipc_aead __rcu *aead, int lim)
{
	struct tipc_aead *tmp;

	rcu_read_lock();
	tmp = rcu_dereference(aead);
	if (tmp)
		atomic_add_unless(&rcu_dereference(aead)->users, -1, lim);
	rcu_read_unlock();
}

static void tipc_aead_users_set(struct tipc_aead __rcu *aead, int val)
{
	struct tipc_aead *tmp;
	int cur;

	rcu_read_lock();
	tmp = rcu_dereference(aead);
	if (tmp) {
		do {
			cur = atomic_read(&tmp->users);
			if (cur == val)
				break;
		} while (atomic_cmpxchg(&tmp->users, cur, val) != cur);
	}
	rcu_read_unlock();
}

/**
 * tipc_aead_tfm_next - Move TFM entry to the next one in list and return it
 */
static struct crypto_aead *tipc_aead_tfm_next(struct tipc_aead *aead)
{
	struct tipc_tfm **tfm_entry = this_cpu_ptr(aead->tfm_entry);

	*tfm_entry = list_next_entry(*tfm_entry, list);
	return (*tfm_entry)->tfm;
}

/**
 * tipc_aead_init - Initiate TIPC AEAD
 * @aead: returned new TIPC AEAD key handle pointer
 * @ukey: pointer to user key data
 * @mode: the key mode
 *
 * Allocate a (list of) new cipher transformation (TFM) with the specific user
 * key data if valid. The number of the allocated TFMs can be set via the sysfs
 * "net/tipc/max_tfms" first.
 * Also, all the other AEAD data are also initialized.
 *
 * Return: 0 if the initiation is successful, otherwise: < 0
 */
static int tipc_aead_init(struct tipc_aead **aead, struct tipc_aead_key *ukey,
			  u8 mode)
{
	struct tipc_tfm *tfm_entry, *head;
	struct crypto_aead *tfm;
	struct tipc_aead *tmp;
	int keylen, err, cpu;
	int tfm_cnt = 0;

	if (unlikely(*aead))
		return -EEXIST;

	/* Allocate a new AEAD */
	tmp = kzalloc(sizeof(*tmp), GFP_ATOMIC);
	if (unlikely(!tmp))
		return -ENOMEM;

	/* The key consists of two parts: [AES-KEY][SALT] */
	keylen = ukey->keylen - TIPC_AES_GCM_SALT_SIZE;

	/* Allocate per-cpu TFM entry pointer */
	tmp->tfm_entry = alloc_percpu(struct tipc_tfm *);
	if (!tmp->tfm_entry) {
		kzfree(tmp);
		return -ENOMEM;
	}

	/* Make a list of TFMs with the user key data */
	do {
		tfm = crypto_alloc_aead(ukey->alg_name, 0, 0);
		if (IS_ERR(tfm)) {
			err = PTR_ERR(tfm);
			break;
		}

		if (unlikely(!tfm_cnt &&
			     crypto_aead_ivsize(tfm) != TIPC_AES_GCM_IV_SIZE)) {
			crypto_free_aead(tfm);
			err = -ENOTSUPP;
			break;
		}

		err = crypto_aead_setauthsize(tfm, TIPC_AES_GCM_TAG_SIZE);
		err |= crypto_aead_setkey(tfm, ukey->key, keylen);
		if (unlikely(err)) {
			crypto_free_aead(tfm);
			break;
		}

		tfm_entry = kmalloc(sizeof(*tfm_entry), GFP_KERNEL);
		if (unlikely(!tfm_entry)) {
			crypto_free_aead(tfm);
			err = -ENOMEM;
			break;
		}
		INIT_LIST_HEAD(&tfm_entry->list);
		tfm_entry->tfm = tfm;

		/* First entry? */
		if (!tfm_cnt) {
			head = tfm_entry;
			for_each_possible_cpu(cpu) {
				*per_cpu_ptr(tmp->tfm_entry, cpu) = head;
			}
		} else {
			list_add_tail(&tfm_entry->list, &head->list);
		}

	} while (++tfm_cnt < sysctl_tipc_max_tfms);

	/* Not any TFM is allocated? */
	if (!tfm_cnt) {
		free_percpu(tmp->tfm_entry);
		kzfree(tmp);
		return err;
	}

	/* Copy some chars from the user key as a hint */
	memcpy(tmp->hint, ukey->key, TIPC_AEAD_HINT_LEN);
	tmp->hint[TIPC_AEAD_HINT_LEN] = '\0';

	/* Initialize the other data */
	tmp->mode = mode;
	tmp->cloned = NULL;
	tmp->authsize = TIPC_AES_GCM_TAG_SIZE;
	memcpy(&tmp->salt, ukey->key + keylen, TIPC_AES_GCM_SALT_SIZE);
	atomic_set(&tmp->users, 0);
	atomic64_set(&tmp->seqno, 0);
	refcount_set(&tmp->refcnt, 1);

	*aead = tmp;
	return 0;
}

/**
 * tipc_aead_clone - Clone a TIPC AEAD key
 * @dst: dest key for the cloning
 * @src: source key to clone from
 *
 * Make a "copy" of the source AEAD key data to the dest, the TFMs list is
 * common for the keys.
 * A reference to the source is hold in the "cloned" pointer for the later
 * freeing purposes.
 *
 * Note: this must be done in cluster-key mode only!
 * Return: 0 in case of success, otherwise < 0
 */
static int tipc_aead_clone(struct tipc_aead **dst, struct tipc_aead *src)
{
	struct tipc_aead *aead;
	int cpu;

	if (!src)
		return -ENOKEY;

	if (src->mode != CLUSTER_KEY)
		return -EINVAL;

	if (unlikely(*dst))
		return -EEXIST;

	aead = kzalloc(sizeof(*aead), GFP_ATOMIC);
	if (unlikely(!aead))
		return -ENOMEM;

	aead->tfm_entry = alloc_percpu_gfp(struct tipc_tfm *, GFP_ATOMIC);
	if (unlikely(!aead->tfm_entry)) {
		kzfree(aead);
		return -ENOMEM;
	}

	for_each_possible_cpu(cpu) {
		*per_cpu_ptr(aead->tfm_entry, cpu) =
				*per_cpu_ptr(src->tfm_entry, cpu);
	}

	memcpy(aead->hint, src->hint, sizeof(src->hint));
	aead->mode = src->mode;
	aead->salt = src->salt;
	aead->authsize = src->authsize;
	atomic_set(&aead->users, 0);
	atomic64_set(&aead->seqno, 0);
	refcount_set(&aead->refcnt, 1);

	WARN_ON(!refcount_inc_not_zero(&src->refcnt));
	aead->cloned = src;

	*dst = aead;
	return 0;
}

/**
 * tipc_aead_mem_alloc - Allocate memory for AEAD request operations
 * @tfm: cipher handle to be registered with the request
 * @crypto_ctx_size: size of crypto context for callback
 * @iv: returned pointer to IV data
 * @req: returned pointer to AEAD request data
 * @sg: returned pointer to SG lists
 * @nsg: number of SG lists to be allocated
 *
 * Allocate memory to store the crypto context data, AEAD request, IV and SG
 * lists, the memory layout is as follows:
 * crypto_ctx || iv || aead_req || sg[]
 *
 * Return: the pointer to the memory areas in case of success, otherwise NULL
 */
static void *tipc_aead_mem_alloc(struct crypto_aead *tfm,
				 unsigned int crypto_ctx_size,
				 u8 **iv, struct aead_request **req,
				 struct scatterlist **sg, int nsg)
{
	unsigned int iv_size, req_size;
	unsigned int len;
	u8 *mem;

	iv_size = crypto_aead_ivsize(tfm);
	req_size = sizeof(**req) + crypto_aead_reqsize(tfm);

	len = crypto_ctx_size;
	len += iv_size;
	len += crypto_aead_alignmask(tfm) & ~(crypto_tfm_ctx_alignment() - 1);
	len = ALIGN(len, crypto_tfm_ctx_alignment());
	len += req_size;
	len = ALIGN(len, __alignof__(struct scatterlist));
	len += nsg * sizeof(**sg);

	mem = kmalloc(len, GFP_ATOMIC);
	if (!mem)
		return NULL;

	*iv = (u8 *)PTR_ALIGN(mem + crypto_ctx_size,
			      crypto_aead_alignmask(tfm) + 1);
	*req = (struct aead_request *)PTR_ALIGN(*iv + iv_size,
						crypto_tfm_ctx_alignment());
	*sg = (struct scatterlist *)PTR_ALIGN((u8 *)*req + req_size,
					      __alignof__(struct scatterlist));

	return (void *)mem;
}

/**
 * tipc_aead_encrypt - Encrypt a message
 * @aead: TIPC AEAD key for the message encryption
 * @skb: the input/output skb
 * @b: TIPC bearer where the message will be delivered after the encryption
 * @dst: the destination media address
 * @__dnode: TIPC dest node if "known"
 *
 * Return:
 * 0                   : if the encryption has completed
 * -EINPROGRESS/-EBUSY : if a callback will be performed
 * < 0                 : the encryption has failed
 */
static int tipc_aead_encrypt(struct tipc_aead *aead, struct sk_buff *skb,
			     struct tipc_bearer *b,
			     struct tipc_media_addr *dst,
			     struct tipc_node *__dnode)
{
	struct crypto_aead *tfm = tipc_aead_tfm_next(aead);
	struct tipc_crypto_tx_ctx *tx_ctx;
	struct aead_request *req;
	struct sk_buff *trailer;
	struct scatterlist *sg;
	struct tipc_ehdr *ehdr;
	int ehsz, len, tailen, nsg, rc;
	void *ctx;
	u32 salt;
	u8 *iv;

	/* Make sure message len at least 4-byte aligned */
	len = ALIGN(skb->len, 4);
	tailen = len - skb->len + aead->authsize;

	/* Expand skb tail for authentication tag:
	 * As for simplicity, we'd have made sure skb having enough tailroom
	 * for authentication tag @skb allocation. Even when skb is nonlinear
	 * but there is no frag_list, it should be still fine!
	 * Otherwise, we must cow it to be a writable buffer with the tailroom.
	 */
#ifdef TIPC_CRYPTO_DEBUG
	SKB_LINEAR_ASSERT(skb);
	if (tailen > skb_tailroom(skb)) {
		pr_warn("TX: skb tailroom is not enough: %d, requires: %d\n",
			skb_tailroom(skb), tailen);
	}
#endif

	if (unlikely(!skb_cloned(skb) && tailen <= skb_tailroom(skb))) {
		nsg = 1;
		trailer = skb;
	} else {
		/* TODO: We could avoid skb_cow_data() if skb has no frag_list
		 * e.g. by skb_fill_page_desc() to add another page to the skb
		 * with the wanted tailen... However, page skbs look not often,
		 * so take it easy now!
		 * Cloned skbs e.g. from link_xmit() seems no choice though :(
		 */
		nsg = skb_cow_data(skb, tailen, &trailer);
		if (unlikely(nsg < 0)) {
			pr_err("TX: skb_cow_data() returned %d\n", nsg);
			return nsg;
		}
	}

	pskb_put(skb, trailer, tailen);

	/* Allocate memory for the AEAD operation */
	ctx = tipc_aead_mem_alloc(tfm, sizeof(*tx_ctx), &iv, &req, &sg, nsg);
	if (unlikely(!ctx))
		return -ENOMEM;
	TIPC_SKB_CB(skb)->crypto_ctx = ctx;

	/* Map skb to the sg lists */
	sg_init_table(sg, nsg);
	rc = skb_to_sgvec(skb, sg, 0, skb->len);
	if (unlikely(rc < 0)) {
		pr_err("TX: skb_to_sgvec() returned %d, nsg %d!\n", rc, nsg);
		goto exit;
	}

	/* Prepare IV: [SALT (4 octets)][SEQNO (8 octets)]
	 * In case we're in cluster-key mode, SALT is varied by xor-ing with
	 * the source address (or w0 of id), otherwise with the dest address
	 * if dest is known.
	 */
	ehdr = (struct tipc_ehdr *)skb->data;
	salt = aead->salt;
	if (aead->mode == CLUSTER_KEY)
		salt ^= ehdr->addr; /* __be32 */
	else if (__dnode)
		salt ^= tipc_node_get_addr(__dnode);
	memcpy(iv, &salt, 4);
	memcpy(iv + 4, (u8 *)&ehdr->seqno, 8);

	/* Prepare request */
	ehsz = tipc_ehdr_size(ehdr);
	aead_request_set_tfm(req, tfm);
	aead_request_set_ad(req, ehsz);
	aead_request_set_crypt(req, sg, sg, len - ehsz, iv);

	/* Set callback function & data */
	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				  tipc_aead_encrypt_done, skb);
	tx_ctx = (struct tipc_crypto_tx_ctx *)ctx;
	tx_ctx->aead = aead;
	tx_ctx->bearer = b;
	memcpy(&tx_ctx->dst, dst, sizeof(*dst));

	/* Hold bearer */
	if (unlikely(!tipc_bearer_hold(b))) {
		rc = -ENODEV;
		goto exit;
	}

	/* Now, do encrypt */
	rc = crypto_aead_encrypt(req);
	if (rc == -EINPROGRESS || rc == -EBUSY)
		return rc;

	tipc_bearer_put(b);

exit:
	kfree(ctx);
	TIPC_SKB_CB(skb)->crypto_ctx = NULL;
	return rc;
}

static void tipc_aead_encrypt_done(struct crypto_async_request *base, int err)
{
	struct sk_buff *skb = base->data;
	struct tipc_crypto_tx_ctx *tx_ctx = TIPC_SKB_CB(skb)->crypto_ctx;
	struct tipc_bearer *b = tx_ctx->bearer;
	struct tipc_aead *aead = tx_ctx->aead;
	struct tipc_crypto *tx = aead->crypto;
	struct net *net = tx->net;

	switch (err) {
	case 0:
		this_cpu_inc(tx->stats->stat[STAT_ASYNC_OK]);
		if (likely(test_bit(0, &b->up)))
			b->media->send_msg(net, skb, b, &tx_ctx->dst);
		else
			kfree_skb(skb);
		break;
	case -EINPROGRESS:
		return;
	default:
		this_cpu_inc(tx->stats->stat[STAT_ASYNC_NOK]);
		kfree_skb(skb);
		break;
	}

	kfree(tx_ctx);
	tipc_bearer_put(b);
	tipc_aead_put(aead);
}

/**
 * tipc_aead_decrypt - Decrypt an encrypted message
 * @net: struct net
 * @aead: TIPC AEAD for the message decryption
 * @skb: the input/output skb
 * @b: TIPC bearer where the message has been received
 *
 * Return:
 * 0                   : if the decryption has completed
 * -EINPROGRESS/-EBUSY : if a callback will be performed
 * < 0                 : the decryption has failed
 */
static int tipc_aead_decrypt(struct net *net, struct tipc_aead *aead,
			     struct sk_buff *skb, struct tipc_bearer *b)
{
	struct tipc_crypto_rx_ctx *rx_ctx;
	struct aead_request *req;
	struct crypto_aead *tfm;
	struct sk_buff *unused;
	struct scatterlist *sg;
	struct tipc_ehdr *ehdr;
	int ehsz, nsg, rc;
	void *ctx;
	u32 salt;
	u8 *iv;

	if (unlikely(!aead))
		return -ENOKEY;

	/* Cow skb data if needed */
	if (likely(!skb_cloned(skb) &&
		   (!skb_is_nonlinear(skb) || !skb_has_frag_list(skb)))) {
		nsg = 1 + skb_shinfo(skb)->nr_frags;
	} else {
		nsg = skb_cow_data(skb, 0, &unused);
		if (unlikely(nsg < 0)) {
			pr_err("RX: skb_cow_data() returned %d\n", nsg);
			return nsg;
		}
	}

	/* Allocate memory for the AEAD operation */
	tfm = tipc_aead_tfm_next(aead);
	ctx = tipc_aead_mem_alloc(tfm, sizeof(*rx_ctx), &iv, &req, &sg, nsg);
	if (unlikely(!ctx))
		return -ENOMEM;
	TIPC_SKB_CB(skb)->crypto_ctx = ctx;

	/* Map skb to the sg lists */
	sg_init_table(sg, nsg);
	rc = skb_to_sgvec(skb, sg, 0, skb->len);
	if (unlikely(rc < 0)) {
		pr_err("RX: skb_to_sgvec() returned %d, nsg %d\n", rc, nsg);
		goto exit;
	}

	/* Reconstruct IV: */
	ehdr = (struct tipc_ehdr *)skb->data;
	salt = aead->salt;
	if (aead->mode == CLUSTER_KEY)
		salt ^= ehdr->addr; /* __be32 */
	else if (ehdr->destined)
		salt ^= tipc_own_addr(net);
	memcpy(iv, &salt, 4);
	memcpy(iv + 4, (u8 *)&ehdr->seqno, 8);

	/* Prepare request */
	ehsz = tipc_ehdr_size(ehdr);
	aead_request_set_tfm(req, tfm);
	aead_request_set_ad(req, ehsz);
	aead_request_set_crypt(req, sg, sg, skb->len - ehsz, iv);

	/* Set callback function & data */
	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				  tipc_aead_decrypt_done, skb);
	rx_ctx = (struct tipc_crypto_rx_ctx *)ctx;
	rx_ctx->aead = aead;
	rx_ctx->bearer = b;

	/* Hold bearer */
	if (unlikely(!tipc_bearer_hold(b))) {
		rc = -ENODEV;
		goto exit;
	}

	/* Now, do decrypt */
	rc = crypto_aead_decrypt(req);
	if (rc == -EINPROGRESS || rc == -EBUSY)
		return rc;

	tipc_bearer_put(b);

exit:
	kfree(ctx);
	TIPC_SKB_CB(skb)->crypto_ctx = NULL;
	return rc;
}

static void tipc_aead_decrypt_done(struct crypto_async_request *base, int err)
{
	struct sk_buff *skb = base->data;
	struct tipc_crypto_rx_ctx *rx_ctx = TIPC_SKB_CB(skb)->crypto_ctx;
	struct tipc_bearer *b = rx_ctx->bearer;
	struct tipc_aead *aead = rx_ctx->aead;
	struct tipc_crypto_stats __percpu *stats = aead->crypto->stats;
	struct net *net = aead->crypto->net;

	switch (err) {
	case 0:
		this_cpu_inc(stats->stat[STAT_ASYNC_OK]);
		break;
	case -EINPROGRESS:
		return;
	default:
		this_cpu_inc(stats->stat[STAT_ASYNC_NOK]);
		break;
	}

	kfree(rx_ctx);
	tipc_crypto_rcv_complete(net, aead, b, &skb, err);
	if (likely(skb)) {
		if (likely(test_bit(0, &b->up)))
			tipc_rcv(net, skb, b);
		else
			kfree_skb(skb);
	}

	tipc_bearer_put(b);
}

static inline int tipc_ehdr_size(struct tipc_ehdr *ehdr)
{
	return (ehdr->user != LINK_CONFIG) ? EHDR_SIZE : EHDR_CFG_SIZE;
}

/**
 * tipc_ehdr_validate - Validate an encryption message
 * @skb: the message buffer
 *
 * Returns "true" if this is a valid encryption message, otherwise "false"
 */
bool tipc_ehdr_validate(struct sk_buff *skb)
{
	struct tipc_ehdr *ehdr;
	int ehsz;

	if (unlikely(!pskb_may_pull(skb, EHDR_MIN_SIZE)))
		return false;

	ehdr = (struct tipc_ehdr *)skb->data;
	if (unlikely(ehdr->version != TIPC_EVERSION))
		return false;
	ehsz = tipc_ehdr_size(ehdr);
	if (unlikely(!pskb_may_pull(skb, ehsz)))
		return false;
	if (unlikely(skb->len <= ehsz + TIPC_AES_GCM_TAG_SIZE))
		return false;
	if (unlikely(!ehdr->tx_key))
		return false;

	return true;
}

/**
 * tipc_ehdr_build - Build TIPC encryption message header
 * @net: struct net
 * @aead: TX AEAD key to be used for the message encryption
 * @tx_key: key id used for the message encryption
 * @skb: input/output message skb
 * @__rx: RX crypto handle if dest is "known"
 *
 * Return: the header size if the building is successful, otherwise < 0
 */
static int tipc_ehdr_build(struct net *net, struct tipc_aead *aead,
			   u8 tx_key, struct sk_buff *skb,
			   struct tipc_crypto *__rx)
{
	struct tipc_msg *hdr = buf_msg(skb);
	struct tipc_ehdr *ehdr;
	u32 user = msg_user(hdr);
	u64 seqno;
	int ehsz;

	/* Make room for encryption header */
	ehsz = (user != LINK_CONFIG) ? EHDR_SIZE : EHDR_CFG_SIZE;
	WARN_ON(skb_headroom(skb) < ehsz);
	ehdr = (struct tipc_ehdr *)skb_push(skb, ehsz);

	/* Obtain a seqno first:
	 * Use the key seqno (= cluster wise) if dest is unknown or we're in
	 * cluster key mode, otherwise it's better for a per-peer seqno!
	 */
	if (!__rx || aead->mode == CLUSTER_KEY)
		seqno = atomic64_inc_return(&aead->seqno);
	else
		seqno = atomic64_inc_return(&__rx->sndnxt);

	/* Revoke the key if seqno is wrapped around */
	if (unlikely(!seqno))
		return tipc_crypto_key_revoke(net, tx_key);

	/* Word 1-2 */
	ehdr->seqno = cpu_to_be64(seqno);

	/* Words 0, 3- */
	ehdr->version = TIPC_EVERSION;
	ehdr->user = 0;
	ehdr->keepalive = 0;
	ehdr->tx_key = tx_key;
	ehdr->destined = (__rx) ? 1 : 0;
	ehdr->rx_key_active = (__rx) ? __rx->key.active : 0;
	ehdr->reserved_1 = 0;
	ehdr->reserved_2 = 0;

	switch (user) {
	case LINK_CONFIG:
		ehdr->user = LINK_CONFIG;
		memcpy(ehdr->id, tipc_own_id(net), NODE_ID_LEN);
		break;
	default:
		if (user == LINK_PROTOCOL && msg_type(hdr) == STATE_MSG) {
			ehdr->user = LINK_PROTOCOL;
			ehdr->keepalive = msg_is_keepalive(hdr);
		}
		ehdr->addr = hdr->hdr[3];
		break;
	}

	return ehsz;
}

static inline void tipc_crypto_key_set_state(struct tipc_crypto *c,
					     u8 new_passive,
					     u8 new_active,
					     u8 new_pending)
{
#ifdef TIPC_CRYPTO_DEBUG
	struct tipc_key old = c->key;
	char buf[32];
#endif

	c->key.keys = ((new_passive & KEY_MASK) << (KEY_BITS * 2)) |
		      ((new_active  & KEY_MASK) << (KEY_BITS)) |
		      ((new_pending & KEY_MASK));

#ifdef TIPC_CRYPTO_DEBUG
	pr_info("%s(%s): key changing %s ::%pS\n",
		(c->node) ? "RX" : "TX",
		(c->node) ? tipc_node_get_id_str(c->node) :
			    tipc_own_id_string(c->net),
		tipc_key_change_dump(old, c->key, buf),
		__builtin_return_address(0));
#endif
}

/**
 * tipc_crypto_key_init - Initiate a new user / AEAD key
 * @c: TIPC crypto to which new key is attached
 * @ukey: the user key
 * @mode: the key mode (CLUSTER_KEY or PER_NODE_KEY)
 *
 * A new TIPC AEAD key will be allocated and initiated with the specified user
 * key, then attached to the TIPC crypto.
 *
 * Return: new key id in case of success, otherwise: < 0
 */
int tipc_crypto_key_init(struct tipc_crypto *c, struct tipc_aead_key *ukey,
			 u8 mode)
{
	struct tipc_aead *aead = NULL;
	int rc = 0;

	/* Initiate with the new user key */
	rc = tipc_aead_init(&aead, ukey, mode);

	/* Attach it to the crypto */
	if (likely(!rc)) {
		rc = tipc_crypto_key_attach(c, aead, 0);
		if (rc < 0)
			tipc_aead_free(&aead->rcu);
	}

	pr_info("%s(%s): key initiating, rc %d!\n",
		(c->node) ? "RX" : "TX",
		(c->node) ? tipc_node_get_id_str(c->node) :
			    tipc_own_id_string(c->net),
		rc);

	return rc;
}

/**
 * tipc_crypto_key_attach - Attach a new AEAD key to TIPC crypto
 * @c: TIPC crypto to which the new AEAD key is attached
 * @aead: the new AEAD key pointer
 * @pos: desired slot in the crypto key array, = 0 if any!
 *
 * Return: new key id in case of success, otherwise: -EBUSY
 */
static int tipc_crypto_key_attach(struct tipc_crypto *c,
				  struct tipc_aead *aead, u8 pos)
{
	u8 new_pending, new_passive, new_key;
	struct tipc_key key;
	int rc = -EBUSY;

	spin_lock_bh(&c->lock);
	key = c->key;
	if (key.active && key.passive)
		goto exit;
	if (key.passive && !tipc_aead_users(c->aead[key.passive]))
		goto exit;
	if (key.pending) {
		if (pos)
			goto exit;
		if (tipc_aead_users(c->aead[key.pending]) > 0)
			goto exit;
		/* Replace it */
		new_pending = key.pending;
		new_passive = key.passive;
		new_key = new_pending;
	} else {
		if (pos) {
			if (key.active && pos != key_next(key.active)) {
				new_pending = key.pending;
				new_passive = pos;
				new_key = new_passive;
				goto attach;
			} else if (!key.active && !key.passive) {
				new_pending = pos;
				new_passive = key.passive;
				new_key = new_pending;
				goto attach;
			}
		}
		new_pending = key_next(key.active ?: key.passive);
		new_passive = key.passive;
		new_key = new_pending;
	}

attach:
	aead->crypto = c;
	tipc_crypto_key_set_state(c, new_passive, key.active, new_pending);
	tipc_aead_rcu_replace(c->aead[new_key], aead, &c->lock);

	c->working = 1;
	c->timer1 = jiffies;
	c->timer2 = jiffies;
	rc = new_key;

exit:
	spin_unlock_bh(&c->lock);
	return rc;
}

void tipc_crypto_key_flush(struct tipc_crypto *c)
{
	int k;

	spin_lock_bh(&c->lock);
	c->working = 0;
	tipc_crypto_key_set_state(c, 0, 0, 0);
	for (k = KEY_MIN; k <= KEY_MAX; k++)
		tipc_crypto_key_detach(c->aead[k], &c->lock);
	atomic_set(&c->peer_rx_active, 0);
	atomic64_set(&c->sndnxt, 0);
	spin_unlock_bh(&c->lock);
}

/**
 * tipc_crypto_key_try_align - Align RX keys if possible
 * @rx: RX crypto handle
 * @new_pending: new pending slot if aligned (= TX key from peer)
 *
 * Peer has used an unknown key slot, this only happens when peer has left and
 * rejoned, or we are newcomer.
 * That means, there must be no active key but a pending key at unaligned slot.
 * If so, we try to move the pending key to the new slot.
 * Note: A potential passive key can exist, it will be shifted correspondingly!
 *
 * Return: "true" if key is successfully aligned, otherwise "false"
 */
static bool tipc_crypto_key_try_align(struct tipc_crypto *rx, u8 new_pending)
{
	struct tipc_aead *tmp1, *tmp2 = NULL;
	struct tipc_key key;
	bool aligned = false;
	u8 new_passive = 0;
	int x;

	spin_lock(&rx->lock);
	key = rx->key;
	if (key.pending == new_pending) {
		aligned = true;
		goto exit;
	}
	if (key.active)
		goto exit;
	if (!key.pending)
		goto exit;
	if (tipc_aead_users(rx->aead[key.pending]) > 0)
		goto exit;

	/* Try to "isolate" this pending key first */
	tmp1 = tipc_aead_rcu_ptr(rx->aead[key.pending], &rx->lock);
	if (!refcount_dec_if_one(&tmp1->refcnt))
		goto exit;
	rcu_assign_pointer(rx->aead[key.pending], NULL);

	/* Move passive key if any */
	if (key.passive) {
		tmp2 = rcu_replace_pointer(rx->aead[key.passive], tmp2, lockdep_is_held(&rx->lock));
		x = (key.passive - key.pending + new_pending) % KEY_MAX;
		new_passive = (x <= 0) ? x + KEY_MAX : x;
	}

	/* Re-allocate the key(s) */
	tipc_crypto_key_set_state(rx, new_passive, 0, new_pending);
	rcu_assign_pointer(rx->aead[new_pending], tmp1);
	if (new_passive)
		rcu_assign_pointer(rx->aead[new_passive], tmp2);
	refcount_set(&tmp1->refcnt, 1);
	aligned = true;
	pr_info("RX(%s): key is aligned!\n", tipc_node_get_id_str(rx->node));

exit:
	spin_unlock(&rx->lock);
	return aligned;
}

/**
 * tipc_crypto_key_pick_tx - Pick one TX key for message decryption
 * @tx: TX crypto handle
 * @rx: RX crypto handle (can be NULL)
 * @skb: the message skb which will be decrypted later
 *
 * This function looks up the existing TX keys and pick one which is suitable
 * for the message decryption, that must be a cluster key and not used before
 * on the same message (i.e. recursive).
 *
 * Return: the TX AEAD key handle in case of success, otherwise NULL
 */
static struct tipc_aead *tipc_crypto_key_pick_tx(struct tipc_crypto *tx,
						 struct tipc_crypto *rx,
						 struct sk_buff *skb)
{
	struct tipc_skb_cb *skb_cb = TIPC_SKB_CB(skb);
	struct tipc_aead *aead = NULL;
	struct tipc_key key = tx->key;
	u8 k, i = 0;

	/* Initialize data if not yet */
	if (!skb_cb->tx_clone_deferred) {
		skb_cb->tx_clone_deferred = 1;
		memset(&skb_cb->tx_clone_ctx, 0, sizeof(skb_cb->tx_clone_ctx));
	}

	skb_cb->tx_clone_ctx.rx = rx;
	if (++skb_cb->tx_clone_ctx.recurs > 2)
		return NULL;

	/* Pick one TX key */
	spin_lock(&tx->lock);
	do {
		k = (i == 0) ? key.pending :
			((i == 1) ? key.active : key.passive);
		if (!k)
			continue;
		aead = tipc_aead_rcu_ptr(tx->aead[k], &tx->lock);
		if (!aead)
			continue;
		if (aead->mode != CLUSTER_KEY ||
		    aead == skb_cb->tx_clone_ctx.last) {
			aead = NULL;
			continue;
		}
		/* Ok, found one cluster key */
		skb_cb->tx_clone_ctx.last = aead;
		WARN_ON(skb->next);
		skb->next = skb_clone(skb, GFP_ATOMIC);
		if (unlikely(!skb->next))
			pr_warn("Failed to clone skb for next round if any\n");
		WARN_ON(!refcount_inc_not_zero(&aead->refcnt));
		break;
	} while (++i < 3);
	spin_unlock(&tx->lock);

	return aead;
}

/**
 * tipc_crypto_key_synch: Synch own key data according to peer key status
 * @rx: RX crypto handle
 * @new_rx_active: latest RX active key from peer
 * @hdr: TIPCv2 message
 *
 * This function updates the peer node related data as the peer RX active key
 * has changed, so the number of TX keys' users on this node are increased and
 * decreased correspondingly.
 *
 * The "per-peer" sndnxt is also reset when the peer key has switched.
 */
static void tipc_crypto_key_synch(struct tipc_crypto *rx, u8 new_rx_active,
				  struct tipc_msg *hdr)
{
	struct net *net = rx->net;
	struct tipc_crypto *tx = tipc_net(net)->crypto_tx;
	u8 cur_rx_active;

	/* TX might be even not ready yet */
	if (unlikely(!tx->key.active && !tx->key.pending))
		return;

	cur_rx_active = atomic_read(&rx->peer_rx_active);
	if (likely(cur_rx_active == new_rx_active))
		return;

	/* Make sure this message destined for this node */
	if (unlikely(msg_short(hdr) ||
		     msg_destnode(hdr) != tipc_own_addr(net)))
		return;

	/* Peer RX active key has changed, try to update owns' & TX users */
	if (atomic_cmpxchg(&rx->peer_rx_active,
			   cur_rx_active,
			   new_rx_active) == cur_rx_active) {
		if (new_rx_active)
			tipc_aead_users_inc(tx->aead[new_rx_active], INT_MAX);
		if (cur_rx_active)
			tipc_aead_users_dec(tx->aead[cur_rx_active], 0);

		atomic64_set(&rx->sndnxt, 0);
		/* Mark the point TX key users changed */
		tx->timer1 = jiffies;

#ifdef TIPC_CRYPTO_DEBUG
		pr_info("TX(%s): key users changed %d-- %d++, peer RX(%s)\n",
			tipc_own_id_string(net), cur_rx_active,
			new_rx_active, tipc_node_get_id_str(rx->node));
#endif
	}
}

static int tipc_crypto_key_revoke(struct net *net, u8 tx_key)
{
	struct tipc_crypto *tx = tipc_net(net)->crypto_tx;
	struct tipc_key key;

	spin_lock(&tx->lock);
	key = tx->key;
	WARN_ON(!key.active || tx_key != key.active);

	/* Free the active key */
	tipc_crypto_key_set_state(tx, key.passive, 0, key.pending);
	tipc_crypto_key_detach(tx->aead[key.active], &tx->lock);
	spin_unlock(&tx->lock);

	pr_warn("TX(%s): key is revoked!\n", tipc_own_id_string(net));
	return -EKEYREVOKED;
}

int tipc_crypto_start(struct tipc_crypto **crypto, struct net *net,
		      struct tipc_node *node)
{
	struct tipc_crypto *c;

	if (*crypto)
		return -EEXIST;

	/* Allocate crypto */
	c = kzalloc(sizeof(*c), GFP_ATOMIC);
	if (!c)
		return -ENOMEM;

	/* Allocate statistic structure */
	c->stats = alloc_percpu_gfp(struct tipc_crypto_stats, GFP_ATOMIC);
	if (!c->stats) {
		kzfree(c);
		return -ENOMEM;
	}

	c->working = 0;
	c->net = net;
	c->node = node;
	tipc_crypto_key_set_state(c, 0, 0, 0);
	atomic_set(&c->peer_rx_active, 0);
	atomic64_set(&c->sndnxt, 0);
	c->timer1 = jiffies;
	c->timer2 = jiffies;
	spin_lock_init(&c->lock);
	*crypto = c;

	return 0;
}

void tipc_crypto_stop(struct tipc_crypto **crypto)
{
	struct tipc_crypto *c, *tx, *rx;
	bool is_rx;
	u8 k;

	if (!*crypto)
		return;

	rcu_read_lock();
	/* RX stopping? => decrease TX key users if any */
	is_rx = !!((*crypto)->node);
	if (is_rx) {
		rx = *crypto;
		tx = tipc_net(rx->net)->crypto_tx;
		k = atomic_read(&rx->peer_rx_active);
		if (k) {
			tipc_aead_users_dec(tx->aead[k], 0);
			/* Mark the point TX key users changed */
			tx->timer1 = jiffies;
		}
	}

	/* Release AEAD keys */
	c = *crypto;
	for (k = KEY_MIN; k <= KEY_MAX; k++)
		tipc_aead_put(rcu_dereference(c->aead[k]));
	rcu_read_unlock();

	pr_warn("%s(%s) has been purged, node left!\n",
		(is_rx) ? "RX" : "TX",
		(is_rx) ? tipc_node_get_id_str((*crypto)->node) :
			  tipc_own_id_string((*crypto)->net));

	/* Free this crypto statistics */
	free_percpu(c->stats);

	*crypto = NULL;
	kzfree(c);
}

void tipc_crypto_timeout(struct tipc_crypto *rx)
{
	struct tipc_net *tn = tipc_net(rx->net);
	struct tipc_crypto *tx = tn->crypto_tx;
	struct tipc_key key;
	u8 new_pending, new_passive;
	int cmd;

	/* TX key activating:
	 * The pending key (users > 0) -> active
	 * The active key if any (users == 0) -> free
	 */
	spin_lock(&tx->lock);
	key = tx->key;
	if (key.active && tipc_aead_users(tx->aead[key.active]) > 0)
		goto s1;
	if (!key.pending || tipc_aead_users(tx->aead[key.pending]) <= 0)
		goto s1;
	if (time_before(jiffies, tx->timer1 + TIPC_TX_LASTING_LIM))
		goto s1;

	tipc_crypto_key_set_state(tx, key.passive, key.pending, 0);
	if (key.active)
		tipc_crypto_key_detach(tx->aead[key.active], &tx->lock);
	this_cpu_inc(tx->stats->stat[STAT_SWITCHES]);
	pr_info("TX(%s): key %d is activated!\n", tipc_own_id_string(tx->net),
		key.pending);

s1:
	spin_unlock(&tx->lock);

	/* RX key activating:
	 * The pending key (users > 0) -> active
	 * The active key if any -> passive, freed later
	 */
	spin_lock(&rx->lock);
	key = rx->key;
	if (!key.pending || tipc_aead_users(rx->aead[key.pending]) <= 0)
		goto s2;

	new_pending = (key.passive &&
		       !tipc_aead_users(rx->aead[key.passive])) ?
				       key.passive : 0;
	new_passive = (key.active) ?: ((new_pending) ? 0 : key.passive);
	tipc_crypto_key_set_state(rx, new_passive, key.pending, new_pending);
	this_cpu_inc(rx->stats->stat[STAT_SWITCHES]);
	pr_info("RX(%s): key %d is activated!\n",
		tipc_node_get_id_str(rx->node),	key.pending);
	goto s5;

s2:
	/* RX key "faulty" switching:
	 * The faulty pending key (users < -30) -> passive
	 * The passive key (users = 0) -> pending
	 * Note: This only happens after RX deactivated - s3!
	 */
	key = rx->key;
	if (!key.pending || tipc_aead_users(rx->aead[key.pending]) > -30)
		goto s3;
	if (!key.passive || tipc_aead_users(rx->aead[key.passive]) != 0)
		goto s3;

	new_pending = key.passive;
	new_passive = key.pending;
	tipc_crypto_key_set_state(rx, new_passive, key.active, new_pending);
	goto s5;

s3:
	/* RX key deactivating:
	 * The passive key if any -> pending
	 * The active key -> passive (users = 0) / pending
	 * The pending key if any -> passive (users = 0)
	 */
	key = rx->key;
	if (!key.active)
		goto s4;
	if (time_before(jiffies, rx->timer1 + TIPC_RX_ACTIVE_LIM))
		goto s4;

	new_pending = (key.passive) ?: key.active;
	new_passive = (key.passive) ? key.active : key.pending;
	tipc_aead_users_set(rx->aead[new_pending], 0);
	if (new_passive)
		tipc_aead_users_set(rx->aead[new_passive], 0);
	tipc_crypto_key_set_state(rx, new_passive, 0, new_pending);
	pr_info("RX(%s): key %d is deactivated!\n",
		tipc_node_get_id_str(rx->node), key.active);
	goto s5;

s4:
	/* RX key passive -> freed: */
	key = rx->key;
	if (!key.passive || !tipc_aead_users(rx->aead[key.passive]))
		goto s5;
	if (time_before(jiffies, rx->timer2 + TIPC_RX_PASSIVE_LIM))
		goto s5;

	tipc_crypto_key_set_state(rx, 0, key.active, key.pending);
	tipc_crypto_key_detach(rx->aead[key.passive], &rx->lock);
	pr_info("RX(%s): key %d is freed!\n", tipc_node_get_id_str(rx->node),
		key.passive);

s5:
	spin_unlock(&rx->lock);

	/* Limit max_tfms & do debug commands if needed */
	if (likely(sysctl_tipc_max_tfms <= TIPC_MAX_TFMS_LIM))
		return;

	cmd = sysctl_tipc_max_tfms;
	sysctl_tipc_max_tfms = TIPC_MAX_TFMS_DEF;
	tipc_crypto_do_cmd(rx->net, cmd);
}

/**
 * tipc_crypto_xmit - Build & encrypt TIPC message for xmit
 * @net: struct net
 * @skb: input/output message skb pointer
 * @b: bearer used for xmit later
 * @dst: destination media address
 * @__dnode: destination node for reference if any
 *
 * First, build an encryption message header on the top of the message, then
 * encrypt the original TIPC message by using the active or pending TX key.
 * If the encryption is successful, the encrypted skb is returned directly or
 * via the callback.
 * Otherwise, the skb is freed!
 *
 * Return:
 * 0                   : the encryption has succeeded (or no encryption)
 * -EINPROGRESS/-EBUSY : the encryption is ongoing, a callback will be made
 * -ENOKEK             : the encryption has failed due to no key
 * -EKEYREVOKED        : the encryption has failed due to key revoked
 * -ENOMEM             : the encryption has failed due to no memory
 * < 0                 : the encryption has failed due to other reasons
 */
int tipc_crypto_xmit(struct net *net, struct sk_buff **skb,
		     struct tipc_bearer *b, struct tipc_media_addr *dst,
		     struct tipc_node *__dnode)
{
	struct tipc_crypto *__rx = tipc_node_crypto_rx(__dnode);
	struct tipc_crypto *tx = tipc_net(net)->crypto_tx;
	struct tipc_crypto_stats __percpu *stats = tx->stats;
	struct tipc_key key = tx->key;
	struct tipc_aead *aead = NULL;
	struct sk_buff *probe;
	int rc = -ENOKEY;
	u8 tx_key;

	/* No encryption? */
	if (!tx->working)
		return 0;

	/* Try with the pending key if available and:
	 * 1) This is the only choice (i.e. no active key) or;
	 * 2) Peer has switched to this key (unicast only) or;
	 * 3) It is time to do a pending key probe;
	 */
	if (unlikely(key.pending)) {
		tx_key = key.pending;
		if (!key.active)
			goto encrypt;
		if (__rx && atomic_read(&__rx->peer_rx_active) == tx_key)
			goto encrypt;
		if (TIPC_SKB_CB(*skb)->probe)
			goto encrypt;
		if (!__rx &&
		    time_after(jiffies, tx->timer2 + TIPC_TX_PROBE_LIM)) {
			tx->timer2 = jiffies;
			probe = skb_clone(*skb, GFP_ATOMIC);
			if (probe) {
				TIPC_SKB_CB(probe)->probe = 1;
				tipc_crypto_xmit(net, &probe, b, dst, __dnode);
				if (probe)
					b->media->send_msg(net, probe, b, dst);
			}
		}
	}
	/* Else, use the active key if any */
	if (likely(key.active)) {
		tx_key = key.active;
		goto encrypt;
	}
	goto exit;

encrypt:
	aead = tipc_aead_get(tx->aead[tx_key]);
	if (unlikely(!aead))
		goto exit;
	rc = tipc_ehdr_build(net, aead, tx_key, *skb, __rx);
	if (likely(rc > 0))
		rc = tipc_aead_encrypt(aead, *skb, b, dst, __dnode);

exit:
	switch (rc) {
	case 0:
		this_cpu_inc(stats->stat[STAT_OK]);
		break;
	case -EINPROGRESS:
	case -EBUSY:
		this_cpu_inc(stats->stat[STAT_ASYNC]);
		*skb = NULL;
		return rc;
	default:
		this_cpu_inc(stats->stat[STAT_NOK]);
		if (rc == -ENOKEY)
			this_cpu_inc(stats->stat[STAT_NOKEYS]);
		else if (rc == -EKEYREVOKED)
			this_cpu_inc(stats->stat[STAT_BADKEYS]);
		kfree_skb(*skb);
		*skb = NULL;
		break;
	}

	tipc_aead_put(aead);
	return rc;
}

/**
 * tipc_crypto_rcv - Decrypt an encrypted TIPC message from peer
 * @net: struct net
 * @rx: RX crypto handle
 * @skb: input/output message skb pointer
 * @b: bearer where the message has been received
 *
 * If the decryption is successful, the decrypted skb is returned directly or
 * as the callback, the encryption header and auth tag will be trimed out
 * before forwarding to tipc_rcv() via the tipc_crypto_rcv_complete().
 * Otherwise, the skb will be freed!
 * Note: RX key(s) can be re-aligned, or in case of no key suitable, TX
 * cluster key(s) can be taken for decryption (- recursive).
 *
 * Return:
 * 0                   : the decryption has successfully completed
 * -EINPROGRESS/-EBUSY : the decryption is ongoing, a callback will be made
 * -ENOKEY             : the decryption has failed due to no key
 * -EBADMSG            : the decryption has failed due to bad message
 * -ENOMEM             : the decryption has failed due to no memory
 * < 0                 : the decryption has failed due to other reasons
 */
int tipc_crypto_rcv(struct net *net, struct tipc_crypto *rx,
		    struct sk_buff **skb, struct tipc_bearer *b)
{
	struct tipc_crypto *tx = tipc_net(net)->crypto_tx;
	struct tipc_crypto_stats __percpu *stats;
	struct tipc_aead *aead = NULL;
	struct tipc_key key;
	int rc = -ENOKEY;
	u8 tx_key = 0;

	/* New peer?
	 * Let's try with TX key (i.e. cluster mode) & verify the skb first!
	 */
	if (unlikely(!rx))
		goto pick_tx;

	/* Pick RX key according to TX key, three cases are possible:
	 * 1) The current active key (likely) or;
	 * 2) The pending (new or deactivated) key (if any) or;
	 * 3) The passive or old active key (i.e. users > 0);
	 */
	tx_key = ((struct tipc_ehdr *)(*skb)->data)->tx_key;
	key = rx->key;
	if (likely(tx_key == key.active))
		goto decrypt;
	if (tx_key == key.pending)
		goto decrypt;
	if (tx_key == key.passive) {
		rx->timer2 = jiffies;
		if (tipc_aead_users(rx->aead[key.passive]) > 0)
			goto decrypt;
	}

	/* Unknown key, let's try to align RX key(s) */
	if (tipc_crypto_key_try_align(rx, tx_key))
		goto decrypt;

pick_tx:
	/* No key suitable? Try to pick one from TX... */
	aead = tipc_crypto_key_pick_tx(tx, rx, *skb);
	if (aead)
		goto decrypt;
	goto exit;

decrypt:
	rcu_read_lock();
	if (!aead)
		aead = tipc_aead_get(rx->aead[tx_key]);
	rc = tipc_aead_decrypt(net, aead, *skb, b);
	rcu_read_unlock();

exit:
	stats = ((rx) ?: tx)->stats;
	switch (rc) {
	case 0:
		this_cpu_inc(stats->stat[STAT_OK]);
		break;
	case -EINPROGRESS:
	case -EBUSY:
		this_cpu_inc(stats->stat[STAT_ASYNC]);
		*skb = NULL;
		return rc;
	default:
		this_cpu_inc(stats->stat[STAT_NOK]);
		if (rc == -ENOKEY) {
			kfree_skb(*skb);
			*skb = NULL;
			if (rx)
				tipc_node_put(rx->node);
			this_cpu_inc(stats->stat[STAT_NOKEYS]);
			return rc;
		} else if (rc == -EBADMSG) {
			this_cpu_inc(stats->stat[STAT_BADMSGS]);
		}
		break;
	}

	tipc_crypto_rcv_complete(net, aead, b, skb, rc);
	return rc;
}

static void tipc_crypto_rcv_complete(struct net *net, struct tipc_aead *aead,
				     struct tipc_bearer *b,
				     struct sk_buff **skb, int err)
{
	struct tipc_skb_cb *skb_cb = TIPC_SKB_CB(*skb);
	struct tipc_crypto *rx = aead->crypto;
	struct tipc_aead *tmp = NULL;
	struct tipc_ehdr *ehdr;
	struct tipc_node *n;
	u8 rx_key_active;
	bool destined;

	/* Is this completed by TX? */
	if (unlikely(!rx->node)) {
		rx = skb_cb->tx_clone_ctx.rx;
#ifdef TIPC_CRYPTO_DEBUG
		pr_info("TX->RX(%s): err %d, aead %p, skb->next %p, flags %x\n",
			(rx) ? tipc_node_get_id_str(rx->node) : "-", err, aead,
			(*skb)->next, skb_cb->flags);
		pr_info("skb_cb [recurs %d, last %p], tx->aead [%p %p %p]\n",
			skb_cb->tx_clone_ctx.recurs, skb_cb->tx_clone_ctx.last,
			aead->crypto->aead[1], aead->crypto->aead[2],
			aead->crypto->aead[3]);
#endif
		if (unlikely(err)) {
			if (err == -EBADMSG && (*skb)->next)
				tipc_rcv(net, (*skb)->next, b);
			goto free_skb;
		}

		if (likely((*skb)->next)) {
			kfree_skb((*skb)->next);
			(*skb)->next = NULL;
		}
		ehdr = (struct tipc_ehdr *)(*skb)->data;
		if (!rx) {
			WARN_ON(ehdr->user != LINK_CONFIG);
			n = tipc_node_create(net, 0, ehdr->id, 0xffffu, 0,
					     true);
			rx = tipc_node_crypto_rx(n);
			if (unlikely(!rx))
				goto free_skb;
		}

		/* Skip cloning this time as we had a RX pending key */
		if (rx->key.pending)
			goto rcv;
		if (tipc_aead_clone(&tmp, aead) < 0)
			goto rcv;
		if (tipc_crypto_key_attach(rx, tmp, ehdr->tx_key) < 0) {
			tipc_aead_free(&tmp->rcu);
			goto rcv;
		}
		tipc_aead_put(aead);
		aead = tipc_aead_get(tmp);
	}

	if (unlikely(err)) {
		tipc_aead_users_dec(aead, INT_MIN);
		goto free_skb;
	}

	/* Set the RX key's user */
	tipc_aead_users_set(aead, 1);

rcv:
	/* Mark this point, RX works */
	rx->timer1 = jiffies;

	/* Remove ehdr & auth. tag prior to tipc_rcv() */
	ehdr = (struct tipc_ehdr *)(*skb)->data;
	destined = ehdr->destined;
	rx_key_active = ehdr->rx_key_active;
	skb_pull(*skb, tipc_ehdr_size(ehdr));
	pskb_trim(*skb, (*skb)->len - aead->authsize);

	/* Validate TIPCv2 message */
	if (unlikely(!tipc_msg_validate(skb))) {
		pr_err_ratelimited("Packet dropped after decryption!\n");
		goto free_skb;
	}

	/* Update peer RX active key & TX users */
	if (destined)
		tipc_crypto_key_synch(rx, rx_key_active, buf_msg(*skb));

	/* Mark skb decrypted */
	skb_cb->decrypted = 1;

	/* Clear clone cxt if any */
	if (likely(!skb_cb->tx_clone_deferred))
		goto exit;
	skb_cb->tx_clone_deferred = 0;
	memset(&skb_cb->tx_clone_ctx, 0, sizeof(skb_cb->tx_clone_ctx));
	goto exit;

free_skb:
	kfree_skb(*skb);
	*skb = NULL;

exit:
	tipc_aead_put(aead);
	if (rx)
		tipc_node_put(rx->node);
}

static void tipc_crypto_do_cmd(struct net *net, int cmd)
{
	struct tipc_net *tn = tipc_net(net);
	struct tipc_crypto *tx = tn->crypto_tx, *rx;
	struct list_head *p;
	unsigned int stat;
	int i, j, cpu;
	char buf[200];

	/* Currently only one command is supported */
	switch (cmd) {
	case 0xfff1:
		goto print_stats;
	default:
		return;
	}

print_stats:
	/* Print a header */
	pr_info("\n=============== TIPC Crypto Statistics ===============\n\n");

	/* Print key status */
	pr_info("Key status:\n");
	pr_info("TX(%7.7s)\n%s", tipc_own_id_string(net),
		tipc_crypto_key_dump(tx, buf));

	rcu_read_lock();
	for (p = tn->node_list.next; p != &tn->node_list; p = p->next) {
		rx = tipc_node_crypto_rx_by_list(p);
		pr_info("RX(%7.7s)\n%s", tipc_node_get_id_str(rx->node),
			tipc_crypto_key_dump(rx, buf));
	}
	rcu_read_unlock();

	/* Print crypto statistics */
	for (i = 0, j = 0; i < MAX_STATS; i++)
		j += scnprintf(buf + j, 200 - j, "|%11s ", hstats[i]);
	pr_info("\nCounter     %s", buf);

	memset(buf, '-', 115);
	buf[115] = '\0';
	pr_info("%s\n", buf);

	j = scnprintf(buf, 200, "TX(%7.7s) ", tipc_own_id_string(net));
	for_each_possible_cpu(cpu) {
		for (i = 0; i < MAX_STATS; i++) {
			stat = per_cpu_ptr(tx->stats, cpu)->stat[i];
			j += scnprintf(buf + j, 200 - j, "|%11d ", stat);
		}
		pr_info("%s", buf);
		j = scnprintf(buf, 200, "%12s", " ");
	}

	rcu_read_lock();
	for (p = tn->node_list.next; p != &tn->node_list; p = p->next) {
		rx = tipc_node_crypto_rx_by_list(p);
		j = scnprintf(buf, 200, "RX(%7.7s) ",
			      tipc_node_get_id_str(rx->node));
		for_each_possible_cpu(cpu) {
			for (i = 0; i < MAX_STATS; i++) {
				stat = per_cpu_ptr(rx->stats, cpu)->stat[i];
				j += scnprintf(buf + j, 200 - j, "|%11d ",
					       stat);
			}
			pr_info("%s", buf);
			j = scnprintf(buf, 200, "%12s", " ");
		}
	}
	rcu_read_unlock();

	pr_info("\n======================== Done ========================\n");
}

static char *tipc_crypto_key_dump(struct tipc_crypto *c, char *buf)
{
	struct tipc_key key = c->key;
	struct tipc_aead *aead;
	int k, i = 0;
	char *s;

	for (k = KEY_MIN; k <= KEY_MAX; k++) {
		if (k == key.passive)
			s = "PAS";
		else if (k == key.active)
			s = "ACT";
		else if (k == key.pending)
			s = "PEN";
		else
			s = "-";
		i += scnprintf(buf + i, 200 - i, "\tKey%d: %s", k, s);

		rcu_read_lock();
		aead = rcu_dereference(c->aead[k]);
		if (aead)
			i += scnprintf(buf + i, 200 - i,
				       "{\"%s...\", \"%s\"}/%d:%d",
				       aead->hint,
				       (aead->mode == CLUSTER_KEY) ? "c" : "p",
				       atomic_read(&aead->users),
				       refcount_read(&aead->refcnt));
		rcu_read_unlock();
		i += scnprintf(buf + i, 200 - i, "\n");
	}

	if (c->node)
		i += scnprintf(buf + i, 200 - i, "\tPeer RX active: %d\n",
			       atomic_read(&c->peer_rx_active));

	return buf;
}

#ifdef TIPC_CRYPTO_DEBUG
static char *tipc_key_change_dump(struct tipc_key old, struct tipc_key new,
				  char *buf)
{
	struct tipc_key *key = &old;
	int k, i = 0;
	char *s;

	/* Output format: "[%s %s %s] -> [%s %s %s]", max len = 32 */
again:
	i += scnprintf(buf + i, 32 - i, "[");
	for (k = KEY_MIN; k <= KEY_MAX; k++) {
		if (k == key->passive)
			s = "pas";
		else if (k == key->active)
			s = "act";
		else if (k == key->pending)
			s = "pen";
		else
			s = "-";
		i += scnprintf(buf + i, 32 - i,
			       (k != KEY_MAX) ? "%s " : "%s", s);
	}
	if (key != &new) {
		i += scnprintf(buf + i, 32 - i, "] -> ");
		key = &new;
		goto again;
	}
	i += scnprintf(buf + i, 32 - i, "]");
	return buf;
}
#endif
