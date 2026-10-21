/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Following copyright information was take from the original file
 * <include/linux/tpm.h> where the definitions were moved from:
 *
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

#ifndef __LINUX_TPM_BUF_H__
#define __LINUX_TPM_BUF_H__

#include <linux/types.h>
#include <linux/bits.h>

enum tpm_buf_flags {
	/* TPM2B format: */
	TPM_BUF_TPM2B		= BIT(0),
	/* The buffer is in invalid and unusable state: */
	TPM_BUF_INVALID		= BIT(1),
};

/*
 * A buffer for constructing and parsing TPM commands, responses and sized
 * (TPM2B) buffers.
 */
struct tpm_buf {
	u8 flags;
	u8 handles;
	u16 length;
	u16 capacity;
	u8 data[];
};

void tpm_buf_init(struct tpm_buf *buf, u16 buf_size);
void tpm_buf_init_sized(struct tpm_buf *buf, u16 buf_size);
void tpm_buf_reset(struct tpm_buf *buf, u16 tag, u32 ordinal);
void tpm_buf_reset_sized(struct tpm_buf *buf);
u16 tpm_buf_length(struct tpm_buf *buf);
void tpm_buf_append(struct tpm_buf *buf, const u8 *new_data, u16 new_length);
void tpm_buf_append_u8(struct tpm_buf *buf, const u8 value);
void tpm_buf_append_u16(struct tpm_buf *buf, const u16 value);
void tpm_buf_append_u32(struct tpm_buf *buf, const u32 value);
u8 tpm_buf_read_u8(struct tpm_buf *buf, off_t *offset);
u16 tpm_buf_read_u16(struct tpm_buf *buf, off_t *offset);
u32 tpm_buf_read_u32(struct tpm_buf *buf, off_t *offset);
void tpm_buf_append_handle(struct tpm_buf *buf, u32 handle);

#endif
