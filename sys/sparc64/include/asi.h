/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1997 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Berkeley Software Design Inc's name may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN INC BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: BSDI: asi.h,v 1.3 1997/08/08 14:31:42 torek
 * $FreeBSD$
 */

#ifndef	_MACHINE_ASI_H_
#define	_MACHINE_ASI_H_

/*
 * Standard v9 ASIs
 */
#define	ASI_N					0x4
#define	ASI_NL					0xc
#define	ASI_AIUP				0x10
#define	ASI_AIUS				0x11
#define	ASI_AIUPL				0x18
#define	ASI_AIUSL				0x19
#define	ASI_P					0x80
#define	ASI_S					0x81
#define	ASI_PNF					0x82
#define	ASI_SNF					0x83
#define	ASI_PL					0x88
#define	ASI_SL					0x89
#define	ASI_PNFL				0x8a
#define	ASI_SNFL				0x8b

/*
 * UltraSPARC extensions - ASIs limited to a certain family are annotated.
 */
#define	ASI_PHYS_USE_EC				0x14
#define	ASI_PHYS_BYPASS_EC_WITH_EBIT		0x15
#define	ASI_PHYS_USE_EC_L			0x1c
#define	ASI_PHYS_BYPASS_EC_WITH_EBIT_L		0x1d

#define	ASI_NUCLEUS_QUAD_LDD			0x24
#define	ASI_NUCLEUS_QUAD_LDD_L			0x2c

#define	ASI_PCACHE_STATUS_DATA			0x30	/* US-III Cu */
#define	ASI_PCACHE_DATA				0x31	/* US-III Cu */
#define	ASI_PCACHE_TAG				0x32	/* US-III Cu */
#define	ASI_PCACHE_SNOOP_TAG			0x33	/* US-III Cu */

#define	ASI_ATOMIC_QUAD_LDD_PHYS		0x34	/* US-III Cu */

#define	ASI_WCACHE_VALID_BITS			0x38	/* US-III Cu */
#define	ASI_WCACHE_DATA				0x39	/* US-III Cu */
#define	ASI_WCACHE_TAG				0x3a	/* US-III Cu */
#define	ASI_WCACHE_SNOOP_TAG			0x3b	/* US-III Cu */

#define	ASI_ATOMIC_QUAD_LDD_PHYS_L		0x3c	/* US-III Cu */

#define	ASI_SRAM_FAST_INIT			0x40	/* US-III Cu */

#define	ASI_DCACHE_INVALIDATE			0x42	/* US-III Cu */
#define	ASI_DCACHE_UTAG				0x43	/* US-III Cu */
#define	ASI_DCACHE_SNOOP_TAG			0x44	/* US-III Cu */

/* Named ASI_DCUCR on US-III, but is mostly identical except for added bits. */
#define	ASI_LSU_CTL_REG				0x45	/* US only */

#define	ASI_MCNTL				0x45	/* SPARC64 only */
#define		AA_MCNTL			0x08

#define	ASI_DCACHE_DATA				0x46
#define	ASI_DCACHE_TAG				0x47

#define	ASI_INTR_DISPATCH_STATUS		0x48
#define	ASI_INTR_RECEIVE			0x49
#define	ASI_UPA_CONFIG_REG			0x4a	/* US-I, II */

#define	ASI_FIREPLANE_CONFIG_REG		0x4a	/* US-III{,+}, IV{,+} */
#define		AA_FIREPLANE_CONFIG		0x0	/* US-III{,+}, IV{,+} */
#define		AA_FIREPLANE_ADDRESS		0x8	/* US-III{,+}, IV{,+} */
#define		AA_FIREPLANE_CONFIG_2		0x10	/* US-IV{,+} */

#define	ASI_JBUS_CONFIG_REG			0x4a	/* US-IIIi{,+} */

#define	ASI_ESTATE_ERROR_EN_REG			0x4b
#define		AA_ESTATE_CEEN			0x1
#define		AA_ESTATE_NCEEN			0x2
#define		AA_ESTATE_ISAPEN		0x4

#define	ASI_AFSR				0x4c
#define	ASI_AFAR				0x4d

#define	ASI_ECACHE_TAG_DATA			0x4e

#define	ASI_IMMU_TAG_TARGET_REG			0x50
#define	ASI_IMMU				0x50
#define		AA_IMMU_TTR			0x0
#define		AA_IMMU_SFSR			0x18
#define		AA_IMMU_TSB			0x28
#define		AA_IMMU_TAR			0x30
#define		AA_IMMU_TSB_PEXT_REG		0x48	/* US-III family */
#define		AA_IMMU_TSB_SEXT_REG		0x50	/* US-III family */
#define		AA_IMMU_TSB_NEXT_REG		0x58	/* US-III family */

#define	ASI_IMMU_TSB_8KB_PTR_REG		0x51
#define	ASI_IMMU_TSB_64KB_PTR_REG		0x52

#define	ASI_SERIAL_ID				0x53	/* US-III family */

#define	ASI_ITLB_DATA_IN_REG			0x54
/* US-III Cu: also ASI_ITLB_CAM_ADDRESS_REG */
#define	ASI_ITLB_DATA_ACCESS_REG		0x55
#define	ASI_ITLB_TAG_READ_REG			0x56
#define	ASI_IMMU_DEMAP				0x57

#define	ASI_DMMU_TAG_TARGET_REG			0x58
#define	ASI_DMMU				0x58
#define		AA_DMMU_TTR			0x0
#define		AA_DMMU_PCXR			0x8
#define		AA_DMMU_SCXR			0x10
#define		AA_DMMU_SFSR			0x18
#define		AA_DMMU_SFAR			0x20
#define		AA_DMMU_TSB			0x28
#define		AA_DMMU_TAR			0x30
#define		AA_DMMU_VWPR			0x38
#define		AA_DMMU_PWPR			0x40
#define		AA_DMMU_TSB_PEXT_REG		0x48
#define		AA_DMMU_TSB_SEXT_REG		0x50
#define		AA_DMMU_TSB_NEXT_REG		0x58
#define		AA_DMMU_TAG_ACCESS_EXT		0x60	/* US-III family */

#define	ASI_DMMU_TSB_8KB_PTR_REG		0x59
#define	ASI_DMMU_TSB_64KB_PTR_REG		0x5a
#define	ASI_DMMU_TSB_DIRECT_PTR_REG		0x5b
#define	ASI_DTLB_DATA_IN_REG			0x5c
/* US-III Cu: also ASI_DTLB_CAM_ADDRESS_REG */
#define	ASI_DTLB_DATA_ACCESS_REG		0x5d
#define	ASI_DTLB_TAG_READ_REG			0x5e
#define	ASI_DMMU_DEMAP				0x5f

#define	ASI_IIU_INST_TRAP			0x60	/* US-III family */

#define	ASI_INTR_ID				0x63	/* US-IV{,+} */
#define		AA_INTR_ID			0x0	/* US-IV{,+} */
#define		AA_CORE_ID			0x10	/* US-IV{,+} */
#define		AA_CESR_ID			0x40	/* US-IV{,+} */

#define	ASI_ICACHE_INSTR			0x66
#define	ASI_ICACHE_TAG				0x67
#define	ASI_ICACHE_SNOOP_TAG			0x68	/* US-III family */
#define	ASI_ICACHE_PRE_DECODE			0x6e	/* US-I, II */
#define	ASI_ICACHE_PRE_NEXT_FIELD		0x6f	/* US-I, II */

#define	ASI_FLUSH_L1I				0x67	/* SPARC64 only */

#define	ASI_BLK_AUIP				0x70
#define	ASI_BLK_AIUS				0x71

#define	ASI_MCU_CONFIG_REG			0x72	/* US-III Cu */
#define		AA_MCU_TIMING1_REG		0x0	/* US-III Cu */
#define		AA_MCU_TIMING2_REG		0x8	/* US-III Cu */
#define		AA_MCU_TIMING3_REG		0x10	/* US-III Cu */
#define		AA_MCU_TIMING4_REG		0x18	/* US-III Cu */
#define		AA_MCU_DEC1_REG			0x20	/* US-III Cu */
#define		AA_MCU_DEC2_REG			0x28	/* US-III Cu */
#define		AA_MCU_DEC3_REG			0x30	/* US-III Cu */
#define		AA_MCU_DEC4_REG			0x38	/* US-III Cu */
#define		AA_MCU_ADDR_CNTL_REG		0x40	/* US-III Cu */

#define	ASI_ECACHE_DATA				0x74	/* US-III Cu */
#define	ASI_ECACHE_CONTROL			0x75	/* US-III Cu */
#define	ASI_ECACHE_W				0x76

/*
 * With the advent of the US-III, the numbering has changed, as additional
 * registers were inserted in between.  We retain the original ordering for
 * now, and append an A to the inserted registers.
 * Exceptions are AA_SDB_INTR_D6 and AA_SDB_INTR_D7, which were appended
 * at the end.
 */
#define	ASI_SDB_ERROR_W				0x77
#define	ASI_SDB_CONTROL_W			0x77
#define	ASI_SDB_INTR_W				0x77
#define		AA_SDB_ERR_HIGH			0x0
#define		AA_SDB_ERR_LOW			0x18
#define		AA_SDB_CNTL_HIGH		0x20
#define		AA_SDB_CNTL_LOW			0x38
#define		AA_SDB_INTR_D0			0x40
#define		AA_SDB_INTR_D0A			0x48	/* US-III family */
#define		AA_SDB_INTR_D1			0x50
#define		AA_SDB_INTR_D1A			0x5A	/* US-III family */
#define		AA_SDB_INTR_D2			0x60
#define		AA_SDB_INTR_D2A			0x68	/* US-III family */
#define		AA_INTR_SEND			0x70
#define		AA_SDB_INTR_D6			0x80	/* US-III family */
#define		AA_SDB_INTR_D7			0x88	/* US-III family */

#define	ASI_BLK_AIUPL				0x78
#define	ASI_BLK_AIUSL				0x79

#define	ASI_ECACHE_R				0x7e

/*
 * These have the same registers as their corresponding write versions
 * except for AA_INTR_SEND.
 */
#define	ASI_SDB_ERROR_R				0x7f
#define	ASI_SDB_CONTROL_R			0x7f
#define	ASI_SDB_INTR_R				0x7f

#define	ASI_PST8_P				0xc0
#define	ASI_PST8_S				0xc1
#define	ASI_PST16_P				0xc2
#define	ASI_PST16_S				0xc3
#define	ASI_PST32_P				0xc4
#define	ASI_PST32_S				0xc5

#define	ASI_PST8_PL				0xc8
#define	ASI_PST8_SL				0xc9
#define	ASI_PST16_PL				0xca
#define	ASI_PST16_SL				0xcb
#define	ASI_PST32_PL				0xcc
#define	ASI_PST32_SL				0xcd

#define	ASI_FL8_P				0xd0
#define	ASI_FL8_S				0xd1
#define	ASI_FL16_P				0xd2
#define	ASI_FL16_S				0xd3
#define	ASI_FL8_PL				0xd8
#define	ASI_FL8_SL				0xd9
#define	ASI_FL16_PL				0xda
#define	ASI_FL16_SL				0xdb

#define	ASI_BLK_COMMIT_P			0xe0
#define	ASI_BLK_COMMIT_S			0xe1
#define	ASI_BLK_P				0xf0
#define	ASI_BLK_S				0xf1
#define	ASI_BLK_PL				0xf8
#define	ASI_BLK_SL				0xf9

#endif /* !_MACHINE_ASI_H_ */
