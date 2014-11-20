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
#if defined(CONFIG_INET_ESP) || defined(CONFIG_INET_ESP_MODULE) || defined(CONFIG_INET6_ESP) || defined(CONFIG_INET6_ESP_MODULE)
#include <net/esp.h>
#endif

/*
 * Algorithms supported by IPsec.  These entries contain properties which
 * are used in key negotiation and xfrm processing, and are used to verify
 * that instantiated crypto transforms have correct parameters for IPsec
 * purposes.
 */
static struct xfrm_algo_desc aead_list[] = {
{
	.name = "rfc4106(gcm(aes))",

	.uinfo = {
		.aead = {
			.icv_truncbits = 64,
		}
	},

	.pfkey_supported = 1,

	.desc = {
		.sadb_alg_id = SADB_X_EALG_AES_GCM_ICV8,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 128,
		.sadb_alg_maxbits = 256
	}
},
{
	.name = "rfc4106(gcm(aes))",

	.uinfo = {
		.aead = {
			.icv_truncbits = 96,
		}
	},

	.pfkey_supported = 1,

	.desc = {
		.sadb_alg_id = SADB_X_EALG_AES_GCM_ICV12,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 128,
		.sadb_alg_maxbits = 256
	}
},
{
	.name = "rfc4106(gcm(aes))",

	.uinfo = {
		.aead = {
			.icv_truncbits = 128,
		}
	},

	.pfkey_supported = 1,

	.desc = {
		.sadb_alg_id = SADB_X_EALG_AES_GCM_ICV16,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 128,
		.sadb_alg_maxbits = 256
	}
},
{
	.name = "rfc4309(ccm(aes))",

	.uinfo = {
		.aead = {
			.icv_truncbits = 64,
		}
	},

	.pfkey_supported = 1,

	.desc = {
		.sadb_alg_id = SADB_X_EALG_AES_CCM_ICV8,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 128,
		.sadb_alg_maxbits = 256
	}
},
{
	.name = "rfc4309(ccm(aes))",

	.uinfo = {
		.aead = {
			.icv_truncbits = 96,
		}
	},

	.pfkey_supported = 1,

	.desc = {
		.sadb_alg_id = SADB_X_EALG_AES_CCM_ICV12,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 128,
		.sadb_alg_maxbits = 256
	}
},
{
	.name = "rfc4309(ccm(aes))",

	.uinfo = {
		.aead = {
			.icv_truncbits = 128,
		}
	},

	.pfkey_supported = 1,

	.desc = {
		.sadb_alg_id = SADB_X_EALG_AES_CCM_ICV16,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 128,
		.sadb_alg_maxbits = 256
	}
},
{
	.name = "rfc4543(gcm(aes))",

	.uinfo = {
		.aead = {
			.icv_truncbits = 128,
		}
	},

	.pfkey_supported = 1,

	.desc = {
		.sadb_alg_id = SADB_X_EALG_NULL_AES_GMAC,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 128,
		.sadb_alg_maxbits = 256
	}
},
};

static struct xfrm_algo_desc aalg_list[] = {
{
	.name = "digest_null",

	.uinfo = {
		.auth = {
			.icv_truncbits = 0,
			.icv_fullbits = 0,
		}
	},

	.pfkey_supported = 1,

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

	.pfkey_supported = 1,

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

	.pfkey_supported = 1,

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

	.pfkey_supported = 1,

	.desc = {
		.sadb_alg_id = SADB_X_AALG_SHA2_256HMAC,
		.sadb_alg_ivlen = 0,
		.sadb_alg_minbits = 256,
		.sadb_alg_maxbits = 256
	}
},
{
	.name = "hmac(sha384)",

	.uinfo = {
		.auth = {
			.icv_truncbits = 192,
			.icv_fullbits = 384,
		}
	},

	.pfkey_supported = 1,

	.desc = {
		.sadb_alg_id = SADB_X_AALG_SHA2_384HMAC,
		.sadb_alg_ivlen = 0,
		.sadb_alg_minbits = 384,
		.sadb_alg_maxbits = 384
	}
},
{
	.name = "hmac(sha512)",

	.uinfo = {
		.auth = {
			.icv_truncbits = 256,
			.icv_fullbits = 512,
		}
	},

	.pfkey_supported = 1,

	.desc = {
		.sadb_alg_id = SADB_X_AALG_SHA2_512HMAC,
		.sadb_alg_ivlen = 0,
		.sadb_alg_minbits = 512,
		.sadb_alg_maxbits = 512
	}
},
{
	.name = "hmac(rmd160)",
	.compat = "rmd160",

	.uinfo = {
		.auth = {
			.icv_truncbits = 96,
			.icv_fullbits = 160,
		}
	},

	.pfkey_supported = 1,

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

	.pfkey_supported = 1,

	.desc = {
		.sadb_alg_id = SADB_X_AALG_AES_XCBC_MAC,
		.sadb_alg_ivlen = 0,
		.sadb_alg_minbits = 128,
		.sadb_alg_maxbits = 128
	}
},
{
	/* rfc4494 */
	.name = "cmac(aes)",

	.uinfo = {
		.auth = {
			.icv_truncbits = 96,
			.icv_fullbits = 128,
		}
	},

	.pfkey_supported = 0,
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

	.pfkey_supported = 1,

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

	.pfkey_supported = 1,

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

	.pfkey_supported = 1,

	.desc = {
		.sadb_alg_id = SADB_EALG_3DESCBC,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 192,
		.sadb_alg_maxbits = 192
	}
},
{
	.name = "cbc(cast5)",
	.compat = "cast5",

	.uinfo = {
		.encr = {
			.blockbits = 64,
			.defkeybits = 128,
		}
	},

	.pfkey_supported = 1,

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

	.pfkey_supported = 1,

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

	.pfkey_supported = 1,

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

	.pfkey_supported = 1,

	.desc = {
		.sadb_alg_id = SADB_X_EALG_SERPENTCBC,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 128,
		.sadb_alg_maxbits = 256,
	}
},
{
	.name = "cbc(camellia)",
	.compat = "camellia",

	.uinfo = {
		.encr = {
			.blockbits = 128,
			.defkeybits = 128,
		}
	},

	.pfkey_supported = 1,

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

	.pfkey_supported = 1,

	.desc = {
		.sadb_alg_id = SADB_X_EALG_TWOFISHCBC,
		.sadb_alg_ivlen = 8,
		.sadb_alg_minbits = 128,
		.sadb_alg_maxbits = 256
	}
},
{
	.name = "rfc3686(ctr(aes))",

	.uinfo = {
		.encr = {
			.blockbits = 128,
			.defkeybits = 160, /* 128-bit key + 32-bit nonce */
		}
	},

	.pfkey_supported = 1,

	.desc = {
		.sadb_alg_id = SADB_X_EALG_AESCTR,
		.sadb_alg_ivlen	= 8,
		.sadb_alg_minbits = 160,
		.sadb_alg_maxbits = 288
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
	.pfkey_supported = 1,
	.desc = { .sadb_alg_id = SADB_X_CALG_DEFLATE }
},
{
	.name = "lzs",
	.uinfo = {
		.comp = {
			.threshold = 90,
		}
	},
	.pfkey_supported = 1,
	.desc = { .sadb_alg_id = SADB_X_CALG_LZS }
},
{
	.name = "lzjh",
	.uinfo = {
		.comp = {
			.threshold = 50,
		}
	},
	.pfkey_supported = 1,
	.desc = { .sadb_alg_id = SADB_X_CALG_LZJH }
},
};

static inline int aead_entries(void)
{
	return ARRAY_SIZE(aead_list);
}

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

static const struct xfrm_algo_list xfrm_aead_list = {
	.algs = aead_list,
	.entries = ARRAY_SIZE(aead_list),
	.type = CRYPTO_ALG_TYPE_AEAD,
	.mask = CRYPTO_ALG_TYPE_MASK,
};

static const struct xfrm_algo_list xfrm_aalg_list = {
	.algs = aalg_list,
	.entries = ARRAY_SIZE(aalg_list),
	.type = CRYPTO_ALG_TYPE_HASH,
	.mask = CRYPTO_ALG_TYPE_HASH_MASK,
};

static const struct xfrm_algo_list xfrm_ealg_list = {
	.algs = ealg_list,
	.entries = ARRAY_SIZE(ealg_list),
	.type = CRYPTO_ALG_TYPE_BLKCIPHER,
	.mask = CRYPTO_ALG_TYPE_BLKCIPHER_MASK,
};

static const struct xfrm_algo_list xfrm_calg_list = {
	.algs = calg_list,
	.entries = ARRAY_SIZE(calg_list),
	.type = CRYPTO_ALG_TYPE_COMPRESS,
	.mask = CRYPTO_ALG_TYPE_MASK,
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

struct xfrm_algo_desc *xfrm_aalg_get_byname(const char *name, int probe)
{
	return xfrm_find_algo(&xfrm_aalg_list, xfrm_alg_name_match, name,
			      probe);
}
EXPORT_SYMBOL_GPL(xfrm_aalg_get_byname);

struct xfrm_algo_desc *xfrm_ealg_get_byname(const char *name, int probe)
{
	return xfrm_find_algo(&xfrm_ealg_list, xfrm_alg_name_match, name,
			      probe);
}
EXPORT_SYMBOL_GPL(xfrm_ealg_get_byname);

struct xfrm_algo_desc *xfrm_calg_get_byname(const char *name, int probe)
{
	return xfrm_find_algo(&xfrm_calg_list, xfrm_alg_name_match, name,
			      probe);
}
EXPORT_SYMBOL_GPL(xfrm_calg_get_byname);

struct xfrm_aead_name {
	const char *name;
	int icvbits;
};

static int xfrm_aead_name_match(const struct xfrm_algo_desc *entry,
				const void *data)
{
	const struct xfrm_aead_name *aead = data;
	const char *name = aead->name;

	return aead->icvbits == entry->uinfo.aead.icv_truncbits && name &&
	       !strcmp(name, entry->name);
}

struct xfrm_algo_desc *xfrm_aead_get_byname(const char *name, int icv_len, int probe)
{
	struct xfrm_aead_name data = {
		.name = name,
		.icvbits = icv_len,
	};

	return xfrm_find_algo(&xfrm_aead_list, xfrm_aead_name_match, &data,
			      probe);
}
EXPORT_SYMBOL_GPL(xfrm_aead_get_byname);

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
	int i, status;

	BUG_ON(in_softirq());

	for (i = 0; i < aalg_entries(); i++) {
		status = crypto_has_hash(aalg_list[i].name, 0,
					 CRYPTO_ALG_ASYNC);
		if (aalg_list[i].available != status)
			aalg_list[i].available = status;
	}

	for (i = 0; i < ealg_entries(); i++) {
		status = crypto_has_ablkcipher(ealg_list[i].name, 0, 0);
		if (ealg_list[i].available != status)
			ealg_list[i].available = status;
	}

	for (i = 0; i < calg_entries(); i++) {
		status = crypto_has_comp(calg_list[i].name, 0,
					 CRYPTO_ALG_ASYNC);
		if (calg_list[i].available != status)
			calg_list[i].available = status;
	}
}
EXPORT_SYMBOL_GPL(xfrm_probe_algs);

int xfrm_count_pfkey_auth_supported(void)
{
	int i, n;

	for (i = 0, n = 0; i < aalg_entries(); i++)
		if (aalg_list[i].available && aalg_list[i].pfkey_supported)
			n++;
	return n;
}
EXPORT_SYMBOL_GPL(xfrm_count_pfkey_auth_supported);

int xfrm_count_pfkey_enc_supported(void)
{
	int i, n;

	for (i = 0, n = 0; i < ealg_entries(); i++)
		if (ealg_list[i].available && ealg_list[i].pfkey_supported)
			n++;
	return n;
}
EXPORT_SYMBOL_GPL(xfrm_count_pfkey_enc_supported);

MODULE_LICENSE("GPL");
