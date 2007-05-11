#ifndef _ASM_IA64_PAL_H
#define _ASM_IA64_PAL_H

/*
 * Processor Abstraction Layer definitions.
 *
 * This is based on Intel IA-64 Architecture Software Developer's Manual rev 1.0
 * chapter 11 IA-64 Processor Abstraction Layer
 *
 * Copyright (C) 1998-2001 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999 Srinivasa Prasad Thirumalachar <sprasad@sprasad.engr.sgi.com>
 *
 * 99/10/01	davidm	Make sure we pass zero for reserved parameters.
 * 00/03/07	davidm	Updated pal_cache_flush() to be in sync with PAL v2.6.
 * 00/03/23     cfleck  Modified processor min-state save area to match updated PAL & SAL info
 * 00/05/24     eranian Updated to latest PAL spec, fix structures bugs, added
 * 00/05/25	eranian Support for stack calls, and static physical calls
 * 00/06/18	eranian Support for stacked physical calls
 * 06/10/26	rja	Support for Intel Itanium Architecture Software Developer's
 *			Manual Rev 2.2 (Jan 2006)
 */

/*
 * Note that some of these calls use a static-register only calling
 * convention which has nothing to do with the regular calling
 * convention.
 */
#define PAL_CACHE_FLUSH		1	/* flush i/d cache */
#define PAL_CACHE_INFO		2	/* get detailed i/d cache info */
#define PAL_CACHE_INIT		3	/* initialize i/d cache */
#define PAL_CACHE_SUMMARY	4	/* get summary of cache hierarchy */
#define PAL_MEM_ATTRIB		5	/* list supported memory attributes */
#define PAL_PTCE_INFO		6	/* purge TLB info */
#define PAL_VM_INFO		7	/* return supported virtual memory features */
#define PAL_VM_SUMMARY		8	/* return summary on supported vm features */
#define PAL_BUS_GET_FEATURES	9	/* return processor bus interface features settings */
#define PAL_BUS_SET_FEATURES	10	/* set processor bus features */
#define PAL_DEBUG_INFO		11	/* get number of debug registers */
#define PAL_FIXED_ADDR		12	/* get fixed component of processors's directed address */
#define PAL_FREQ_BASE		13	/* base frequency of the platform */
#define PAL_FREQ_RATIOS		14	/* ratio of processor, bus and ITC frequency */
#define PAL_PERF_MON_INFO	15	/* return performance monitor info */
#define PAL_PLATFORM_ADDR	16	/* set processor interrupt block and IO port space addr */
#define PAL_PROC_GET_FEATURES	17	/* get configurable processor features & settings */
#define PAL_PROC_SET_FEATURES	18	/* enable/disable configurable processor features */
#define PAL_RSE_INFO		19	/* return rse information */
#define PAL_VERSION		20	/* return version of PAL code */
#define PAL_MC_CLEAR_LOG	21	/* clear all processor log info */
#define PAL_MC_DRAIN		22	/* drain operations which could result in an MCA */
#define PAL_MC_EXPECTED		23	/* set/reset expected MCA indicator */
#define PAL_MC_DYNAMIC_STATE	24	/* get processor dynamic state */
#define PAL_MC_ERROR_INFO	25	/* get processor MCA info and static state */
#define PAL_MC_RESUME		26	/* Return to interrupted process */
#define PAL_MC_REGISTER_MEM	27	/* Register memory for PAL to use during MCAs and inits */
#define PAL_HALT		28	/* enter the low power HALT state */
#define PAL_HALT_LIGHT		29	/* enter the low power light halt state*/
#define PAL_COPY_INFO		30	/* returns info needed to relocate PAL */
#define PAL_CACHE_LINE_INIT	31	/* init tags & data of cache line */
#define PAL_PMI_ENTRYPOINT	32	/* register PMI memory entry points with the processor */
#define PAL_ENTER_IA_32_ENV	33	/* enter IA-32 system environment */
#define PAL_VM_PAGE_SIZE	34	/* return vm TC and page walker page sizes */

#define PAL_MEM_FOR_TEST	37	/* get amount of memory needed for late processor test */
#define PAL_CACHE_PROT_INFO	38	/* get i/d cache protection info */
#define PAL_REGISTER_INFO	39	/* return AR and CR register information*/
#define PAL_SHUTDOWN		40	/* enter processor shutdown state */
#define PAL_PREFETCH_VISIBILITY	41	/* Make Processor Prefetches Visible */
#define PAL_LOGICAL_TO_PHYSICAL 42	/* returns information on logical to physical processor mapping */
#define PAL_CACHE_SHARED_INFO	43	/* returns information on caches shared by logical processor */
#define PAL_GET_HW_POLICY	48	/* Get current hardware resource sharing policy */
#define PAL_SET_HW_POLICY	49	/* Set current hardware resource sharing policy */

#define PAL_COPY_PAL		256	/* relocate PAL procedures and PAL PMI */
#define PAL_HALT_INFO		257	/* return the low power capabilities of processor */
#define PAL_TEST_PROC		258	/* perform late processor self-test */
#define PAL_CACHE_READ		259	/* read tag & data of cacheline for diagnostic testing */
#define PAL_CACHE_WRITE		260	/* write tag & data of cacheline for diagnostic testing */
#define PAL_VM_TR_READ		261	/* read contents of translation register */
#define PAL_GET_PSTATE		262	/* get the current P-state */
#define PAL_SET_PSTATE		263	/* set the P-state */
#define PAL_BRAND_INFO		274	/* Processor branding information */

#define PAL_GET_PSTATE_TYPE_LASTSET	0
#define PAL_GET_PSTATE_TYPE_AVGANDRESET	1
#define PAL_GET_PSTATE_TYPE_AVGNORESET	2
#define PAL_GET_PSTATE_TYPE_INSTANT	3

#define PAL_MC_ERROR_INJECT	276	/* Injects processor error or returns injection capabilities */

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <asm/fpu.h>

/*
 * Data types needed to pass information into PAL procedures and
 * interpret information returned by them.
 */

/* Return status from the PAL procedure */
typedef s64				pal_status_t;

#define PAL_STATUS_SUCCESS		0	/* No error */
#define PAL_STATUS_UNIMPLEMENTED	(-1)	/* Unimplemented procedure */
#define PAL_STATUS_EINVAL		(-2)	/* Invalid argument */
#define PAL_STATUS_ERROR		(-3)	/* Error */
#define PAL_STATUS_CACHE_INIT_FAIL	(-4)	/* Could not initialize the
						 * specified level and type of
						 * cache without sideeffects
						 * and "restrict" was 1
						 */
#define PAL_STATUS_REQUIRES_MEMORY	(-9)	/* Call requires PAL memory buffer */

/* Processor cache level in the hierarchy */
typedef u64				pal_cache_level_t;
#define PAL_CACHE_LEVEL_L0		0	/* L0 */
#define PAL_CACHE_LEVEL_L1		1	/* L1 */
#define PAL_CACHE_LEVEL_L2		2	/* L2 */


/* Processor cache type at a particular level in the hierarchy */

typedef u64				pal_cache_type_t;
#define PAL_CACHE_TYPE_INSTRUCTION	1	/* Instruction cache */
#define PAL_CACHE_TYPE_DATA		2	/* Data or unified cache */
#define PAL_CACHE_TYPE_INSTRUCTION_DATA	3	/* Both Data & Instruction */


#define PAL_CACHE_FLUSH_INVALIDATE	1	/* Invalidate clean lines */
#define PAL_CACHE_FLUSH_CHK_INTRS	2	/* check for interrupts/mc while flushing */

/* Processor cache line size in bytes  */
typedef int				pal_cache_line_size_t;

/* Processor cache line state */
typedef u64				pal_cache_line_state_t;
#define PAL_CACHE_LINE_STATE_INVALID	0	/* Invalid */
#define PAL_CACHE_LINE_STATE_SHARED	1	/* Shared */
#define PAL_CACHE_LINE_STATE_EXCLUSIVE	2	/* Exclusive */
#define PAL_CACHE_LINE_STATE_MODIFIED	3	/* Modified */

typedef struct pal_freq_ratio {
	u32 den, num;		/* numerator & denominator */
} itc_ratio, proc_ratio;

typedef	union  pal_cache_config_info_1_s {
	struct {
		u64		u		: 1,	/* 0 Unified cache ? */
				at		: 2,	/* 2-1 Cache mem attr*/
				reserved	: 5,	/* 7-3 Reserved */
				associativity	: 8,	/* 16-8 Associativity*/
				line_size	: 8,	/* 23-17 Line size */
				stride		: 8,	/* 31-24 Stride */
				store_latency	: 8,	/*39-32 Store latency*/
				load_latency	: 8,	/* 47-40 Load latency*/
				store_hints	: 8,	/* 55-48 Store hints*/
				load_hints	: 8;	/* 63-56 Load hints */
	} pcci1_bits;
	u64			pcci1_data;
} pal_cache_config_info_1_t;

typedef	union  pal_cache_config_info_2_s {
	struct {
		u32		cache_size;		/*cache size in bytes*/


		u32		alias_boundary	: 8,	/* 39-32 aliased addr
							 * separation for max
							 * performance.
							 */
				tag_ls_bit	: 8,	/* 47-40 LSb of addr*/
				tag_ms_bit	: 8,	/* 55-48 MSb of addr*/
				reserved	: 8;	/* 63-56 Reserved */
	} pcci2_bits;
	u64			pcci2_data;
} pal_cache_config_info_2_t;


typedef struct pal_cache_config_info_s {
	pal_status_t			pcci_status;
	pal_cache_config_info_1_t	pcci_info_1;
	pal_cache_config_info_2_t	pcci_info_2;
	u64				pcci_reserved;
} pal_cache_config_info_t;

#define pcci_ld_hints		pcci_info_1.pcci1_bits.load_hints
#define pcci_st_hints		pcci_info_1.pcci1_bits.store_hints
#define pcci_ld_latency		pcci_info_1.pcci1_bits.load_latency
#define pcci_st_latency		pcci_info_1.pcci1_bits.store_latency
#define pcci_stride		pcci_info_1.pcci1_bits.stride
#define pcci_line_size		pcci_info_1.pcci1_bits.line_size
#define pcci_assoc		pcci_info_1.pcci1_bits.associativity
#define pcci_cache_attr		pcci_info_1.pcci1_bits.at
#define pcci_unified		pcci_info_1.pcci1_bits.u
#define pcci_tag_msb		pcci_info_2.pcci2_bits.tag_ms_bit
#define pcci_tag_lsb		pcci_info_2.pcci2_bits.tag_ls_bit
#define pcci_alias_boundary	pcci_info_2.pcci2_bits.alias_boundary
#define pcci_cache_size		pcci_info_2.pcci2_bits.cache_size



/* Possible values for cache attributes */

#define PAL_CACHE_ATTR_WT		0	/* Write through cache */
#define PAL_CACHE_ATTR_WB		1	/* Write back cache */
#define PAL_CACHE_ATTR_WT_OR_WB		2	/* Either write thru or write
						 * back depending on TLB
						 * memory attributes
						 */


/* Possible values for cache hints */

#define PAL_CACHE_HINT_TEMP_1		0	/* Temporal level 1 */
#define PAL_CACHE_HINT_NTEMP_1		1	/* Non-temporal level 1 */
#define PAL_CACHE_HINT_NTEMP_ALL	3	/* Non-temporal all levels */

/* Processor cache protection  information */
typedef union pal_cache_protection_element_u {
	u32			pcpi_data;
	struct {
		u32		data_bits	: 8, /* # data bits covered by
						      * each unit of protection
						      */

				tagprot_lsb	: 6, /* Least -do- */
				tagprot_msb	: 6, /* Most Sig. tag address
						      * bit that this
						      * protection covers.
						      */
				prot_bits	: 6, /* # of protection bits */
				method		: 4, /* Protection method */
				t_d		: 2; /* Indicates which part
						      * of the cache this
						      * protection encoding
						      * applies.
						      */
	} pcp_info;
} pal_cache_protection_element_t;

#define pcpi_cache_prot_part	pcp_info.t_d
#define pcpi_prot_method	pcp_info.method
#define pcpi_prot_bits		pcp_info.prot_bits
#define pcpi_tagprot_msb	pcp_info.tagprot_msb
#define pcpi_tagprot_lsb	pcp_info.tagprot_lsb
#define pcpi_data_bits		pcp_info.data_bits

/* Processor cache part encodings */
#define PAL_CACHE_PROT_PART_DATA	0	/* Data protection  */
#define PAL_CACHE_PROT_PART_TAG		1	/* Tag  protection */
#define PAL_CACHE_PROT_PART_TAG_DATA	2	/* Tag+data protection (tag is
						 * more significant )
						 */
#define PAL_CACHE_PROT_PART_DATA_TAG	3	/* Data+tag protection (data is
						 * more significant )
						 */
#define PAL_CACHE_PROT_PART_MAX		6


typedef struct pal_cache_protection_info_s {
	pal_status_t			pcpi_status;
	pal_cache_protection_element_t	pcp_info[PAL_CACHE_PROT_PART_MAX];
} pal_cache_protection_info_t;


/* Processor cache protection method encodings */
#define PAL_CACHE_PROT_METHOD_NONE		0	/* No protection */
#define PAL_CACHE_PROT_METHOD_ODD_PARITY	1	/* Odd parity */
#define PAL_CACHE_PROT_METHOD_EVEN_PARITY	2	/* Even parity */
#define PAL_CACHE_PROT_METHOD_ECC		3	/* ECC protection */


/* Processor cache line identification in the hierarchy */
typedef union pal_cache_line_id_u {
	u64			pclid_data;
	struct {
		u64		cache_type	: 8,	/* 7-0 cache type */
				level		: 8,	/* 15-8 level of the
							 * cache in the
							 * hierarchy.
							 */
				way		: 8,	/* 23-16 way in the set
							 */
				part		: 8,	/* 31-24 part of the
							 * cache
							 */
				reserved	: 32;	/* 63-32 is reserved*/
	} pclid_info_read;
	struct {
		u64		cache_type	: 8,	/* 7-0 cache type */
				level		: 8,	/* 15-8 level of the
							 * cache in the
							 * hierarchy.
							 */
				way		: 8,	/* 23-16 way in the set
							 */
				part		: 8,	/* 31-24 part of the
							 * cache
							 */
				mesi		: 8,	/* 39-32 cache line
							 * state
							 */
				start		: 8,	/* 47-40 lsb of data to
							 * invert
							 */
				length		: 8,	/* 55-48 #bits to
							 * invert
							 */
				trigger		: 8;	/* 63-56 Trigger error
							 * by doing a load
							 * after the write
							 */

	} pclid_info_write;
} pal_cache_line_id_u_t;

#define pclid_read_part		pclid_info_read.part
#define pclid_read_way		pclid_info_read.way
#define pclid_read_level	pclid_info_read.level
#define pclid_read_cache_type	pclid_info_read.cache_type

#define pclid_write_trigger	pclid_info_write.trigger
#define pclid_write_length	pclid_info_write.length
#define pclid_write_start	pclid_info_write.start
#define pclid_write_mesi	pclid_info_write.mesi
#define pclid_write_part	pclid_info_write.part
#define pclid_write_way		pclid_info_write.way
#define pclid_write_level	pclid_info_write.level
#define pclid_write_cache_type	pclid_info_write.cache_type

/* Processor cache line part encodings */
#define PAL_CACHE_LINE_ID_PART_DATA		0	/* Data */
#define PAL_CACHE_LINE_ID_PART_TAG		1	/* Tag */
#define PAL_CACHE_LINE_ID_PART_DATA_PROT	2	/* Data protection */
#define PAL_CACHE_LINE_ID_PART_TAG_PROT		3	/* Tag protection */
#define PAL_CACHE_LINE_ID_PART_DATA_TAG_PROT	4	/* Data+tag
							 * protection
							 */
typedef struct pal_cache_line_info_s {
	pal_status_t		pcli_status;		/* Return status of the read cache line
							 * info call.
							 */
	u64			pcli_data;		/* 64-bit data, tag, protection bits .. */
	u64			pcli_data_len;		/* data length in bits */
	pal_cache_line_state_t	pcli_cache_line_state;	/* mesi state */

} pal_cache_line_info_t;


/* Machine Check related crap */

/* Pending event status bits  */
typedef u64					pal_mc_pending_events_t;

#define PAL_MC_PENDING_MCA			(1 << 0)
#define PAL_MC_PENDING_INIT			(1 << 1)

/* Error information type */
typedef u64					pal_mc_info_index_t;

#define PAL_MC_INFO_PROCESSOR			0	/* Processor */
#define PAL_MC_INFO_CACHE_CHECK			1	/* Cache check */
#define PAL_MC_INFO_TLB_CHECK			2	/* Tlb check */
#define PAL_MC_INFO_BUS_CHECK			3	/* Bus check */
#define PAL_MC_INFO_REQ_ADDR			4	/* Requestor address */
#define PAL_MC_INFO_RESP_ADDR			5	/* Responder address */
#define PAL_MC_INFO_TARGET_ADDR			6	/* Target address */
#define PAL_MC_INFO_IMPL_DEP			7	/* Implementation
							 * dependent
							 */

#define PAL_TLB_CHECK_OP_PURGE			8

typedef struct pal_process_state_info_s {
	u64		reserved1	: 2,
			rz		: 1,	/* PAL_CHECK processor
						 * rendezvous
						 * successful.
						 */

			ra		: 1,	/* PAL_CHECK attempted
						 * a rendezvous.
						 */
			me		: 1,	/* Distinct multiple
						 * errors occurred
						 */

			mn		: 1,	/* Min. state save
						 * area has been
						 * registered with PAL
						 */

			sy		: 1,	/* Storage integrity
						 * synched
						 */


			co		: 1,	/* Continuable */
			ci		: 1,	/* MC isolated */
			us		: 1,	/* Uncontained storage
						 * damage.
						 */


			hd		: 1,	/* Non-essential hw
						 * lost (no loss of
						 * functionality)
						 * causing the
						 * processor to run in
						 * degraded mode.
						 */

			tl		: 1,	/* 1 => MC occurred
						 * after an instr was
						 * executed but before
						 * the trap that
						 * resulted from instr
						 * execution was
						 * generated.
						 * (Trap Lost )
						 */
			mi		: 1,	/* More information available
						 * call PAL_MC_ERROR_INFO
						 */
			pi		: 1,	/* Precise instruction pointer */
			pm		: 1,	/* Precise min-state save area */

			dy		: 1,	/* Processor dynamic
						 * state valid
						 */


			in		: 1,	/* 0 = MC, 1 = INIT */
			rs		: 1,	/* RSE valid */
			cm		: 1,	/* MC corrected */
			ex		: 1,	/* MC is expected */
			cr		: 1,	/* Control regs valid*/
			pc		: 1,	/* Perf cntrs valid */
			dr		: 1,	/* Debug regs valid */
			tr		: 1,	/* Translation regs
						 * valid
						 */
			rr		: 1,	/* Region regs valid */
			ar		: 1,	/* App regs valid */
			br		: 1,	/* Branch regs valid */
			pr		: 1,	/* Predicate registers
						 * valid
						 */

			fp		: 1,	/* fp registers valid*/
			b1		: 1,	/* Preserved bank one
						 * general registers
						 * are valid
						 */
			b0		: 1,	/* Preserved bank zero
						 * general registers
						 * are valid
						 */
			gr		: 1,	/* General registers
						 * are valid
						 * (excl. banked regs)
						 */
			dsize		: 16,	/* size of dynamic
						 * state returned
						 * by the processor
						 */

			se		: 1,	/* Shared error.  MCA in a
						   shared structure */
			reserved2	: 10,
			cc		: 1,	/* Cache check */
			tc		: 1,	/* TLB check */
			bc		: 1,	/* Bus check */
			rc		: 1,	/* Register file check */
			uc		: 1;	/* Uarch check */

} pal_processor_state_info_t;

typedef struct pal_cache_check_info_s {
	u64		op		: 4,	/* Type of cache
						 * operation that
						 * caused the machine
						 * check.
						 */
			level		: 2,	/* Cache level */
			reserved1	: 2,
			dl		: 1,	/* Failure in data part
						 * of cache line
						 */
			tl		: 1,	/* Failure in tag part
						 * of cache line
						 */
			dc		: 1,	/* Failure in dcache */
			ic		: 1,	/* Failure in icache */
			mesi		: 3,	/* Cache line state */
			mv		: 1,	/* mesi valid */
			way		: 5,	/* Way in which the
						 * error occurred
						 */
			wiv		: 1,	/* Way field valid */
			reserved2	: 1,
			dp		: 1,	/* Data poisoned on MBE */
			reserved3	: 8,

			index		: 20,	/* Cache line index */
			reserved4	: 2,

			is		: 1,	/* instruction set (1 == ia32) */
			iv		: 1,	/* instruction set field valid */
			pl		: 2,	/* privilege level */
			pv		: 1,	/* privilege level field valid */
			mcc		: 1,	/* Machine check corrected */
			tv		: 1,	/* Target address
						 * structure is valid
						 */
			rq		: 1,	/* Requester identifier
						 * structure is valid
						 */
			rp		: 1,	/* Responder identifier
						 * structure is valid
						 */
			pi		: 1;	/* Precise instruction pointer
						 * structure is valid
						 */
} pal_cache_check_info_t;

typedef struct pal_tlb_check_info_s {

	u64		tr_slot		: 8,	/* Slot# of TR where
						 * error occurred
						 */
			trv		: 1,	/* tr_slot field is valid */
			reserved1	: 1,
			level		: 2,	/* TLB level where failure occurred */
			reserved2	: 4,
			dtr		: 1,	/* Fail in data TR */
			itr		: 1,	/* Fail in inst TR */
			dtc		: 1,	/* Fail in data TC */
			itc		: 1,	/* Fail in inst. TC */
			op		: 4,	/* Cache operation */
			reserved3	: 30,

			is		: 1,	/* instruction set (1 == ia32) */
			iv		: 1,	/* instruction set field valid */
			pl		: 2,	/* privilege level */
			pv		: 1,	/* privilege level field valid */
			mcc		: 1,	/* Machine check corrected */
			tv		: 1,	/* Target address
						 * structure is valid
						 */
			rq		: 1,	/* Requester identifier
						 * structure is valid
						 */
			rp		: 1,	/* Responder identifier
						 * structure is valid
						 */
			pi		: 1;	/* Precise instruction pointer
						 * structure is valid
						 */
} pal_tlb_check_info_t;

typedef struct pal_bus_check_info_s {
	u64		size		: 5,	/* Xaction size */
			ib		: 1,	/* Internal bus error */
			eb		: 1,	/* External bus error */
			cc		: 1,	/* Error occurred
						 * during cache-cache
						 * transfer.
						 */
			type		: 8,	/* Bus xaction type*/
			sev		: 5,	/* Bus error severity*/
			hier		: 2,	/* Bus hierarchy level */
			dp		: 1,	/* Data poisoned on MBE */
			bsi		: 8,	/* Bus error status
						 * info
						 */
			reserved2	: 22,

			is		: 1,	/* instruction set (1 == ia32) */
			iv		: 1,	/* instruction set field valid */
			pl		: 2,	/* privilege level */
			pv		: 1,	/* privilege level field valid */
			mcc		: 1,	/* Machine check corrected */
			tv		: 1,	/* Target address
						 * structure is valid
						 */
			rq		: 1,	/* Requester identifier
						 * structure is valid
						 */
			rp		: 1,	/* Responder identifier
						 * structure is valid
						 */
			pi		: 1;	/* Precise instruction pointer
						 * structure is valid
						 */
} pal_bus_check_info_t;

typedef struct pal_reg_file_check_info_s {
	u64		id		: 4,	/* Register file identifier */
			op		: 4,	/* Type of register
						 * operation that
						 * caused the machine
						 * check.
						 */
			reg_num		: 7,	/* Register number */
			rnv		: 1,	/* reg_num valid */
			reserved2	: 38,

			is		: 1,	/* instruction set (1 == ia32) */
			iv		: 1,	/* instruction set field valid */
			pl		: 2,	/* privilege level */
			pv		: 1,	/* privilege level field valid */
			mcc		: 1,	/* Machine check corrected */
			reserved3	: 3,
			pi		: 1;	/* Precise instruction pointer
						 * structure is valid
						 */
} pal_reg_file_check_info_t;

typedef struct pal_uarch_check_info_s {
	u64		sid		: 5,	/* Structure identification */
			level		: 3,	/* Level of failure */
			array_id	: 4,	/* Array identification */
			op		: 4,	/* Type of
						 * operation that
						 * caused the machine
						 * check.
						 */
			way		: 6,	/* Way of structure */
			wv		: 1,	/* way valid */
			xv		: 1,	/* index valid */
			reserved1	: 8,
			index		: 8,	/* Index or set of the uarch
						 * structure that failed.
						 */
			reserved2	: 24,

			is		: 1,	/* instruction set (1 == ia32) */
			iv		: 1,	/* instruction set field valid */
			pl		: 2,	/* privilege level */
			pv		: 1,	/* privilege level field valid */
			mcc		: 1,	/* Machine check corrected */
			tv		: 1,	/* Target address
						 * structure is valid
						 */
			rq		: 1,	/* Requester identifier
						 * structure is valid
						 */
			rp		: 1,	/* Responder identifier
						 * structure is valid
						 */
			pi		: 1;	/* Precise instruction pointer
						 * structure is valid
						 */
} pal_uarch_check_info_t;

typedef union pal_mc_error_info_u {
	u64				pmei_data;
	pal_processor_state_info_t	pme_processor;
	pal_cache_check_info_t		pme_cache;
	pal_tlb_check_info_t		pme_tlb;
	pal_bus_check_info_t		pme_bus;
	pal_reg_file_check_info_t	pme_reg_file;
	pal_uarch_check_info_t		pme_uarch;
} pal_mc_error_info_t;

#define pmci_proc_unknown_check			pme_processor.uc
#define pmci_proc_bus_check			pme_processor.bc
#define pmci_proc_tlb_check			pme_processor.tc
#define pmci_proc_cache_check			pme_processor.cc
#define pmci_proc_dynamic_state_size		pme_processor.dsize
#define pmci_proc_gpr_valid			pme_processor.gr
#define pmci_proc_preserved_bank0_gpr_valid	pme_processor.b0
#define pmci_proc_preserved_bank1_gpr_valid	pme_processor.b1
#define pmci_proc_fp_valid			pme_processor.fp
#define pmci_proc_predicate_regs_valid		pme_processor.pr
#define pmci_proc_branch_regs_valid		pme_processor.br
#define pmci_proc_app_regs_valid		pme_processor.ar
#define pmci_proc_region_regs_valid		pme_processor.rr
#define pmci_proc_translation_regs_valid	pme_processor.tr
#define pmci_proc_debug_regs_valid		pme_processor.dr
#define pmci_proc_perf_counters_valid		pme_processor.pc
#define pmci_proc_control_regs_valid		pme_processor.cr
#define pmci_proc_machine_check_expected	pme_processor.ex
#define pmci_proc_machine_check_corrected	pme_processor.cm
#define pmci_proc_rse_valid			pme_processor.rs
#define pmci_proc_machine_check_or_init		pme_processor.in
#define pmci_proc_dynamic_state_valid		pme_processor.dy
#define pmci_proc_operation			pme_processor.op
#define pmci_proc_trap_lost			pme_processor.tl
#define pmci_proc_hardware_damage		pme_processor.hd
#define pmci_proc_uncontained_storage_damage	pme_processor.us
#define pmci_proc_machine_check_isolated	pme_processor.ci
#define pmci_proc_continuable			pme_processor.co
#define pmci_proc_storage_intergrity_synced	pme_processor.sy
#define pmci_proc_min_state_save_area_regd	pme_processor.mn
#define	pmci_proc_distinct_multiple_errors	pme_processor.me
#define pmci_proc_pal_attempted_rendezvous	pme_processor.ra
#define pmci_proc_pal_rendezvous_complete	pme_processor.rz


#define pmci_cache_level			pme_cache.level
#define pmci_cache_line_state			pme_cache.mesi
#define pmci_cache_line_state_valid		pme_cache.mv
#define pmci_cache_line_index			pme_cache.index
#define pmci_cache_instr_cache_fail		pme_cache.ic
#define pmci_cache_data_cache_fail		pme_cache.dc
#define pmci_cache_line_tag_fail		pme_cache.tl
#define pmci_cache_line_data_fail		pme_cache.dl
#define pmci_cache_operation			pme_cache.op
#define pmci_cache_way_valid			pme_cache.wv
#define pmci_cache_target_address_valid		pme_cache.tv
#define pmci_cache_way				pme_cache.way
#define pmci_cache_mc				pme_cache.mc

#define pmci_tlb_instr_translation_cache_fail	pme_tlb.itc
#define pmci_tlb_data_translation_cache_fail	pme_tlb.dtc
#define pmci_tlb_instr_translation_reg_fail	pme_tlb.itr
#define pmci_tlb_data_translation_reg_fail	pme_tlb.dtr
#define pmci_tlb_translation_reg_slot		pme_tlb.tr_slot
#define pmci_tlb_mc				pme_tlb.mc

#define pmci_bus_status_info			pme_bus.bsi
#define pmci_bus_req_address_valid		pme_bus.rq
#define pmci_bus_resp_address_valid		pme_bus.rp
#define pmci_bus_target_address_valid		pme_bus.tv
#define pmci_bus_error_severity			pme_bus.sev
#define pmci_bus_transaction_type		pme_bus.type
#define pmci_bus_cache_cache_transfer		pme_bus.cc
#define pmci_bus_transaction_size		pme_bus.size
#define pmci_bus_internal_error			pme_bus.ib
#define pmci_bus_external_error			pme_bus.eb
#define pmci_bus_mc				pme_bus.mc

/*
 * NOTE: this min_state_save area struct only includes the 1KB
 * architectural state save area.  The other 3 KB is scratch space
 * for PAL.
 */

typedef struct pal_min_state_area_s {
	u64	pmsa_nat_bits;		/* nat bits for saved GRs  */
	u64	pmsa_gr[15];		/* GR1	- GR15		   */
	u64	pmsa_bank0_gr[16];	/* GR16 - GR31		   */
	u64	pmsa_bank1_gr[16];	/* GR16 - GR31		   */
	u64	pmsa_pr;		/* predicate registers	   */
	u64	pmsa_br0;		/* branch register 0	   */
	u64	pmsa_rsc;		/* ar.rsc		   */
	u64	pmsa_iip;		/* cr.iip		   */
	u64	pmsa_ipsr;		/* cr.ipsr		   */
	u64	pmsa_ifs;		/* cr.ifs		   */
	u64	pmsa_xip;		/* previous iip		   */
	u64	pmsa_xpsr;		/* previous psr		   */
	u64	pmsa_xfs;		/* previous ifs		   */
	u64	pmsa_br1;		/* branch register 1	   */
	u64	pmsa_reserved[70];	/* pal_min_state_area should total to 1KB */
} pal_min_state_area_t;


struct ia64_pal_retval {
	/*
	 * A zero status value indicates call completed without error.
	 * A negative status value indicates reason of call failure.
	 * A positive status value indicates success but an
	 * informational value should be printed (e.g., "reboot for
	 * change to take effect").
	 */
	s64 status;
	u64 v0;
	u64 v1;
	u64 v2;
};

/*
 * Note: Currently unused PAL arguments are generally labeled
 * "reserved" so the value specified in the PAL documentation
 * (generally 0) MUST be passed.  Reserved parameters are not optional
 * parameters.
 */
extern struct ia64_pal_retval ia64_pal_call_static (u64, u64, u64, u64);
extern struct ia64_pal_retval ia64_pal_call_stacked (u64, u64, u64, u64);
extern struct ia64_pal_retval ia64_pal_call_phys_static (u64, u64, u64, u64);
extern struct ia64_pal_retval ia64_pal_call_phys_stacked (u64, u64, u64, u64);
extern void ia64_save_scratch_fpregs (struct ia64_fpreg *);
extern void ia64_load_scratch_fpregs (struct ia64_fpreg *);

#define PAL_CALL(iprv,a0,a1,a2,a3) do {			\
	struct ia64_fpreg fr[6];			\
	ia64_save_scratch_fpregs(fr);			\
	iprv = ia64_pal_call_static(a0, a1, a2, a3);	\
	ia64_load_scratch_fpregs(fr);			\
} while (0)

#define PAL_CALL_STK(iprv,a0,a1,a2,a3) do {		\
	struct ia64_fpreg fr[6];			\
	ia64_save_scratch_fpregs(fr);			\
	iprv = ia64_pal_call_stacked(a0, a1, a2, a3);	\
	ia64_load_scratch_fpregs(fr);			\
} while (0)

#define PAL_CALL_PHYS(iprv,a0,a1,a2,a3) do {			\
	struct ia64_fpreg fr[6];				\
	ia64_save_scratch_fpregs(fr);				\
	iprv = ia64_pal_call_phys_static(a0, a1, a2, a3);	\
	ia64_load_scratch_fpregs(fr);				\
} while (0)

#define PAL_CALL_PHYS_STK(iprv,a0,a1,a2,a3) do {		\
	struct ia64_fpreg fr[6];				\
	ia64_save_scratch_fpregs(fr);				\
	iprv = ia64_pal_call_phys_stacked(a0, a1, a2, a3);	\
	ia64_load_scratch_fpregs(fr);				\
} while (0)

typedef int (*ia64_pal_handler) (u64, ...);
extern ia64_pal_handler ia64_pal;
extern void ia64_pal_handler_init (void *);

extern ia64_pal_handler ia64_pal;

extern pal_cache_config_info_t		l0d_cache_config_info;
extern pal_cache_config_info_t		l0i_cache_config_info;
extern pal_cache_config_info_t		l1_cache_config_info;
extern pal_cache_config_info_t		l2_cache_config_info;

extern pal_cache_protection_info_t	l0d_cache_protection_info;
extern pal_cache_protection_info_t	l0i_cache_protection_info;
extern pal_cache_protection_info_t	l1_cache_protection_info;
extern pal_cache_protection_info_t	l2_cache_protection_info;

extern pal_cache_config_info_t		pal_cache_config_info_get(pal_cache_level_t,
								  pal_cache_type_t);

extern pal_cache_protection_info_t	pal_cache_protection_info_get(pal_cache_level_t,
								      pal_cache_type_t);


extern void				pal_error(int);


/* Useful wrappers for the current list of pal procedures */

typedef union pal_bus_features_u {
	u64	pal_bus_features_val;
	struct {
		u64	pbf_reserved1				:	29;
		u64	pbf_req_bus_parking			:	1;
		u64	pbf_bus_lock_mask			:	1;
		u64	pbf_enable_half_xfer_rate		:	1;
		u64	pbf_reserved2				:	20;
		u64	pbf_enable_shared_line_replace		:	1;
		u64	pbf_enable_exclusive_line_replace	:	1;
		u64	pbf_disable_xaction_queueing		:	1;
		u64	pbf_disable_resp_err_check		:	1;
		u64	pbf_disable_berr_check			:	1;
		u64	pbf_disable_bus_req_internal_err_signal	:	1;
		u64	pbf_disable_bus_req_berr_signal		:	1;
		u64	pbf_disable_bus_init_event_check	:	1;
		u64	pbf_disable_bus_init_event_signal	:	1;
		u64	pbf_disable_bus_addr_err_check		:	1;
		u64	pbf_disable_bus_addr_err_signal		:	1;
		u64	pbf_disable_bus_data_err_check		:	1;
	} pal_bus_features_s;
} pal_bus_features_u_t;

extern void pal_bus_features_print (u64);

/* Provide information about configurable processor bus features */
static inline s64
ia64_pal_bus_get_features (pal_bus_features_u_t *features_avail,
			   pal_bus_features_u_t *features_status,
			   pal_bus_features_u_t *features_control)
{
	struct ia64_pal_retval iprv;
	PAL_CALL_PHYS(iprv, PAL_BUS_GET_FEATURES, 0, 0, 0);
	if (features_avail)
		features_avail->pal_bus_features_val = iprv.v0;
	if (features_status)
		features_status->pal_bus_features_val = iprv.v1;
	if (features_control)
		features_control->pal_bus_features_val = iprv.v2;
	return iprv.status;
}

/* Enables/disables specific processor bus features */
static inline s64
ia64_pal_bus_set_features (pal_bus_features_u_t feature_select)
{
	struct ia64_pal_retval iprv;
	PAL_CALL_PHYS(iprv, PAL_BUS_SET_FEATURES, feature_select.pal_bus_features_val, 0, 0);
	return iprv.status;
}

/* Get detailed cache information */
static inline s64
ia64_pal_cache_config_info (u64 cache_level, u64 cache_type, pal_cache_config_info_t *conf)
{
	struct ia64_pal_retval iprv;

	PAL_CALL(iprv, PAL_CACHE_INFO, cache_level, cache_type, 0);

	if (iprv.status == 0) {
		conf->pcci_status                 = iprv.status;
		conf->pcci_info_1.pcci1_data      = iprv.v0;
		conf->pcci_info_2.pcci2_data      = iprv.v1;
		conf->pcci_reserved               = iprv.v2;
	}
	return iprv.status;

}

/* Get detailed cche protection information */
static inline s64
ia64_pal_cache_prot_info (u64 cache_level, u64 cache_type, pal_cache_protection_info_t *prot)
{
	struct ia64_pal_retval iprv;

	PAL_CALL(iprv, PAL_CACHE_PROT_INFO, cache_level, cache_type, 0);

	if (iprv.status == 0) {
		prot->pcpi_status           = iprv.status;
		prot->pcp_info[0].pcpi_data = iprv.v0 & 0xffffffff;
		prot->pcp_info[1].pcpi_data = iprv.v0 >> 32;
		prot->pcp_info[2].pcpi_data = iprv.v1 & 0xffffffff;
		prot->pcp_info[3].pcpi_data = iprv.v1 >> 32;
		prot->pcp_info[4].pcpi_data = iprv.v2 & 0xffffffff;
		prot->pcp_info[5].pcpi_data = iprv.v2 >> 32;
	}
	return iprv.status;
}

/*
 * Flush the processor instruction or data caches.  *PROGRESS must be
 * initialized to zero before calling this for the first time..
 */
static inline s64
ia64_pal_cache_flush (u64 cache_type, u64 invalidate, u64 *progress, u64 *vector)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_CACHE_FLUSH, cache_type, invalidate, *progress);
	if (vector)
		*vector = iprv.v0;
	*progress = iprv.v1;
	return iprv.status;
}


/* Initialize the processor controlled caches */
static inline s64
ia64_pal_cache_init (u64 level, u64 cache_type, u64 rest)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_CACHE_INIT, level, cache_type, rest);
	return iprv.status;
}

/* Initialize the tags and data of a data or unified cache line of
 * processor controlled cache to known values without the availability
 * of backing memory.
 */
static inline s64
ia64_pal_cache_line_init (u64 physical_addr, u64 data_value)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_CACHE_LINE_INIT, physical_addr, data_value, 0);
	return iprv.status;
}


/* Read the data and tag of a processor controlled cache line for diags */
static inline s64
ia64_pal_cache_read (pal_cache_line_id_u_t line_id, u64 physical_addr)
{
	struct ia64_pal_retval iprv;
	PAL_CALL_PHYS_STK(iprv, PAL_CACHE_READ, line_id.pclid_data,
				physical_addr, 0);
	return iprv.status;
}

/* Return summary information about the hierarchy of caches controlled by the processor */
static inline s64
ia64_pal_cache_summary (u64 *cache_levels, u64 *unique_caches)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_CACHE_SUMMARY, 0, 0, 0);
	if (cache_levels)
		*cache_levels = iprv.v0;
	if (unique_caches)
		*unique_caches = iprv.v1;
	return iprv.status;
}

/* Write the data and tag of a processor-controlled cache line for diags */
static inline s64
ia64_pal_cache_write (pal_cache_line_id_u_t line_id, u64 physical_addr, u64 data)
{
	struct ia64_pal_retval iprv;
	PAL_CALL_PHYS_STK(iprv, PAL_CACHE_WRITE, line_id.pclid_data,
				physical_addr, data);
	return iprv.status;
}


/* Return the parameters needed to copy relocatable PAL procedures from ROM to memory */
static inline s64
ia64_pal_copy_info (u64 copy_type, u64 num_procs, u64 num_iopics,
		    u64 *buffer_size, u64 *buffer_align)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_COPY_INFO, copy_type, num_procs, num_iopics);
	if (buffer_size)
		*buffer_size = iprv.v0;
	if (buffer_align)
		*buffer_align = iprv.v1;
	return iprv.status;
}

/* Copy relocatable PAL procedures from ROM to memory */
static inline s64
ia64_pal_copy_pal (u64 target_addr, u64 alloc_size, u64 processor, u64 *pal_proc_offset)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_COPY_PAL, target_addr, alloc_size, processor);
	if (pal_proc_offset)
		*pal_proc_offset = iprv.v0;
	return iprv.status;
}

/* Return the number of instruction and data debug register pairs */
static inline s64
ia64_pal_debug_info (u64 *inst_regs,  u64 *data_regs)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_DEBUG_INFO, 0, 0, 0);
	if (inst_regs)
		*inst_regs = iprv.v0;
	if (data_regs)
		*data_regs = iprv.v1;

	return iprv.status;
}

#ifdef TBD
/* Switch from IA64-system environment to IA-32 system environment */
static inline s64
ia64_pal_enter_ia32_env (ia32_env1, ia32_env2, ia32_env3)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_ENTER_IA_32_ENV, ia32_env1, ia32_env2, ia32_env3);
	return iprv.status;
}
#endif

/* Get unique geographical address of this processor on its bus */
static inline s64
ia64_pal_fixed_addr (u64 *global_unique_addr)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_FIXED_ADDR, 0, 0, 0);
	if (global_unique_addr)
		*global_unique_addr = iprv.v0;
	return iprv.status;
}

/* Get base frequency of the platform if generated by the processor */
static inline s64
ia64_pal_freq_base (u64 *platform_base_freq)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_FREQ_BASE, 0, 0, 0);
	if (platform_base_freq)
		*platform_base_freq = iprv.v0;
	return iprv.status;
}

/*
 * Get the ratios for processor frequency, bus frequency and interval timer to
 * to base frequency of the platform
 */
static inline s64
ia64_pal_freq_ratios (struct pal_freq_ratio *proc_ratio, struct pal_freq_ratio *bus_ratio,
		      struct pal_freq_ratio *itc_ratio)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_FREQ_RATIOS, 0, 0, 0);
	if (proc_ratio)
		*(u64 *)proc_ratio = iprv.v0;
	if (bus_ratio)
		*(u64 *)bus_ratio = iprv.v1;
	if (itc_ratio)
		*(u64 *)itc_ratio = iprv.v2;
	return iprv.status;
}

/*
 * Get the current hardware resource sharing policy of the processor
 */
static inline s64
ia64_pal_get_hw_policy (u64 proc_num, u64 *cur_policy, u64 *num_impacted,
			u64 *la)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_GET_HW_POLICY, proc_num, 0, 0);
	if (cur_policy)
		*cur_policy = iprv.v0;
	if (num_impacted)
		*num_impacted = iprv.v1;
	if (la)
		*la = iprv.v2;
	return iprv.status;
}

/* Make the processor enter HALT or one of the implementation dependent low
 * power states where prefetching and execution are suspended and cache and
 * TLB coherency is not maintained.
 */
static inline s64
ia64_pal_halt (u64 halt_state)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_HALT, halt_state, 0, 0);
	return iprv.status;
}

typedef union pal_power_mgmt_info_u {
	u64			ppmi_data;
	struct {
	       u64		exit_latency		: 16,
				entry_latency		: 16,
				power_consumption	: 28,
				im			: 1,
				co			: 1,
				reserved		: 2;
	} pal_power_mgmt_info_s;
} pal_power_mgmt_info_u_t;

/* Return information about processor's optional power management capabilities. */
static inline s64
ia64_pal_halt_info (pal_power_mgmt_info_u_t *power_buf)
{
	struct ia64_pal_retval iprv;
	PAL_CALL_STK(iprv, PAL_HALT_INFO, (unsigned long) power_buf, 0, 0);
	return iprv.status;
}

/* Get the current P-state information */
static inline s64
ia64_pal_get_pstate (u64 *pstate_index, unsigned long type)
{
	struct ia64_pal_retval iprv;
	PAL_CALL_STK(iprv, PAL_GET_PSTATE, type, 0, 0);
	*pstate_index = iprv.v0;
	return iprv.status;
}

/* Set the P-state */
static inline s64
ia64_pal_set_pstate (u64 pstate_index)
{
	struct ia64_pal_retval iprv;
	PAL_CALL_STK(iprv, PAL_SET_PSTATE, pstate_index, 0, 0);
	return iprv.status;
}

/* Processor branding information*/
static inline s64
ia64_pal_get_brand_info (char *brand_info)
{
	struct ia64_pal_retval iprv;
	PAL_CALL_STK(iprv, PAL_BRAND_INFO, 0, (u64)brand_info, 0);
	return iprv.status;
}

/* Cause the processor to enter LIGHT HALT state, where prefetching and execution are
 * suspended, but cache and TLB coherency is maintained.
 */
static inline s64
ia64_pal_halt_light (void)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_HALT_LIGHT, 0, 0, 0);
	return iprv.status;
}

/* Clear all the processor error logging   registers and reset the indicator that allows
 * the error logging registers to be written. This procedure also checks the pending
 * machine check bit and pending INIT bit and reports their states.
 */
static inline s64
ia64_pal_mc_clear_log (u64 *pending_vector)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_MC_CLEAR_LOG, 0, 0, 0);
	if (pending_vector)
		*pending_vector = iprv.v0;
	return iprv.status;
}

/* Ensure that all outstanding transactions in a processor are completed or that any
 * MCA due to thes outstanding transaction is taken.
 */
static inline s64
ia64_pal_mc_drain (void)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_MC_DRAIN, 0, 0, 0);
	return iprv.status;
}

/* Return the machine check dynamic processor state */
static inline s64
ia64_pal_mc_dynamic_state (u64 offset, u64 *size, u64 *pds)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_MC_DYNAMIC_STATE, offset, 0, 0);
	if (size)
		*size = iprv.v0;
	if (pds)
		*pds = iprv.v1;
	return iprv.status;
}

/* Return processor machine check information */
static inline s64
ia64_pal_mc_error_info (u64 info_index, u64 type_index, u64 *size, u64 *error_info)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_MC_ERROR_INFO, info_index, type_index, 0);
	if (size)
		*size = iprv.v0;
	if (error_info)
		*error_info = iprv.v1;
	return iprv.status;
}

/* Injects the requested processor error or returns info on
 * supported injection capabilities for current processor implementation
 */
static inline s64
ia64_pal_mc_error_inject_phys (u64 err_type_info, u64 err_struct_info,
			u64 err_data_buffer, u64 *capabilities, u64 *resources)
{
	struct ia64_pal_retval iprv;
	PAL_CALL_PHYS_STK(iprv, PAL_MC_ERROR_INJECT, err_type_info,
			  err_struct_info, err_data_buffer);
	if (capabilities)
		*capabilities= iprv.v0;
	if (resources)
		*resources= iprv.v1;
	return iprv.status;
}

static inline s64
ia64_pal_mc_error_inject_virt (u64 err_type_info, u64 err_struct_info,
			u64 err_data_buffer, u64 *capabilities, u64 *resources)
{
	struct ia64_pal_retval iprv;
	PAL_CALL_STK(iprv, PAL_MC_ERROR_INJECT, err_type_info,
			  err_struct_info, err_data_buffer);
	if (capabilities)
		*capabilities= iprv.v0;
	if (resources)
		*resources= iprv.v1;
	return iprv.status;
}

/* Inform PALE_CHECK whether a machine check is expected so that PALE_CHECK willnot
 * attempt to correct any expected machine checks.
 */
static inline s64
ia64_pal_mc_expected (u64 expected, u64 *previous)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_MC_EXPECTED, expected, 0, 0);
	if (previous)
		*previous = iprv.v0;
	return iprv.status;
}

/* Register a platform dependent location with PAL to which it can save
 * minimal processor state in the event of a machine check or initialization
 * event.
 */
static inline s64
ia64_pal_mc_register_mem (u64 physical_addr)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_MC_REGISTER_MEM, physical_addr, 0, 0);
	return iprv.status;
}

/* Restore minimal architectural processor state, set CMC interrupt if necessary
 * and resume execution
 */
static inline s64
ia64_pal_mc_resume (u64 set_cmci, u64 save_ptr)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_MC_RESUME, set_cmci, save_ptr, 0);
	return iprv.status;
}

/* Return the memory attributes implemented by the processor */
static inline s64
ia64_pal_mem_attrib (u64 *mem_attrib)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_MEM_ATTRIB, 0, 0, 0);
	if (mem_attrib)
		*mem_attrib = iprv.v0 & 0xff;
	return iprv.status;
}

/* Return the amount of memory needed for second phase of processor
 * self-test and the required alignment of memory.
 */
static inline s64
ia64_pal_mem_for_test (u64 *bytes_needed, u64 *alignment)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_MEM_FOR_TEST, 0, 0, 0);
	if (bytes_needed)
		*bytes_needed = iprv.v0;
	if (alignment)
		*alignment = iprv.v1;
	return iprv.status;
}

typedef union pal_perf_mon_info_u {
	u64			  ppmi_data;
	struct {
	       u64		generic		: 8,
				width		: 8,
				cycles		: 8,
				retired		: 8,
				reserved	: 32;
	} pal_perf_mon_info_s;
} pal_perf_mon_info_u_t;

/* Return the performance monitor information about what can be counted
 * and how to configure the monitors to count the desired events.
 */
static inline s64
ia64_pal_perf_mon_info (u64 *pm_buffer, pal_perf_mon_info_u_t *pm_info)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_PERF_MON_INFO, (unsigned long) pm_buffer, 0, 0);
	if (pm_info)
		pm_info->ppmi_data = iprv.v0;
	return iprv.status;
}

/* Specifies the physical address of the processor interrupt block
 * and I/O port space.
 */
static inline s64
ia64_pal_platform_addr (u64 type, u64 physical_addr)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_PLATFORM_ADDR, type, physical_addr, 0);
	return iprv.status;
}

/* Set the SAL PMI entrypoint in memory */
static inline s64
ia64_pal_pmi_entrypoint (u64 sal_pmi_entry_addr)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_PMI_ENTRYPOINT, sal_pmi_entry_addr, 0, 0);
	return iprv.status;
}

struct pal_features_s;
/* Provide information about configurable processor features */
static inline s64
ia64_pal_proc_get_features (u64 *features_avail,
			    u64 *features_status,
			    u64 *features_control)
{
	struct ia64_pal_retval iprv;
	PAL_CALL_PHYS(iprv, PAL_PROC_GET_FEATURES, 0, 0, 0);
	if (iprv.status == 0) {
		*features_avail   = iprv.v0;
		*features_status  = iprv.v1;
		*features_control = iprv.v2;
	}
	return iprv.status;
}

/* Enable/disable processor dependent features */
static inline s64
ia64_pal_proc_set_features (u64 feature_select)
{
	struct ia64_pal_retval iprv;
	PAL_CALL_PHYS(iprv, PAL_PROC_SET_FEATURES, feature_select, 0, 0);
	return iprv.status;
}

/*
 * Put everything in a struct so we avoid the global offset table whenever
 * possible.
 */
typedef struct ia64_ptce_info_s {
	u64		base;
	u32		count[2];
	u32		stride[2];
} ia64_ptce_info_t;

/* Return the information required for the architected loop used to purge
 * (initialize) the entire TC
 */
static inline s64
ia64_get_ptce (ia64_ptce_info_t *ptce)
{
	struct ia64_pal_retval iprv;

	if (!ptce)
		return -1;

	PAL_CALL(iprv, PAL_PTCE_INFO, 0, 0, 0);
	if (iprv.status == 0) {
		ptce->base = iprv.v0;
		ptce->count[0] = iprv.v1 >> 32;
		ptce->count[1] = iprv.v1 & 0xffffffff;
		ptce->stride[0] = iprv.v2 >> 32;
		ptce->stride[1] = iprv.v2 & 0xffffffff;
	}
	return iprv.status;
}

/* Return info about implemented application and control registers. */
static inline s64
ia64_pal_register_info (u64 info_request, u64 *reg_info_1, u64 *reg_info_2)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_REGISTER_INFO, info_request, 0, 0);
	if (reg_info_1)
		*reg_info_1 = iprv.v0;
	if (reg_info_2)
		*reg_info_2 = iprv.v1;
	return iprv.status;
}

typedef union pal_hints_u {
	u64			ph_data;
	struct {
	       u64		si		: 1,
				li		: 1,
				reserved	: 62;
	} pal_hints_s;
} pal_hints_u_t;

/* Return information about the register stack and RSE for this processor
 * implementation.
 */
static inline s64
ia64_pal_rse_info (u64 *num_phys_stacked, pal_hints_u_t *hints)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_RSE_INFO, 0, 0, 0);
	if (num_phys_stacked)
		*num_phys_stacked = iprv.v0;
	if (hints)
		hints->ph_data = iprv.v1;
	return iprv.status;
}

/*
 * Set the current hardware resource sharing policy of the processor
 */
static inline s64
ia64_pal_set_hw_policy (u64 policy)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_SET_HW_POLICY, policy, 0, 0);
	return iprv.status;
}

/* Cause the processor to enter	SHUTDOWN state, where prefetching and execution are
 * suspended, but cause cache and TLB coherency to be maintained.
 * This is usually called in IA-32 mode.
 */
static inline s64
ia64_pal_shutdown (void)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_SHUTDOWN, 0, 0, 0);
	return iprv.status;
}

/* Perform the second phase of processor self-test. */
static inline s64
ia64_pal_test_proc (u64 test_addr, u64 test_size, u64 attributes, u64 *self_test_state)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_TEST_PROC, test_addr, test_size, attributes);
	if (self_test_state)
		*self_test_state = iprv.v0;
	return iprv.status;
}

typedef union  pal_version_u {
	u64	pal_version_val;
	struct {
		u64	pv_pal_b_rev		:	8;
		u64	pv_pal_b_model		:	8;
		u64	pv_reserved1		:	8;
		u64	pv_pal_vendor		:	8;
		u64	pv_pal_a_rev		:	8;
		u64	pv_pal_a_model		:	8;
		u64	pv_reserved2		:	16;
	} pal_version_s;
} pal_version_u_t;


/*
 * Return PAL version information.  While the documentation states that
 * PAL_VERSION can be called in either physical or virtual mode, some
 * implementations only allow physical calls.  We don't call it very often,
 * so the overhead isn't worth eliminating.
 */
static inline s64
ia64_pal_version (pal_version_u_t *pal_min_version, pal_version_u_t *pal_cur_version)
{
	struct ia64_pal_retval iprv;
	PAL_CALL_PHYS(iprv, PAL_VERSION, 0, 0, 0);
	if (pal_min_version)
		pal_min_version->pal_version_val = iprv.v0;

	if (pal_cur_version)
		pal_cur_version->pal_version_val = iprv.v1;

	return iprv.status;
}

typedef union pal_tc_info_u {
	u64			pti_val;
	struct {
	       u64		num_sets	:	8,
				associativity	:	8,
				num_entries	:	16,
				pf		:	1,
				unified		:	1,
				reduce_tr	:	1,
				reserved	:	29;
	} pal_tc_info_s;
} pal_tc_info_u_t;

#define tc_reduce_tr		pal_tc_info_s.reduce_tr
#define tc_unified		pal_tc_info_s.unified
#define tc_pf			pal_tc_info_s.pf
#define tc_num_entries		pal_tc_info_s.num_entries
#define tc_associativity	pal_tc_info_s.associativity
#define tc_num_sets		pal_tc_info_s.num_sets


/* Return information about the virtual memory characteristics of the processor
 * implementation.
 */
static inline s64
ia64_pal_vm_info (u64 tc_level, u64 tc_type,  pal_tc_info_u_t *tc_info, u64 *tc_pages)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_VM_INFO, tc_level, tc_type, 0);
	if (tc_info)
		tc_info->pti_val = iprv.v0;
	if (tc_pages)
		*tc_pages = iprv.v1;
	return iprv.status;
}

/* Get page size information about the virtual memory characteristics of the processor
 * implementation.
 */
static inline s64
ia64_pal_vm_page_size (u64 *tr_pages, u64 *vw_pages)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_VM_PAGE_SIZE, 0, 0, 0);
	if (tr_pages)
		*tr_pages = iprv.v0;
	if (vw_pages)
		*vw_pages = iprv.v1;
	return iprv.status;
}

typedef union pal_vm_info_1_u {
	u64			pvi1_val;
	struct {
		u64		vw		: 1,
				phys_add_size	: 7,
				key_size	: 8,
				max_pkr		: 8,
				hash_tag_id	: 8,
				max_dtr_entry	: 8,
				max_itr_entry	: 8,
				max_unique_tcs	: 8,
				num_tc_levels	: 8;
	} pal_vm_info_1_s;
} pal_vm_info_1_u_t;

#define PAL_MAX_PURGES		0xFFFF		/* all ones is means unlimited */

typedef union pal_vm_info_2_u {
	u64			pvi2_val;
	struct {
		u64		impl_va_msb	: 8,
				rid_size	: 8,
				max_purges	: 16,
				reserved	: 32;
	} pal_vm_info_2_s;
} pal_vm_info_2_u_t;

/* Get summary information about the virtual memory characteristics of the processor
 * implementation.
 */
static inline s64
ia64_pal_vm_summary (pal_vm_info_1_u_t *vm_info_1, pal_vm_info_2_u_t *vm_info_2)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_VM_SUMMARY, 0, 0, 0);
	if (vm_info_1)
		vm_info_1->pvi1_val = iprv.v0;
	if (vm_info_2)
		vm_info_2->pvi2_val = iprv.v1;
	return iprv.status;
}

typedef union pal_itr_valid_u {
	u64			piv_val;
	struct {
	       u64		access_rights_valid	: 1,
				priv_level_valid	: 1,
				dirty_bit_valid		: 1,
				mem_attr_valid		: 1,
				reserved		: 60;
	} pal_tr_valid_s;
} pal_tr_valid_u_t;

/* Read a translation register */
static inline s64
ia64_pal_tr_read (u64 reg_num, u64 tr_type, u64 *tr_buffer, pal_tr_valid_u_t *tr_valid)
{
	struct ia64_pal_retval iprv;
	PAL_CALL_PHYS_STK(iprv, PAL_VM_TR_READ, reg_num, tr_type,(u64)ia64_tpa(tr_buffer));
	if (tr_valid)
		tr_valid->piv_val = iprv.v0;
	return iprv.status;
}

/*
 * PAL_PREFETCH_VISIBILITY transaction types
 */
#define PAL_VISIBILITY_VIRTUAL		0
#define PAL_VISIBILITY_PHYSICAL		1

/*
 * PAL_PREFETCH_VISIBILITY return codes
 */
#define PAL_VISIBILITY_OK		1
#define PAL_VISIBILITY_OK_REMOTE_NEEDED	0
#define PAL_VISIBILITY_INVAL_ARG	-2
#define PAL_VISIBILITY_ERROR		-3

static inline s64
ia64_pal_prefetch_visibility (s64 trans_type)
{
	struct ia64_pal_retval iprv;
	PAL_CALL(iprv, PAL_PREFETCH_VISIBILITY, trans_type, 0, 0);
	return iprv.status;
}

/* data structure for getting information on logical to physical mappings */
typedef union pal_log_overview_u {
	struct {
		u64	num_log		:16,	/* Total number of logical
						 * processors on this die
						 */
			tpc		:8,	/* Threads per core */
			reserved3	:8,	/* Reserved */
			cpp		:8,	/* Cores per processor */
			reserved2	:8,	/* Reserved */
			ppid		:8,	/* Physical processor ID */
			reserved1	:8;	/* Reserved */
	} overview_bits;
	u64 overview_data;
} pal_log_overview_t;

typedef union pal_proc_n_log_info1_u{
	struct {
		u64	tid		:16,	/* Thread id */
			reserved2	:16,	/* Reserved */
			cid		:16,	/* Core id */
			reserved1	:16;	/* Reserved */
	} ppli1_bits;
	u64	ppli1_data;
} pal_proc_n_log_info1_t;

typedef union pal_proc_n_log_info2_u {
	struct {
		u64	la		:16,	/* Logical address */
			reserved	:48;	/* Reserved */
	} ppli2_bits;
	u64	ppli2_data;
} pal_proc_n_log_info2_t;

typedef struct pal_logical_to_physical_s
{
	pal_log_overview_t overview;
	pal_proc_n_log_info1_t ppli1;
	pal_proc_n_log_info2_t ppli2;
} pal_logical_to_physical_t;

#define overview_num_log	overview.overview_bits.num_log
#define overview_tpc		overview.overview_bits.tpc
#define overview_cpp		overview.overview_bits.cpp
#define overview_ppid		overview.overview_bits.ppid
#define log1_tid		ppli1.ppli1_bits.tid
#define log1_cid		ppli1.ppli1_bits.cid
#define log2_la			ppli2.ppli2_bits.la

/* Get information on logical to physical processor mappings. */
static inline s64
ia64_pal_logical_to_phys(u64 proc_number, pal_logical_to_physical_t *mapping)
{
	struct ia64_pal_retval iprv;

	PAL_CALL(iprv, PAL_LOGICAL_TO_PHYSICAL, proc_number, 0, 0);

	if (iprv.status == PAL_STATUS_SUCCESS)
	{
		mapping->overview.overview_data = iprv.v0;
		mapping->ppli1.ppli1_data = iprv.v1;
		mapping->ppli2.ppli2_data = iprv.v2;
	}

	return iprv.status;
}

typedef struct pal_cache_shared_info_s
{
	u64 num_shared;
	pal_proc_n_log_info1_t ppli1;
	pal_proc_n_log_info2_t ppli2;
} pal_cache_shared_info_t;

/* Get information on logical to physical processor mappings. */
static inline s64
ia64_pal_cache_shared_info(u64 level,
		u64 type,
		u64 proc_number,
		pal_cache_shared_info_t *info)
{
	struct ia64_pal_retval iprv;

	PAL_CALL(iprv, PAL_CACHE_SHARED_INFO, level, type, proc_number);

	if (iprv.status == PAL_STATUS_SUCCESS) {
		info->num_shared = iprv.v0;
		info->ppli1.ppli1_data = iprv.v1;
		info->ppli2.ppli2_data = iprv.v2;
	}

	return iprv.status;
}
#endif /* __ASSEMBLY__ */

#endif /* _ASM_IA64_PAL_H */
