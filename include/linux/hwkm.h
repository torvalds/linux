/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __HWKM_H_
#define __HWKM_H_

#include <linux/types.h>
#include <linux/tme_hwkm_master_defs.h>
#include <linux/crypto-qti-common.h>

#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER)
/* Maximum number of bytes in a key used in a KEY_SLOT_RDWR operation */
#define HWKM_MAX_KEY_SIZE TME_PT_KEY_BYTES_MAX
/* Maximum number of bytes in a SW ctx used in a SYSTEM_KDF operation */
#define HWKM_MAX_CTX_SIZE TME_KDF_SW_CONTEXT_BYTES_MAX
/* Maximum number of bytes in a WKB used in a key wrap or unwrap operation */
#define HWKM_MAX_BLOB_SIZE TME_WK_CONTEXT_BYTES_MAX

#define HWKM_TPKEY_SLOT_MASTER TME_KID_TP
#define HWKM_TPKEY_SLOT_ICE		0x8C

#define HWKM_EXPECTED_UNWRAP_KEY_SIZE 100
#endif /* CONFIG_QTI_HW_KEY_MANAGER */

#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER_V1)
/* Maximum number of bytes in a key used in a KEY_SLOT_RDWR operation */
#define HWKM_MAX_KEY_SIZE 32
/* Maximum number of bytes in a SW ctx used in a SYSTEM_KDF operation */
#define HWKM_MAX_CTX_SIZE 64
/* Maximum number of bytes in a WKB used in a key wrap or unwrap operation */
#define HWKM_MAX_BLOB_SIZE 68
#endif /* CONFIG_QTI_HW_KEY_MANAGER_V1 */

/* Opcodes to be set in the op field of a command */
enum hwkm_op {
	/* Opcode to generate a random key */
	NIST_KEYGEN = 0,
	/* Opcode to derive a key */
	SYSTEM_KDF,
	/* Used only by HW */
	QFPROM_KEY_RDWR,
	/* Opcode to wrap a key and export the wrapped key */
	KEY_WRAP_EXPORT,
	/*
	 * Opcode to import a wrapped key and unwrap it in the
	 * specified key slot
	 */
	KEY_UNWRAP_IMPORT,
	/* Opcode to clear a slot */
	KEY_SLOT_CLEAR,
	/* Opcode to read or write a key from/to a slot */
	KEY_SLOT_RDWR,
	/*
	 * Opcode to broadcast a TPKEY to all slaves configured
	 * to receive a TPKEY.
	 */
	SET_TPKEY,


	HWKM_MAX_OP,
	HWKM_UNDEF_OP = 0xFF
};

/*
 * Algorithm values which can be used in the alg_allowed field of the
 * key policy.
 */
#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER)
enum hwkm_alg {
	/* Symmetric Algorithms */
	AES128_ECB = TME_KT_Symmetric | TME_KAL_AES128_ECB | TME_KL_128,
	AES256_ECB = TME_KT_Symmetric | TME_KAL_AES256_ECB | TME_KL_256,
	DES_ECB = TME_KT_Symmetric | TME_KAL_DES_ECB | TME_KL_64,
	TDES_ECB = TME_KT_Symmetric | TME_KAL_TDES_ECB | TME_KL_192,
	AES128_CBC = TME_KT_Symmetric | TME_KAL_AES128_CBC | TME_KL_128,
	AES256_CBC = TME_KT_Symmetric | TME_KAL_AES256_CBC | TME_KL_256,
	DES_CBC = TME_KT_Symmetric | TME_KAL_DES_CBC | TME_KL_64,
	TDES_CBC = TME_KT_Symmetric | TME_KAL_TDES_CBC | TME_KL_192,
	AES128_CCM_TC = TME_KT_Symmetric | TME_KAL_AES128_CCM_TC | TME_KL_128,
	AES128_CCM_NTC = TME_KT_Symmetric | TME_KAL_AES128_CCM_NTC | TME_KL_128,
	AES256_CCM_TC = TME_KT_Symmetric | TME_KAL_AES256_CCM_TC | TME_KL_256,
	AES256_CCM_NTC = TME_KT_Symmetric | TME_KAL_AES256_CCM_NTC | TME_KL_256,
	AES256_SIV = TME_KT_Symmetric | TME_KAL_AES256_SIV | TME_KL_512,
	AES128_CTR = TME_KT_Symmetric | TME_KAL_AES128_CTR | TME_KL_128,
	AES256_CTR = TME_KT_Symmetric | TME_KAL_AES256_CTR | TME_KL_256,
	AES128_XTS = TME_KT_Symmetric | TME_KAL_AES128_XTS | TME_KL_512,
	AES256_XTS = TME_KT_Symmetric | TME_KAL_AES256_XTS | TME_KL_512,
	SHA1_HMAC = TME_KT_Symmetric | TME_KAL_SHA1_HMAC | TME_KL_512,
	SHA256_HMAC = TME_KT_Symmetric | TME_KAL_SHA256_HMAC | TME_KL_512,
	AES128_CMAC = TME_KT_Symmetric | TME_KAL_AES128_CMAC | TME_KL_128,
	AES256_CMAC = TME_KT_Symmetric | TME_KAL_AES256_CMAC | TME_KL_256,
	SHA384_HMAC = TME_KT_Symmetric | TME_KAL_SHA384_HMAC | TME_KL_512,
	SHA512_HMAC = TME_KT_Symmetric | TME_KAL_SHA512_HMAC | TME_KL_512,
	AES128_GCM = TME_KT_Symmetric | TME_KAL_AES128_GCM | TME_KL_128,
	AES256_GCM = TME_KT_Symmetric | TME_KAL_AES256_GCM | TME_KL_256,

	// TODO: Verify Key Lengths for these algorithms
	KASUMI = TME_KT_Symmetric | TME_KAL_KASUMI | TME_KL_128,
	SNOW3G = TME_KT_Symmetric | TME_KAL_SNOW3G | TME_KL_128,
	ZUC = TME_KT_Symmetric | TME_KAL_ZUC | TME_KL_128,
	PRINCE = TME_KT_Symmetric | TME_KAL_PRINCE | TME_KL_128,
	SIPHASH = TME_KT_Symmetric | TME_KAL_SIPHASH | TME_KL_128,

	KDF_NIST = TME_KT_Symmetric | TME_KAL_KDF_NIST | TME_KL_512,
	KDF_HKDF = TME_KT_Symmetric | TME_KAL_KDF_HKDF,

	/* Asymmetric Algorithms */
	ECDSA_P224_NIST = TME_KT_Asymmetric_ECC | TME_KAL_ECC_ALGO_ECDSA | TME_KL_224 |
						TME_KAL_ECC_CURVE_NIST,
	ECDSA_P256_NIST = TME_KT_Asymmetric_ECC | TME_KAL_ECC_ALGO_ECDSA | TME_KL_256 |
						TME_KAL_ECC_CURVE_NIST,
	ECDSA_P384_NIST = TME_KT_Asymmetric_ECC | TME_KAL_ECC_ALGO_ECDSA | TME_KL_384 |
						TME_KAL_ECC_CURVE_NIST,
	ECDSA_P521_NIST = TME_KT_Asymmetric_ECC | TME_KAL_ECC_ALGO_ECDSA | TME_KL_521 |
						TME_KAL_ECC_CURVE_NIST,
	ECDSA_P224_BP = TME_KT_Asymmetric_ECC | TME_KAL_ECC_ALGO_ECDSA | TME_KL_224 |
						TME_KAL_ECC_CURVE_BPOOL,
	ECDSA_P256_BP = TME_KT_Asymmetric_ECC | TME_KAL_ECC_ALGO_ECDSA | TME_KL_256 |
						TME_KAL_ECC_CURVE_BPOOL,
	ECDSA_P384_BP = TME_KT_Asymmetric_ECC | TME_KAL_ECC_ALGO_ECDSA | TME_KL_384 |
						TME_KAL_ECC_CURVE_BPOOL,
	ECDSA_P512_BP = TME_KT_Asymmetric_ECC | TME_KAL_ECC_ALGO_ECDSA | TME_KL_512 |
						TME_KAL_ECC_CURVE_BPOOL,
	ECDH_P224_NIST = TME_KT_Asymmetric_ECC | TME_KAL_ECC_ALGO_ECDH | TME_KL_224 |
						TME_KAL_ECC_CURVE_NIST,
	ECDH_P256_NIST = TME_KT_Asymmetric_ECC | TME_KAL_ECC_ALGO_ECDH | TME_KL_256 |
						TME_KAL_ECC_CURVE_NIST,
	ECDH_P384_NIST = TME_KT_Asymmetric_ECC | TME_KAL_ECC_ALGO_ECDH | TME_KL_384 |
						TME_KAL_ECC_CURVE_NIST,
	ECDH_P521_NIST = TME_KT_Asymmetric_ECC | TME_KAL_ECC_ALGO_ECDH | TME_KL_521 |
						TME_KAL_ECC_CURVE_NIST,
	ECDH_P224_BP = TME_KT_Asymmetric_ECC | TME_KAL_ECC_ALGO_ECDH | TME_KL_224 |
						TME_KAL_ECC_CURVE_BPOOL,
	ECDH_P256_BP = TME_KT_Asymmetric_ECC | TME_KAL_ECC_ALGO_ECDH | TME_KL_256 |
						TME_KAL_ECC_CURVE_BPOOL,
	ECDH_P384_BP = TME_KT_Asymmetric_ECC | TME_KAL_ECC_ALGO_ECDH | TME_KL_384 |
						TME_KAL_ECC_CURVE_BPOOL,
	ECDH_P512_BP = TME_KT_Asymmetric_ECC | TME_KAL_ECC_ALGO_ECDH | TME_KL_512 |
						TME_KAL_ECC_CURVE_BPOOL,

	HWKM_UNDEF_ALG = 0xFFFFFFFF
};

/* Key type values which can be used in the key_type field of the key policy */
enum hwkm_type {
	KEY_DERIVATION_KEY = TME_KP_KeyDerivation,
	KEY_WRAPPING_KEY = TME_KP_KWK_STORAGE,
	KEY_SWAPPING_KEY = TME_KP_KWK_SESSION,
	TRANSPORT_KEY = TME_KP_KWK_TRANSPORT,
	GENERIC_KEY = TME_KP_Generic,
	EXPORT_KEY = TME_KP_KWK_XPORT,

	HWKM_UNDEF_KEY_TYPE = 0xFFFFFFFF
};

// TODO: Handle v3 vs v4 gracefully
/* Destinations which a context can use */
enum hwkm_destination {
	KM_MASTER = TME_KD_TME_HW_V4,
	GPCE_SLAVE = TME_KD_GPCE_V4,
	MCE_SLAVE = TME_KD_MDM_CE_V4,
	ICE_SLAVE = TME_KD_ICE_V4,
	ICEMEM_SLAVE = 10,

	HWKM_UNDEF_DESTINATION = 0xFFFFFFFF
};

/*
 * Key security levels which can be set in the security_lvl field of
 * key policy.
 */
enum hwkm_security_level {
	/* Can be read by SW in plaintext using KEY_SLOT_RDWR cmd. */
	SW_KEY = TME_KSL_SWKey,
	/* Imported key managed by HW. */
	MANAGED_KEY = TME_KSL_ImportKey,
	/* Key only known to HW. */
	HW_KEY = TME_KSL_HWKey,

	HWKM_UNDEF_SECURITY_LEVEL = 0xFFFFFFFF
};

enum hwkm_key_lineage {
	KEY_LINEAGE_NA = TME_KLI_NA,
	KEY_LINEAGE_NOT_PROVISIONED_UNIQUE = TME_KLI_NP_CU,
	KEY_LINEAGE_NOT_PROVISIONED_NOT_UNIQUE = TME_KLI_P_NCU,
	KEY_LINEAGE_PROVISIONED_UNIQUE = TME_KLI_P_CU,

	HWKM_UNDEF_KEY_LINEAGE = 0xFFFFFFFF
};

#define HWKM_CRED_SLOT_NONE TME_CRED_SLOT_ID_NONE
#define HWKM_CRED_SLOT_1 TME_CRED_SLOT_ID_1
#define HWKM_CRED_SLOT_2 TME_CRED_SLOT_ID_2

/** Slots 18-25 are reserved for use by TZ in the TME key table */
enum hwkm_master_key_slots {
	/** L2 KDKs, used to derive keys by SW. Cannot be used for crypto, only key derivation */
	TZ_NKDK_L2 = TME_KID_CHIP_FAM_L1,
	TZ_PKDK_L2 = TME_KID_CHIP_UNIQUE_SEED,
	TZ_SKDK_L2 = TME_KID_CHIP_UNIQUE_SEED,
	TZ_UKDK_L2 = TME_KID_CHIP_RAND_BASE,

	/** Slots reserved for TPKEY */
	TPKEY_SLOT = TME_KID_TP,

	/** Slots reserved for Swap key */
	TZ_SWAP_KEY_SLOT = 18,

	/** Reserved for wrapping keys to persist or unwrap keys */
	TZ_WRAP_KEY_SLOT = 19,

	/** Reserved for intermediate operations in IHWKeyFactory */
	TZ_GENERAL_PURPOSE_SLOT1 = 20,
	TZ_GENERAL_PURPOSE_SLOT2 = 21,

	/** Reserved for mixing keys in KDF */
	TZ_MIXING_KEY_SLOT = 22,

	/** Used for asymmetric operations */
	TZ_ASYMMETRIC_OPERATION_SLOT = 23,

	/**
	 * Reserved for privileged use cases which need to persist a key
	 * and share it between execution environments.
	 *
	 * WARNING: Modifying these values may cause issues in execution
	 * environments which depend on these specific slots being used for
	 * privileged persistent use cases.
	 */
	PERSISTENT_SHARED_SLOT_PAIR1 = 24,
	PERSISTENT_SHARED_SLOT_PAIR2 = 25,

	MASTER_SLOT_MAX,

	UNDEF_SLOT = 0xFF
};

struct hwkm_key_policy_v2_extension {
	bool expand_allowed;
	bool extract_allowed;
	enum hwkm_key_lineage lineage;
	u32 credential_slot;
	bool export_key_wrap_allowed;
};
#endif

#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER_V1)
enum hwkm_alg {
	AES128_ECB = 0,
	AES256_ECB = 1,
	DES_ECB = 2,
	TDES_ECB = 3,
	AES128_CBC = 4,
	AES256_CBC = 5,
	DES_CBC = 6,
	TDES_CBC = 7,
	AES128_CCM_TC = 8,
	AES128_CCM_NTC = 9,
	AES256_CCM_TC = 10,
	AES256_CCM_NTC = 11,
	AES256_SIV = 12,
	AES128_CTR = 13,
	AES256_CTR = 14,
	AES128_XTS = 15,
	AES256_XTS = 16,
	SHA1_HMAC = 17,
	SHA256_HMAC = 18,
	AES128_CMAC = 19,
	AES256_CMAC = 20,
	SHA384_HMAC = 21,
	SHA512_HMAC = 22,
	AES128_GCM = 23,
	AES256_GCM = 24,
	KASUMI = 25,
	SNOW3G = 26,
	ZUC = 27,
	PRINCE = 28,
	SIPHASH = 29,
	QARMA64 = 30,
	QARMA128 = 31,

	HWKM_ALG_MAX,

	HWKM_UNDEF_ALG = 0xFF
};

enum hwkm_type {
	KEY_DERIVATION_KEY = 0,
	KEY_WRAPPING_KEY = 1,
	KEY_SWAPPING_KEY = 2,
	TRANSPORT_KEY = 3,
	GENERIC_KEY = 4,

	HWKM_TYPE_MAX,

	HWKM_UNDEF_KEY_TYPE = 0xFF
};

/* Destinations which a context can use */
enum hwkm_destination {
	KM_MASTER = 0,
	GPCE_SLAVE = 1,
	MCE_SLAVE = 2,
	PIMEM_SLAVE = 3,
	ICE0_SLAVE = 4,
	ICE1_SLAVE = 5,
	ICE2_SLAVE = 6,
	ICE3_SLAVE = 7,
	DP0_HDCP_SLAVE = 8,
	DP1_HDCP_SLAVE = 9,
	ICEMEM_SLAVE = 10,

	HWKM_DESTINATION_MAX,

	HWKM_UNDEF_DESTINATION = 0xFF
};

enum hwkm_security_level {
	/* Can be read by SW in plaintext using KEY_SLOT_RDWR cmd. */
	SW_KEY = 0,
	/* Usable by SW, but not readable in plaintext. */
	MANAGED_KEY = 1,
	/* Not usable by SW. */
	HW_KEY = 2,

	HWKM_SECURITY_LEVEL_MAX,

	HWKM_UNDEF_SECURITY_LEVEL = 0xFF
};

enum hwkm_master_key_slots {
	/** L1 KDKs. Not usable by SW. Used by HW to derive L2 KDKs */
	NKDK_L1 = 0,
	PKDK_L1 = 1,
	SKDK_L1 = 2,
	UKDK_L1 = 3,

	/*
	 * L2 KDKs, used to derive keys by SW.
	 * Cannot be used for crypto, only key derivation
	 */
	TZ_NKDK_L2 = 4,
	TZ_PKDK_L2 = 5,
	TZ_SKDK_L2 = 6,
	MODEM_PKDK_L2 = 7,
	MODEM_SKDK_L2 = 8,
	TZ_UKDK_L2 = 9,

	/** Slots reserved for TPKEY */
	TPKEY_EVEN_SLOT = 10,
	TPKEY_KEY_ODD_SLOT = 11,

	/** First key slot available for general purpose use cases */
	MASTER_GENERIC_SLOTS_START,

	UNDEF_SLOT = 0xFF
};

#endif

struct hwkm_key_policy {
	bool km_by_spu_allowed;
	bool km_by_modem_allowed;
	bool km_by_nsec_allowed;
	bool km_by_tz_allowed;

	enum hwkm_alg alg_allowed;

	bool enc_allowed;
	bool dec_allowed;

	enum hwkm_type key_type;
	u8 kdf_depth;

	bool wrap_export_allowed;
	bool swap_export_allowed;

	enum hwkm_security_level security_lvl;

	enum hwkm_destination hw_destination;

	bool wrap_with_tpk_allowed;
#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER)
	struct hwkm_key_policy_v2_extension v2;
#endif
};

struct hwkm_bsve {
	bool enabled;
	bool km_key_policy_ver_en;
	bool km_apps_secure_en;
	bool km_msa_secure_en;
	bool km_lcm_fuse_en;
	bool km_boot_stage_otp_en;
	bool km_swc_en;
	bool km_child_key_policy_en;
	bool km_mks_en;
	u64 km_fuse_region_sha_digest_en;
#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER)
	bool km_oem_id_en;
	bool km_pkhash_en;
	bool km_oem_product_id_en;
	bool km_oem_product_seed_en;
#endif
};

struct hwkm_keygen_cmd {
	u8 dks;					/* Destination Key Slot */
	struct hwkm_key_policy policy;		/* Key policy */
};

struct hwkm_rdwr_cmd {
	uint8_t slot;			/* Key Slot */
	bool is_write;			/* Write or read op */
	struct hwkm_key_policy policy;	/* Key policy for write */
	uint8_t key[HWKM_MAX_KEY_SIZE];	/* Key for write */
	size_t sz;			/* Length of key in bytes */
};

struct hwkm_kdf_cmd {
	uint8_t dks;			/* Destination Key Slot */
	uint8_t kdk;			/* Key Derivation Key Slot */
	uint8_t mks;			/* Mixing key slot (bsve controlled) */
	struct hwkm_key_policy policy;	/* Key policy. */
	struct hwkm_bsve bsve;		/* Binding state vector */
	uint8_t ctx[HWKM_MAX_CTX_SIZE];	/* Context */
	size_t sz;			/* Length of context in bytes */
	enum hwkm_alg parent_alg;	/* Underlying KDF algorithm (required for TME) */
};

struct hwkm_set_tpkey_cmd {
	uint8_t sks;			/* The slot to use as the TPKEY */
};

struct hwkm_unwrap_cmd {
	uint8_t dks;			/* Destination Key Slot */
	uint8_t kwk;			/* Key Wrapping Key Slot */
	uint8_t wkb[HWKM_MAX_BLOB_SIZE];/* Wrapped Key Blob */
	uint8_t sz;			/* Length of WKB in bytes */
};

struct hwkm_wrap_cmd {
	uint8_t sks;			/* Destination Key Slot */
	uint8_t kwk;			/* Key Wrapping Key Slot */
	struct hwkm_bsve bsve;		/* Binding state vector */
};

struct hwkm_clear_cmd {
	uint8_t dks;			/* Destination key slot */
	bool is_double_key;		/* Whether this is a double key */
};


struct hwkm_cmd {
	enum hwkm_op op;		/* Operation */
#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER)
	enum hwkm_destination dest;
#endif
	union /* Structs with opcode specific parameters */
	{
		struct hwkm_keygen_cmd keygen;
		struct hwkm_rdwr_cmd rdwr;
		struct hwkm_kdf_cmd kdf;
		struct hwkm_set_tpkey_cmd set_tpkey;
		struct hwkm_unwrap_cmd unwrap;
		struct hwkm_wrap_cmd wrap;
		struct hwkm_clear_cmd clear;
	};
};

struct hwkm_rdwr_rsp {
	struct hwkm_key_policy policy;	/* Key policy for read */
	uint8_t key[HWKM_MAX_KEY_SIZE];	/* Only available for read op */
	size_t sz;			/* Length of the key (bytes) */
};

struct hwkm_wrap_rsp {
	uint8_t wkb[HWKM_MAX_BLOB_SIZE];	/* Wrapping key blob */
	size_t sz;				/* key blob len (bytes) */
};

struct hwkm_rsp {
	u32 status;
	union /* Structs with opcode specific outputs */
	{
		struct hwkm_rdwr_rsp rdwr;
		struct hwkm_wrap_rsp wrap;
	};
};

#if (IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER) || IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER_V1))
int qti_hwkm_handle_cmd(struct hwkm_cmd *cmd, struct hwkm_rsp *rsp);
int qti_hwkm_clocks(bool on);
int qti_hwkm_init(const struct ice_mmio_data *mmio_data);
#else
static inline int qti_hwkm_add_req(struct hwkm_cmd *cmd,
				   struct hwkm_rsp *rsp)
{
	return -EOPNOTSUPP;
}
static inline int qti_hwkm_clocks(bool on)
{
	return -EOPNOTSUPP;
}
static inline int qti_hwkm_init(const struct ice_mmio_data *mmio_data)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_QTI_HW_KEY_MANAGER */
#endif /* __HWKM_H_ */
