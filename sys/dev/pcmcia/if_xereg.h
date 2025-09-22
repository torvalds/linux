/*	$OpenBSD: if_xereg.h,v 1.4 2003/10/22 09:58:46 jmc Exp $	*/

/*
 * Copyright (c) 1999 Niklas Hallqvist, C Stone, Job de Haas
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Niklas Hallqvist,
 *	C Stone and Job de Haas.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Additional Card Configuration Registers on Dingo */

#define PCMCIA_CCR_DCOR0		0x20
#define PCMCIA_CCR_DCOR0_MRST_SFRST		0x80
#define PCMCIA_CCR_DCOR0_MRST_SFPWDN		0x40
#define PCMCIA_CCR_DCOR0_LED3_SFRST		0x20
#define PCMCIA_CCR_DCOR0_LED3_SFPWDN		0x10
#define PCMCIA_CCR_DCOR0_BUS			0x08
#define PCMCIA_CCR_DCOR0_DECODE			0x04
#define PCMCIA_CCR_DCOR0_SFINT			0x01
#define PCMCIA_CCR_DCOR1		0x22
#define PCMCIA_CCR_DCOR1_SFCSR_WAIT		0xC0
#define PCMCIA_CCR_DCOR1_SHADOW_SFIOB		0x20
#define PCMCIA_CCR_DCOR1_SHADOW_SFCSR		0x10
#define PCMCIA_CCR_DCOR1_FORCE_LEVIREQ		0x08
#define PCMCIA_CCR_DCOR1_D6			0x04
#define PCMCIA_CCR_DCOR1_SF_STSCHG		0x02
#define PCMCIA_CCR_DCOR1_SF_IREQ		0x01
#define PCMCIA_CCR_DCOR2		0x24
#define PCMCIA_CCR_DCOR2_SHADOW_SFCOR		0x10
#define PCMCIA_CCR_DCOR2_SMEM_BASE		0x0F
#define PCMCIA_CCR_DCOR3		0x26
#define PCMCIA_CCR_DCOR4		0x28
#define PCMCIA_CCR_SFCOR		0x40
#define PCMCIA_CCR_SFCOR_SRESET			0x80
#define PCMCIA_CCR_SFCOR_LEVIREQ		0x40
#define PCMCIA_CCR_SFCOR_IRQ_STSCHG		0x20
#define PCMCIA_CCR_SFCOR_CFINDEX		0x18
#define PCMCIA_CCR_SFCOR_IREQ_ENABLE		0x04
#define PCMCIA_CCR_SFCOR_ADDR_DECODE		0x02
#define PCMCIA_CCR_SFCOR_FUNC_ENABLE		0x01
#define PCMCIA_CCR_SFCSR		0x42
#define PCMCIA_CCR_SFCSR_IOIS8			0x20
#define PCMCIA_CCR_SFCSR_AUDIO			0x08
#define PCMCIA_CCR_SFCSR_PWRDWN			0x04
#define PCMCIA_CCR_SFCSR_INTR			0x02
#define PCMCIA_CCR_SFCSR_INTRACK		0x01
#define PCMCIA_CCR_SFIOBASE0		0x4A
#define PCMCIA_CCR_SFIOBASE1		0x4C
#define PCMCIA_CCR_SFILR		0x52

#define PCMCIA_CCR_SIZE_DINGO		0x54

/* All pages */
#define CR	0x0	/* W  - Command register */
#define ESR	0x0	/* R  - Ethernet status register */
#define PR	0x1	/* RW - Page register select */
#define EDP	0x4	/* RW - Ethernet data port, 4 registers */
#define ISR0	0x6	/* R  - Ethernet interrupt status register */
#define GIR	0x7	/* RW - Global interrupt register */
#define PTR	0xd	/* R  - Packets Transmitted register */

/* Page 0 */
#define TSO0	0x8	/* R  - Transmit space open, 3 registers */
#define TSO1	0x9
#define TSO2	0xa
#define DO0	0xc	/* W  - Data offset, 2 registers */
#define DO1	0xd
#define RSR	0xc	/* R  - Rx status register */
#define TPR	0xd	/* R  - Tx packets register */
#define RBC0	0xe	/* R  - Rx byte count, 2 registers */
#define RBC1	0xf

/* Page 1 */
#define IMR0	0xc	/* RW - Interrupt mask, 2 registers */
#define IMR1	0xd
#define ECR	0xe	/* RW - Ethernet config register */

/* Page 2 */
#define RBS0	0x8	/* RW - Receive buffer start, 2 registers */
#define RBS1	0x9
#define LED	0xa	/* RW - LED control register */
#define LED3	0xb	/* RW - LED3 control register */
#define MSR	0xc	/* RW - Misc. setup register */
#define GP2	0xd	/* RW - General purpose register 2 */

/* Page 3 */
#define TPT0	0xa	/* RW - Tx packet threshold, 2 registers */
#define TPT1	0xb

/* Page 4 */
#define GP0	0x8	/* RW - General purpose register 0 */
#define GP1	0x9	/* RW - General purpose register 1 */
#define BV	0xa	/* R  - Bonding version register */
#define EES	0xb	/* RW - EEPROM control register */

/* Page 5 */
#define RHSA0	0xa	/* RX host start address */

/* Page 6 */

/* Page 7 */

/* Page 8 */

/* Page 16 */

/* Page 0x40 */
#define CMD0	0x8	/* W  - Receive status register */
#define RXST0	0x9	/* RW - Receive status register */
#define TXST0	0xb	/* RW - Transmit status, 2 registers */
#define TXST1	0xc
#define RX0MSK	0xd	/* RW - Receive status mask register */
#define TX0MSK	0xe	/* RW - Transmit status mask, 2 registers */
#define TX1MSK	0xf	/* RW - Dingo does not define this register */

/* Page 0x42 */
#define SWC0	0x8	/* RW - Software configuration, 2 registers */
#define SWC1	0x9

/* Page 0x50-0x57 */
#define	IA	0x8	/* RW - Individual address */

/* CR register bits */
#define TX_PKT		0x01	/* Transmit packet. */
#define SOFT_RESET	0x02	/* Software reset. */
#define ENABLE_INT	0x04	/* Enable interrupt. */
#define FORCE_INT	0x08	/* Force interrupt. */
#define CLR_TX_FIFO	0x10	/* Clear transmit FIFO. */
#define CLR_RX_OVERRUN	0x20	/* Clear receive overrun. */
#define RESTART_TX	0x40	/* Restart transmit process. */

/* ESR register bits */
#define FULL_PKT_RCV	0x01	/* Full packet received. */
#define PKT_REJECTED	0x04	/* A packet was rejected. */
#define TX_PKT_PEND	0x08	/* TX Packet Pending. */
#define INCOR_POLARITY	0x10	/* XXX from linux driver, but not used there */
#define MEDIA_SELECT	0x20	/* set if TP, clear if AUI */

/* DO register bits */
#define DO_OFF_MASK	0x1fff	/* Mask for offset value. */
#define DO_CHG_OFFSET	0x2000	/* Change offset command. */
#define DO_SHM_MODE	0x4000	/* Shared memory mode. */
#define DO_SKIP_RX_PKT	0x8000	/* Skip Rx packet. */

/* RBC register bits */
#define RBC_COUNT_MASK	0x1fff	/* Mask for byte count. */
#define RBC_RX_FULL	0x2000	/* Receive full packet. */
#define RBC_RX_PARTIAL	0x4000	/* Receive partial packet. */
#define RBC_RX_PKT_REJ	0x8000	/* Receive packet rejected. */

/* ISR0(/IMR0) register bits */
#define ISR_TX_OFLOW	0x01	/* Transmit buffer overflow. */
#define ISR_PKT_TX	0x02	/* Packet transmitted. */
#define ISR_MAC_INT	0x04	/* MAC interrupt. */
#define ISR_RX_EARLY	0x10	/* Receive early packet. */
#define ISR_RX_FULL	0x20	/* Receive full packet. */
#define ISR_RX_PKT_REJ	0x40	/* Receive packet rejected. */
#define ISR_FORCED_INT	0x80	/* Forced interrupt. */

/* ECR register bits */
#define ECR_EARLY_TX	0x01	/* Early transmit mode. */
#define ECR_EARLY_RX	0x02	/* Early receive mode. */
#define ECR_FULL_DUPLEX	0x04	/* Full duplex select. */
#define ECR_LNK_PLS_DIS	0x20	/* Link pulse disable. */
#define ECR_SW_COMPAT	0x80	/* Software compatibility switch. */

/* GP0 register bits */
#define GP1_WR		0x01	/* GP1 pin output value. */
#define GP2_WR		0x02	/* GP2 pin output value. */
#define GP1_OUT		0x04	/* GP1 pin output select. */
#define GP2_OUT		0x08	/* GP2 pin output select. */
#define GP1_RD		0x10	/* GP1 pin input value. */
#define GP2_RD		0x20	/* GP2 pin input value. */

/* GP1 register bits */
#define POWER_UP	0x01	/* When 0, power down analogue part of chip. */

/* LED register bits */
#define LED0_SHIFT	0	/* LED0 Output shift & mask */
#define LED0_MASK	0x7
#define LED1_SHIFT	3	/* LED1 Output shift & mask */
#define LED1_MASK	0x38
#define LED0_RX_ENA	0x40	/* LED0 - receive enable */
#define LED1_RX_ENA	0x80	/* LED1 - receive enable */

/* LED3 register bits */
#define LED3_SHIFT	0	/* LED0 output shift & mask */
#define LED3_MASK	0x7
#define LED3_RX_ENA	0x40	/* LED0 - receive enable */

/* LED output values */
#define LED_DISABLE	0	/* LED disabled */
#define LED_COLL_ACT	1	/* Collision activity */
#define LED_COLL_INACT	2	/* (NOT) Collision activity */
#define LED_10MB_LINK	3	/* 10 Mb link detected */
#define LED_100MB_LINK	4	/* 100 Mb link detected */
#define LED_LINK	5	/* 10 Mb or 100 Mb link detected */
#define LED_AUTO	6	/* Automatic assertion */
#define LED_TX_ACT	7	/* Transmit activity */

/* MSR register bits */
#define SRAM_128K_EXT	0x01	/* 128K SRAM extension */
#define RBS_BIT16	0x02	/* RBS bit 16 */
#define SELECT_MII	0x08	/* Select MII */
#define HASH_TBL_ENA	0x20	/* Hash table enable */

/* GP2 register bits */
#define GP3_WR		0x01	/* GP3 pin output value. */
#define GP4_WR		0x02	/* GP4 pin output value. */
#define GP3_OUT		0x04	/* GP3 pin output select. */
#define GP4_OUT		0x08	/* GP4 pin output select. */
#define GP3_RD		0x10	/* GP3 pin input value. */
#define GP4_RD		0x20	/* GP4 pin input value. */

/* RSR register bits */
#define RSR_NOTMCAST	0x01	/* clear when multicast packet */
#define RSR_BCAST	0x02	/* set when broadcast packet */
#define RSR_TOO_LONG	0x04	/* set if packet is longer than 1518 octets */
#define RSR_ALIGNERR	0x10	/* incorrect CRC and last octet not complete */
#define RSR_CRCERR	0x20	/* incorrect CRC and last octet complete */
#define RSR_RX_OK	0x80	/* packet received okay */

/* CMD0 register bits */
#define ONLINE		0x04	/* Online */
#define OFFLINE		0x08	/* Online */
#define ENABLE_RX	0x20	/* Enable receiver */
#define DISABLE_RX	0x80	/* Disable receiver */

/* RX0Msk register bits */
#define PKT_TOO_LONG	0x02	/* Packet too long mask. */
#define CRC_ERR		0x08	/* CRC error mask. */
#define RX_OVERRUN	0x10	/* Receive overrun mask. */
#define RX_ABORT	0x40	/* Receive abort mask. */
#define RX_OK		0x80	/* Receive OK mask. */

/* TX0Msk register bits */
#define CARRIER_LOST	0x01	/* Carrier sense lost. */
#define EXCESSIVE_COLL	0x02	/* Excessive collisions mask. */
#define TX_UNDERRUN	0x08	/* Transmit underrun mask. */
#define LATE_COLLISION	0x10	/* Late collision mask. */
#define SQE		0x20	/* Signal quality error mask.. */
#define TX_ABORT	0x40	/* Transmit abort mask. */
#define TX_OK		0x80	/* Transmit OK mask. */

/* SWC1 register bits */
#define SWC1_IND_ADDR	0x01	/* Individual address enable. */
#define SWC1_MCAST_PROM	0x02	/* Multicast promiscuous enable. */
#define SWC1_PROMISC	0x04	/* Promiscuous mode enable. */
#define SWC1_BCAST_DIS	0x08	/* Broadcast disable. */
#define SWC1_MEDIA_SEL	0x40	/* Media select (Mohawk). */
#define SWC1_AUTO_MEDIA	0x80	/* Automatic media select (Mohawk). */

/* Misc. defines. */

#define PAGE(sc, page)	\
    bus_space_write_1((sc->sc_bst), (sc->sc_bsh), (sc->sc_offset) + PR, (page))

/*
 * GP3 is connected to the MDC pin of the NS DP83840A PHY, GP4 is
 * connected to the MDIO pin.  These are utility macros to enhance
 * readability of the code.
 */
#define MDC_LOW		GP3_OUT
#define MDC_HIGH	(GP3_OUT | GP3_WR)
#define MDIO_LOW	GP4_OUT
#define MDIO_HIGH	(GP4_OUT | GP4_WR)
#define MDIO		GP4_RD

/* Values found in MANFID. */
#define XEMEDIA_ETHER		0x01
#define XEMEDIA_TOKEN		0x02
#define XEMEDIA_ARC		0x04
#define XEMEDIA_WIRELESS	0x08
#define XEMEDIA_MODEM		0x10
#define XEMEDIA_GSM		0x20

#define XEPROD_IDMASK		0x0f
#define XEPROD_POCKET		0x10
#define XEPROD_EXTERNAL		0x20
#define XEPROD_CREDITCARD	0x40
#define XEPROD_CARDBUS		0x80
