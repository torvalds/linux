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

struct smb_compress_ctx {
	struct TCP_Server_Info *server;
	struct work_struct work;
	struct mid_q_entry *mid;

	void *buf; /* compressed data */
	void *data; /* uncompressed data */
	size_t len;
};

#ifdef CONFIG_CIFS_COMPRESSION
int smb_compress(void *buf, const void *data, size_t *len);

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

/**
 * should_compress() - Determines if a request (write) or the response to a
 *		       request (read) should be compressed.
 * @tcon: tcon of the request is being sent to
 * @buf: buffer with an SMB2 READ/WRITE request
 *
 * Return: true iff:
 * - compression was successfully negotiated with server
 * - server has enabled compression for the share
 * - it's a read or write request
 * - if write, request length is >= SMB_COMPRESS_MIN_LEN
 *
 * Return false otherwise.
 */
static __always_inline bool should_compress(const struct cifs_tcon *tcon, const void *buf)
{
	const struct smb2_hdr *shdr = buf;

	if (!tcon || !tcon->ses || !tcon->ses->server)
		return false;

	if (!tcon->ses->server->compression.enabled)
		return false;

	if (!(tcon->share_flags & SMB2_SHAREFLAG_COMPRESS_DATA))
		return false;

	if (shdr->Command == SMB2_WRITE) {
		const struct smb2_write_req *req = buf;

		return (req->Length >= SMB_COMPRESS_MIN_LEN);
	}

	return (shdr->Command == SMB2_READ);
}
/*
 * #else !CONFIG_CIFS_COMPRESSION ...
 * These routines should not be called when CONFIG_CIFS_COMPRESSION disabled
 * #define smb_compress(arg1, arg2, arg3)		(-EOPNOTSUPP)
 * #define smb_compress_alg_valid(arg1, arg2)	(-EOPNOTSUPP)
 * #define should_compress(arg1, arg2)		(false)
 */
#endif /* !CONFIG_CIFS_COMPRESSION */
#endif /* _SMB_COMPRESS_H */
