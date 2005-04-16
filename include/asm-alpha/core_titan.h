#ifndef __ALPHA_TITAN__H__
#define __ALPHA_TITAN__H__

#include <linux/types.h>
#include <linux/pci.h>
#include <asm/compiler.h>

/*
 * TITAN is the internal names for a core logic chipset which provides
 * memory controller and PCI/AGP access for 21264 based systems.
 *
 * This file is based on:
 *
 * Titan Chipset Engineering Specification
 * Revision 0.12
 * 13 July 1999
 *
 */

/* XXX: Do we need to conditionalize on this?  */
#ifdef USE_48_BIT_KSEG
#define TI_BIAS 0x80000000000UL
#else
#define TI_BIAS 0x10000000000UL
#endif

/*
 * CChip, DChip, and PChip registers
 */

typedef struct {
	volatile unsigned long csr __attribute__((aligned(64)));
} titan_64;

typedef struct {
	titan_64	csc;
	titan_64	mtr;
	titan_64	misc;
	titan_64	mpd;
	titan_64	aar0;
	titan_64	aar1;
	titan_64	aar2;
	titan_64	aar3;
	titan_64	dim0;
	titan_64	dim1;
	titan_64	dir0;
	titan_64	dir1;
	titan_64	drir;
	titan_64	prben;
	titan_64	iic0;
	titan_64	iic1;
	titan_64	mpr0;
	titan_64	mpr1;
	titan_64	mpr2;
	titan_64	mpr3;
	titan_64	rsvd[2];
	titan_64	ttr;
	titan_64	tdr;
	titan_64	dim2;
	titan_64	dim3;
	titan_64	dir2;
	titan_64	dir3;
	titan_64	iic2;
	titan_64	iic3;
	titan_64	pwr;
	titan_64	reserved[17];
	titan_64	cmonctla;
	titan_64	cmonctlb;
	titan_64	cmoncnt01;
	titan_64	cmoncnt23;
	titan_64	cpen;
} titan_cchip;

typedef struct {
	titan_64	dsc;
	titan_64	str;
	titan_64	drev;
	titan_64	dsc2;
} titan_dchip;

typedef struct {
	titan_64	wsba[4];
	titan_64	wsm[4];
	titan_64	tba[4];
	titan_64	pctl;
	titan_64	plat;
	titan_64	reserved0[2];
	union {
		struct {
			titan_64	serror;
			titan_64	serren;
			titan_64	serrset;
			titan_64	reserved0;
			titan_64	gperror;
			titan_64	gperren;
			titan_64	gperrset;
			titan_64	reserved1;
			titan_64	gtlbiv;
			titan_64	gtlbia;
			titan_64	reserved2[2];
			titan_64	sctl;
			titan_64	reserved3[3];
		} g;
		struct {
			titan_64	agperror;
			titan_64	agperren;
			titan_64	agperrset;
			titan_64	agplastwr;
			titan_64	aperror;
			titan_64	aperren;
			titan_64	aperrset;
			titan_64	reserved0;
			titan_64	atlbiv;
			titan_64	atlbia;
			titan_64	reserved1[6];
		} a;
	} port_specific;
	titan_64	sprst;
	titan_64	reserved1[31];
} titan_pachip_port;

typedef struct {
	titan_pachip_port	g_port;
	titan_pachip_port	a_port;
} titan_pachip;

#define TITAN_cchip	((titan_cchip  *)(IDENT_ADDR+TI_BIAS+0x1A0000000UL))
#define TITAN_dchip    	((titan_dchip  *)(IDENT_ADDR+TI_BIAS+0x1B0000800UL))
#define TITAN_pachip0 	((titan_pachip *)(IDENT_ADDR+TI_BIAS+0x180000000UL))
#define TITAN_pachip1 	((titan_pachip *)(IDENT_ADDR+TI_BIAS+0x380000000UL))
extern unsigned TITAN_agp;
extern int TITAN_bootcpu;

/*
 * TITAN PA-chip Window Space Base Address register.
 * (WSBA[0-2])
 */
#define wsba_m_ena 0x1                
#define wsba_m_sg 0x2
#define wsba_m_addr 0xFFF00000  
#define wmask_k_sz1gb 0x3FF00000                   
union TPAchipWSBA {
	struct  {
		unsigned wsba_v_ena : 1;
		unsigned wsba_v_sg : 1;
		unsigned wsba_v_rsvd1 : 18;
		unsigned wsba_v_addr : 12;
		unsigned wsba_v_rsvd2 : 32;
        } wsba_r_bits;
	int wsba_q_whole [2];
};

/*
 * TITAN PA-chip Control Register
 * This definition covers both the G-Port GPCTL and the A-PORT APCTL.
 * Bits <51:0> are the same in both cases. APCTL<63:52> are only 
 * applicable to AGP.
 */
#define pctl_m_fbtb 			0x00000001
#define pctl_m_thdis 			0x00000002
#define pctl_m_chaindis 		0x00000004
#define pctl_m_tgtlat 			0x00000018
#define pctl_m_hole  	  		0x00000020
#define pctl_m_mwin 	  		0x00000040
#define pctl_m_arbena 	  		0x00000080
#define pctl_m_prigrp 	  		0x0000FF00
#define pctl_m_ppri 	  		0x00010000
#define pctl_m_pcispd66  		0x00020000
#define pctl_m_cngstlt	  		0x003C0000
#define pctl_m_ptpdesten 		0x3FC00000
#define pctl_m_dpcen			0x40000000
#define pctl_m_apcen		0x0000000080000000UL
#define pctl_m_dcrtv		0x0000000300000000UL
#define pctl_m_en_stepping	0x0000000400000000UL
#define apctl_m_rsvd1		0x000FFFF800000000UL
#define apctl_m_agp_rate	0x0030000000000000UL
#define apctl_m_agp_sba_en	0x0040000000000000UL
#define apctl_m_agp_en		0x0080000000000000UL
#define apctl_m_rsvd2		0x0100000000000000UL
#define apctl_m_agp_present	0x0200000000000000UL
#define apctl_agp_hp_rd		0x1C00000000000000UL
#define apctl_agp_lp_rd		0xE000000000000000UL
#define gpctl_m_rsvd		0xFFFFFFF800000000UL
union TPAchipPCTL {
	struct {
		unsigned pctl_v_fbtb : 1;		/* A/G [0]     */
		unsigned pctl_v_thdis : 1;		/* A/G [1]     */
		unsigned pctl_v_chaindis : 1;		/* A/G [2]     */
		unsigned pctl_v_tgtlat : 2;		/* A/G [4:3]   */
		unsigned pctl_v_hole : 1;		/* A/G [5]     */
		unsigned pctl_v_mwin : 1;		/* A/G [6]     */
		unsigned pctl_v_arbena : 1;		/* A/G [7]     */
		unsigned pctl_v_prigrp : 8;		/* A/G [15:8]  */
		unsigned pctl_v_ppri : 1;		/* A/G [16]    */
		unsigned pctl_v_pcispd66 : 1;		/* A/G [17]    */
		unsigned pctl_v_cngstlt : 4;		/* A/G [21:18] */
		unsigned pctl_v_ptpdesten : 8;		/* A/G [29:22] */
		unsigned pctl_v_dpcen : 1;		/* A/G [30]    */
		unsigned pctl_v_apcen : 1;		/* A/G [31]    */
		unsigned pctl_v_dcrtv : 2;		/* A/G [33:32] */
		unsigned pctl_v_en_stepping :1;		/* A/G [34]    */
		unsigned apctl_v_rsvd1 : 17;		/* A   [51:35] */
		unsigned apctl_v_agp_rate : 2;		/* A   [53:52] */
		unsigned apctl_v_agp_sba_en : 1;	/* A   [54]    */
		unsigned apctl_v_agp_en : 1;		/* A   [55]    */
		unsigned apctl_v_rsvd2 : 1;		/* A   [56]    */
		unsigned apctl_v_agp_present : 1;	/* A   [57]    */
		unsigned apctl_v_agp_hp_rd : 3;		/* A   [60:58] */
		unsigned apctl_v_agp_lp_rd : 3;		/* A   [63:61] */
	} pctl_r_bits;
	unsigned int pctl_l_whole [2];
	unsigned long pctl_q_whole;
};

/*
 * SERROR / SERREN / SERRSET
 */
union TPAchipSERR {
	struct {
		unsigned serr_v_lost_uecc : 1;		/* [0]		*/
		unsigned serr_v_uecc : 1;		/* [1]  	*/
		unsigned serr_v_cre : 1;		/* [2]		*/
		unsigned serr_v_nxio : 1;		/* [3]		*/
		unsigned serr_v_lost_cre : 1;		/* [4]		*/
		unsigned serr_v_rsvd0 : 10;		/* [14:5]	*/
		unsigned serr_v_addr : 32;		/* [46:15]	*/
		unsigned serr_v_rsvd1 : 5;		/* [51:47]	*/
		unsigned serr_v_source : 2;		/* [53:52]	*/
		unsigned serr_v_cmd : 2;		/* [55:54]	*/
		unsigned serr_v_syn : 8;		/* [63:56]	*/
	} serr_r_bits;
	unsigned int serr_l_whole[2];
	unsigned long serr_q_whole;
};

/*
 * GPERROR / APERROR / GPERREN / APERREN / GPERRSET / APERRSET
 */
union TPAchipPERR {
	struct {
		unsigned long perr_v_lost : 1;	     	/* [0]		*/
		unsigned long perr_v_serr : 1;		/* [1]		*/
		unsigned long perr_v_perr : 1;		/* [2]		*/
		unsigned long perr_v_dcrto : 1;		/* [3]		*/
		unsigned long perr_v_sge : 1;		/* [4]		*/
		unsigned long perr_v_ape : 1;		/* [5]		*/
		unsigned long perr_v_ta : 1;		/* [6]		*/
		unsigned long perr_v_dpe : 1;		/* [7]		*/
		unsigned long perr_v_nds : 1;		/* [8]		*/
		unsigned long perr_v_iptpr : 1;		/* [9]		*/
		unsigned long perr_v_iptpw : 1;		/* [10] 	*/
		unsigned long perr_v_rsvd0 : 3;		/* [13:11]	*/
		unsigned long perr_v_addr : 33;		/* [46:14]	*/
		unsigned long perr_v_dac : 1;		/* [47]		*/
		unsigned long perr_v_mwin : 1;		/* [48]		*/
		unsigned long perr_v_rsvd1 : 3;		/* [51:49]	*/
		unsigned long perr_v_cmd : 4;		/* [55:52]	*/
		unsigned long perr_v_rsvd2 : 8;		/* [63:56]	*/
	} perr_r_bits;
	unsigned int perr_l_whole[2];
	unsigned long perr_q_whole;
};

/*
 * AGPERROR / AGPERREN / AGPERRSET
 */
union TPAchipAGPERR {
	struct {
		unsigned agperr_v_lost : 1;		/* [0]		*/
		unsigned agperr_v_lpqfull : 1;		/* [1]		*/
		unsigned apgerr_v_hpqfull : 1;		/* [2]		*/
		unsigned agperr_v_rescmd : 1;		/* [3]		*/
		unsigned agperr_v_ipte : 1;		/* [4]		*/
		unsigned agperr_v_ptp :	1;      	/* [5]		*/
		unsigned agperr_v_nowindow : 1;		/* [6]		*/
		unsigned agperr_v_rsvd0 : 8;		/* [14:7]	*/
		unsigned agperr_v_addr : 32;		/* [46:15]	*/
		unsigned agperr_v_rsvd1 : 1;		/* [47]		*/
		unsigned agperr_v_dac : 1;		/* [48]		*/
		unsigned agperr_v_mwin : 1;		/* [49]		*/
		unsigned agperr_v_cmd : 3;		/* [52:50]	*/
		unsigned agperr_v_length : 6;		/* [58:53]	*/
		unsigned agperr_v_fence : 1;		/* [59]		*/
		unsigned agperr_v_rsvd2 : 4;		/* [63:60]	*/
	} agperr_r_bits;
	unsigned int agperr_l_whole[2];
	unsigned long agperr_q_whole;
};
/*
 * Memory spaces:
 * Hose numbers are assigned as follows:
 *		0 - pachip 0 / G Port
 *		1 - pachip 1 / G Port
 * 		2 - pachip 0 / A Port
 *      	3 - pachip 1 / A Port
 */
#define TITAN_HOSE_SHIFT       (33) 
#define TITAN_HOSE(h)		(((unsigned long)(h)) << TITAN_HOSE_SHIFT)
#define TITAN_BASE		(IDENT_ADDR + TI_BIAS)
#define TITAN_MEM(h)	     	(TITAN_BASE+TITAN_HOSE(h)+0x000000000UL)
#define _TITAN_IACK_SC(h)    	(TITAN_BASE+TITAN_HOSE(h)+0x1F8000000UL)
#define TITAN_IO(h)	     	(TITAN_BASE+TITAN_HOSE(h)+0x1FC000000UL)
#define TITAN_CONF(h)	     	(TITAN_BASE+TITAN_HOSE(h)+0x1FE000000UL)

#define TITAN_HOSE_MASK		TITAN_HOSE(3)
#define TITAN_IACK_SC	     	_TITAN_IACK_SC(0) /* hack! */

/* 
 * The canonical non-remaped I/O and MEM addresses have these values
 * subtracted out.  This is arranged so that folks manipulating ISA
 * devices can use their familiar numbers and have them map to bus 0.
 */

#define TITAN_IO_BIAS		TITAN_IO(0)
#define TITAN_MEM_BIAS		TITAN_MEM(0)

/* The IO address space is larger than 0xffff */
#define TITAN_IO_SPACE		(TITAN_CONF(0) - TITAN_IO(0))

/* TIG Space */
#define TITAN_TIG_SPACE		(TITAN_BASE + 0x100000000UL)

/* Offset between ram physical addresses and pci64 DAC bus addresses.  */
/* ??? Just a guess.  Ought to confirm it hasn't been moved.  */
#define TITAN_DAC_OFFSET	(1UL << 40)

/*
 * Data structure for handling TITAN machine checks:
 */
#define SCB_Q_SYSERR	0x620
#define SCB_Q_PROCERR	0x630
#define SCB_Q_SYSMCHK	0x660
#define SCB_Q_PROCMCHK	0x670
#define SCB_Q_SYSEVENT	0x680	/* environmental / system management */
struct el_TITAN_sysdata_mcheck {
	u64 summary;	/* 0x00 */
	u64 c_dirx;	/* 0x08 */
	u64 c_misc;	/* 0x10 */
	u64 p0_serror;	/* 0x18 */
	u64 p0_gperror; /* 0x20 */
	u64 p0_aperror; /* 0x28 */
	u64 p0_agperror;/* 0x30 */
	u64 p1_serror;	/* 0x38 */
	u64 p1_gperror; /* 0x40 */
	u64 p1_aperror; /* 0x48 */
	u64 p1_agperror;/* 0x50 */
};

/*
 * System area for a privateer 680 environmental/system management mcheck 
 */
struct el_PRIVATEER_envdata_mcheck {
	u64 summary;	/* 0x00 */
	u64 c_dirx;	/* 0x08 */
	u64 smir;	/* 0x10 */
	u64 cpuir;	/* 0x18 */
	u64 psir;	/* 0x20 */
	u64 fault;	/* 0x28 */
	u64 sys_doors;	/* 0x30 */
	u64 temp_warn;	/* 0x38 */
	u64 fan_ctrl;	/* 0x40 */
	u64 code;	/* 0x48 */
	u64 reserved;	/* 0x50 */
};

#ifdef __KERNEL__

#ifndef __EXTERN_INLINE
#define __EXTERN_INLINE extern inline
#define __IO_EXTERN_INLINE
#endif

/*
 * I/O functions:
 *
 * TITAN, a 21??? PCI/memory support chipset for the EV6 (21264)
 * can only use linear accesses to get at PCI/AGP memory and I/O spaces.
 */

/*
 * Memory functions.  all accesses are done through linear space.
 */

__EXTERN_INLINE void __iomem *titan_ioportmap(unsigned long addr)
{
	return (void __iomem *)(addr + TITAN_IO_BIAS);
}

extern void __iomem *titan_ioremap(unsigned long addr, unsigned long size);
extern void titan_iounmap(volatile void __iomem *addr);

__EXTERN_INLINE int titan_is_ioaddr(unsigned long addr)
{
	return addr >= TITAN_BASE;
}

extern int titan_is_mmio(const volatile void __iomem *addr);

#undef __IO_PREFIX
#define __IO_PREFIX		titan
#define titan_trivial_rw_bw	1
#define titan_trivial_rw_lq	1
#define titan_trivial_io_bw	1
#define titan_trivial_io_lq	1
#define titan_trivial_iounmap	0
#include <asm/io_trivial.h>

#ifdef __IO_EXTERN_INLINE
#undef __EXTERN_INLINE
#undef __IO_EXTERN_INLINE
#endif

#endif /* __KERNEL__ */

#endif /* __ALPHA_TITAN__H__ */
