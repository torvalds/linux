/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024, SUSE LLC
 *
 * Authors: Enzo Matsumiya <ematsumiya@suse.de>
 *
 * Implementation of the LZ77 "plain" compression algorithm, as per MS-XCA spec.
 */
#ifndef _SMB_COMPRESS_LZ77_H
#define _SMB_COMPRESS_LZ77_H

#include <linux/kernel.h>

int lz77_compress(const void *src, u32 slen, void *dst, u32 *dlen);
#endif /* _SMB_COMPRESS_LZ77_H */
