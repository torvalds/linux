/*
 * include/asm-arm/arch-ks8695/regs-lan.h
 *
 * Copyright (C) 2006 Andrew Victor
 *
 * KS8695 - LAN Registers and bit definitions.
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef KS8695_LAN_H
#define KS8695_LAN_H

#define KS8695_LAN_OFFSET	(0xF0000 + 0x8000)
#define KS8695_LAN_VA		(KS8695_IO_VA + KS8695_LAN_OFFSET)
#define KS8695_LAN_PA		(KS8695_IO_PA + KS8695_LAN_OFFSET)


/*
 * LAN registers
 */
#define KS8695_LMDTXC		(0x00)		/* DMA Transmit Control */
#define KS8695_LMDRXC		(0x04)		/* DMA Receive Control */
#define KS8695_LMDTSC		(0x08)		/* DMA Transmit Start Command */
#define KS8695_LMDRSC		(0x0c)		/* DMA Receive Start Command */
#define KS8695_LTDLB		(0x10)		/* Transmit Descriptor List Base Address */
#define KS8695_LRDLB		(0x14)		/* Receive Descriptor List Base Address */
#define KS8695_LMAL		(0x18)		/* MAC Station Address Low */
#define KS8695_LMAH		(0x1c)		/* MAC Station Address High */
#define KS8695_LMAAL_(n)	(0x80 + ((n)*8))	/* MAC Additional Station Address (0..15) Low */
#define KS8695_LMAAH_(n)	(0x84 + ((n)*8))	/* MAC Additional Station Address (0..15) High */


/* DMA Transmit Control Register */
#define LMDTXC_LMTRST		(1    << 31)	/* Soft Reset */
#define LMDTXC_LMTBS		(0x3f << 24)	/* Transmit Burst Size */
#define LMDTXC_LMTUCG		(1    << 18)	/* Transmit UDP Checksum Generate */
#define LMDTXC_LMTTCG		(1    << 17)	/* Transmit TCP Checksum Generate */
#define LMDTXC_LMTICG		(1    << 16)	/* Transmit IP Checksum Generate */
#define LMDTXC_LMTFCE		(1    <<  9)	/* Transmit Flow Control Enable */
#define LMDTXC_LMTLB		(1    <<  8)	/* Loopback mode */
#define LMDTXC_LMTEP		(1    <<  2)	/* Transmit Enable Padding */
#define LMDTXC_LMTAC		(1    <<  1)	/* Transmit Add CRC */
#define LMDTXC_LMTE		(1    <<  0)	/* TX Enable */

/* DMA Receive Control Register */
#define LMDRXC_LMRBS		(0x3f << 24)	/* Receive Burst Size */
#define LMDRXC_LMRUCC		(1    << 18)	/* Receive UDP Checksum check */
#define LMDRXC_LMRTCG		(1    << 17)	/* Receive TCP Checksum check */
#define LMDRXC_LMRICG		(1    << 16)	/* Receive IP Checksum check */
#define LMDRXC_LMRFCE		(1    <<  9)	/* Receive Flow Control Enable */
#define LMDRXC_LMRB		(1    <<  6)	/* Receive Broadcast */
#define LMDRXC_LMRM		(1    <<  5)	/* Receive Multicast */
#define LMDRXC_LMRU		(1    <<  4)	/* Receive Unicast */
#define LMDRXC_LMRERR		(1    <<  3)	/* Receive Error Frame */
#define LMDRXC_LMRA		(1    <<  2)	/* Receive All */
#define LMDRXC_LMRE		(1    <<  1)	/* RX Enable */

/* Additional Station Address High */
#define LMAAH_E			(1    << 31)	/* Address Enabled */


#endif
