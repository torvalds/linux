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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pfkeyv2.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <net/xfrm.h>
#if defined(CONFIG_INET_AH) || defined(CONFIG_INET_AH_MODULE) || defined(CONFIG_INET6_AH) || defined(CONFIG_INET6_AH_MODULE)
#include <net/ah.h>
#endif
#if defined(CONFIG_INET_ESP) || defined(CONFIG_INET_ESP_MODULE) || defined(CONFIG_INET6_ESP) || defined(CONFIG_INET6_ESP_MODULE)
#include <net/esp.h>
#endif

/*
 * Algorithms supported by IPsec.  These entries contain properties which
 * are used in key negotiation and xfrm processing, and are used to verify
 * that instantiated crypto transforms have correct parameters for IPsec
 * purposes.
 */
static struct xfrm_algo_desc aalg_list[] = {
{
	.name = "hmac(digest_null)",
	.compat = "digest_null",

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
	.name = "hmac(md5)",
	.compat = "md5",

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
	.name = "hmac(sha1)",
	.compat = "sha1",

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
	.name = "hmac(sha256)",
	.compat = "sha256",

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
	.name = "hmac(ripemd160)",
	.compat = "ripemd160",

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
{
	.name = "xcbc(aes)",

	.uinfo = {
		.auth = {
			.icv_truncbits = 96,
			.icv_fullbits = 128,
		}
	},

	.desc = {
		.sadb_alg_id = SADB_X_AALG_AES_XCBC_MAC,
		.sadb_alg_ivlen = 0,
		.sadb_alg_minbits = 128,
		.sadb_alg_maxbits = 128
	}
},
};

static struct xfrm_algo_desc ealg_list[] = {
{
	.name = "ecb(cipher_null)",
	.compat = "cipher_null",

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
	.name = "cbc(des)",
	.compat = "des",

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
	.name = "cbc(des3_ede)",
	.compat = "des3_ede",

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
	.name = "cbc(cast128)",
	.compat = "cast128",

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
	.name = "cbc(blowfish)",
	.compat = "blowfish",

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
	.name = "cbc(aes)",
	.compat = "aes",

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
	.name = "cbc(serpent)",
	.compat = "serpent",

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
	.name = "cbc(camellia)",

	.uinfo = {
		.encr = {
			.blockbits = 128,
			.defkeybits = 128,
		}
	},

	.desc = {
		.sadb_alg_id = SADB_X_EALG_CAMELLIACBC,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 128,
		.sadb_alg_maxbits = 256
	}
},
{
	.name = "cbc(twofish)",
	.compat = "twofish",

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

struct xfrm_algo_list {
	struct xfrm_algo_desc *algs;
	int entries;
	u32 type;
	u32 mask;
};

static const struct xfrm_algo_list xfrm_aalg_list = {
	.algs = aalg_list,
	.entries = ARRAY_SIZE(aalg_list),
	.type = CRYPTO_ALG_TYPE_HASH,
	.mask = CRYPTO_ALG_TYPE_HASH_MASK | CRYPTO_ALG_ASYNC,
};

static const struct xfrm_algo_list xfrm_ealg_list = {
	.algs = ealg_list,
	.entries = ARRAY_SIZE(ealg_list),
	.type = CRYPTO_ALG_TYPE_BLKCIPHER,
	.mask = CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_ASYNC,
};

static const struct xfrm_algo_list xfrm_calg_list = {
	.algs = calg_list,
	.entries = ARRAY_SIZE(calg_list),
	.type = CRYPTO_ALG_TYPE_COMPRESS,
	.mask = CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_ASYNC,
};

static struct xfrm_algo_desc *xfrm_find_algo(
	const struct xfrm_algo_list *algo_list,
	int match(const struct xfrm_algo_desc *entry, const void *data),
	const void *data, int probe)
{
	struct xfrm_algo_desc *list = algo_list->algs;
	int i, status;

	for (i = 0; i < algo_list->entries; i++) {
		if (!match(list + i, data))
			continue;

		if (list[i].available)
			return &list[i];

		if (!probe)
			break;

		status = crypto_has_alg(list[i].name, algo_list->type,
					algo_list->mask);
		if (!status)
			break;

		list[i].available = status;
		return &list[i];
	}
	return NULL;
}

static int xfrm_alg_id_match(const struct xfrm_algo_desc *entry,
			     const void *data)
{
	return entry->desc.sadb_alg_id == (unsigned long)data;
}

struct xfrm_algo_desc *xfrm_aalg_get_byid(int alg_id)
{
	return xfrm_find_algo(&xfrm_aalg_list, xfrm_alg_id_match,
			      (void *)(unsigned long)alg_id, 1);
}
EXPORT_SYMBOL_GPL(xfrm_aalg_get_byid);

struct xfrm_algo_desc *xfrm_ealg_get_byid(int alg_id)
{
	return xfrm_find_algo(&xfrm_ealg_list, xfrm_alg_id_match,
			      (void *)(unsigned long)alg_id, 1);
}
EXPORT_SYMBOL_GPL(xfrm_ealg_get_byid);

struct xfrm_algo_desc *xfrm_calg_get_byid(int alg_id)
{
	return xfrm_find_algo(&xfrm_calg_list, xfrm_alg_id_match,
			      (void *)(unsigned long)alg_id, 1);
}
EXPORT_SYMBOL_GPL(xfrm_calg_get_byid);

static int xfrm_alg_name_match(const struct xfrm_algo_desc *entry,
			       const void *data)
{
	const char *name = data;

	return name && (!strcmp(name, entry->name) ||
			(entry->compat && !strcmp(name, entry->compat)));
}

struct xfrm_algo_desc *xfrm_aalg_get_byname(char *name, int probe)
{
	return xfrm_find_algo(&xfrm_aalg_list, xfrm_alg_name_match, name,
			      probe);
}
EXPORT_SYMBOL_GPL(xfrm_aalg_get_byname);

struct xfrm_algo_desc *xfrm_ealg_get_byname(char *name, int probe)
{
	return xfrm_find_algo(&xfrm_ealg_list, xfrm_alg_name_match, name,
			      probe);
}
EXPORT_SYMBOL_GPL(xfrm_ealg_get_byname);

struct xfrm_algo_desc *xfrm_calg_get_byname(char *name, int probe)
{
	return xfrm_find_algo(&xfrm_calg_list, xfrm_alg_name_match, name,
			      probe);
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
		status = crypto_has_hash(aalg_list[i].name, 0,
					 CRYPTO_ALG_ASYNC);
		if (aalg_list[i].available != status)
			aalg_list[i].available = status;
	}

	for (i = 0; i < ealg_entries(); i++) {
		status = crypto_has_blkcipher(ealg_list[i].name, 0,
					      CRYPTO_ALG_ASYNC);
		if (ealg_list[i].available != status)
			ealg_list[i].available = status;
	}

	for (i = 0; i < calg_entries(); i++) {
		status = crypto_has_comp(calg_list[i].name, 0,
					 CRYPTO_ALG_ASYNC);
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

int skb_icv_walk(const struct sk_buff *skb, struct hash_desc *desc,
		 int offset, int len, icv_update_fn_t icv_update)
{
	int start = skb_headlen(skb);
	int i, copy = start - offset;
	int err;
	struct scatterlist sg;

	/* Checksum header. */
	if (copy > 0) {
		if (copy > len)
			copy = len;

		sg_init_one(&sg, skb->data + offset, copy);

		err = icv_update(desc, &sg, copy);
		if (unlikely(err))
			return err;

		if ((len -= copy) == 0)
			return 0;
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

			sg_init_table(&sg, 1);
			sg_set_page(&sg, frag->page, copy,
				    frag->page_offset + offset-start);

			err = icv_update(desc, &sg, copy);
			if (unlikely(err))
				return err;

			if (!(len -= copy))
				return 0;
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
				err = skb_icv_walk(list, desc, offset-start,
						   copy, icv_update);
				if (unlikely(err))
					return err;
				if ((len -= copy) == 0)
					return 0;
				offset += copy;
			}
			start = end;
		}
	}
	BUG_ON(len);
	return 0;
}
EXPORT_SYMBOL_GPL(skb_icv_walk);

#if defined(CONFIG_INET_ESP) || defined(CONFIG_INET_ESP_MODULE) || defined(CONFIG_INET6_ESP) || defined(CONFIG_INET6_ESP_MODULE)

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
