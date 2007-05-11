/*
 * file:         include/asm-blackfin/mach-bf537/mem_map.h
 * based on:
 * author:
 *
 * created:
 * description:
 *	Memory MAP Common header file for blackfin BF537/6/4 of processors.
 * rev:
 *
 * modified:
 *
 * bugs:         enter bugs at http://blackfin.uclinux.org/
 *
 * this program is free software; you can redistribute it and/or modify
 * it under the terms of the gnu general public license as published by
 * the free software foundation; either version 2, or (at your option)
 * any later version.
 *
 * this program is distributed in the hope that it will be useful,
 * but without any warranty; without even the implied warranty of
 * merchantability or fitness for a particular purpose.  see the
 * gnu general public license for more details.
 *
 * you should have received a copy of the gnu general public license
 * along with this program; see the file copying.
 * if not, write to the free software foundation,
 * 59 temple place - suite 330, boston, ma 02111-1307, usa.
 */

#ifndef _MEM_MAP_537_H_
#define _MEM_MAP_537_H_

#define COREMMR_BASE           0xFFE00000	 /* Core MMRs */
#define SYSMMR_BASE            0xFFC00000	 /* System MMRs */

/* Async Memory Banks */
#define ASYNC_BANK3_BASE	0x20300000	 /* Async Bank 3 */
#define ASYNC_BANK3_SIZE	0x00100000	/* 1M */
#define ASYNC_BANK2_BASE	0x20200000	 /* Async Bank 2 */
#define ASYNC_BANK2_SIZE	0x00100000	/* 1M */
#define ASYNC_BANK1_BASE	0x20100000	 /* Async Bank 1 */
#define ASYNC_BANK1_SIZE	0x00100000	/* 1M */
#define ASYNC_BANK0_BASE	0x20000000	 /* Async Bank 0 */
#define ASYNC_BANK0_SIZE	0x00100000	/* 1M */

/* Boot ROM Memory */

#define BOOT_ROM_START		0xEF000000

/* Level 1 Memory */

/* Memory Map for ADSP-BF537 processors */

#ifdef CONFIG_BLKFIN_CACHE
#define BLKFIN_ICACHESIZE	(16*1024)
#else
#define BLKFIN_ICACHESIZE	(0*1024)
#endif


#ifdef CONFIG_BF537
#define L1_CODE_START       0xFFA00000
#define L1_DATA_A_START     0xFF800000
#define L1_DATA_B_START     0xFF900000

#define L1_CODE_LENGTH      0xC000

#ifdef CONFIG_BLKFIN_DCACHE

#ifdef CONFIG_BLKFIN_DCACHE_BANKA
#define DMEM_CNTR (ACACHE_BSRAM | ENDCPLB | PORT_PREF0)
#define L1_DATA_A_LENGTH      (0x8000 - 0x4000)
#define L1_DATA_B_LENGTH      0x8000
#define BLKFIN_DCACHESIZE	(16*1024)
#define BLKFIN_DSUPBANKS	1
#else
#define DMEM_CNTR (ACACHE_BCACHE | ENDCPLB | PORT_PREF0)
#define L1_DATA_A_LENGTH      (0x8000 - 0x4000)
#define L1_DATA_B_LENGTH      (0x8000 - 0x4000)
#define BLKFIN_DCACHESIZE	(32*1024)
#define BLKFIN_DSUPBANKS	2
#endif

#else
#define DMEM_CNTR (ASRAM_BSRAM | ENDCPLB | PORT_PREF0)
#define L1_DATA_A_LENGTH      0x8000
#define L1_DATA_B_LENGTH      0x8000
#define BLKFIN_DCACHESIZE	(0*1024)
#define BLKFIN_DSUPBANKS	0
#endif /*CONFIG_BLKFIN_DCACHE*/

#endif /*CONFIG_BF537*/

/* Memory Map for ADSP-BF536 processors */

#ifdef CONFIG_BF536
#define L1_CODE_START       0xFFA00000
#define L1_DATA_A_START     0xFF804000
#define L1_DATA_B_START     0xFF904000

#define L1_CODE_LENGTH      0xC000


#ifdef CONFIG_BLKFIN_DCACHE

#ifdef CONFIG_BLKFIN_DCACHE_BANKA
#define DMEM_CNTR (ACACHE_BSRAM | ENDCPLB | PORT_PREF0)
#define L1_DATA_A_LENGTH      (0x4000 - 0x4000)
#define L1_DATA_B_LENGTH      0x4000
#define BLKFIN_DCACHESIZE	(16*1024)
#define BLKFIN_DSUPBANKS	1

#else
#define DMEM_CNTR (ACACHE_BCACHE | ENDCPLB | PORT_PREF0)
#define L1_DATA_A_LENGTH      (0x4000 - 0x4000)
#define L1_DATA_B_LENGTH      (0x4000 - 0x4000)
#define BLKFIN_DCACHESIZE	(32*1024)
#define BLKFIN_DSUPBANKS	2
#endif

#else
#define DMEM_CNTR (ASRAM_BSRAM | ENDCPLB | PORT_PREF0)
#define L1_DATA_A_LENGTH      0x4000
#define L1_DATA_B_LENGTH      0x4000
#define BLKFIN_DCACHESIZE	(0*1024)
#define BLKFIN_DSUPBANKS	0
#endif /*CONFIG_BLKFIN_DCACHE*/

#endif

/* Memory Map for ADSP-BF534 processors */

#ifdef CONFIG_BF534
#define L1_CODE_START       0xFFA00000
#define L1_DATA_A_START     0xFF800000
#define L1_DATA_B_START     0xFF900000

#define L1_CODE_LENGTH      0xC000

#ifdef CONFIG_BLKFIN_DCACHE

#ifdef CONFIG_BLKFIN_DCACHE_BANKA
#define DMEM_CNTR (ACACHE_BSRAM | ENDCPLB | PORT_PREF0)
#define L1_DATA_A_LENGTH      (0x8000 - 0x4000)
#define L1_DATA_B_LENGTH      0x8000
#define BLKFIN_DCACHESIZE	(16*1024)
#define BLKFIN_DSUPBANKS	1

#else
#define DMEM_CNTR (ACACHE_BCACHE | ENDCPLB | PORT_PREF0)
#define L1_DATA_A_LENGTH      (0x8000 - 0x4000)
#define L1_DATA_B_LENGTH      (0x8000 - 0x4000)
#define BLKFIN_DCACHESIZE	(32*1024)
#define BLKFIN_DSUPBANKS	2
#endif

#else
#define DMEM_CNTR (ASRAM_BSRAM | ENDCPLB | PORT_PREF0)
#define L1_DATA_A_LENGTH      0x8000
#define L1_DATA_B_LENGTH      0x8000
#define BLKFIN_DCACHESIZE	(0*1024)
#define BLKFIN_DSUPBANKS	0
#endif /*CONFIG_BLKFIN_DCACHE*/

#endif

/* Scratch Pad Memory */

#if defined(CONFIG_BF537) || defined(CONFIG_BF536) || defined(CONFIG_BF534)
#define L1_SCRATCH_START	0xFFB00000
#define L1_SCRATCH_LENGTH	0x1000
#endif

#endif				/* _MEM_MAP_537_H_ */
