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
#include <linux/highmem.h>
#include <crypto/hash_info.h>
#include <crypto/aes.h>

#include <linux/tpm_command.h>
#include <linux/tpm_buf.h>

struct tpm_chip;
struct trusted_key_payload;
struct trusted_key_options;
/* opaque structure, holds auth session parameters like the session key */
struct tpm2_auth;

enum TPM_OPS_FLAGS {
	TPM_OPS_AUTO_STARTUP = BIT(0),
};

struct tpm_class_ops {
	unsigned int flags;
	const u8 req_complete_mask;
	const u8 req_complete_val;
	bool (*req_canceled)(struct tpm_chip *chip, u8 status);
	int (*recv) (struct tpm_chip *chip, u8 *buf, size_t len);
	int (*send)(struct tpm_chip *chip, u8 *buf, size_t bufsiz,
		    size_t cmd_len);
	void (*cancel) (struct tpm_chip *chip);
	u8 (*status) (struct tpm_chip *chip);
	void (*update_timeouts)(struct tpm_chip *chip,
				unsigned long *timeout_cap);
	void (*update_durations)(struct tpm_chip *chip,
				 unsigned long *duration_cap);
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
	u32 buf_size;
};

struct tpm_bios_log {
	void *bios_event_log;
	void *bios_event_log_end;
};

struct tpm_chip_seqops {
	struct tpm_chip *chip;
	const struct seq_operations *seqops;
};

/* Fixed define for the curve we use which is NIST_P256 */
#define EC_PT_SZ	32

/*
 * fixed define for the size of a name.  This is actually HASHALG size
 * plus 2, so 32 for SHA256
 */
#define TPM2_NAME_SIZE	34

/*
 * The maximum size for an object context
 */
#define TPM2_MAX_CONTEXT_SIZE 4096

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

	struct dentry *bios_dir;

	const struct attribute_group *groups[3 + TPM_MAX_HASHES];
	unsigned int groups_cnt;

	u32 nr_allocated_banks;
	struct tpm_bank_info allocated_banks[TPM2_MAX_PCR_BANKS];
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

#ifdef CONFIG_TCG_TPM2_HMAC
	/* details for communication security via sessions */

	/* saved context for NULL seed */
	u8 null_key_context[TPM2_MAX_CONTEXT_SIZE];
	 /* name of NULL seed */
	u8 null_key_name[TPM2_NAME_SIZE];
	u8 null_ec_key_x[EC_PT_SZ];
	u8 null_ec_key_y[EC_PT_SZ];
	struct tpm2_auth *auth;
#endif
};

static inline enum tpm2_mso_type tpm2_handle_mso(u32 handle)
{
	return handle >> 24;
}

#define TPM_VID_INTEL    0x8086
#define TPM_VID_WINBOND  0x1050
#define TPM_VID_STM      0x104A
#define TPM_VID_ATML     0x1114
#define TPM_VID_IFX      0x15D1

enum tpm_chip_flags {
	TPM_CHIP_FLAG_BOOTSTRAPPED		= BIT(0),
	TPM_CHIP_FLAG_TPM2			= BIT(1),
	TPM_CHIP_FLAG_IRQ			= BIT(2),
	TPM_CHIP_FLAG_VIRTUAL			= BIT(3),
	TPM_CHIP_FLAG_HAVE_TIMEOUTS		= BIT(4),
	TPM_CHIP_FLAG_ALWAYS_POWERED		= BIT(5),
	TPM_CHIP_FLAG_FIRMWARE_POWER_MANAGED	= BIT(6),
	TPM_CHIP_FLAG_FIRMWARE_UPGRADE		= BIT(7),
	TPM_CHIP_FLAG_SUSPENDED			= BIT(8),
	TPM_CHIP_FLAG_HWRNG_DISABLED		= BIT(9),
	TPM_CHIP_FLAG_DISABLE			= BIT(10),
	TPM_CHIP_FLAG_SYNC			= BIT(11),
};

#define to_tpm_chip(d) container_of(d, struct tpm_chip, dev)

struct tpm2_hash {
	unsigned int crypto_id;
	unsigned int tpm_id;
};

/*
 * Check if TPM device is in the firmware upgrade mode.
 */
static inline bool tpm_is_firmware_upgrade(struct tpm_chip *chip)
{
	return chip->flags & TPM_CHIP_FLAG_FIRMWARE_UPGRADE;
}

static inline u32 tpm2_rc_value(u32 rc)
{
	return (rc & BIT(7)) ? rc & 0xbf : rc;
}

/*
 * Convert a return value from tpm_transmit_cmd() to POSIX error code.
 */
static inline ssize_t tpm_ret_to_err(ssize_t ret)
{
	if (ret < 0)
		return ret;

	switch (tpm2_rc_value(ret)) {
	case TPM2_RC_SUCCESS:
		return 0;
	case TPM2_RC_SESSION_MEMORY:
		return -ENOMEM;
	case TPM2_RC_HASH:
		return -EINVAL;
	default:
		return -EPERM;
	}
}

#if defined(CONFIG_TCG_TPM) || defined(CONFIG_TCG_TPM_MODULE)

extern int tpm_is_tpm2(struct tpm_chip *chip);
extern __must_check int tpm_try_get_ops(struct tpm_chip *chip);
extern void tpm_put_ops(struct tpm_chip *chip);
extern ssize_t tpm_transmit_cmd(struct tpm_chip *chip, struct tpm_buf *buf,
				size_t min_rsp_body_length, const char *desc);
extern int tpm_pcr_read(struct tpm_chip *chip, u32 pcr_idx,
			struct tpm_digest *digest);
extern int tpm_pcr_extend(struct tpm_chip *chip, u32 pcr_idx,
			  struct tpm_digest *digests);
extern int tpm_get_random(struct tpm_chip *chip, u8 *data, size_t max);
extern struct tpm_chip *tpm_default_chip(void);
void tpm2_flush_context(struct tpm_chip *chip, u32 handle);
int tpm2_find_hash_alg(unsigned int crypto_id);

static inline void tpm_buf_append_empty_auth(struct tpm_buf *buf, u32 handle)
{
	/* simple authorization for empty auth */
	tpm_buf_append_u32(buf, 9);		/* total length of auth */
	tpm_buf_append_u32(buf, handle);
	tpm_buf_append_u16(buf, 0);		/* nonce len */
	tpm_buf_append_u8(buf, 0);		/* attributes */
	tpm_buf_append_u16(buf, 0);		/* hmac len */
}
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

static inline int tpm_get_random(struct tpm_chip *chip, u8 *data, size_t max)
{
	return -ENODEV;
}

static inline struct tpm_chip *tpm_default_chip(void)
{
	return NULL;
}

static inline void tpm_buf_append_empty_auth(struct tpm_buf *buf, u32 handle)
{
}
#endif

static inline struct tpm2_auth *tpm2_chip_auth(struct tpm_chip *chip)
{
#ifdef CONFIG_TCG_TPM2_HMAC
	return chip->auth;
#else
	return NULL;
#endif
}

int tpm_buf_append_name(struct tpm_chip *chip, struct tpm_buf *buf,
			u32 handle, u8 *name);
void tpm_buf_append_hmac_session(struct tpm_chip *chip, struct tpm_buf *buf,
				 u8 attributes, u8 *passphrase,
				 int passphraselen);
void tpm_buf_append_auth(struct tpm_chip *chip, struct tpm_buf *buf,
			 u8 *passphrase, int passphraselen);

#ifdef CONFIG_TCG_TPM2_HMAC

int tpm2_start_auth_session(struct tpm_chip *chip);
int tpm_buf_fill_hmac_session(struct tpm_chip *chip, struct tpm_buf *buf);
int tpm_buf_check_hmac_response(struct tpm_chip *chip, struct tpm_buf *buf,
				int rc);
void tpm2_end_auth_session(struct tpm_chip *chip);
#else
#include <linux/unaligned.h>

static inline int tpm2_start_auth_session(struct tpm_chip *chip)
{
	return 0;
}
static inline void tpm2_end_auth_session(struct tpm_chip *chip)
{
}

static inline int tpm_buf_fill_hmac_session(struct tpm_chip *chip,
					    struct tpm_buf *buf)
{
	return 0;
}

static inline int tpm_buf_check_hmac_response(struct tpm_chip *chip,
					      struct tpm_buf *buf,
					      int rc)
{
	return rc;
}
#endif	/* CONFIG_TCG_TPM2_HMAC */

#endif
