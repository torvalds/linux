/*	$OpenBSD: if_devar.h,v 1.42 2024/09/04 07:54:52 mglocker Exp $	*/
/*	$NetBSD: if_devar.h,v 1.13 1997/06/08 18:46:36 thorpej Exp $	*/

/*-
 * Copyright (c) 1994-1997 Matt Thomas (matt@3am-software.com)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
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
 *
 * Id: if_devar.h,v 1.23 1997/06/03 18:51:16 thomas Exp
 */

#define TULIP_CSR_READ(sc, csr) \
    bus_space_read_4((sc)->tulip_bustag, (sc)->tulip_bushandle, (sc)->tulip_csrs.csr)
#define TULIP_CSR_WRITE(sc, csr, val) \
    bus_space_write_4((sc)->tulip_bustag, (sc)->tulip_bushandle, (sc)->tulip_csrs.csr, (val))

#define TULIP_CSR_READBYTE(sc, csr) \
    bus_space_read_1((sc)->tulip_bustag, (sc)->tulip_bushandle, (sc)->tulip_csrs.csr)
#define TULIP_CSR_WRITEBYTE(sc, csr, val) \
    bus_space_write_1((sc)->tulip_bustag, (sc)->tulip_bushandle, (sc)->tulip_csrs.csr, (val))

#ifdef TULIP_IOMAPPED
#define	TULIP_PCI_CSRSIZE	8
#define	TULIP_PCI_CSROFFSET	0
#else /* TULIP_IOMAPPED */
#define	TULIP_PCI_CSRSIZE	8
#define	TULIP_PCI_CSROFFSET	0
#endif /* TULIP_IOMAPPED */

/*
 * This structure contains "pointers" for the registers on
 * the various 21x4x chips.  CSR0 through CSR8 are common
 * to all chips.  After that, it gets messy...
 */
typedef struct {
    bus_size_t csr_busmode;			/* CSR0 */
    bus_size_t csr_txpoll;			/* CSR1 */
    bus_size_t csr_rxpoll;			/* CSR2 */
    bus_size_t csr_rxlist;			/* CSR3 */
    bus_size_t csr_txlist;			/* CSR4 */
    bus_size_t csr_status;			/* CSR5 */
    bus_size_t csr_command;			/* CSR6 */
    bus_size_t csr_intr;			/* CSR7 */
    bus_size_t csr_missed_frames;		/* CSR8 */
    bus_size_t csr_9;			/* CSR9 */
    bus_size_t csr_10;			/* CSR10 */
    bus_size_t csr_11;			/* CSR11 */
    bus_size_t csr_12;			/* CSR12 */
    bus_size_t csr_13;			/* CSR13 */
    bus_size_t csr_14;			/* CSR14 */
    bus_size_t csr_15;			/* CSR15 */
} tulip_regfile_t;

#define	csr_enetrom		csr_9	/* 21040 */
#define	csr_reserved		csr_10	/* 21040 */
#define	csr_full_duplex		csr_11	/* 21040 */
#define	csr_bootrom		csr_10	/* 21041/21140A/?? */
#define	csr_gp			csr_12	/* 21140* */
#define	csr_watchdog		csr_15	/* 21140* */
#define	csr_gp_timer		csr_11	/* 21041/21140* */
#define	csr_srom_mii		csr_9	/* 21041/21140* */
#define	csr_sia_status		csr_12	/* 2104x */
#define csr_sia_connectivity	csr_13	/* 2104x */
#define csr_sia_tx_rx		csr_14	/* 2104x */
#define csr_sia_general		csr_15	/* 2104x */

/*
 * While 21x4x allows chaining of its descriptors, this driver
 * doesn't take advantage of it.  We keep the descriptors in a
 * traditional FIFO ring.
 */
typedef struct {
    tulip_desc_t *ri_first;	/* first entry in ring */
    tulip_desc_t *ri_last;	/* one after last entry */
    tulip_desc_t *ri_nextin;	/* next to processed by host */
    tulip_desc_t *ri_nextout;	/* next to processed by adapter */
    int ri_max;
    int ri_free;
} tulip_ringinfo_t;

/*
 * The 21040 has a stupid restriction in that the receive
 * buffers must be longword aligned.  But since Ethernet
 * headers are not a multiple of longwords in size this forces
 * the data to non-longword aligned.  Since IP requires the
 * data to be longword aligned, we need to copy it after it has
 * been DMA'ed in our memory.
 *
 * Since we have to copy it anyways, we might as well as allocate
 * dedicated receive space for the input.  This allows to use a
 * small receive buffer size and more ring entries to be able to
 * better keep with a flood of tiny Ethernet packets.
 *
 * The receive space MUST ALWAYS be a multiple of the page size.
 * And the number of receive descriptors multiplied by the size
 * of the receive buffers must equal the receive space.  This
 * is so that we can manipulate the page tables so that even if a
 * packet wraps around the end of the receive space, we can
 * treat it as virtually contiguous.
 *
 * The above used to be true (the stupid restriction is still true)
 * but we gone to directly DMA'ing into MBUFs (unless it's on an
 * architecture which can't handle unaligned accesses) because with
 * 100Mb/s cards the copying is just too much of a hit.
 */
#ifdef __STRICT_ALIGNMENT
#define	TULIP_COPY_RXDATA	1
#endif

#define	TULIP_DATA_PER_DESC	2032
#define	TULIP_TXTIMER		4
#define	TULIP_RXDESCS		48
#define	TULIP_TXDESCS		32
#define	TULIP_RXQ_TARGET	32
#if TULIP_RXQ_TARGET >= TULIP_RXDESCS
#error TULIP_RXQ_TARGET must be less than TULIP_RXDESCS
#endif
#define	TULIP_RX_BUFLEN		((MCLBYTES < 2048 ? MCLBYTES : 2048) - 16)

/*
 * Forward reference to make C happy.
 */
typedef struct _tulip_softc_t tulip_softc_t;

/*
 * The various controllers support.  Technically the DE425 is just
 * a 21040 on EISA.  But since it remarkably difference from normal
 * 21040s, we give it its own chip id.
 */

typedef enum {
    TULIP_21040, TULIP_DE425,
    TULIP_21041,
    TULIP_21140, TULIP_21140A, TULIP_21142,
    TULIP_21143,
    TULIP_CHIPID_UNKNOWN
} tulip_chipid_t;

/*
 * Various physical media types supported.
 * BNCAUI is BNC or AUI since on the 21040 you can't really tell
 * which is in use.
 */
typedef enum {
    TULIP_MEDIA_UNKNOWN,
    TULIP_MEDIA_10BASET,
    TULIP_MEDIA_10BASET_FD,
    TULIP_MEDIA_BNC,
    TULIP_MEDIA_AUI,
    TULIP_MEDIA_EXTSIA,
    TULIP_MEDIA_AUIBNC,
    TULIP_MEDIA_100BASETX,
    TULIP_MEDIA_100BASETX_FD,
    TULIP_MEDIA_100BASET4,
    TULIP_MEDIA_100BASEFX,
    TULIP_MEDIA_100BASEFX_FD,
    TULIP_MEDIA_MAX
} tulip_media_t;

#define	TULIP_BIT(b)		(1L << ((int)(b)))
#define	TULIP_FDBIT(m)		(1L << ((int)TULIP_MEDIA_ ## m ## _FD))
#define	TULIP_MBIT(m)		(1L << ((int)TULIP_MEDIA_ ## m ))
#define	TULIP_IS_MEDIA_FD(m)	(TULIP_BIT(m) & \
				 (TULIP_FDBIT(10BASET) \
				  |TULIP_FDBIT(100BASETX) \
				  |TULIP_FDBIT(100BASEFX)))
#define	TULIP_CAN_MEDIA_FD(m)	(TULIP_BIT(m) & \
				 (TULIP_MBIT(10BASET) \
				  |TULIP_MBIT(100BASETX) \
				  |TULIP_MBIT(100BASEFX)))
#define	TULIP_FD_MEDIA_OF(m)	((tulip_media_t)((m) + 1))
#define	TULIP_HD_MEDIA_OF(m)	((tulip_media_t)((m) - 1))
#define	TULIP_IS_MEDIA_100MB(m)	((m) >= TULIP_MEDIA_100BASETX)
#define	TULIP_IS_MEDIA_TP(m)	((TULIP_BIT(m) & \
				  (TULIP_MBIT(BNC) \
				   |TULIP_MBIT(AUI) \
				   |TULIP_MBIT(AUIBNC) \
				   |TULIP_MBIT(EXTSIA))) == 0)

#define	TULIP_SROM_ATTR_MII		0x0100
#define	TULIP_SROM_ATTR_NWAY		0x0200
#define	TULIP_SROM_ATTR_AUTOSENSE	0x0400
#define	TULIP_SROM_ATTR_POWERUP		0x0800
#define	TULIP_SROM_ATTR_NOLINKPASS	0x1000

typedef struct {
    enum {
	TULIP_MEDIAINFO_NONE,
	TULIP_MEDIAINFO_SIA,
	TULIP_MEDIAINFO_GPR,
	TULIP_MEDIAINFO_MII,
	TULIP_MEDIAINFO_RESET,
	TULIP_MEDIAINFO_SYM
    } mi_type;
    union {
	struct {
	    u_int16_t sia_connectivity;
	    u_int16_t sia_tx_rx;
	    u_int16_t sia_general;
	    u_int32_t sia_gp_control;	/* 21142/21143 */
	    u_int32_t sia_gp_data;	/* 21142/21143 */
	} un_sia;
	struct {
	    u_int32_t gpr_cmdmode;
	    u_int32_t gpr_gpcontrol;	/* 21142/21143 */
	    u_int32_t gpr_gpdata;
	    u_int8_t gpr_actmask;
	    u_int8_t gpr_actdata;
	    u_int8_t gpr_default : 1;
	} un_gpr;
	struct {
	    u_int32_t mii_mediamask;
	    u_int16_t mii_capabilities;
	    u_int16_t mii_advertisement;
	    u_int16_t mii_full_duplex;
	    u_int16_t mii_tx_threshold;
	    u_int16_t mii_interrupt;	/* 21142/21143 */
	    u_int8_t mii_phyaddr;
	    u_int8_t mii_gpr_length;
	    u_int8_t mii_gpr_offset;
	    u_int8_t mii_reset_length;
	    u_int8_t mii_reset_offset;
	    u_int32_t mii_phyid;
	} un_mii;
    } mi_un;
} tulip_media_info_t;

#define	mi_sia_connectivity	mi_un.un_sia.sia_connectivity
#define	mi_sia_tx_rx		mi_un.un_sia.sia_tx_rx
#define mi_sia_general		mi_un.un_sia.sia_general
#define	mi_sia_gp_control	mi_un.un_sia.sia_gp_control
#define	mi_sia_gp_data		mi_un.un_sia.sia_gp_data

#define	mi_gpcontrol		mi_un.un_gpr.gpr_gpcontrol
#define	mi_gpdata		mi_un.un_gpr.gpr_gpdata
#define	mi_actmask		mi_un.un_gpr.gpr_actmask
#define	mi_actdata		mi_un.un_gpr.gpr_actdata
#define	mi_default		mi_un.un_gpr.gpr_default
#define	mi_cmdmode		mi_un.un_gpr.gpr_cmdmode

#define	mi_phyaddr		mi_un.un_mii.mii_phyaddr
#define	mi_gpr_length		mi_un.un_mii.mii_gpr_length
#define	mi_gpr_offset		mi_un.un_mii.mii_gpr_offset
#define	mi_reset_length		mi_un.un_mii.mii_reset_length
#define	mi_reset_offset		mi_un.un_mii.mii_reset_offset
#define	mi_capabilities		mi_un.un_mii.mii_capabilities
#define	mi_advertisement	mi_un.un_mii.mii_advertisement
#define	mi_full_duplex		mi_un.un_mii.mii_full_duplex
#define	mi_tx_threshold		mi_un.un_mii.mii_tx_threshold
#define	mi_mediamask		mi_un.un_mii.mii_mediamask
#define	mi_mii_interrupt	mi_un.un_mii.mii_interrupt
#define	mi_phyid		mi_un.un_mii.mii_phyid

#define	TULIP_MEDIAINFO_SIA_INIT(sc, mi, chipid, media) do { \
    (mi)->mi_type = TULIP_MEDIAINFO_SIA; \
    sc->tulip_mediums[TULIP_MEDIA_ ## media] = (mi); \
    (mi)->mi_sia_connectivity = TULIP_ ## chipid ## _SIACONN_ ## media; \
    (mi)->mi_sia_tx_rx        = TULIP_ ## chipid ## _SIATXRX_ ## media; \
    (mi)->mi_sia_general      = TULIP_ ## chipid ## _SIAGEN_ ## media; \
} while (0)

#define TULIP_MEDIAINFO_ADD_CAPABILITY(sc, mi, media) do {	\
    if ((sc)->tulip_mediums[TULIP_MEDIA_ ## media] == NULL	\
	    && ((mi)->mi_capabilities & PHYSTS_ ## media)) {	\
	(sc)->tulip_mediums[TULIP_MEDIA_ ## media] = (mi);	\
	(mi)->mi_mediamask |= TULIP_BIT(TULIP_MEDIA_ ## media);	\
    } \
} while (0)

#define	TULIP_MII_NOPHY		32
/*
 * Some boards need to treated specially.  The following enumeration
 * identifies the cards with quirks (or those we just want to single
 * out for special merit or scorn).
 */
typedef enum {
    TULIP_21040_GENERIC,		/* Generic 21040 (works with most boards) */
    TULIP_21140_ISV,			/* Digital Semiconductor 21140 ISV SROM Format */
    TULIP_21142_ISV,			/* Digital Semiconductor 21142 ISV SROM Format */
    TULIP_21143_ISV,			/* Digital Semiconductor 21143 ISV SROM Format */
    TULIP_21140_DEC_EB,			/* Digital Semiconductor 21140 Evaluation Board */
    TULIP_21140_MII,			/* 21140[A] with MII */
    TULIP_21140_DEC_DE500,		/* Digital DE500-?? 10/100 */
    TULIP_21140_SMC_9332,		/* SMC 9332 */
    TULIP_21140_COGENT_EM100,		/* Cogent EM100 100 only */
    TULIP_21140_ZNYX_ZX34X,		/* ZNYX ZX342 10/100 */
    TULIP_21140_ASANTE,			/* AsanteFast 10/100 */
    TULIP_21140_EN1207,			/* Accton EN2107 10/100 BNC */
    TULIP_21041_GENERIC			/* Generic 21041 card */
} tulip_board_t;

typedef enum {
    TULIP_MEDIAPOLL_TIMER,		/* 100ms timer fired */
    TULIP_MEDIAPOLL_FASTTIMER,		/* <100ms timer fired */
    TULIP_MEDIAPOLL_LINKFAIL,		/* called from interrupt routine */
    TULIP_MEDIAPOLL_LINKPASS,		/* called from interrupt routine */
    TULIP_MEDIAPOLL_START,		/* start a media probe (called from reset) */
    TULIP_MEDIAPOLL_TXPROBE_OK,		/* txprobe succeeded */
    TULIP_MEDIAPOLL_TXPROBE_FAILED,	/* txprobe failed */
    TULIP_MEDIAPOLL_MAX
} tulip_mediapoll_event_t;

typedef enum {
    TULIP_LINK_DOWN,			/* Link is down */
    TULIP_LINK_UP,			/* link is ok */
    TULIP_LINK_UNKNOWN			/* we can't tell either way */
} tulip_link_status_t;

/*
 * This data structure is used to abstract out the quirks.
 * media_probe  = tries to determine the media type.
 * media_select = enables the current media (or autosenses)
 * media_poll	= autosenses media
 * media_preset = 21140, etal requires bit to set before the
 *		  the software reset; hence pre-set.  Should be
 *		  pre-reset but that's ugly.
 */

typedef struct {
    tulip_board_t bd_type;
    void (*bd_media_probe)(tulip_softc_t * const sc);
    void (*bd_media_select)(tulip_softc_t * const sc);
    void (*bd_media_poll)(tulip_softc_t * const sc, tulip_mediapoll_event_t event);
    void (*bd_media_preset)(tulip_softc_t * const sc);
} tulip_boardsw_t;

/*
 * The next few declarations are for MII/PHY based board.
 *
 *    The first enumeration identifies a superset of various datums
 * that can be obtained from various PHY chips.  Not all PHYs will
 * support all datums.
 *    The modedata structure indicates what register contains
 * a datum, what mask is applied the register contents, and what the
 * result should be.
 *    The attr structure records information about a supported PHY.
 *    The phy structure records information about a PHY instance.
 */

typedef enum {
    PHY_MODE_10T,
    PHY_MODE_100TX,
    PHY_MODE_100T4,
    PHY_MODE_FULLDUPLEX,
    PHY_MODE_MAX
} tulip_phy_mode_t;

typedef struct {
    u_int16_t pm_regno;
    u_int16_t pm_mask;
    u_int16_t pm_value;
} tulip_phy_modedata_t;

typedef struct {
    u_int32_t attr_id;
    u_int16_t attr_flags;
#define	PHY_NEED_HARD_RESET	0x0001
#define	PHY_DUAL_CYCLE_TA	0x0002
    tulip_phy_modedata_t attr_modes[PHY_MODE_MAX];
#ifdef TULIP_DEBUG
    const char *attr_name;
#endif
} tulip_phy_attr_t;

/*
 * Various probe states used when trying to autosense the media.
 */

typedef enum {
    TULIP_PROBE_INACTIVE,
    TULIP_PROBE_PHYRESET,
    TULIP_PROBE_PHYAUTONEG,
    TULIP_PROBE_GPRTEST,
    TULIP_PROBE_MEDIATEST,
    TULIP_PROBE_FAILED
} tulip_probe_state_t;

typedef struct {
    /*
     * Transmit Statistics
     */
    u_int32_t dot3StatsSingleCollisionFrames;
    u_int32_t dot3StatsMultipleCollisionFrames;
    u_int32_t dot3StatsSQETestErrors;
    u_int32_t dot3StatsDeferredTransmissions;
    u_int32_t dot3StatsLateCollisions;
    u_int32_t dot3StatsExcessiveCollisions;
    u_int32_t dot3StatsCarrierSenseErrors;
    u_int32_t dot3StatsInternalMacTransmitErrors;
    u_int32_t dot3StatsInternalTransmitUnderflows;	/* not in rfc1650! */
    u_int32_t dot3StatsInternalTransmitBabbles;		/* not in rfc1650! */
    /*
     * Receive Statistics
     */
    u_int32_t dot3StatsMissedFrames;	/* not in rfc1650! */
    u_int32_t dot3StatsAlignmentErrors;
    u_int32_t dot3StatsFCSErrors;
    u_int32_t dot3StatsFrameTooLongs;
    u_int32_t dot3StatsInternalMacReceiveErrors;
} tulip_dot3_stats_t;

/*
 * Now to important stuff.  This is softc structure (where does softc
 * come from??? No idea) for the tulip device.
 *
 */
struct _tulip_softc_t {
    struct device tulip_dev;		/* base device */
    void *tulip_ih;			/* interrupt vectoring */
    void *tulip_ats;			/* shutdown hook */

    bus_space_tag_t tulip_bustag;	/* tag of CSR region being used */
    bus_space_handle_t tulip_bushandle;	/* handle for CSR region being used */
    pci_chipset_tag_t tulip_pc;
    u_int8_t tulip_enaddr[ETHER_ADDR_LEN];
    struct ifmedia tulip_ifmedia;
    bus_dma_tag_t tulip_dmatag;		/* bus DMA tag */
    bus_dmamap_t tulip_setupmap;
    bus_dmamap_t tulip_txdescmap;
    bus_dmamap_t tulip_free_txmaps[TULIP_TXDESCS];
    unsigned tulip_num_free_txmaps;
    bus_dmamap_t tulip_rxdescmap;
    bus_dmamap_t tulip_free_rxmaps[TULIP_RXDESCS];
    unsigned tulip_num_free_rxmaps;
    struct arpcom tulip_ac;
    struct timeout tulip_stmo;
    tulip_regfile_t tulip_csrs;
    u_int32_t tulip_flags;
#define	TULIP_WANTSETUP		0x00000001
#define	TULIP_WANTHASHPERFECT	0x00000002
#define	TULIP_WANTHASHONLY	0x00000004
#define	TULIP_DOINGSETUP	0x00000008
#define	TULIP_PRINTMEDIA	0x00000010
#define	TULIP_TXPROBE_ACTIVE	0x00000020
#define	TULIP_ALLMULTI		0x00000040
#define	TULIP_WANTRXACT		0x00000080
#define	TULIP_RXACT		0x00000100
#define	TULIP_INRESET		0x00000200
#define	TULIP_NEEDRESET		0x00000400
#define	TULIP_SQETEST		0x00000800
#define	TULIP_FULLDUPLEX	0x00001000
#define	TULIP_xxxxxx1		0x00002000
#define	TULIP_WANTTXSTART	0x00004000
#define	TULIP_NEWTXTHRESH	0x00008000
#define	TULIP_NOAUTOSENSE	0x00010000
#define	TULIP_PRINTLINKUP	0x00020000
#define	TULIP_LINKUP		0x00040000
#define	TULIP_RXBUFSLOW		0x00080000
#define	TULIP_NOMESSAGES	0x00100000
#define	TULIP_SYSTEMERROR	0x00200000
#define	TULIP_TIMEOUTPENDING	0x00400000
#define	TULIP_xxxxxx2		0x00800000
#define	TULIP_TRYNWAY		0x01000000
#define	TULIP_DIDNWAY		0x02000000
#define	TULIP_RXIGNORE		0x04000000
#define	TULIP_PROBE1STPASS	0x08000000
#define	TULIP_DEVICEPROBE	0x10000000
#define	TULIP_PROMISC		0x20000000
#define	TULIP_HASHONLY		0x40000000
#define	TULIP_xxxxxx3		0x80000000
    /* only 4 bits left! */
    u_int32_t tulip_features;	/* static bits indicating features of chip */
#define	TULIP_HAVE_GPR		0x00000001	/* have gp register (140[A]) */
#define	TULIP_HAVE_RXBADOVRFLW	0x00000002	/* RX corrupts on overflow */
#define	TULIP_HAVE_POWERMGMT	0x00000004	/* Snooze/sleep modes */
#define	TULIP_HAVE_MII		0x00000008	/* Some medium on MII */
#define	TULIP_HAVE_SIANWAY	0x00000010	/* SIA does NWAY */
#define	TULIP_HAVE_DUALSENSE	0x00000020	/* SIA senses both AUI & TP */
#define	TULIP_HAVE_SIAGP	0x00000040	/* SIA has a GP port */
#define	TULIP_HAVE_BROKEN_HASH	0x00000080	/* Broken Multicast Hash */
#define	TULIP_HAVE_ISVSROM	0x00000100	/* uses ISV SROM Format */
#define	TULIP_HAVE_BASEROM	0x00000200	/* Board ROM can be cloned */
#define	TULIP_HAVE_SLAVEDROM	0x00000400	/* Board ROM cloned */
#define	TULIP_HAVE_SLAVEDINTR	0x00000800	/* Board slaved interrupt */
#define	TULIP_HAVE_SHAREDINTR	0x00001000	/* Board shares interrupts */
#define	TULIP_HAVE_OKROM	0x00002000	/* ROM was recognized */
#define	TULIP_HAVE_NOMEDIA	0x00004000	/* did not detect any media */
#define	TULIP_HAVE_STOREFWD	0x00008000	/* have CMD_STOREFWD */
#define	TULIP_HAVE_SIA100	0x00010000	/* has LS100 in SIA status */
#define	TULIP_HAVE_OKSROM	0x00020000	/* SROM CRC is OK */
    u_int32_t tulip_intrmask;	/* our copy of csr_intr */
    u_int32_t tulip_cmdmode;	/* our copy of csr_cmdmode */
    u_int32_t tulip_last_system_error : 3;	/* last system error (only value is
						   TULIP_SYSTEMERROR is also set) */
    u_int32_t tulip_txtimer;		/* transmission timer */
    u_int32_t tulip_system_errors;	/* number of system errors encountered */
    u_int32_t tulip_statusbits;	/* status bits from CSR5 that may need to be printed */

    tulip_media_info_t *tulip_mediums[TULIP_MEDIA_MAX];	/* indexes into mediainfo */
    tulip_media_t tulip_media;			/* current media type */
    u_int32_t tulip_abilities;	/* remote system's abilities (as defined in IEEE 802.3u) */

    u_int8_t tulip_revinfo;			/* revision of chip */
    u_int8_t tulip_phyaddr;			/* 0..31 -- address of current phy */
    u_int8_t tulip_gpinit;			/* active pins on 21140 */
    u_int8_t tulip_gpdata;			/* default gpdata for 21140 */

    struct {
	u_int8_t probe_count;			/* count of probe operations */
	int32_t probe_timeout;			/* time in ms of probe timeout */
	tulip_probe_state_t probe_state;	/* current media probe state */
	tulip_media_t probe_media;		/* current media being probed */
	u_int32_t probe_mediamask;		/* medias checked */
	u_int32_t probe_passes;			/* times autosense failed */
	u_int32_t probe_txprobes;		/* txprobes attempted */
    } tulip_probe;
#define	tulip_probe_count	tulip_probe.probe_count
#define	tulip_probe_timeout	tulip_probe.probe_timeout
#define	tulip_probe_state	tulip_probe.probe_state
#define	tulip_probe_media	tulip_probe.probe_media
#define	tulip_probe_mediamask	tulip_probe.probe_mediamask
#define	tulip_probe_passes	tulip_probe.probe_passes

    tulip_chipid_t tulip_chipid;		/* type of chip we are using */
    const tulip_boardsw_t *tulip_boardsw;	/* board/chip characteristics */
    tulip_softc_t *tulip_slaves;		/* slaved devices (ZX3xx) */
#if defined(TULIP_DEBUG)
    /*
     * Debugging/Statistical information
     */
    struct {
	tulip_media_t dbg_last_media;
	u_int32_t dbg_intrs;
	u_int32_t dbg_media_probes;
	u_int32_t dbg_txprobe_nocarr;
	u_int32_t dbg_txprobe_exccoll;
	u_int32_t dbg_link_downed;
	u_int32_t dbg_link_suspected;
	u_int32_t dbg_link_intrs;
	u_int32_t dbg_link_pollintrs;
	u_int32_t dbg_link_failures;
	u_int32_t dbg_nway_starts;
	u_int32_t dbg_nway_failures;
	u_int16_t dbg_phyregs[32][4];
	u_int32_t dbg_rxlowbufs;
	u_int32_t dbg_rxintrs;
	u_int32_t dbg_last_rxintrs;
	u_int32_t dbg_high_rxintrs_hz;
	u_int32_t dbg_no_txmaps;
	u_int32_t dbg_txput_finishes[8];
	u_int32_t dbg_txprobes_ok[TULIP_MEDIA_MAX];
	u_int32_t dbg_txprobes_failed[TULIP_MEDIA_MAX];
	u_int32_t dbg_events[TULIP_MEDIAPOLL_MAX];
	u_int32_t dbg_rxpktsperintr[TULIP_RXDESCS];
    } tulip_dbg;
#endif
#if defined(TULIP_PERFSTATS)
#define	TULIP_PERF_CURRENT	0
#define	TULIP_PERF_PREVIOUS	1
#define	TULIP_PERF_TOTAL	2
#define	TULIP_PERF_MAX		3
    struct tulip_perfstats {
	uint64_t perf_intr_cycles;
	uint64_t perf_ifstart_cycles;
	uint64_t perf_ifioctl_cycles;
	uint64_t perf_ifwatchdog_cycles;
	uint64_t perf_timeout_cycles;
	uint64_t perf_txput_cycles;
	uint64_t perf_txintr_cycles;
	uint64_t perf_rxintr_cycles;
	uint64_t perf_rxget_cycles;
	unsigned perf_intr;
	unsigned perf_ifstart;
	unsigned perf_ifioctl;
	unsigned perf_ifwatchdog;
	unsigned perf_timeout;
	unsigned perf_txput;
	unsigned perf_txintr;
	unsigned perf_rxintr;
	unsigned perf_rxget;
    } tulip_perfstats[TULIP_PERF_MAX];
#define	tulip_curperfstats		tulip_perfstats[TULIP_PERF_CURRENT]
#endif
    struct mbuf_list tulip_txq;
    struct mbuf_list tulip_rxq;
    tulip_dot3_stats_t tulip_dot3stats;
    tulip_ringinfo_t tulip_rxinfo;
    tulip_ringinfo_t tulip_txinfo;
    tulip_media_info_t tulip_mediainfo[10];
    /*
     * The setup buffers for sending the setup frame to the chip.
     * one is the one being sent while the other is the one being
     * filled.
     */
#define TULIP_SETUP	192
    tulip_desc_t *tulip_setupbuf;
    u_int32_t tulip_setupdata[TULIP_SETUP / sizeof(u_int32_t)];

    char tulip_boardid[16];		/* buffer for board ID */
    u_int8_t tulip_rombuf[128];
    struct device *tulip_pci_busno;	/* needed for multiport boards */
    u_int8_t tulip_pci_devno;		/* needed for multiport boards */
    u_int8_t tulip_connidx;
    tulip_srom_connection_t tulip_conntype;
    tulip_desc_t *tulip_rxdescs;
    tulip_desc_t *tulip_txdescs;
};

#define	TULIP_DO_AUTOSENSE(sc)	(IFM_SUBTYPE((sc)->tulip_ifmedia.ifm_media) == IFM_AUTO)

static const char * const tulip_chipdescs[] = {
    "21040",
    NULL,
    "21041",
    "21140",
    "21140A",
    "21142",
    "21143",
    "82C168",
};

#ifdef TULIP_DEBUG
static const char * const tulip_mediums[] = {
    "unknown",			/* TULIP_MEDIA_UNKNOWN */
    "10baseT",			/* TULIP_MEDIA_10BASET */
    "Full Duplex 10baseT",	/* TULIP_MEDIA_10BASET_FD */
    "BNC",			/* TULIP_MEDIA_BNC */
    "AUI",			/* TULIP_MEDIA_AUI */
    "External SIA",		/* TULIP_MEDIA_EXTSIA */
    "AUI/BNC",			/* TULIP_MEDIA_AUIBNC */
    "100baseTX",		/* TULIP_MEDIA_100BASET */
    "Full Duplex 100baseTX",	/* TULIP_MEDIA_100BASET_FD */
    "100baseT4",		/* TULIP_MEDIA_100BASET4 */
    "100baseFX",		/* TULIP_MEDIA_100BASEFX */
    "Full Duplex 100baseFX",	/* TULIP_MEDIA_100BASEFX_FD */
};
#endif

static const uint64_t tulip_media_to_ifmedia[] = {
    IFM_ETHER | IFM_NONE,		/* TULIP_MEDIA_UNKNOWN */
    IFM_ETHER | IFM_10_T,		/* TULIP_MEDIA_10BASET */
    IFM_ETHER | IFM_10_T | IFM_FDX,	/* TULIP_MEDIA_10BASET_FD */
    IFM_ETHER | IFM_10_2,		/* TULIP_MEDIA_BNC */
    IFM_ETHER | IFM_10_5,		/* TULIP_MEDIA_AUI */
    IFM_ETHER | IFM_MANUAL,		/* TULIP_MEDIA_EXTSIA */
    IFM_ETHER | IFM_10_5,		/* TULIP_MEDIA_AUIBNC */
    IFM_ETHER | IFM_100_TX,		/* TULIP_MEDIA_100BASET */
    IFM_ETHER | IFM_100_TX | IFM_FDX,	/* TULIP_MEDIA_100BASET_FD */
    IFM_ETHER | IFM_100_T4,		/* TULIP_MEDIA_100BASET4 */
    IFM_ETHER | IFM_100_FX,		/* TULIP_MEDIA_100BASEFX */
    IFM_ETHER | IFM_100_FX | IFM_FDX,	/* TULIP_MEDIA_100BASEFX_FD */
};

#ifdef TULIP_DEBUG
static const char * const tulip_system_errors[] = {
    "parity error",
    "master abort",
    "target abort",
    "reserved #3",
    "reserved #4",
    "reserved #5",
    "reserved #6",
    "reserved #7",
};

static const char * const tulip_status_bits[] = {
    NULL,
    "transmit process stopped",
    NULL,
    "transmit jabber timeout",

    NULL,
    "transmit underflow",
    NULL,
    "receive underflow",

    "receive process stopped",
    "receive watchdog timeout",
    NULL,
    NULL,

    "link failure",
    NULL,
    NULL,
};
#endif

static const struct {
    tulip_srom_connection_t sc_type;
    tulip_media_t sc_media;
    u_int32_t sc_attrs;
} tulip_srom_conninfo[] = {
    { TULIP_SROM_CONNTYPE_10BASET,		TULIP_MEDIA_10BASET },
    { TULIP_SROM_CONNTYPE_BNC,			TULIP_MEDIA_BNC },
    { TULIP_SROM_CONNTYPE_AUI,			TULIP_MEDIA_AUI },
    { TULIP_SROM_CONNTYPE_100BASETX,		TULIP_MEDIA_100BASETX },
    { TULIP_SROM_CONNTYPE_100BASET4,		TULIP_MEDIA_100BASET4 },
    { TULIP_SROM_CONNTYPE_100BASEFX,		TULIP_MEDIA_100BASEFX },
    { TULIP_SROM_CONNTYPE_MII_10BASET,		TULIP_MEDIA_10BASET,
		TULIP_SROM_ATTR_MII },
    { TULIP_SROM_CONNTYPE_MII_100BASETX,	TULIP_MEDIA_100BASETX,
		TULIP_SROM_ATTR_MII },
    { TULIP_SROM_CONNTYPE_MII_100BASET4,	TULIP_MEDIA_100BASET4,
		TULIP_SROM_ATTR_MII },
    { TULIP_SROM_CONNTYPE_MII_100BASEFX,	TULIP_MEDIA_100BASEFX,
		TULIP_SROM_ATTR_MII },
    { TULIP_SROM_CONNTYPE_10BASET_NWAY,		TULIP_MEDIA_10BASET,
		TULIP_SROM_ATTR_NWAY },
    { TULIP_SROM_CONNTYPE_10BASET_FD,		TULIP_MEDIA_10BASET_FD },
    { TULIP_SROM_CONNTYPE_MII_10BASET_FD,	TULIP_MEDIA_10BASET_FD,
		TULIP_SROM_ATTR_MII },
    { TULIP_SROM_CONNTYPE_100BASETX_FD,		TULIP_MEDIA_100BASETX_FD },
    { TULIP_SROM_CONNTYPE_MII_100BASETX_FD,	TULIP_MEDIA_100BASETX_FD,
		TULIP_SROM_ATTR_MII },
    { TULIP_SROM_CONNTYPE_10BASET_NOLINKPASS,	TULIP_MEDIA_10BASET,
		TULIP_SROM_ATTR_NOLINKPASS },
    { TULIP_SROM_CONNTYPE_AUTOSENSE,		TULIP_MEDIA_UNKNOWN,
		TULIP_SROM_ATTR_AUTOSENSE },
    { TULIP_SROM_CONNTYPE_AUTOSENSE_POWERUP,	TULIP_MEDIA_UNKNOWN,
		TULIP_SROM_ATTR_AUTOSENSE|TULIP_SROM_ATTR_POWERUP },
    { TULIP_SROM_CONNTYPE_AUTOSENSE_NWAY,	TULIP_MEDIA_UNKNOWN,
		TULIP_SROM_ATTR_AUTOSENSE|TULIP_SROM_ATTR_NWAY },
    { TULIP_SROM_CONNTYPE_NOT_USED,		TULIP_MEDIA_UNKNOWN }
};
#define	TULIP_SROM_LASTCONNIDX (nitems(tulip_srom_conninfo) - 1)

static const struct {
    tulip_media_t sm_type;
    tulip_srom_media_t sm_srom_type;
} tulip_srom_mediums[] = {
    { 	TULIP_MEDIA_100BASEFX_FD,	TULIP_SROM_MEDIA_100BASEFX_FD	},
    {	TULIP_MEDIA_100BASEFX,		TULIP_SROM_MEDIA_100BASEFX	},
    {	TULIP_MEDIA_100BASET4,		TULIP_SROM_MEDIA_100BASET4	},
    {	TULIP_MEDIA_100BASETX_FD,	TULIP_SROM_MEDIA_100BASETX_FD	},
    {	TULIP_MEDIA_100BASETX,		TULIP_SROM_MEDIA_100BASETX	},
    {	TULIP_MEDIA_10BASET_FD,		TULIP_SROM_MEDIA_10BASET_FD	},
    {	TULIP_MEDIA_AUI,		TULIP_SROM_MEDIA_AUI		},
    {	TULIP_MEDIA_BNC,		TULIP_SROM_MEDIA_BNC		},
    {	TULIP_MEDIA_10BASET,		TULIP_SROM_MEDIA_10BASET	},
    {	TULIP_MEDIA_UNKNOWN						}
};

/*
 * This driver supports a maximum of 32 tulip boards.
 * This should be enough for the foreseeable future.
 */
#define	TULIP_MAX_DEVICES	32

#define TULIP_RXDESC_PRESYNC(sc, di, s)	\
	bus_dmamap_sync((sc)->tulip_dmatag, (sc)->tulip_rxdescmap, \
		   (caddr_t) di - (caddr_t) (sc)->tulip_rxdescs, \
		   (s), BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE)
#define TULIP_RXDESC_POSTSYNC(sc, di, s)	\
	bus_dmamap_sync((sc)->tulip_dmatag, (sc)->tulip_rxdescmap, \
		   (caddr_t) di - (caddr_t) (sc)->tulip_rxdescs, \
		   (s), BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE)
#define	TULIP_RXMAP_PRESYNC(sc, map) \
	bus_dmamap_sync((sc)->tulip_dmatag, (map), 0, (map)->dm_mapsize, \
			BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE)
#define	TULIP_RXMAP_POSTSYNC(sc, map) \
	bus_dmamap_sync((sc)->tulip_dmatag, (map), 0, (map)->dm_mapsize, \
			BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE)
#define	TULIP_RXMAP_CREATE(sc, mapp) \
	bus_dmamap_create((sc)->tulip_dmatag, TULIP_RX_BUFLEN, 2, \
			  TULIP_DATA_PER_DESC, 0, \
			  BUS_DMA_NOWAIT|BUS_DMA_ALLOCNOW, (mapp))

#define TULIP_TXDESC_PRESYNC(sc, di, s)	\
	bus_dmamap_sync((sc)->tulip_dmatag, (sc)->tulip_txdescmap, \
			(caddr_t) di - (caddr_t) (sc)->tulip_txdescs, \
			(s), BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE)
#define TULIP_TXDESC_POSTSYNC(sc, di, s)	\
	bus_dmamap_sync((sc)->tulip_dmatag, (sc)->tulip_txdescmap, \
			(caddr_t) di - (caddr_t) (sc)->tulip_txdescs, \
			(s), BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE)
#define	TULIP_TXMAP_PRESYNC(sc, map) \
	bus_dmamap_sync((sc)->tulip_dmatag, (map), 0, (map)->dm_mapsize, \
			BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE)
#define	TULIP_TXMAP_POSTSYNC(sc, map) \
	bus_dmamap_sync((sc)->tulip_dmatag, (map), 0, (map)->dm_mapsize, \
			BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE)
#define	TULIP_TXMAP_CREATE(sc, mapp) \
	bus_dmamap_create((sc)->tulip_dmatag, TULIP_DATA_PER_DESC, \
			  TULIP_MAX_TXSEG, TULIP_DATA_PER_DESC, \
			  0, BUS_DMA_NOWAIT, (mapp))

extern struct cfdriver de_cd;
#define	TULIP_UNIT_TO_SOFTC(unit)	((tulip_softc_t *) de_cd.cd_devs[unit])
#define TULIP_IFP_TO_SOFTC(ifp)         ((tulip_softc_t *)((ifp)->if_softc))
#define	tulip_unit			tulip_dev.dv_unit
#define tulip_xname                     tulip_dev.dv_cfdata->cf_driver->cd_name

#define	TULIP_PRINTF_FMT		"%s%d"
#define	TULIP_PRINTF_ARGS		sc->tulip_xname, sc->tulip_unit

#define	TULIP_BURSTSIZE(unit)		3

#define	tulip_if	tulip_ac.ac_if
#define	tulip_name	tulip_if.if_name
#define	tulip_enaddr	tulip_ac.ac_enaddr
#define	tulip_multicnt	tulip_ac.ac_multicnt

#define	tulip_bpf	tulip_if.if_bpf

#define	tulip_intrfunc_t	int

#if defined(TULIP_PERFSTATS)
#define	TULIP_PERFMERGE(sc, member) \
	do { (sc)->tulip_perfstats[TULIP_PERF_TOTAL].member \
	     += (sc)->tulip_perfstats[TULIP_PERF_CURRENT].member; \
	 (sc)->tulip_perfstats[TULIP_PERF_PREVIOUS].member \
	      = (sc)->tulip_perfstats[TULIP_PERF_CURRENT].member; \
	    (sc)->tulip_perfstats[TULIP_PERF_CURRENT].member = 0; } while (0)
#define	TULIP_PERFSTART(name) const tulip_cycle_t perfstart_ ## name = TULIP_PERFREAD();
#define	TULIP_PERFEND(name)	do { \
	    (sc)->tulip_curperfstats.perf_ ## name ## _cycles += TULIP_PERFDIFF(perfstart_ ## name, TULIP_PERFREAD()); \
	    (sc)->tulip_curperfstats.perf_ ## name++; \
	} while (0)
#if defined(__i386__)
typedef uint64_t tulip_cycle_t;
static __inline__ tulip_cycle_t
TULIP_PERFREAD(
    void)
{
    tulip_cycle_t x;
    __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
    return x;
}
#define	TULIP_PERFDIFF(s, f)	((f) - (s))
#elif defined(__alpha__)
typedef unsigned long tulip_cycle_t;
static __inline__ tulip_cycle_t
TULIP_PERFREAD(
    void)
{
    tulip_cycle_t x;
    __asm__ volatile ("rpcc %0" : "=r" (x));
    return x;
}
#define	TULIP_PERFDIFF(s, f)	((unsigned int) ((f) - (s)))
#endif
#else
#define	TULIP_PERFSTART(name)	
#define	TULIP_PERFEND(name)	do { } while (0)
#define	TULIP_PERFMERGE(s,n)	do { } while (0)
#endif /* TULIP_PERFSTATS */

#define	TULIP_MAX_TXSEG		30

#define	TULIP_ADDREQUAL(a1, a2) \
	(((u_int16_t *)a1)[0] == ((u_int16_t *)a2)[0] \
	 && ((u_int16_t *)a1)[1] == ((u_int16_t *)a2)[1] \
	 && ((u_int16_t *)a1)[2] == ((u_int16_t *)a2)[2])
#define	TULIP_ADDRBRDCST(a1) \
	(((u_int16_t *)a1)[0] == 0xFFFFU \
	 && ((u_int16_t *)a1)[1] == 0xFFFFU \
	 && ((u_int16_t *)a1)[2] == 0xFFFFU)

#define TULIP_GETCTX(m, t)	((t) (m)->m_pkthdr.ph_cookie + 0)
#define TULIP_SETCTX(m, c)	((void) ((m)->m_pkthdr.ph_cookie = (void *)(c)))
