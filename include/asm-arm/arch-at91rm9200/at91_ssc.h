/*
 * include/asm-arm/arch-at91rm9200/at91_ssc.h
 *
 * Copyright (C) SAN People
 *
 * Serial Synchronous Controller (SSC) registers.
 * Based on AT91RM9200 datasheet revision E.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91_SSC_H
#define AT91_SSC_H

#define AT91_SSC_CR		0x00	/* Control Register */
#define		AT91_SSC_RXEN		(1 <<  0)	/* Receive Enable */
#define		AT91_SSC_RXDIS		(1 <<  1)	/* Receive Disable */
#define		AT91_SSC_TXEN		(1 <<  8)	/* Transmit Enable */
#define		AT91_SSC_TXDIS		(1 <<  9)	/* Transmit Disable */
#define		AT91_SSC_SWRST		(1 << 15)	/* Software Reset */

#define AT91_SSC_CMR		0x04	/* Clock Mode Register */
#define		AT91_SSC_CMR_DIV	(0xfff << 0)	/* Clock Divider */

#define AT91_SSC_RCMR		0x10	/* Receive Clock Mode Register */
#define		AT91_SSC_CKS		(3    <<  0)	/* Clock Selection */
#define			AT91_SSC_CKS_DIV		(0 << 0)
#define			AT91_SSC_CKS_CLOCK		(1 << 0)
#define			AT91_SSC_CKS_PIN		(2 << 0)
#define		AT91_SSC_CKO		(7    <<  2)	/* Clock Output Mode Selection */
#define			AT91_SSC_CKO_NONE		(0 << 2)
#define			AT91_SSC_CKO_CONTINUOUS		(1 << 2)
#define		AT91_SSC_CKI		(1    <<  5)	/* Clock Inversion */
#define			AT91_SSC_CKI_FALLING		(0 << 5)
#define			AT91_SSC_CK_RISING		(1 << 5)
#define		AT91_SSC_CKG		(1    <<  6)	/* Receive Clock Gating Selection [AT91SAM9261 only] */
#define			AT91_SSC_CKG_NONE		(0 << 6)
#define			AT91_SSC_CKG_RFLOW		(1 << 6)
#define			AT91_SSC_CKG_RFHIGH		(2 << 6)
#define		AT91_SSC_START		(0xf  <<  8)	/* Start Selection */
#define			AT91_SSC_START_CONTINUOUS	(0 << 8)
#define			AT91_SSC_START_TX_RX		(1 << 8)
#define			AT91_SSC_START_LOW_RF		(2 << 8)
#define			AT91_SSC_START_HIGH_RF		(3 << 8)
#define			AT91_SSC_START_FALLING_RF	(4 << 8)
#define			AT91_SSC_START_RISING_RF	(5 << 8)
#define			AT91_SSC_START_LEVEL_RF		(6 << 8)
#define			AT91_SSC_START_EDGE_RF		(7 << 8)
#define		AT91_SSC_STOP		(1    << 12)	/* Receive Stop Selection [AT91SAM9261 only] */
#define		AT91_SSC_STTDLY		(0xff << 16)	/* Start Delay */
#define		AT91_SSC_PERIOD		(0xff << 24)	/* Period Divider Selection */

#define AT91_SSC_RFMR		0x14	/* Receive Frame Mode Register */
#define		AT91_SSC_DATALEN	(0x1f <<  0)	/* Data Length */
#define		AT91_SSC_LOOP		(1    <<  5)	/* Loop Mode */
#define		AT91_SSC_MSBF		(1    <<  7)	/* Most Significant Bit First */
#define		AT91_SSC_DATNB		(0xf  <<  8)	/* Data Number per Frame */
#define		AT91_SSC_FSLEN		(0xf  << 16)	/* Frame Sync Length */
#define		AT91_SSC_FSOS		(7    << 20)	/* Frame Sync Output Selection */
#define			AT91_SSC_FSOS_NONE		(0 << 20)
#define			AT91_SSC_FSOS_NEGATIVE		(1 << 20)
#define			AT91_SSC_FSOS_POSITIVE		(2 << 20)
#define			AT91_SSC_FSOS_LOW		(3 << 20)
#define			AT91_SSC_FSOS_HIGH		(4 << 20)
#define			AT91_SSC_FSOS_TOGGLE		(5 << 20)
#define		AT91_SSC_FSEDGE		(1    << 24)	/* Frame Sync Edge Detection */
#define			AT91_SSC_FSEDGE_POSITIVE	(0 << 24)
#define			AT91_SSC_FSEDGE_NEGATIVE	(1 << 24)

#define AT91_SSC_TCMR		0x18	/* Transmit Clock Mode Register */
#define AT91_SSC_TFMR		0x1c	/* Transmit Fram Mode Register */
#define		AT91_SSC_DATDEF		(1 <<  5)	/* Data Default Value */
#define		AT91_SSC_FSDEN		(1 << 23)	/* Frame Sync Data Enable */

#define AT91_SSC_RHR		0x20	/* Receive Holding Register */
#define AT91_SSC_THR		0x24	/* Transmit Holding Register */
#define AT91_SSC_RSHR		0x30	/* Receive Sync Holding Register */
#define AT91_SSC_TSHR		0x34	/* Transmit Sync Holding Register */

#define AT91_SSC_RC0R		0x38	/* Receive Compare 0 Register [AT91SAM9261 only] */
#define AT91_SSC_RC1R		0x3c	/* Receive Compare 1 Register [AT91SAM9261 only] */

#define AT91_SSC_SR		0x40	/* Status Register */
#define		AT91_SSC_TXRDY		(1 <<  0)	/* Transmit Ready */
#define		AT91_SSC_TXEMPTY	(1 <<  1)	/* Transmit Empty */
#define		AT91_SSC_ENDTX		(1 <<  2)	/* End of Transmission */
#define		AT91_SSC_TXBUFE		(1 <<  3)	/* Transmit Buffer Empty */
#define		AT91_SSC_RXRDY		(1 <<  4)	/* Receive Ready */
#define		AT91_SSC_OVRUN		(1 <<  5)	/* Receive Overrun */
#define		AT91_SSC_ENDRX		(1 <<  6)	/* End of Reception */
#define		AT91_SSC_RXBUFF		(1 <<  7)	/* Receive Buffer Full */
#define		AT91_SSC_CP0		(1 <<  8)	/* Compare 0 [AT91SAM9261 only] */
#define		AT91_SSC_CP1		(1 <<  9)	/* Compare 1 [AT91SAM9261 only] */
#define		AT91_SSC_TXSYN		(1 << 10)	/* Transmit Sync */
#define		AT91_SSC_RXSYN		(1 << 11)	/* Receive Sync */
#define		AT91_SSC_TXENA		(1 << 16)	/* Transmit Enable */
#define		AT91_SSC_RXENA		(1 << 17)	/* Receive Enable */

#define AT91_SSC_IER		0x44	/* Interrupt Enable Register */
#define AT91_SSC_IDR		0x48	/* Interrupt Disable Register */
#define AT91_SSC_IMR		0x4c	/* Interrupt Mask Register */

#endif
