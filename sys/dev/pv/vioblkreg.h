/*	$OpenBSD: vioblkreg.h,v 1.5 2024/07/26 07:55:23 sf Exp $	*/

/*
 * Copyright (c) 2012 Stefan Fritsch.
 * Copyright (c) 2010 Minoura Makoto.
 * Copyright (c) 1998, 2001 Manuel Bouyer.
 * All rights reserved.
 *
 * This code is based in part on the NetBSD ld_virtio driver and the
 * OpenBSD wd driver.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Configuration registers */
#define VIRTIO_BLK_CONFIG_CAPACITY	0 /* 64bit */
#define VIRTIO_BLK_CONFIG_SIZE_MAX	8 /* 32bit */
#define VIRTIO_BLK_CONFIG_SEG_MAX	12 /* 32bit */
#define VIRTIO_BLK_CONFIG_GEOMETRY_C	16 /* 16bit */
#define VIRTIO_BLK_CONFIG_GEOMETRY_H	18 /* 8bit */
#define VIRTIO_BLK_CONFIG_GEOMETRY_S	19 /* 8bit */
#define VIRTIO_BLK_CONFIG_BLK_SIZE	20 /* 32bit */

/* Feature bits */
#define VIRTIO_BLK_F_BARRIER		(1ULL<<0)
#define VIRTIO_BLK_F_SIZE_MAX		(1ULL<<1)
#define VIRTIO_BLK_F_SEG_MAX		(1ULL<<2)
#define VIRTIO_BLK_F_GEOMETRY		(1ULL<<4)
#define VIRTIO_BLK_F_RO			(1ULL<<5)
#define VIRTIO_BLK_F_BLK_SIZE		(1ULL<<6)
#define VIRTIO_BLK_F_SCSI		(1ULL<<7)
#define VIRTIO_BLK_F_FLUSH		(1ULL<<9)
#define VIRTIO_BLK_F_TOPOLOGY		(1ULL<<10)
#define VIRTIO_BLK_F_CONFIG_WCE		(1ULL<<11)
#define VIRTIO_BLK_F_MQ			(1ULL<<12)
#define VIRTIO_BLK_F_DISCARD		(1ULL<<13)
#define VIRTIO_BLK_F_WRITE_ZEROES	(1ULL<<14)
#define VIRTIO_BLK_F_LIFETIME		(1ULL<<15)
#define VIRTIO_BLK_F_SECURE_ERASE	(1ULL<<16)

/* Command */
#define VIRTIO_BLK_T_IN			0
#define VIRTIO_BLK_T_OUT		1
#define VIRTIO_BLK_T_SCSI_CMD		2
#define VIRTIO_BLK_T_SCSI_CMD_OUT	3
#define VIRTIO_BLK_T_FLUSH		4
#define VIRTIO_BLK_T_FLUSH_OUT		5
#define VIRTIO_BLK_T_GET_ID		8 /* from qemu, not in spec, yet */
#define VIRTIO_BLK_T_BARRIER		0x80000000

/* Status */
#define VIRTIO_BLK_S_OK		0
#define VIRTIO_BLK_S_IOERR	1
#define VIRTIO_BLK_S_UNSUPP	2

#define VIRTIO_BLK_ID_BYTES	20 /* length of serial number */

/* Request header structure */
struct virtio_blk_req_hdr {
	uint32_t	type;	/* VIRTIO_BLK_T_* */
	uint32_t	ioprio;
	uint64_t	sector;
} __packed;
/* 512*virtio_blk_req_hdr.sector byte payload and 1 byte status follows */

#define VIRTIO_BLK_SECTOR_SIZE	512
