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

#define TPM_DIGEST_SIZE 20	/* Max TPM v1.2 PCR size */
#define TPM_MAX_DIGEST_SIZE SHA512_DIGEST_SIZE

struct tpm_chip;
struct trusted_key_payload;
struct trusted_key_options;
/* opaque structure, holds auth session parameters like the session key */
struct tpm2_auth;

enum tpm2_session_types {
	TPM2_SE_HMAC	= 0x00,
	TPM2_SE_POLICY	= 0x01,
	TPM2_SE_TRIAL	= 0x02,
};

/* if you add a new hash to this, increment TPM_MAX_HASHES below */
enum tpm_algorithms {
	TPM_ALG_ERROR		= 0x0000,
	TPM_ALG_SHA1		= 0x0004,
	TPM_ALG_AES		= 0x0006,
	TPM_ALG_KEYEDHASH	= 0x0008,
	TPM_ALG_SHA256		= 0x000B,
	TPM_ALG_SHA384		= 0x000C,
	TPM_ALG_SHA512		= 0x000D,
	TPM_ALG_NULL		= 0x0010,
	TPM_ALG_SM3_256		= 0x0012,
	TPM_ALG_ECC		= 0x0023,
	TPM_ALG_CFB		= 0x0043,
};

/*
 * maximum number of hashing algorithms a TPM can have.  This is
 * basically a count of every hash in tpm_algorithms above
 */
#define TPM_MAX_HASHES	5

enum tpm2_curves {
	TPM2_ECC_NONE		= 0x0000,
	TPM2_ECC_NIST_P256	= 0x0003,
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

/* fixed define for the curve we use which is NIST_P256 */
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

	struct dentry *bios_dir[TPM_NUM_EVENT_LOG_FILES];

	const struct attribute_group *groups[3 + TPM_MAX_HASHES];
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

#define TPM_HEADER_SIZE		10

enum tpm2_const {
	TPM2_PLATFORM_PCR       =     24,
	TPM2_PCR_SELECT_MIN     = ((TPM2_PLATFORM_PCR + 7) / 8),
};

enum tpm2_timeouts {
	TPM2_TIMEOUT_A          =    750,
	TPM2_TIMEOUT_B          =   2000,
	TPM2_TIMEOUT_C          =    200,
	TPM2_TIMEOUT_D          =     30,
	TPM2_DURATION_SHORT     =     20,
	TPM2_DURATION_MEDIUM    =    750,
	TPM2_DURATION_LONG      =   2000,
	TPM2_DURATION_LONG_LONG = 300000,
	TPM2_DURATION_DEFAULT   = 120000,
};

enum tpm2_structures {
	TPM2_ST_NO_SESSIONS	= 0x8001,
	TPM2_ST_SESSIONS	= 0x8002,
	TPM2_ST_CREATION	= 0x8021,
};

/* Indicates from what layer of the software stack the error comes from */
#define TSS2_RC_LAYER_SHIFT	 16
#define TSS2_RESMGR_TPM_RC_LAYER (11 << TSS2_RC_LAYER_SHIFT)

enum tpm2_return_codes {
	TPM2_RC_SUCCESS		= 0x0000,
	TPM2_RC_HASH		= 0x0083, /* RC_FMT1 */
	TPM2_RC_HANDLE		= 0x008B,
	TPM2_RC_INTEGRITY	= 0x009F,
	TPM2_RC_INITIALIZE	= 0x0100, /* RC_VER1 */
	TPM2_RC_FAILURE		= 0x0101,
	TPM2_RC_DISABLED	= 0x0120,
	TPM2_RC_UPGRADE		= 0x012D,
	TPM2_RC_COMMAND_CODE    = 0x0143,
	TPM2_RC_TESTING		= 0x090A, /* RC_WARN */
	TPM2_RC_REFERENCE_H0	= 0x0910,
	TPM2_RC_RETRY		= 0x0922,
};

enum tpm2_command_codes {
	TPM2_CC_FIRST		        = 0x011F,
	TPM2_CC_HIERARCHY_CONTROL       = 0x0121,
	TPM2_CC_HIERARCHY_CHANGE_AUTH   = 0x0129,
	TPM2_CC_CREATE_PRIMARY          = 0x0131,
	TPM2_CC_SEQUENCE_COMPLETE       = 0x013E,
	TPM2_CC_SELF_TEST	        = 0x0143,
	TPM2_CC_STARTUP		        = 0x0144,
	TPM2_CC_SHUTDOWN	        = 0x0145,
	TPM2_CC_NV_READ                 = 0x014E,
	TPM2_CC_CREATE		        = 0x0153,
	TPM2_CC_LOAD		        = 0x0157,
	TPM2_CC_SEQUENCE_UPDATE         = 0x015C,
	TPM2_CC_UNSEAL		        = 0x015E,
	TPM2_CC_CONTEXT_LOAD	        = 0x0161,
	TPM2_CC_CONTEXT_SAVE	        = 0x0162,
	TPM2_CC_FLUSH_CONTEXT	        = 0x0165,
	TPM2_CC_READ_PUBLIC		= 0x0173,
	TPM2_CC_START_AUTH_SESS		= 0x0176,
	TPM2_CC_VERIFY_SIGNATURE        = 0x0177,
	TPM2_CC_GET_CAPABILITY	        = 0x017A,
	TPM2_CC_GET_RANDOM	        = 0x017B,
	TPM2_CC_PCR_READ	        = 0x017E,
	TPM2_CC_PCR_EXTEND	        = 0x0182,
	TPM2_CC_EVENT_SEQUENCE_COMPLETE = 0x0185,
	TPM2_CC_HASH_SEQUENCE_START     = 0x0186,
	TPM2_CC_CREATE_LOADED           = 0x0191,
	TPM2_CC_LAST		        = 0x0193, /* Spec 1.36 */
};

enum tpm2_permanent_handles {
	TPM2_RH_NULL		= 0x40000007,
	TPM2_RS_PW		= 0x40000009,
};

/* Most Significant Octet for key types  */
enum tpm2_mso_type {
	TPM2_MSO_NVRAM		= 0x01,
	TPM2_MSO_SESSION	= 0x02,
	TPM2_MSO_POLICY		= 0x03,
	TPM2_MSO_PERMANENT	= 0x40,
	TPM2_MSO_VOLATILE	= 0x80,
	TPM2_MSO_PERSISTENT	= 0x81,
};

static inline enum tpm2_mso_type tpm2_handle_mso(u32 handle)
{
	return handle >> 24;
}

enum tpm2_capabilities {
	TPM2_CAP_HANDLES	= 1,
	TPM2_CAP_COMMANDS	= 2,
	TPM2_CAP_PCRS		= 5,
	TPM2_CAP_TPM_PROPERTIES = 6,
};

enum tpm2_properties {
	TPM_PT_TOTAL_COMMANDS	= 0x0129,
};

enum tpm2_startup_types {
	TPM2_SU_CLEAR	= 0x0000,
	TPM2_SU_STATE	= 0x0001,
};

enum tpm2_cc_attrs {
	TPM2_CC_ATTR_CHANDLES	= 25,
	TPM2_CC_ATTR_RHANDLE	= 28,
	TPM2_CC_ATTR_VENDOR	= 29,
};

#define TPM_VID_INTEL    0x8086
#define TPM_VID_WINBOND  0x1050
#define TPM_VID_STM      0x104A
#define TPM_VID_ATML     0x1114

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
};

#define to_tpm_chip(d) container_of(d, struct tpm_chip, dev)

struct tpm_header {
	__be16 tag;
	__be32 length;
	union {
		__be32 ordinal;
		__be32 return_code;
	};
} __packed;

enum tpm_buf_flags {
	/* the capacity exceeded: */
	TPM_BUF_OVERFLOW	= BIT(0),
	/* TPM2B format: */
	TPM_BUF_TPM2B		= BIT(1),
	/* read out of boundary: */
	TPM_BUF_BOUNDARY_ERROR	= BIT(2),
};

/*
 * A string buffer type for constructing TPM commands.
 */
struct tpm_buf {
	u32 flags;
	u32 length;
	u8 *data;
	u8 handles;
};

enum tpm2_object_attributes {
	TPM2_OA_FIXED_TPM		= BIT(1),
	TPM2_OA_ST_CLEAR		= BIT(2),
	TPM2_OA_FIXED_PARENT		= BIT(4),
	TPM2_OA_SENSITIVE_DATA_ORIGIN	= BIT(5),
	TPM2_OA_USER_WITH_AUTH		= BIT(6),
	TPM2_OA_ADMIN_WITH_POLICY	= BIT(7),
	TPM2_OA_NO_DA			= BIT(10),
	TPM2_OA_ENCRYPTED_DUPLICATION	= BIT(11),
	TPM2_OA_RESTRICTED		= BIT(16),
	TPM2_OA_DECRYPT			= BIT(17),
	TPM2_OA_SIGN			= BIT(18),
};

enum tpm2_session_attributes {
	TPM2_SA_CONTINUE_SESSION	= BIT(0),
	TPM2_SA_AUDIT_EXCLUSIVE		= BIT(1),
	TPM2_SA_AUDIT_RESET		= BIT(3),
	TPM2_SA_DECRYPT			= BIT(5),
	TPM2_SA_ENCRYPT			= BIT(6),
	TPM2_SA_AUDIT			= BIT(7),
};

struct tpm2_hash {
	unsigned int crypto_id;
	unsigned int tpm_id;
};

int tpm_buf_init(struct tpm_buf *buf, u16 tag, u32 ordinal);
void tpm_buf_reset(struct tpm_buf *buf, u16 tag, u32 ordinal);
int tpm_buf_init_sized(struct tpm_buf *buf);
void tpm_buf_reset_sized(struct tpm_buf *buf);
void tpm_buf_destroy(struct tpm_buf *buf);
u32 tpm_buf_length(struct tpm_buf *buf);
void tpm_buf_append(struct tpm_buf *buf, const u8 *new_data, u16 new_length);
void tpm_buf_append_u8(struct tpm_buf *buf, const u8 value);
void tpm_buf_append_u16(struct tpm_buf *buf, const u16 value);
void tpm_buf_append_u32(struct tpm_buf *buf, const u32 value);
u8 tpm_buf_read_u8(struct tpm_buf *buf, off_t *offset);
u16 tpm_buf_read_u16(struct tpm_buf *buf, off_t *offset);
u32 tpm_buf_read_u32(struct tpm_buf *buf, off_t *offset);
void tpm_buf_append_handle(struct tpm_chip *chip, struct tpm_buf *buf, u32 handle);

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

void tpm_buf_append_name(struct tpm_chip *chip, struct tpm_buf *buf,
			 u32 handle, u8 *name);
void tpm_buf_append_hmac_session(struct tpm_chip *chip, struct tpm_buf *buf,
				 u8 attributes, u8 *passphrase,
				 int passphraselen);
void tpm_buf_append_auth(struct tpm_chip *chip, struct tpm_buf *buf,
			 u8 attributes, u8 *passphrase, int passphraselen);
static inline void tpm_buf_append_hmac_session_opt(struct tpm_chip *chip,
						   struct tpm_buf *buf,
						   u8 attributes,
						   u8 *passphrase,
						   int passphraselen)
{
	struct tpm_header *head;
	int offset;

	if (tpm2_chip_auth(chip)) {
		tpm_buf_append_hmac_session(chip, buf, attributes, passphrase, passphraselen);
	} else  {
		offset = buf->handles * 4 + TPM_HEADER_SIZE;
		head = (struct tpm_header *)buf->data;

		/*
		 * If the only sessions are optional, the command tag must change to
		 * TPM2_ST_NO_SESSIONS.
		 */
		if (tpm_buf_length(buf) == offset)
			head->tag = cpu_to_be16(TPM2_ST_NO_SESSIONS);
	}
}

#ifdef CONFIG_TCG_TPM2_HMAC

int tpm2_start_auth_session(struct tpm_chip *chip);
void tpm_buf_fill_hmac_session(struct tpm_chip *chip, struct tpm_buf *buf);
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
static inline void tpm_buf_fill_hmac_session(struct tpm_chip *chip,
					     struct tpm_buf *buf)
{
}
static inline int tpm_buf_check_hmac_response(struct tpm_chip *chip,
					      struct tpm_buf *buf,
					      int rc)
{
	return rc;
}
#endif	/* CONFIG_TCG_TPM2_HMAC */

#endif
