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

/*
 * Chip num is this value or a valid tpm idx
 */
#define	TPM_ANY_NUM 0xFFFF

#if defined(CONFIG_TCG_TPM) || defined(CONFIG_TCG_TPM_MODULE)

extern int tpm_pcr_read(u32 chip_num, int pcr_idx, u8 *res_buf);
extern int tpm_pcr_extend(u32 chip_num, int pcr_idx, const u8 *hash);
extern int tpm_send(u32 chip_num, void *cmd, size_t buflen);
extern int tpm_get_random(u32 chip_num, u8 *data, size_t max);
#else
static inline int tpm_pcr_read(u32 chip_num, int pcr_idx, u8 *res_buf) {
	return -ENODEV;
}
static inline int tpm_pcr_extend(u32 chip_num, int pcr_idx, const u8 *hash) {
	return -ENODEV;
}
static inline int tpm_send(u32 chip_num, void *cmd, size_t buflen) {
	return -ENODEV;
}
static inline int tpm_get_random(u32 chip_num, u8 *data, size_t max) {
	return -ENODEV;
}
#endif
#endif
