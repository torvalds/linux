/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _TME_HWKM_MASTER_DEFS_H_
#define _TME_HWKM_MASTER_DEFS_H_

#include <linux/types.h>

#define UINT32_C(x)  (x ## U)

/**
 * Key ID
 */
/* L1 Key IDs that are Key Table slot numbers */
/**< CUS, 512 bits, in fuses */
#define TME_KID_CHIP_UNIQUE_SEED 8
/**< CRBK, 512 bits, in fuses */
#define TME_KID_CHIP_RAND_BASE   9
/**< L1 Key derived from L0 slot numbers 0-3 or 4-7 */
#define TME_KID_CHIP_FAM_L1      10

/* Transport Key ID */
#define TME_KID_TP               11/**< 528 bits, retained */

/**
 * KeyPolicy
 */
/** Key Policy: 64-bit integer with bit encoded values */
struct tme_key_policy {
	uint32_t low;
	uint32_t high;
} __packed;

#define TME_KPHALFBITS                32

#define TME_KPCOMBINE(lo32, hi32) (((uint64_t)(lo32)) | \
		(((uint64_t)(hi32)) << TME_KPHALFBITS))

/**
 * Fields in Key Policy low word
 */

/** Key Type: Fundamental crypto algorithm groups */
/**< Position of Key Type bits */
#define TME_KT_Shift          0
/**< Mask for Key Type bits */
#define TME_KT_Mask           (UINT32_C(0x07) << TME_KT_Shift)
/**< Symmetric algorithms */
#define TME_KT_Symmetric      (UINT32_C(0x00) << TME_KT_Shift)
/**< Asymmetric algorithms: ECC */
#define TME_KT_Asymmetric_ECC (UINT32_C(0x01) << TME_KT_Shift)
/**< Asymmetric algorithms: RSA */
#define TME_KT_Asymmetric_RSA (UINT32_C(0x05) << TME_KT_Shift)

/** Key Length */
/**< Position of Key Length bits */
#define TME_KL_Shift 3
/**< Mask for Key Length bits */
#define TME_KL_Mask  (UINT32_C(0x0F) << TME_KL_Shift)
/**< 64 bits   - AES/2TDES */
#define TME_KL_64    (UINT32_C(0x00) << TME_KL_Shift)
/**< 128 bits  - AES/2TDES */
#define TME_KL_128   (UINT32_C(0x01) << TME_KL_Shift)
/**< 192 bits  - AES/3TDES */
#define TME_KL_192   (UINT32_C(0x02) << TME_KL_Shift)
/**< 224 bits  - ECDSA */
#define TME_KL_224   (UINT32_C(0x03) << TME_KL_Shift)
/**< 256 bits  - ECDSA/AES */
#define TME_KL_256   (UINT32_C(0x04) << TME_KL_Shift)
/**< 384 bits  - ECDSA */
#define TME_KL_384   (UINT32_C(0x05) << TME_KL_Shift)
/**< 448 bits  - ECDSA */
#define TME_KL_448   (UINT32_C(0x06) << TME_KL_Shift)
/**< 512 bits  - ECDSA/HMAC/KDF/AES-SIV/AES-XTS */
#define TME_KL_512   (UINT32_C(0x07) << TME_KL_Shift)
/**< 521 bits  - ECDSA/HMAC/KDF */
#define TME_KL_521   (UINT32_C(0x08) << TME_KL_Shift)
/**< 2048 bits - RSA */
#define TME_KL_2048  (UINT32_C(0x09) << TME_KL_Shift)
/**< 3072 bits - RSA */
#define TME_KL_3072  (UINT32_C(0x0A) << TME_KL_Shift)
/**< 4096 bits - RSA */
#define TME_KL_4096  (UINT32_C(0x0B) << TME_KL_Shift)
/**< 456 bits - Ed448 */
#define TME_KL_456  (UINT32_C(0x0C) << TME_KL_Shift)

/**
 * Key Profile: Only applicable at present
 * if Key Type is #TME_KT_Symmetric
 */
/**< Position of Key Profile bits */
#define TME_KP_Shift         7
/**< Mask for Key Class bits */
#define TME_KP_Mask          (UINT32_C(0x07) << TME_KP_Shift)
/**< If Key Type is #TME_KT_Symmetric */
#define TME_KP_Generic       (UINT32_C(0x00) << TME_KP_Shift)
/**< If Key Type is #TME_KT_Symmetric (aka KDK) */
#define TME_KP_KeyDerivation (UINT32_C(0x01) << TME_KP_Shift)
/**< If Key Type is #TME_KT_Symmetric (aka KWK) */
#define TME_KP_KWK_STORAGE   (UINT32_C(0x02) << TME_KP_Shift)
/**< If Key Type is #TME_KT_Symmetric (aka KSK) */
#define TME_KP_KWK_SESSION   (UINT32_C(0x03) << TME_KP_Shift)
/**< If Key Type is #TME_KT_Symmetric (aka TPK) */
#define TME_KP_KWK_TRANSPORT (UINT32_C(0x04) << TME_KP_Shift)
/**< If Key Type is #TME_KT_Symmetric */
#define TME_KP_KWK_XPORT     (UINT32_C(0x05) << TME_KP_Shift)
/**< If Key Type is not #TME_KT_Symmetric */
#define TME_KP_Unused        (UINT32_C(0x00) << TME_KP_Shift)

/** Key Operation: Crypto operations permitted for a key */
/**< Position of Key Operation bits */
#define TME_KOP_Shift             10
/**< Mask for Key Operation bits */
#define TME_KOP_Mask              (UINT32_C(0x0F) << TME_KOP_Shift)
/**< If Key Type is #TME_KT_Symmetric */
#define TME_KOP_Encryption        (UINT32_C(0x01) << TME_KOP_Shift)
/**< If Key Type is #TME_KT_Symmetric */
#define TME_KOP_Decryption        (UINT32_C(0x02) << TME_KOP_Shift)
/**< If Key Type is #TME_KT_Symmetric */
#define TME_KOP_Digest            (UINT32_C(0x04) << TME_KOP_Shift)
/**< If Key Type is #TME_KT_Symmetric */
#define TME_KOP_CMAC_Sign         (UINT32_C(0x0D) << TME_KOP_Shift)
/**< If Key Type is #TME_KT_Symmetric */
#define TME_KOP_CMAC_Verify       (UINT32_C(0x0E) << TME_KOP_Shift)
/**< If Key Type is #TME_KT_Symmetric */
#define TME_KOP_NISTDerive        (UINT32_C(0x04) << TME_KOP_Shift)
/**< If Key Type is #TME_KT_Symmetric */
#define TME_KOP_HKDFExtract       (UINT32_C(0x08) << TME_KOP_Shift)
/**< If Key Type is #TME_KT_Symmetric */
#define TME_KOP_HKDFExpand        (UINT32_C(0x09) << TME_KOP_Shift)
/**< If Key Type is #TME_KT_Asymmetric_ECC */
#define TME_KOP_ECDSASign         (UINT32_C(0x01) << TME_KOP_Shift)
/**< If Key Type is #TME_KT_Asymmetric_ECC */
#define TME_KOP_ECDHSharedSecret  (UINT32_C(0x02) << TME_KOP_Shift)
/**< If Key Type is #TME_KT_Asymmetric_RSA */
#define TME_KOP_RSAASign          (UINT32_C(0x01) << TME_KOP_Shift)
/**< If Key Type is #TME_KT_Asymmetric_RSA */
#define TME_KOP_RSAAVerify        (UINT32_C(0x02) << TME_KOP_Shift)
/**< If Key Type is #TME_KT_Asymmetric_RSA */
#define TME_KOP_RSAEnc            (UINT32_C(0x04) << TME_KOP_Shift)
/**< If Key Type is #TME_KT_Asymmetric_RSA */
#define TME_KOP_RSADec            (UINT32_C(0x08) << TME_KOP_Shift)
/**< If Key Type is #TME_KT_Asymmetric */
#define TME_KOP_SM2Enc            (UINT32_C(0x04) << TME_KOP_Shift)
/**< If Key Type is #TME_KT_Asymmetric */
#define TME_KOP_SM2Dec            (UINT32_C(0x08) << TME_KOP_Shift)

/** Key Algorithm */
/**< Position of Key Algorithm bits */
#define TME_KAL_Shift             14
/**< Mask for Key Algorithm bits */
#define TME_KAL_Mask              (UINT32_C(0x3F) << TME_KAL_Shift)
/**< If Key Type is #TME_KT_Symmetric */
#define TME_KAL_AES128_ECB        (UINT32_C(0x00) << TME_KAL_Shift)
#define TME_KAL_AES256_ECB        (UINT32_C(0x01) << TME_KAL_Shift)
#define TME_KAL_DES_ECB           (UINT32_C(0x02) << TME_KAL_Shift)
#define TME_KAL_TDES_ECB          (UINT32_C(0x03) << TME_KAL_Shift)
#define TME_KAL_AES128_CBC        (UINT32_C(0x04) << TME_KAL_Shift)
#define TME_KAL_AES256_CBC        (UINT32_C(0x05) << TME_KAL_Shift)
#define TME_KAL_DES_CBC           (UINT32_C(0x06) << TME_KAL_Shift)
#define TME_KAL_TDES_CBC          (UINT32_C(0x07) << TME_KAL_Shift)
#define TME_KAL_AES128_CCM_TC     (UINT32_C(0x08) << TME_KAL_Shift)
#define TME_KAL_AES128_CCM_NTC    (UINT32_C(0x09) << TME_KAL_Shift)
#define TME_KAL_AES256_CCM_TC     (UINT32_C(0x0A) << TME_KAL_Shift)
#define TME_KAL_AES256_CCM_NTC    (UINT32_C(0x0B) << TME_KAL_Shift)
#define TME_KAL_AES256_SIV        (UINT32_C(0x0C) << TME_KAL_Shift)
#define TME_KAL_AES128_CTR        (UINT32_C(0x0D) << TME_KAL_Shift)
#define TME_KAL_AES256_CTR        (UINT32_C(0x0E) << TME_KAL_Shift)
#define TME_KAL_AES128_XTS        (UINT32_C(0x0F) << TME_KAL_Shift)
#define TME_KAL_AES256_XTS        (UINT32_C(0x10) << TME_KAL_Shift)
#define TME_KAL_SHA1_HMAC         (UINT32_C(0x11) << TME_KAL_Shift)
#define TME_KAL_SHA256_HMAC       (UINT32_C(0x12) << TME_KAL_Shift)
#define TME_KAL_AES128_CMAC       (UINT32_C(0x13) << TME_KAL_Shift)
#define TME_KAL_AES256_CMAC       (UINT32_C(0x14) << TME_KAL_Shift)
#define TME_KAL_SHA384_HMAC       (UINT32_C(0x15) << TME_KAL_Shift)
#define TME_KAL_SHA512_HMAC       (UINT32_C(0x16) << TME_KAL_Shift)
#define TME_KAL_AES128_GCM        (UINT32_C(0x17) << TME_KAL_Shift)
#define TME_KAL_AES256_GCM        (UINT32_C(0x18) << TME_KAL_Shift)
#define TME_KAL_KASUMI            (UINT32_C(0x19) << TME_KAL_Shift)
#define TME_KAL_SNOW3G            (UINT32_C(0x1A) << TME_KAL_Shift)
#define TME_KAL_ZUC               (UINT32_C(0x1B) << TME_KAL_Shift)
#define TME_KAL_PRINCE            (UINT32_C(0x1C) << TME_KAL_Shift)
#define TME_KAL_SIPHASH           (UINT32_C(0x1D) << TME_KAL_Shift)
#define TME_KAL_TDES_2KEY_CBC     (UINT32_C(0x1E) << TME_KAL_Shift)
#define TME_KAL_TDES_2KEY_ECB     (UINT32_C(0x1F) << TME_KAL_Shift)
#define TME_KAL_KDF_NIST          (UINT32_C(0x20) << TME_KAL_Shift)
#define TME_KAL_KDF_HKDF          (UINT32_C(0x21) << TME_KAL_Shift)
#define TME_KAL_SHA3224_HMAC      (UINT32_C(0x28) << TME_KAL_Shift)
#define TME_KAL_SHA3256_HMAC      (UINT32_C(0x29) << TME_KAL_Shift)
#define TME_KAL_SHA3384_HMAC      (UINT32_C(0x2A) << TME_KAL_Shift)
#define TME_KAL_SHA3512_HMAC      (UINT32_C(0x2B) << TME_KAL_Shift)
/**< If Key Type is #TME_KT_Asymmetric, Key Subtype is ECC */
#define TME_KAL_ECC_ALGO_ECDSA    (UINT32_C(0x00) << TME_KAL_Shift)
/**< If Key Type is #TME_KT_Asymmetric, Key Subtype is ECC */
#define TME_KAL_ECC_ALGO_ECDH     (UINT32_C(0x01) << TME_KAL_Shift)
/**< If Key Type is #TME_KT_Asymmetric, Key Subtype is ECC */
#define TME_KAL_ECC_ALGO_EdDSA    (UINT32_C(0x02) << TME_KAL_Shift)
/**< If Key Type is #TME_KT_Asymmetric, Key Subtype is ECC */
#define TME_KAL_ECC_ALGO_SM2DSA   (UINT32_C(0x04) << TME_KAL_Shift)
/**< If Key Type is #TME_KT_Asymmetric, Key Subtype is ECC */
#define TME_KAL_ECC_CURVE_NIST    (UINT32_C(0x00) << TME_KAL_Shift)
/**< If Key Type is #TME_KT_Asymmetric, Key Subtype is ECC */
#define TME_KAL_ECC_CURVE_BPOOL   (UINT32_C(0x08) << TME_KAL_Shift)
/**< If Key Type is #TME_KT_Asymmetric, Key Subtype is ECC */
#define TME_KAL_ECC_CURVE_SM2     (UINT32_C(0x10) << TME_KAL_Shift)
/**< If Key Type is #TME_KT_Asymmetric, Key Subtype is ECC */
#define TME_KAL_ECC_CURVE_Ed25519 (UINT32_C(0x18) << TME_KAL_Shift)
/**< If Key Type is #TME_KT_Asymmetric, Key Subtype is RSA */
#define TME_KAL_DSA               (UINT32_C(0x00) << TME_KAL_Shift)
/**< If Key Type is #TME_KT_Asymmetric, Key Subtype is RSA */
#define TME_KAL_DH                (UINT32_C(0x01) << TME_KAL_Shift)

/** Key Security Level */
/**< Position of Key Security Level bits */
#define TME_KSL_Shift             20
/**< Mask for Key Security Level bits */
#define TME_KSL_Mask              (UINT32_C(0x03) << TME_KSL_Shift)
/**< Software Key */
#define TME_KSL_SWKey             (UINT32_C(0x00) << TME_KSL_Shift)
/**< Hardware Managed Key */
#define TME_KSL_ImportKey         (UINT32_C(0x01) << TME_KSL_Shift)
/**< Hardware Key */
#define TME_KSL_HWKey             (UINT32_C(0x02) << TME_KSL_Shift)

/** Key Destination */
/**< Position of Key Destination bits */
#define TME_KD_Shift_V3           22
/**< Mask for Key Destination bits */
#define TME_KD_Mask_V3            (UINT32_C(0x0F) << TME_KD_Shift_V3)
/**< Master */
#define TME_KD_TME_HW_V3          (UINT32_C(0x01) << TME_KD_Shift_V3)
/**< ICE Slave */
#define TME_KD_ICE_V3             (UINT32_C(0x02) << TME_KD_Shift_V3)
/**< GPCE Slave */
#define TME_KD_GPCE_V3            (UINT32_C(0x04) << TME_KD_Shift_V3)
/**< Modem CE Slave */
#define TME_KD_MDM_CE_V3          (UINT32_C(0x08) << TME_KD_Shift_V3)

/** Key Owner */
/**< Position of Key Owner bits */
#define TME_KO_Shift              26
/**< Mask for Key Owner bits */
#define TME_KO_Mask               (UINT32_C(0x0F) << TME_KO_Shift)
/**< TME Hardware */
#define TME_KO_TME_HW             (UINT32_C(0x00) << TME_KO_Shift)
/**< TME Firmware */
#define TME_KO_TME_FW             (UINT32_C(0x01) << TME_KO_Shift)
/**< TZ         (= APPS-S) */
#define TME_KO_TZ                 (UINT32_C(0x02) << TME_KO_Shift)
/**< HLOS / HYP (= APPS-NS) */
#define TME_KO_HLOS_HYP           (UINT32_C(0x03) << TME_KO_Shift)
/**< Modem */
#define TME_KO_MDM                (UINT32_C(0x04) << TME_KO_Shift)
/**< QSM */
#define TME_KO_QSM                (UINT32_C(0x05) << TME_KO_Shift)

/** Key Lineage */
/**< Position of Key Lineage bits */
#define TME_KLI_Shift             30
/**< Mask for Key Lineage bits */
#define TME_KLI_Mask              (UINT32_C(0x03) << TME_KLI_Shift)
/**< Not applicable */
#define TME_KLI_NA                (UINT32_C(0x00) << TME_KLI_Shift)
/**< Not provisioned, chip unique */
#define TME_KLI_NP_CU             (UINT32_C(0x01) << TME_KLI_Shift)
/**< Provisioned, not chip unique */
#define TME_KLI_P_NCU             (UINT32_C(0x02) << TME_KLI_Shift)
/**< Provisioned, chip unique */
#define TME_KLI_P_CU              (UINT32_C(0x03) << TME_KLI_Shift)

/**
 * Fields in Key Policy high word *
 */

/** Key Wrapping Constraints */
/**< Position of Key Attribute bits */
#define TME_KWC_Shift             (33 - TME_KPHALFBITS)
/**< Mask for Key Attribute bits */
#define TME_KWC_Mask              (UINT32_C(0x0F) << TME_KWC_Shift)
/**< Key is wrappable with KWK_EXPORT */
#define TME_KWC_Wrappable_KXP     (UINT32_C(0x01) << TME_KWC_Shift)
/**< Key is wrappable with KWK_STORAGE */
#define TME_KWC_Wrappable_KWK     (UINT32_C(0x02) << TME_KWC_Shift)
/**< Key is wrappable with KWK_TRANSPORT */
#define TME_KWC_Wrappable_KTP     (UINT32_C(0x04) << TME_KWC_Shift)
/**< Key is wrappable with KWK_SESSION */
#define TME_KWC_Wrappable_KSK     (UINT32_C(0x08) << TME_KWC_Shift)

/** Throttling */
/**< Position of Throttling bits */
#define TME_KTH_Shift             (37 - TME_KPHALFBITS)
/**< Mask for Throttling bits */
#define TME_KTH_Mask              (UINT32_C(0x01) << TME_KTH_Shift)
/**< Throttling enabled */
#define TME_KTH_Enabled           (UINT32_C(0x01) << TME_KTH_Shift)

/** Key Destination */
/**< Position of Key Destination bits */
#define TME_KD_Shift_V4           (38 - TME_KPHALFBITS)
/**< Mask for Key Destination bits */
#define TME_KD_Mask_V4            (UINT32_C(0x3F) << TME_KD_Shift_V4)
/**< Master */
#define TME_KD_TME_HW_V4          (UINT32_C(0x01) << TME_KD_Shift_V4)
/**< ICE Slave */
#define TME_KD_ICE_V4             (UINT32_C(0x02) << TME_KD_Shift_V4)
/**< GPCE Slave */
#define TME_KD_GPCE_V4            (UINT32_C(0x04) << TME_KD_Shift_V4)
/**< Modem CE Slave */
#define TME_KD_MDM_CE_V4          (UINT32_C(0x08) << TME_KD_Shift_V4)
/**< TICE Slave */
#define TME_KD_TICE_V4            (UINT32_C(0x10) << TME_KD_Shift_V4)
/**< PCIE Slave */
#define TME_KD_PCIE_V4            (UINT32_C(0x20) << TME_KD_Shift_V4)

/** Key Policy Version */
/**< Position of Key Policy Version bits */
#define TME_KPV_Shift             (44 - TME_KPHALFBITS)
/**< Mask for Key Policy Version bits */
#define TME_KPV_Mask              (UINT32_C(0x0F) << TME_KPV_Shift)
/**< Mask for Key Policy Version bits */
#define TME_KPV_Version           (UINT32_C(0x03) << TME_KPV_Shift)

/** Key Authorised Users */
/**< Position of Authorised User bits */
#define TME_KAU_Shift             (48 - TME_KPHALFBITS)
/**< Mask for Authorised User bits */
#define TME_KAU_Mask              (UINT32_C(0xFFF) << TME_KAU_Shift)
/**< Key usable by TME Hardware */
#define TME_KAU_TME_HW            (UINT32_C(0x01) << TME_KAU_Shift)
/**< Key usable by TME Firmware */
#define TME_KAU_TME_FW            (UINT32_C(0x02) << TME_KAU_Shift)
/**< Key usable by TZ         (= APPS_S) */
#define TME_KAU_TZ                (UINT32_C(0x04) << TME_KAU_Shift)
/**< Key usable by HLOS / HYP (= APPS_NS) */
#define TME_KAU_HLOS_HYP          (UINT32_C(0x08) << TME_KAU_Shift)
/**< Key usable by Modem */
#define TME_KAU_MDM               (UINT32_C(0x10) << TME_KAU_Shift)
/**< Key usable by QSM */
#define TME_KAU_QSM               (UINT32_C(0x40) << TME_KAU_Shift)
/**< Key usable by APPS_NS_VM1 */
#define TME_KAU_APPS_NS_VM1       (UINT32_C(0x108) << TME_KAU_Shift)
/**< Key usable by APPS_NS_VM2 */
#define TME_KAU_APPS_NS_VM2       (UINT32_C(0x208) << TME_KAU_Shift)
/**< Key usable by APPS_NS_VM3 */
#define TME_KAU_APPS_NS_VM3       (UINT32_C(0x408) << TME_KAU_Shift)
/**< Key usable by APPS_NS_VM4 */
#define TME_KAU_APPS_NS_VM4       (UINT32_C(0x808) << TME_KAU_Shift)
/**< Key usable by all EEs */
#define TME_KAU_ALL               TME_KAU_Mask

/**
 * Credentials for throttling
 */
#define TME_CRED_SLOT_ID_NONE     0  /**< No throttling */
#define TME_CRED_SLOT_ID_1        1  /**< Credential slot 1 */
#define TME_CRED_SLOT_ID_2        2  /**< Credential slot 2 */

/**
 * KDFSpec and associated structures
 */
/** Maximum context size that can be sent to the TME, in bytes */
#define TME_KDF_SW_CONTEXT_BYTES_MAX  128
#define TME_KDF_SALT_LABEL_BYTES_MAX   64

/**
 *  Security info to be appended to a KDF context by the Sequencer
 *
 *  These fields allow keys to be tied to specific devices, states,
 *  OEMs, subsystems, etc.
 *  Values are obtained by the Sequencer from hardware, such as
 *  fuses or internal registers.
 */
#define TME_KSC_SOCTestSignState  0x00000001  /**<  (32 bits) */
#define TME_KSC_SOCSecBootState   0x00000002  /**<   (8 bits) */
#define TME_KSC_SOCDebugState     0x00000004  /**<   (8 bits) */
#define TME_KSC_TMELifecycleState 0x00000008  /**<   (8 bits) */
#define TME_KSC_BootStageOTP      0x00000010  /**<   (8 bits) */
#define TME_KSC_SWContext         0x00000020  /**< (variable) */
#define TME_KSC_ChildKeyPolicy    0x00000040  /**<  (64 bits) */
#define TME_KSC_MixingKey         0x00000080  /**<  (key len) */
#define TME_KSC_ChipUniqueID      0x00000100  /**<  (64 bits) */
#define TME_KSC_ChipDeviceNumber  0x00000200  /**<  (32 bits) */
#define TME_KSC_TMEPatchVer       0x00000400  /**< (512 bits) */
#define TME_KSC_SOCPatchVer       0x00000800  /**< (512 bits) */
#define TME_KSC_OEMID             0x00001000  /**<  (16 bits) */
#define TME_KSC_OEMProductID      0x00002000  /**<  (16 bits) */
#define TME_KSC_TMEImgSecVer      0x00004000  /**< (512 bits) */
#define TME_KSC_SOCInitImgSecVer  0x00008000  /**< (512 bits) */
#define TME_KSC_OEMMRCHash        0x00010000  /**< (512 bits) */
#define TME_KSC_OEMProductSeed    0x00020000  /**< (128 bits) */
#define TME_KSC_SeqPatchVer       0x00040000  /**< (512 bits) */
#define TME_KSC_HWMeasurement1    0x00080000  /**< (512 bits) */
#define TME_KSC_HWMeasurement2    0x00100000  /**< (512 bits) */
#define TME_KSC_Reserved          0xFFE00000  /**< RFU */

/** KDF Specification: encompasses both HKDF and NIST KDF algorithms */
struct tme_kdf_spec {
	/* Info common to HKDF and NIST algorithms */
	/**< @c TME_KAL_KDF_HKDF or @c TME_KAL_KDF_NIST */
	uint32_t                kdfalgo;
	/**< IKM for HKDF; IKS for NIST */
	uint32_t                inputkey;
	/**< If @c TME_KSC_MixingKey set in Security Context */
	uint32_t                mixkey;
	/**< If deriving a L3 key */
	uint32_t                l2key;
	/**< Derived key policy */
	struct tme_key_policy   policy;
	/**< Software provided context */
	uint8_t                 swcontext[TME_KDF_SW_CONTEXT_BYTES_MAX];
	/**< Length of @c swContext in bytes */
	uint32_t                swcontextLength;
	/**< Info to be appended to @c swContext */
	uint32_t                security_context;
	/**< Salt for HKDF; Label for NIST */
	uint8_t                 salt_label[TME_KDF_SALT_LABEL_BYTES_MAX];
	/**< Length of @c saltLabel in bytes */
	uint32_t                salt_labelLength;
	/* Additional info specific to HKDF: kdfAlgo == @c KAL_KDF_HKDF */
	/**< PRF Digest algorithm: @c KAL_SHA256_HMAC or @c KAL_SHA512_HMAC */
	uint32_t                prf_digest_algo;
} __packed;

/**
 * WrappedKey and associated structures
 */
/* Maximum wrapped key context size, in bytes */
/**< Cipher Text 68B, MAC 16B, KeyPolicy 8B, Nonce 8B */
#define TME_WK_CONTEXT_BYTES_MAX  100
struct tme_wrapped_key {
	/**< Wrapped key context */
	uint8_t   key[TME_WK_CONTEXT_BYTES_MAX];
	/**< Length of @c key in bytes*/
	uint32_t  length;
} __packed;

/**
 * Plain text Key and associated structures
 */
/* Maximum plain text key size, in bytes */
#define TME_PT_KEY_BYTES_MAX  68

/**
 * Key format for intrinsically word aligned key
 * lengths like 128/256/384/512... bits.
 *
 * Example: 256-bit key integer representation,
 * Key = 0xK31 K30 K29.......K0
 * Byte array, key[] = {0xK31, 0xK30, 0xK29, ...., 0xK0}
 *
 *
 * Key format for non-word aligned key lengths like 521 bits.
 * The key length is rounded off to next word ie, 544 bits.
 *
 * Example: 521-bit key, Key = 0xK65 K64 K63.......K2 K1 K0
 * [bits 1-7 of K0 is expected to be zeros]
 * 544 bit integer representation, Key = 0xK65 K64 K63.......K2 K1 K0 00 00
 * Byte array, key[] = {0xK65, 0xK64, 0xK63, ...., 0xK2, 0xK1, 0xK0, 0x00, 0x00}
 *
 */
struct tme_plaintext_key {
	/**< Plain text key */
	uint8_t   key[TME_PT_KEY_BYTES_MAX];
	/**< Length of @c key in bytes */
	uint32_t  length;
} __packed;

/**
 * Extended Error Information structure
 */
struct tme_ext_err_info {
	/* TME FW */
	/**< TME FW Response status. */
	uint32_t  tme_err_status;

	/* SEQ FW */
	/**< Contents of CSR_CMD_ERROR_STATUS */
	uint32_t  seq_err_status;

	/* SEQ HW Key Policy */
	/**< CRYPTO_ENGINE_CRYPTO_KEY_POLICY_ERROR_STATUS0 */
	uint32_t  seq_kp_err_status0;
	/**< CRYPTO_ENGINE_CRYPTO_KEY_POLICY_ERROR_STATUS1 */
	uint32_t  seq_kp_err_status1;

	/**
	 * Debug information: log/print this information
	 * if any of the above fields is non-zero
	 */
	/**< Contents of CSR_CMD_RESPONSE_STATUS */
	uint32_t  seq_rsp_status;
} __packed;

#endif /* _TME_HWKM_MASTER_DEFS_H_ */

