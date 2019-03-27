/*-
 * Copyright (c) 2015 Landon Fuller <landon@landonf.org>
 * Copyright (c) 2010 Broadcom Corporation
 * 
 * This file was derived from the sbconfig.h header distributed with
 * Broadcom's initial brcm80211 Linux driver release, as
 * contributed to the Linux staging repository. 
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 * 
 * $FreeBSD$
 */

#ifndef _BHND_SIBA_SIBAREG_
#define _BHND_SIBA_SIBAREG_

#include <dev/bhnd/bhndreg.h>

/*
 * Broadcom SIBA Configuration Space Registers.
 * 
 * Backplane configuration registers common to siba(4) core register
 * blocks.
 */

/**
 * Extract a config attribute by applying _MASK and _SHIFT defines.
 * 
 * @param _reg The register value containing the desired attribute
 * @param _attr The BCMA EROM attribute name (e.g. ENTRY_ISVALID), to be
 * concatenated with the `SB` prefix and `_MASK`/`_SHIFT` suffixes.
 */
#define	SIBA_REG_GET(_entry, _attr)			\
	((_entry & SIBA_ ## _attr ## _MASK)	\
	>> SIBA_ ## _attr ## _SHIFT)


#define	SIBA_ENUM_ADDR		BHND_DEFAULT_CHIPC_ADDR	/**< enumeration space */
#define	SIBA_ENUM_SIZE		0x00100000		/**< size of the enumeration space */ 
#define	SIBA_CORE_SIZE		BHND_DEFAULT_CORE_SIZE	/**< per-core register block size */
#define	SIBA_MAX_INTR		32			/**< maximum number of backplane interrupt vectors */
#define	SIBA_MAX_CORES	\
    (SIBA_ENUM_SIZE/SIBA_CORE_SIZE)			/**< Maximum number of cores */

/** Evaluates to the bus address offset of the @p idx core register block */
#define	SIBA_CORE_OFFSET(idx)	((idx) * SIBA_CORE_SIZE)

/** Evaluates to the bus address of the @p idx core register block */
#define	SIBA_CORE_ADDR(idx)	(SIBA_ENUM_ADDR + SIBA_CORE_OFFSET(idx))

/*
 * Sonics configuration registers are mapped to each core's enumeration
 * space, at the end of the 4kb device register block, in reverse
 * order:
 * 
 * [0x0000-0x0dff]	core registers
 * [0x0e00-0x0eff]	SIBA_R1 registers	(sonics >= 2.3)
 * [0x0f00-0x0fff]	SIBA_R0 registers
 */

#define	SIBA_CFG0_OFFSET	0xf00	/**< first configuration block */
#define	SIBA_CFG1_OFFSET	0xe00	/**< second configuration block (sonics >= 2.3) */
#define	SIBA_CFG_SIZE		0x100	/**< cfg register block size */

/* Return the SIBA_CORE_ADDR-relative offset for the given siba configuration
 * register block; configuration blocks are allocated starting at
 * SIBA_CFG0_OFFSET, growing downwards. */
#define	SIBA_CFG_OFFSET(_n)	(SIBA_CFG0_OFFSET - ((_n) * SIBA_CFG_SIZE))

/* Return the SIBA_CORE_ADDR-relative offset for a SIBA_CFG* register. */
#define	SB0_REG_ABS(off)	((off) + SIBA_CFG0_OFFSET)
#define	SB1_REG_ABS(off)	((off) + SIBA_CFG1_OFFSET)

/* SIBA_CFG0 registers */
#define	SIBA_CFG0_IPSFLAG	0x08	/**< initiator port ocp slave flag */
#define	SIBA_CFG0_TPSFLAG	0x18	/**< target port ocp slave flag */
#define	SIBA_CFG0_TMERRLOGA	0x48	/**< sonics >= 2.3 */
#define	SIBA_CFG0_TMERRLOG	0x50	/**< sonics >= 2.3 */
#define	SIBA_CFG0_ADMATCH3	0x60	/**< address match3 */
#define	SIBA_CFG0_ADMATCH2	0x68	/**< address match2 */
#define	SIBA_CFG0_ADMATCH1	0x70	/**< address match1 */
#define	SIBA_CFG0_IMSTATE	0x90	/**< initiator agent state */
#define	SIBA_CFG0_INTVEC	0x94	/**< interrupt mask */
#define	SIBA_CFG0_TMSTATELOW	0x98	/**< target state */
#define	SIBA_CFG0_TMSTATEHIGH	0x9c	/**< target state */
#define	SIBA_CFG0_BWA0		0xa0	/**< bandwidth allocation table0 */
#define	SIBA_CFG0_IMCONFIGLOW	0xa8	/**< initiator configuration */
#define	SIBA_CFG0_IMCONFIGHIGH	0xac	/**< initiator configuration */
#define	SIBA_CFG0_ADMATCH0	0xb0	/**< address match0 */
#define	SIBA_CFG0_TMCONFIGLOW	0xb8	/**< target configuration */
#define	SIBA_CFG0_TMCONFIGHIGH	0xbc	/**< target configuration */
#define	SIBA_CFG0_BCONFIG	0xc0	/**< broadcast configuration */
#define	SIBA_CFG0_BSTATE	0xc8	/**< broadcast state */
#define	SIBA_CFG0_ACTCNFG	0xd8	/**< activate configuration */
#define	SIBA_CFG0_FLAGST	0xe8	/**< current sbflags */
#define	SIBA_CFG0_IDLOW		0xf8	/**< identification */
#define	SIBA_CFG0_IDHIGH	0xfc	/**< identification */

/* SIBA_CFG1 registers (sonics >= 2.3) */
#define	SIBA_CFG1_IMERRLOGA	0xa8	/**< (sonics >= 2.3) */
#define	SIBA_CFG1_IMERRLOG	0xb0	/**< sbtmerrlog (sonics >= 2.3) */
#define	SIBA_CFG1_TMPORTCONNID0	0xd8	/**< sonics >= 2.3 */
#define	SIBA_CFG1_TMPORTLOCK0	0xf8	/**< sonics >= 2.3 */

/* sbipsflag */
#define	SIBA_IPS_INT1_MASK	0x3f		/* which sbflags get routed to mips interrupt 1 */
#define	SIBA_IPS_INT1_SHIFT	0
#define	SIBA_IPS_INT2_MASK	0x3f00		/* which sbflags get routed to mips interrupt 2 */
#define	SIBA_IPS_INT2_SHIFT	8
#define	SIBA_IPS_INT3_MASK	0x3f0000	/* which sbflags get routed to mips interrupt 3 */
#define	SIBA_IPS_INT3_SHIFT	16
#define	SIBA_IPS_INT4_MASK	0x3f000000	/* which sbflags get routed to mips interrupt 4 */
#define	SIBA_IPS_INT4_SHIFT	24

#define	SIBA_IPS_INT_SHIFT(_i)	((_i - 1) * 8)
#define	SIBA_IPS_INT_MASK(_i)	(SIBA_IPS_INT1_MASK << SIBA_IPS_INT_SHIFT(_i))

/* sbtpsflag */
#define	SIBA_TPS_NUM0_MASK	0x3f		/* interrupt sbFlag # generated by this core */
#define	SIBA_TPS_NUM0_SHIFT	0
#define	SIBA_TPS_F0EN0		0x40		/* interrupt is always sent on the backplane */

/* sbtmerrlog */
#define	SIBA_TMEL_CM		0x00000007	/* command */
#define	SIBA_TMEL_CI		0x0000ff00	/* connection id */
#define	SIBA_TMEL_EC		0x0f000000	/* error code */
#define	SIBA_TMEL_ME		0x80000000	/* multiple error */

/* sbimstate */
#define	SIBA_IM_PC		0xf		/* pipecount */
#define	SIBA_IM_AP_MASK		0x30		/* arbitration policy */
#define	SIBA_IM_AP_BOTH		0x00		/* use both timeslaces and token */
#define	SIBA_IM_AP_TS		0x10		/* use timesliaces only */
#define	SIBA_IM_AP_TK		0x20		/* use token only */
#define	SIBA_IM_AP_RSV		0x30		/* reserved */
#define	SIBA_IM_IBE		0x20000		/* inbanderror */
#define	SIBA_IM_TO		0x40000		/* timeout */
#define	SIBA_IM_BY		0x01800000	/* busy (sonics >= 2.3) */
#define	SIBA_IM_RJ		0x02000000	/* reject (sonics >= 2.3) */

/* sbtmstatelow */
#define	SIBA_TML_RESET		0x0001		/* reset */
#define	SIBA_TML_REJ_MASK	0x0006		/* reject field */
#define	SIBA_TML_REJ		0x0002		/* reject */
#define	SIBA_TML_TMPREJ		0x0004		/* temporary reject, for error recovery */
#define	SIBA_TML_SICF_MASK	0xFFFF0000	/* core IOCTL flags */
#define	SIBA_TML_SICF_SHIFT	16

/* sbtmstatehigh */
#define	SIBA_TMH_SERR		0x0001		/* serror */
#define	SIBA_TMH_INT		0x0002		/* interrupt */
#define	SIBA_TMH_BUSY		0x0004		/* busy */
#define	SIBA_TMH_TO		0x0020		/* timeout (sonics >= 2.3) */
#define	SIBA_TMH_SISF_MASK	0xFFFF0000	/* core IOST flags */
#define	SIBA_TMH_SISF_SHIFT	16

/* sbbwa0 */
#define	SIBA_BWA_TAB0_MASK	0xffff		/* lookup table 0 */
#define	SIBA_BWA_TAB1_MASK	0xffff		/* lookup table 1 */
#define	SIBA_BWA_TAB1_SHIFT	16

/* sbimconfiglow */
#define	SIBA_IMCL_STO_MASK	0x7		/* service timeout */
#define	SIBA_IMCL_RTO_MASK	0x70		/* request timeout */
#define	SIBA_IMCL_RTO_SHIFT	4
#define	SIBA_IMCL_CID_MASK	0xff0000	/* connection id */
#define	SIBA_IMCL_CID_SHIFT	16

/* sbimconfighigh */
#define	SIBA_IMCH_IEM_MASK	0xc		/* inband error mode */
#define	SIBA_IMCH_TEM_MASK	0x30		/* timeout error mode */
#define	SIBA_IMCH_TEM_SHIFT	4
#define	SIBA_IMCH_BEM_MASK	0xc0		/* bus error mode */
#define	SIBA_IMCH_BEM_SHIFT	6

/* sbadmatch0-4 */
#define	SIBA_AM_TYPE_MASK	0x3		/* address type */
#define	SIBA_AM_TYPE_SHIFT	0x0
#define	SIBA_AM_AD64		0x4		/* reserved */
#define	SIBA_AM_ADINT0_MASK	0xf8		/* type0 size */
#define	SIBA_AM_ADINT0_SHIFT	3
#define	SIBA_AM_ADINT1_MASK	0x1f8		/* type1 size */
#define	SIBA_AM_ADINT1_SHIFT	3
#define	SIBA_AM_ADINT2_MASK	0x1f8		/* type2 size */
#define	SIBA_AM_ADINT2_SHIFT	3
#define	SIBA_AM_ADEN		0x400		/* enable */
#define	SIBA_AM_ADNEG		0x800		/* negative decode */
#define	SIBA_AM_BASE0_MASK	0xffffff00	/* type0 base address */
#define	SIBA_AM_BASE0_SHIFT	8
#define	SIBA_AM_BASE1_MASK	0xfffff000	/* type1 base address for the core */
#define	SIBA_AM_BASE1_SHIFT	12
#define	SIBA_AM_BASE2_MASK	0xffff0000	/* type2 base address for the core */
#define	SIBA_AM_BASE2_SHIFT	16

/* sbtmconfiglow */
#define	SIBA_TMCL_CD_MASK	0xff		/* clock divide */
#define	SIBA_TMCL_CO_MASK	0xf800		/* clock offset */
#define	SIBA_TMCL_CO_SHIFT	11
#define	SIBA_TMCL_IF_MASK	0xfc0000	/* interrupt flags */
#define	SIBA_TMCL_IF_SHIFT	18
#define	SIBA_TMCL_IM_MASK	0x3000000	/* interrupt mode */
#define	SIBA_TMCL_IM_SHIFT	24

/* sbtmconfighigh */
#define	SIBA_TMCH_BM_MASK	0x3		/* busy mode */
#define	SIBA_TMCH_RM_MASK	0x3		/* retry mode */
#define	SIBA_TMCH_RM_SHIFT	2
#define	SIBA_TMCH_SM_MASK	0x30		/* stop mode */
#define	SIBA_TMCH_SM_SHIFT	4
#define	SIBA_TMCH_EM_MASK	0x300		/* sb error mode */
#define	SIBA_TMCH_EM_SHIFT	8
#define	SIBA_TMCH_IM_MASK	0xc00		/* int mode */
#define	SIBA_TMCH_IM_SHIFT	10

/* sbbconfig */
#define	SIBA_BC_LAT_MASK	0x3		/* sb latency */
#define	SIBA_BC_MAX0_MASK	0xf0000		/* maxccntr0 */
#define	SIBA_BC_MAX0_SHIFT	16
#define	SIBA_BC_MAX1_MASK	0xf00000	/* maxccntr1 */
#define	SIBA_BC_MAX1_SHIFT	20

/* sbbstate */
#define	SIBA_BS_SRD		0x1		/* st reg disable */
#define	SIBA_BS_HRD		0x2		/* hold reg disable */

/* sbidlow */
#define	SIBA_IDL_CS_MASK	0x3		/* config space */
#define	SIBA_IDL_CS_SHIFT	0
#define	SIBA_IDL_NRADDR_MASK	0x38		/* # address ranges supported */
#define	SIBA_IDL_NRADDR_SHIFT	3
#define	SIBA_IDL_SYNCH		0x40		/* sync */
#define	SIBA_IDL_INIT		0x80		/* initiator */
#define	SIBA_IDL_MINLAT_MASK	0xf00		/* minimum backplane latency */
#define	SIBA_IDL_MINLAT_SHIFT	8
#define	SIBA_IDL_MAXLAT_MASK	0xf000		/* maximum backplane latency */
#define	SIBA_IDL_MAXLAT_SHIFT	12
#define	SIBA_IDL_FIRST_MASK	0x10000		/* this initiator is first */
#define	SIBA_IDL_FIRST_SHIFT	16
#define	SIBA_IDL_CW_MASK	0xc0000		/* cycle counter width */
#define	SIBA_IDL_CW_SHIFT	18
#define	SIBA_IDL_TP_MASK	0xf00000	/* target ports */
#define	SIBA_IDL_TP_SHIFT	20
#define	SIBA_IDL_IP_MASK	0xf000000	/* initiator ports */
#define	SIBA_IDL_IP_SHIFT	24
#define	SIBA_IDL_SBREV_MASK	0xf0000000	/* sonics backplane revision code */
#define	SIBA_IDL_SBREV_SHIFT	28
#define	SIBA_IDL_SBREV_2_2	0x0		/* version 2.2 or earlier */
#define	SIBA_IDL_SBREV_2_3	0x1		/* version 2.3 */

/* sbidhigh */
#define	SIBA_IDH_RC_MASK	0x000f		/* revision code */
#define	SIBA_IDH_RCE_MASK	0x7000		/* revision code extension field */
#define	SIBA_IDH_RCE_SHIFT	8
#define	SIBA_IDH_DEVICE_MASK	0x8ff0		/* core code */
#define	SIBA_IDH_DEVICE_SHIFT	4
#define	SIBA_IDH_VENDOR_MASK	0xffff0000	/* vendor code */
#define	SIBA_IDH_VENDOR_SHIFT	16

#define	SIBA_IDH_CORE_REV(sbidh) \
	(SIBA_REG_GET((sbidh), IDH_RCE) | ((sbidh) & SIBA_IDH_RC_MASK))

#define	SIBA_COMMIT		0xfd8		/* update buffered registers value */

#endif /* _BHND_SIBA_SIBAREG_ */
