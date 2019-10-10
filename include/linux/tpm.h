/* SPDX-License-Identifier: GPL-2.0-only */
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
 */
#ifndef __LINUX_TPM_H__
#define __LINUX_TPM_H__

#include <linux/hw_random.h>
#include <linux/acpi.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <crypto/hash_info.h>

#define TPM_DIGEST_SIZE 20	/* Max TPM v1.2 PCR size */
#define TPM_MAX_DIGEST_SIZE SHA512_DIGEST_SIZE

struct tpm_chip;
struct trusted_key_payload;
struct trusted_key_options;

enum tpm_algorithms {
	TPM_ALG_ERROR		= 0x0000,
	TPM_ALG_SHA1		= 0x0004,
	TPM_ALG_KEYEDHASH	= 0x0008,
	TPM_ALG_SHA256		= 0x000B,
	TPM_ALG_SHA384		= 0x000C,
	TPM_ALG_SHA512		= 0x000D,
	TPM_ALG_NULL		= 0x0010,
	TPM_ALG_SM3_256		= 0x0012,
};

struct tpm_digest {
	u16 alg_id;
	u8 digest[TPM_MAX_DIGEST_SIZE];
} __packed;

struct tpm_bank_info {
	u16 alg_id;
	u16 digest_size;
	u16 crypto_id;
};

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
	void (*update_timeouts)(struct tpm_chip *chip,
				unsigned long *timeout_cap);
	int (*go_idle)(struct tpm_chip *chip);
	int (*cmd_ready)(struct tpm_chip *chip);
	int (*request_locality)(struct tpm_chip *chip, int loc);
	int (*relinquish_locality)(struct tpm_chip *chip, int loc);
	void (*clk_enable)(struct tpm_chip *chip, bool value);
};

#define TPM_NUM_EVENT_LOG_FILES		3

/* Indexes the duration array */
enum tpm_duration {
	TPM_SHORT = 0,
	TPM_MEDIUM = 1,
	TPM_LONG = 2,
	TPM_LONG_LONG = 3,
	TPM_UNDEFINED,
	TPM_NUM_DURATIONS = TPM_UNDEFINED,
};

#define TPM_PPI_VERSION_LEN		3

struct tpm_space {
	u32 context_tbl[3];
	u8 *context_buf;
	u32 session_tbl[3];
	u8 *session_buf;
};

struct tpm_bios_log {
	void *bios_event_log;
	void *bios_event_log_end;
};

struct tpm_chip_seqops {
	struct tpm_chip *chip;
	const struct seq_operations *seqops;
};

struct tpm_chip {
	struct device dev;
	struct device devs;
	struct cdev cdev;
	struct cdev cdevs;

	/* A driver callback under ops cannot be run unless ops_sem is held
	 * (sometimes implicitly, eg for the sysfs code). ops becomes null
	 * when the driver is unregistered, see tpm_try_get_ops.
	 */
	struct rw_semaphore ops_sem;
	const struct tpm_class_ops *ops;

	struct tpm_bios_log log;
	struct tpm_chip_seqops bin_log_seqops;
	struct tpm_chip_seqops ascii_log_seqops;

	unsigned int flags;

	int dev_num;		/* /dev/tpm# */
	unsigned long is_open;	/* only one allowed */

	char hwrng_name[64];
	struct hwrng hwrng;

	struct mutex tpm_mutex;	/* tpm is processing */

	unsigned long timeout_a; /* jiffies */
	unsigned long timeout_b; /* jiffies */
	unsigned long timeout_c; /* jiffies */
	unsigned long timeout_d; /* jiffies */
	bool timeout_adjusted;
	unsigned long duration[TPM_NUM_DURATIONS]; /* jiffies */
	bool duration_adjusted;

	struct dentry *bios_dir[TPM_NUM_EVENT_LOG_FILES];

	const struct attribute_group *groups[3];
	unsigned int groups_cnt;

	u32 nr_allocated_banks;
	struct tpm_bank_info *allocated_banks;
#ifdef CONFIG_ACPI
	acpi_handle acpi_dev_handle;
	char ppi_version[TPM_PPI_VERSION_LEN + 1];
#endif /* CONFIG_ACPI */

	struct tpm_space work_space;
	u32 last_cc;
	u32 nr_commands;
	u32 *cc_attrs_tbl;

	/* active locality */
	int locality;
};

#if defined(CONFIG_TCG_TPM) || defined(CONFIG_TCG_TPM_MODULE)

extern int tpm_is_tpm2(struct tpm_chip *chip);
extern int tpm_pcr_read(struct tpm_chip *chip, u32 pcr_idx,
			struct tpm_digest *digest);
extern int tpm_pcr_extend(struct tpm_chip *chip, u32 pcr_idx,
			  struct tpm_digest *digests);
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

static inline int tpm_pcr_read(struct tpm_chip *chip, int pcr_idx,
			       struct tpm_digest *digest)
{
	return -ENODEV;
}

static inline int tpm_pcr_extend(struct tpm_chip *chip, u32 pcr_idx,
				 struct tpm_digest *digests)
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
