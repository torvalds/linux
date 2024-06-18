/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Hardware interface of the NX-GZIP compression accelerator
 *
 * Copyright (C) IBM Corporation, 2020
 *
 * Author: Bulent Abali <abali@us.ibm.com>
 *
 */

#ifndef _NXU_H
#define _NXU_H

#include <stdint.h>
#include <endian.h>
#include "nx.h"

/* deflate */
#define LLSZ   286
#define DSZ    30

/* nx */
#define DHTSZ  18
#define DHT_MAXSZ 288
#define MAX_DDE_COUNT 256

/* util */
#ifdef NXDBG
#define NXPRT(X)	X
#else
#define NXPRT(X)
#endif

#ifdef NXTIMER
#include <sys/platform/ppc.h>
#define NX_CLK(X)	X
#define nx_get_time()	__ppc_get_timebase()
#define nx_get_freq()	__ppc_get_timebase_freq()
#else
#define NX_CLK(X)
#define nx_get_time()  (-1)
#define nx_get_freq()  (-1)
#endif

#define NX_MAX_FAULTS  500

/*
 * Definitions of acronyms used here. See
 * P9 NX Gzip Accelerator User's Manual for details:
 * https://github.com/libnxz/power-gzip/blob/develop/doc/power_nx_gzip_um.pdf
 *
 * adler/crc: 32 bit checksums appended to stream tail
 * ce:       completion extension
 * cpb:      coprocessor parameter block (metadata)
 * crb:      coprocessor request block (command)
 * csb:      coprocessor status block (status)
 * dht:      dynamic huffman table
 * dde:      data descriptor element (address, length)
 * ddl:      list of ddes
 * dh/fh:    dynamic and fixed huffman types
 * fc:       coprocessor function code
 * histlen:  history/dictionary length
 * history:  sliding window of up to 32KB of data
 * lzcount:  Deflate LZ symbol counts
 * rembytecnt: remaining byte count
 * sfbt:     source final block type; last block's type during decomp
 * spbc:     source processed byte count
 * subc:     source unprocessed bit count
 * tebc:     target ending bit count; valid bits in the last byte
 * tpbc:     target processed byte count
 * vas:      virtual accelerator switch; the user mode interface
 */

union nx_qw_t {
	uint32_t word[4];
	uint64_t dword[2];
} __aligned(16);

/*
 * Note: NX registers with fewer than 32 bits are declared by
 * convention as uint32_t variables in unions. If *_offset and *_mask
 * are defined for a variable, then use get_ put_ macros to
 * conveniently access the register fields for endian conversions.
 */

struct nx_dde_t {
	/* Data Descriptor Element, Section 6.4 */
	union {
		uint32_t dde_count;
		/* When dde_count == 0 ddead is a pointer to a data buffer;
		 * ddebc is the buffer length bytes.
		 * When dde_count > 0 dde is an indirect dde; ddead is a
		 * pointer to a contiguous list of direct ddes; ddebc is the
		 * total length of all data pointed to by the list of direct
		 * ddes. Note that only one level of indirection is permitted.
		 * See Section 6.4 of the user manual for additional details.
		 */
	};
	uint32_t ddebc; /* dde byte count */
	uint64_t ddead; /* dde address */
} __aligned(16);

struct nx_csb_t {
	/* Coprocessor Status Block, Section 6.6  */
	union {
		uint32_t csb_v;
		/* Valid bit. v must be set to 0 by the program
		 * before submitting the coprocessor command.
		 * Software can poll for the v bit
		 */

		uint32_t csb_f;
		/* 16B CSB size. Written to 0 by DMA when it writes the CPB */

		uint32_t csb_cs;
		/* cs completion sequence; unused */

		uint32_t csb_cc;
		/* cc completion code; cc != 0 exception occurred */

		uint32_t csb_ce;
		/* ce completion extension */

	};
	uint32_t tpbc;
	/* target processed byte count TPBC */

	uint64_t fsaddr;
	/* Section 6.12.1 CSB NonZero error summary.  FSA Failing storage
	 * address.  Address where error occurred. When available, written
	 * to A field of CSB
	 */
} __aligned(16);

struct nx_ccb_t {
	/* Coprocessor Completion Block, Section 6.7 */

	uint32_t reserved[3];
	union {
		/* When crb.c==0 (no ccb defined) it is reserved;
		 * When crb.c==1 (ccb defined) it is cm
		 */

		uint32_t ccb_cm;
		/* Signal interrupt of crb.c==1 and cm==1 */

		uint32_t word;
		/* generic access to the 32bit word */
	};
} __aligned(16);

struct vas_stamped_crb_t {
	/*
	 * CRB operand of the paste coprocessor instruction is stamped
	 * in quadword 4 with the information shown here as its written
	 * in to the receive FIFO of the coprocessor
	 */

	union {
		uint32_t vas_buf_num;
		/* Verification only vas buffer number which correlates to
		 * the low order bits of the atag in the paste command
		 */

		uint32_t send_wc_id;
		/* Pointer to Send Window Context that provides for NX address
		 * translation information, such as MSR and LPCR bits, job
		 * completion interrupt RA, PSWID, and job utilization counter.
		 */

	};
	union {
		uint32_t recv_wc_id;
		/* Pointer to Receive Window Context. NX uses this to return
		 * credits to a Receive FIFO as entries are dequeued.
		 */

	};
	uint32_t reserved2;
	union {
		uint32_t vas_invalid;
		/* Invalid bit. If this bit is 1 the CRB is discarded by
		 * NX upon fetching from the receive FIFO. If this bit is 0
		 * the CRB is processed normally. The bit is stamped to 0
		 * by VAS and may be written to 1 by hypervisor while
		 * the CRB is in the receive FIFO (in memory).
		 */

	};
};

struct nx_stamped_fault_crb_t {
	/*
	 * A CRB that has a translation fault is stamped by NX in quadword 4
	 * and pasted to the Fault Send Window in VAS.
	 */
	uint64_t fsa;
	union {
		uint32_t nxsf_t;
		uint32_t nxsf_fs;
	};
	uint32_t pswid;
};

union stamped_crb_t {
	struct vas_stamped_crb_t      vas;
	struct nx_stamped_fault_crb_t nx;
};

struct nx_gzip_cpb_t {
	/*
	 * Coprocessor Parameter Block In/Out are used to pass metadata
	 * to/from accelerator.  Tables 6.5 and 6.6 of the user manual.
	 */

	/* CPBInput */

	struct {
		union {
		union nx_qw_t qw0;
			struct {
				uint32_t in_adler;            /* bits 0:31  */
				uint32_t in_crc;              /* bits 32:63 */
				union {
					uint32_t in_histlen;  /* bits 64:75 */
					uint32_t in_subc;     /* bits 93:95 */
				};
				union {
					/* bits 108:111 */
					uint32_t in_sfbt;
					/* bits 112:127 */
					uint32_t in_rembytecnt;
					/* bits 116:127 */
					uint32_t in_dhtlen;
				};
			};
		};
		union {
			union nx_qw_t  in_dht[DHTSZ];	/* qw[1:18]     */
			char in_dht_char[DHT_MAXSZ];	/* byte access  */
		};
		union nx_qw_t  reserved[5];		/* qw[19:23]    */
	};

	/* CPBOutput */

	volatile struct {
		union {
			union nx_qw_t qw24;
			struct {
				uint32_t out_adler;    /* bits 0:31  qw[24] */
				uint32_t out_crc;      /* bits 32:63 qw[24] */
				union {
					/* bits 77:79 qw[24] */
					uint32_t out_tebc;
					/* bits 80:95 qw[24] */
					uint32_t out_subc;
				};
				union {
					/* bits 108:111 qw[24] */
					uint32_t out_sfbt;
					/* bits 112:127 qw[24] */
					uint32_t out_rembytecnt;
					/* bits 116:127 qw[24] */
					uint32_t out_dhtlen;
				};
			};
		};
		union {
			union nx_qw_t  qw25[79];        /* qw[25:103] */
			/* qw[25] compress no lzcounts or wrap */
			uint32_t out_spbc_comp_wrap;
			uint32_t out_spbc_wrap;         /* qw[25] wrap */
			/* qw[25] compress no lzcounts */
			uint32_t out_spbc_comp;
			 /* 286 LL and 30 D symbol counts */
			uint32_t out_lzcount[LLSZ+DSZ];
			struct {
				union nx_qw_t  out_dht[DHTSZ];  /* qw[25:42] */
				/* qw[43] decompress */
				uint32_t out_spbc_decomp;
			};
		};
		/* qw[104] compress with lzcounts */
		uint32_t out_spbc_comp_with_count;
	};
} __aligned(128);

struct nx_gzip_crb_t {
	union {                   /* byte[0:3]   */
		uint32_t gzip_fc;     /* bits[24-31] */
	};
	uint32_t reserved1;       /* byte[4:7]   */
	union {
		uint64_t csb_address; /* byte[8:15]  */
		struct {
			uint32_t reserved2;
			union {
				uint32_t crb_c;
				/* c==0 no ccb defined */

				uint32_t crb_at;
				/* at==0 address type is ignored;
				 * all addrs effective assumed.
				 */

			};
		};
	};
	struct nx_dde_t source_dde;           /* byte[16:31] */
	struct nx_dde_t target_dde;           /* byte[32:47] */
	volatile struct nx_ccb_t ccb;         /* byte[48:63] */
	volatile union {
		/* byte[64:239] shift csb by 128 bytes out of the crb; csb was
		 * in crb earlier; JReilly says csb written with partial inject
		 */
		union nx_qw_t reserved64[11];
		union stamped_crb_t stamp;       /* byte[64:79] */
	};
	volatile struct nx_csb_t csb;
} __aligned(128);

struct nx_gzip_crb_cpb_t {
	struct nx_gzip_crb_t crb;
	struct nx_gzip_cpb_t cpb;
} __aligned(2048);


/*
 * NX hardware convention has the msb bit on the left numbered 0.
 * The defines below has *_offset defined as the right most bit
 * position of a field.  x of size_mask(x) is the field width in bits.
 */

#define size_mask(x)          ((1U<<(x))-1)

/*
 * Offsets and Widths within the containing 32 bits of the various NX
 * gzip hardware registers.  Use the getnn/putnn macros to access
 * these regs
 */

#define dde_count_mask        size_mask(8)
#define dde_count_offset      23

/* CSB */

#define csb_v_mask            size_mask(1)
#define csb_v_offset          0
#define csb_f_mask            size_mask(1)
#define csb_f_offset          6
#define csb_cs_mask           size_mask(8)
#define csb_cs_offset         15
#define csb_cc_mask           size_mask(8)
#define csb_cc_offset         23
#define csb_ce_mask           size_mask(8)
#define csb_ce_offset         31

/* CCB */

#define ccb_cm_mask           size_mask(3)
#define ccb_cm_offset         31

/* VAS stamped CRB fields */

#define vas_buf_num_mask      size_mask(6)
#define vas_buf_num_offset    5
#define send_wc_id_mask       size_mask(16)
#define send_wc_id_offset     31
#define recv_wc_id_mask       size_mask(16)
#define recv_wc_id_offset     31
#define vas_invalid_mask      size_mask(1)
#define vas_invalid_offset    31

/* NX stamped fault CRB fields */

#define nxsf_t_mask           size_mask(1)
#define nxsf_t_offset         23
#define nxsf_fs_mask          size_mask(8)
#define nxsf_fs_offset        31

/* CPB input */

#define in_histlen_mask       size_mask(12)
#define in_histlen_offset     11
#define in_dhtlen_mask        size_mask(12)
#define in_dhtlen_offset      31
#define in_subc_mask          size_mask(3)
#define in_subc_offset        31
#define in_sfbt_mask          size_mask(4)
#define in_sfbt_offset        15
#define in_rembytecnt_mask    size_mask(16)
#define in_rembytecnt_offset  31

/* CPB output */

#define out_tebc_mask         size_mask(3)
#define out_tebc_offset       15
#define out_subc_mask         size_mask(16)
#define out_subc_offset       31
#define out_sfbt_mask         size_mask(4)
#define out_sfbt_offset       15
#define out_rembytecnt_mask   size_mask(16)
#define out_rembytecnt_offset 31
#define out_dhtlen_mask       size_mask(12)
#define out_dhtlen_offset     31

/* CRB */

#define gzip_fc_mask          size_mask(8)
#define gzip_fc_offset        31
#define crb_c_mask            size_mask(1)
#define crb_c_offset          28
#define crb_at_mask           size_mask(1)
#define crb_at_offset         30
#define csb_address_mask      ~(15UL) /* mask off bottom 4b */

/*
 * Access macros for the registers.  Do not access registers directly
 * because of the endian conversion.  P9 processor may run either as
 * Little or Big endian. However the NX coprocessor regs are always
 * big endian.
 * Use the 32 and 64b macros to access respective
 * register sizes.
 * Use nn forms for the register fields shorter than 32 bits.
 */

#define getnn(ST, REG)      ((be32toh(ST.REG) >> (31-REG##_offset)) \
				 & REG##_mask)
#define getpnn(ST, REG)     ((be32toh((ST)->REG) >> (31-REG##_offset)) \
				 & REG##_mask)
#define get32(ST, REG)      (be32toh(ST.REG))
#define getp32(ST, REG)     (be32toh((ST)->REG))
#define get64(ST, REG)      (be64toh(ST.REG))
#define getp64(ST, REG)     (be64toh((ST)->REG))

#define unget32(ST, REG)    (get32(ST, REG) & ~((REG##_mask) \
				<< (31-REG##_offset)))
/* get 32bits less the REG field */

#define ungetp32(ST, REG)   (getp32(ST, REG) & ~((REG##_mask) \
				<< (31-REG##_offset)))
/* get 32bits less the REG field */

#define clear_regs(ST)      memset((void *)(&(ST)), 0, sizeof(ST))
#define clear_dde(ST)       do { ST.dde_count = ST.ddebc = 0; ST.ddead = 0; \
				} while (0)
#define clearp_dde(ST)      do { (ST)->dde_count = (ST)->ddebc = 0; \
				 (ST)->ddead = 0; \
				} while (0)
#define clear_struct(ST)    memset((void *)(&(ST)), 0, sizeof(ST))
#define putnn(ST, REG, X)   (ST.REG = htobe32(unget32(ST, REG) | (((X) \
				 & REG##_mask) << (31-REG##_offset))))
#define putpnn(ST, REG, X)  ((ST)->REG = htobe32(ungetp32(ST, REG) \
				| (((X) & REG##_mask) << (31-REG##_offset))))

#define put32(ST, REG, X)   (ST.REG = htobe32(X))
#define putp32(ST, REG, X)  ((ST)->REG = htobe32(X))
#define put64(ST, REG, X)   (ST.REG = htobe64(X))
#define putp64(ST, REG, X)  ((ST)->REG = htobe64(X))

/*
 * Completion extension ce(0) ce(1) ce(2).  Bits ce(3-7)
 * unused.  Section 6.6 Figure 6.7.
 */

#define get_csb_ce(ST) ((uint32_t)getnn(ST, csb_ce))
#define get_csb_ce_ms3b(ST) (get_csb_ce(ST) >> 5)
#define put_csb_ce_ms3b(ST, X) putnn(ST, csb_ce, ((uint32_t)(X) << 5))

#define CSB_CE_PARTIAL         0x4
#define CSB_CE_TERMINATE       0x2
#define CSB_CE_TPBC_VALID      0x1

#define csb_ce_termination(X)         (!!((X) & CSB_CE_TERMINATE))
/* termination, output buffers may be modified, SPBC/TPBC invalid Fig.6-7 */

#define csb_ce_check_completion(X)    (!csb_ce_termination(X))
/* if not terminated then check full or partial completion */

#define csb_ce_partial_completion(X)  (!!((X) & CSB_CE_PARTIAL))
#define csb_ce_full_completion(X)     (!csb_ce_partial_completion(X))
#define csb_ce_tpbc_valid(X)          (!!((X) & CSB_CE_TPBC_VALID))
/* TPBC indicates successfully stored data count */

#define csb_ce_default_err(X)         csb_ce_termination(X)
/* most error CEs have CE(0)=0 and CE(1)=1 */

#define csb_ce_cc3_partial(X)         csb_ce_partial_completion(X)
/* some CC=3 are partially completed, Table 6-8 */

#define csb_ce_cc64(X)                ((X)&(CSB_CE_PARTIAL \
					| CSB_CE_TERMINATE) == 0)
/* Compression: when TPBC>SPBC then CC=64 Table 6-8; target didn't
 * compress smaller than source.
 */

/* Decompress SFBT combinations Tables 5-3, 6-4, 6-6 */

#define SFBT_BFINAL 0x1
#define SFBT_LIT    0x4
#define SFBT_FHT    0x5
#define SFBT_DHT    0x6
#define SFBT_HDR    0x7

/*
 * NX gzip function codes. Table 6.2.
 * Bits 0:4 are the FC. Bit 5 is used by the DMA controller to
 * select one of the two Byte Count Limits.
 */

#define GZIP_FC_LIMIT_MASK                               0x01
#define GZIP_FC_COMPRESS_FHT                             0x00
#define GZIP_FC_COMPRESS_DHT                             0x02
#define GZIP_FC_COMPRESS_FHT_COUNT                       0x04
#define GZIP_FC_COMPRESS_DHT_COUNT                       0x06
#define GZIP_FC_COMPRESS_RESUME_FHT                      0x08
#define GZIP_FC_COMPRESS_RESUME_DHT                      0x0a
#define GZIP_FC_COMPRESS_RESUME_FHT_COUNT                0x0c
#define GZIP_FC_COMPRESS_RESUME_DHT_COUNT                0x0e
#define GZIP_FC_DECOMPRESS                               0x10
#define GZIP_FC_DECOMPRESS_SINGLE_BLK_N_SUSPEND          0x12
#define GZIP_FC_DECOMPRESS_RESUME                        0x14
#define GZIP_FC_DECOMPRESS_RESUME_SINGLE_BLK_N_SUSPEND   0x16
#define GZIP_FC_WRAP                                     0x1e

#define fc_is_compress(fc)  (((fc) & 0x10) == 0)
#define fc_has_count(fc)    (fc_is_compress(fc) && (((fc) & 0x4) != 0))

/* CSB.CC Error codes */

#define ERR_NX_OK             0
#define ERR_NX_ALIGNMENT      1
#define ERR_NX_OPOVERLAP      2
#define ERR_NX_DATA_LENGTH    3
#define ERR_NX_TRANSLATION    5
#define ERR_NX_PROTECTION     6
#define ERR_NX_EXTERNAL_UE7   7
#define ERR_NX_INVALID_OP     8
#define ERR_NX_PRIVILEGE      9
#define ERR_NX_INTERNAL_UE   10
#define ERR_NX_EXTERN_UE_WR  12
#define ERR_NX_TARGET_SPACE  13
#define ERR_NX_EXCESSIVE_DDE 14
#define ERR_NX_TRANSL_WR     15
#define ERR_NX_PROTECT_WR    16
#define ERR_NX_SUBFUNCTION   17
#define ERR_NX_FUNC_ABORT    18
#define ERR_NX_BYTE_MAX      19
#define ERR_NX_CORRUPT_CRB   20
#define ERR_NX_INVALID_CRB   21
#define ERR_NX_INVALID_DDE   30
#define ERR_NX_SEGMENTED_DDL 31
#define ERR_NX_DDE_OVERFLOW  33
#define ERR_NX_TPBC_GT_SPBC  64
#define ERR_NX_MISSING_CODE  66
#define ERR_NX_INVALID_DIST  67
#define ERR_NX_INVALID_DHT   68
#define ERR_NX_EXTERNAL_UE90 90
#define ERR_NX_WDOG_TIMER   224
#define ERR_NX_AT_FAULT     250
#define ERR_NX_INTR_SERVER  252
#define ERR_NX_UE253        253
#define ERR_NX_NO_HW        254
#define ERR_NX_HUNG_OP      255
#define ERR_NX_END          256

/* initial values for non-resume operations */
#define INIT_CRC   0  /* crc32(0L, Z_NULL, 0) */
#define INIT_ADLER 1  /* adler32(0L, Z_NULL, 0)  adler is initialized to 1 */

/* prototypes */
int nxu_submit_job(struct nx_gzip_crb_cpb_t *c, void *handle);

extern void nxu_sigsegv_handler(int sig, siginfo_t *info, void *ctx);
extern int nxu_touch_pages(void *buf, long buf_len, long page_len, int wr);

/* caller supplies a print buffer 4*sizeof(crb) */

char *nx_crb_str(struct nx_gzip_crb_t *crb, char *prbuf);
char *nx_cpb_str(struct nx_gzip_cpb_t *cpb, char *prbuf);
char *nx_prt_hex(void *cp, int sz, char *prbuf);
char *nx_lzcount_str(struct nx_gzip_cpb_t *cpb, char *prbuf);
char *nx_strerror(int e);

#ifdef NX_SIM
#include <stdio.h>
int nx_sim_init(void *ctx);
int nx_sim_end(void *ctx);
int nxu_run_sim_job(struct nx_gzip_crb_cpb_t *c, void *ctx);
#endif /* NX_SIM */

/* Deflate stream manipulation */

#define set_final_bit(x)	(x |= (unsigned char)1)
#define clr_final_bit(x)	(x &= ~(unsigned char)1)

#define append_empty_fh_blk(p, b) do { *(p) = (2 | (1&(b))); *((p)+1) = 0; \
					} while (0)
/* append 10 bits 0000001b 00...... ;
 * assumes appending starts on a byte boundary; b is the final bit.
 */


#ifdef NX_842

/* 842 Engine */

struct nx_eft_crb_t {
	union {                   /* byte[0:3]   */
		uint32_t eft_fc;      /* bits[29-31] */
	};
	uint32_t reserved1;       /* byte[4:7]   */
	union {
		uint64_t csb_address; /* byte[8:15]  */
		struct {
			uint32_t reserved2;
			union {
				uint32_t crb_c;
				/* c==0 no ccb defined */

				uint32_t crb_at;
				/* at==0 address type is ignored;
				 * all addrs effective assumed.
				 */

			};
		};
	};
	struct nx_dde_t source_dde;           /* byte[16:31] */
	struct nx_dde_t target_dde;           /* byte[32:47] */
	struct nx_ccb_t ccb;                  /* byte[48:63] */
	union {
		union nx_qw_t reserved64[3];     /* byte[64:96] */
	};
	struct nx_csb_t csb;
} __aligned(128);

/* 842 CRB */

#define EFT_FC_MASK                 size_mask(3)
#define EFT_FC_OFFSET               31
#define EFT_FC_COMPRESS             0x0
#define EFT_FC_COMPRESS_WITH_CRC    0x1
#define EFT_FC_DECOMPRESS           0x2
#define EFT_FC_DECOMPRESS_WITH_CRC  0x3
#define EFT_FC_BLK_DATA_MOVE        0x4
#endif /* NX_842 */

#endif /* _NXU_H */
