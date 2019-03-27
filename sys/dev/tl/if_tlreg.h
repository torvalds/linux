/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

struct tl_type {
	u_int16_t		tl_vid;
	u_int16_t		tl_did;
	const char		*tl_name;
};

/*
 * ThunderLAN TX/RX list format. The TX and RX lists are pretty much
 * identical: the list begins with a 32-bit forward pointer which points
 * at the next list in the chain, followed by 16 bits for the total
 * frame size, and a 16 bit status field. This is followed by a series
 * of 10 32-bit data count/data address pairs that point to the fragments
 * that make up the complete frame.
 */

#define TL_MAXFRAGS		10
#define TL_RX_LIST_CNT		64
#define TL_TX_LIST_CNT		128
#define TL_MIN_FRAMELEN		64

struct tl_frag {
	u_int32_t		tlist_dcnt;
	u_int32_t		tlist_dadr;
};

struct tl_list {
	u_int32_t		tlist_fptr;	/* phys address of next list */
	u_int16_t		tlist_cstat;	/* status word */
	u_int16_t		tlist_frsize;	/* size of data in frame */
	struct tl_frag		tl_frag[TL_MAXFRAGS];
};

/*
 * This is a special case of an RX list. By setting the One_Frag
 * bit in the NETCONFIG register, the driver can force the ThunderLAN
 * chip to use only one fragment when DMAing RX frames.
 */

struct tl_list_onefrag {
	u_int32_t		tlist_fptr;
	u_int16_t		tlist_cstat;
	u_int16_t		tlist_frsize;
	struct tl_frag		tl_frag;
};

struct tl_list_data {
	struct tl_list_onefrag	tl_rx_list[TL_RX_LIST_CNT];
	struct tl_list		tl_tx_list[TL_TX_LIST_CNT];
	unsigned char		tl_pad[TL_MIN_FRAMELEN];
};

struct tl_chain {
	struct tl_list		*tl_ptr;
	struct mbuf		*tl_mbuf;
	struct tl_chain		*tl_next;
};

struct tl_chain_onefrag {
	struct tl_list_onefrag	*tl_ptr;
	struct mbuf		*tl_mbuf;
	struct tl_chain_onefrag	*tl_next;
};

struct tl_chain_data {
	struct tl_chain_onefrag	tl_rx_chain[TL_RX_LIST_CNT];
	struct tl_chain		tl_tx_chain[TL_TX_LIST_CNT];

	struct tl_chain_onefrag	*tl_rx_head;
	struct tl_chain_onefrag	*tl_rx_tail;

	struct tl_chain		*tl_tx_head;
	struct tl_chain		*tl_tx_tail;
	struct tl_chain		*tl_tx_free;
};

struct tl_softc {
	struct ifnet		*tl_ifp;
	device_t		tl_dev;
	struct ifmedia		ifmedia;	/* media info */
	void			*tl_intrhand;
	struct resource		*tl_irq;
	struct resource		*tl_res;
	device_t		tl_miibus;
	u_int8_t		tl_eeaddr;
	struct tl_list_data	*tl_ldata;	/* TX/RX lists and mbufs */
	struct tl_chain_data	tl_cdata;
	u_int8_t		tl_txeoc;
	u_int8_t		tl_bitrate;
	int			tl_if_flags;
	struct callout		tl_stat_callout;
	struct mtx		tl_mtx;
	int			tl_timer;
};

#define	TL_LOCK(_sc)		mtx_lock(&(_sc)->tl_mtx)
#define	TL_UNLOCK(_sc)		mtx_unlock(&(_sc)->tl_mtx)
#define	TL_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->tl_mtx, MA_OWNED)

/*
 * Transmit interrupt threshold.
 */
#define TX_THR		0x00000004

/*
 * General constants that are fun to know.
 *
 * The ThunderLAN controller is made by Texas Instruments. The
 * manual indicates that if the EEPROM checksum fails, the PCI
 * vendor and device ID registers will be loaded with TI-specific
 * values.
 */
#define	TI_VENDORID		0x104C
#define	TI_DEVICEID_THUNDERLAN	0x0500

/*
 * These are the PCI vendor and device IDs for Compaq ethernet
 * adapters based on the ThunderLAN controller.
 */
#define COMPAQ_VENDORID				0x0E11
#define COMPAQ_DEVICEID_NETEL_10_100		0xAE32
#define COMPAQ_DEVICEID_NETEL_UNKNOWN		0xAE33
#define COMPAQ_DEVICEID_NETEL_10		0xAE34
#define COMPAQ_DEVICEID_NETFLEX_3P_INTEGRATED	0xAE35
#define COMPAQ_DEVICEID_NETEL_10_100_DUAL	0xAE40
#define COMPAQ_DEVICEID_NETEL_10_100_PROLIANT	0xAE43
#define COMPAQ_DEVICEID_NETEL_10_100_EMBEDDED	0xB011
#define COMPAQ_DEVICEID_NETEL_10_T2_UTP_COAX	0xB012
#define COMPAQ_DEVICEID_NETEL_10_100_TX_UTP	0xB030
#define COMPAQ_DEVICEID_NETFLEX_3P		0xF130
#define COMPAQ_DEVICEID_NETFLEX_3P_BNC		0xF150

/*
 * These are the PCI vendor and device IDs for Olicom
 * adapters based on the ThunderLAN controller.
 */
#define OLICOM_VENDORID				0x108D
#define OLICOM_DEVICEID_OC2183			0x0013
#define OLICOM_DEVICEID_OC2325			0x0012
#define OLICOM_DEVICEID_OC2326			0x0014

/*
 * PCI low memory base and low I/O base
 */
#define TL_PCI_LOIO		0x10
#define TL_PCI_LOMEM		0x14

/*
 * PCI latency timer (it's actually 0x0D, but we want a value
 * that's longword aligned).
 */
#define TL_PCI_LATENCY_TIMER	0x0C

#define	TL_DIO_ADDR_INC		0x8000	/* Increment addr on each read */
#define TL_DIO_RAM_SEL		0x4000	/* RAM address select */
#define	TL_DIO_ADDR_MASK	0x3FFF	/* address bits mask */

/*
 * Interrupt types
 */
#define TL_INTR_INVALID		0x0
#define TL_INTR_TXEOF		0x1
#define TL_INTR_STATOFLOW	0x2
#define TL_INTR_RXEOF		0x3
#define TL_INTR_DUMMY		0x4
#define TL_INTR_TXEOC		0x5
#define TL_INTR_ADCHK		0x6
#define TL_INTR_RXEOC		0x7

#define TL_INT_MASK		0x001C
#define TL_VEC_MASK		0x1FE0

/*
 * Host command register bits
 */
#define TL_CMD_GO               0x80000000
#define TL_CMD_STOP             0x40000000
#define TL_CMD_ACK              0x20000000
#define TL_CMD_CHSEL7		0x10000000
#define TL_CMD_CHSEL6		0x08000000
#define TL_CMD_CHSEL5		0x04000000
#define TL_CMD_CHSEL4		0x02000000
#define TL_CMD_CHSEL3		0x01000000
#define TL_CMD_CHSEL2           0x00800000
#define TL_CMD_CHSEL1           0x00400000
#define TL_CMD_CHSEL0           0x00200000
#define TL_CMD_EOC              0x00100000
#define TL_CMD_RT               0x00080000
#define TL_CMD_NES              0x00040000
#define TL_CMD_ZERO0            0x00020000
#define TL_CMD_ZERO1            0x00010000
#define TL_CMD_ADRST            0x00008000
#define TL_CMD_LDTMR            0x00004000
#define TL_CMD_LDTHR            0x00002000
#define TL_CMD_REQINT           0x00001000
#define TL_CMD_INTSOFF          0x00000800
#define TL_CMD_INTSON		0x00000400
#define TL_CMD_RSVD0		0x00000200
#define TL_CMD_RSVD1		0x00000100
#define TL_CMD_ACK7		0x00000080
#define TL_CMD_ACK6		0x00000040
#define TL_CMD_ACK5		0x00000020
#define TL_CMD_ACK4		0x00000010
#define TL_CMD_ACK3		0x00000008
#define TL_CMD_ACK2		0x00000004
#define TL_CMD_ACK1		0x00000002
#define TL_CMD_ACK0		0x00000001

#define TL_CMD_CHSEL_MASK	0x01FE0000
#define TL_CMD_ACK_MASK		0xFF

/*
 * EEPROM address where station address resides.
 */
#define TL_EEPROM_EADDR		0x83
#define TL_EEPROM_EADDR2	0x99
#define TL_EEPROM_EADDR3	0xAF
#define TL_EEPROM_EADDR_OC	0xF8	/* Olicom cards use a different
					   address than Compaqs. */
/*
 * ThunderLAN host command register offsets.
 * (Can be accessed either by IO ports or memory map.)
 */
#define TL_HOSTCMD		0x00
#define TL_CH_PARM		0x04
#define TL_DIO_ADDR		0x08
#define TL_HOST_INT		0x0A
#define TL_DIO_DATA		0x0C

/*
 * ThunderLAN internal registers
 */
#define TL_NETCMD		0x00
#define TL_NETSIO		0x01
#define TL_NETSTS		0x02
#define TL_NETMASK		0x03

#define TL_NETCONFIG		0x04
#define TL_MANTEST		0x06

#define TL_VENID_LSB		0x08
#define TL_VENID_MSB		0x09
#define TL_DEVID_LSB		0x0A
#define TL_DEVID_MSB		0x0B

#define TL_REVISION		0x0C
#define TL_SUBCLASS		0x0D
#define TL_MINLAT		0x0E
#define TL_MAXLAT		0x0F

#define TL_AREG0_B5		0x10
#define TL_AREG0_B4		0x11
#define TL_AREG0_B3		0x12
#define TL_AREG0_B2		0x13

#define TL_AREG0_B1		0x14
#define TL_AREG0_B0		0x15
#define TL_AREG1_B5		0x16
#define TL_AREG1_B4		0x17

#define TL_AREG1_B3		0x18
#define TL_AREG1_B2		0x19
#define TL_AREG1_B1		0x1A
#define TL_AREG1_B0		0x1B

#define TL_AREG2_B5		0x1C
#define TL_AREG2_B4		0x1D
#define TL_AREG2_B3		0x1E
#define TL_AREG2_B2		0x1F

#define TL_AREG2_B1		0x20
#define TL_AREG2_B0		0x21
#define TL_AREG3_B5		0x22
#define TL_AREG3_B4		0x23

#define TL_AREG3_B3		0x24
#define TL_AREG3_B2		0x25
#define TL_AREG3_B1		0x26
#define TL_AREG3_B0		0x27

#define TL_HASH1		0x28
#define TL_HASH2		0x2C
#define TL_TXGOODFRAMES		0x30
#define TL_TXUNDERRUN		0x33
#define TL_RXGOODFRAMES		0x34
#define TL_RXOVERRUN		0x37
#define TL_DEFEREDTX		0x38
#define TL_CRCERROR		0x3A
#define TL_CODEERROR		0x3B
#define TL_MULTICOLTX		0x3C
#define TL_SINGLECOLTX		0x3E
#define TL_EXCESSIVECOL		0x40
#define TL_LATECOL		0x41
#define TL_CARRIERLOSS		0x42
#define TL_ACOMMIT		0x43
#define TL_LDREG		0x44
#define TL_BSIZEREG		0x45
#define TL_MAXRX		0x46

/*
 * ThunderLAN SIO register bits
 */
#define TL_SIO_MINTEN		0x80
#define TL_SIO_ECLOK		0x40
#define TL_SIO_ETXEN		0x20
#define TL_SIO_EDATA		0x10
#define TL_SIO_NMRST		0x08
#define TL_SIO_MCLK		0x04
#define TL_SIO_MTXEN		0x02
#define TL_SIO_MDATA		0x01

/*
 * Thunderlan NETCONFIG bits
 */
#define TL_CFG_RCLKTEST		0x8000
#define TL_CFG_TCLKTEST		0x4000
#define TL_CFG_BITRATE		0x2000
#define TL_CFG_RXCRC		0x1000
#define TL_CFG_PEF		0x0800
#define TL_CFG_ONEFRAG		0x0400
#define TL_CFG_ONECHAN		0x0200
#define TL_CFG_MTEST		0x0100
#define TL_CFG_PHYEN		0x0080
#define TL_CFG_MACSEL6		0x0040
#define TL_CFG_MACSEL5		0x0020
#define TL_CFG_MACSEL4		0x0010
#define TL_CFG_MACSEL3		0x0008
#define TL_CFG_MACSEL2		0x0004
#define TL_CFG_MACSEL1		0x0002
#define TL_CFG_MACSEL0		0x0001

/*
 * ThunderLAN NETSTS bits
 */
#define TL_STS_MIRQ		0x80
#define TL_STS_HBEAT		0x40
#define TL_STS_TXSTOP		0x20
#define TL_STS_RXSTOP		0x10

/*
 * ThunderLAN NETCMD bits
 */
#define TL_CMD_NRESET		0x80
#define TL_CMD_NWRAP		0x40
#define TL_CMD_CSF		0x20
#define TL_CMD_CAF		0x10
#define TL_CMD_NOBRX		0x08
#define TL_CMD_DUPLEX		0x04
#define TL_CMD_TRFRAM		0x02
#define TL_CMD_TXPACE		0x01

/*
 * ThunderLAN NETMASK bits
 */
#define TL_MASK_MASK7		0x80
#define TL_MASK_MASK6		0x40
#define TL_MASK_MASK5		0x20
#define TL_MASK_MASK4		0x10

#define TL_LAST_FRAG		0x80000000
#define TL_CSTAT_UNUSED		0x8000
#define TL_CSTAT_FRAMECMP	0x4000
#define TL_CSTAT_READY		0x3000
#define TL_CSTAT_UNUSED13	0x2000
#define TL_CSTAT_UNUSED12	0x1000
#define TL_CSTAT_EOC		0x0800
#define TL_CSTAT_RXERROR	0x0400
#define TL_CSTAT_PASSCRC	0x0200
#define TL_CSTAT_DPRIO		0x0100

#define TL_FRAME_MASK		0x00FFFFFF
#define tl_tx_goodframes(x)	(x.tl_txstat & TL_FRAME_MASK)
#define tl_tx_underrun(x)	((x.tl_txstat & ~TL_FRAME_MASK) >> 24)
#define tl_rx_goodframes(x)	(x.tl_rxstat & TL_FRAME_MASK)
#define tl_rx_overrun(x)	((x.tl_rxstat & ~TL_FRAME_MASK) >> 24)

struct tl_stats {
	u_int32_t		tl_txstat;
	u_int32_t		tl_rxstat;
	u_int16_t		tl_deferred;
	u_int8_t		tl_crc_errors;
	u_int8_t		tl_code_errors;
	u_int16_t		tl_tx_multi_collision;
	u_int16_t		tl_tx_single_collision;
	u_int8_t		tl_excessive_collision;
	u_int8_t		tl_late_collision;
	u_int8_t		tl_carrier_loss;
	u_int8_t		acommit;
};

/*
 * ACOMMIT register bits. These are used only when a bitrate
 * PHY is selected ('bitrate' bit in netconfig register is set).
 */
#define TL_AC_MTXER		0x01	/* reserved */
#define TL_AC_MTXD1		0x02	/* 0 == 10baseT 1 == AUI */
#define TL_AC_MTXD2		0x04	/* loopback disable */
#define TL_AC_MTXD3		0x08	/* full duplex disable */

#define TL_AC_TXTHRESH		0xF0
#define TL_AC_TXTHRESH_16LONG	0x00
#define TL_AC_TXTHRESH_32LONG	0x10
#define TL_AC_TXTHRESH_64LONG	0x20
#define TL_AC_TXTHRESH_128LONG	0x30
#define TL_AC_TXTHRESH_256LONG	0x40
#define TL_AC_TXTHRESH_WHOLEPKT	0x50

/*
 * PCI burst size register (TL_BSIZEREG).
 */
#define TL_RXBURST		0x0F
#define TL_TXBURST		0xF0

#define TL_RXBURST_4LONG	0x00
#define TL_RXBURST_8LONG	0x01
#define TL_RXBURST_16LONG	0x02
#define TL_RXBURST_32LONG	0x03
#define TL_RXBURST_64LONG	0x04
#define TL_RXBURST_128LONG	0x05

#define TL_TXBURST_4LONG	0x00
#define TL_TXBURST_8LONG	0x10
#define TL_TXBURST_16LONG	0x20
#define TL_TXBURST_32LONG	0x30
#define TL_TXBURST_64LONG	0x40
#define TL_TXBURST_128LONG	0x50

/*
 * register space access macros
 */
#define CSR_WRITE_4(sc, reg, val)	bus_write_4(sc->tl_res, reg, val)
#define CSR_WRITE_2(sc, reg, val)	bus_write_2(sc->tl_res, reg, val)
#define CSR_WRITE_1(sc, reg, val)	bus_write_1(sc->tl_res, reg, val)

#define CSR_READ_4(sc, reg)		bus_read_4(sc->tl_res, reg)
#define CSR_READ_2(sc, reg)		bus_read_2(sc->tl_res, reg)
#define CSR_READ_1(sc, reg)		bus_read_1(sc->tl_res, reg)

#define	CSR_BARRIER(sc, reg, length, flags)				\
	bus_barrier(sc->tl_res, reg, length, flags)

#define CMD_PUT(sc, x) CSR_WRITE_4(sc, TL_HOSTCMD, x)
#define CMD_SET(sc, x)	\
	CSR_WRITE_4(sc, TL_HOSTCMD, CSR_READ_4(sc, TL_HOSTCMD) | (x))
#define CMD_CLR(sc, x)	\
	CSR_WRITE_4(sc, TL_HOSTCMD, CSR_READ_4(sc, TL_HOSTCMD) & ~(x))

/*
 * ThunderLAN adapters typically have a serial EEPROM containing
 * configuration information. The main reason we're interested in
 * it is because it also contains the adapters's station address.
 *
 * Access to the EEPROM is a bit goofy since it is a serial device:
 * you have to do reads and writes one bit at a time. The state of
 * the DATA bit can only change while the CLOCK line is held low.
 * Transactions work basically like this:
 *
 * 1) Send the EEPROM_START sequence to prepare the EEPROM for
 *    accepting commands. This pulls the clock high, sets
 *    the data bit to 0, enables transmission to the EEPROM,
 *    pulls the data bit up to 1, then pulls the clock low.
 *    The idea is to do a 0 to 1 transition of the data bit
 *    while the clock pin is held high.
 *
 * 2) To write a bit to the EEPROM, set the TXENABLE bit, then
 *    set the EDATA bit to send a 1 or clear it to send a 0.
 *    Finally, set and then clear ECLOK. Strobing the clock
 *    transmits the bit. After 8 bits have been written, the
 *    EEPROM should respond with an ACK, which should be read.
 *
 * 3) To read a bit from the EEPROM, clear the TXENABLE bit,
 *    then set ECLOK. The bit can then be read by reading EDATA.
 *    ECLOCK should then be cleared again. This can be repeated
 *    8 times to read a whole byte, after which the 
 *
 * 4) We need to send the address byte to the EEPROM. For this
 *    we have to send the write control byte to the EEPROM to
 *    tell it to accept data. The byte is 0xA0. The EEPROM should
 *    ack this. The address byte can be send after that.
 *
 * 5) Now we have to tell the EEPROM to send us data. For that we
 *    have to transmit the read control byte, which is 0xA1. This
 *    byte should also be acked. We can then read the data bits
 *    from the EEPROM.
 *
 * 6) When we're all finished, send the EEPROM_STOP sequence.
 *
 * Note that we use the ThunderLAN's NetSio register to access the
 * EEPROM, however there is an alternate method. There is a PCI NVRAM
 * register at PCI offset 0xB4 which can also be used with minor changes.
 * The difference is that access to PCI registers via pci_conf_read()
 * and pci_conf_write() is done using programmed I/O, which we want to
 * avoid.
 */

/*
 * Note that EEPROM_START leaves transmission enabled.
 */
#define EEPROM_START							\
	tl_dio_setbit(sc, TL_NETSIO, TL_SIO_ECLOK); /* Pull clock pin high */\
	tl_dio_setbit(sc, TL_NETSIO, TL_SIO_EDATA); /* Set DATA bit to 1 */	\
	tl_dio_setbit(sc, TL_NETSIO, TL_SIO_ETXEN); /* Enable xmit to write bit */\
	tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_EDATA); /* Pull DATA bit to 0 again */\
	tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_ECLOK); /* Pull clock low again */

/*
 * EEPROM_STOP ends access to the EEPROM and clears the ETXEN bit so
 * that no further data can be written to the EEPROM I/O pin.
 */
#define EEPROM_STOP							\
	tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_ETXEN); /* Disable xmit */	\
	tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_EDATA); /* Pull DATA to 0 */	\
	tl_dio_setbit(sc, TL_NETSIO, TL_SIO_ECLOK); /* Pull clock high */	\
	tl_dio_setbit(sc, TL_NETSIO, TL_SIO_ETXEN); /* Enable xmit */	\
	tl_dio_setbit(sc, TL_NETSIO, TL_SIO_EDATA); /* Toggle DATA to 1 */	\
	tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_ETXEN); /* Disable xmit. */	\
	tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_ECLOK); /* Pull clock low again */


/*
 * Microchip Technology 24Cxx EEPROM control bytes
 */
#define EEPROM_CTL_READ			0xA1	/* 0101 0001 */
#define EEPROM_CTL_WRITE		0xA0	/* 0101 0000 */
