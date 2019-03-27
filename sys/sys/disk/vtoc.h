/*-
 * Copyright (c) 2008 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 *
 * $FreeBSD$
 */

#ifndef _SYS_DISK_VTOC_H_
#define	_SYS_DISK_VTOC_H_

#define	VTOC_TAG_UNASSIGNED	0x00
#define	VTOC_TAG_BOOT		0x01
#define	VTOC_TAG_ROOT		0x02
#define	VTOC_TAG_SWAP		0x03
#define	VTOC_TAG_USR		0x04
#define	VTOC_TAG_BACKUP		0x05	/* "c" partition */
#define	VTOC_TAG_STAND		0x06
#define	VTOC_TAG_VAR		0x07
#define	VTOC_TAG_HOME		0x08
#define	VTOC_TAG_ALTSCTR	0x09	/* alternate sector partition */
#define	VTOC_TAG_CACHE		0x0a	/* Solaris cachefs partition */
#define	VTOC_TAG_VXVM_PUB	0x0e	/* VxVM public region */
#define	VTOC_TAG_VXVM_PRIV	0x0f	/* VxVM private region */

/* NetBSD/mips defines this */
#define	VTOC_TAG_NETBSD_FFS	0xff

/* FreeBSD tags: the high byte equals ELFOSABI_FREEBSD */
#define	VTOC_TAG_FREEBSD_SWAP	0x0901
#define	VTOC_TAG_FREEBSD_UFS	0x0902
#define	VTOC_TAG_FREEBSD_VINUM	0x0903
#define	VTOC_TAG_FREEBSD_ZFS	0x0904
#define	VTOC_TAG_FREEBSD_NANDFS	0x0905

#define	VTOC_FLAG_UNMNT		0x01	/* unmountable partition */
#define	VTOC_FLAG_RDONLY	0x10    /* partition is read/only */

#define	VTOC_ASCII_LEN	128
#define	VTOC_BOOTSIZE	8192		/* 16 sectors */
#define	VTOC_MAGIC	0xdabe
#define	VTOC_RAW_PART	2
#define	VTOC_SANITY	0x600ddeee
#define	VTOC_VERSION	1
#define	VTOC_VOLUME_LEN	8

#define	VTOC8_NPARTS	8

struct vtoc8 {
	char		ascii[VTOC_ASCII_LEN];
	uint32_t	version;
	char		volume[VTOC_VOLUME_LEN];
	uint16_t	nparts;
	struct {
		uint16_t	tag;
		uint16_t	flag;
	} part[VTOC8_NPARTS];
	uint16_t	__alignment;
	uint32_t	bootinfo[3];
	uint32_t	sanity;
	uint32_t	reserved[10];
	uint32_t	timestamp[VTOC8_NPARTS];
	uint16_t	wskip;
	uint16_t	rskip;
	char		padding[152];
	uint16_t	rpm;
	uint16_t	physcyls;
	uint16_t	sparesecs;
	uint16_t	spare1[2];
	uint16_t	interleave;
	uint16_t	ncyls;
	uint16_t	altcyls;
	uint16_t	nheads;
	uint16_t	nsecs;
	uint16_t	spare2[2];
	struct {
		uint32_t	cyl;
		uint32_t	nblks;
	} map[VTOC8_NPARTS];
	uint16_t	magic;
	uint16_t	cksum;
};

#ifdef CTASSERT
CTASSERT(sizeof(struct vtoc8) == 512);
#endif

#endif /* _SYS_DISK_VTOC_H_ */
