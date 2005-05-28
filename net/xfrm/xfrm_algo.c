/* 
 * xfrm algorithm interface
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pfkeyv2.h>
#include <linux/crypto.h>
#include <net/xfrm.h>
#if defined(CONFIG_INET_AH) || defined(CONFIG_INET_AH_MODULE) || defined(CONFIG_INET6_AH) || defined(CONFIG_INET6_AH_MODULE)
#include <net/ah.h>
#endif
#if defined(CONFIG_INET_ESP) || defined(CONFIG_INET_ESP_MODULE) || defined(CONFIG_INET6_ESP) || defined(CONFIG_INET6_ESP_MODULE)
#include <net/esp.h>
#endif
#include <asm/scatterlist.h>

/*
 * Algorithms supported by IPsec.  These entries contain properties which
 * are used in key negotiation and xfrm processing, and are used to verify
 * that instantiated crypto transforms have correct parameters for IPsec
 * purposes.
 */
static struct xfrm_algo_desc aalg_list[] = {
{
	.name = "digest_null",
	
	.uinfo = {
		.auth = {
			.icv_truncbits = 0,
			.icv_fullbits = 0,
		}
	},
	
	.desc = {
		.sadb_alg_id = SADB_X_AALG_NULL,
		.sadb_alg_ivlen = 0,
		.sadb_alg_minbits = 0,
		.sadb_alg_maxbits = 0
	}
},
{
	.name = "md5",

	.uinfo = {
		.auth = {
			.icv_truncbits = 96,
			.icv_fullbits = 128,
		}
	},
	
	.desc = {
		.sadb_alg_id = SADB_AALG_MD5HMAC,
		.sadb_alg_ivlen = 0,
		.sadb_alg_minbits = 128,
		.sadb_alg_maxbits = 128
	}
},
{
	.name = "sha1",

	.uinfo = {
		.auth = {
			.icv_truncbits = 96,
			.icv_fullbits = 160,
		}
	},

	.desc = {
		.sadb_alg_id = SADB_AALG_SHA1HMAC,
		.sadb_alg_ivlen = 0,
		.sadb_alg_minbits = 160,
		.sadb_alg_maxbits = 160
	}
},
{
	.name = "sha256",

	.uinfo = {
		.auth = {
			.icv_truncbits = 96,
			.icv_fullbits = 256,
		}
	},

	.desc = {
		.sadb_alg_id = SADB_X_AALG_SHA2_256HMAC,
		.sadb_alg_ivlen = 0,
		.sadb_alg_minbits = 256,
		.sadb_alg_maxbits = 256
	}
},
{
	.name = "ripemd160",

	.uinfo = {
		.auth = {
			.icv_truncbits = 96,
			.icv_fullbits = 160,
		}
	},

	.desc = {
		.sadb_alg_id = SADB_X_AALG_RIPEMD160HMAC,
		.sadb_alg_ivlen = 0,
		.sadb_alg_minbits = 160,
		.sadb_alg_maxbits = 160
	}
},
};

static struct xfrm_algo_desc ealg_list[] = {
{
	.name = "cipher_null",
	
	.uinfo = {
		.encr = {
			.blockbits = 8,
			.defkeybits = 0,
		}
	},
	
	.desc = {
		.sadb_alg_id =	SADB_EALG_NULL,
		.sadb_alg_ivlen = 0,
		.sadb_alg_minbits = 0,
		.sadb_alg_maxbits = 0
	}
},
{
	.name = "des",

	.uinfo = {
		.encr = {
			.blockbits = 64,
			.defkeybits = 64,
		}
	},

	.desc = {
		.sadb_alg_id = SADB_EALG_DESCBC,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 64,
		.sadb_alg_maxbits = 64
	}
},
{
	.name = "des3_ede",

	.uinfo = {
		.encr = {
			.blockbits = 64,
			.defkeybits = 192,
		}
	},

	.desc = {
		.sadb_alg_id = SADB_EALG_3DESCBC,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 192,
		.sadb_alg_maxbits = 192
	}
},
{
	.name = "cast128",

	.uinfo = {
		.encr = {
			.blockbits = 64,
			.defkeybits = 128,
		}
	},

	.desc = {
		.sadb_alg_id = SADB_X_EALG_CASTCBC,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 40,
		.sadb_alg_maxbits = 128
	}
},
{
	.name = "blowfish",

	.uinfo = {
		.encr = {
			.blockbits = 64,
			.defkeybits = 128,
		}
	},

	.desc = {
		.sadb_alg_id = SADB_X_EALG_BLOWFISHCBC,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 40,
		.sadb_alg_maxbits = 448
	}
},
{
	.name = "aes",

	.uinfo = {
		.encr = {
			.blockbits = 128,
			.defkeybits = 128,
		}
	},

	.desc = {
		.sadb_alg_id = SADB_X_EALG_AESCBC,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 128,
		.sadb_alg_maxbits = 256
	}
},
{
        .name = "serpent",

        .uinfo = {
                .encr = {
                        .blockbits = 128,
                        .defkeybits = 128,
                }
        },

        .desc = {
                .sadb_alg_id = SADB_X_EALG_SERPENTCBC,
                .sadb_alg_ivlen = 8,
                .sadb_alg_minbits = 128,
                .sadb_alg_maxbits = 256,
        }
},
{
        .name = "twofish",
                 
        .uinfo = {
                .encr = {
                        .blockbits = 128,
                        .defkeybits = 128,
                }
        },

        .desc = {
                .sadb_alg_id = SADB_X_EALG_TWOFISHCBC,
                .sadb_alg_ivlen = 8,
                .sadb_alg_minbits = 128,
                .sadb_alg_maxbits = 256
        }
},
};

static struct xfrm_algo_desc calg_list[] = {
{
	.name = "deflate",
	.uinfo = {
		.comp = {
			.threshold = 90,
		}
	},
	.desc = { .sadb_alg_id = SADB_X_CALG_DEFLATE }
},
{
	.name = "lzs",
	.uinfo = {
		.comp = {
			.threshold = 90,
		}
	},
	.desc = { .sadb_alg_id = SADB_X_CALG_LZS }
},
{
	.name = "lzjh",
	.uinfo = {
		.comp = {
			.threshold = 50,
		}
	},
	.desc = { .sadb_alg_id = SADB_X_CALG_LZJH }
},
};

static inline int aalg_entries(void)
{
	return ARRAY_SIZE(aalg_list);
}

static inline int ealg_entries(void)
{
	return ARRAY_SIZE(ealg_list);
}

static inline int calg_entries(void)
{
	return ARRAY_SIZE(calg_list);
}

/* Todo: generic iterators */
struct xfrm_algo_desc *xfrm_aalg_get_byid(int alg_id)
{
	int i;

	for (i = 0; i < aalg_entries(); i++) {
		if (aalg_list[i].desc.sadb_alg_id == alg_id) {
			if (aalg_list[i].available)
				return &aalg_list[i];
			else
				break;
		}
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(xfrm_aalg_get_byid);

struct xfrm_algo_desc *xfrm_ealg_get_byid(int alg_id)
{
	int i;

	for (i = 0; i < ealg_entries(); i++) {
		if (ealg_list[i].desc.sadb_alg_id == alg_id) {
			if (ealg_list[i].available)
				return &ealg_list[i];
			else
				break;
		}
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(xfrm_ealg_get_byid);

struct xfrm_algo_desc *xfrm_calg_get_byid(int alg_id)
{
	int i;

	for (i = 0; i < calg_entries(); i++) {
		if (calg_list[i].desc.sadb_alg_id == alg_id) {
			if (calg_list[i].available)
				return &calg_list[i];
			else
				break;
		}
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(xfrm_calg_get_byid);

static struct xfrm_algo_desc *xfrm_get_byname(struct xfrm_algo_desc *list,
					      int entries, char *name,
					      int probe)
{
	int i, status;

	if (!name)
		return NULL;

	for (i = 0; i < entries; i++) {
		if (strcmp(name, list[i].name))
			continue;

		if (list[i].available)
			return &list[i];

		if (!probe)
			break;

		status = crypto_alg_available(name, 0);
		if (!status)
			break;

		list[i].available = status;
		return &list[i];
	}
	return NULL;
}

struct xfrm_algo_desc *xfrm_aalg_get_byname(char *name, int probe)
{
	return xfrm_get_byname(aalg_list, aalg_entries(), name, probe);
}
EXPORT_SYMBOL_GPL(xfrm_aalg_get_byname);

struct xfrm_algo_desc *xfrm_ealg_get_byname(char *name, int probe)
{
	return xfrm_get_byname(ealg_list, ealg_entries(), name, probe);
}
EXPORT_SYMBOL_GPL(xfrm_ealg_get_byname);

struct xfrm_algo_desc *xfrm_calg_get_byname(char *name, int probe)
{
	return xfrm_get_byname(calg_list, calg_entries(), name, probe);
}
EXPORT_SYMBOL_GPL(xfrm_calg_get_byname);

struct xfrm_algo_desc *xfrm_aalg_get_byidx(unsigned int idx)
{
	if (idx >= aalg_entries())
		return NULL;

	return &aalg_list[idx];
}
EXPORT_SYMBOL_GPL(xfrm_aalg_get_byidx);

struct xfrm_algo_desc *xfrm_ealg_get_byidx(unsigned int idx)
{
	if (idx >= ealg_entries())
		return NULL;

	return &ealg_list[idx];
}
EXPORT_SYMBOL_GPL(xfrm_ealg_get_byidx);

/*
 * Probe for the availability of crypto algorithms, and set the available
 * flag for any algorithms found on the system.  This is typically called by
 * pfkey during userspace SA add, update or register.
 */
void xfrm_probe_algs(void)
{
#ifdef CONFIG_CRYPTO
	int i, status;
	
	BUG_ON(in_softirq());

	for (i = 0; i < aalg_entries(); i++) {
		status = crypto_alg_available(aalg_list[i].name, 0);
		if (aalg_list[i].available != status)
			aalg_list[i].available = status;
	}
	
	for (i = 0; i < ealg_entries(); i++) {
		status = crypto_alg_available(ealg_list[i].name, 0);
		if (ealg_list[i].available != status)
			ealg_list[i].available = status;
	}
	
	for (i = 0; i < calg_entries(); i++) {
		status = crypto_alg_available(calg_list[i].name, 0);
		if (calg_list[i].available != status)
			calg_list[i].available = status;
	}
#endif
}
EXPORT_SYMBOL_GPL(xfrm_probe_algs);

int xfrm_count_auth_supported(void)
{
	int i, n;

	for (i = 0, n = 0; i < aalg_entries(); i++)
		if (aalg_list[i].available)
			n++;
	return n;
}
EXPORT_SYMBOL_GPL(xfrm_count_auth_supported);

int xfrm_count_enc_supported(void)
{
	int i, n;

	for (i = 0, n = 0; i < ealg_entries(); i++)
		if (ealg_list[i].available)
			n++;
	return n;
}
EXPORT_SYMBOL_GPL(xfrm_count_enc_supported);

/* Move to common area: it is shared with AH. */

void skb_icv_walk(const struct sk_buff *skb, struct crypto_tfm *tfm,
		  int offset, int len, icv_update_fn_t icv_update)
{
	int start = skb_headlen(skb);
	int i, copy = start - offset;
	struct scatterlist sg;

	/* Checksum header. */
	if (copy > 0) {
		if (copy > len)
			copy = len;
		
		sg.page = virt_to_page(skb->data + offset);
		sg.offset = (unsigned long)(skb->data + offset) % PAGE_SIZE;
		sg.length = copy;
		
		icv_update(tfm, &sg, 1);
		
		if ((len -= copy) == 0)
			return;
		offset += copy;
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int end;

		BUG_TRAP(start <= offset + len);

		end = start + skb_shinfo(skb)->frags[i].size;
		if ((copy = end - offset) > 0) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

			if (copy > len)
				copy = len;
			
			sg.page = frag->page;
			sg.offset = frag->page_offset + offset-start;
			sg.length = copy;
			
			icv_update(tfm, &sg, 1);

			if (!(len -= copy))
				return;
			offset += copy;
		}
		start = end;
	}

	if (skb_shinfo(skb)->frag_list) {
		struct sk_buff *list = skb_shinfo(skb)->frag_list;

		for (; list; list = list->next) {
			int end;

			BUG_TRAP(start <= offset + len);

			end = start + list->len;
			if ((copy = end - offset) > 0) {
				if (copy > len)
					copy = len;
				skb_icv_walk(list, tfm, offset-start, copy, icv_update);
				if ((len -= copy) == 0)
					return;
				offset += copy;
			}
			start = end;
		}
	}
	if (len)
		BUG();
}
EXPORT_SYMBOL_GPL(skb_icv_walk);

#if defined(CONFIG_INET_ESP) || defined(CONFIG_INET_ESP_MODULE) || defined(CONFIG_INET6_ESP) || defined(CONFIG_INET6_ESP_MODULE)

/* Looking generic it is not used in another places. */

int
skb_to_sgvec(struct sk_buff *skb, struct scatterlist *sg, int offset, int len)
{
	int start = skb_headlen(skb);
	int i, copy = start - offset;
	int elt = 0;

	if (copy > 0) {
		if (copy > len)
			copy = len;
		sg[elt].page = virt_to_page(skb->data + offset);
		sg[elt].offset = (unsigned long)(skb->data + offset) % PAGE_SIZE;
		sg[elt].length = copy;
		elt++;
		if ((len -= copy) == 0)
			return elt;
		offset += copy;
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int end;

		BUG_TRAP(start <= offset + len);

		end = start + skb_shinfo(skb)->frags[i].size;
		if ((copy = end - offset) > 0) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

			if (copy > len)
				copy = len;
			sg[elt].page = frag->page;
			sg[elt].offset = frag->page_offset+offset-start;
			sg[elt].length = copy;
			elt++;
			if (!(len -= copy))
				return elt;
			offset += copy;
		}
		start = end;
	}

	if (skb_shinfo(skb)->frag_list) {
		struct sk_buff *list = skb_shinfo(skb)->frag_list;

		for (; list; list = list->next) {
			int end;

			BUG_TRAP(start <= offset + len);

			end = start + list->len;
			if ((copy = end - offset) > 0) {
				if (copy > len)
					copy = len;
				elt += skb_to_sgvec(list, sg+elt, offset - start, copy);
				if ((len -= copy) == 0)
					return elt;
				offset += copy;
			}
			start = end;
		}
	}
	if (len)
		BUG();
	return elt;
}
EXPORT_SYMBOL_GPL(skb_to_sgvec);

/* Check that skb data bits are writable. If they are not, copy data
 * to newly created private area. If "tailbits" is given, make sure that
 * tailbits bytes beyond current end of skb are writable.
 *
 * Returns amount of elements of scatterlist to load for subsequent
 * transformations and pointer to writable trailer skb.
 */

int skb_cow_data(struct sk_buff *skb, int tailbits, struct sk_buff **trailer)
{
	int copyflag;
	int elt;
	struct sk_buff *skb1, **skb_p;

	/* If skb is cloned or its head is paged, reallocate
	 * head pulling out all the pages (pages are considered not writable
	 * at the moment even if they are anonymous).
	 */
	if ((skb_cloned(skb) || skb_shinfo(skb)->nr_frags) &&
	    __pskb_pull_tail(skb, skb_pagelen(skb)-skb_headlen(skb)) == NULL)
		return -ENOMEM;

	/* Easy case. Most of packets will go this way. */
	if (!skb_shinfo(skb)->frag_list) {
		/* A little of trouble, not enough of space for trailer.
		 * This should not happen, when stack is tuned to generate
		 * good frames. OK, on miss we reallocate and reserve even more
		 * space, 128 bytes is fair. */

		if (skb_tailroom(skb) < tailbits &&
		    pskb_expand_head(skb, 0, tailbits-skb_tailroom(skb)+128, GFP_ATOMIC))
			return -ENOMEM;

		/* Voila! */
		*trailer = skb;
		return 1;
	}

	/* Misery. We are in troubles, going to mincer fragments... */

	elt = 1;
	skb_p = &skb_shinfo(skb)->frag_list;
	copyflag = 0;

	while ((skb1 = *skb_p) != NULL) {
		int ntail = 0;

		/* The fragment is partially pulled by someone,
		 * this can happen on input. Copy it and everything
		 * after it. */

		if (skb_shared(skb1))
			copyflag = 1;

		/* If the skb is the last, worry about trailer. */

		if (skb1->next == NULL && tailbits) {
			if (skb_shinfo(skb1)->nr_frags ||
			    skb_shinfo(skb1)->frag_list ||
			    skb_tailroom(skb1) < tailbits)
				ntail = tailbits + 128;
		}

		if (copyflag ||
		    skb_cloned(skb1) ||
		    ntail ||
		    skb_shinfo(skb1)->nr_frags ||
		    skb_shinfo(skb1)->frag_list) {
			struct sk_buff *skb2;

			/* Fuck, we are miserable poor guys... */
			if (ntail == 0)
				skb2 = skb_copy(skb1, GFP_ATOMIC);
			else
				skb2 = skb_copy_expand(skb1,
						       skb_headroom(skb1),
						       ntail,
						       GFP_ATOMIC);
			if (unlikely(skb2 == NULL))
				return -ENOMEM;

			if (skb1->sk)
				skb_set_owner_w(skb2, skb1->sk);

			/* Looking around. Are we still alive?
			 * OK, link new skb, drop old one */

			skb2->next = skb1->next;
			*skb_p = skb2;
			kfree_skb(skb1);
			skb1 = skb2;
		}
		elt++;
		*trailer = skb1;
		skb_p = &skb1->next;
	}

	return elt;
}
EXPORT_SYMBOL_GPL(skb_cow_data);

void *pskb_put(struct sk_buff *skb, struct sk_buff *tail, int len)
{
	if (tail != skb) {
		skb->data_len += len;
		skb->len += len;
	}
	return skb_put(tail, len);
}
EXPORT_SYMBOL_GPL(pskb_put);
#endif
