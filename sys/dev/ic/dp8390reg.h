/*	$OpenBSD: dp8390reg.h,v 1.9 2003/10/21 18:58:49 jmc Exp $	*/
/*	$NetBSD: dp8390reg.h,v 1.3 1997/04/29 04:32:08 scottr Exp $	*/

/*
 * National Semiconductor DS8390 NIC register definitions.
 *
 * Copyright (C) 1993, David Greenman.  This software may be used, modified,
 * copied, distributed, and sold, in both source and binary form provided that
 * the above copyright and these terms are retained.  Under no circumstances is
 * the author responsible for the proper functioning of this software, nor does
 * the author assume any responsibility for damages incurred with its use.
 */

/*
 * Page 0 register offsets
 */
#define ED_P0_CR	0x00	/* Command Register */

#define ED_P0_CLDA0	0x01	/* Current Local DMA Addr low (read) */
#define ED_P0_PSTART	0x01	/* Page Start register (write) */

#define ED_P0_CLDA1	0x02	/* Current Local DMA Addr high (read) */
#define ED_P0_PSTOP	0x02	/* Page Stop register (write) */

#define ED_P0_BNRY	0x03	/* Boundary Pointer */

#define ED_P0_TSR	0x04	/* Transmit Status Register (read) */
#define ED_P0_TPSR	0x04	/* Transmit Page Start (write) */

#define ED_P0_NCR	0x05	/* Number of Collisions Reg (read) */
#define ED_P0_TBCR0	0x05	/* Transmit Byte count, low (write) */

#define ED_P0_FIFO	0x06	/* FIFO register (read) */
#define ED_P0_TBCR1	0x06	/* Transmit Byte count, high (write) */

#define ED_P0_ISR	0x07	/* Interrupt Status Register */

#define ED_P0_CRDA0	0x08	/* Current Remote DMA Addr low (read) */
#define ED_P0_RSAR0	0x08	/* Remote Start Address low (write) */

#define ED_P0_CRDA1	0x09	/* Current Remote DMA Addr high (read) */
#define ED_P0_RSAR1	0x09	/* Remote Start Address high (write) */

#define ED_P0_RBCR0	0x0a	/* Remote Byte Count low (write) */

#define ED_P0_RBCR1	0x0b	/* Remote Byte Count high (write) */

#define ED_P0_RSR	0x0c	/* Receive Status (read) */
#define ED_P0_RCR	0x0c	/* Receive Configuration Reg (write) */

#define ED_P0_CNTR0	0x0d	/* frame alignment error counter (read) */
#define ED_P0_TCR	0x0d	/* Transmit Configuration Reg (write) */

#define ED_P0_CNTR1	0x0e	/* CRC error counter (read) */
#define ED_P0_DCR	0x0e	/* Data Configuration Reg (write) */

#define ED_P0_CNTR2	0x0f	/* missed packet counter (read) */
#define ED_P0_IMR	0x0f	/* Interrupt Mask Register (write) */

/*
 * Page 1 register offsets
 */
#define ED_P1_CR	0x00	/* Command Register */
#define ED_P1_PAR0	0x01	/* Physical Address Register 0 */
#define ED_P1_PAR1	0x02	/* Physical Address Register 1 */
#define ED_P1_PAR2	0x03	/* Physical Address Register 2 */
#define ED_P1_PAR3	0x04	/* Physical Address Register 3 */
#define ED_P1_PAR4	0x05	/* Physical Address Register 4 */
#define ED_P1_PAR5	0x06	/* Physical Address Register 5 */
#define ED_P1_CURR	0x07	/* Current RX ring-buffer page */
#define ED_P1_MAR0	0x08	/* Multicast Address Register 0 */
#define ED_P1_MAR1	0x09	/* Multicast Address Register 1 */
#define ED_P1_MAR2	0x0a	/* Multicast Address Register 2 */
#define ED_P1_MAR3	0x0b	/* Multicast Address Register 3 */
#define ED_P1_MAR4	0x0c	/* Multicast Address Register 4 */
#define ED_P1_MAR5	0x0d	/* Multicast Address Register 5 */
#define ED_P1_MAR6	0x0e	/* Multicast Address Register 6 */
#define ED_P1_MAR7	0x0f	/* Multicast Address Register 7 */

/*
 * Page 2 register offsets
 */
#define ED_P2_CR	0x00	/* Command Register */
#define ED_P2_PSTART	0x01	/* Page Start (read) */
#define ED_P2_CLDA0	0x01	/* Current Local DMA Addr 0 (write) */
#define ED_P2_PSTOP	0x02	/* Page Stop (read) */
#define ED_P2_CLDA1	0x02	/* Current Local DMA Addr 1 (write) */
#define ED_P2_RNPP	0x03	/* Remote Next Packet Pointer */
#define ED_P2_TPSR	0x04	/* Transmit Page Start (read) */
#define ED_P2_LNPP	0x05	/* Local Next Packet Pointer */
#define ED_P2_ACU	0x06	/* Address Counter Upper */
#define ED_P2_ACL	0x07	/* Address Counter Lower */
#define ED_P2_RCR	0x0c	/* Receive Configuration Register (read) */
#define ED_P2_TCR	0x0d	/* Transmit Configuration Register (read) */
#define ED_P2_DCR	0x0e	/* Data Configuration Register (read) */
#define ED_P2_IMR	0x0f	/* Interrupt Mask Register (read) */

/*
 *		Command Register (CR) definitions
 */

/*
 * STP: SToP.  Software reset command.  Takes the controller offline.  No
 * packets will be received or transmitted.  Any reception or transmission in
 * progress will continue to completion before entering reset state.  To exit
 * this state, the STP bit must reset and the STA bit must be set.  The
 * software reset has executed only when indicated by the RST bit in the ISR
 * being set.
 */
#define ED_CR_STP	0x01

/*
 * STA: STArt.  This bit is used to activate the NIC after either power-up, or
 * when the NIC has been put in reset mode by software command or error.
 */
#define ED_CR_STA	0x02

/*
 * TXP: Transmit Packet.  This bit must be set to indicate transmission of a
 * packet.  TXP is internally reset either after the transmission is completed
 * or aborted.  This bit should be set only after the Transmit Byte Count and
 * Transmit Page Start register have been programmed.
 */
#define ED_CR_TXP	0x04

/*
 * RD0, RD1, RD2: Remote DMA Command.  These three bits control the operation
 * of the remote DMA channel.  RD2 can be set to abort any remote DMA command
 * in progress.  The Remote Byte Count registers should be cleared when a
 * remote DMA has been aborted.  The Remote Start Addresses are not restored
 * to the starting address if the remote DMA is aborted.
 *
 * RD2 RD1 RD0	function
 *  0   0   0	not allowed
 *  0   0   1	remote read
 *  0   1   0	remote write
 *  0   1   1	send packet
 *  1   X   X	abort
 */
#define ED_CR_RD0	0x08
#define ED_CR_RD1	0x10
#define ED_CR_RD2	0x20

/*
 * PS0, PS1: Page Select.  The two bits select which register set or 'page' to
 * access.
 *
 * PS1 PS0  page
 *  0   0   0
 *  0   1   1
 *  1   0   2
 *  1   1   3 (only on chips which have extensions to the dp8390)
 */
#define ED_CR_PS0	0x40
#define ED_CR_PS1	0x80
/* bit encoded aliases */
#define ED_CR_PAGE_0	0x00 /* (for consistency) */
#define ED_CR_PAGE_1	(ED_CR_PS0)
#define ED_CR_PAGE_2	(ED_CR_PS1)
#define	ED_CR_PAGE_3	(ED_CR_PS1|ED_CR_PS0)

/*
 *		Interrupt Status Register (ISR) definitions
 */

/*
 * PRX: Packet Received.  Indicates packet received with no errors.
 */
#define ED_ISR_PRX	0x01

/*
 * PTX: Packet Transmitted.  Indicates packet transmitted with no errors.
 */
#define ED_ISR_PTX	0x02

/*
 * RXE: Receive Error.  Indicates that a packet was received with one or more
 * the following errors: CRC error, frame alignment error, FIFO overrun,
 * missed packet.
 */
#define ED_ISR_RXE	0x04

/*
 * TXE: Transmission Error.  Indicates that an attempt to transmit a packet
 * resulted in one or more of the following errors: excessive collisions, FIFO
 * underrun.
 */
#define ED_ISR_TXE	0x08

/*
 * OVW: OverWrite.  Indicates a receive ring-buffer overrun.  Incoming network
 * would exceed (has exceeded?) the boundary pointer, resulting in data that
 * was previously received and not yet read from the buffer to be overwritten.
 */
#define ED_ISR_OVW	0x10

/*
 * CNT: Counter Overflow.  Set when the MSB of one or more of the Network Tally
 * Counters has been set.
 */
#define ED_ISR_CNT	0x20

/*
 * RDC: Remote Data Complete.  Indicates that a Remote DMA operation has
 * completed.
 */
#define ED_ISR_RDC	0x40

/*
 * RST: Reset status.  Set when the NIC enters the reset state and cleared when
 * a Start Command is issued to the CR.  This bit is also set when a receive
 * ring-buffer overrun (OverWrite) occurs and is cleared when one or more
 * packets have been removed from the ring.  This is a read-only bit.
 */
#define ED_ISR_RST	0x80

/*
 *		Interrupt Mask Register (IMR) definitions
 */

/*
 * PRXE: Packet Received interrupt Enable.  If set, a received packet will
 * cause an interrupt.
 */
#define ED_IMR_PRXE	0x01

/*
 * PTXE: Packet Transmit interrupt Enable.  If set, an interrupt is generated
 * when a packet transmission completes.
 */
#define ED_IMR_PTXE	0x02

/*
 * RXEE: Receive Error interrupt Enable.  If set, an interrupt will occur
 * whenever a packet is received with an error.
 */
#define ED_IMR_RXEE 	0x04

/*
 * TXEE: Transmit Error interrupt Enable.  If set, an interrupt will occur
 * whenever a transmission results in an error.
 */
#define ED_IMR_TXEE	0x08

/*
 * OVWE: OverWrite error interrupt Enable.  If set, an interrupt is generated
 * whenever the receive ring-buffer is overrun.  i.e. when the boundary pointer
 * is exceeded.
 */
#define ED_IMR_OVWE	0x10

/*
 * CNTE: Counter overflow interrupt Enable.  If set, an interrupt is generated
 * whenever the MSB of one or more of the Network Statistics counters has been
 * set.
 */
#define ED_IMR_CNTE	0x20

/*
 * RDCE: Remote DMA Complete interrupt Enable.  If set, an interrupt is
 * generated when a remote DMA transfer has completed.
 */
#define ED_IMR_RDCE	0x40

/*
 * Bit 7 is unused/reserved.
 */

/*
 *		Data Configuration Register (DCR) definitions
 */

/*
 * WTS: Word Transfer Select.  WTS establishes byte or word transfers for both
 * remote and local DMA transfers
 */
#define ED_DCR_WTS	0x01

/*
 * BOS: Byte Order Select.  BOS sets the byte order for the host.  Should be 0
 * for 80x86, and 1 for 68000 series processors
 */
#define ED_DCR_BOS	0x02

/*
 * LAS: Long Address Select.  When LAS is 1, the contents of the remote DMA
 * registers RSAR0 and RSAR1 are used to provide A16-A31.
 */
#define ED_DCR_LAS	0x04

/*
 * LS: Loopback Select.  When 0, loopback mode is selected.  Bits D1 and D2 of
 * the TCR must also be programmed for loopback operation.  When 1, normal
 * operation is selected.
 */
#define ED_DCR_LS	0x08

/*
 * AR: Auto-initialize Remote.  When 0, data must be removed from ring-buffer
 * under program control.  When 1, remote DMA is automatically initiated and
 * the boundary pointer is automatically updated.
 */
#define ED_DCR_AR	0x10

/*
 * FT0, FT1: Fifo Threshold select.
 *
 * FT1 FT0  Word-width  Byte-width
 *  0   0   1 word      2 bytes
 *  0   1   2 words     4 bytes
 *  1   0   4 words     8 bytes
 *  1   1   8 words     12 bytes
 *
 * During transmission, the FIFO threshold indicates the number of bytes or
 * words that the FIFO has filled from the local DMA before BREQ is asserted.
 * The transmission threshold is 16 bytes minus the receiver threshold.
 */
#define ED_DCR_FT0	0x20
#define ED_DCR_FT1	0x40

/*
 * bit 7 (0x80) is unused/reserved
 */

/*
 *		Transmit Configuration Register (TCR) definitions
 */

/*
 * CRC: Inhibit CRC.  If 0, CRC will be appended by the transmitter, if 0, CRC
 * is not appended by the transmitter.
 */
#define ED_TCR_CRC	0x01

/*
 * LB0, LB1: Loopback control.  These two bits set the type of loopback that is
 * to be performed.
 *
 * LB1 LB0		mode
 *  0   0		0 - normal operation (DCR_LS = 0)
 *  0   1		1 - internal loopback (DCR_LS = 0)
 *  1   0		2 - external loopback (DCR_LS = 1)
 *  1   1		3 - external loopback (DCR_LS = 0)
 */
#define ED_TCR_LB0	0x02
#define ED_TCR_LB1	0x04

/*
 * ATD: Auto Transmit Disable.  Clear for normal operation.  When set, allows
 * another station to disable the NIC's transmitter by transmitting to a
 * multicast address hashing to bit 62.  Reception of a multicast address
 * hashing to bit 63 enables the transmitter.
 */
#define ED_TCR_ATD	0x08

/*
 * OFST: Collision Offset enable.  This bit when set modifies the backoff
 * algorithm to allow prioritization of nodes.
 */
#define ED_TCR_OFST	0x10
 
/*
 * bits 5, 6, and 7 are unused/reserved
 */

/*
 *		Transmit Status Register (TSR) definitions
 */

/*
 * PTX: Packet Transmitted.  Indicates successful transmission of packet.
 */
#define ED_TSR_PTX	0x01

/*
 * bit 1 (0x02) is unused/reserved
 */

/*
 * COL: Transmit Collided.  Indicates that the transmission collided at least
 * once with another station on the network.
 */
#define ED_TSR_COL	0x04

/*
 * ABT: Transmit aborted.  Indicates that the transmission was aborted due to
 * excessive collisions.
 */
#define ED_TSR_ABT	0x08

/*
 * CRS: Carrier Sense Lost.  Indicates that carrier was lost during the
 * transmission of the packet.  (Transmission is not aborted because of a loss
 * of carrier).
 */
#define ED_TSR_CRS	0x10

/*
 * FU: FIFO Underrun.  Indicates that the NIC wasn't able to access bus/
 * transmission memory before the FIFO emptied.  Transmission of the packet was
 * aborted.
 */
#define ED_TSR_FU	0x20

/*
 * CDH: CD Heartbeat.  Indicates that the collision detection circuitry isn't
 * working correctly during a collision heartbeat test.
 */
#define ED_TSR_CDH	0x40

/*
 * OWC: Out of Window Collision: Indicates that a collision occurred after a
 * slot time (51.2us).  The transmission is rescheduled just as in normal
 * collisions.
 */
#define ED_TSR_OWC	0x80

/*
 *		Receiver Configuration Register (RCR) definitions
 */

/*
 * SEP: Save Errored Packets.  If 0, error packets are discarded.  If set to 1,
 * packets with CRC and frame errors are not discarded.
 */
#define ED_RCR_SEP	0x01

/*
 * AR: Accept Runt packet.  If 0, packet with less than 64 byte are discarded.
 * If set to 1, packets with less than 64 byte are not discarded.
 */
#define ED_RCR_AR	0x02

/*
 * AB: Accept Broadcast.  If set, packets sent to the broadcast address will be
 * accepted.
 */
#define ED_RCR_AB	0x04

/*
 * AM: Accept Multicast.  If set, packets sent to a multicast address are
 * checked for a match in the hashing array.  If clear, multicast packets are
 * ignored.
 */
#define ED_RCR_AM	0x08

/*
 * PRO: Promiscuous Physical.  If set, all packets with a physical addresses
 * are accepted.  If clear, a physical destination address must match this
 * station's address.  Note: for full promiscuous mode, RCR_AB and RCR_AM must
 * also be set.  In addition, the multicast hashing array must be set to all
 * 1's so that all multicast addresses are accepted.
 */
#define ED_RCR_PRO	0x10

/*
 * MON: Monitor Mode.  If set, packets will be checked for good CRC and
 * framing, but are not stored in the ring-buffer.  If clear, packets are
 * stored (normal operation).
 */
#define ED_RCR_MON	0x20

/*
 * INTT: Interrupt Trigger Mode.  Must be set if AX88190.
 */
#define ED_RCR_INTT	0x40

/*
 * Bit 7 is unused/reserved.
 */

/*
 *		Receiver Status Register (RSR) definitions
 */

/*
 * PRX: Packet Received without error.
 */
#define ED_RSR_PRX	0x01

/*
 * CRC: CRC error.  Indicates that a packet has a CRC error.  Also set for
 * frame alignment errors.
 */
#define ED_RSR_CRC	0x02

/*
 * FAE: Frame Alignment Error.  Indicates that the incoming packet did not end
 * on a byte boundary and the CRC did not match at the last byte boundary.
 */
#define ED_RSR_FAE	0x04

/*
 * FO: FIFO Overrun.  Indicates that the FIFO was not serviced (during local
 * DMA) causing it to overrun.  Reception of the packet is aborted.
 */
#define ED_RSR_FO	0x08

/*
 * MPA: Missed Packet.  Indicates that the received packet couldn't be stored
 * in the ring-buffer because of insufficient buffer space (exceeding the
 * boundary pointer), or because the transfer to the ring-buffer was inhibited
 * by RCR_MON - monitor mode.
 */
#define ED_RSR_MPA	0x10

/*
 * PHY: Physical address.  If 0, the packet received was sent to a physical
 * address.  If 1, the packet was accepted because of a multicast/broadcast
 * address match.
 */
#define ED_RSR_PHY	0x20

/*
 * DIS: Receiver Disabled.  Set to indicate that the receiver has entered
 * monitor mode.  Cleared when the receiver exits monitor mode.
 */
#define ED_RSR_DIS	0x40

/*
 * DFR: Deferring.  Set to indicate a 'jabber' condition.  The CRS and COL
 * inputs are active, and the transceiver has set the CD line as a result of
 * the jabber.
 */
#define ED_RSR_DFR	0x80

/*
 * receive ring descriptor
 *
 * The National Semiconductor DS8390 Network interface controller uses the
 * following receive ring headers.  The way this works is that the memory on
 * the interface card is chopped up into 256 bytes blocks.  A contiguous
 * portion of those blocks are marked for receive packets by setting start and
 * end block #'s in the NIC.  For each packet that is put into the receive
 * ring, one of these headers (4 bytes each) is tacked onto the front.   The
 * first byte is a copy of the receiver status register at the time the packet
 * was received.
 */
struct dp8390_ring	{
	u_int8_t	rsr;		/* receiver status */
	u_int8_t	next_packet;	/* pointer to next packet */
	u_int16_t	count;		/* bytes in packet (length + 4) */
};

/* Some drivers prefer to use byte-constants to get at this structure.  */
#define ED_RING_RSR		0	/* receiver status */
#define ED_RING_NEXT_PACKET	1	/* pointer to next packet */
#define ED_RING_COUNT		2	/* bytes in packet (length + 4) */
#define ED_RING_HDRSZ		4	/* Header size */

/*
 * Common constants
 */
#define ED_PAGE_SIZE		256	/* Size of RAM pages in bytes */
#define	ED_PAGE_MASK		255
#define	ED_PAGE_SHIFT		8

#define ED_TXBUF_SIZE		6	/* Size of TX buffer in pages */
