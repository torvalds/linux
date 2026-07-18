/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Namjae Jeon <linkinjeon@kernel.org>
 */
#ifndef _COMMON_SMB_COMPRESS_H
#define _COMMON_SMB_COMPRESS_H

#include "../smb2pdu.h"

/*
 * SMB3_COMPRESS_NONE is valid only in chained payload headers. It is never
 * negotiated as a compression algorithm.
 */
static __always_inline bool smb_compress_alg_valid(__le16 alg, bool valid_none)
{
	if (alg == SMB3_COMPRESS_NONE)
		return valid_none;

	return alg == SMB3_COMPRESS_LZ77 || alg == SMB3_COMPRESS_PATTERN;
}

int smb_compression_decompress(__le16 alg, bool allow_chained,
			       const void *src, u32 slen, void *dst, u32 dlen);
int smb_compression_compress_chained(__le16 alg, bool allow_pattern,
				     const void *src, u32 slen,
				     void *dst, u32 *dlen);

#endif /* _COMMON_SMB_COMPRESS_H */
