/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _TME_HWKM_MASTER_H_
#define _TME_HWKM_MASTER_H_

#include <linux/tme_hwkm_master_defs.h>

/**
 * API functions
 */

/**
 *  Clear a Key Table entry.
 *
 *  @param  [in]   key_id      The ID of the key to clear.
 *  @param  [out]  err_info    Extended error info
 *
 *  @return  0 if successful, error code otherwise.
 */
uint32_t tme_hwkm_master_clearkey(uint32_t key_id,
		struct tme_ext_err_info *err_info);

/**
 *  Generate a random key with an associated policy.
 *
 *  @param  [in]   key_id      The ID of the key to be generated.
 *  @param  [in]   policy      The policy specifying the key to be generated.
 *  @param  [in]   cred_slot   Credential slot to which this key will be bound.
 *  @param  [out]  err_info    Extended error info
 *
 *  @return  0 if successful, error code otherwise.
 */
uint32_t tme_hwkm_master_generatekey(uint32_t key_id,
		struct tme_key_policy *policy,
		uint32_t cred_slot,
		struct tme_ext_err_info *err_info);

/**
 *  Derive a KEY using either HKDF or NIST algorithms.
 *
 *  @param  [in]   key_id     The ID of the key to be derived.
 *  @param  [in]   kdf_info   Specifies how the key is to be derived
 *                            and the properties of the derived key.
 *  @param  [in]   cred_slot  Credential slot to which this key will be bound.
 *  @param  [out]  err_info    Extended error info
 *
 *  @return  0 if successful, error code otherwise.
 */
uint32_t tme_hwkm_master_derivekey(uint32_t key_id,
		struct tme_kdf_spec *kdf_info,
		uint32_t cred_slot,
		struct tme_ext_err_info *err_info);

/**
 *  Wrap a key so that it can be safely moved outside the TME.
 *
 *  @param  [in]   kwkey_id     Denotes a key, already present in the
 *                              Key Table, to be used to secure the target key.
 *  @param  [in]   targetkey_id Denotes the key to be wrapped.
 *  @param  [in]   cred_slot    Credential slot to which this key is bound.
 *  @param  [out]  wrapped      Buffer for wrapped key output from response
 *  @param  [out]  err_info     Extended error info
 *
 *  @return  0 if successful, error code otherwise.
 */
uint32_t tme_hwkm_master_wrapkey(uint32_t key_id,
		uint32_t targetkey_id,
		uint32_t cred_slot,
		struct tme_wrapped_key *wrapped,
		struct tme_ext_err_info *err_info);

/**
 *  Unwrap a key from outside the TME and store in the Key Table.
 *
 *  @param  [in]   key_id      The ID of the key to be unwrapped.
 *  @param  [in]   kwkey_id    Denotes a key, already present in the
 *                             Key Table, to be used to unwrap the key.
 *  @param  [in]   cred_slot   Credential slot to which this key will be bound.
 *  @param  [in]   wrapped     The key to be unwrapped.
 *  @param  [out]  err_info    Extended error info
 *
 *  @return  0 if successful, error code otherwise.
 */
uint32_t tme_hwkm_master_unwrapkey(uint32_t key_id,
		uint32_t kwkey_id,
		uint32_t cred_slot,
		struct tme_wrapped_key *wrapped,
		struct tme_ext_err_info *err_info);

/**
 *  Import a plaintext key from outside the TME and store in the Key Table.
 *
 *  @param  [in]   key_id      The ID of the key to be imported.
 *  @param  [in]   policy      The Key Policy to be associated with the key.
 *  @param  [in]   keyMaterial The plaintext key material.
 *  @param  [in]   cred_slot   Credential slot to which this key will be bound.
 *  @param  [out]  err_info    Extended error info
 *
 *  @return  0 if successful, error code otherwise.
 */
uint32_t tme_hwkm_master_importkey(uint32_t key_id,
		struct tme_key_policy *policy,
		struct tme_plaintext_key *key_material,
		uint32_t cred_slot,
		struct tme_ext_err_info *err_info);

/**
 *  Broadcast Transport Key to HWKM slaves.
 *
 *  @param  [out]  err_info    Extended error info
 *
 *  @return  0 if successful, error code otherwise.
 */
uint32_t tme_hwkm_master_broadcast_transportkey(
		struct tme_ext_err_info *err_info);

#endif /* _TME_HWKM_MASTER_H_ */
