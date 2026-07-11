/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_TPM_COMMAND_H__
#define __LINUX_TPM_COMMAND_H__

/*
 * == TPM 1 Family Chips ==
 *
 * TPM 1.2 Main Specification:
 * https://trustedcomputinggroup.org/resource/tpm-main-specification/
 */

#define TPM_MAX_ORDINAL	243

/* Command TAGS */
enum tpm_command_tags {
	TPM_TAG_RQU_COMMAND		= 193,
	TPM_TAG_RQU_AUTH1_COMMAND	= 194,
	TPM_TAG_RQU_AUTH2_COMMAND	= 195,
	TPM_TAG_RSP_COMMAND		= 196,
	TPM_TAG_RSP_AUTH1_COMMAND	= 197,
	TPM_TAG_RSP_AUTH2_COMMAND	= 198,
};

/* Command Ordinals */
enum tpm_command_ordinals {
	TPM_ORD_CONTINUE_SELFTEST	= 83,
	TPM_ORD_GET_CAP			= 101,
	TPM_ORD_GET_RANDOM		= 70,
	TPM_ORD_PCR_EXTEND		= 20,
	TPM_ORD_PCR_READ		= 21,
	TPM_ORD_OSAP			= 11,
	TPM_ORD_OIAP			= 10,
	TPM_ORD_SAVESTATE		= 152,
	TPM_ORD_SEAL			= 23,
	TPM_ORD_STARTUP			= 153,
	TPM_ORD_UNSEAL			= 24,
};

enum tpm_capabilities {
	TPM_CAP_FLAG		= 4,
	TPM_CAP_PROP		= 5,
	TPM_CAP_VERSION_1_1	= 0x06,
	TPM_CAP_VERSION_1_2	= 0x1A,
};

enum tpm_sub_capabilities {
	TPM_CAP_PROP_PCR		= 0x101,
	TPM_CAP_PROP_MANUFACTURER	= 0x103,
	TPM_CAP_FLAG_PERM		= 0x108,
	TPM_CAP_FLAG_VOL		= 0x109,
	TPM_CAP_PROP_OWNER		= 0x111,
	TPM_CAP_PROP_TIS_TIMEOUT	= 0x115,
	TPM_CAP_PROP_TIS_DURATION	= 0x120,
};

/* Return Codes */
enum tpm_return_codes {
	TPM_BASE_MASK			= 0,
	TPM_NON_FATAL_MASK		= 0x00000800,
	TPM_SUCCESS			= TPM_BASE_MASK + 0,
	TPM_ERR_DEACTIVATED		= TPM_BASE_MASK + 6,
	TPM_ERR_DISABLED		= TPM_BASE_MASK + 7,
	TPM_ERR_FAIL			= TPM_BASE_MASK + 9,
	TPM_ERR_FAILEDSELFTEST		= TPM_BASE_MASK + 28,
	TPM_ERR_INVALID_POSTINIT	= TPM_BASE_MASK + 38,
	TPM_ERR_INVALID_FAMILY		= TPM_BASE_MASK + 55,
	TPM_WARN_RETRY			= TPM_BASE_MASK + TPM_NON_FATAL_MASK + 0,
	TPM_WARN_DOING_SELFTEST		= TPM_BASE_MASK + TPM_NON_FATAL_MASK + 2,
};

struct	stclear_flags_t {
	__be16 tag;
	u8 deactivated;
	u8 disableForceClear;
	u8 physicalPresence;
	u8 physicalPresenceLock;
	u8 bGlobalLock;
} __packed;

struct tpm1_version {
	u8 major;
	u8 minor;
	u8 rev_major;
	u8 rev_minor;
} __packed;

struct tpm1_version2 {
	__be16 tag;
	struct tpm1_version version;
} __packed;

struct	timeout_t {
	__be32 a;
	__be32 b;
	__be32 c;
	__be32 d;
} __packed;

struct duration_t {
	__be32 tpm_short;
	__be32 tpm_medium;
	__be32 tpm_long;
} __packed;

struct permanent_flags_t {
	__be16 tag;
	u8 disable;
	u8 ownership;
	u8 deactivated;
	u8 readPubek;
	u8 disableOwnerClear;
	u8 allowMaintenance;
	u8 physicalPresenceLifetimeLock;
	u8 physicalPresenceHWEnable;
	u8 physicalPresenceCMDEnable;
	u8 CEKPUsed;
	u8 TPMpost;
	u8 TPMpostLock;
	u8 FIPS;
	u8 operator;
	u8 enableRevokeEK;
	u8 nvLocked;
	u8 readSRKPub;
	u8 tpmEstablished;
	u8 maintenanceDone;
	u8 disableFullDALogicInfo;
} __packed;

typedef union {
	struct permanent_flags_t perm_flags;
	struct stclear_flags_t stclear_flags;
	__u8 owned;
	__be32 num_pcrs;
	struct tpm1_version version1;
	struct tpm1_version2 version2;
	__be32 manufacturer_id;
	struct timeout_t timeout;
	struct duration_t duration;
} cap_t;

/*
 * 128 bytes is an arbitrary cap. This could be as large as TPM_BUFSIZE - 18
 * bytes, but 128 is still a relatively large number of random bytes and
 * anything much bigger causes users of struct tpm_cmd_t to start getting
 * compiler warnings about stack frame size.
 */
#define TPM_MAX_RNG_DATA		128

struct tpm1_get_random_out {
	__be32 rng_data_len;
	u8 rng_data[TPM_MAX_RNG_DATA];
} __packed;

/* Other constants */
#define SRKHANDLE                       0x40000000
#define TPM_NONCE_SIZE                  20
#define TPM_ST_CLEAR			1

#endif
