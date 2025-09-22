/*	$OpenBSD: disklabel.h,v 1.16 2015/09/30 15:13:54 krw Exp $	*/

/*
 * Copyright (c) 1994 Christopher G. Demetriou
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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

#ifndef _MACHINE_DISKLABEL_H_
#define _MACHINE_DISKLABEL_H_

#define	LABELSECTOR	1	/* sector containing label */
#define	LABELOFFSET	0	/* offset of label in sector */
#define	MAXPARTITIONS	16	/* number of partitions */

/* HFS/DPME */

/* partition map structure from Inside Macintosh: Devices, SCSI Manager
 * pp. 13-14.  The partition map always begins on physical block 1.
 *
 * With the exception of block 0, all blocks on the disk must belong to
 * exactly one partition.  The partition map itself belongs to a partition
 * of type `APPLE_PARTITION_MAP', and is not limited in size by anything
 * other than available disk space.  The partition map is not necessarily
 * the first partition listed.
 */
struct part_map_entry {
#define PART_ENTRY_MAGIC        0x504d
	u_int16_t       pmSig;          /* partition signature */
	u_int16_t       pmSigPad;       /* (reserved) */
	u_int32_t       pmMapBlkCnt;    /* number of blocks in partition map */
	u_int32_t       pmPyPartStart;  /* first physical block of partition */
	u_int32_t       pmPartBlkCnt;   /* number of blocks in partition */
	char            pmPartName[32]; /* partition name */
	char            pmPartType[32]; /* partition type */
	u_int32_t       pmLgDataStart;  /* first logical block of data area */
	u_int32_t       pmDataCnt;      /* number of blocks in data area */
	u_int32_t       pmPartStatus;   /* partition status information */
	u_int32_t       pmLgBootStart;  /* first logical block of boot code */
	u_int32_t       pmBootSize;     /* size of boot code, in bytes */
	u_int32_t       pmBootLoad;     /* boot code load address */
	u_int32_t       pmBootLoad2;    /* (reserved) */
	u_int32_t       pmBootEntry;    /* boot code entry point */
	u_int32_t       pmBootEntry2;   /* (reserved) */
	u_int32_t       pmBootCksum;    /* boot code checksum */
	char            pmProcessor[16]; /* processor type (e.g. "68020") */
	u_int8_t        pmBootArgs[128]; /* A/UX boot arguments */
	/* we do not index the disk image as an array,
	 * leave out the on disk padding
	 */
#if 0
	u_int8_t        pad[248];       /* pad to end of block */
#endif
};

#define PART_TYPE_DRIVER        "APPLE_DRIVER"
#define PART_TYPE_DRIVER43      "APPLE_DRIVER43"
#define PART_TYPE_DRIVERATA     "APPLE_DRIVER_ATA"
#define PART_TYPE_DRIVERIOKIT   "APPLE_DRIVER_IOKIT"
#define PART_TYPE_FWDRIVER      "APPLE_FWDRIVER"
#define PART_TYPE_FWB_COMPONENT "FWB DRIVER COMPONENTS"
#define PART_TYPE_FREE          "APPLE_FREE"
#define PART_TYPE_MAC           "APPLE_HFS"
#define PART_TYPE_OPENBSD       "OPENBSD"

#endif /* _MACHINE_DISKLABEL_H_ */
