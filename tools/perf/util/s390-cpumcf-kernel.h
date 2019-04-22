/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for s390 CPU measurement counter set diagnostic facility
 *
 * Copyright IBM Corp. 2019
   Author(s): Hendrik Brueckner <brueckner@linux.ibm.com>
 *		Thomas Richter <tmricht@linux.ibm.com>
 */
#ifndef S390_CPUMCF_KERNEL_H
#define S390_CPUMCF_KERNEL_H

#define	S390_CPUMCF_DIAG_DEF	0xfeef	/* Counter diagnostic entry ID */
#define	PERF_EVENT_CPUM_CF_DIAG	0xBC000	/* Event: Counter sets */

struct cf_ctrset_entry {	/* CPU-M CF counter set entry (8 byte) */
	unsigned int def:16;	/* 0-15  Data Entry Format */
	unsigned int set:16;	/* 16-23 Counter set identifier */
	unsigned int ctr:16;	/* 24-39 Number of stored counters */
	unsigned int res1:16;	/* 40-63 Reserved */
};

struct cf_trailer_entry {	/* CPU-M CF trailer for raw traces (64 byte) */
	/* 0 - 7 */
	union {
		struct {
			unsigned int clock_base:1;	/* TOD clock base */
			unsigned int speed:1;		/* CPU speed */
			/* Measurement alerts */
			unsigned int mtda:1;	/* Loss of MT ctr. data alert */
			unsigned int caca:1;	/* Counter auth. change alert */
			unsigned int lcda:1;	/* Loss of counter data alert */
		};
		unsigned long flags;		/* 0-63    All indicators */
	};
	/* 8 - 15 */
	unsigned int cfvn:16;			/* 64-79   Ctr First Version */
	unsigned int csvn:16;			/* 80-95   Ctr Second Version */
	unsigned int cpu_speed:32;		/* 96-127  CPU speed */
	/* 16 - 23 */
	unsigned long timestamp;		/* 128-191 Timestamp (TOD) */
	/* 24 - 55 */
	union {
		struct {
			unsigned long progusage1;
			unsigned long progusage2;
			unsigned long progusage3;
			unsigned long tod_base;
		};
		unsigned long progusage[4];
	};
	/* 56 - 63 */
	unsigned int mach_type:16;		/* Machine type */
	unsigned int res1:16;			/* Reserved */
	unsigned int res2:32;			/* Reserved */
};

#define	CPUMF_CTR_SET_BASIC	0	/* Basic Counter Set */
#define	CPUMF_CTR_SET_USER	1	/* Problem-State Counter Set */
#define	CPUMF_CTR_SET_CRYPTO	2	/* Crypto-Activity Counter Set */
#define	CPUMF_CTR_SET_EXT	3	/* Extended Counter Set */
#define	CPUMF_CTR_SET_MT_DIAG	4	/* MT-diagnostic Counter Set */
#endif
