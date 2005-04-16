/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000,2001 by Hiroyuki Kondo
 */

#ifndef __ASSEMBLY__

typedef	void	V;
typedef	char	B;
typedef	short	S;
typedef	int		W;
typedef	long	L;
typedef	float	F;
typedef	double	D;
typedef	unsigned char	UB;
typedef	unsigned short	US;
typedef	unsigned int	UW;
typedef	unsigned long	UL;
typedef	const unsigned int	CUW;

/*********************************

M32102 ICU

*********************************/
#define		ICUISTS		(UW *)0xa0EFF004
#define		ICUIREQ0	(UW *)0xa0EFF008
#define		ICUIREQ1	(UW *)0xa0EFF00C

#define		ICUSBICR	(UW *)0xa0EFF018
#define		ICUIMASK	(UW *)0xa0EFF01C

#define		ICUCR1		(UW *)0xa0EFF200	/* INT0 */
#define		ICUCR2		(UW *)0xa0EFF204	/* INT1 */
#define		ICUCR3		(UW *)0xa0EFF208	/* INT2 */
#define		ICUCR4		(UW *)0xa0EFF20C	/* INT3 */
#define		ICUCR5		(UW *)0xa0EFF210	/* INT4 */
#define		ICUCR6		(UW *)0xa0EFF214	/* INT5 */
#define		ICUCR7		(UW *)0xa0EFF218	/* INT6 */

#define		ICUCR16		(UW *)0xa0EFF23C	/* MFT0 */
#define		ICUCR17		(UW *)0xa0EFF240	/* MFT1 */
#define		ICUCR18		(UW *)0xa0EFF244	/* MFT2 */
#define		ICUCR19		(UW *)0xa0EFF248	/* MFT3 */
#define		ICUCR20		(UW *)0xa0EFF24C	/* MFT4 */
#define		ICUCR21		(UW *)0xa0EFF250	/* MFT5 */

#define		ICUCR32		(UW *)0xa0EFF27C	/* DMA0 */
#define		ICUCR33		(UW *)0xa0EFF280	/* DMA1 */

#define		ICUCR48		(UW *)0xa0EFF2BC	/* SIO0R */
#define		ICUCR49		(UW *)0xa0EFF2C0	/* SIO0S */
#define		ICUCR50		(UW *)0xa0EFF2C4	/* SIO1R */
#define		ICUCR51		(UW *)0xa0EFF2C8	/* SIO1S */
#define		ICUCR52		(UW *)0xa0EFF2CC	/* SIO2R */
#define		ICUCR53		(UW *)0xa0EFF2D0	/* SIO2S */
#define		ICUCR54		(UW *)0xa0EFF2D4	/* SIO3R */
#define		ICUCR55		(UW *)0xa0EFF2D8	/* SIO3S */
#define		ICUCR56		(UW *)0xa0EFF2DC	/* SIO4R */
#define		ICUCR57		(UW *)0xa0EFF2E0	/* SIO4S */

/*********************************

M32102 MFT

*********************************/
#define		MFTCR		(US *)0xa0EFC002
#define		MFTRPR		(UB *)0xa0EFC006

#define		MFT0MOD		(US *)0xa0EFC102
#define		MFT0BOS		(US *)0xa0EFC106
#define		MFT0CUT		(US *)0xa0EFC10A
#define		MFT0RLD		(US *)0xa0EFC10E
#define		MFT0CRLD	(US *)0xa0EFC112

#define		MFT1MOD		(US *)0xa0EFC202
#define		MFT1BOS		(US *)0xa0EFC206
#define		MFT1CUT		(US *)0xa0EFC20A
#define		MFT1RLD		(US *)0xa0EFC20E
#define		MFT1CRLD	(US *)0xa0EFC212

#define		MFT2MOD		(US *)0xa0EFC302
#define		MFT2BOS		(US *)0xa0EFC306
#define		MFT2CUT		(US *)0xa0EFC30A
#define		MFT2RLD		(US *)0xa0EFC30E
#define		MFT2CRLD	(US *)0xa0EFC312

#define		MFT3MOD		(US *)0xa0EFC402
#define		MFT3CUT		(US *)0xa0EFC40A
#define		MFT3RLD		(US *)0xa0EFC40E
#define		MFT3CRLD	(US *)0xa0EFC412

#define		MFT4MOD		(US *)0xa0EFC502
#define		MFT4CUT		(US *)0xa0EFC50A
#define		MFT4RLD		(US *)0xa0EFC50E
#define		MFT4CRLD	(US *)0xa0EFC512

#define		MFT5MOD		(US *)0xa0EFC602
#define		MFT5CUT		(US *)0xa0EFC60A
#define		MFT5RLD		(US *)0xa0EFC60E
#define		MFT5CRLD	(US *)0xa0EFC612

/*********************************

M32102 SIO

*********************************/

#define SIO0CR     (volatile int *)0xa0efd000
#define SIO0MOD0   (volatile int *)0xa0efd004
#define SIO0MOD1   (volatile int *)0xa0efd008
#define SIO0STS    (volatile int *)0xa0efd00c
#define SIO0IMASK  (volatile int *)0xa0efd010
#define SIO0BAUR   (volatile int *)0xa0efd014
#define SIO0RBAUR  (volatile int *)0xa0efd018
#define SIO0TXB    (volatile int *)0xa0efd01c
#define SIO0RXB    (volatile int *)0xa0efd020

#define SIO1CR     (volatile int *)0xa0efd100
#define SIO1MOD0   (volatile int *)0xa0efd104
#define SIO1MOD1   (volatile int *)0xa0efd108
#define SIO1STS    (volatile int *)0xa0efd10c
#define SIO1IMASK  (volatile int *)0xa0efd110
#define SIO1BAUR   (volatile int *)0xa0efd114
#define SIO1RBAUR  (volatile int *)0xa0efd118
#define SIO1TXB    (volatile int *)0xa0efd11c
#define SIO1RXB    (volatile int *)0xa0efd120
/*********************************

M32102 PORT

*********************************/
#define		PIEN		(UB *)0xa0EF1003	/* input enable */

#define		P0DATA		(UB *)0xa0EF1020	/* data */
#define		P1DATA		(UB *)0xa0EF1021
#define		P2DATA		(UB *)0xa0EF1022
#define		P3DATA		(UB *)0xa0EF1023
#define		P4DATA		(UB *)0xa0EF1024
#define		P5DATA		(UB *)0xa0EF1025
#define		P6DATA		(UB *)0xa0EF1026
#define		P7DATA		(UB *)0xa0EF1027

#define		P0DIR		(UB *)0xa0EF1040	/* direction */
#define		P1DIR		(UB *)0xa0EF1041
#define		P2DIR		(UB *)0xa0EF1042
#define		P3DIR		(UB *)0xa0EF1043
#define		P4DIR		(UB *)0xa0EF1044
#define		P5DIR		(UB *)0xa0EF1045
#define		P6DIR		(UB *)0xa0EF1046
#define		P7DIR		(UB *)0xa0EF1047

#define		P0MOD		(US *)0xa0EF1060	/* mode control */
#define		P1MOD		(US *)0xa0EF1062
#define		P2MOD		(US *)0xa0EF1064
#define		P3MOD		(US *)0xa0EF1066
#define		P4MOD		(US *)0xa0EF1068
#define		P5MOD		(US *)0xa0EF106A
#define		P6MOD		(US *)0xa0EF106C
#define		P7MOD		(US *)0xa0EF106E

#define		P0ODCR		(UB *)0xa0EF1080	/* open-drain control */
#define		P1ODCR		(UB *)0xa0EF1081
#define		P2ODCR		(UB *)0xa0EF1082
#define		P3ODCR		(UB *)0xa0EF1083
#define		P4ODCR		(UB *)0xa0EF1084
#define		P5ODCR		(UB *)0xa0EF1085
#define		P6ODCR		(UB *)0xa0EF1086
#define		P7ODCR		(UB *)0xa0EF1087

/*********************************

M32102 Cache

********************************/

#define		MCCR	(US *)0xFFFFFFFE


#else  /* __ASSEMBLY__ */

;;
;; PIO     0x80ef1000
;;

#define PIEN          0xa0ef1000

#define P0DATA        0xa0ef1020
#define P1DATA        0xa0ef1021
#define P2DATA        0xa0ef1022
#define P3DATA        0xa0ef1023
#define P4DATA        0xa0ef1024
#define P5DATA        0xa0ef1025
#define P6DATA        0xa0ef1026
#define P7DATA        0xa0ef1027

#define P0DIR         0xa0ef1040
#define P1DIR         0xa0ef1041
#define P2DIR         0xa0ef1042
#define P3DIR         0xa0ef1043
#define P4DIR         0xa0ef1044
#define P5DIR         0xa0ef1045
#define P6DIR         0xa0ef1046
#define P7DIR         0xa0ef1047

#define P0MOD         0xa0ef1060
#define P1MOD         0xa0ef1062
#define P2MOD         0xa0ef1064
#define P3MOD         0xa0ef1066
#define P4MOD         0xa0ef1068
#define P5MOD         0xa0ef106a
#define P6MOD         0xa0ef106c
#define P7MOD         0xa0ef106e
;
#define P0ODCR        0xa0ef1080
#define P1ODCR        0xa0ef1081
#define P2ODCR        0xa0ef1082
#define P3ODCR        0xa0ef1083
#define P4ODCR        0xa0ef1084
#define P5ODCR        0xa0ef1085
#define P6ODCR        0xa0ef1086
#define P7ODCR        0xa0ef1087

;;
;; WDT     0xa0ef2000
;;

#define WDTCR         0xa0ef2000


;;
;; CLK     0xa0ef4000
;;

#define CPUCLKCR      0xa0ef4000
#define CLKMOD        0xa0ef4004
#define PLLCR         0xa0ef4008


;;
;; BSEL    0xa0ef5000
;;

#define BSEL0CR       0xa0ef5000
#define BSEL1CR       0xa0ef5004
#define BSEL2CR       0xa0ef5008
#define BSEL3CR       0xa0ef500c
#define BSEL4CR       0xa0ef5010
#define BSEL5CR       0xa0ef5014


;;
;; SDRAMC  0xa0ef6000
;;

#define SDRF0         0xa0ef6000
#define SDRF1         0xa0ef6004
#define SDIR0         0xa0ef6008
#define SDIR1         0xa0ef600c
#define SDBR          0xa0ef6010

;; CH0
#define SD0ADR        0xa0ef6020
#define SD0SZ         0xa0ef6022
#define SD0ER         0xa0ef6024
#define SD0TR         0xa0ef6028
#define SD0MOD        0xa0ef602c

;; CH1
#define SD1ADR        0xa0ef6040
#define SD1SZ         0xa0ef6042
#define SD1ER         0xa0ef6044
#define SD1TR         0xa0ef6048
#define SD1MOD        0xa0ef604c


;;
;; DMAC    0xa0ef8000
;;

#define DMAEN         0xa0ef8000
#define DMAISTS       0xa0ef8004
#define DMAEDET       0xa0ef8008
#define DMAASTS       0xa0ef800c

;; CH0
#define DMA0CR0       0xa0ef8100
#define DMA0CR1       0xa0ef8104
#define DMA0CSA       0xa0ef8108
#define DMA0RSA       0xa0ef810c
#define DMA0CDA       0xa0ef8110
#define DMA0RDA       0xa0ef8114
#define DMA0CBCUT     0xa0ef8118
#define DMA0RBCUT     0xa0ef811c

;; CH1
#define DMA1CR0       0xa0ef8200
#define DMA1CR1       0xa0ef8204
#define DMA1CSA       0xa0ef8208
#define DMA1RSA       0xa0ef820c
#define DMA1CDA       0xa0ef8210
#define DMA1RDA       0xa0ef8214
#define DMA1CBCUT     0xa0ef8218
#define DMA1RBCUT     0xa0ef821c


;;
;; MFT     0xa0efc000
;;

#define MFTCR        0xa0efc000
#define MFTRPR       0xa0efc004

;; CH0
#define MFT0MOD      0xa0efc100
#define MFT0BOS      0xa0efc104
#define MFT0CUT      0xa0efc108
#define MFT0RLD      0xa0efc10c
#define MFT0CMPRLD   0xa0efc110

;; CH1
#define MFT1MOD      0xa0efc200
#define MFT1BOS      0xa0efc204
#define MFT1CUT      0xa0efc208
#define MFT1RLD      0xa0efc20c
#define MFT1CMPRLD   0xa0efc210

;; CH2
#define MFT2MOD      0xa0efc300
#define MFT2BOS      0xa0efc304
#define MFT2CUT      0xa0efc308
#define MFT2RLD      0xa0efc30c
#define MFT2CMPRLD   0xa0efc310

;; CH3
#define MFT3MOD      0xa0efc400
#define MFT3BOS      0xa0efc404
#define MFT3CUT      0xa0efc408
#define MFT3RLD      0xa0efc40c
#define MFT3CMPRLD   0xa0efc410

;; CH4
#define MFT4MOD      0xa0efc500
#define MFT4BOS      0xa0efc504
#define MFT4CUT      0xa0efc508
#define MFT4RLD      0xa0efc50c
#define MFT4CMPRLD   0xa0efc510

;; CH5
#define MFT5MOD      0xa0efc600
#define MFT5BOS      0xa0efc604
#define MFT5CUT      0xa0efc608
#define MFT5RLD      0xa0efc60c
#define MFT5CMPRLD   0xa0efc610


;;
;; SIO     0xa0efd000
;;

;; CH0
#define SIO0CR        0xa0efd000
#define SIO0MOD0      0xa0efd004
#define SIO0MOD1      0xa0efd008
#define SIO0STS       0xa0efd00c
#define SIO0IMASK     0xa0efd010
#define SIO0BAUR      0xa0efd014
#define SIO0RBAUR     0xa0efd018
#define SIO0TXB       0xa0efd01c
#define SIO0RXB       0xa0efd020

;; CH1
#define SIO1CR        0xa0efd100
#define SIO1MOD0      0xa0efd104
#define SIO1MOD1      0xa0efd108
#define SIO1STS       0xa0efd10c
#define SIO1IMASK     0xa0efd110
#define SIO1BAUR      0xa0efd114
#define SIO1RBAUR     0xa0efd118
#define SIO1TXB       0xa0efd11c
#define SIO1RXB       0xa0efd120

;; CH2
#define SIO2CR        0xa0efd200
#define SIO2MOD0      0xa0efd204
#define SIO2MOD1      0xa0efd208
#define SIO2STS       0xa0efd20c
#define SIO2IMASK     0xa0efd210
#define SIO2BAUR      0xa0efd214
#define SIO2RBAUR     0xa0efd218
#define SIO2TXB       0xa0efd21c
#define SIO2RXB       0xa0efd220

;; CH3
#define SIO3CR        0xa0efd300
#define SIO3MOD0      0xa0efd304
#define SIO3MOD1      0xa0efd308
#define SIO3STS       0xa0efd30c
#define SIO3IMASK     0xa0efd310
#define SIO3BAUR      0xa0efd314
#define SIO3RBAUR     0xa0efd318
#define SIO3TXB       0xa0efd31c
#define SIO3RXB       0xa0efd320

;; CH4
#define SIO4CR        0xa0efd400
#define SIO4MOD0      0xa0efd404
#define SIO4MOD1      0xa0efd408
#define SIO4STS       0xa0efd40c
#define SIO4IMASK     0xa0efd410
#define SIO4BAUR      0xa0efd414
#define SIO4RBAUR     0xa0efd418
#define SIO4TXB       0xa0efd41c
#define SIO4RXB       0xa0efd420


;;
;; ICU     0xa0eff000
;;

#define ICUISTS       0xa0eff004
#define ICUIREQ0      0xa0eff008
#define ICUIREQ1      0xa0eff00c

#define ICUSBICR      0xa0eff018
#define ICUIMASK      0xa0eff01c

#define ICUCR1        0xa0eff200
#define ICUCR2        0xa0eff204
#define ICUCR3        0xa0eff208
#define ICUCR4        0xa0eff20c
#define ICUCR5        0xa0eff210
#define ICUCR6        0xa0eff214
#define ICUCR7        0xa0eff218

#define ICUCR16       0xa0eff23c
#define ICUCR17       0xa0eff240
#define ICUCR18       0xa0eff244
#define ICUCR19       0xa0eff248
#define ICUCR20       0xa0eff24c
#define ICUCR21       0xa0eff250

#define ICUCR32       0xa0eff27c
#define ICUCR33       0xa0eff280

#define ICUCR48       0xa0eff2bc
#define ICUCR49       0xa0eff2c0
#define ICUCR50       0xa0eff2c4
#define ICUCR51       0xa0eff2c8
#define ICUCR52       0xa0eff2cc
#define ICUCR53       0xa0eff2d0
#define ICUCR54       0xa0eff2d4
#define ICUCR55       0xa0eff2d8
#define ICUCR56       0xa0eff2dc
#define ICUCR57       0xa0eff2e0

;;
;; CACHE
;;

#define MCCR		  0xfffffffc


#endif  /* __ASSEMBLY__ */
