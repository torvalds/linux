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

/* Other constants */
#define SRKHANDLE                       0x40000000
#define TPM_NONCE_SIZE                  20
#define TPM_ST_CLEAR			1

#endif
