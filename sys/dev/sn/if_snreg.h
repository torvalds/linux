/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1996 Gardner Buchanan <gbuchanan@shl.com>
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
 *      This product includes software developed by Gardner Buchanan.
 * 4. The name of Gardner Buchanan may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 *
 *   $FreeBSD$
 */

/*
 * This file contains register information and access macros for
 * the SMC91xxx chipset.
 *
 * Information contained in this file was obtained from the SMC91C92
 * and SMC91C94 manuals from SMC.  You will need one of these in order
 * to make any meaningful changes to this driver.  Information about
 * obtaining one can be found at http://www.smc.com in the components
 * division.
 *
 * This FreeBSD driver is derived in part from the smc9194 Linux driver
 * by Erik Stahlman and is Copyright (C) 1996 by Erik Stahlman.
 * It is also derived in part from the FreeBSD ep (3C509) driver which
 * is Copyright (c) 1993 Herb Peyerl (hpeyerl@novatel.ca) All rights
 * reserved.
 *
 */
#ifndef _IF_SNREG_H_
#define _IF_SNREG_H_

/*
 * Wait time for memory to be free.  This probably shouldn't be
 * tuned that much, as waiting for this means nothing else happens
 * in the system
 */
#define	MEMORY_WAIT_TIME	1000


/* The SMC91xxx uses 16 I/O ports
 */
#define SMC_IO_EXTENT   16


/*
 * A description of the SMC registers is probably in order here,
 * although for details, the SMC datasheet is invaluable.
 * The data sheet I (GB) am using is "SMC91C92 Single Chip Ethernet
 * Controller With RAM", Rev. 12/0/94.  Constant definitions I give
 * here are loosely based on the mnemonic names given to them in the
 * data sheet, but there are many exceptions.
 *
 * Basically, the chip has 4 banks of registers (0 to 3), which
 * are accessed by writing a number into the BANK_SELECT register
 * (I also use a SMC_SELECT_BANK macro for this).  Registers are
 * either Byte or Word sized.  My constant definitions end in _B
 * or _W as appropriate.
 *
 * The banks are arranged so that for most purposes, bank 2 is all
 * that is needed for normal run time tasks.
 */

/*
 * Bank Select Register.  This also doubles as
 * a chip identification register.  This register
 * is mapped at the same position in all banks.
 */
#define BANK_SELECT_REG_W       0x0e
#define BSR_DETECT_MASK         0xff00
#define BSR_DETECT_VALUE        0x3300


/* BANK 0
 */

/* Transmit Control Register controls some aspects of the transmit
 * behavior of the Ethernet Protocol Handler.
 */
#define TXMIT_CONTROL_REG_W  0x00

#define TCR_ENABLE      0x0001	/* if this is 1, we can transmit */
#define TCR_LOOP        0x0002	/* Enable internal analogue loopback */
#define TCR_FORCOL      0x0004	/* Force Collision on next TX */
#define TCR_PAD_ENABLE  0x0080	/* Pad short packets to 64 bytes */
#define TCR_NOCRC       0x0100	/* Do not append CRC */
#define TCR_MON_CSN     0x0400	/* monitors the carrier status */
#define TCR_FDUPLX      0x0800	/* receive packets sent out */
#define TCR_STP_SQET    0x1000	/* stop transmitting if Signal quality error */
#define TCR_EPH_LOOP    0x2000	/* Enable internal digital loopback */


/* Status of the last transmitted frame and instantaneous status of
 * the Ethernet Protocol Handler jumbled together.  In auto-release
 * mode this information is simply discarded after each TX.  This info
 * is copied to the status word of in-memory packets after transmit
 * where relevant statuses can be checked.
 */
#define EPH_STATUS_REG_W 0x02

#define EPHSR_TX_SUC    0x0001	/* Transmit was successful */
#define EPHSR_SNGLCOL   0x0002	/* Single collision occurred */
#define EPHSR_MULCOL    0x0004	/* Multiple Collisions occurred */
#define EPHSR_LTX_MULT  0x0008	/* Transmit was a multicast */
#define EPHSR_16COL     0x0010	/* 16 Collisions occurred, TX disabled */
#define EPHSR_SQET      0x0020	/* SQE Test failed, TX disabled */
#define EPHSR_LTX_BRD   0x0040	/* Transmit was a broadcast */
#define EPHSR_DEFR      0x0080	/* TX deferred due to carrier det. */
#define EPHSR_LATCOL    0x0200	/* Late collision detected, TX disabled */
#define EPHSR_LOST_CAR  0x0400	/* Lost carrier sense, TX disabled */
#define EPHSR_EXC_DEF   0x0800	/* Excessive deferrals in TX >2 MAXETHER
				 * times */
#define EPHSR_CTR_ROL   0x1000	/* Some ECR Counter(s) rolled over */
#define EPHSR_RX_OVRN   0x2000	/* Receiver overrun, packets dropped */
#define EPHSR_LINK_OK   0x4000	/* Link integrity is OK */
#define EPHSR_TXUNRN    0x8000	/* Transmit underrun */


/* Receiver Control Register controls some aspects of the receive
 * behavior of the Ethernet Protocol Handler.
 */
#define RECV_CONTROL_REG_W 0x04

#define RCR_RX_ABORT    0x0001	/* Received huge packet */
#define RCR_PROMISC     0x0002	/* enable promiscuous mode */
#define RCR_ALMUL       0x0004	/* receive all multicast packets */
#define	RCR_ENABLE      0x0100	/* IFF this is set, we can receive packets */
#define RCR_STRIP_CRC   0x0200	/* strips CRC */
#define RCR_GAIN_BITS   0x0c00	/* PLL Gain control (for testing) */
#define RCR_FILT_CAR    0x4000	/* Enable 12 bit carrier filter */
#define RCR_SOFTRESET   0x8000	/* Resets the EPH logic */


/* TX Statistics counters
 */
#define COUNTER_REG_W   0x06

#define ECR_COLN_MASK   0x000f	/* Vanilla collisions */
#define ECR_MCOLN_MASK  0x00f0	/* Multiple collisions */
#define ECR_DTX_MASK    0x0f00	/* Deferred transmits */
#define ECR_EXDTX_MASK  0xf000	/* Excessively deferred transmits */

/* Memory Information
 */
#define MEM_INFO_REG_W  0x08

#define MIR_FREE_MASK   0xff00	/* Free memory pages available */
#define MIR_TOTAL_MASK  0x00ff	/* Total memory pages available */

/* Memory Configuration
 */
#define MEM_CFG_REG_W   0x0a

#define MCR_TXRSV_MASK  0x001f	/* Count of pages reserved for transmit */


/* Bank 0, Register 0x0c is unised in the SMC91C92
 */


/* BANK 1
 */

/* Adapter configuration
 */
#define CONFIG_REG_W    0x00

#define CR_INT_SEL0     0x0002	/* Interrupt selector */
#define CR_INT_SEL1     0x0004	/* Interrupt selector */
#define CR_DIS_LINK     0x0040	/* Disable 10BaseT Link Test */
#define CR_16BIT        0x0080	/* Bus width */
#define CR_AUI_SELECT   0x0100	/* Use external (AUI) Transceiver */
#define CR_SET_SQLCH    0x0200	/* Squelch level */
#define CR_FULL_STEP    0x0400	/* AUI signalling mode */
#define CR_NOW_WAIT_ST  0x1000	/* Disable bus wait states */

/* The contents of this port are used by the adapter
 * to decode its I/O address.  We use it as a varification
 * that the adapter is detected properly when probing.
 */
#define BASE_ADDR_REG_W 0x02	/* The select IO Base addr. */

/* These registers hold the Ethernet MAC address.
 */
#define IAR_ADDR0_REG_W 0x04	/* My Ethernet address */
#define IAR_ADDR1_REG_W 0x06	/* My Ethernet address */
#define IAR_ADDR2_REG_W 0x08	/* My Ethernet address */

/* General purpose register used for talking to the EEPROM.
 */
#define GENERAL_REG_W   0x0a

/* Control register used for talking to the EEPROM and
 * setting some EPH functions.
 */
#define CONTROL_REG_W    0x0c
#define CTR_STORE        0x0001	/* Store something to EEPROM */
#define CTR_RELOAD       0x0002	/* Read EEPROM into registers */
#define CTR_EEPROM_SEL   0x0004	/* Select registers for Reload/Store */
#define CTR_TE_ENABLE    0x0020	/* Enable TX Error detection via EPH_INT */
#define CTR_CR_ENABLE    0x0040	/* Enable Counter Rollover via EPH_INT */
#define CTR_LE_ENABLE    0x0080	/* Enable Link Error detection via EPH_INT */
#define CTR_AUTO_RELEASE 0x0800	/* Enable auto release mode for TX */
#define CTR_POWERDOWN    0x2000	/* Enter powerdown mode */
#define CTR_RCV_BAD      0x4000	/* Enable receipt of frames with bad CRC */


/* BANK 2
 */

/* Memory Management Unit Control Register
 * Controls allocation of memory to receive and
 * transmit functions.
 */
#define MMU_CMD_REG_W   0x00
#define MMUCR_BUSY      0x0001	/* MMU busy performing a release */

/* MMU Commands:
 */
#define MMUCR_NOP       0x0000	/* Do nothing */
#define MMUCR_ALLOC     0x0020	/* Or with number of 256 byte packets - 1 */
#define MMUCR_RESET     0x0040	/* Reset MMU State */
#define MMUCR_REMOVE    0x0060	/* Dequeue (but not free) current RX packet */
#define MMUCR_RELEASE   0x0080	/* Dequeue and free the current RX packet */
#define MMUCR_FREEPKT   0x00a0	/* Release packet in PNR register */
#define MMUCR_ENQUEUE   0x00c0	/* Enqueue the packet for transmit */
#define MMUCR_RESETTX   0x00e0	/* Reset transmit queues */

/* Packet Number at TX Area
 */
#define PACKET_NUM_REG_B   0x02

/* Packet number resulting from MMUCR_ALLOC
 */
#define ALLOC_RESULT_REG_B 0x03
#define ARR_FAILED      0x80

/* Transmit and receive queue heads
 */
#define FIFO_PORTS_REG_W 0x04
#define FIFO_REMPTY     0x8000
#define FIFO_TEMPTY     0x0080
#define FIFO_RX_MASK    0x7f00
#define FIFO_TX_MASK    0x007f

/* The address within the packet for reading/writing.  The
 * PTR_RCV bit is tricky.  When PTR_RCV==1, the packet number
 * to be read is found in the FIFO_PORTS_REG_W, FIFO_RX_MASK.
 * When PTR_RCV==0, the packet number to be written is found
 * in the PACKET_NUM_REG_B.
 */
#define POINTER_REG_W   0x06
#define PTR_READ        0x2000	/* Intended access mode */
#define PTR_AUTOINC     0x4000	/* Do auto inc after read/write */
#define PTR_RCV         0x8000	/* FIFO_RX is packet, otherwise PNR is packet */

/* Data I/O register to be used in conjunction with
 * The pointer register to read and write data from the
 * card.  The same register can be used for byte and word
 * ops.
 */
#define DATA_REG_W      0x08
#define DATA_REG_B      0x08
#define DATA_1_REG_B    0x08
#define DATA_2_REG_B    0x0a

/* Sense interrupt status (READ)
 */
#define INTR_STAT_REG_B 0x0c

/* Acknowledge interrupt sources (WRITE)
 */
#define INTR_ACK_REG_B  0x0c

/* Interrupt mask.  Bit set indicates interrupt allowed.
 */
#define INTR_MASK_REG_B 0x0d

/* Interrupts
 */
#define IM_RCV_INT      0x01	/* A packet has been received */
#define IM_TX_INT       0x02	/* Packet TX complete */
#define IM_TX_EMPTY_INT 0x04	/* No packets left to TX  */
#define IM_ALLOC_INT    0x08	/* Memory allocation completed */
#define IM_RX_OVRN_INT  0x10	/* Receiver was overrun */
#define IM_EPH_INT      0x20	/* Misc. EPH conditions (see CONTROL_REG_W) */
#define IM_ERCV_INT     0x40	/* not on SMC9192 */

/* BANK 3
 */

/* Multicast subscriptions.
 * The multicast handling in the SMC90Cxx is quite complicated.  A table
 * of multicast address subscriptions is provided and a clever way of
 * speeding the search of that table by hashing is implemented in the
 * hardware.  I have ignored this and simply subscribed to all multicasts
 * and let the kernel deal with the results.
 */
#define MULTICAST1_REG_W 0x00
#define MULTICAST2_REG_W 0x02
#define MULTICAST3_REG_W 0x04
#define MULTICAST4_REG_W 0x06

/* These registers do not exist on SMC9192, or at least
 * are not documented in the SMC91C92 data sheet.
 * The REVISION_REG_W register does however seem to work.
 */
#define MGMT_REG_W      0x08
#define REVISION_REG_W  0x0a	/* (hi: chip id low: rev #) */
#define ERCV_REG_W      0x0c

/* These are constants expected to be found in the
 * chip id register.
 */
#define CHIP_9190       3
#define CHIP_9194       4
#define CHIP_9195       5
#define CHIP_91100      7
#define CHIP_91100FD    8

/* When packets are stuffed into the card or sucked out of the card
 * they are set up more or less as follows:
 *
 * Addr msbyte   lsbyte
 * 00   SSSSSSSS SSSSSSSS - STATUS-WORD 16 bit TX or RX status
 * 02   RRRRR             - RESERVED (unused)
 * 02        CCC CCCCCCCC - BYTE COUNT (RX: always even, TX: bit 0 ignored)
 * 04   DDDDDDDD DDDDDDDD - DESTINATION ADDRESS
 * 06   DDDDDDDD DDDDDDDD        (48 bit Ethernet MAC Address)
 * 08   DDDDDDDD DDDDDDDD
 * 0A   SSSSSSSS SSSSSSSS - SOURCE ADDRESS
 * 0C   SSSSSSSS SSSSSSSS        (48 bit Ethernet MAC Address)
 * 0E   SSSSSSSS SSSSSSSS
 * 10   PPPPPPPP PPPPPPPP
 * ..   PPPPPPPP PPPPPPPP
 * C-2  CCCCCCCC          - CONTROL BYTE
 * C-2           PPPPPPPP - Last data byte (If odd length)
 *
 * The STATUS_WORD is derived from the EPH_STATUS_REG_W register
 * during transmit and is composed of another set of bits described
 * below during receive.
 */


/* Receive status bits.  These values are found in the status word
 * field of a received packet.  For receive packets I use the RS_ODDFRAME
 * to detect whether a frame has an extra byte on it.  The CTLB_ODD
 * bit of the control byte tells the same thing.
 */
#define RS_MULTICAST    0x0001	/* Packet is multicast */
#define RS_HASH_MASK    0x007e	/* Mask of multicast hash value */
#define RS_TOOSHORT     0x0400	/* Frame was a runt, <64 bytes */
#define RS_TOOLONG      0x0800	/* Frame was giant, >1518 */
#define RS_ODDFRAME     0x1000	/* Frame is odd lengthed */
#define RS_BADCRC       0x2000	/* Frame had CRC error */
#define RS_ALGNERR      0x8000	/* Frame had alignment error */
#define RS_ERRORS       (RS_ALGNERR | RS_BADCRC | RS_TOOLONG | RS_TOOSHORT)

#define RLEN_MASK       0x07ff	/* Significant length bits in RX length */

/* The control byte has the following significant bits.
 * For transmit, the CTLB_ODD bit specifies whether an extra byte
 * is present in the frame.  Bit 0 of the byte count field is
 * ignored.  I just pad every frame to even length and forget about
 * it.
 */
#define CTLB_CRC        0x10	/* Add CRC for this packet (TX only) */
#define CTLB_ODD        0x20	/* The packet length is ODD */


/*
 * I define some macros to make it easier to do somewhat common
 * or slightly complicated, repeated tasks.
 */

/* Select a register bank, 0 to 3
 */
#define SMC_SELECT_BANK(sc, x)  { CSR_WRITE_2(sc, BANK_SELECT_REG_W, (x)); }

/* Define a small delay for the reset
 */
#define SMC_DELAY(sc) { CSR_READ_2(sc, RECV_CONTROL_REG_W); \
                        CSR_READ_2(sc, RECV_CONTROL_REG_W); \
                        CSR_READ_2(sc, RECV_CONTROL_REG_W); }

#endif	/* _IF_SNREG_H_ */
