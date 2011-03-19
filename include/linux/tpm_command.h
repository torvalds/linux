#ifndef __LINUX_TPM_COMMAND_H__
#define __LINUX_TPM_COMMAND_H__

/*
 * TPM Command constants from specifications at
 * http://www.trustedcomputinggroup.org
 */

/* Command TAGS */
#define TPM_TAG_RQU_COMMAND             193
#define TPM_TAG_RQU_AUTH1_COMMAND       194
#define TPM_TAG_RQU_AUTH2_COMMAND       195
#define TPM_TAG_RSP_COMMAND             196
#define TPM_TAG_RSP_AUTH1_COMMAND       197
#define TPM_TAG_RSP_AUTH2_COMMAND       198

/* Command Ordinals */
#define TPM_ORD_GETRANDOM               70
#define TPM_ORD_OSAP                    11
#define TPM_ORD_OIAP                    10
#define TPM_ORD_SEAL                    23
#define TPM_ORD_UNSEAL                  24

/* Other constants */
#define SRKHANDLE                       0x40000000
#define TPM_NONCE_SIZE                  20

#endif
