/*
 * include/asm-sh/cpu-sh4/ubc.h
 *
 * Copyright (C) 1999 Niibe Yutaka
 * Copyright (C) 2003 Paul Mundt
 * Copyright (C) 2006 Lineo Solutions Inc. support SH4A UBC
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH4_UBC_H
#define __ASM_CPU_SH4_UBC_H

#if defined(CONFIG_CPU_SH4A)
#define UBC_CBR0		0xff200000
#define UBC_CRR0		0xff200004
#define UBC_CAR0		0xff200008
#define UBC_CAMR0		0xff20000c
#define UBC_CBR1		0xff200020
#define UBC_CRR1		0xff200024
#define UBC_CAR1		0xff200028
#define UBC_CAMR1		0xff20002c
#define UBC_CDR1		0xff200030
#define UBC_CDMR1		0xff200034
#define UBC_CETR1		0xff200038
#define UBC_CCMFR		0xff200600
#define UBC_CBCR		0xff200620

/* CBR	*/
#define UBC_CBR_AIE		(0x01<<30)
#define UBC_CBR_ID_INST		(0x01<<4)
#define UBC_CBR_RW_READ		(0x01<<1)
#define UBC_CBR_CE		(0x01)

#define	UBC_CBR_AIV_MASK	(0x00FF0000)
#define	UBC_CBR_AIV_SHIFT	(16)
#define UBC_CBR_AIV_SET(asid)	(((asid)<<UBC_CBR_AIV_SHIFT) & UBC_CBR_AIV_MASK)

#define UBC_CBR_INIT		0x20000000

/* CRR	*/
#define UBC_CRR_RES		(0x01<<13)
#define UBC_CRR_PCB		(0x01<<1)
#define UBC_CRR_BIE		(0x01)

#define UBC_CRR_INIT		0x00002000

#else	/* CONFIG_CPU_SH4 */
#define UBC_BARA		0xff200000
#define UBC_BAMRA		0xff200004
#define UBC_BBRA		0xff200008
#define UBC_BASRA		0xff000014
#define UBC_BARB		0xff20000c
#define UBC_BAMRB		0xff200010
#define UBC_BBRB		0xff200014
#define UBC_BASRB		0xff000018
#define UBC_BDRB		0xff200018
#define UBC_BDMRB		0xff20001c
#define UBC_BRCR		0xff200020
#endif	/* CONFIG_CPU_SH4 */

#endif /* __ASM_CPU_SH4_UBC_H */

