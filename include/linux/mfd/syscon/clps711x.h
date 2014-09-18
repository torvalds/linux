/*
 *  CLPS711X system register bits definitions
 *
 *  Copyright (C) 2013 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _LINUX_MFD_SYSCON_CLPS711X_H_
#define _LINUX_MFD_SYSCON_CLPS711X_H_

#define SYSCON_OFFSET		(0x00)
#define SYSFLG_OFFSET		(0x40)

#define SYSCON1_KBDSCAN(x)	((x) & 15)
#define SYSCON1_KBDSCAN_MASK	(15)
#define SYSCON1_TC1M		(1 << 4)
#define SYSCON1_TC1S		(1 << 5)
#define SYSCON1_TC2M		(1 << 6)
#define SYSCON1_TC2S		(1 << 7)
#define SYSCON1_BZTOG		(1 << 9)
#define SYSCON1_BZMOD		(1 << 10)
#define SYSCON1_DBGEN		(1 << 11)
#define SYSCON1_LCDEN		(1 << 12)
#define SYSCON1_CDENTX		(1 << 13)
#define SYSCON1_CDENRX		(1 << 14)
#define SYSCON1_SIREN		(1 << 15)
#define SYSCON1_ADCKSEL(x)	(((x) & 3) << 16)
#define SYSCON1_ADCKSEL_MASK	(3 << 16)
#define SYSCON1_EXCKEN		(1 << 18)
#define SYSCON1_WAKEDIS		(1 << 19)
#define SYSCON1_IRTXM		(1 << 20)

#define SYSCON2_SERSEL		(1 << 0)
#define SYSCON2_KBD6		(1 << 1)
#define SYSCON2_DRAMZ		(1 << 2)
#define SYSCON2_KBWEN		(1 << 3)
#define SYSCON2_SS2TXEN		(1 << 4)
#define SYSCON2_PCCARD1		(1 << 5)
#define SYSCON2_PCCARD2		(1 << 6)
#define SYSCON2_SS2RXEN		(1 << 7)
#define SYSCON2_SS2MAEN		(1 << 9)
#define SYSCON2_OSTB		(1 << 12)
#define SYSCON2_CLKENSL		(1 << 13)
#define SYSCON2_BUZFREQ		(1 << 14)

#define SYSCON3_ADCCON		(1 << 0)
#define SYSCON3_CLKCTL0		(1 << 1)
#define SYSCON3_CLKCTL1		(1 << 2)
#define SYSCON3_DAISEL		(1 << 3)
#define SYSCON3_ADCCKNSEN	(1 << 4)
#define SYSCON3_VERSN(x)	(((x) >> 5) & 7)
#define SYSCON3_VERSN_MASK	(7 << 5)
#define SYSCON3_FASTWAKE	(1 << 8)
#define SYSCON3_DAIEN		(1 << 9)
#define SYSCON3_128FS		SYSCON3_DAIEN
#define SYSCON3_ENPD67		(1 << 10)

#define SYSCON_UARTEN		(1 << 8)

#define SYSFLG1_MCDR		(1 << 0)
#define SYSFLG1_DCDET		(1 << 1)
#define SYSFLG1_WUDR		(1 << 2)
#define SYSFLG1_WUON		(1 << 3)
#define SYSFLG1_CTS		(1 << 8)
#define SYSFLG1_DSR		(1 << 9)
#define SYSFLG1_DCD		(1 << 10)
#define SYSFLG1_NBFLG		(1 << 12)
#define SYSFLG1_RSTFLG		(1 << 13)
#define SYSFLG1_PFFLG		(1 << 14)
#define SYSFLG1_CLDFLG		(1 << 15)
#define SYSFLG1_CRXFE		(1 << 24)
#define SYSFLG1_CTXFF		(1 << 25)
#define SYSFLG1_SSIBUSY		(1 << 26)
#define SYSFLG1_ID		(1 << 29)
#define SYSFLG1_VERID(x)	(((x) >> 30) & 3)
#define SYSFLG1_VERID_MASK	(3 << 30)

#define SYSFLG2_SSRXOF		(1 << 0)
#define SYSFLG2_RESVAL		(1 << 1)
#define SYSFLG2_RESFRM		(1 << 2)
#define SYSFLG2_SS2RXFE		(1 << 3)
#define SYSFLG2_SS2TXFF		(1 << 4)
#define SYSFLG2_SS2TXUF		(1 << 5)
#define SYSFLG2_CKMODE		(1 << 6)

#define SYSFLG_UBUSY		(1 << 11)
#define SYSFLG_URXFE		(1 << 22)
#define SYSFLG_UTXFF		(1 << 23)

#endif
