#ifndef __ASM_SH_MIGOR_H
#define __ASM_SH_MIGOR_H

/*
 * linux/include/asm-sh/migor.h
 *
 * Copyright (C) 2008 Renesas Solutions
 *
 * Portions Copyright (C) 2007 Nobuhiro Iwamatsu
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */
#include <asm/addrspace.h>

/* GPIO */
#define MSTPCR0 0xa4150030
#define MSTPCR1 0xa4150034
#define MSTPCR2 0xa4150038

#define PORT_PACR 0xa4050100
#define PORT_PDCR 0xa4050106
#define PORT_PECR 0xa4050108
#define PORT_PHCR 0xa405010e
#define PORT_PJCR 0xa4050110
#define PORT_PKCR 0xa4050112
#define PORT_PLCR 0xa4050114
#define PORT_PMCR 0xa4050116
#define PORT_PRCR 0xa405011c
#define PORT_PWCR 0xa4050146
#define PORT_PXCR 0xa4050148
#define PORT_PYCR 0xa405014a
#define PORT_PZCR 0xa405014c
#define PORT_PADR 0xa4050120
#define PORT_PWDR 0xa4050166

#define PORT_HIZCRA 0xa4050158
#define PORT_HIZCRC 0xa405015c

#define PORT_MSELCRB 0xa4050182

#define MSTPCR1 0xa4150034
#define MSTPCR2 0xa4150038

#define PORT_PSELA 0xa405014e
#define PORT_PSELB 0xa4050150
#define PORT_PSELC 0xa4050152
#define PORT_PSELD 0xa4050154

#define PORT_HIZCRA 0xa4050158
#define PORT_HIZCRB 0xa405015a
#define PORT_HIZCRC 0xa405015c

#define BSC_CS6ABCR 0xfec1001c

#endif /* __ASM_SH_MIGOR_H */
