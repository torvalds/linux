#ifndef XTENSA_HAL_H
#define XTENSA_HAL_H

/*
 * THIS FILE IS GENERATED -- DO NOT MODIFY BY HAND
 *
 * include/asm-xtensa/xtensa/hal.h -- contains a definition of the
 * Core HAL interface.
 *
 * All definitions in this header file are independent of any specific
 * Xtensa processor configuration.  Thus an OS or other software can
 * include this header file and be compiled into configuration-
 * independent objects that can be distributed and eventually linked
 * to the HAL library (libhal.a) to create a configuration-specific
 * final executable.
 *
 * Certain definitions, however, are release-specific -- such as the
 * XTHAL_RELEASE_xxx macros (or additions made in later releases).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2002 Tensilica Inc.
 */


/*----------------------------------------------------------------------
  			 Constant Definitions
			(shared with assembly)
  ----------------------------------------------------------------------*/

/*  Software release information (not configuration-specific!):  */
#define XTHAL_RELEASE_MAJOR	1050
#define XTHAL_RELEASE_MINOR	0
#define XTHAL_RELEASE_NAME	"T1050.0-2002-08-06-eng0"
#define XTHAL_RELEASE_INTERNAL	"2002-08-06-eng0"
#define XTHAL_REL_T1050	1
#define XTHAL_REL_T1050_0	1
#define XTHAL_REL_T1050_0_2002	1
#define XTHAL_REL_T1050_0_2002_08	1
#define XTHAL_REL_T1050_0_2002_08_06	1
#define XTHAL_REL_T1050_0_2002_08_06_ENG0	1

/*  HAL version numbers (these names are for backward compatibility):  */
#define XTHAL_MAJOR_REV		XTHAL_RELEASE_MAJOR
#define XTHAL_MINOR_REV		XTHAL_RELEASE_MINOR
/*
 *  A bit of software release history on values of XTHAL_{MAJOR,MINOR}_REV:
 *
 *	Release		MAJOR	MINOR		Comment
 *	=======		=====	=====		=======
 *	T1015.n		n/a	n/a		(HAL not yet available)
 *	T1020.{0,1,2}	0	1		(HAL beta)
 *	T1020.{3,4}	0	2		First release.
 *	T1020.n (n>4)	0	2 or >3		(TBD)
 *	T1030.0		0	1		(HAL beta)
 *	T1030.{1,2}	0	3		Equivalent to first release.
 *	T1030.n (n>=3)	0	>= 3		(TBD)
 *	T1040.n		1040	n		Full CHAL available from T1040.2
 *	T1050.n		1050	n		Current release.
 *
 *
 *  Note:  there is a distinction between the software release with
 *  which something is compiled (accessible using XTHAL_RELEASE_* macros)
 *  and the software release with which the HAL library was compiled
 *  (accessible using Xthal_release_* global variables).  This
 *  distinction is particularly relevant for vendors that distribute
 *  configuration-independent binaries (eg. an OS), where their customer
 *  might link it with a HAL of a different Xtensa software release.
 *  In this case, it may be appropriate for the OS to verify at run-time
 *  whether XTHAL_RELEASE_* and Xthal_release_* are compatible.
 *  [Guidelines as to which release is compatible with which are not
 *  currently provided explicitly, but might be inferred from reading
 *  OSKit documentation for all releases -- compatibility is also highly
 *  dependent on which HAL features are used.  Each release is usually
 *  backward compatible, with very few exceptions if any.]
 *
 *  Notes:
 *	Tornado 2.0 supported in T1020.3+, T1030.1+, and T1040.{0,1} only.
 *	Tornado 2.0.2 supported in T1040.2+, and T1050.
 *	Compile-time HAL port of NucleusPlus supported by T1040.2+ and T1050.
 */


/*
 *  Architectural limits, independent of configuration.
 *  Note that these are ISA-defined limits, not micro-architecture implementation
 *  limits enforced by the Xtensa Processor Generator (which may be stricter than
 *  these below).
 */
#define XTHAL_MAX_CPS		8	/* max number of coprocessors (0..7) */
#define XTHAL_MAX_INTERRUPTS	32	/* max number of interrupts (0..31) */
#define XTHAL_MAX_INTLEVELS	16	/* max number of interrupt levels (0..15) */
					/* (as of T1040, implementation limit is 7: 0..6) */
#define XTHAL_MAX_TIMERS	4	/* max number of timers (CCOMPARE0..CCOMPARE3) */
					/* (as of T1040, implementation limit is 3: 0..2) */

/*  Misc:  */
#define XTHAL_LITTLEENDIAN		0
#define XTHAL_BIGENDIAN			1


/*  Interrupt types:  */
#define XTHAL_INTTYPE_UNCONFIGURED	0
#define XTHAL_INTTYPE_SOFTWARE		1
#define XTHAL_INTTYPE_EXTERN_EDGE	2
#define XTHAL_INTTYPE_EXTERN_LEVEL	3
#define XTHAL_INTTYPE_TIMER		4
#define XTHAL_INTTYPE_NMI		5
#define XTHAL_MAX_INTTYPES		6	/* number of interrupt types */

/*  Timer related:  */
#define XTHAL_TIMER_UNCONFIGURED	-1	/* Xthal_timer_interrupt[] value for non-existent timers */
#define XTHAL_TIMER_UNASSIGNED	XTHAL_TIMER_UNCONFIGURED	/* (for backwards compatibility only) */


/*  Access Mode bits (tentative):  */	/* bit abbr unit short_name       PPC equ - Description */
#define XTHAL_AMB_EXCEPTION	0	/* 001 E EX fls: EXception        none    - generate exception on any access (aka "illegal") */
#define XTHAL_AMB_HITCACHE	1	/* 002 C CH fls: use Cache on Hit ~(I CI) - use cache on hit -- way from tag match [or H HC, or U UC] (ISA: same, except for Isolate case) */
#define XTHAL_AMB_ALLOCATE	2	/* 004 A AL fl?: ALlocate         none    - refill cache on miss -- way from LRU [or F FI fill] (ISA: Read/Write Miss Refill) */
#define XTHAL_AMB_WRITETHRU	3	/* 008 W WT --s: WriteThrough     W WT    - store immediately to memory (ISA: same) */
#define XTHAL_AMB_ISOLATE	4	/* 010 I IS fls: ISolate          none    - use cache regardless of hit-vs-miss -- way from vaddr (ISA: use-cache-on-miss+hit) */
#define XTHAL_AMB_GUARD		5	/* 020 G GU ?l?: GUard            G *     - non-speculative; spec/replay refs not permitted */
#if 0
#define XTHAL_AMB_ORDERED	x	/* 000 O OR fls: ORdered          G *     - mem accesses cannot be out of order */
#define XTHAL_AMB_FUSEWRITES	x	/* 000 F FW --s: FuseWrites       none    - allow combining/merging multiple writes (to same datapath data unit) into one (implied by writeback) */
#define XTHAL_AMB_COHERENT	x	/* 000 M MC fl?: Mem/MP Coherent  M       - on reads, other CPUs/bus-masters may need to supply data */
#define XTHAL_AMB_TRUSTED	x	/* 000 T TR ?l?: TRusted          none    - memory will not bus error (if it does, handle as fatal imprecise interrupt) */
#define XTHAL_AMB_PREFETCH	x	/* 000 P PR fl?: PRefetch         none    - on refill, read line+1 into prefetch buffers */
#define XTHAL_AMB_STREAM	x	/* 000 S ST ???: STreaming        none    - access one of N stream buffers */
#endif /*0*/

#define XTHAL_AM_EXCEPTION	(1<<XTHAL_AMB_EXCEPTION)
#define XTHAL_AM_HITCACHE	(1<<XTHAL_AMB_HITCACHE)
#define XTHAL_AM_ALLOCATE	(1<<XTHAL_AMB_ALLOCATE)
#define XTHAL_AM_WRITETHRU	(1<<XTHAL_AMB_WRITETHRU)
#define XTHAL_AM_ISOLATE	(1<<XTHAL_AMB_ISOLATE)
#define XTHAL_AM_GUARD		(1<<XTHAL_AMB_GUARD)
#if 0
#define XTHAL_AM_ORDERED	(1<<XTHAL_AMB_ORDERED)
#define XTHAL_AM_FUSEWRITES	(1<<XTHAL_AMB_FUSEWRITES)
#define XTHAL_AM_COHERENT	(1<<XTHAL_AMB_COHERENT)
#define XTHAL_AM_TRUSTED	(1<<XTHAL_AMB_TRUSTED)
#define XTHAL_AM_PREFETCH	(1<<XTHAL_AMB_PREFETCH)
#define XTHAL_AM_STREAM		(1<<XTHAL_AMB_STREAM)
#endif /*0*/

/*
 *  Allowed Access Modes (bit combinations).
 *
 *  Columns are:
 *  "FOGIWACE"
 *	Access mode bits (see XTHAL_AMB_xxx above).
 *	<letter> = bit is set
 *	'-'      = bit is clear
 *	'.'      = bit is irrelevant / don't care, as follows:
 *			E=1 makes all others irrelevant
 *			W,F relevant only for stores
 *  "2345"
 *	Indicates which Xtensa releases support the corresponding
 *	access mode.  Releases for each character column are:
 *		2 = prior to T1020.2:   T1015 (V1.5), T1020.0, T1020.1
 *		3 = T1020.2 and later:  T1020.2+, T1030
 *		4 = T1040
 *		5 = T1050 (maybe)
 *	And the character column contents are:
 *		<number> = support by release(s)
 *		"." = unsupported by release(s)
 *		"?" = support unknown
 */
					/* FOGIWACE 2345 */
/*  For instruction fetch:  */
#define XTHAL_FAM_EXCEPTION	0x001	/* .......E 2345 exception */
#define XTHAL_FAM_ISOLATE	0x012	/* .--I.-C- .... isolate */
#define XTHAL_FAM_BYPASS	0x000	/* .---.--- 2345 bypass */
#define XTHAL_FAM_NACACHED	0x002	/* .---.-C- .... cached no-allocate (frozen) */
#define XTHAL_FAM_CACHED	0x006	/* .---.AC- 2345 cached */
/*  For data load:  */
#define XTHAL_LAM_EXCEPTION	0x001	/* .......E 2345 exception */
#define XTHAL_LAM_ISOLATE	0x012	/* .--I.-C- 2345 isolate */
#define XTHAL_LAM_BYPASS	0x000	/* .O--.--- 2... bypass speculative */
#define XTHAL_LAM_BYPASSG	0x020	/* .OG-.--- .345 bypass guarded */
#define XTHAL_LAM_NACACHED	0x002	/* .O--.-C- 2... cached no-allocate speculative */
#define XTHAL_LAM_NACACHEDG	0x022	/* .OG-.-C- .345 cached no-allocate guarded */
#define XTHAL_LAM_CACHED	0x006	/* .---.AC- 2345 cached speculative */
#define XTHAL_LAM_CACHEDG	0x026	/* .?G-.AC- .... cached guarded */
/*  For data store:  */
#define XTHAL_SAM_EXCEPTION	0x001	/* .......E 2345 exception */
#define XTHAL_SAM_ISOLATE	0x032	/* .-GI--C- 2345 isolate */
#define XTHAL_SAM_BYPASS	0x028	/* -OG-W--- 2345 bypass */
/*efine XTHAL_SAM_BYPASSF	0x028*/	/* F-G-W--- ...? bypass write-combined */
#define XTHAL_SAM_WRITETHRU	0x02A	/* -OG-W-C- 234? writethrough */
/*efine XTHAL_SAM_WRITETHRUF	0x02A*/	/* F-G-W-C- ...5 writethrough write-combined */
#define XTHAL_SAM_WRITEALLOC	0x02E	/* -OG-WAC- ...? writethrough-allocate */
/*efine XTHAL_SAM_WRITEALLOCF	0x02E*/	/* F-G-WAC- ...? writethrough-allocate write-combined */
#define XTHAL_SAM_WRITEBACK	0x026	/* F-G--AC- ...5 writeback */

#if 0
/*
    Cache attribute encoding for CACHEATTR (per ISA):
    (Note:  if this differs from ISA Ref Manual, ISA has precedence)

	Inst-fetches	Loads		Stores
	-------------	------------	-------------
0x0	FCA_EXCEPTION  ?LCA_NACACHED_G*	SCA_WRITETHRU	"uncached"
0x1	FCA_CACHED	LCA_CACHED	SCA_WRITETHRU	cached
0x2	FCA_BYPASS	LCA_BYPASS_G*	SCA_BYPASS	bypass
0x3	FCA_CACHED	LCA_CACHED	SCA_WRITEALLOCF	write-allocate
		     or LCA_EXCEPTION	SCA_EXCEPTION	(if unimplemented)
0x4	FCA_CACHED	LCA_CACHED	SCA_WRITEBACK	write-back
		     or LCA_EXCEPTION	SCA_EXCEPTION	(if unimplemented)
0x5..D	FCA_EXCEPTION	LCA_EXCEPTION	SCA_EXCEPTION	(reserved)
0xE	FCA_EXCEPTION	LCA_ISOLATE	SCA_ISOLATE	isolate
0xF	FCA_EXCEPTION	LCA_EXCEPTION	SCA_EXCEPTION	illegal
     *  Prior to T1020.2?, guard feature not supported, this defaulted to speculative (no _G)
*/
#endif /*0*/


#if !defined(__ASSEMBLY__) && !defined(_NOCLANGUAGE)
#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------
  			     HAL
  ----------------------------------------------------------------------*/

/* Constant to be checked in build = (XTHAL_MAJOR_REV<<16)|XTHAL_MINOR_REV */
extern const unsigned int Xthal_rev_no;


/*----------------------------------------------------------------------
  			Processor State
  ----------------------------------------------------------------------*/
/* save & restore the extra processor state */
extern void xthal_save_extra(void *base);
extern void xthal_restore_extra(void *base);

extern void xthal_save_cpregs(void *base, int);
extern void xthal_restore_cpregs(void *base, int);

/*extern void xthal_save_all_extra(void *base);*/
/*extern void xthal_restore_all_extra(void *base);*/

/* space for processor state */
extern const unsigned int Xthal_extra_size;
extern const unsigned int Xthal_extra_align;
/* space for TIE register files */
extern const unsigned int Xthal_cpregs_size[XTHAL_MAX_CPS];
extern const unsigned int Xthal_cpregs_align[XTHAL_MAX_CPS];

/* total of space for the processor state (for Tor2) */
extern const unsigned int Xthal_all_extra_size;
extern const unsigned int Xthal_all_extra_align;

/* initialize the extra processor */
/*extern void xthal_init_extra(void);*/
/* initialize the TIE coprocessor */
/*extern void xthal_init_cp(int);*/

/* initialize the extra processor */
extern void xthal_init_mem_extra(void *);
/* initialize the TIE coprocessor */
extern void xthal_init_mem_cp(void *, int);

/* validate & invalidate the TIE register file */
extern void xthal_validate_cp(int);
extern void xthal_invalidate_cp(int);

/* the number of TIE coprocessors contiguous from zero (for Tor2) */
extern const unsigned int Xthal_num_coprocessors;

/* actual number of coprocessors */
extern const unsigned char Xthal_cp_num;
/* index of highest numbered coprocessor, plus one */
extern const unsigned char Xthal_cp_max;
/* index of highest allowed coprocessor number, per cfg, plus one */
/*extern const unsigned char Xthal_cp_maxcfg;*/
/* bitmask of which coprocessors are present */
extern const unsigned int  Xthal_cp_mask;

/* read and write cpenable register */
extern void xthal_set_cpenable(unsigned);
extern unsigned xthal_get_cpenable(void);

/* read & write extra state register */
/*extern int xthal_read_extra(void *base, unsigned reg, unsigned *value);*/
/*extern int xthal_write_extra(void *base, unsigned reg, unsigned value);*/

/* read & write a TIE coprocessor register */
/*extern int xthal_read_cpreg(void *base, int cp, unsigned reg, unsigned *value);*/
/*extern int xthal_write_cpreg(void *base, int cp, unsigned reg, unsigned value);*/

/* return coprocessor number based on register */
/*extern int xthal_which_cp(unsigned reg);*/

/*----------------------------------------------------------------------
   				Interrupts
  ----------------------------------------------------------------------*/

/* the number of interrupt levels */
extern const unsigned char Xthal_num_intlevels;
/* the number of interrupts */
extern const unsigned char Xthal_num_interrupts;

/* mask for level of interrupts */
extern const unsigned int Xthal_intlevel_mask[XTHAL_MAX_INTLEVELS];
/* mask for level 0 to N interrupts */
extern const unsigned int Xthal_intlevel_andbelow_mask[XTHAL_MAX_INTLEVELS];

/* level of each interrupt */
extern const unsigned char Xthal_intlevel[XTHAL_MAX_INTERRUPTS];

/* type per interrupt */
extern const unsigned char Xthal_inttype[XTHAL_MAX_INTERRUPTS];

/* masks of each type of interrupt */
extern const unsigned int Xthal_inttype_mask[XTHAL_MAX_INTTYPES];

/* interrupt numbers assigned to each timer interrupt */
extern const int Xthal_timer_interrupt[XTHAL_MAX_TIMERS];

/***  Virtual interrupt prioritization:  ***/

/*  Convert between interrupt levels (as per PS.INTLEVEL) and virtual interrupt priorities:  */
extern unsigned	xthal_vpri_to_intlevel(unsigned vpri);
extern unsigned	xthal_intlevel_to_vpri(unsigned intlevel);

/*  Enables/disables given set (mask) of interrupts; returns previous enabled-mask of all ints:  */
extern unsigned	xthal_int_enable(unsigned);
extern unsigned	xthal_int_disable(unsigned);

/*  Set/get virtual priority of an interrupt:  */
extern int	xthal_set_int_vpri(int intnum, int vpri);
extern int	xthal_get_int_vpri(int intnum);

/*  Set/get interrupt lockout level for exclusive access to virtual priority data structures:  */
extern void	xthal_set_vpri_locklevel(unsigned intlevel);
extern unsigned	xthal_get_vpri_locklevel(void);

/*  Set/get current virtual interrupt priority:  */
extern unsigned	xthal_set_vpri(unsigned vpri);
extern unsigned	xthal_get_vpri(unsigned vpri);
extern unsigned	xthal_set_vpri_intlevel(unsigned intlevel);
extern unsigned	xthal_set_vpri_lock(void);



/*----------------------------------------------------------------------
   			Generic Interrupt Trampolining Support
  ----------------------------------------------------------------------*/

typedef void (XtHalVoidFunc)(void);

/*
 *  Bitmask of interrupts currently trampolining down:
 */
extern unsigned Xthal_tram_pending;

/*
 *  Bitmask of which interrupts currently trampolining down
 *  synchronously are actually enabled; this bitmask is necessary
 *  because INTENABLE cannot hold that state (sync-trampolining
 *  interrupts must be kept disabled while trampolining);
 *  in the current implementation, any bit set here is not set
 *  in INTENABLE, and vice-versa; once a sync-trampoline is
 *  handled (at level one), its enable bit must be moved from
 *  here to INTENABLE:
 */
extern unsigned Xthal_tram_enabled;

/*
 *  Bitmask of interrupts configured for sync trampolining:
 */
extern unsigned Xthal_tram_sync;


/*  Trampoline support functions:  */
extern unsigned  xthal_tram_pending_to_service( void );
extern void      xthal_tram_done( unsigned serviced_mask );
extern int       xthal_tram_set_sync( int intnum, int sync );
extern XtHalVoidFunc* xthal_set_tram_trigger_func( XtHalVoidFunc *trigger_fn );

/*  INTENABLE,INTREAD,INTSET,INTCLEAR register access functions:  */
extern unsigned  xthal_get_intenable( void );
extern void      xthal_set_intenable( unsigned );
extern unsigned  xthal_get_intread( void );
extern void      xthal_set_intset( unsigned );
extern void      xthal_set_intclear( unsigned );


/*----------------------------------------------------------------------
   				Register Windows
  ----------------------------------------------------------------------*/

/* number of registers in register window */
extern const unsigned int  Xthal_num_aregs;
extern const unsigned char Xthal_num_aregs_log2;

/*  This spill any live register windows (other than the caller's):  */
extern void      xthal_window_spill( void );


/*----------------------------------------------------------------------
   				Cache
  ----------------------------------------------------------------------*/

/* size of the cache lines in log2(bytes) */
extern const unsigned char Xthal_icache_linewidth;
extern const unsigned char Xthal_dcache_linewidth;
/* size of the cache lines in bytes */
extern const unsigned short Xthal_icache_linesize;
extern const unsigned short Xthal_dcache_linesize;
/* number of cache sets in log2(lines per way) */
extern const unsigned char Xthal_icache_setwidth;
extern const unsigned char Xthal_dcache_setwidth;
/* cache set associativity (number of ways) */
extern const unsigned int  Xthal_icache_ways;
extern const unsigned int  Xthal_dcache_ways;
/* size of the caches in bytes (ways * 2^(linewidth + setwidth)) */
extern const unsigned int  Xthal_icache_size;
extern const unsigned int  Xthal_dcache_size;
/* cache features */
extern const unsigned char Xthal_dcache_is_writeback;
extern const unsigned char Xthal_icache_line_lockable;
extern const unsigned char Xthal_dcache_line_lockable;

/* cache attribute register control (used by other HAL routines) */
extern unsigned xthal_get_cacheattr( void );
extern unsigned xthal_get_icacheattr( void );
extern unsigned xthal_get_dcacheattr( void );
extern void     xthal_set_cacheattr( unsigned );
extern void     xthal_set_icacheattr( unsigned );
extern void     xthal_set_dcacheattr( unsigned );

/* initialize cache support (must be called once at startup, before all other cache calls) */
/*extern void xthal_cache_startinit( void );*/
/* reset caches */
/*extern void xthal_icache_reset( void );*/
/*extern void xthal_dcache_reset( void );*/
/* enable caches */
extern void xthal_icache_enable( void );	/* DEPRECATED */
extern void xthal_dcache_enable( void );	/* DEPRECATED */
/* disable caches */
extern void xthal_icache_disable( void );	/* DEPRECATED */
extern void xthal_dcache_disable( void );	/* DEPRECATED */

/* invalidate the caches */
extern void xthal_icache_all_invalidate( void );
extern void xthal_dcache_all_invalidate( void );
extern void xthal_icache_region_invalidate( void *addr, unsigned size );
extern void xthal_dcache_region_invalidate( void *addr, unsigned size );
extern void xthal_icache_line_invalidate(void *addr);
extern void xthal_dcache_line_invalidate(void *addr);
/* write dirty data back */
extern void xthal_dcache_all_writeback( void );
extern void xthal_dcache_region_writeback( void *addr, unsigned size );
extern void xthal_dcache_line_writeback(void *addr);
/* write dirty data back and invalidate */
extern void xthal_dcache_all_writeback_inv( void );
extern void xthal_dcache_region_writeback_inv( void *addr, unsigned size );
extern void xthal_dcache_line_writeback_inv(void *addr);
/* prefetch and lock specified memory range into cache */
extern void xthal_icache_region_lock( void *addr, unsigned size );
extern void xthal_dcache_region_lock( void *addr, unsigned size );
extern void xthal_icache_line_lock(void *addr);
extern void xthal_dcache_line_lock(void *addr);
/* unlock from cache */
extern void xthal_icache_all_unlock( void );
extern void xthal_dcache_all_unlock( void );
extern void xthal_icache_region_unlock( void *addr, unsigned size );
extern void xthal_dcache_region_unlock( void *addr, unsigned size );
extern void xthal_icache_line_unlock(void *addr);
extern void xthal_dcache_line_unlock(void *addr);


/* sync icache and memory */
extern void xthal_icache_sync( void );
/* sync dcache and memory */
extern void xthal_dcache_sync( void );

/*----------------------------------------------------------------------
   				Debug
  ----------------------------------------------------------------------*/

/*  1 if debug option configured, 0 if not:  */
extern const int Xthal_debug_configured;

/*  Number of instruction and data break registers:  */
extern const int Xthal_num_ibreak;
extern const int Xthal_num_dbreak;

/*  Set (plant) and remove software breakpoint, both synchronizing cache:  */
extern unsigned int xthal_set_soft_break(void *addr);
extern void         xthal_remove_soft_break(void *addr, unsigned int);


/*----------------------------------------------------------------------
   				Disassembler
  ----------------------------------------------------------------------*/

/*  Max expected size of the return buffer for a disassembled instruction (hint only):  */
#define XTHAL_DISASM_BUFSIZE	80

/*  Disassembly option bits for selecting what to return:  */
#define XTHAL_DISASM_OPT_ADDR	0x0001	/* display address */
#define XTHAL_DISASM_OPT_OPHEX	0x0002	/* display opcode bytes in hex */
#define XTHAL_DISASM_OPT_OPCODE	0x0004	/* display opcode name (mnemonic) */
#define XTHAL_DISASM_OPT_PARMS	0x0008	/* display parameters */
#define XTHAL_DISASM_OPT_ALL	0x0FFF	/* display everything */

/* routine to get a string for the disassembled instruction */
extern int xthal_disassemble( unsigned char *instr_buf, void *tgt_addr,
		       char *buffer, unsigned buflen, unsigned options );

/* routine to get the size of the next instruction. Returns 0 for
   illegal instruction */
extern int xthal_disassemble_size( unsigned char *instr_buf );


/*----------------------------------------------------------------------
   				Core Counter
  ----------------------------------------------------------------------*/

/* counter info */
extern const unsigned char Xthal_have_ccount;	/* set if CCOUNT register present */
extern const unsigned char Xthal_num_ccompare;	/* number of CCOMPAREn registers */

/* get CCOUNT register (if not present return 0) */
extern unsigned xthal_get_ccount(void);

/* set and get CCOMPAREn registers (if not present, get returns 0) */
extern void     xthal_set_ccompare(int, unsigned);
extern unsigned xthal_get_ccompare(int);


/*----------------------------------------------------------------------
			Instruction/Data RAM/ROM Access
  ----------------------------------------------------------------------*/

extern void* xthal_memcpy(void *dst, const void *src, unsigned len);
extern void* xthal_bcopy(const void *src, void *dst, unsigned len);

/*----------------------------------------------------------------------
                           MP Synchronization
  ----------------------------------------------------------------------*/
extern int      xthal_compare_and_set( int *addr, int test_val, int compare_val );
extern unsigned xthal_get_prid( void );

/*extern const char  Xthal_have_s32c1i;*/
extern const unsigned char Xthal_have_prid;


/*----------------------------------------------------------------------
                             Miscellaneous
  ----------------------------------------------------------------------*/

extern const unsigned int  Xthal_release_major;
extern const unsigned int  Xthal_release_minor;
extern const char * const  Xthal_release_name;
extern const char * const  Xthal_release_internal;

extern const unsigned char Xthal_memory_order;
extern const unsigned char Xthal_have_windowed;
extern const unsigned char Xthal_have_density;
extern const unsigned char Xthal_have_booleans;
extern const unsigned char Xthal_have_loops;
extern const unsigned char Xthal_have_nsa;
extern const unsigned char Xthal_have_minmax;
extern const unsigned char Xthal_have_sext;
extern const unsigned char Xthal_have_clamps;
extern const unsigned char Xthal_have_mac16;
extern const unsigned char Xthal_have_mul16;
extern const unsigned char Xthal_have_fp;
extern const unsigned char Xthal_have_speculation;
extern const unsigned char Xthal_have_exceptions;
extern const unsigned char Xthal_xea_version;
extern const unsigned char Xthal_have_interrupts;
extern const unsigned char Xthal_have_highlevel_interrupts;
extern const unsigned char Xthal_have_nmi;

extern const unsigned short Xthal_num_writebuffer_entries;

extern const unsigned int  Xthal_build_unique_id;
/*  Release info for hardware targeted by software upgrades:  */
extern const unsigned int  Xthal_hw_configid0;
extern const unsigned int  Xthal_hw_configid1;
extern const unsigned int  Xthal_hw_release_major;
extern const unsigned int  Xthal_hw_release_minor;
extern const char * const  Xthal_hw_release_name;
extern const char * const  Xthal_hw_release_internal;


/*  Internal memories...  */

extern const unsigned char Xthal_num_instrom;
extern const unsigned char Xthal_num_instram;
extern const unsigned char Xthal_num_datarom;
extern const unsigned char Xthal_num_dataram;
extern const unsigned char Xthal_num_xlmi;
extern const unsigned int  Xthal_instrom_vaddr[1];
extern const unsigned int  Xthal_instrom_paddr[1];
extern const unsigned int  Xthal_instrom_size [1];
extern const unsigned int  Xthal_instram_vaddr[1];
extern const unsigned int  Xthal_instram_paddr[1];
extern const unsigned int  Xthal_instram_size [1];
extern const unsigned int  Xthal_datarom_vaddr[1];
extern const unsigned int  Xthal_datarom_paddr[1];
extern const unsigned int  Xthal_datarom_size [1];
extern const unsigned int  Xthal_dataram_vaddr[1];
extern const unsigned int  Xthal_dataram_paddr[1];
extern const unsigned int  Xthal_dataram_size [1];
extern const unsigned int  Xthal_xlmi_vaddr[1];
extern const unsigned int  Xthal_xlmi_paddr[1];
extern const unsigned int  Xthal_xlmi_size [1];



/*----------------------------------------------------------------------
                         Memory Management Unit
  ----------------------------------------------------------------------*/

extern const unsigned char Xthal_have_spanning_way;
extern const unsigned char Xthal_have_identity_map;
extern const unsigned char Xthal_have_mimic_cacheattr;
extern const unsigned char Xthal_have_xlt_cacheattr;
extern const unsigned char Xthal_have_cacheattr;
extern const unsigned char Xthal_have_tlbs;

extern const unsigned char Xthal_mmu_asid_bits;		/* 0 .. 8 */
extern const unsigned char Xthal_mmu_asid_kernel;
extern const unsigned char Xthal_mmu_rings;		/* 1 .. 4 (perhaps 0 if no MMU and/or no protection?) */
extern const unsigned char Xthal_mmu_ring_bits;
extern const unsigned char Xthal_mmu_sr_bits;
extern const unsigned char Xthal_mmu_ca_bits;
extern const unsigned int  Xthal_mmu_max_pte_page_size;
extern const unsigned int  Xthal_mmu_min_pte_page_size;

extern const unsigned char Xthal_itlb_way_bits;
extern const unsigned char Xthal_itlb_ways;
extern const unsigned char Xthal_itlb_arf_ways;
extern const unsigned char Xthal_dtlb_way_bits;
extern const unsigned char Xthal_dtlb_ways;
extern const unsigned char Xthal_dtlb_arf_ways;

/*  Convert between virtual and physical addresses (through static maps only):  */
/*** WARNING: these two functions may go away in a future release; don't depend on them! ***/
extern int  xthal_static_v2p( unsigned vaddr, unsigned *paddrp );
extern int  xthal_static_p2v( unsigned paddr, unsigned *vaddrp, unsigned cached );

#if 0
/*******************   EXPERIMENTAL AND TENTATIVE ONLY   ********************/

#define XTHAL_MMU_PAGESZ_COUNT_MAX	8	/* maximum number of different page sizes */
extern const char	Xthal_mmu_pagesz_count;		/* 0 .. 8		number of different page sizes configured */

/*  Note:  the following table doesn't necessarily have page sizes in increasing order: */
extern const char	Xthal_mmu_pagesz_log2[XTHAL_MMU_PAGESZ_COUNT_MAX];	/* 10 .. 28 (0 past count) */

/*  Sorted (increasing) table of page sizes, that indexes into the above table: */
extern const char	Xthal_mmu_pagesz_sorted[XTHAL_MMU_PAGESZ_COUNT_MAX];	/* 0 .. 7 (0 past count) */

/*u32	Xthal_virtual_exceptions;*/	/* bitmask of which exceptions execute in virtual mode... */

extern const char	Xthal_mmu_pte_pagesz_log2_min;	/* ?? minimum page size in PTEs */
extern const char	Xthal_mmu_pte_pagesz_log2_max;	/* ?? maximum page size in PTEs */

/*  Cache Attribute Bits Implemented by the Cache (part of the cache abstraction) */
extern const char	Xthal_icache_fca_bits_implemented;	/* ITLB/UTLB only! */
extern const char	Xthal_dcache_lca_bits_implemented;	/* DTLB/UTLB only! */
extern const char	Xthal_dcache_sca_bits_implemented; 	/* DTLB/UTLB only! */

/*  Per TLB Parameters (Instruction, Data, Unified)  */
struct XtHalMmuTlb	Xthal_itlb;	/* description of MMU I-TLB generic features */
struct XtHalMmuTlb	Xthal_dtlb;	/* description of MMU D-TLB generic features */
struct XtHalMmuTlb	Xthal_utlb;	/* description of MMU U-TLB generic features */

#define XTHAL_MMU_WAYS_MAX	8	/* maximum number of ways (associativities) for each TLB */

/*  Structure for common information described for each possible TLB (instruction, data and unified): */
typedef struct XtHalMmuTlb {
    u8  	va_bits;		/* 32		(number of virtual address bits) */
    u8  	pa_bits;		/* 32		(number of physical address bits) */
    bool	tlb_va_indexed;		/* 1	(set if TLB is indexed by virtual address) */
    bool	tlb_va_tagged;		/* 0	(set if TLB is tagged by virtual address) */
    bool	cache_va_indexed;	/* 1	(set if cache is indexed by virtual address) */
    bool	cache_va_tagged;	/* 0	(set if cache is tagged by virtual address) */
    /*bool	(whether page tables are traversed in vaddr sorted order, paddr sorted order, ...) */
    /*u8	(set of available page attribute bits, other than cache attribute bits defined above) */
    /*u32	(various masks for pages, MMU table/TLB entries, etc.) */
    u8  	way_count;		/* 0 .. 8	(number of ways, a.k.a. associativities, for this TLB) */
    XtHalMmuTlbWay *	ways[XTHAL_MMU_WAYS_MAX];	/* pointers to per-way parms for each way */
} XtHalMmuTlb;

/*  Per TLB Way (Per Associativity) Parameters  */
typedef struct XtHalMmuTlbWay {
     u32	index_count_log2;	/* 0 .. 4 */
     u32	pagesz_mask;		/* 0 .. 2^pagesz_count - 1	(each bit corresponds to a size */
					/*		defined in the Xthal_mmu_pagesz_log2[] table) */
     u32	vpn_const_mask;
     u32	vpn_const_value;
     u64	ppn_const_mask;		/* future may support pa_bits > 32 */
     u64	ppn_const_value;
     u32	ppn_id_mask;		/* paddr bits taken directly from vaddr */
     bool	backgnd_match;		/* 0 or 1 */
     /*  These are defined in terms of the XTHAL_CACHE_xxx bits: */
     u8 	fca_const_mask;		/* ITLB/UTLB only! */
     u8 	fca_const_value;	/* ITLB/UTLB only! */
     u8 	lca_const_mask;		/* DTLB/UTLB only! */
     u8 	lca_const_value; 	/* DTLB/UTLB only! */
     u8 	sca_const_mask; 	/* DTLB/UTLB only! */
     u8 	sca_const_value; 	/* DTLB/UTLB only! */
     /*  These define an encoding that map 5 bits in TLB and PTE entries to */
     /*  8 bits (FCA, ITLB), 16 bits (LCA+SCA, DTLB) or 24 bits (FCA+LCA+SCA, UTLB): */
     /*  (they may be moved to struct XtHalMmuTlb) */
     u8		ca_bits;		/* number of bits in TLB/PTE entries for cache attributes */
     u32 *	ca_map;			/* pointer to array of 2^ca_bits entries of FCA+LCA+SCA bits */
} XtHalMmuTlbWay;

/*
 *  The way to determine whether protection support is present in core
 *  is to [look at Xthal_mmu_rings ???].
 *  Give info on memory requirements for MMU tables and other in-memory
 *  data structures (globally, per task, base and per page, etc.) - whatever bounds can be calculated.
 */


/*  Default vectors:  */
xthal_immu_fetch_miss_vector
xthal_dmmu_load_miss_vector
xthal_dmmu_store_miss_vector

/*  Functions called when a fault is detected:  */
typedef void (XtHalMmuFaultFunc)( unsigned vaddr, ...context... );
/*  Or, */
/*	a? = vaddr */
/*	a? = context... */
/*	PS.xxx = xxx */
XtHalMMuFaultFunc *Xthal_immu_fetch_fault_func;
XtHalMMuFaultFunc *Xthal_dmmu_load_fault_func;
XtHalMMuFaultFunc *Xthal_dmmu_store_fault_func;

/*  Default Handlers:  */
/*  The user and/or kernel exception handlers may jump to these handlers to handle the relevant exceptions,
 *  according to the value of EXCCAUSE.  The exact register state on entry to these handlers is TBD.  */
/*  When multiple TLB entries match (hit) on the same access:  */
xthal_immu_fetch_multihit_handler
xthal_dmmu_load_multihit_handler
xthal_dmmu_store_multihit_handler
/*  Protection violations according to cache attributes, and other cache attribute mismatches:  */
xthal_immu_fetch_attr_handler
xthal_dmmu_load_attr_handler
xthal_dmmu_store_attr_handler
/*  Protection violations due to insufficient ring level:  */
xthal_immu_fetch_priv_handler
xthal_dmmu_load_priv_handler
xthal_dmmu_store_priv_handler
/*  Alignment exception handlers (if supported by the particular Xtensa MMU configuration):  */
xthal_dmmu_load_align_handler
xthal_dmmu_store_align_handler

/*  Or, alternatively, the OS user and/or kernel exception handlers may simply jump to the
 *  following entry points which will handle any values of EXCCAUSE not handled by the OS:  */
xthal_user_exc_default_handler
xthal_kernel_exc_default_handler

#endif /*0*/

#ifdef INCLUDE_DEPRECATED_HAL_CODE
extern const unsigned char Xthal_have_old_exc_arch;
extern const unsigned char Xthal_have_mmu;
extern const unsigned int  Xthal_num_regs;
extern const unsigned char Xthal_num_iroms;
extern const unsigned char Xthal_num_irams;
extern const unsigned char Xthal_num_droms;
extern const unsigned char Xthal_num_drams;
extern const unsigned int  Xthal_configid0;
extern const unsigned int  Xthal_configid1;
#endif

#ifdef INCLUDE_DEPRECATED_HAL_DEBUG_CODE
#define XTHAL_24_BIT_BREAK		0x80000000
#define XTHAL_16_BIT_BREAK		0x40000000
extern const unsigned short	Xthal_ill_inst_16[16];
#define XTHAL_DEST_REG		0xf0000000	/* Mask for destination register */
#define XTHAL_DEST_REG_INST	0x08000000	/* Branch address is in register */
#define XTHAL_DEST_REL_INST	0x04000000	/* Branch address is relative */
#define XTHAL_RFW_INST		0x00000800
#define XTHAL_RFUE_INST		0x00000400
#define XTHAL_RFI_INST		0x00000200
#define XTHAL_RFE_INST		0x00000100
#define XTHAL_RET_INST		0x00000080
#define XTHAL_BREAK_INST	0x00000040
#define XTHAL_SYSCALL_INST	0x00000020
#define XTHAL_LOOP_END		0x00000010	/* Not set by xthal_inst_type */
#define XTHAL_JUMP_INST		0x00000008	/* Call or jump instruction */
#define XTHAL_BRANCH_INST	0x00000004	/* Branch instruction */
#define XTHAL_24_BIT_INST	0x00000002
#define XTHAL_16_BIT_INST   0x00000001
typedef struct xthal_state {
    unsigned	pc;
    unsigned	ar[16];
    unsigned	lbeg;
    unsigned	lend;
    unsigned	lcount;
    unsigned	extra_ptr;
    unsigned	cpregs_ptr[XTHAL_MAX_CPS];
} XTHAL_STATE;
extern unsigned int xthal_inst_type(void *addr);
extern unsigned int xthal_branch_addr(void *addr);
extern unsigned int xthal_get_npc(XTHAL_STATE *user_state);
#endif /* INCLUDE_DEPRECATED_HAL_DEBUG_CODE */

#ifdef __cplusplus
}
#endif
#endif /*!__ASSEMBLY__ */

#endif /*XTENSA_HAL_H*/

