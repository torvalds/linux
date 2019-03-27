/*-
 * Copyright (c) 2014 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/errno.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "endian.h"
#include "image.h"
#include "format.h"
#include "mkimg.h"

/* Default cluster sizes. */
#define	QCOW1_CLSTR_LOG2SZ	12	/* 4KB */
#define	QCOW2_CLSTR_LOG2SZ	16	/* 64KB */

/* Flag bits in cluster offsets */
#define	QCOW_CLSTR_COMPRESSED	(1ULL << 62)
#define	QCOW_CLSTR_COPIED	(1ULL << 63)

struct qcow_header {
	uint32_t	magic;
#define	QCOW_MAGIC		0x514649fb
	uint32_t	version;
#define	QCOW_VERSION_1		1
#define	QCOW_VERSION_2		2
	uint64_t	path_offset;
	uint32_t	path_length;
	uint32_t	clstr_log2sz;	/* v2 only */
	uint64_t	disk_size;
	union {
		struct {
			uint8_t		clstr_log2sz;
			uint8_t		l2_log2sz;
			uint16_t	_pad;
			uint32_t	encryption;
			uint64_t	l1_offset;
		} v1;
		struct {
			uint32_t	encryption;
			uint32_t	l1_entries;
			uint64_t	l1_offset;
			uint64_t	refcnt_offset;
			uint32_t	refcnt_clstrs;
			uint32_t	snapshot_count;
			uint64_t	snapshot_offset;
		} v2;
	} u;
};

static u_int clstr_log2sz;

static uint64_t
round_clstr(uint64_t ofs)
{
	uint64_t clstrsz;

	clstrsz = 1UL << clstr_log2sz;
	return ((ofs + clstrsz - 1) & ~(clstrsz - 1));
}

static int
qcow_resize(lba_t imgsz, u_int version)
{
	uint64_t imagesz;

	switch (version) {
	case QCOW_VERSION_1:
		clstr_log2sz = QCOW1_CLSTR_LOG2SZ;
		break;
	case QCOW_VERSION_2:
		clstr_log2sz = QCOW2_CLSTR_LOG2SZ;
		break;
	default:
		assert(0);
	}

	imagesz = round_clstr(imgsz * secsz);

	if (verbose)
		fprintf(stderr, "QCOW: image size = %ju, cluster size = %u\n",
		    (uintmax_t)imagesz, (u_int)(1U << clstr_log2sz));

	return (image_set_size(imagesz / secsz));
}

static int
qcow1_resize(lba_t imgsz)
{

	return (qcow_resize(imgsz, QCOW_VERSION_1));
}

static int
qcow2_resize(lba_t imgsz)
{

	return (qcow_resize(imgsz, QCOW_VERSION_2));
}

static int
qcow_write(int fd, u_int version)
{
	struct qcow_header *hdr;
	uint64_t *l1tbl, *l2tbl, *rctbl;
	uint16_t *rcblk;
	uint64_t clstr_imgsz, clstr_l2tbls, clstr_l1tblsz;
	uint64_t clstr_rcblks, clstr_rctblsz;
	uint64_t n, imagesz, nclstrs, ofs, ofsflags;
	lba_t blk, blkofs, blk_imgsz;
	u_int l1clno, l2clno, rcclno;
	u_int blk_clstrsz, refcnt_clstrs;
	u_int clstrsz, l1idx, l2idx;
	int error;

	assert(clstr_log2sz != 0);

	clstrsz = 1U << clstr_log2sz;
	blk_clstrsz = clstrsz / secsz;
	blk_imgsz = image_get_size();
	imagesz = blk_imgsz * secsz;
	clstr_imgsz = imagesz >> clstr_log2sz;
	clstr_l2tbls = round_clstr(clstr_imgsz * 8) >> clstr_log2sz;
	clstr_l1tblsz = round_clstr(clstr_l2tbls * 8) >> clstr_log2sz;
	nclstrs = clstr_imgsz + clstr_l2tbls + clstr_l1tblsz + 1;
	clstr_rcblks = clstr_rctblsz = 0;
	do {
		n = clstr_rcblks + clstr_rctblsz;
		clstr_rcblks = round_clstr((nclstrs + n) * 2) >> clstr_log2sz;
		clstr_rctblsz = round_clstr(clstr_rcblks * 8) >> clstr_log2sz;
	} while (n < (clstr_rcblks + clstr_rctblsz));

	/*
	 * We got all the sizes in clusters. Start the layout.
	 * 0 - header
	 * 1 - L1 table
	 * 2 - RC table (v2 only)
	 * 3 - L2 tables
	 * 4 - RC block (v2 only)
	 * 5 - data
	 */

	l1clno = 1;
	rcclno = 0;
	rctbl = l2tbl = l1tbl = NULL;
	rcblk = NULL;

	hdr = calloc(1, clstrsz);
	if (hdr == NULL)
		return (errno);

	be32enc(&hdr->magic, QCOW_MAGIC);
	be32enc(&hdr->version, version);
	be64enc(&hdr->disk_size, imagesz);
	switch (version) {
	case QCOW_VERSION_1:
		ofsflags = 0;
		l2clno = l1clno + clstr_l1tblsz;
		hdr->u.v1.clstr_log2sz = clstr_log2sz;
		hdr->u.v1.l2_log2sz = clstr_log2sz - 3;
		be64enc(&hdr->u.v1.l1_offset, clstrsz * l1clno);
		break;
	case QCOW_VERSION_2:
		ofsflags = QCOW_CLSTR_COPIED;
		rcclno = l1clno + clstr_l1tblsz;
		l2clno = rcclno + clstr_rctblsz;
		be32enc(&hdr->clstr_log2sz, clstr_log2sz);
		be32enc(&hdr->u.v2.l1_entries, clstr_l2tbls);
		be64enc(&hdr->u.v2.l1_offset, clstrsz * l1clno);
		be64enc(&hdr->u.v2.refcnt_offset, clstrsz * rcclno);
		refcnt_clstrs = round_clstr(clstr_rcblks * 8) >> clstr_log2sz;
		be32enc(&hdr->u.v2.refcnt_clstrs, refcnt_clstrs);
		break;
	default:
		assert(0);
	}

	if (sparse_write(fd, hdr, clstrsz) < 0) {
		error = errno;
		goto out;
	}

	free(hdr);
	hdr = NULL;

	ofs = clstrsz * l2clno;
	nclstrs = 1 + clstr_l1tblsz + clstr_rctblsz;

	l1tbl = calloc(clstr_l1tblsz, clstrsz);
	if (l1tbl == NULL) {
		error = ENOMEM;
		goto out;
	}

	for (n = 0; n < clstr_imgsz; n++) {
		blk = n * blk_clstrsz;
		if (image_data(blk, blk_clstrsz)) {
			nclstrs++;
			l1idx = n >> (clstr_log2sz - 3);
			if (l1tbl[l1idx] == 0) {
				be64enc(l1tbl + l1idx, ofs + ofsflags);
				ofs += clstrsz;
				nclstrs++;
			}
		}
	}

	if (sparse_write(fd, l1tbl, clstrsz * clstr_l1tblsz) < 0) {
		error = errno;
		goto out;
	}

	clstr_rcblks = 0;
	do {
		n = clstr_rcblks;
		clstr_rcblks = round_clstr((nclstrs + n) * 2) >> clstr_log2sz;
	} while (n < clstr_rcblks);

	if (rcclno > 0) {
		rctbl = calloc(clstr_rctblsz, clstrsz);
		if (rctbl == NULL) {
			error = ENOMEM;
			goto out;
		}
		for (n = 0; n < clstr_rcblks; n++) {
			be64enc(rctbl + n, ofs);
			ofs += clstrsz;
			nclstrs++;
		}
		if (sparse_write(fd, rctbl, clstrsz * clstr_rctblsz) < 0) {
			error = errno;
			goto out;
		}
		free(rctbl);
		rctbl = NULL;
	}

	l2tbl = malloc(clstrsz);
	if (l2tbl == NULL) {
		error = ENOMEM;
		goto out;
	}

	for (l1idx = 0; l1idx < clstr_l2tbls; l1idx++) {
		if (l1tbl[l1idx] == 0)
			continue;
		memset(l2tbl, 0, clstrsz);
		blkofs = (lba_t)l1idx * blk_clstrsz * (clstrsz >> 3);
		for (l2idx = 0; l2idx < (clstrsz >> 3); l2idx++) {
			blk = blkofs + (lba_t)l2idx * blk_clstrsz;
			if (blk >= blk_imgsz)
				break;
			if (image_data(blk, blk_clstrsz)) {
				be64enc(l2tbl + l2idx, ofs + ofsflags);
				ofs += clstrsz;
			}
		}
		if (sparse_write(fd, l2tbl, clstrsz) < 0) {
			error = errno;
			goto out;
		}
	}

	free(l2tbl);
	l2tbl = NULL;
	free(l1tbl);
	l1tbl = NULL;

	if (rcclno > 0) {
		rcblk = calloc(clstr_rcblks, clstrsz);
		if (rcblk == NULL) {
			error = ENOMEM;
			goto out;
		}
		for (n = 0; n < nclstrs; n++)
			be16enc(rcblk + n, 1);
		if (sparse_write(fd, rcblk, clstrsz * clstr_rcblks) < 0) {
			error = errno;
			goto out;
		}
		free(rcblk);
		rcblk = NULL;
	}

	error = 0;
	for (n = 0; n < clstr_imgsz; n++) {
		blk = n * blk_clstrsz;
		if (image_data(blk, blk_clstrsz)) {
			error = image_copyout_region(fd, blk, blk_clstrsz);
			if (error)
				break;
		}
	}
	if (!error)
		error = image_copyout_done(fd);

 out:
	if (rcblk != NULL)
		free(rcblk);
	if (l2tbl != NULL)
		free(l2tbl);
	if (rctbl != NULL)
		free(rctbl);
	if (l1tbl != NULL)
		free(l1tbl);
	if (hdr != NULL)
		free(hdr);
	return (error);
}

static int
qcow1_write(int fd)
{

	return (qcow_write(fd, QCOW_VERSION_1));
}

static int
qcow2_write(int fd)
{

	return (qcow_write(fd, QCOW_VERSION_2));
}

static struct mkimg_format qcow1_format = {
	.name = "qcow",
	.description = "QEMU Copy-On-Write, version 1",
	.resize = qcow1_resize,
	.write = qcow1_write,
};
FORMAT_DEFINE(qcow1_format);

static struct mkimg_format qcow2_format = {
	.name = "qcow2",
	.description = "QEMU Copy-On-Write, version 2",
	.resize = qcow2_resize,
	.write = qcow2_write,
};
FORMAT_DEFINE(qcow2_format);
