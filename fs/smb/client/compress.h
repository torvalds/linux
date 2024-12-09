/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024, SUSE LLC
 *
 * Authors: Enzo Matsumiya <ematsumiya@suse.de>
 *
 * This file implements I/O compression support for SMB2 messages (SMB 3.1.1 only).
 * See compress/ for implementation details of each algorithm.
 *
 * References:
 * MS-SMB2 "3.1.4.4 Compressing the Message" - for compression details
 * MS-SMB2 "3.1.5.3 Decompressing the Chained Message" - for decompression details
 * MS-XCA - for details of the supported algorithms
 */
#ifndef _SMB_COMPRESS_H
#define _SMB_COMPRESS_H

#include <linux/uio.h>
#include <linux/kernel.h>
#include "../common/smb2pdu.h"
#include "cifsglob.h"

/* sizeof(smb2_compression_hdr) - sizeof(OriginalPayloadSize) */
#define SMB_COMPRESS_HDR_LEN		16
/* sizeof(smb2_compression_payload_hdr) - sizeof(OriginalPayloadSize) */
#define SMB_COMPRESS_PAYLOAD_HDR_LEN	8
#define SMB_COMPRESS_MIN_LEN		PAGE_SIZE

#ifdef CONFIG_CIFS_COMPRESSION
typedef int (*compress_send_fn)(struct TCP_Server_Info *, int, struct smb_rqst *);

int smb_compress(struct TCP_Server_Info *server, struct smb_rqst *rq, compress_send_fn send_fn);

/**
 * should_compress() - Determines if a request (write) or the response to a
 *		       request (read) should be compressed.
 * @tcon: tcon of the request is being sent to
 * @rqst: request to evaluate
 *
 * Return: true iff:
 * - compression was successfully negotiated with server
 * - server has enabled compression for the share
 * - it's a read or write request
 * - (write only) request length is >= SMB_COMPRESS_MIN_LEN
 * - (write only) is_compressible() returns 1
 *
 * Return false otherwise.
 */
bool should_compress(const struct cifs_tcon *tcon, const struct smb_rqst *rq);

/**
 * smb_compress_alg_valid() - Validate a compression algorithm.
 * @alg: Compression algorithm to check.
 * @valid_none: Conditional check whether NONE algorithm should be
 *		considered valid or not.
 *
 * If @alg is SMB3_COMPRESS_NONE, this function returns @valid_none.
 *
 * Note that 'NONE' (0) compressor type is considered invalid in protocol
 * negotiation, as it's never requested to/returned from the server.
 *
 * Return: true if @alg is valid/supported, false otherwise.
 */
static __always_inline int smb_compress_alg_valid(__le16 alg, bool valid_none)
{
	if (alg == SMB3_COMPRESS_NONE)
		return valid_none;

	if (alg == SMB3_COMPRESS_LZ77 || alg == SMB3_COMPRESS_PATTERN)
		return true;

	return false;
}
#else /* !CONFIG_CIFS_COMPRESSION */
static inline int smb_compress(void *unused1, void *unused2, void *unused3)
{
	return -EOPNOTSUPP;
}

static inline bool should_compress(void *unused1, void *unused2)
{
	return false;
}

static inline int smb_compress_alg_valid(__le16 unused1, bool unused2)
{
	return -EOPNOTSUPP;
}
#endif /* !CONFIG_CIFS_COMPRESSION */
#endif /* _SMB_COMPRESS_H */
