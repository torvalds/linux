/*
 * hfsutils - tools for reading and writing Macintosh HFS volumes
 * Copyright (C) 1996, 1997 Robert Leslie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

typedef struct {
  Integer	sbSig;		/* device signature (should be 0x4552) */
  Integer	sbBlkSize;	/* block size of the device (in bytes) */
  LongInt	sbBlkCount;	/* number of blocks on the device */
  Integer	sbDevType;	/* reserved */
  Integer	sbDevId;	/* reserved */
  LongInt	sbData;		/* reserved */
  Integer	sbDrvrCount;	/* number of driver descriptor entries */
  LongInt	ddBlock;	/* first driver's starting block */
  Integer	ddSize;		/* size of the driver, in 512-byte blocks */
  Integer	ddType;		/* driver operating system type (MacOS = 1) */
  Integer	ddPad[243];	/* additional drivers, if any */
} Block0;

typedef struct {
  Integer	bbID;		/* boot blocks signature */
  LongInt	bbEntry;	/* entry point to boot code */
  Integer	bbVersion;	/* boot blocks version number */
  Integer	bbPageFlags;	/* used internally */
  Str15		bbSysName;	/* System filename */
  Str15		bbShellName;	/* Finder filename */
  Str15		bbDbg1Name;	/* debugger filename */
  Str15		bbDbg2Name;	/* debugger filename */
  Str15		bbScreenName;	/* name of startup screen */
  Str15		bbHelloName;	/* name of startup program */
  Str15		bbScrapName;	/* name of system scrap file */
  Integer	bbCntFCBs;	/* number of FCBs to allocate */
  Integer	bbCntEvts;	/* number of event queue elements */
  LongInt	bb128KSHeap;	/* system heap size on 128K Mac */
  LongInt	bb256KSHeap;	/* used internally */
  LongInt	bbSysHeapSize;	/* system heap size on all machines */
  Integer	filler;		/* reserved */
  LongInt	bbSysHeapExtra;	/* additional system heap space */
  LongInt	bbSysHeapFract;	/* fraction of RAM for system heap */
} BootBlkHdr;

typedef struct {
  Integer	pmSig;		/* partition signature (0x504d or 0x5453) */
  Integer	pmSigPad;	/* reserved */
  LongInt	pmMapBlkCnt;	/* number of blocks in partition map */
  LongInt	pmPyPartStart;	/* first physical block of partition */
  LongInt	pmPartBlkCnt;	/* number of blocks in partition */
  Char		pmPartName[33];	/* partition name */
  Char		pmParType[33];	/* partition type */
  /*
   * Apple_partition_map	partition map
   * Apple_Driver		device driver
   * Apple_Driver43		SCSI Manager 4.3 device driver
   * Apple_MFS			Macintosh 64K ROM filesystem
   * Apple_HFS			Macintosh hierarchical filesystem
   * Apple_Unix_SVR2		Unix filesystem
   * Apple_PRODOS		ProDOS filesystem
   * Apple_Free			unused
   * Apple_Scratch		empty
   */
  LongInt	pmLgDataStart;	/* first logical block of data area */
  LongInt	pmDataCnt;	/* number of blocks in data area */
  LongInt	pmPartStatus;	/* partition status information */
  LongInt	pmLgBootStart;	/* first logical block of boot code */
  LongInt	pmBootSize;	/* size of boot code, in bytes */
  LongInt	pmBootAddr;	/* boot code load address */
  LongInt	pmBootAddr2;	/* reserved */
  LongInt	pmBootEntry;	/* boot code entry point */
  LongInt	pmBootEntry2;	/* reserved */
  LongInt	pmBootCksum;	/* boot code checksum */
  Char		pmProcessor[17];/* processor type */
  Integer	pmPad[188];	/* reserved */
} Partition;

int l_lockvol(hfsvol *);

int l_readblock0(hfsvol *);
int l_readpm(hfsvol *);

int l_readmdb(hfsvol *);
int l_writemdb(hfsvol *);

int l_readvbm(hfsvol *);
int l_writevbm(hfsvol *);
