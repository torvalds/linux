/*
 * Memory MAP
 * Common header file for blackfin BF561 of processors.
 */

#ifndef _MEM_MAP_561_H_
#define _MEM_MAP_561_H_

#define COREMMR_BASE           0xFFE00000	 /* Core MMRs */
#define SYSMMR_BASE            0xFFC00000	 /* System MMRs */

/* Async Memory Banks */
#define ASYNC_BANK3_BASE	0x2C000000	 /* Async Bank 3 */
#define ASYNC_BANK3_SIZE	0x04000000	/* 64M */
#define ASYNC_BANK2_BASE	0x28000000	 /* Async Bank 2 */
#define ASYNC_BANK2_SIZE	0x04000000	/* 64M */
#define ASYNC_BANK1_BASE	0x24000000	 /* Async Bank 1 */
#define ASYNC_BANK1_SIZE	0x04000000	/* 64M */
#define ASYNC_BANK0_BASE	0x20000000	 /* Async Bank 0 */
#define ASYNC_BANK0_SIZE	0x04000000	/* 64M */

/* Level 1 Memory */

#ifdef CONFIG_BLKFIN_CACHE
#define BLKFIN_ICACHESIZE	(16*1024)
#else
#define BLKFIN_ICACHESIZE	(0*1024)
#endif

/* Memory Map for ADSP-BF561 processors */

#ifdef CONFIG_BF561
#define L1_CODE_START     0xFFA00000
#define L1_DATA_A_START     0xFF800000
#define L1_DATA_B_START     0xFF900000

#define L1_CODE_LENGTH      0x4000

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

/* Level 2 Memory */
#define L2_START		0xFEB00000
#define L2_LENGTH		0x20000

/* Scratch Pad Memory */

#if defined(CONFIG_BF561)
#define L1_SCRATCH_START	0xFFB00000
#define L1_SCRATCH_LENGTH	0x1000
#endif

#endif				/* _MEM_MAP_533_H_ */
