/*-
 * Copyright 1991-1998 by Open Software Foundation, Inc.
 *              All Rights Reserved
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appears in all copies and
 * that both the copyright notice and this permission notice appear in
 * supporting documentation.
 *
 * OSF DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL OSF BE LIABLE FOR ANY SPECIAL, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT,
 * NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2003 by Peter Grehan. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * BMAC resource indices
 */

#define BM_MAIN_REGISTERS	0
#define	BM_TXDMA_REGISTERS	1
#define	BM_RXDMA_REGISTERS	2

#define BM_MAIN_INTERRUPT	0
#define BM_TXDMA_INTERRUPT	1
#define BM_RXDMA_INTERRUPT	2

/*
 * BMAC/BMAC+ register offsets
 */

#define BM_TX_IFC	0x0000		/* interface control */
#define BM_TXFIFO_CSR	0x0100		/* TX FIFO control/status */
#define BM_TX_THRESH   	0x0110		/* TX threshold */
#define BM_RXFIFO_CSR	0x0120		/* receive FIFO control/status */
#define BM_MEMADD	0x0130		/* unused */
#define BM_MEMDATA_HI	0x0140		/* unused */
#define BM_MEMDATA_LO	0x0150		/* unused */
#define BM_XCVR		0x0160		/* transceiver control register */
#define BM_CHIPID	0x0170		/* chip ID */
#define BM_MII_CSR	0x0180		/* MII control register */
#define BM_SROM_CSR	0x0190		/* unused, OFW provides enet addr */
#define BM_TX_PTR	0x01A0		/* unused */
#define BM_RX_PTR	0x01B0		/* unused */
#define BM_STATUS	0x01C0		/* status register */
#define BM_INTR_DISABLE	0x0200		/* interrupt control register */
#define BM_TX_RESET	0x0420		/* TX reset */
#define BM_TX_CONFIG	0x0430		/* TX config */
#define BM_IPG1		0x0440		/* inter-packet gap hi */
#define BM_IPG2		0x0450		/* inter-packet gap lo */
#define BM_TX_ALIMIT	0x0460		/* TX attempt limit */
#define BM_TX_STIME	0x0470		/* TX slot time */
#define BM_TX_PASIZE	0x0480		/* TX preamble size */
#define BM_TX_PAPAT	0x0490		/* TX preamble pattern */
#define BM_TX_SFD	0x04A0		/* TX start-frame delimiter */
#define BM_JAMSIZE	0x04B0		/* collision jam size */
#define BM_TX_MAXLEN	0x04C0		/* max TX packet length */
#define BM_TX_MINLEN	0x04D0		/* min TX packet length */
#define BM_TX_PEAKCNT	0x04E0		/* TX peak attempts count */
#define BM_TX_DCNT	0x04F0		/* TX defer timer */
#define BM_TX_NCCNT	0x0500		/* TX normal collision cnt */
#define BM_TX_FCCNT	0x0510		/* TX first collision cnt */
#define BM_TX_EXCNT	0x0520		/* TX excess collision cnt */
#define BM_TX_LTCNT	0x0530		/* TX late collision cnt */
#define BM_TX_RANDSEED	0x0540		/* TX random seed */
#define BM_TXSM		0x0550		/* TX state machine */
#define BM_RX_RESET	0x0620		/* RX reset */
#define BM_RX_CONFIG	0x0630		/* RX config */
#define BM_RX_MAXLEN	0x0640		/* max RX packet length */
#define BM_RX_MINLEN	0x0650		/* min RX packet length */
#define BM_MACADDR2	0x0660		/* MAC address */
#define BM_MACADDR1	0x0670
#define BM_MACADDR0	0x0680
#define BM_RX_FRCNT	0x0690		/* RX frame count */
#define BM_RX_LECNT	0x06A0		/* RX too-long frame count */
#define BM_RX_AECNT	0x06B0		/* RX misaligned frame count */
#define BM_RX_FECNT	0x06C0		/* RX CRC error count */
#define BM_RXSM		0x06D0		/* RX state machine */
#define BM_RXCV		0x06E0		/* RX code violations */
#define BM_HASHTAB3	0x0700		/* Address hash table */
#define BM_HASHTAB2	0x0710
#define BM_HASHTAB1	0x0720
#define BM_HASHTAB0	0x0730
#define BM_AFILTER2	0x0740		/* Address filter */
#define BM_AFILTER1	0x0750
#define BM_AFILTER0	0x0760
#define BM_AFILTER_MASK 0x0770

/*
 * MII control register bits
 */
#define BM_MII_CLK	0x0001		/* MDIO clock */
#define BM_MII_DATAOUT	0x0002		/* MDIO data out */
#define BM_MII_OENABLE	0x0004		/* MDIO output enable */
#define BM_MII_DATAIN	0x0008		/* MDIO data in */

/*
 * Various flags
 */

#define BM_ENABLE		0x0001

#define BM_CRC_ENABLE		0x0100
#define BM_HASH_FILTER_ENABLE 	0x0200
#define BM_REJECT_OWN_PKTS 	0x0800
#define	BM_PROMISC		0x0040

#define BM_TX_FULLDPX		0x0200
#define BM_TX_IGNORECOLL	0x0040

#define BM_INTR_PKT_RX		0x0001
#define BM_INTR_PKT_TX		0x0100
#define BM_INTR_TX_UNDERRUN	0x0200

#define BM_INTR_NORMAL		~(BM_INTR_PKT_TX | BM_INTR_TX_UNDERRUN)
#define BM_INTR_NONE		0xffff

/*
 * register space access macros
 */
#define	CSR_WRITE_4(sc, reg, val)	\
	bus_write_4(sc->sc_memr, reg, val)
#define	CSR_WRITE_2(sc, reg, val)	\
	bus_write_2(sc->sc_memr, reg, val)
#define CSR_WRITE_1(sc, reg, val)	\
	bus_write_1(sc->sc_memr, reg, val)

#define CSR_READ_4(sc, reg)		\
	bus_read_4(sc->sc_memr, reg)
#define CSR_READ_2(sc, reg)		\
	bus_read_2(sc->sc_memr, reg)
#define	CSR_READ_1(sc, reg)		\
	bus_read_1(sc->sc_memr, reg)

#define CSR_BARRIER(sc, reg, length, flags)				\
	bus_barrier(sc->sc_memr, reg, length, flags)
