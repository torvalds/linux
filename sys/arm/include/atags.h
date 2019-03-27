/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 M. Warner Losh.
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
 *
 * $FreeBSD$
 */

#ifndef	__MACHINE_ATAGS_H__
#define __MACHINE_ATAGS_H__

/*
 * Linux boot ABI compatable ATAG definitions.  All these structures
 * assume tight packing, but since they are all uint32_t's, I've not
 * bothered to do the usual alignment dance.
 */

#define	LBABI_MAX_COMMAND_LINE  1024

struct arm_lbabi_header
{
	uint32_t	size;		/* Size of this node, including header */
	uint32_t	tag;		/* Node type */
};

#define	ATAG_NONE       0x00000000	/* End of atags list */
#define	ATAG_CORE	0x54410001	/* List must start with ATAG_CORE */
#define	ATAG_MEM	0x54410002	/* Multiple ATAG_MEM nodes possible */
#define	ATAG_VIDEOTEXT	0x54410003	/* Video parameters */
#define	ATAG_RAMDISK	0x54410004	/* Describes the ramdisk parameters */
#define	ATAG_INITRD	0x54410005	/* Deprecated ramdisk -- used va not pa */
#define	ATAG_INITRD2	0x54420005	/* compressed ramdisk image */
#define	ATAG_SERIAL	0x54410006	/* 64-bits of serial number */
#define	ATAG_REVISION	0x54410007	/* Board revision */
#define	ATAG_VIDEOLFB	0x54410008	/* vesafb framebuffer */
#define	ATAG_CMDLINE	0x54410009	/* Command line */

/*
 * ATAG_CORE data
 */
struct arm_lbabi_core
{
	uint32_t flags;			/* bit 0 == read-only */
	uint32_t pagesize;
	uint32_t rootdev;
};

/*
 * ATAG_MEM data -- Can be more than one to describe different
 * banks.
 */
struct arm_lbabi_mem32
{
	uint32_t size;
	uint32_t start;			/* start of physical memory */
};

/*
 * ATAG_INITRD2 - Compressed ramdisk image details
 */
struct arm_lbabi_initrd
{
	uint32_t start;			/* pa of start */
	uint32_t size;			/* How big the ram disk is */
};

/*
 * ATAG_SERIAL - serial number
 */
struct arm_lbabi_serial_number
{
	uint32_t low;
	uint32_t high;
};

/*
 * ATAG_REVISION - board revision
 */
struct arm_lbabi_revision
{
	uint32_t rev;
};

/*
 * ATAG_CMDLINE - Command line from uboot
 */
struct arm_lbabi_command_line
{
	char command[1];		/* Minimum command length */
};

struct arm_lbabi_tag
{
	struct arm_lbabi_header tag_hdr;
	union {
		struct arm_lbabi_core tag_core;
		struct arm_lbabi_mem32 tag_mem;
		struct arm_lbabi_initrd tag_initrd;
		struct arm_lbabi_serial_number tag_sn;
		struct arm_lbabi_revision tag_rev;
		struct arm_lbabi_command_line tag_cmd;
	} u;
};

#define	ATAG_TAG(a)  (a)->tag_hdr.tag
#define ATAG_SIZE(a) ((a)->tag_hdr.size * sizeof(uint32_t))
#define ATAG_NEXT(a) (struct arm_lbabi_tag *)((char *)(a) + ATAG_SIZE(a))

#endif /* __MACHINE_ATAGS_H__ */
