// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SMB2 compression support for ksmbd.
 *
 * Receive and send SMB 3.1.1 compression transforms using the common helpers.
 *
 * Copyright (C) 2026 Namjae Jeon <linkinjeon@kernel.org>
 */
#include <linux/slab.h>

#include "compress.h"
#include "smb_common.h"
#include "../common/compress/lz77.h"

#define SMB_COMPRESS_MIN_LEN	PAGE_SIZE

/**
 * ksmbd_decompress_request() - replace a compressed request with its SMB2 PDU
 * @conn: connection which owns the current RFC1002 request buffer
 *
 * Derive the uncompressed size from the transform variant, enforce ksmbd's
 * normal message limits, and ask the common decoder to validate every payload.
 * On success, replace conn->request_buf with a regular RFC1002-framed SMB2
 * message so the rest of the request path needs no compression awareness.
 *
 * Return: 0 on success, otherwise a negative errno.
 */
int ksmbd_decompress_request(struct ksmbd_conn *conn)
{
	struct smb2_compression_hdr *hdr;
	unsigned int pdu_size = get_rfc1002_len(conn->request_buf);
	u32 orig_size, offset, out_size;
	u32 max_allowed_pdu_size;
	char *buf, *out;
	int rc;

	if (pdu_size < sizeof(struct smb2_compression_hdr))
		return -EINVAL;

	if (conn->dialect != SMB311_PROT_ID ||
	    conn->compress_algorithm == SMB3_COMPRESS_NONE)
		return -EINVAL;

	hdr = smb_get_msg(conn->request_buf);
	if (hdr->ProtocolId != SMB2_COMPRESSION_TRANSFORM_ID)
		return -EINVAL;

	orig_size = le32_to_cpu(hdr->OriginalCompressedSegmentSize);
	if (hdr->Flags == cpu_to_le16(SMB2_COMPRESSION_FLAG_CHAINED)) {
		out_size = orig_size;
	} else {
		offset = le32_to_cpu(hdr->Offset);
		if (offset > pdu_size - sizeof(*hdr) ||
		    check_add_overflow(orig_size, offset, &out_size))
			return -EINVAL;
	}

	max_allowed_pdu_size = SMB3_MAX_MSGSIZE + conn->vals->max_write_size;
	if (out_size > max_allowed_pdu_size ||
	    out_size > MAX_STREAM_PROT_LEN)
		return -EINVAL;

	out = kvmalloc(out_size + 4 + 1, KSMBD_DEFAULT_GFP);
	if (!out)
		return -ENOMEM;

	buf = (char *)hdr;
	*(__be32 *)out = cpu_to_be32(out_size);
	rc = smb_compression_decompress(conn->compress_algorithm,
					conn->compress_chained,
					buf, pdu_size, out + 4, out_size);
	if (rc) {
		kvfree(out);
		return rc;
	}

	kvfree(conn->request_buf);
	conn->request_buf = out;
	return 0;
}

/**
 * ksmbd_compress_response() - compress an eligible ksmbd response
 * @work: request work item containing the response iov
 *
 * Compression transforms describe one contiguous SMB2 message, while ksmbd
 * builds responses from multiple iov entries. Flatten the response first,
 * produce the negotiated transform, and replace the response iov only when the
 * result is smaller than the original message.
 *
 * Encrypted and compound responses are intentionally left unchanged. The
 * caller may still continue sending the original response when this function
 * returns zero.
 *
 * Return: 1 if the response was replaced, 0 if compression was skipped, or a
 * negative errno on failure.
 */
int ksmbd_compress_response(struct ksmbd_work *work)
{
	struct smb2_compression_hdr *chdr;
	struct smb2_hdr *req_hdr;
	u32 src_len, dst_len, compressed_pdu_len, max_dst_len;
	u8 *src = NULL, *out = NULL, *p;
	int i, rc;

	if (!work->compress_response || work->encrypted ||
	    work->conn->compress_algorithm != SMB3_COMPRESS_LZ77)
		return 0;

	req_hdr = smb_get_msg(work->request_buf);
	if (req_hdr->NextCommand || work->next_smb2_rcv_hdr_off ||
	    work->next_smb2_rsp_hdr_off)
		return 0;

	src_len = get_rfc1002_len(work->iov[0].iov_base);
	if (src_len < SMB_COMPRESS_MIN_LEN)
		return 0;

	src = kvmalloc(src_len, KSMBD_DEFAULT_GFP);
	if (!src)
		return -ENOMEM;

	p = src;
	/* iov[0] contains only the RFC1002 length; the SMB2 PDU starts at iov[1]. */
	for (i = 1; i < work->iov_cnt; i++) {
		if (work->iov[i].iov_len > src + src_len - p) {
			rc = -EINVAL;
			goto out;
		}
		memcpy(p, work->iov[i].iov_base, work->iov[i].iov_len);
		p += work->iov[i].iov_len;
	}
	if (p != src + src_len) {
		rc = -EINVAL;
		goto out;
	}

	max_dst_len = smb_lz77_compressed_alloc_size(src_len) +
		sizeof(struct smb2_compression_hdr) +
		3 * sizeof(struct smb2_compression_payload_hdr) +
		2 * sizeof(struct smb2_compression_pattern_v1);
	out = kvzalloc(sizeof(__be32) + max_dst_len,
		       KSMBD_DEFAULT_GFP);
	if (!out) {
		rc = -ENOMEM;
		goto out;
	}

	if (work->conn->compress_chained) {
		dst_len = max_dst_len;
		rc = smb_compression_compress_chained(SMB3_COMPRESS_LZ77,
						      work->conn->compress_pattern,
						      src, src_len,
						      out + sizeof(__be32),
						      &dst_len);
		if (rc == -EMSGSIZE || dst_len >= src_len) {
			rc = 0;
			goto out;
		}
		if (rc)
			goto out;
		compressed_pdu_len = dst_len;
	} else {
		/*
		 * Peers which did not negotiate chained compression still use
		 * the original 16-byte unchained transform format.
		 */
		dst_len = smb_lz77_compressed_alloc_size(src_len);
		rc = smb_lz77_compress(src, src_len,
				       out + sizeof(__be32) + sizeof(*chdr),
				       &dst_len);
		if (rc == -EMSGSIZE ||
		    dst_len + sizeof(*chdr) >= src_len) {
			rc = 0;
			goto out;
		}
		if (rc)
			goto out;

		compressed_pdu_len = sizeof(*chdr) + dst_len;
		chdr = (struct smb2_compression_hdr *)(out + sizeof(__be32));
		chdr->ProtocolId = SMB2_COMPRESSION_TRANSFORM_ID;
		chdr->OriginalCompressedSegmentSize = cpu_to_le32(src_len);
		chdr->CompressionAlgorithm = SMB3_COMPRESS_LZ77;
		chdr->Flags = cpu_to_le16(SMB2_COMPRESSION_FLAG_NONE);
		chdr->Offset = 0;
	}

	*(__be32 *)out = cpu_to_be32(compressed_pdu_len);

	/*
	 * Keep the transform in work->compress_buf until send completion.
	 * Existing response iovs can then be replaced without changing their
	 * individual ownership rules.
	 */
	work->compress_buf = out;
	work->iov[0].iov_base = out;
	work->iov[0].iov_len = sizeof(__be32);
	work->iov[1].iov_base = out + sizeof(__be32);
	work->iov[1].iov_len = compressed_pdu_len;
	work->iov_cnt = 2;
	work->iov_idx = 1;
	out = NULL;
	rc = 1;
out:
	kvfree(out);
	kvfree(src);
	return rc;
}
