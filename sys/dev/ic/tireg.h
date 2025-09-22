/*	$OpenBSD: tireg.h,v 1.6 2022/01/09 05:42:42 jsg Exp $	*/

/*
 * Copyright (c) 1997, 1998, 1999
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
 * $FreeBSD: src/sys/pci/if_tireg.h,v 1.12 2000/01/18 00:26:29 wpaul Exp $
 */

/*
 * Tigon register offsets. These are memory mapped registers
 * which can be accessed with the CSR_READ_4()/CSR_WRITE_4() macros.
 * Each register must be accessed using 32 bit operations.
 *
 * All registers are accessed through a 16K shared memory block.
 * The first group of registers are actually copies of the PCI
 * configuration space registers.
 */

#define TI_PCI_ID			PCI_ID_REG /* PCI device/vendor ID */
#define TI_PCI_CMDSTAT			PCI_COMMAND_STATUS_REG
#define TI_PCI_CLASSCODE		PCI_CLASS_REG
#define TI_PCI_BIST			PCI_BHLC_REG
#define TI_PCI_LOMEM			PCI_MAPS /* Shared memory base address */
#define TI_PCI_SUBSYS			PCI_SUBVEND_0
#define TI_PCI_ROMBASE			0x030
#define TI_PCI_INT			PCI_INTLINE

/*
 * Tigon configuration and control registers.
 */
#define TI_MISC_HOST_CTL		0x040
#define TI_MISC_LOCAL_CTL		0x044
#define TI_SEM_AB			0x048 /* Tigon 2 only */
#define TI_MISC_CONF			0x050 /* Tigon 2 only */
#define TI_TIMER_BITS			0x054
#define TI_TIMERREF			0x058
#define TI_PCI_STATE			0x05C
#define TI_MAIN_EVENT_A			0x060
#define TI_MAILBOX_EVENT_A		0x064
#define TI_WINBASE			0x068
#define TI_WINDATA			0x06C
#define TI_MAIN_EVENT_B			0x070 /* Tigon 2 only */
#define TI_MAILBOX_EVENT_B		0x074 /* Tigon 2 only */
#define TI_TIMERREF_B			0x078 /* Tigon 2 only */
#define TI_SERIAL			0x07C

/*
 * Misc host control bits.
 */
#define TI_MHC_INTSTATE			0x00000001
#define TI_MHC_CLEARINT			0x00000002
#define TI_MHC_RESET			0x00000008
#define TI_MHC_BYTE_SWAP_ENB		0x00000010
#define TI_MHC_WORD_SWAP_ENB		0x00000020
#define TI_MHC_MASK_INTS		0x00000040
#define TI_MHC_CHIP_REV_MASK		0xF0000000

#define TI_MHC_BIGENDIAN_INIT	\
	(TI_MHC_BYTE_SWAP_ENB|TI_MHC_WORD_SWAP_ENB|TI_MHC_CLEARINT)

#define TI_MHC_LITTLEENDIAN_INIT	\
	(TI_MHC_WORD_SWAP_ENB|TI_MHC_CLEARINT)

/*
 * Tigon chip rev values. Rev 4 is the Tigon 1. Rev 6 is the Tigon 2.
 * Rev 5 is also the Tigon 2, but is a broken version which was never
 * used in any actual hardware, so we ignore it.
 */
#define TI_REV_TIGON_I			0x40000000
#define TI_REV_TIGON_II			0x60000000

/*
 * Firmware revision that we want.
 */
#define TI_FIRMWARE_MAJOR		0xc
#define TI_FIRMWARE_MINOR		0x4
#define TI_FIRMWARE_FIX			0xd

/*
 * Miscellaneous Local Control registers.
 */
#define TI_MLC_EE_WRITE_ENB		0x00000010
#define TI_MLC_SRAM_BANK_SIZE		0x00000300 /* Tigon 2 only */
#define TI_MLC_LOCALADDR_21		0x00004000
#define TI_MLC_LOCALADDR_22		0x00008000
#define TI_MLC_SBUS_WRITEERR		0x00080000
#define TI_MLC_EE_CLK			0x00100000
#define TI_MLC_EE_TXEN			0x00200000
#define TI_MLC_EE_DOUT			0x00400000
#define TI_MLC_EE_DIN			0x00800000

/* Possible memory sizes. */
#define TI_MLC_SRAM_BANK_DISA		0x00000000
#define TI_MLC_SRAM_BANK_1024K		0x00000100
#define TI_MLC_SRAM_BANK_512K		0x00000200
#define TI_MLC_SRAM_BANK_256K		0x00000300

/*
 * Offset of MAC address inside EEPROM.
 */
#define TI_EE_MAC_OFFSET		0x8c

#define TI_DMA_ASSIST			0x11C
#define TI_CPU_STATE			0x140
#define TI_CPU_PROGRAM_COUNTER		0x144
#define TI_SRAM_ADDR			0x154
#define TI_SRAM_DATA			0x158
#define TI_GEN_0			0x180
#define TI_GEN_X			0x1FC
#define TI_MAC_TX_STATE			0x200
#define TI_MAC_RX_STATE			0x220
#define TI_CPU_CTL_B			0x240 /* Tigon 2 only */
#define TI_CPU_PROGRAM_COUNTER_B	0x244 /* Tigon 2 only */
#define TI_SRAM_ADDR_B			0x254 /* Tigon 2 only */
#define TI_SRAM_DATA_B			0x258 /* Tigon 2 only */
#define TI_GEN_B_0			0x280 /* Tigon 2 only */
#define TI_GEN_B_X			0x2FC /* Tigon 2 only */

/*
 * Misc config register.
 */
#define TI_MCR_SRAM_SYNCHRONOUS		0x00100000 /* Tigon 2 only */

/*
 * PCI state register.
 */
#define TI_PCISTATE_FORCE_RESET		0x00000001
#define TI_PCISTATE_PROVIDE_LEN		0x00000002
#define TI_PCISTATE_READ_MAXDMA		0x0000001C
#define TI_PCISTATE_WRITE_MAXDMA	0x000000E0
#define TI_PCISTATE_MINDMA		0x0000FF00
#define TI_PCISTATE_FIFO_RETRY_ENB	0x00010000
#define TI_PCISTATE_USE_MEM_RD_MULT	0x00020000
#define TI_PCISTATE_NO_SWAP_READ_DMA	0x00040000
#define TI_PCISTATE_NO_SWAP_WRITE_DMA	0x00080000
#define TI_PCISTATE_66MHZ_BUS		0x00080000 /* Tigon 2 only */
#define TI_PCISTATE_32BIT_BUS		0x00100000 /* Tigon 2 only */
#define TI_PCISTATE_ENB_BYTE_ENABLES	0x00800000 /* Tigon 2 only */
#define TI_PCISTATE_READ_CMD		0x0F000000
#define TI_PCISTATE_WRITE_CMD		0xF0000000

#define TI_PCI_READMAX_4		0x04
#define TI_PCI_READMAX_16		0x08
#define TI_PCI_READMAX_32		0x0C
#define TI_PCI_READMAX_64		0x10
#define TI_PCI_READMAX_128		0x14
#define TI_PCI_READMAX_256		0x18
#define TI_PCI_READMAX_1024		0x1C

#define TI_PCI_WRITEMAX_4		0x20
#define TI_PCI_WRITEMAX_16		0x40
#define TI_PCI_WRITEMAX_32		0x60
#define TI_PCI_WRITEMAX_64		0x80
#define TI_PCI_WRITEMAX_128		0xA0
#define TI_PCI_WRITEMAX_256		0xC0
#define TI_PCI_WRITEMAX_1024		0xE0

#define TI_PCI_READ_CMD			0x06000000
#define TI_PCI_WRITE_CMD		0x70000000

/*
 * DMA state register.
 */
#define TI_DMASTATE_ENABLE		0x00000001
#define TI_DMASTATE_PAUSE		0x00000002

/*
 * CPU state register.
 */
#define TI_CPUSTATE_RESET		0x00000001
#define TI_CPUSTATE_STEP		0x00000002
#define TI_CPUSTATE_ROMFAIL		0x00000010
#define TI_CPUSTATE_HALT		0x00010000
/*
 * MAC TX state register
 */
#define TI_TXSTATE_RESET		0x00000001
#define TI_TXSTATE_ENB			0x00000002
#define TI_TXSTATE_STOP			0x00000004

/*
 * MAC RX state register
 */
#define TI_RXSTATE_RESET		0x00000001
#define TI_RXSTATE_ENB			0x00000002
#define TI_RXSTATE_STOP			0x00000004

/*
 * Tigon 2 mailbox registers. The mailbox area consists of 256 bytes
 * split into 64 bit registers. Only the lower 32 bits of each mailbox
 * are used.
 */
#define TI_MB_HOSTINTR_HI		0x500
#define TI_MB_HOSTINTR_LO		0x504
#define TI_MB_HOSTINTR			TI_MB_HOSTINTR_LO
#define TI_MB_CMDPROD_IDX_HI		0x508
#define TI_MB_CMDPROD_IDX_LO		0x50C
#define TI_MB_CMDPROD_IDX		TI_MB_CMDPROD_IDX_LO
#define TI_MB_SENDPROD_IDX_HI		0x510
#define TI_MB_SENDPROD_IDX_LO		0x514
#define TI_MB_SENDPROD_IDX		TI_MB_SENDPROD_IDX_LO
#define TI_MB_STDRXPROD_IDX_HI		0x518 /* Tigon 2 only */
#define TI_MB_STDRXPROD_IDX_LO		0x51C /* Tigon 2 only */
#define TI_MB_STDRXPROD_IDX		TI_MB_STDRXPROD_IDX_LO
#define TI_MB_JUMBORXPROD_IDX_HI	0x520 /* Tigon 2 only */
#define TI_MB_JUMBORXPROD_IDX_LO	0x524 /* Tigon 2 only */
#define TI_MB_JUMBORXPROD_IDX		TI_MB_JUMBORXPROD_IDX_LO
#define TI_MB_MINIRXPROD_IDX_HI		0x528 /* Tigon 2 only */
#define TI_MB_MINIRXPROD_IDX_LO		0x52C /* Tigon 2 only */
#define TI_MB_MINIRXPROD_IDX		TI_MB_MINIRXPROD_IDX_LO
#define TI_MB_RSVD			0x530

/*
 * Tigon 2 general communication registers. These are 64 and 32 bit
 * registers which are only valid after the firmware has been
 * loaded and started. They actually exist in NIC memory but are
 * mapped into the host memory via the shared memory region.
 *
 * The NIC internally maps these registers starting at address 0,
 * so to determine the NIC address of any of these registers, we
 * subtract 0x600 (the address of the first register).
 */

#define TI_GCR_BASE			0x600
#define TI_GCR_MACADDR			0x600
#define TI_GCR_PAR0			0x600
#define TI_GCR_PAR1			0x604
#define TI_GCR_GENINFO_HI		0x608
#define TI_GCR_GENINFO_LO		0x60C
#define TI_GCR_MCASTADDR		0x610 /* obsolete */
#define TI_GCR_MAR0			0x610 /* obsolete */
#define TI_GCR_MAR1			0x614 /* obsolete */
#define TI_GCR_OPMODE			0x618
#define TI_GCR_DMA_READCFG		0x61C
#define TI_GCR_DMA_WRITECFG		0x620
#define TI_GCR_TX_BUFFER_RATIO		0x624
#define TI_GCR_EVENTCONS_IDX		0x628
#define TI_GCR_CMDCONS_IDX		0x62C
#define TI_GCR_TUNEPARMS		0x630
#define TI_GCR_RX_COAL_TICKS		0x630
#define TI_GCR_TX_COAL_TICKS		0x634
#define TI_GCR_STAT_TICKS		0x638
#define TI_GCR_TX_MAX_COAL_BD		0x63C
#define TI_GCR_RX_MAX_COAL_BD		0x640
#define TI_GCR_NIC_TRACING		0x644
#define TI_GCR_GLINK			0x648
#define TI_GCR_LINK			0x64C
#define TI_GCR_NICTRACE_PTR		0x650
#define TI_GCR_NICTRACE_START		0x654
#define TI_GCR_NICTRACE_LEN		0x658
#define TI_GCR_IFINDEX			0x65C
#define TI_GCR_IFMTU			0x660
#define TI_GCR_MASK_INTRS		0x664
#define TI_GCR_GLINK_STAT		0x668
#define TI_GCR_LINK_STAT		0x66C
#define TI_GCR_RXRETURNCONS_IDX		0x680
#define TI_GCR_CMDRING			0x700

#define TI_GCR_NIC_ADDR(x)		(x - TI_GCR_BASE)

/*
 * Local memory window. The local memory window is a 2K shared
 * memory region which can be used to access the NIC's internal
 * SRAM. The window can be mapped to a given 2K region using
 * the TI_WINDOW_BASE register.
 */
#define TI_WINDOW			0x800
#define TI_WINLEN			0x800

#define TI_TICKS_PER_SEC		1000000

/*
 * Operation mode register.
 */
#define TI_OPMODE_BYTESWAP_BD		0x00000002
#define TI_OPMODE_WORDSWAP_BD		0x00000004
#define TI_OPMODE_WARN_ENB		0x00000008 /* not yet implemented */
#define TI_OPMODE_BYTESWAP_DATA		0x00000010
#define TI_OPMODE_1_DMA_ACTIVE		0x00000040
#define TI_OPMODE_SBUS			0x00000100
#define TI_OPMODE_DONT_FRAG_JUMBO	0x00000200
#define TI_OPMODE_INCLUDE_CRC		0x00000400
#define TI_OPMODE_RX_BADFRAMES		0x00000800
#define TI_OPMODE_NO_EVENT_INTRS	0x00001000
#define TI_OPMODE_NO_TX_INTRS		0x00002000
#define TI_OPMODE_NO_RX_INTRS		0x00004000
#define TI_OPMODE_FATAL_ENB		0x40000000 /* not yet implemented */

#if BYTE_ORDER == BIG_ENDIAN
#define TI_DMA_SWAP_OPTIONS \
	TI_OPMODE_BYTESWAP_DATA| \
	TI_OPMODE_BYTESWAP_BD|TI_OPMODE_WORDSWAP_BD
#else
#define TI_DMA_SWAP_OPTIONS \
	TI_OPMODE_BYTESWAP_DATA
#endif

/*
 * DMA configuration thresholds.
 */
#define TI_DMA_STATE_THRESH_16W		0x00000100
#define TI_DMA_STATE_THRESH_8W		0x00000080
#define TI_DMA_STATE_THRESH_4W		0x00000040
#define TI_DMA_STATE_THRESH_2W		0x00000020
#define TI_DMA_STATE_THRESH_1W		0x00000010

#define TI_DMA_STATE_FORCE_32_BIT	0x00000008

/*
 * Gigabit link status bits.
 */
#define TI_GLNK_SENSE_NO_BEG		0x00002000
#define TI_GLNK_LOOPBACK		0x00004000
#define TI_GLNK_PREF			0x00008000
#define TI_GLNK_1000MB			0x00040000
#define TI_GLNK_FULL_DUPLEX		0x00080000
#define TI_GLNK_TX_FLOWCTL_Y		0x00200000 /* Tigon 2 only */
#define TI_GLNK_RX_FLOWCTL_Y		0x00800000
#define TI_GLNK_AUTONEGENB		0x20000000
#define TI_GLNK_ENB			0x40000000

/*
 * Link status bits.
 */
#define TI_LNK_LOOPBACK			0x00004000
#define TI_LNK_PREF			0x00008000
#define TI_LNK_10MB			0x00010000
#define TI_LNK_100MB			0x00020000
#define TI_LNK_1000MB			0x00040000
#define TI_LNK_FULL_DUPLEX		0x00080000
#define TI_LNK_HALF_DUPLEX		0x00100000
#define TI_LNK_TX_FLOWCTL_Y		0x00200000 /* Tigon 2 only */
#define TI_LNK_RX_FLOWCTL_Y		0x00800000
#define TI_LNK_AUTONEGENB		0x20000000
#define TI_LNK_ENB			0x40000000

/*
 * Ring size constants.
 */
#define TI_EVENT_RING_CNT	256
#define TI_CMD_RING_CNT		64
#define TI_STD_RX_RING_CNT	512
#define TI_JUMBO_RX_RING_CNT	256
#define TI_MINI_RX_RING_CNT	1024
#define TI_RETURN_RING_CNT	2048

/*
 * Possible TX ring sizes.
 */
#define TI_TX_RING_CNT_128	128
#define TI_TX_RING_BASE_128	0x3800

#define TI_TX_RING_CNT_256	256
#define TI_TX_RING_BASE_256	0x3000

#define TI_TX_RING_CNT_512	512
#define TI_TX_RING_BASE_512	0x2000

#define TI_TX_RING_CNT		TI_TX_RING_CNT_512
#define TI_TX_RING_BASE		TI_TX_RING_BASE_512

/*
 * The Tigon can have up to 8MB of external SRAM, however the Tigon 1
 * is limited to 2MB total, and in general I think most adapters have
 * around 1MB. We use this value for zeroing the NIC's SRAM, so to
 * be safe we use the largest possible value (zeroing memory that
 * isn't there doesn't hurt anything).
 */
#define TI_MEM_MAX		0x7FFFFF

/*
 * Even on the alpha, pci addresses are 32-bit quantities
 */

typedef struct {
	u_int32_t		ti_addr_hi;
	u_int32_t		ti_addr_lo;
} ti_hostaddr;
#define TI_HOSTADDR(x)	x.ti_addr_lo

/*
 * Ring control block structure. The rules for the max_len field
 * are as follows:
 * 
 * For the send ring, max_len indicates the number of entries in the
 * ring (128, 256 or 512).
 *
 * For the standard receive ring, max_len indicates the threshold
 * used to decide when a frame should be put in the jumbo receive ring
 * instead of the standard one.
 *
 * For the mini ring, max_len indicates the size of the buffers in the
 * ring. This is the value used to decide when a frame is small enough
 * to be placed in the mini ring.
 *
 * For the return receive ring, max_len indicates the number of entries
 * in the ring. It can be one of 2048, 1024 or 0 (which is the same as
 * 2048 for backwards compatibility). The value 1024 can only be used
 * if the mini ring is disabled.
 */
struct ti_rcb {
	ti_hostaddr		ti_hostaddr;
#if BYTE_ORDER == BIG_ENDIAN
	u_int16_t		ti_max_len;
	u_int16_t		ti_flags;
#else
	u_int16_t		ti_flags;
	u_int16_t		ti_max_len;
#endif
	u_int32_t		ti_unused;
};

#define TI_RCB_FLAG_TCP_UDP_CKSUM	0x00000001
#define TI_RCB_FLAG_IP_CKSUM		0x00000002
#define TI_RCB_FLAG_NO_PHDR_CKSUM	0x00000008
#define TI_RCB_FLAG_VLAN_ASSIST		0x00000010
#define TI_RCB_FLAG_COAL_UPD_ONLY	0x00000020
#define TI_RCB_FLAG_HOST_RING		0x00000040
#define TI_RCB_FLAG_IEEE_SNAP_CKSUM	0x00000080
#define TI_RCB_FLAG_USE_EXT_RX_BD	0x00000100
#define TI_RCB_FLAG_RING_DISABLED	0x00000200

struct ti_producer {
	u_int32_t		ti_idx;
	u_int32_t		ti_unused;
};

/*
 * Tigon statistics counters.
 */
struct ti_stats {
	/*
	 * MAC stats, taken from RFC 1643, ethernet-like MIB
	 */
	volatile u_int32_t dot3StatsAlignmentErrors;		/* 0 */
	volatile u_int32_t dot3StatsFCSErrors;			/* 1 */
	volatile u_int32_t dot3StatsSingleCollisionFrames;	/* 2 */
	volatile u_int32_t dot3StatsMultipleCollisionFrames;	/* 3 */
	volatile u_int32_t dot3StatsSQETestErrors;		/* 4 */
	volatile u_int32_t dot3StatsDeferredTransmissions;	/* 5 */
	volatile u_int32_t dot3StatsLateCollisions;		/* 6 */
	volatile u_int32_t dot3StatsExcessiveCollisions;	/* 7 */
	volatile u_int32_t dot3StatsInternalMacTransmitErrors;	/* 8 */
	volatile u_int32_t dot3StatsCarrierSenseErrors;		/* 9 */
	volatile u_int32_t dot3StatsFrameTooLongs;		/* 10 */
	volatile u_int32_t dot3StatsInternalMacReceiveErrors;	/* 11 */
	/*
	 * interface stats, taken from RFC 1213, MIB-II, interfaces group
	 */
	volatile u_int32_t ifIndex;				/* 12 */
	volatile u_int32_t ifType;				/* 13 */
	volatile u_int32_t ifMtu;				/* 14 */
	volatile u_int32_t ifSpeed;				/* 15 */
	volatile u_int32_t ifAdminStatus;			/* 16 */
#define IF_ADMIN_STATUS_UP      1
#define IF_ADMIN_STATUS_DOWN    2
#define IF_ADMIN_STATUS_TESTING 3
	volatile u_int32_t ifOperStatus;			/* 17 */
#define IF_OPER_STATUS_UP       1
#define IF_OPER_STATUS_DOWN     2
#define IF_OPER_STATUS_TESTING  3
#define IF_OPER_STATUS_UNKNOWN  4
#define IF_OPER_STATUS_DORMANT  5
	volatile u_int32_t ifLastChange;			/* 18 */
	volatile u_int32_t ifInDiscards;			/* 19 */
	volatile u_int32_t ifInErrors;				/* 20 */
	volatile u_int32_t ifInUnknownProtos;			/* 21 */
	volatile u_int32_t ifOutDiscards;			/* 22 */
	volatile u_int32_t ifOutErrors;				/* 23 */
	volatile u_int32_t ifOutQLen;     /* deprecated */	/* 24 */
	volatile u_int8_t  ifPhysAddress[8]; /* 8 bytes */	/* 25 - 26 */
	volatile u_int8_t  ifDescr[32];				/* 27 - 34 */
	u_int32_t alignIt;      /* align to 64 bit for u_int64_ts following */
	/*
	 * more interface stats, taken from RFC 1573, MIB-IIupdate,
	 * interfaces group
	 */
	volatile u_int64_t ifHCInOctets;			/* 36 - 37 */
	volatile u_int64_t ifHCInUcastPkts;			/* 38 - 39 */
	volatile u_int64_t ifHCInMulticastPkts;			/* 40 - 41 */
	volatile u_int64_t ifHCInBroadcastPkts;			/* 42 - 43 */
	volatile u_int64_t ifHCOutOctets;			/* 44 - 45 */
	volatile u_int64_t ifHCOutUcastPkts;			/* 46 - 47 */
	volatile u_int64_t ifHCOutMulticastPkts;		/* 48 - 49 */
	volatile u_int64_t ifHCOutBroadcastPkts;		/* 50 - 51 */
	volatile u_int32_t ifLinkUpDownTrapEnable;		/* 52 */
	volatile u_int32_t ifHighSpeed;				/* 53 */
	volatile u_int32_t ifPromiscuousMode; 			/* 54 */
	volatile u_int32_t ifConnectorPresent; /* follow link state 55 */
	/*
	 * Host Commands
	 */
	volatile u_int32_t nicCmdsHostState;			/* 56 */
	volatile u_int32_t nicCmdsFDRFiltering;			/* 57 */
	volatile u_int32_t nicCmdsSetRecvProdIndex;		/* 58 */
	volatile u_int32_t nicCmdsUpdateGencommStats;		/* 59 */
	volatile u_int32_t nicCmdsResetJumboRing;		/* 60 */
	volatile u_int32_t nicCmdsAddMCastAddr;			/* 61 */
	volatile u_int32_t nicCmdsDelMCastAddr;			/* 62 */
	volatile u_int32_t nicCmdsSetPromiscMode;		/* 63 */
	volatile u_int32_t nicCmdsLinkNegotiate;		/* 64 */
	volatile u_int32_t nicCmdsSetMACAddr;			/* 65 */
	volatile u_int32_t nicCmdsClearProfile;			/* 66 */
	volatile u_int32_t nicCmdsSetMulticastMode;		/* 67 */
	volatile u_int32_t nicCmdsClearStats;			/* 68 */
	volatile u_int32_t nicCmdsSetRecvJumboProdIndex;	/* 69 */
	volatile u_int32_t nicCmdsSetRecvMiniProdIndex;		/* 70 */
	volatile u_int32_t nicCmdsRefreshStats;			/* 71 */
	volatile u_int32_t nicCmdsUnknown;			/* 72 */
	/*
	 * NIC Events
	 */
	volatile u_int32_t nicEventsNICFirmwareOperational;	/* 73 */
	volatile u_int32_t nicEventsStatsUpdated;		/* 74 */
	volatile u_int32_t nicEventsLinkStateChanged;		/* 75 */
	volatile u_int32_t nicEventsError;			/* 76 */
	volatile u_int32_t nicEventsMCastListUpdated;		/* 77 */
	volatile u_int32_t nicEventsResetJumboRing;		/* 78 */
	/*
	 * Ring manipulation
	 */
	volatile u_int32_t nicRingSetSendProdIndex;		/* 79 */
	volatile u_int32_t nicRingSetSendConsIndex;		/* 80 */
	volatile u_int32_t nicRingSetRecvReturnProdIndex;	/* 81 */
	/*
	 * Interrupts
	 */
	volatile u_int32_t nicInterrupts;			/* 82 */
	volatile u_int32_t nicAvoidedInterrupts;		/* 83 */
	/*
	 * BD Coalescing Thresholds
	 */
	volatile u_int32_t nicEventThresholdHit;		/* 84 */
	volatile u_int32_t nicSendThresholdHit;			/* 85 */
	volatile u_int32_t nicRecvThresholdHit;			/* 86 */
	/*
	 * DMA Attentions
	 */
	volatile u_int32_t nicDmaRdOverrun;			/* 87 */
	volatile u_int32_t nicDmaRdUnderrun;			/* 88 */
	volatile u_int32_t nicDmaWrOverrun;			/* 89 */
	volatile u_int32_t nicDmaWrUnderrun;			/* 90 */
	volatile u_int32_t nicDmaWrMasterAborts;		/* 91 */
	volatile u_int32_t nicDmaRdMasterAborts;		/* 92 */
	/*
	 * NIC Resources
	 */
	volatile u_int32_t nicDmaWriteRingFull;			/* 93 */
	volatile u_int32_t nicDmaReadRingFull;			/* 94 */
	volatile u_int32_t nicEventRingFull;			/* 95 */
	volatile u_int32_t nicEventProducerRingFull;		/* 96 */
	volatile u_int32_t nicTxMacDescrRingFull;		/* 97 */
	volatile u_int32_t nicOutOfTxBufSpaceFrameRetry;	/* 98 */
	volatile u_int32_t nicNoMoreWrDMADescriptors;		/* 99 */
	volatile u_int32_t nicNoMoreRxBDs;			/* 100 */
	volatile u_int32_t nicNoSpaceInReturnRing;		/* 101 */
	volatile u_int32_t nicSendBDs;            /* current count 102 */
	volatile u_int32_t nicRecvBDs;            /* current count 103 */
	volatile u_int32_t nicJumboRecvBDs;       /* current count 104 */
	volatile u_int32_t nicMiniRecvBDs;        /* current count 105 */
	volatile u_int32_t nicTotalRecvBDs;       /* current count 106 */
	volatile u_int32_t nicTotalSendBDs;       /* current count 107 */
	volatile u_int32_t nicJumboSpillOver;			/* 108 */
	volatile u_int32_t nicSbusHangCleared;			/* 109 */
	volatile u_int32_t nicEnqEventDelayed;			/* 110 */
	/*
	 * Stats from MAC rx completion
	 */
	volatile u_int32_t nicMacRxLateColls;			/* 111 */
	volatile u_int32_t nicMacRxLinkLostDuringPkt;		/* 112 */
	volatile u_int32_t nicMacRxPhyDecodeErr;		/* 113 */
	volatile u_int32_t nicMacRxMacAbort;			/* 114 */
	volatile u_int32_t nicMacRxTruncNoResources;		/* 115 */
	/*
	 * Stats from the mac_stats area
	 */
	volatile u_int32_t nicMacRxDropUla;			/* 116 */
	volatile u_int32_t nicMacRxDropMcast;			/* 117 */
	volatile u_int32_t nicMacRxFlowControl;			/* 118 */
	volatile u_int32_t nicMacRxDropSpace;			/* 119 */
	volatile u_int32_t nicMacRxColls;			/* 120 */
	/*
 	 * MAC RX Attentions
	 */
	volatile u_int32_t nicMacRxTotalAttns;			/* 121 */
	volatile u_int32_t nicMacRxLinkAttns;			/* 122 */
	volatile u_int32_t nicMacRxSyncAttns;			/* 123 */
	volatile u_int32_t nicMacRxConfigAttns;			/* 124 */
	volatile u_int32_t nicMacReset;				/* 125 */
	volatile u_int32_t nicMacRxBufDescrAttns;		/* 126 */
	volatile u_int32_t nicMacRxBufAttns;			/* 127 */
	volatile u_int32_t nicMacRxZeroFrameCleanup;		/* 128 */
	volatile u_int32_t nicMacRxOneFrameCleanup;		/* 129 */
	volatile u_int32_t nicMacRxMultipleFrameCleanup;	/* 130 */
	volatile u_int32_t nicMacRxTimerCleanup;		/* 131 */
	volatile u_int32_t nicMacRxDmaCleanup;			/* 132 */
	/*
	 * Stats from the mac_stats area
	 */
	volatile u_int32_t nicMacTxCollisionHistogram[15];	/* 133 */
	/*
	 * MAC TX Attentions
	 */
	volatile u_int32_t nicMacTxTotalAttns;			/* 134 */
	/*
	 * NIC Profile
	 */
	volatile u_int32_t nicProfile[32];			/* 135 */
	/*
	 * Pat to 1024 bytes.
	 */
	u_int32_t		pad[75];
};
/*
 * Tigon general information block. This resides in host memory
 * and contains the status counters, ring control blocks and
 * producer pointers.
 */

struct ti_gib {
	struct ti_stats		ti_stats;
	struct ti_rcb		ti_ev_rcb;
	struct ti_rcb		ti_cmd_rcb;
	struct ti_rcb		ti_tx_rcb;
	struct ti_rcb		ti_std_rx_rcb;
	struct ti_rcb		ti_jumbo_rx_rcb;
	struct ti_rcb		ti_mini_rx_rcb;
	struct ti_rcb		ti_return_rcb;
	ti_hostaddr		ti_ev_prodidx_ptr;
	ti_hostaddr		ti_return_prodidx_ptr;
	ti_hostaddr		ti_tx_considx_ptr;
	ti_hostaddr		ti_refresh_stats_ptr;
};

/*
 * Buffer descriptor structures. There are basically three types
 * of structures: normal receive descriptors, extended receive
 * descriptors and transmit descriptors. The extended receive
 * descriptors are optionally used only for the jumbo receive ring.
 */

struct ti_rx_desc {
	ti_hostaddr		ti_addr;
#if BYTE_ORDER == BIG_ENDIAN
	u_int16_t		ti_idx;
	u_int16_t		ti_len;
#else
	u_int16_t		ti_len;
	u_int16_t		ti_idx;
#endif
#if BYTE_ORDER == BIG_ENDIAN
	u_int16_t		ti_type;
	u_int16_t		ti_flags;
#else
	u_int16_t		ti_flags;
	u_int16_t		ti_type;
#endif
#if BYTE_ORDER == BIG_ENDIAN
	u_int16_t		ti_ip_cksum;
	u_int16_t		ti_tcp_udp_cksum;
#else
	u_int16_t		ti_tcp_udp_cksum;
	u_int16_t		ti_ip_cksum;
#endif
#if BYTE_ORDER == BIG_ENDIAN
	u_int16_t		ti_error_flags;
	u_int16_t		ti_vlan_tag;
#else
	u_int16_t		ti_vlan_tag;
	u_int16_t		ti_error_flags;
#endif
	u_int32_t		ti_rsvd;
	u_int32_t		ti_opaque;
};

struct ti_rx_desc_ext {
	ti_hostaddr		ti_addr1;
	ti_hostaddr		ti_addr2;
	ti_hostaddr		ti_addr3;
#if BYTE_ORDER == BIG_ENDIAN
	u_int16_t		ti_len1;
	u_int16_t		ti_len2;
#else
	u_int16_t		ti_len2;
	u_int16_t		ti_len1;
#endif
#if BYTE_ORDER == BIG_ENDIAN
	u_int16_t		ti_len3;
	u_int16_t		ti_rsvd0;
#else
	u_int16_t		ti_rsvd0;
	u_int16_t		ti_len3;
#endif
	ti_hostaddr		ti_addr0;
#if BYTE_ORDER == BIG_ENDIAN
	u_int16_t		ti_idx;
	u_int16_t		ti_len0;
#else
	u_int16_t		ti_len0;
	u_int16_t		ti_idx;
#endif
#if BYTE_ORDER == BIG_ENDIAN
	u_int16_t		ti_type;
	u_int16_t		ti_flags;
#else
	u_int16_t		ti_flags;
	u_int16_t		ti_type;
#endif
#if BYTE_ORDER == BIG_ENDIAN
	u_int16_t		ti_ip_cksum;
	u_int16_t		ti_tcp_udp_cksum;
#else
	u_int16_t		ti_tcp_udp_cksum;
	u_int16_t		ti_ip_cksum;
#endif
#if BYTE_ORDER == BIG_ENDIAN
	u_int16_t		ti_error_flags;
	u_int16_t		ti_vlan_tag;
#else
	u_int16_t		ti_vlan_tag;
	u_int16_t		ti_error_flags;
#endif
	u_int32_t		ti_rsvd1;
	u_int32_t		ti_opaque;
};

/*
 * Transmit descriptors are, mercifully, very small.
 */
struct ti_tx_desc {
	ti_hostaddr		ti_addr;
#if BYTE_ORDER == BIG_ENDIAN
	u_int16_t		ti_len;
	u_int16_t		ti_flags;
#else
	u_int16_t		ti_flags;
	u_int16_t		ti_len;
#endif
#if BYTE_ORDER == BIG_ENDIAN
	u_int16_t		ti_rsvd;
	u_int16_t		ti_vlan_tag;
#else
	u_int16_t		ti_vlan_tag;
	u_int16_t		ti_rsvd;
#endif
};

/*
 * NOTE!  On the Alpha, we have an alignment constraint.
 * The first thing in the packet is a 14-byte Ethernet header.
 * This means that the packet is misaligned.  To compensate,
 * we actually offset the data 2 bytes into the cluster.  This
 * aligns the packet after the Ethernet header at a 32-bit
 * boundary.
 */

#define TI_JUMBO_FRAMELEN	9018
#define TI_JUMBO_MTU		(TI_JUMBO_FRAMELEN - ETHER_HDR_LEN - ETHER_CRC_LEN)
#define TI_PAGE_SIZE		PAGE_SIZE

/*
 * Buffer descriptor error flags.
 */
#define TI_BDERR_CRC			0x0001
#define TI_BDERR_COLLDETECT		0x0002
#define TI_BDERR_LINKLOST		0x0004
#define TI_BDERR_DECODE			0x0008
#define TI_BDERR_ODD_NIBBLES		0x0010
#define TI_BDERR_MAC_ABRT		0x0020
#define TI_BDERR_RUNT			0x0040
#define TI_BDERR_TRUNC			0x0080
#define TI_BDERR_GIANT			0x0100

/*
 * Buffer descriptor flags.
 */
#define TI_BDFLAG_TCP_UDP_CKSUM		0x0001
#define TI_BDFLAG_IP_CKSUM		0x0002
#define TI_BDFLAG_END			0x0004
#define TI_BDFLAG_MORE			0x0008
#define TI_BDFLAG_JUMBO_RING		0x0010
#define TI_BDFLAG_UCAST_PKT		0x0020
#define TI_BDFLAG_MCAST_PKT		0x0040
#define TI_BDFLAG_BCAST_PKT		0x0060
#define TI_BDFLAG_IP_FRAG		0x0080
#define TI_BDFLAG_IP_FRAG_END		0x0100
#define TI_BDFLAG_VLAN_TAG		0x0200
#define TI_BDFLAG_ERROR			0x0400
#define TI_BDFLAG_COAL_NOW		0x0800
#define	TI_BDFLAG_MINI_RING		0x1000

/*
 * Descriptor type flags. I think these only have meaning for
 * the Tigon 1. I had to extract them from the sample driver source
 * since they aren't in the manual.
 */
#define TI_BDTYPE_TYPE_NULL			0x0000
#define TI_BDTYPE_SEND_BD			0x0001
#define TI_BDTYPE_RECV_BD			0x0002
#define TI_BDTYPE_RECV_JUMBO_BD			0x0003
#define TI_BDTYPE_RECV_BD_LAST			0x0004
#define TI_BDTYPE_SEND_DATA			0x0005
#define TI_BDTYPE_SEND_DATA_LAST		0x0006
#define TI_BDTYPE_RECV_DATA			0x0007
#define TI_BDTYPE_RECV_DATA_LAST		0x000b
#define TI_BDTYPE_EVENT_RUPT			0x000c
#define TI_BDTYPE_EVENT_NO_RUPT			0x000d
#define TI_BDTYPE_ODD_START			0x000e
#define TI_BDTYPE_UPDATE_STATS			0x000f
#define TI_BDTYPE_SEND_DUMMY_DMA		0x0010
#define TI_BDTYPE_EVENT_PROD			0x0011
#define TI_BDTYPE_TX_CONS			0x0012
#define TI_BDTYPE_RX_PROD			0x0013
#define TI_BDTYPE_REFRESH_STATS			0x0014
#define TI_BDTYPE_SEND_DATA_LAST_VLAN		0x0015
#define TI_BDTYPE_SEND_DATA_COAL		0x0016
#define TI_BDTYPE_SEND_DATA_LAST_COAL		0x0017
#define TI_BDTYPE_SEND_DATA_LAST_VLAN_COAL	0x0018
#define TI_BDTYPE_TX_CONS_NO_INTR		0x0019

/*
 * Tigon command structure.
 */
struct ti_cmd_desc {
	u_int32_t		ti_cmdx;
};

#define TI_CMD_CMD(cmd) (((((cmd)->ti_cmdx)) >> 24) & 0xff)
#define TI_CMD_CODE(cmd) (((((cmd)->ti_cmdx)) >> 12) & 0xfff)
#define TI_CMD_IDX(cmd) ((((cmd)->ti_cmdx)) & 0xfff)

#define TI_CMD_HOST_STATE		0x01
#define TI_CMD_CODE_STACK_UP		0x01
#define TI_CMD_CODE_STACK_DOWN		0x02

/*
 * This command enables software address filtering. It's a workaround
 * for a bug in the Tigon 1 and not implemented for the Tigon 2.
 */
#define TI_CMD_FDR_FILTERING		0x02
#define TI_CMD_CODE_FILT_ENB		0x01
#define TI_CMD_CODE_FILT_DIS		0x02

#define TI_CMD_SET_RX_PROD_IDX		0x03 /* obsolete */
#define TI_CMD_UPDATE_GENCOM		0x04
#define TI_CMD_RESET_JUMBO_RING		0x05
#define TI_CMD_SET_PARTIAL_RX_CNT	0x06
#define TI_CMD_ADD_MCAST_ADDR		0x08 /* obsolete */
#define TI_CMD_DEL_MCAST_ADDR		0x09 /* obsolete */

#define TI_CMD_SET_PROMISC_MODE		0x0A
#define TI_CMD_CODE_PROMISC_ENB		0x01
#define TI_CMD_CODE_PROMISC_DIS		0x02

#define TI_CMD_LINK_NEGOTIATION		0x0B
#define TI_CMD_CODE_NEGOTIATE_BOTH	0x00
#define TI_CMD_CODE_NEGOTIATE_GIGABIT	0x01
#define TI_CMD_CODE_NEGOTIATE_10_100	0x02

#define TI_CMD_SET_MAC_ADDR		0x0C
#define TI_CMD_CLR_PROFILE		0x0D

#define TI_CMD_SET_ALLMULTI		0x0E
#define TI_CMD_CODE_ALLMULTI_ENB	0x01
#define TI_CMD_CODE_ALLMULTI_DIS	0x02

#define TI_CMD_CLR_STATS		0x0F
#define TI_CMD_SET_RX_JUMBO_PROD_IDX	0x10 /* obsolete */
#define TI_CMD_RFRSH_STATS		0x11

#define TI_CMD_EXT_ADD_MCAST		0x12
#define TI_CMD_EXT_DEL_MCAST		0x13

/*
 * Utility macros to make issuing commands a little simpler. Assumes
 * that 'sc' and 'cmd' are in local scope.
 */
#define TI_DO_CMD(x, y, z)					\
do {								\
	cmd.ti_cmdx = (((x) << 24) | ((y) << 12) | ((z)));	\
	ti_cmd(sc, &cmd);					\
} while (0)

#define TI_DO_CMD_EXT(x, y, z, v, w)				\
do {								\
	cmd.ti_cmdx = (((x) << 24) | ((y) << 12) | ((z)));	\
	ti_cmd_ext(sc, &cmd, v, w);				\
} while (0)

/*
 * Other utility macros.
 */
#define TI_INC(x, y)						\
do {								\
	(x) = (x + 1) % y;					\
} while (0)

#define TI_UPDATE_JUMBOPROD(x, y)				\
do {								\
	if (x->ti_hwrev == TI_HWREV_TIGON) {			\
		TI_DO_CMD(TI_CMD_SET_RX_JUMBO_PROD_IDX, 0, y);	\
	} else {						\
		CSR_WRITE_4(x, TI_MB_JUMBORXPROD_IDX, y);	\
	}							\
} while (0)

#define TI_UPDATE_MINIPROD(x, y)				\
do {								\
		CSR_WRITE_4(x, TI_MB_MINIRXPROD_IDX, y);	\
} while (0)

#define TI_UPDATE_STDPROD(x, y)					\
do {								\
	if (x->ti_hwrev == TI_HWREV_TIGON) {			\
		TI_DO_CMD(TI_CMD_SET_RX_PROD_IDX, 0, y);	\
	} else {						\
		CSR_WRITE_4(x, TI_MB_STDRXPROD_IDX, y);		\
	}							\
} while (0)


/*
 * Tigon event structure.
 */
struct ti_event_desc {
	u_int32_t		ti_eventx;
	u_int32_t		ti_rsvd;
};

#define TI_EVENT_EVENT(e) (((((e)->ti_eventx)) >> 24) & 0xff)
#define TI_EVENT_CODE(e) (((((e)->ti_eventx)) >> 12) & 0xfff)
#define TI_EVENT_IDX(e) (((((e)->ti_eventx))) & 0xfff)

/*
 * Tigon events.
 */
#define TI_EV_FIRMWARE_UP		0x01
#define TI_EV_STATS_UPDATED		0x04

#define TI_EV_LINKSTAT_CHANGED		0x06
#define TI_EV_CODE_GIG_LINK_UP		0x01
#define TI_EV_CODE_LINK_DOWN		0x02
#define TI_EV_CODE_LINK_UP		0x03

#define TI_EV_ERROR			0x07
#define TI_EV_CODE_ERR_INVAL_CMD	0x01
#define TI_EV_CODE_ERR_UNIMP_CMD	0x02
#define TI_EV_CODE_ERR_BADCFG		0x03

#define TI_EV_MCAST_UPDATED		0x08
#define TI_EV_CODE_MCAST_ADD		0x01
#define TI_EV_CODE_MCAST_DEL		0x02

#define TI_EV_RESET_JUMBO_RING		0x09
/*
 * Register access macros. The Tigon always uses memory mapped register
 * accesses and all registers must be accessed with 32 bit operations.
 */

#define CSR_WRITE_4(sc, reg, val)	\
	bus_space_write_4(sc->ti_btag, sc->ti_bhandle, (reg), (val))

#define CSR_READ_4(sc, reg)		\
	bus_space_read_4(sc->ti_btag, sc->ti_bhandle, (reg))

#define TI_SETBIT(sc, reg, x)	\
	CSR_WRITE_4(sc, (reg), (CSR_READ_4(sc, (reg)) | (x)))
#define TI_CLRBIT(sc, reg, x)	\
	CSR_WRITE_4(sc, (reg), (CSR_READ_4(sc, (reg)) & ~(x)))

/*
 * Memory management stuff. Note: the SSLOTS, MSLOTS and JSLOTS
 * values are tuneable. They control the actual amount of buffers
 * allocated for the standard, mini and jumbo receive rings.
 */

#define TI_SSLOTS	256
#define TI_MSLOTS	256
#define TI_JSLOTS	384

#define TI_JRAWLEN	(TI_JUMBO_FRAMELEN + ETHER_ALIGN)
#define TI_JLEN		(TI_JRAWLEN + (sizeof(u_int64_t) - \
       (TI_JRAWLEN % sizeof(u_int64_t))))
#define TI_JPAGESZ	PAGE_SIZE
#define TI_RESID	(TI_JPAGESZ - (TI_JLEN * TI_JSLOTS) % TI_JPAGESZ)
#define TI_JMEM		((TI_JLEN * TI_JSLOTS) + TI_RESID)

struct ti_jslot {
	caddr_t			ti_buf;
	int			ti_inuse;
};

/*
 * Ring structures. Most of these reside in host memory and we tell
 * the NIC where they are via the ring control blocks. The exceptions
 * are the tx and command rings, which live in NIC memory and which
 * we access via the shared memory window.
 */
struct ti_ring_data {
	struct ti_rx_desc	ti_rx_std_ring[TI_STD_RX_RING_CNT];
	struct ti_rx_desc	ti_rx_jumbo_ring[TI_JUMBO_RX_RING_CNT];
	struct ti_rx_desc	ti_rx_mini_ring[TI_MINI_RX_RING_CNT];
	struct ti_rx_desc	ti_rx_return_ring[TI_RETURN_RING_CNT];
	struct ti_event_desc	ti_event_ring[TI_EVENT_RING_CNT];
	struct ti_tx_desc	ti_tx_ring[TI_TX_RING_CNT];

	/*
	 * Make sure producer structures are aligned on 32-byte cache
	 * line boundaries.
	 */
	struct ti_producer	ti_ev_prodidx_r;
	u_int32_t		ti_pad0[6];
	struct ti_producer	ti_return_prodidx_r;
	u_int32_t		ti_pad1[6];
	struct ti_producer	ti_tx_considx_r;
	u_int32_t		ti_pad2[6];
	struct ti_gib		ti_info;
};

#define TI_RING_DMA_ADDR(sc, offset) \
	((sc)->ti_ring_map->dm_segs[0].ds_addr + \
	offsetof(struct ti_ring_data, offset))

#define TI_RING_DMASYNC(sc, offset, op) \
	bus_dmamap_sync((sc)->sc_dmatag, (sc)->ti_ring_map, \
			offsetof(struct ti_ring_data, offset), \
			sizeof(((struct ti_ring_data *)0)->offset), (op))

/*
 * Number of DMA segments in a TxCB. Note that this is carefully
 * chosen to make the total struct size an even power of two. It's
 * critical that no TxCB be split across a page boundary since
 * no attempt is made to allocate physically contiguous memory.
 * 
 */
#ifdef __LP64__
#define TI_NTXSEG      30
#else
#define TI_NTXSEG      31
#endif

struct ti_txmap_entry {
	bus_dmamap_t			dmamap;
	SLIST_ENTRY(ti_txmap_entry)	link;
};

/*
 * Mbuf pointers. We need these to keep track of the virtual addresses
 * of our mbuf chains since we can only convert from physical to virtual,
 * not the other way around.
 */
struct ti_chain_data {
	struct mbuf		*ti_tx_chain[TI_TX_RING_CNT];
	struct mbuf		*ti_rx_std_chain[TI_STD_RX_RING_CNT];
	struct mbuf		*ti_rx_jumbo_chain[TI_JUMBO_RX_RING_CNT];
	struct mbuf		*ti_rx_mini_chain[TI_MINI_RX_RING_CNT];

	struct ti_txmap_entry	*ti_tx_map[TI_TX_RING_CNT];
	bus_dmamap_t		ti_rx_std_map[TI_STD_RX_RING_CNT];
	bus_dmamap_t		ti_rx_jumbo_map[TI_JUMBO_RX_RING_CNT];
	bus_dmamap_t		ti_rx_mini_map[TI_MINI_RX_RING_CNT];

	/* Stick the jumbo mem management stuff here too. */
	struct ti_jslot		ti_jslots[TI_JSLOTS];
	void			*ti_jumbo_buf;
};

struct ti_type {
	u_int16_t		ti_vid;
	u_int16_t		ti_did;
	char			*ti_name;
};

#define TI_HWREV_TIGON		0x01
#define TI_HWREV_TIGON_II	0x02
#define TI_TIMEOUT		1000
#define TI_TXCONS_UNSET		0xFFFF	/* impossible value */

struct ti_mc_entry {
	struct ether_addr		mc_addr;
	SLIST_ENTRY(ti_mc_entry)	mc_entries;
};

struct ti_softc {
	struct device		sc_dv;
	struct arpcom		arpcom;		/* interface info */
	bus_space_handle_t	ti_bhandle;
	bus_space_tag_t		ti_btag;
	void *			ti_intrhand;
	struct ifmedia		ifmedia;	/* media info */
	u_int8_t		ti_hwrev;	/* Tigon rev (1 or 2) */
	u_int8_t		ti_sbus;	/* SBus card */
	u_int8_t		ti_copper;	/* 1000baseTX card */
	u_int8_t		ti_linkstat;	/* Link state */
	bus_dma_tag_t		sc_dmatag;
	struct ti_ring_data	*ti_rdata;	/* rings */
	struct ti_chain_data	ti_cdata;	/* mbufs */
#define ti_ev_prodidx		ti_rdata->ti_ev_prodidx_r
#define ti_return_prodidx	ti_rdata->ti_return_prodidx_r
#define ti_tx_considx		ti_rdata->ti_tx_considx_r
	struct ti_tx_desc	*ti_tx_ring_nic;/* pointer to shared mem */
	bus_dmamap_t		ti_ring_map;
	u_int16_t		ti_tx_saved_prodidx;
	u_int16_t		ti_tx_saved_considx;
	u_int16_t		ti_rx_saved_considx;
	u_int16_t		ti_ev_saved_considx;
	u_int16_t		ti_cmd_saved_prodidx;
	u_int16_t		ti_std;		/* current std ring head */
	u_int16_t		ti_mini;	/* current mini ring head */
	u_int16_t		ti_jumbo;	/* current jumbo ring head */
	SLIST_HEAD(__ti_mchead, ti_mc_entry)	ti_mc_listhead;
	SLIST_HEAD(__ti_txmaphead, ti_txmap_entry)	ti_tx_map_listhead;
	u_int32_t		ti_stat_ticks;
	u_int32_t		ti_rx_coal_ticks;
	u_int32_t		ti_tx_coal_ticks;
	u_int32_t		ti_rx_max_coal_bds;
	u_int32_t		ti_tx_max_coal_bds;
	u_int32_t		ti_tx_buf_ratio;
	int			ti_txcnt;
};

/*
 * Microchip Technology 24Cxx EEPROM control bytes
 */
#define EEPROM_CTL_READ			0xA1	/* 0101 0001 */
#define EEPROM_CTL_WRITE		0xA0	/* 0101 0000 */

/*
 * Note that EEPROM_START leaves transmission enabled.
 */
#define EEPROM_START							\
	TI_SETBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_CLK); /* Pull clock pin high */\
	TI_SETBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_DOUT); /* Set DATA bit to 1 */	\
	TI_SETBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_TXEN); /* Enable xmit to write bit */\
	TI_CLRBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_DOUT); /* Pull DATA bit to 0 again */\
	TI_CLRBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_CLK); /* Pull clock low again */

/*
 * EEPROM_STOP ends access to the EEPROM and clears the ETXEN bit so
 * that no further data can be written to the EEPROM I/O pin.
 */
#define EEPROM_STOP							\
	TI_CLRBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_TXEN); /* Disable xmit */	\
	TI_CLRBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_DOUT); /* Pull DATA to 0 */	\
	TI_SETBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_CLK); /* Pull clock high */	\
	TI_SETBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_TXEN); /* Enable xmit */	\
	TI_SETBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_DOUT); /* Toggle DATA to 1 */	\
	TI_CLRBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_TXEN); /* Disable xmit. */	\
	TI_CLRBIT(sc, TI_MISC_LOCAL_CTL, TI_MLC_EE_CLK); /* Pull clock low again */
