/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Firmware layer for XilSECURE APIs.
 *
 * Copyright (C) 2014-2022 Xilinx, Inc.
 * Copyright (C) 2022-2025 Advanced Micro Devices, Inc.
 */

#ifndef __FIRMWARE_XLNX_ZYNQMP_CRYPTO_H__
#define __FIRMWARE_XLNX_ZYNQMP_CRYPTO_H__

/**
 * struct xlnx_feature - Feature data
 * @family:	Family code of platform
 * @subfamily:	Subfamily code of platform
 * @feature_id:	Feature id of module
 * @data:	Collection of all supported platform data
 */
struct xlnx_feature {
	u32 family;
	u32 feature_id;
	void *data;
};

/* xilSecure API commands module id + api id */
#define XSECURE_API_AES_INIT		0x509
#define XSECURE_API_AES_OP_INIT		0x50a
#define XSECURE_API_AES_UPDATE_AAD	0x50b
#define XSECURE_API_AES_ENCRYPT_UPDATE	0x50c
#define XSECURE_API_AES_ENCRYPT_FINAL	0x50d
#define XSECURE_API_AES_DECRYPT_UPDATE	0x50e
#define XSECURE_API_AES_DECRYPT_FINAL	0x50f
#define XSECURE_API_AES_KEY_ZERO	0x510
#define XSECURE_API_AES_WRITE_KEY	0x511

#if IS_REACHABLE(CONFIG_ZYNQMP_FIRMWARE)
int zynqmp_pm_aes_engine(const u64 address, u32 *out);
int zynqmp_pm_sha_hash(const u64 address, const u32 size, const u32 flags);
void *xlnx_get_crypto_dev_data(struct xlnx_feature *feature_map);
int versal_pm_aes_key_write(const u32 keylen,
			    const u32 keysrc, const u64 keyaddr);
int versal_pm_aes_key_zero(const u32 keysrc);
int versal_pm_aes_op_init(const u64 hw_req);
int versal_pm_aes_update_aad(const u64 aad_addr, const u32 aad_len);
int versal_pm_aes_enc_update(const u64 in_params, const u64 in_addr);
int versal_pm_aes_dec_update(const u64 in_params, const u64 in_addr);
int versal_pm_aes_dec_final(const u64 gcm_addr);
int versal_pm_aes_enc_final(const u64 gcm_addr);
int versal_pm_aes_init(void);

#else
static inline int zynqmp_pm_aes_engine(const u64 address, u32 *out)
{
	return -ENODEV;
}

static inline int zynqmp_pm_sha_hash(const u64 address, const u32 size,
				     const u32 flags)
{
	return -ENODEV;
}

static inline void *xlnx_get_crypto_dev_data(struct xlnx_feature *feature_map)
{
	return ERR_PTR(-ENODEV);
}

static inline int versal_pm_aes_key_write(const u32 keylen,
					  const u32 keysrc, const u64 keyaddr)
{
	return -ENODEV;
}

static inline int versal_pm_aes_key_zero(const u32 keysrc)
{
	return -ENODEV;
}

static inline int versal_pm_aes_op_init(const u64 hw_req)
{
	return -ENODEV;
}

static inline int versal_pm_aes_update_aad(const u64 aad_addr,
					   const u32 aad_len)
{
	return -ENODEV;
}

static inline int versal_pm_aes_enc_update(const u64 in_params,
					   const u64 in_addr)
{
	return -ENODEV;
}

static inline int versal_pm_aes_dec_update(const u64 in_params,
					   const u64 in_addr)
{
	return -ENODEV;
}

static inline int versal_pm_aes_enc_final(const u64 gcm_addr)
{
	return -ENODEV;
}

static inline int versal_pm_aes_dec_final(const u64 gcm_addr)
{
	return -ENODEV;
}

static inline int versal_pm_aes_init(void)
{
	return -ENODEV;
}

#endif

#endif /* __FIRMWARE_XLNX_ZYNQMP_CRYPTO_H__ */
