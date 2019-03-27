/*-
 * Dallas DS2153, DS21x54 single-chip E1 tranceiver registers.
 *
 * Copyright (C) 1996 Cronyx Engineering.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * Cronyx Id: ds2153.h,v 1.2.4.1 2003/11/12 17:22:33 rik Exp $
 * $FreeBSD$
 */

/*
 * Control and test registers
 */
#define DS_RCR1         0x10    /* rw - receive control 1 */
#define DS_RCR2         0x11    /* rw - receive control 2 */
#define DS_TCR1         0x12    /* rw - transmit control 1 */
#define DS_TCR2         0x13    /* rw - transmit control 2 */
#define DS_CCR1         0x14    /* rw - common control 1 */
#define DS_CCR2         0x1a    /* rw - common control 2 */
#define DS_CCR3         0x1b    /* rw - common control 3 */
#define DS_LICR         0x18    /* rw - line interface control */
#define DS_IMR1         0x16    /* rw - interrupt mask 1 */
#define DS_IMR2         0x17    /* rw - interrupt mask 2 */
#define DS_TEST1	0x15    /* rw - test 1 */
#define DS_TEST2	0x19    /* rw - test 2 */

/*
 * Status and information registers
 */
#define DS_RIR          0x08    /* r  - receive information */
#define DS_SSR          0x1e    /* r  - synchronizer status */
#define DS_SR1          0x06    /* r  - status 1 */
#define DS_SR2          0x07    /* r  - status 2 */

/*
 * Error count registers
 */
#define DS_VCR1         0x00    /* r  - BPV or code violation count 1 */
#define DS_VCR2         0x01    /* r  - BPV or code violation count 2 */
#define DS_CRCCR1       0x02    /* r  - CRC4 error count 1 */
#define DS_CRCCR2       0x03    /* r  - CRC4 error count 2 */
#define DS_EBCR1        0x04    /* r  - E-bit count 1 */
#define DS_EBCR2        0x05    /* r  - E-bit count 2 */
#define DS_FASCR1       0x02    /* r  - FAS error count 1 */
#define DS_FASCR2       0x04    /* r  - FAS error count 2 */

/*
 * Signaling registers
 */
#define DS_RS           0x30    /* r  - receive signaling 1..16 */
#define DS_TS           0x40    /* rw - transmit signaling 1..16 */

/*
 * Transmit idle registers
 */
#define DS_TIR          0x26    /* rw - transmit idle 1..4 */
#define DS_TIDR         0x2a    /* rw - transmit idle definition */

/*
 * Clock blocking registers
 */
#define DS_RCBR         0x2b    /* rw - receive channel blocking 1..4 */
#define DS_TCBR         0x22    /* rw - transmit channel blocking 1..4 */

/*
 * Slot 0 registers
 */
#define DS_RAF          0x2f    /* r  - receive align frame */
#define DS_RNAF         0x1f    /* r  - receive non-align frame */
#define DS_TAF          0x20    /* rw - transmit align frame */
#define DS_TNAF         0x21    /* rw - transmit non-align frame */

/*----------------------------------------------
 * Receive control register 1
 */
#define RCR1_RSO        0x00    /* RSYNC outputs frame boundaries */
#define RCR1_RSI        0x20    /* RSYNC is input (elastic store) */
#define RCR1_RSO_CAS    0x40    /* RSYNC outputs CAS multiframe boundaries */
#define RCR1_RSO_CRC4   0xc0    /* RSYNC outputs CRC4 multiframe boundaries */

#define RCR1_FRC        0x04    /* frame resync criteria */
#define RCR1_SYNCD      0x02    /* auto resync disable */
#define RCR1_RESYNC     0x01    /* force resync */

/*
 * Receive control register 2
 */
#define RCR2_SA_8       0x80    /* output Sa8 bit at RLINK pin */
#define RCR2_SA_7       0x40    /* output Sa7 bit at RLINK pin */
#define RCR2_SA_6       0x20    /* output Sa6 bit at RLINK pin */
#define RCR2_SA_5       0x10    /* output Sa5 bit at RLINK pin */
#define RCR2_SA_4       0x08    /* output Sa4 bit at RLINK pin */
#define RCR2_RSCLKM     0x04    /* receive side SYSCLK mode 2048 */
#define RCR2_RESE       0x02    /* receive side elastic store enable */

/*
 * Transmit control register 1
 */
#define TCR1_TFPT       0x40    /* source timeslot 0 from TSER pin */
#define TCR1_T16S       0x20    /* source timeslot 16 from TS1..TS16 regs */
#define TCR1_TUA1       0x10    /* transmit unframed all ones */
#define TCR1_TSIS       0x08    /* source Si bits from TAF/TNAF registers */
#define TCR1_TSA1       0x04    /* transmit timeslot 16 all ones */

#define TCR1_TSI        0x00    /* TSYNC is input */
#define TCR1_TSO        0x01    /* TSYNC outputs frame boundaries */
#define TCR1_TSO_MF     0x03    /* TSYNC outputs CAS/CRC4 m/f boundaries */

/*
 * Transmit control register 2
 */
#define TCR2_SA_8       0x80    /* source Sa8 bit from TLINK pin */
#define TCR2_SA_7       0x40    /* source Sa7 bit from TLINK pin */
#define TCR2_SA_6       0x20    /* source Sa6 bit from TLINK pin */
#define TCR2_SA_5       0x10    /* source Sa5 bit from TLINK pin */
#define TCR2_SA_4       0x08    /* source Sa4 bit from TLINK pin */
#define TCR2_AEBE       0x02    /* automatic E-bit enable */
#define TCR2_P16F       0x01    /* pin 16 is Loss of Transmit Clock */

/*
 * Common control register 1
 */
#define CCR1_FLOOP      0x80    /* enable framer loopback */
#define CCR1_THDB3      0x40    /* enable transmit HDB3 */
#define CCR1_TG802      0x20    /* enable transmit G.802 */
#define CCR1_TCRC4      0x10    /* enable transmit CRC4 */
#define CCR1_CCS        0x08    /* common channel signaling mode */
#define CCR1_RHDB3      0x04    /* enable receive HDB3 */
#define CCR1_RG802      0x02    /* enable receive G.802 */
#define CCR1_RCRC4      0x01    /* enable receive CRC4 */

/*
 * Common control register 2
 */
#define CCR2_EC625      0x80    /* update error counters every 62.5 ms */
#define CCR2_CNTCV      0x40    /* count code violations */
#define CCR2_AUTOAIS    0x20    /* automatic AIS generation */
#define CCR2_AUTORA     0x10    /* automatic remote alarm generation */
#define CCR2_LOFA1      0x08    /* force RSER to 1 under loss of frame align */
#define CCR2_TRCLK      0x04    /* switch transmitter to RCLK if TCLK stops */
#define CCR2_RLOOP      0x02    /* enable remote loopback */
#define CCR2_LLOOP      0x01    /* enable local loopback */

/*
 * Common control register 3
 */
#define CCR3_TESE       0x80    /* enable transmit elastic store */
#define CCR3_TCBFS	0x40    /* TCBRs define signaling bits to insert */
#define CCR3_TIRSER     0x20    /* TIRs define data channels from RSER pin */
#define CCR3_ESRESET    0x10    /* elastic store reset */
#define CCR3_LIRESET    0x08    /* line interface reset */
#define CCR3_THSE	0x04    /* insert signaling from TSIG into TSER */
#define CCR3_TSCLKM     0x02    /* transmit backplane clock 2048 */

/*
 * Line interface control register
 */
#define LICR_DB21       0x80    /* return loss 21 dB */

#define LICR_LB75       0x00    /* 75 Ohm normal */
#define LICR_LB120      0x20    /* 120 Ohm normal */
#define LICR_LB75P      0x40    /* 75 Ohm protected */
#define LICR_LB120P     0x60    /* 120 Ohm protected */

#define LICR_HIGAIN     0x10    /* receive gain 30 dB */
#define LICR_JA_TX      0x08    /* transmit side jitter attenuator select */
#define LICR_JA_LOW     0x04    /* low jitter attenuator depth (32 bits) */
#define LICR_JA_DISABLE 0x02    /* disable jitter attenuator */
#define LICR_POWERDOWN  0x01    /* transmit power down */

/*----------------------------------------------
 * Receive information register
 */
#define RIR_TES_FULL    0x80    /* transmit elastic store full */
#define RIR_TES_EMPTY   0x40    /* transmit elastic store empty */
#define RIR_JALT        0x20    /* jitter attenuation limit trip */
#define RIR_ES_FULL     0x10    /* elastic store full */
#define RIR_ES_EMPTY    0x08    /* elastic store empty */
#define RIR_RESYNC_CRC  0x04    /* CRC4 resync (915/1000 errors) */
#define RIR_RESYNC      0x02    /* frame resync (three consec errors) */
#define RIR_RESYNC_CAS  0x01    /* CAS resync (two consec errors) */

/*
 * Synchronizer status register
 */
#define SSR_CSC(v)      (((v) >> 2) & 0x3c | ((v) >> 3) & 1)
				/* CRC4 sync counter (6 bits, bit 1 n/a) */
#define SSR_SYNC        0x04    /* frame alignment sync active */
#define SSR_SYNC_CAS    0x02    /* CAS multiframe sync active */
#define SSR_SYNC_CRC4   0x01    /* CRC4 multiframe sync active */

/*
 * Status register 1
 */
#define SR1_RSA1        0x80    /* receive signaling all ones */
#define SR1_RDMA        0x40    /* receive distant multiframe alarm */
#define SR1_RSA0        0x20    /* receive signaling all zeros */
#define SR1_RSLIP       0x10    /* receive elastic store slip event */
#define SR1_RUA1        0x08    /* receive unframed all ones */
#define SR1_RRA         0x04    /* receive remote alarm */
#define SR1_RCL         0x02    /* receive carrier loss */
#define SR1_RLOS        0x01    /* receive loss of sync */

/*
 * Status register 2
 */
#define SR2_RMF         0x80    /* receive CAS multiframe (every 2 ms) */
#define SR2_RAF         0x40    /* receive align frame (every 250 us) */
#define SR2_TMF         0x20    /* transmit multiframe (every 2 ms) */
#define SR2_SEC         0x10    /* one second timer (or 62.5 ms) */
#define SR2_TAF         0x08    /* transmit align frame (every 250 us) */
#define SR2_LOTC        0x04    /* loss of transmit clock */
#define SR2_RCMF        0x02    /* receive CRC4 multiframe (every 2 ms) */
#define SR2_TSLIP       0x01    /* transmit elastic store slip event */

/*
 * Error count registers
 */
#define VCR(h,l)   (((short) (h) << 8) | (l))              /* 16-bit code violation */
#define CRCCR(h,l) (((short) (h) << 8 & 0x300) | (l))      /* 10-bit CRC4 error count */
#define EBCR(h,l)  (((short) (h) << 8 & 0x300) | (l))      /* 10-bit E-bit count */
#define FASCR(h,l) (((short) (h) << 4 & 0xfc0) | (l) >> 2) /* 12-bit FAS error count */

#define FASCRH(h) ((h) << 4)                    /* 12-bit FAS error count */
#define FASCRL(l) ((l) >> 2)                    /* 12-bit FAS error count */

/*
 * DS21x54 additional registers
 */
#define DS_IDR		0x0f	/* r  - device id */
#define DS_TSACR	0x1c	/* rw - transmit Sa bit control */
#define DS_CCR6		0x1d	/* rw - common control 6 */

#define DS_TSIAF	0x50	/* rw - transmit Si bits align frame */
#define DS_TSINAF	0x51	/* rw - transmit Si bits non-align frame */
#define DS_TRA		0x52	/* rw - transmit remote alarm bits */
#define DS_TSA4		0x53	/* rw - transmit Sa4 bits */
#define DS_TSA5		0x54	/* rw - transmit Sa5 bits */
#define DS_TSA6		0x55	/* rw - transmit Sa6 bits */
#define DS_TSA7		0x56	/* rw - transmit Sa7 bits */
#define DS_TSA8		0x57	/* rw - transmit Sa8 bits */
#define DS_RSIAF	0x58	/* r  - receive Si bits align frame */
#define DS_RSINAF	0x59	/* r  - receive Si bits non-align frame */
#define DS_RRA		0x5a	/* r  - receive remote alarm bits */
#define DS_RSA4		0x5b	/* r  - receive Sa4 bits */
#define DS_RSA5		0x5c	/* r  - receive Sa5 bits */
#define DS_RSA6		0x5d	/* r  - receive Sa6 bits */
#define DS_RSA7		0x5e	/* r  - receive Sa7 bits */
#define DS_RSA8		0x5f	/* r  - receive Sa8 bits */

#define DS_TCC1		0xa0	/* rw - transmit channel control 1 */
#define DS_TCC2		0xa1	/* rw - transmit channel control 2 */
#define DS_TCC3		0xa2	/* rw - transmit channel control 3 */
#define DS_TCC4		0xa3	/* rw - transmit channel control 4 */
#define DS_RCC1		0xa4	/* rw - receive channel control 1 */
#define DS_RCC2		0xa5	/* rw - receive channel control 2 */
#define DS_RCC3		0xa6	/* rw - receive channel control 3 */
#define DS_RCC4		0xa7	/* rw - receive channel control 4 */

#define DS_CCR4		0xa8	/* rw - common control 4 */
#define DS_TDS0M	0xa9	/* r  - transmit ds0 monitor */
#define DS_CCR5		0xaa	/* rw - common control 5 */
#define DS_RDS0M	0xab	/* r  - receive ds0 monitor */
#define DS_TEST3	0xac	/* rw - test 3, set to 00h */

#define DS_HCR		0xb0	/* rw - hdlc control */
#define DS_HSR		0xb1	/* rw - hdlc status */
#define DS_HIMR		0xb2	/* rw - hdlc interrupt mask */
#define DS_RHIR		0xb3	/* rw - receive hdlc information */
#define DS_RHFR		0xb4	/* rw - receive hdlc fifo */
#define DS_IBO		0xb5	/* rw - interleave bus operation */
#define DS_THIR		0xb6	/* rw - transmit hdlc information */
#define DS_THFR		0xb7	/* rw - transmit hdlc fifo */
#define DS_RDC1		0xb8	/* rw - receive hdlc ds0 control 1 */
#define DS_RDC2		0xb9	/* rw - receive hdlc ds0 control 2 */
#define DS_TDC1		0xba	/* rw - transmit hdlc ds0 control 1 */
#define DS_TDC2		0xbb	/* rw - transmit hdlc ds0 control 2 */

#define CCR4_RLB	0x80    /* enable remote loopback */
#define CCR4_LLB	0x40    /* enable local loopback */
#define CCR5_LIRST	0x80    /* line interface reset */
#define CCR6_RESR	0x02    /* receive elastic store reset */
#define CCR6_TESR	0x01    /* transmit elastic store reset */
