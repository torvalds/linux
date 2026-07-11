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

/*
 * == TPM 2 Family Chips ==
 *
 * TPM 2.0 Library
 * https://trustedcomputinggroup.org/resource/tpm-library-specification/
 */

/* TPM2 specific constants. */
#define TPM2_SPACE_BUFFER_SIZE		16384 /* 16 kB */

enum tpm2_session_types {
	TPM2_SE_HMAC	= 0x00,
	TPM2_SE_POLICY	= 0x01,
	TPM2_SE_TRIAL	= 0x02,
};

enum tpm2_timeouts {
	TPM2_TIMEOUT_A		= 750,
	TPM2_TIMEOUT_B		= 4000,
	TPM2_TIMEOUT_C		= 200,
	TPM2_TIMEOUT_D		= 30,
	TPM2_DURATION_SHORT	= 20,
	TPM2_DURATION_MEDIUM	= 750,
	TPM2_DURATION_LONG	= 2000,
	TPM2_DURATION_LONG_LONG	= 300000,
	TPM2_DURATION_DEFAULT	= 120000,
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
	TPM2_RC_COMMAND_CODE	= 0x0143,
	TPM2_RC_TESTING		= 0x090A, /* RC_WARN */
	TPM2_RC_REFERENCE_H0	= 0x0910,
	TPM2_RC_RETRY		= 0x0922,
	TPM2_RC_SESSION_MEMORY	= 0x0903,
};

enum tpm2_command_codes {
	TPM2_CC_FIRST			= 0x011F,
	TPM2_CC_HIERARCHY_CONTROL	= 0x0121,
	TPM2_CC_HIERARCHY_CHANGE_AUTH	= 0x0129,
	TPM2_CC_CREATE_PRIMARY		= 0x0131,
	TPM2_CC_SEQUENCE_COMPLETE	= 0x013E,
	TPM2_CC_SELF_TEST		= 0x0143,
	TPM2_CC_STARTUP			= 0x0144,
	TPM2_CC_SHUTDOWN		= 0x0145,
	TPM2_CC_NV_READ			= 0x014E,
	TPM2_CC_CREATE			= 0x0153,
	TPM2_CC_LOAD			= 0x0157,
	TPM2_CC_SEQUENCE_UPDATE		= 0x015C,
	TPM2_CC_UNSEAL			= 0x015E,
	TPM2_CC_CONTEXT_LOAD		= 0x0161,
	TPM2_CC_CONTEXT_SAVE		= 0x0162,
	TPM2_CC_FLUSH_CONTEXT		= 0x0165,
	TPM2_CC_READ_PUBLIC		= 0x0173,
	TPM2_CC_START_AUTH_SESS		= 0x0176,
	TPM2_CC_VERIFY_SIGNATURE	= 0x0177,
	TPM2_CC_GET_CAPABILITY		= 0x017A,
	TPM2_CC_GET_RANDOM		= 0x017B,
	TPM2_CC_PCR_READ		= 0x017E,
	TPM2_CC_PCR_EXTEND		= 0x0182,
	TPM2_CC_EVENT_SEQUENCE_COMPLETE	= 0x0185,
	TPM2_CC_HASH_SEQUENCE_START	= 0x0186,
	TPM2_CC_CREATE_LOADED		= 0x0191,
	TPM2_CC_LAST			= 0x0193, /* Spec 1.36 */
};

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
	TPM2_SU_CLEAR		= 0x0000,
	TPM2_SU_STATE		= 0x0001,
};

enum tpm2_cc_attrs {
	TPM2_CC_ATTR_CHANDLES	= 25,
	TPM2_CC_ATTR_RHANDLE	= 28,
	TPM2_CC_ATTR_VENDOR	= 29,
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

enum tpm2_curves {
	TPM2_ECC_NONE		= 0x0000,
	TPM2_ECC_NIST_P256	= 0x0003,
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

enum tpm2_pcr_select {
	TPM2_PLATFORM_PCR	= 24,
	TPM2_PCR_SELECT_MIN	= ((TPM2_PLATFORM_PCR + 7) / 8),
};

enum tpm2_handle_types {
	TPM2_HT_HMAC_SESSION	= 0x02000000,
	TPM2_HT_POLICY_SESSION	= 0x03000000,
	TPM2_HT_TRANSIENT	= 0x80000000,
};

enum tpm2_pt_props {
	TPM2_PT_NONE			= 0x00000000,
	TPM2_PT_GROUP			= 0x00000100,
	TPM2_PT_FIXED			= TPM2_PT_GROUP * 1,
	TPM2_PT_FAMILY_INDICATOR	= TPM2_PT_FIXED + 0,
	TPM2_PT_LEVEL		= TPM2_PT_FIXED + 1,
	TPM2_PT_REVISION	= TPM2_PT_FIXED + 2,
	TPM2_PT_DAY_OF_YEAR	= TPM2_PT_FIXED + 3,
	TPM2_PT_YEAR		= TPM2_PT_FIXED + 4,
	TPM2_PT_MANUFACTURER	= TPM2_PT_FIXED + 5,
	TPM2_PT_VENDOR_STRING_1	= TPM2_PT_FIXED + 6,
	TPM2_PT_VENDOR_STRING_2	= TPM2_PT_FIXED + 7,
	TPM2_PT_VENDOR_STRING_3	= TPM2_PT_FIXED + 8,
	TPM2_PT_VENDOR_STRING_4	= TPM2_PT_FIXED + 9,
	TPM2_PT_VENDOR_TPM_TYPE	= TPM2_PT_FIXED + 10,
	TPM2_PT_FIRMWARE_VERSION_1	= TPM2_PT_FIXED + 11,
	TPM2_PT_FIRMWARE_VERSION_2	= TPM2_PT_FIXED + 12,
	TPM2_PT_INPUT_BUFFER		= TPM2_PT_FIXED + 13,
	TPM2_PT_HR_TRANSIENT_MIN	= TPM2_PT_FIXED + 14,
	TPM2_PT_HR_PERSISTENT_MIN	= TPM2_PT_FIXED + 15,
	TPM2_PT_HR_LOADED_MIN		= TPM2_PT_FIXED + 16,
	TPM2_PT_ACTIVE_SESSIONS_MAX	= TPM2_PT_FIXED + 17,
	TPM2_PT_PCR_COUNT	= TPM2_PT_FIXED + 18,
	TPM2_PT_PCR_SELECT_MIN	= TPM2_PT_FIXED + 19,
	TPM2_PT_CONTEXT_GAP_MAX	= TPM2_PT_FIXED + 20,
	TPM2_PT_NV_COUNTERS_MAX	= TPM2_PT_FIXED + 22,
	TPM2_PT_NV_INDEX_MAX	= TPM2_PT_FIXED + 23,
	TPM2_PT_MEMORY		= TPM2_PT_FIXED + 24,
	TPM2_PT_CLOCK_UPDATE	= TPM2_PT_FIXED + 25,
	TPM2_PT_CONTEXT_HASH	= TPM2_PT_FIXED + 26,
	TPM2_PT_CONTEXT_SYM	= TPM2_PT_FIXED + 27,
	TPM2_PT_CONTEXT_SYM_SIZE	= TPM2_PT_FIXED + 28,
	TPM2_PT_ORDERLY_COUNT		= TPM2_PT_FIXED + 29,
	TPM2_PT_MAX_COMMAND_SIZE	= TPM2_PT_FIXED + 30,
	TPM2_PT_MAX_RESPONSE_SIZE	= TPM2_PT_FIXED + 31,
	TPM2_PT_MAX_DIGEST		= TPM2_PT_FIXED + 32,
	TPM2_PT_MAX_OBJECT_CONTEXT	= TPM2_PT_FIXED + 33,
	TPM2_PT_MAX_SESSION_CONTEXT	= TPM2_PT_FIXED + 34,
	TPM2_PT_PS_FAMILY_INDICATOR	= TPM2_PT_FIXED + 35,
	TPM2_PT_PS_LEVEL	= TPM2_PT_FIXED + 36,
	TPM2_PT_PS_REVISION	= TPM2_PT_FIXED + 37,
	TPM2_PT_PS_DAY_OF_YEAR	= TPM2_PT_FIXED + 38,
	TPM2_PT_PS_YEAR		= TPM2_PT_FIXED + 39,
	TPM2_PT_SPLIT_MAX	= TPM2_PT_FIXED + 40,
	TPM2_PT_TOTAL_COMMANDS	= TPM2_PT_FIXED + 41,
	TPM2_PT_LIBRARY_COMMANDS	= TPM2_PT_FIXED + 42,
	TPM2_PT_VENDOR_COMMANDS		= TPM2_PT_FIXED + 43,
	TPM2_PT_NV_BUFFER_MAX		= TPM2_PT_FIXED + 44,
	TPM2_PT_MODES			= TPM2_PT_FIXED + 45,
	TPM2_PT_MAX_CAP_BUFFER		= TPM2_PT_FIXED + 46,
	TPM2_PT_VAR		= TPM2_PT_GROUP * 2,
	TPM2_PT_PERMANENT	= TPM2_PT_VAR + 0,
	TPM2_PT_STARTUP_CLEAR	= TPM2_PT_VAR + 1,
	TPM2_PT_HR_NV_INDEX	= TPM2_PT_VAR + 2,
	TPM2_PT_HR_LOADED	= TPM2_PT_VAR + 3,
	TPM2_PT_HR_LOADED_AVAIL	= TPM2_PT_VAR + 4,
	TPM2_PT_HR_ACTIVE	= TPM2_PT_VAR + 5,
	TPM2_PT_HR_ACTIVE_AVAIL	= TPM2_PT_VAR + 6,
	TPM2_PT_HR_TRANSIENT_AVAIL	= TPM2_PT_VAR + 7,
	TPM2_PT_HR_PERSISTENT		= TPM2_PT_VAR + 8,
	TPM2_PT_HR_PERSISTENT_AVAIL	= TPM2_PT_VAR + 9,
	TPM2_PT_NV_COUNTERS		= TPM2_PT_VAR + 10,
	TPM2_PT_NV_COUNTERS_AVAIL	= TPM2_PT_VAR + 11,
	TPM2_PT_ALGORITHM_SET		= TPM2_PT_VAR + 12,
	TPM2_PT_LOADED_CURVES		= TPM2_PT_VAR + 13,
	TPM2_PT_LOCKOUT_COUNTER		= TPM2_PT_VAR + 14,
	TPM2_PT_MAX_AUTH_FAIL		= TPM2_PT_VAR + 15,
	TPM2_PT_LOCKOUT_INTERVAL	= TPM2_PT_VAR + 16,
	TPM2_PT_LOCKOUT_RECOVERY	= TPM2_PT_VAR + 17,
	TPM2_PT_NV_WRITE_RECOVERY	= TPM2_PT_VAR + 18,
	TPM2_PT_AUDIT_COUNTER_0	= TPM2_PT_VAR + 19,
	TPM2_PT_AUDIT_COUNTER_1	= TPM2_PT_VAR + 20,
};

struct tpm2_pcr_read_out {
	__be32 update_cnt;
	__be32 pcr_selects_cnt;
	__be16 hash_alg;
	u8 pcr_select_size;
	u8 pcr_select[TPM2_PCR_SELECT_MIN];
	__be32 digests_cnt;
	__be16 digest_size;
	u8 digest[];
} __packed;

struct tpm2_get_random_out {
	__be16 size;
	u8 buffer[TPM_MAX_RNG_DATA];
} __packed;

struct tpm2_get_cap_out {
	u8 more_data;
	__be32 subcap_id;
	__be32 property_cnt;
	__be32 property_id;
	__be32 value;
} __packed;

struct tpm2_pcr_selection {
	__be16 hash_alg;
	u8 size_of_select;
	u8 pcr_select[3];
} __packed;

struct tpm2_context {
	__be64 sequence;
	__be32 saved_handle;
	__be32 hierarchy;
	__be16 blob_size;
} __packed;

#endif
