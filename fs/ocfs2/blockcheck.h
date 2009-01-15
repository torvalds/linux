/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * blockcheck.h
 *
 * Checksum and ECC codes for the OCFS2 userspace library.
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef OCFS2_BLOCKCHECK_H
#define OCFS2_BLOCKCHECK_H


/* High level block API */
void ocfs2_compute_meta_ecc(struct super_block *sb, void *data,
			    struct ocfs2_block_check *bc);
int ocfs2_validate_meta_ecc(struct super_block *sb, void *data,
			    struct ocfs2_block_check *bc);
void ocfs2_compute_meta_ecc_bhs(struct super_block *sb,
				struct buffer_head **bhs, int nr,
				struct ocfs2_block_check *bc);
int ocfs2_validate_meta_ecc_bhs(struct super_block *sb,
				struct buffer_head **bhs, int nr,
				struct ocfs2_block_check *bc);

/* Lower level API */
void ocfs2_block_check_compute(void *data, size_t blocksize,
			       struct ocfs2_block_check *bc);
int ocfs2_block_check_validate(void *data, size_t blocksize,
			       struct ocfs2_block_check *bc);
void ocfs2_block_check_compute_bhs(struct buffer_head **bhs, int nr,
				   struct ocfs2_block_check *bc);
int ocfs2_block_check_validate_bhs(struct buffer_head **bhs, int nr,
				   struct ocfs2_block_check *bc);

/*
 * Hamming code functions
 */

/*
 * Encoding hamming code parity bits for a buffer.
 *
 * This is the low level encoder function.  It can be called across
 * multiple hunks just like the crc32 code.  'd' is the number of bits
 * _in_this_hunk_.  nr is the bit offset of this hunk.  So, if you had
 * two 512B buffers, you would do it like so:
 *
 * parity = ocfs2_hamming_encode(0, buf1, 512 * 8, 0);
 * parity = ocfs2_hamming_encode(parity, buf2, 512 * 8, 512 * 8);
 *
 * If you just have one buffer, use ocfs2_hamming_encode_block().
 */
u32 ocfs2_hamming_encode(u32 parity, void *data, unsigned int d,
			 unsigned int nr);
/*
 * Fix a buffer with a bit error.  The 'fix' is the original parity
 * xor'd with the parity calculated now.
 *
 * Like ocfs2_hamming_encode(), this can handle hunks.  nr is the bit
 * offset of the current hunk.  If bit to be fixed is not part of the
 * current hunk, this does nothing.
 *
 * If you only have one buffer, use ocfs2_hamming_fix_block().
 */
void ocfs2_hamming_fix(void *data, unsigned int d, unsigned int nr,
		       unsigned int fix);

/* Convenience wrappers for a single buffer of data */
extern u32 ocfs2_hamming_encode_block(void *data, unsigned int blocksize);
extern void ocfs2_hamming_fix_block(void *data, unsigned int blocksize,
				    unsigned int fix);
#endif
