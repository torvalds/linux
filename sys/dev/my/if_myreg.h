/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Myson Technology Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Written by: yen_cw@myson.com.tw  available at: http://www.myson.com.tw/
 *
 * $FreeBSD$
 *
 * Myson MTD80x register definitions.
 *
 */
#define MY_PAR0         0x0     /* physical address 0-3 */
#define MY_PAR1         0x04    /* physical address 4-5 */
#define MY_MAR0         0x08    /* multicast address 0-3 */
#define MY_MAR1         0x0C    /* multicast address 4-7 */
#define MY_FAR0         0x10    /* flow-control address 0-3 */
#define MY_FAR1         0x14    /* flow-control address 4-5 */
#define MY_TCRRCR       0x18    /* receive & transmit configuration */
#define MY_BCR          0x1C    /* bus command */
#define MY_TXPDR        0x20    /* transmit polling demand */
#define MY_RXPDR        0x24    /* receive polling demand */
#define MY_RXCWP        0x28    /* receive current word pointer */
#define MY_TXLBA        0x2C    /* transmit list base address */
#define MY_RXLBA        0x30    /* receive list base address */
#define MY_ISR          0x34    /* interrupt status */
#define MY_IMR          0x38    /* interrupt mask */
#define MY_FTH          0x3C    /* flow control high/low threshold */
#define MY_MANAGEMENT   0x40    /* bootrom/eeprom and mii management */
#define MY_TALLY        0x44    /* tally counters for crc and mpa */
#define MY_TSR          0x48    /* tally counter for transmit status */
#define MY_PHYBASE	0x4c

/*
 * Receive Configuration Register
 */
#define MY_RXRUN        0x00008000      /* receive running status */
#define MY_EIEN         0x00004000      /* early interrupt enable */
#define MY_RFCEN        0x00002000      /* receive flow control packet enable */
#define MY_NDFA         0x00001000      /* not defined flow control address */
#define MY_RBLEN        0x00000800      /* receive burst length enable */
#define MY_RPBLE1       0x00000000      /* 1 word */
#define MY_RPBLE4       0x00000100      /* 4 words */
#define MY_RPBLE8       0x00000200      /* 8 words */
#define MY_RPBLE16      0x00000300      /* 16 words */
#define MY_RPBLE32      0x00000400      /* 32 words */
#define MY_RPBLE64      0x00000500      /* 64 words */
#define MY_RPBLE128     0x00000600      /* 128 words */
#define MY_RPBLE512     0x00000700      /* 512 words */
#define MY_PROM         0x000000080     /* promiscuous mode */
#define MY_AB           0x000000040     /* accept broadcast */
#define MY_AM           0x000000020     /* accept mutlicast */
#define MY_ARP          0x000000008     /* receive runt pkt */
#define MY_ALP          0x000000004     /* receive long pkt */
#define MY_SEP          0x000000002     /* receive error pkt */
#define MY_RE           0x000000001     /* receive enable */

/*
 * Transmit Configuration Register
 */
#define MY_TXRUN        0x04000000      /* transmit running status */
#define MY_Enhanced     0x02000000      /* transmit enhanced mode */
#define MY_TFCEN        0x01000000      /* tx flow control packet enable */
#define MY_TFT64        0x00000000      /* 64 bytes */
#define MY_TFT32        0x00200000      /* 32 bytes */
#define MY_TFT128       0x00400000      /* 128 bytes */
#define MY_TFT256       0x00600000      /* 256 bytes */
#define MY_TFT512       0x00800000      /* 512 bytes */
#define MY_TFT768       0x00A00000      /* 768 bytes */
#define MY_TFT1024      0x00C00000      /* 1024 bytes */
#define MY_TFTSF        0x00E00000      /* store and forward */
#define MY_FD           0x00100000      /* full duplex mode */
#define MY_PS10         0x00080000      /* port speed is 10M */
#define MY_TE           0x00040000      /* transmit enable */
#define MY_PS1000       0x00010000      /* port speed is 1000M */
/*
 * Bus Command Register
 */
#define MY_PROG		0x00000200	/* programming */
#define MY_RLE          0x00000100      /* read line command enable */
#define MY_RME          0x00000080      /* read multiple command enable */
#define MY_WIE          0x00000040      /* write and invalidate cmd enable */
#define MY_PBL1         0x00000000      /* 1 dword */
#define MY_PBL4         0x00000008      /* 4 dwords */
#define MY_PBL8         0x00000010      /* 8 dwords */
#define MY_PBL16        0x00000018      /* 16 dwords */
#define MY_PBL32        0x00000020      /* 32 dwords */
#define MY_PBL64        0x00000028      /* 64 dwords */
#define MY_PBL128       0x00000030      /* 128 dwords */
#define MY_PBL512       0x00000038      /* 512 dwords */
#define MY_ABR          0x00000004      /* arbitration rule */
#define MY_BLS          0x00000002      /* big/little endian select */
#define MY_SWR          0x00000001      /* software reset */

/*
 * Transmit Poll Demand Register
 */
#define MY_TxPollDemand 0x1

/*
 * Receive Poll Demand Register
 */
#define MY_RxPollDemand 0x01

/*
 * Interrupt Status Register
 */
#define MY_RFCON        0x00020000      /* receive flow control xon packet */
#define MY_RFCOFF       0x00010000      /* receive flow control xoff packet */
#define MY_LSCStatus    0x00008000      /* link status change */
#define MY_ANCStatus    0x00004000      /* autonegotiation completed */
#define MY_FBE          0x00002000      /* fatal bus error */
#define MY_FBEMask      0x00001800
#define MY_ParityErr    0x00000000      /* parity error */
#define MY_MasterErr    0x00000800      /* master error */
#define MY_TargetErr    0x00001000      /* target abort */
#define MY_TUNF         0x00000400      /* transmit underflow */
#define MY_ROVF         0x00000200      /* receive overflow */
#define MY_ETI          0x00000100      /* transmit early int */
#define MY_ERI          0x00000080      /* receive early int */
#define MY_CNTOVF       0x00000040      /* counter overflow */
#define MY_RBU          0x00000020      /* receive buffer unavailable */
#define MY_TBU          0x00000010      /* transmit buffer unavilable */
#define MY_TI           0x00000008      /* transmit interrupt */
#define MY_RI           0x00000004      /* receive interrupt */
#define MY_RxErr        0x00000002      /* receive error */

/*
 * Interrupt Mask Register
 */
#define MY_MRFCON       0x00020000      /* receive flow control xon packet */
#define MY_MRFCOFF      0x00010000      /* receive flow control xoff packet */
#define MY_MLSCStatus   0x00008000      /* link status change */
#define MY_MANCStatus   0x00004000      /* autonegotiation completed */
#define MY_MFBE         0x00002000      /* fatal bus error */
#define MY_MFBEMask     0x00001800
#define MY_MTUNF        0x00000400      /* transmit underflow */
#define MY_MROVF        0x00000200      /* receive overflow */
#define MY_METI         0x00000100      /* transmit early int */
#define MY_MERI         0x00000080      /* receive early int */
#define MY_MCNTOVF      0x00000040      /* counter overflow */
#define MY_MRBU         0x00000020      /* receive buffer unavailable */
#define MY_MTBU         0x00000010      /* transmit buffer unavilable */
#define MY_MTI          0x00000008      /* transmit interrupt */
#define MY_MRI          0x00000004      /* receive interrupt */
#define MY_MRxErr       0x00000002      /* receive error */

/* 90/1/18 delete */
/* #define MY_INTRS MY_FBE|MY_MRBU|MY_TBU|MY_MTI|MY_MRI|MY_METI */
#define MY_INTRS MY_MRBU|MY_TBU|MY_MTI|MY_MRI|MY_METI

/*
 * Flow Control High/Low Threshold Register
 */
#define MY_FCHTShift    16      /* flow control high threshold */
#define MY_FCLTShift    0       /* flow control low threshold */

/*
 * BootROM/EEPROM/MII Management Register
 */
#define MY_MASK_MIIR_MII_READ   0x00000000
#define MY_MASK_MIIR_MII_WRITE  0x00000008
#define MY_MASK_MIIR_MII_MDO    0x00000004
#define MY_MASK_MIIR_MII_MDI    0x00000002
#define MY_MASK_MIIR_MII_MDC    0x00000001

/*
 * Tally Counter for CRC and MPA
 */
#define MY_TCOVF        0x80000000      /* crc tally counter overflow */
#define MY_CRCMask      0x7fff0000      /* crc number: bit 16-30 */
#define MY_CRCShift     16
#define MY_TMOVF        0x00008000      /* mpa tally counter overflow */
#define MY_MPAMask      0x00007fff      /* mpa number: bit 0-14 */
#define MY_MPAShift     0

/*
 * Tally Counters for transmit status
 */
#define MY_AbortMask      0xff000000    /* transmit abort number */
#define MY_AbortShift     24
#define MY_LColMask       0x00ff0000    /* transmit late collisions */
#define MY_LColShift      16
#define MY_NCRMask        0x0000ffff    /* transmit retry number */
#define MY_NCRShift       0

/*
 * Myson TX/RX descriptor structure.
 */

struct my_desc {
        u_int32_t       my_status;
        u_int32_t       my_ctl;
        u_int32_t       my_data;
        u_int32_t       my_next;
};

/*
 * for tx/rx descriptors
 */
#define MY_OWNByNIC     0x80000000
#define MY_OWNByDriver  0x0

/*
 * receive descriptor 0
 */
#define MY_RXOWN        0x80000000      /* own bit */
#define MY_FLNGMASK     0x0fff0000      /* frame length */
#define MY_FLNGShift    16
#define MY_MARSTATUS    0x00004000      /* multicast address received */
#define MY_BARSTATUS    0x00002000      /* broadcast address received */
#define MY_PHYSTATUS    0x00001000      /* physical address received */
#define MY_RXFSD        0x00000800      /* first descriptor */
#define MY_RXLSD        0x00000400      /* last descriptor */
#define MY_ES           0x00000080      /* error summary */
#define MY_RUNT         0x00000040      /* runt packet received */
#define MY_LONG         0x00000020      /* long packet received */
#define MY_FAE          0x00000010      /* frame align error */
#define MY_CRC          0x00000008      /* crc error */
#define MY_RXER         0x00000004      /* receive error */
#define MY_RDES0CHECK   0x000078fc      /* only check MAR, BAR, PHY, ES, RUNT,
                                           LONG, FAE, CRC and RXER bits */

/*
 * receive descriptor 1
 */
#define MY_RXIC         0x00800000      /* interrupt control */
#define MY_RBSMASK      0x000007ff      /* receive buffer size */
#define MY_RBSShift     0

/*
 * transmit descriptor 0
 */
#define MY_TXERR        0x00008000      /* transmit error */
#define MY_JABTO        0x00004000      /* jabber timeout */
#define MY_CSL          0x00002000      /* carrier sense lost */
#define MY_LC           0x00001000      /* late collision */
#define MY_EC           0x00000800      /* excessive collision */
#define MY_UDF          0x00000400      /* fifo underflow */
#define MY_DFR          0x00000200      /* deferred */
#define MY_HF           0x00000100      /* heartbeat fail */
#define MY_NCRMASK      0x000000ff      /* collision retry count */
#define MY_NCRShift     0

/*
 * tx descriptor 1
 */
#define MY_TXIC         0x80000000      /* interrupt control */
#define MY_ETIControl   0x40000000      /* early transmit interrupt */
#define MY_TXLD         0x20000000      /* last descriptor */
#define MY_TXFD         0x10000000      /* first descriptor */
#define MY_CRCDisable   0x00000000      /* crc control */
#define MY_CRCEnable    0x08000000
#define MY_PADDisable   0x00000000      /* padding control */
#define MY_PADEnable    0x04000000
#define MY_RetryTxLC	0x02000000	/* retry late collision */
#define MY_PKTShift     11              /* transmit pkt size */
#define MY_TBSMASK      0x000007ff
#define MY_TBSShift     0               /* transmit buffer size */

#define MY_MAXFRAGS     1
#define MY_RX_LIST_CNT  64
#define MY_TX_LIST_CNT  64
#define MY_MIN_FRAMELEN 60

/*
 * A transmit 'super descriptor' is actually MY_MAXFRAGS regular
 * descriptors clumped together. The idea here is to emulate the
 * multi-fragment descriptor layout found in devices such as the
 * Texas Instruments ThunderLAN and 3Com boomerang and cylone chips.
 * The advantage to using this scheme is that it avoids buffer copies.
 * The disadvantage is that there's a certain amount of overhead due
 * to the fact that each 'fragment' is 16 bytes long. In my tests,
 * this limits top speed to about 10.5MB/sec. It should be more like
 * 11.5MB/sec. However, the upshot is that you can achieve better
 * results on slower machines: a Pentium 200 can pump out packets at
 * same speed as a PII 400.
 */
struct my_txdesc {
        struct my_desc          my_frag[MY_MAXFRAGS];
};

#define MY_TXSTATUS(x)  x->my_ptr->my_frag[x->my_lastdesc].my_status
#define MY_TXCTL(x)     x->my_ptr->my_frag[x->my_lastdesc].my_ctl
#define MY_TXDATA(x)    x->my_ptr->my_frag[x->my_lastdesc].my_data
#define MY_TXNEXT(x)    x->my_ptr->my_frag[x->my_lastdesc].my_next

#define MY_TXOWN(x)     x->my_ptr->my_frag[0].my_status

#define MY_UNSENT       0x1234

struct my_list_data {
        struct my_desc          my_rx_list[MY_RX_LIST_CNT];
        struct my_txdesc        my_tx_list[MY_TX_LIST_CNT];
};

struct my_chain {
        struct my_txdesc        *my_ptr;
        struct mbuf             *my_mbuf;
        struct my_chain         *my_nextdesc;
        u_int8_t                my_lastdesc;
};

struct my_chain_onefrag {
        struct my_desc          *my_ptr;
        struct mbuf             *my_mbuf;
        struct my_chain_onefrag *my_nextdesc;
        u_int8_t                my_rlast;
};

struct my_chain_data {
        struct my_chain_onefrag my_rx_chain[MY_RX_LIST_CNT];
        struct my_chain         my_tx_chain[MY_TX_LIST_CNT];

        struct my_chain_onefrag *my_rx_head;

        struct my_chain         *my_tx_head;
        struct my_chain         *my_tx_tail;
        struct my_chain         *my_tx_free;
};

struct my_type {
        u_int16_t               my_vid;
        u_int16_t               my_did;
        char                    *my_name;
};

#define MY_FLAG_FORCEDELAY      1
#define MY_FLAG_SCHEDDELAY      2
#define MY_FLAG_DELAYTIMEO      3

struct my_softc {
        struct ifnet            *my_ifp;
	device_t		my_dev;
        struct ifmedia          ifmedia;        /* media info */
        bus_space_handle_t      my_bhandle;
        bus_space_tag_t         my_btag;
        struct my_type          *my_info;       /* adapter info */
        struct my_type          *my_pinfo;      /* phy info */
	struct resource		*my_res;
	struct resource		*my_irq;
	void			*my_intrhand;
        u_int8_t                my_phy_addr;    /* PHY address */
        u_int8_t                my_tx_pend;     /* TX pending */
        u_int8_t                my_want_auto;
        u_int8_t                my_autoneg;
        u_int16_t               my_txthresh;
	u_int8_t		my_stats_no_timeout;        
        caddr_t                 my_ldata_ptr;
        struct my_list_data     *my_ldata;
        struct my_chain_data    my_cdata;
	device_t		my_miibus;
/* Add by Surfer 2001/12/2 */
	struct mtx		my_mtx;
	struct callout		my_autoneg_timer;
	struct callout		my_watchdog;
	int			my_timer;
};

/* Add by Surfer 2001/12/2 */
#define	MY_LOCK(_sc)		mtx_lock(&(_sc)->my_mtx)
#define	MY_UNLOCK(_sc)		mtx_unlock(&(_sc)->my_mtx)
#define	MY_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->my_mtx, MA_OWNED)

/*
 * register space access macros
 */
#define CSR_WRITE_4(sc, reg, val)       \
        bus_space_write_4(sc->my_btag, sc->my_bhandle, reg, val)
#define CSR_WRITE_2(sc, reg, val)       \
        bus_space_write_2(sc->my_btag, sc->my_bhandle, reg, val)
#define CSR_WRITE_1(sc, reg, val)       \
        bus_space_write_1(sc->my_btag, sc->my_bhandle, reg, val)

#define CSR_READ_4(sc, reg)     \
        bus_space_read_4(sc->my_btag, sc->my_bhandle, reg)
#define CSR_READ_2(sc, reg)     \
        bus_space_read_2(sc->my_btag, sc->my_bhandle, reg)
#define CSR_READ_1(sc, reg)     \
        bus_space_read_1(sc->my_btag, sc->my_bhandle, reg)

#define MY_TIMEOUT              1000

/*
 * General constants that are fun to know.
 *
 * MYSON PCI vendor ID
 */
#define MYSONVENDORID           0x1516

/*
 * MYSON device IDs.
 */
#define MTD800ID                0x0800
#define MTD803ID                0x0803
#define MTD891ID                0x0891

/*
 * ST+OP+PHYAD+REGAD+TA
 */
#define MY_OP_READ      0x6000  /* ST:01+OP:10+PHYAD+REGAD+TA:Z0 */
#define MY_OP_WRITE     0x5002  /* ST:01+OP:01+PHYAD+REGAD+TA:10 */

/*
 * Constansts for Myson PHY
 */
#define MysonPHYID0     0x0300

/*
 * Constansts for Seeq 80225 PHY
 */
#define SeeqPHYID0      0x0016

#define SEEQ_MIIRegister18      18
#define SEEQ_SPD_DET_100        0x80
#define SEEQ_DPLX_DET_FULL      0x40

/*
 * Constansts for Ahdoc 101 PHY
 */
#define AhdocPHYID0     0x0022

#define AHDOC_DiagnosticReg     18
#define AHDOC_DPLX_FULL         0x0800
#define AHDOC_Speed_100         0x0400

/*
 * Constansts for Marvell 88E1000/88E1000S PHY and LevelOne PHY
 */
#define MarvellPHYID0           0x0141
#define LevelOnePHYID0		0x0013

#define Marvell_SpecificStatus  17
#define Marvell_Speed1000       0x8000
#define Marvell_Speed100        0x4000
#define Marvell_FullDuplex      0x2000

/*
 * PCI low memory base and low I/O base register, and
 * other PCI registers. Note: some are only available on
 * the 3c905B, in particular those that related to power management.
 */
#define MY_PCI_VENDOR_ID        0x00
#define MY_PCI_DEVICE_ID        0x02
#define MY_PCI_COMMAND          0x04
#define MY_PCI_STATUS           0x06
#define MY_PCI_CLASSCODE        0x09
#define MY_PCI_LATENCY_TIMER    0x0D
#define MY_PCI_HEADER_TYPE      0x0E
#define MY_PCI_LOIO             0x10
#define MY_PCI_LOMEM            0x14
#define MY_PCI_BIOSROM          0x30
#define MY_PCI_INTLINE          0x3C
#define MY_PCI_INTPIN           0x3D
#define MY_PCI_MINGNT           0x3E
#define MY_PCI_MINLAT           0x0F
#define MY_PCI_RESETOPT         0x48
#define MY_PCI_EEPROM_DATA      0x4C

#define PHY_UNKNOWN             3

#define MY_PHYADDR_MIN          0x00
#define MY_PHYADDR_MAX          0x1F

#define PHY_BMCR                0x00
#define PHY_BMSR                0x01
#define PHY_VENID               0x02
#define PHY_DEVID               0x03
#define PHY_ANAR                0x04
#define PHY_LPAR                0x05
#define PHY_ANEXP               0x06
#define PHY_NPTR                0x07
#define PHY_LPNPR               0x08
#define PHY_1000CR              0x09
#define PHY_1000SR              0x0a

#define PHY_ANAR_NEXTPAGE       0x8000
#define PHY_ANAR_RSVD0          0x4000
#define PHY_ANAR_TLRFLT         0x2000
#define PHY_ANAR_RSVD1          0x1000
#define PHY_ANAR_RSVD2          0x0800
#define PHY_ANAR_RSVD3          0x0400
#define PHY_ANAR_100BT4         0x0200L
#define PHY_ANAR_100BTXFULL     0x0100
#define PHY_ANAR_100BTXHALF     0x0080
#define PHY_ANAR_10BTFULL       0x0040
#define PHY_ANAR_10BTHALF       0x0020
#define PHY_ANAR_PROTO4         0x0010
#define PHY_ANAR_PROTO3         0x0008
#define PHY_ANAR_PROTO2         0x0004
#define PHY_ANAR_PROTO1         0x0002
#define PHY_ANAR_PROTO0         0x0001

#define PHY_1000SR_1000BTXFULL  0x0800
#define PHY_1000SR_1000BTXHALF  0x0400

/*
 * These are the register definitions for the PHY (physical layer
 * interface chip).
 */
/*
 * PHY BMCR Basic Mode Control Register
 */
#define PHY_BMCR_RESET                  0x8000
#define PHY_BMCR_LOOPBK                 0x4000
#define PHY_BMCR_SPEEDSEL               0x2000
#define PHY_BMCR_AUTONEGENBL            0x1000
#define PHY_BMCR_RSVD0                  0x0800  /* write as zero */
#define PHY_BMCR_ISOLATE                0x0400
#define PHY_BMCR_AUTONEGRSTR            0x0200
#define PHY_BMCR_DUPLEX                 0x0100
#define PHY_BMCR_COLLTEST               0x0080
#define PHY_BMCR_1000                   0x0040  /* only used for Marvell PHY */
#define PHY_BMCR_RSVD2                  0x0020  /* write as zero, don't care */
#define PHY_BMCR_RSVD3                  0x0010  /* write as zero, don't care */
#define PHY_BMCR_RSVD4                  0x0008  /* write as zero, don't care */
#define PHY_BMCR_RSVD5                  0x0004  /* write as zero, don't care */
#define PHY_BMCR_RSVD6                  0x0002  /* write as zero, don't care */
#define PHY_BMCR_RSVD7                  0x0001  /* write as zero, don't care */

/*
 * RESET: 1 == software reset, 0 == normal operation
 * Resets status and control registers to default values.
 * Relatches all hardware config values.
 *
 * LOOPBK: 1 == loopback operation enabled, 0 == normal operation
 *
 * SPEEDSEL: 1 == 100Mb/s, 0 == 10Mb/s
 * Link speed is selected byt his bit or if auto-negotiation if bit
 * 12 (AUTONEGENBL) is set (in which case the value of this register
 * is ignored).
 *
 * AUTONEGENBL: 1 == Autonegotiation enabled, 0 == Autonegotiation disabled
 * Bits 8 and 13 are ignored when autoneg is set, otherwise bits 8 and 13
 * determine speed and mode. Should be cleared and then set if PHY configured
 * for no autoneg on startup.
 *
 * ISOLATE: 1 == isolate PHY from MII, 0 == normal operation
 *
 * AUTONEGRSTR: 1 == restart autonegotiation, 0 = normal operation
 *
 * DUPLEX: 1 == full duplex mode, 0 == half duplex mode
 *
 * COLLTEST: 1 == collision test enabled, 0 == normal operation
 */

/*
 * PHY, BMSR Basic Mode Status Register
 */
#define PHY_BMSR_100BT4                 0x8000
#define PHY_BMSR_100BTXFULL             0x4000
#define PHY_BMSR_100BTXHALF             0x2000
#define PHY_BMSR_10BTFULL               0x1000
#define PHY_BMSR_10BTHALF               0x0800
#define PHY_BMSR_RSVD1                  0x0400  /* write as zero, don't care */
#define PHY_BMSR_RSVD2                  0x0200  /* write as zero, don't care */
#define PHY_BMSR_RSVD3                  0x0100  /* write as zero, don't care */
#define PHY_BMSR_RSVD4                  0x0080  /* write as zero, don't care */
#define PHY_BMSR_MFPRESUP               0x0040
#define PHY_BMSR_AUTONEGCOMP            0x0020
#define PHY_BMSR_REMFAULT               0x0010
#define PHY_BMSR_CANAUTONEG             0x0008
#define PHY_BMSR_LINKSTAT               0x0004
#define PHY_BMSR_JABBER                 0x0002
#define PHY_BMSR_EXTENDED               0x0001


