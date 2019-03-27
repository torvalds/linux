/*-
 * Copyright (c) 2006 Tobias Reifenberger
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#include <sys/types.h>

/*
 * Conversion macros for little endian encoded unsigned integers
 * in byte streams to the local unsigned integer format.
 */
#define UINT16BYTES(p) ((uint32_t)((p)[0] + (256*(p)[1])))
#define UINT32BYTES(p) ((uint32_t)((p)[0] + (256*(p)[1]) +		\
	    (65536*(p)[2]) + (16777216*(p)[3])))

/*
 * All following structures are according to:
 *
 * Microsoft Extensible Firmware Initiative FAT32 File System Specification
 * FAT: General Overview of On-Disk Format
 * Version 1.03, December 6, 2000
 * Microsoft Corporation
 */

/*
 * FAT boot sector and boot parameter block for
 * FAT12 and FAT16 volumes
 */
typedef struct fat_bsbpb {
	/* common fields */
	uint8_t BS_jmpBoot[3];
	uint8_t BS_OEMName[8];
	uint8_t BPB_BytsPerSec[2];
	uint8_t BPB_SecPerClus;
	uint8_t BPB_RsvdSecCnt[2];
	uint8_t BPB_NumFATs;
	uint8_t BPB_RootEntCnt[2];
	uint8_t BPB_TotSec16[2];
	uint8_t BPB_Media;
	uint8_t BPB_FATSz16[2];
	uint8_t BPB_SecPerTrack[2];
	uint8_t BPB_NumHeads[2];
	uint8_t BPB_HiddSec[4];
	uint8_t BPB_TotSec32[4];
	/* FAT12/FAT16 only fields */
	uint8_t BS_DrvNum;
	uint8_t BS_Reserved1;
	uint8_t BS_BootSig;
	uint8_t BS_VolID[4];
	uint8_t BS_VolLab[11];
	uint8_t BS_FilSysType[8];
} FAT_BSBPB; /* 62 bytes */

/*
 * FAT boot sector and boot parameter block for
 * FAT32 volumes
 */
typedef struct fat32_bsbpb {
	/* common fields */
	uint8_t BS_jmpBoot[3];
	uint8_t BS_OEMName[8];
	uint8_t BPB_BytsPerSec[2];
	uint8_t BPB_SecPerClus;
	uint8_t BPB_RsvdSecCnt[2];
	uint8_t BPB_NumFATs;
	uint8_t BPB_RootEntCnt[2];
	uint8_t BPB_TotSec16[2];
	uint8_t BPB_Media;
	uint8_t BPB_FATSz16[2];
	uint8_t BPB_SecPerTrack[2];
	uint8_t BPB_NumHeads[2];
	uint8_t BPB_HiddSec[4];
	uint8_t BPB_TotSec32[4];
	/* FAT32 only fields */
	uint8_t BPB_FATSz32[4];
	uint8_t BPB_ExtFlags[2];
	uint8_t BPB_FSVer[2];
	uint8_t BPB_RootClus[4];
	uint8_t BPB_FSInfo[2];
	uint8_t BPB_BkBootSec[2];
	uint8_t BPB_Reserved[12];
	uint8_t BS_DrvNum;
	uint8_t BS_Reserved1;
	uint8_t BS_BootSig;
	uint8_t BS_VolID[4];
	uint8_t BS_VolLab[11];
	uint8_t BS_FilSysType[8];
} FAT32_BSBPB; /* 90 bytes */

/*
 * FAT directory entry structure
 */
#define	FAT_DES_ATTR_READ_ONLY	0x01
#define	FAT_DES_ATTR_HIDDEN	0x02
#define	FAT_DES_ATTR_SYSTEM	0x04
#define	FAT_DES_ATTR_VOLUME_ID	0x08
#define	FAT_DES_ATTR_DIRECTORY	0x10
#define	FAT_DES_ATTR_ARCHIVE	0x20
#define	FAT_DES_ATTR_LONG_NAME	(FAT_DES_ATTR_READ_ONLY |		\
				 FAT_DES_ATTR_HIDDEN |			\
				 FAT_DES_ATTR_SYSTEM |			\
				 FAT_DES_ATTR_VOLUME_ID)

typedef struct fat_des {
	uint8_t DIR_Name[11];
	uint8_t DIR_Attr;
	uint8_t DIR_NTRes;
	uint8_t DIR_CrtTimeTenth;
	uint8_t DIR_CrtTime[2];
	uint8_t DIR_CrtDate[2];
	uint8_t DIR_LstAccDate[2];
	uint8_t DIR_FstClusHI[2];
	uint8_t DIR_WrtTime[2];
	uint8_t DIR_WrtDate[2];
	uint8_t DIR_FstClusLO[2];
	uint8_t DIR_FileSize[4];
} FAT_DES;
