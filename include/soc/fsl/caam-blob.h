/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Pengutronix, Ahmad Fatoum <kernel@pengutronix.de>
 */

#ifndef __CAAM_BLOB_GEN
#define __CAAM_BLOB_GEN

#include <linux/types.h>
#include <linux/errno.h>

#define CAAM_BLOB_KEYMOD_LENGTH		16
#define CAAM_BLOB_OVERHEAD		(32 + 16)
#define CAAM_BLOB_MAX_LEN		4096

struct caam_blob_priv;

/**
 * struct caam_blob_info - information for CAAM blobbing
 * @input:       pointer to input buffer (must be DMAable)
 * @input_len:   length of @input buffer in bytes.
 * @output:      pointer to output buffer (must be DMAable)
 * @output_len:  length of @output buffer in bytes.
 * @key_mod:     key modifier
 * @key_mod_len: length of @key_mod in bytes.
 *	         May not exceed %CAAM_BLOB_KEYMOD_LENGTH
 */
struct caam_blob_info {
	void *input;
	size_t input_len;

	void *output;
	size_t output_len;

	const void *key_mod;
	size_t key_mod_len;
};

/**
 * caam_blob_gen_init - initialize blob generation
 * Return: pointer to new &struct caam_blob_priv instance on success
 * and ``ERR_PTR(-ENODEV)`` if CAAM has no hardware blobbing support
 * or no job ring could be allocated.
 */
struct caam_blob_priv *caam_blob_gen_init(void);

/**
 * caam_blob_gen_exit - free blob generation resources
 * @priv: instance returned by caam_blob_gen_init()
 */
void caam_blob_gen_exit(struct caam_blob_priv *priv);

/**
 * caam_process_blob - encapsulate or decapsulate blob
 * @priv:   instance returned by caam_blob_gen_init()
 * @info:   pointer to blobbing info describing key, blob and
 *          key modifier buffers.
 * @encap:  true for encapsulation, false for decapsulation
 *
 * Return: %0 and sets ``info->output_len`` on success and a negative
 * error code otherwise.
 */
int caam_process_blob(struct caam_blob_priv *priv,
		      struct caam_blob_info *info, bool encap);

/**
 * caam_encap_blob - encapsulate blob
 * @priv:   instance returned by caam_blob_gen_init()
 * @info:   pointer to blobbing info describing input key,
 *          output blob and key modifier buffers.
 *
 * Return: %0 and sets ``info->output_len`` on success and
 * a negative error code otherwise.
 */
static inline int caam_encap_blob(struct caam_blob_priv *priv,
				  struct caam_blob_info *info)
{
	if (info->output_len < info->input_len + CAAM_BLOB_OVERHEAD)
		return -EINVAL;

	return caam_process_blob(priv, info, true);
}

/**
 * caam_decap_blob - decapsulate blob
 * @priv:   instance returned by caam_blob_gen_init()
 * @info:   pointer to blobbing info describing output key,
 *          input blob and key modifier buffers.
 *
 * Return: %0 and sets ``info->output_len`` on success and
 * a negative error code otherwise.
 */
static inline int caam_decap_blob(struct caam_blob_priv *priv,
				  struct caam_blob_info *info)
{
	if (info->input_len < CAAM_BLOB_OVERHEAD ||
	    info->output_len < info->input_len - CAAM_BLOB_OVERHEAD)
		return -EINVAL;

	return caam_process_blob(priv, info, false);
}

#endif
