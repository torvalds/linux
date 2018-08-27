/*
 * Copyright (C) 2004,2007,2008 IBM Corporation
 *
 * Authors:
 * Leendert van Doorn <leendert@watson.ibm.com>
 * Dave Safford <safford@watson.ibm.com>
 * Reiner Sailer <sailer@watson.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 * Debora Velarde <dvelarde@us.ibm.com>
 *
 * Maintained by: <tpmdd_devel@lists.sourceforge.net>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 */
#ifndef __LINUX_TPM_H__
#define __LINUX_TPM_H__

#define TPM_DIGEST_SIZE 20	/* Max TPM v1.2 PCR size */

struct tpm_chip;
struct trusted_key_payload;
struct trusted_key_options;

enum TPM_OPS_FLAGS {
	TPM_OPS_AUTO_STARTUP = BIT(0),
};

struct tpm_class_ops {
	unsigned int flags;
	const u8 req_complete_mask;
	const u8 req_complete_val;
	bool (*req_canceled)(struct tpm_chip *chip, u8 status);
	int (*recv) (struct tpm_chip *chip, u8 *buf, size_t len);
	int (*send) (struct tpm_chip *chip, u8 *buf, size_t len);
	void (*cancel) (struct tpm_chip *chip);
	u8 (*status) (struct tpm_chip *chip);
	bool (*update_timeouts)(struct tpm_chip *chip,
				unsigned long *timeout_cap);
	int (*go_idle)(struct tpm_chip *chip);
	int (*cmd_ready)(struct tpm_chip *chip);
	int (*request_locality)(struct tpm_chip *chip, int loc);
	int (*relinquish_locality)(struct tpm_chip *chip, int loc);
	void (*clk_enable)(struct tpm_chip *chip, bool value);
};

#if defined(CONFIG_TCG_TPM) || defined(CONFIG_TCG_TPM_MODULE)

extern int tpm_is_tpm2(struct tpm_chip *chip);
extern int tpm_pcr_read(struct tpm_chip *chip, int pcr_idx, u8 *res_buf);
extern int tpm_pcr_extend(struct tpm_chip *chip, int pcr_idx, const u8 *hash);
extern int tpm_send(struct tpm_chip *chip, void *cmd, size_t buflen);
extern int tpm_get_random(struct tpm_chip *chip, u8 *data, size_t max);
extern int tpm_seal_trusted(struct tpm_chip *chip,
			    struct trusted_key_payload *payload,
			    struct trusted_key_options *options);
extern int tpm_unseal_trusted(struct tpm_chip *chip,
			      struct trusted_key_payload *payload,
			      struct trusted_key_options *options);
extern struct tpm_chip *tpm_default_chip(void);
#else
static inline int tpm_is_tpm2(struct tpm_chip *chip)
{
	return -ENODEV;
}
static inline int tpm_pcr_read(struct tpm_chip *chip, int pcr_idx, u8 *res_buf)
{
	return -ENODEV;
}
static inline int tpm_pcr_extend(struct tpm_chip *chip, int pcr_idx,
				 const u8 *hash)
{
	return -ENODEV;
}
static inline int tpm_send(struct tpm_chip *chip, void *cmd, size_t buflen)
{
	return -ENODEV;
}
static inline int tpm_get_random(struct tpm_chip *chip, u8 *data, size_t max)
{
	return -ENODEV;
}

static inline int tpm_seal_trusted(struct tpm_chip *chip,
				   struct trusted_key_payload *payload,
				   struct trusted_key_options *options)
{
	return -ENODEV;
}
static inline int tpm_unseal_trusted(struct tpm_chip *chip,
				     struct trusted_key_payload *payload,
				     struct trusted_key_options *options)
{
	return -ENODEV;
}
static inline struct tpm_chip *tpm_default_chip(void)
{
	return NULL;
}
#endif
#endif
