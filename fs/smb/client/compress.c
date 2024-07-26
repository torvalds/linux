// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024, SUSE LLC
 *
 * Authors: Enzo Matsumiya <ematsumiya@suse.de>
 *
 * This file implements I/O compression support for SMB2 messages (SMB 3.1.1 only).
 * See compress/ for implementation details of each algorithm.
 *
 * References:
 * MS-SMB2 "3.1.4.4 Compressing the Message"
 * MS-SMB2 "3.1.5.3 Decompressing the Chained Message"
 * MS-XCA - for details of the supported algorithms
 */
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/uio.h>

#include "cifsglob.h"
#include "../common/smb2pdu.h"
#include "cifsproto.h"
#include "smb2proto.h"

#include "compress/lz77.h"
#include "compress.h"

int smb_compress(void *buf, const void *data, size_t *len)
{
	struct smb2_compression_hdr *hdr;
	size_t buf_len, data_len;
	int ret;

	buf_len = sizeof(struct smb2_write_req);
	data_len = *len;
	*len = 0;

	hdr = buf;
	hdr->ProtocolId = SMB2_COMPRESSION_TRANSFORM_ID;
	hdr->OriginalCompressedSegmentSize = cpu_to_le32(data_len);
	hdr->Offset = cpu_to_le32(buf_len);
	hdr->Flags = SMB2_COMPRESSION_FLAG_NONE;
	hdr->CompressionAlgorithm = SMB3_COMPRESS_LZ77;

	/* XXX: add other algs here as they're implemented */
	ret = lz77_compress(data, data_len, buf + SMB_COMPRESS_HDR_LEN + buf_len, &data_len);
	if (!ret)
		*len = SMB_COMPRESS_HDR_LEN + buf_len + data_len;

	return ret;
}
