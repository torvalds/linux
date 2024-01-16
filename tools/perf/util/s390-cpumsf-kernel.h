/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Auxtrace support for s390 CPU measurement sampling facility
 *
 *  Copyright IBM Corp. 2018
 *  Author(s): Hendrik Brueckner <brueckner@linux.ibm.com>
 *	       Thomas Richter <tmricht@linux.ibm.com>
 */
#ifndef S390_CPUMSF_KERNEL_H
#define S390_CPUMSF_KERNEL_H

#define	S390_CPUMSF_PAGESZ	4096	/* Size of sample block units */
#define	S390_CPUMSF_DIAG_DEF_FIRST	0x8001	/* Diagnostic entry lowest id */

struct hws_basic_entry {
	unsigned int def:16;	    /* 0-15  Data Entry Format		 */
	unsigned int R:4;	    /* 16-19 reserved			 */
	unsigned int U:4;	    /* 20-23 Number of unique instruct.  */
	unsigned int z:2;	    /* zeros				 */
	unsigned int T:1;	    /* 26 PSW DAT mode			 */
	unsigned int W:1;	    /* 27 PSW wait state		 */
	unsigned int P:1;	    /* 28 PSW Problem state		 */
	unsigned int AS:2;	    /* 29-30 PSW address-space control	 */
	unsigned int I:1;	    /* 31 entry valid or invalid	 */
	unsigned int CL:2;	    /* 32-33 Configuration Level	 */
	unsigned int:14;
	unsigned int prim_asn:16;   /* primary ASN			 */
	unsigned long long ia;	    /* Instruction Address		 */
	unsigned long long gpp;     /* Guest Program Parameter		 */
	unsigned long long hpp;     /* Host Program Parameter		 */
};

struct hws_diag_entry {
	unsigned int def:16;	    /* 0-15  Data Entry Format		 */
	unsigned int R:15;	    /* 16-19 and 20-30 reserved		 */
	unsigned int I:1;	    /* 31 entry valid or invalid	 */
	u8	     data[];	    /* Machine-dependent sample data	 */
};

struct hws_combined_entry {
	struct hws_basic_entry	basic;	/* Basic-sampling data entry */
	struct hws_diag_entry	diag;	/* Diagnostic-sampling data entry */
};

struct hws_trailer_entry {
	union {
		struct {
			unsigned int f:1;	/* 0 - Block Full Indicator   */
			unsigned int a:1;	/* 1 - Alert request control  */
			unsigned int t:1;	/* 2 - Timestamp format	      */
			unsigned int:29;	/* 3 - 31: Reserved	      */
			unsigned int bsdes:16;	/* 32-47: size of basic SDE   */
			unsigned int dsdes:16;	/* 48-63: size of diagnostic SDE */
		};
		unsigned long long flags;	/* 0 - 64: All indicators     */
	};
	unsigned long long overflow;	 /* 64 - sample Overflow count	      */
	unsigned char timestamp[16];	 /* 16 - 31 timestamp		      */
	unsigned long long reserved1;	 /* 32 -Reserved		      */
	unsigned long long reserved2;	 /*				      */
	union {				 /* 48 - reserved for programming use */
		struct {
			unsigned long long clock_base:1; /* in progusage2 */
			unsigned long long progusage1:63;
			unsigned long long progusage2;
		};
		unsigned long long progusage[2];
	};
};

#endif
