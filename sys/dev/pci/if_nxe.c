/*	$OpenBSD: if_nxe.c,v 1.82 2024/09/04 07:54:52 mglocker Exp $ */

/*
 * Copyright (c) 2007 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/timeout.h>
#include <sys/sensors.h>
#include <sys/rwlock.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <net/if.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#ifdef NXE_DEBUG
int nxedebug = 0;

#define DPRINTF(l, f...)	do { if (nxedebug & (l)) printf(f); } while (0)
#define DASSERT(_a)		assert(_a)
#else
#define DPRINTF(l, f...)
#define DASSERT(_a)
#endif

/* this driver likes firmwares around this version */
#define NXE_VERSION_MAJOR	3
#define NXE_VERSION_MINOR	4
#define NXE_VERSION_BUILD	31
#define NXE_VERSION \
    ((NXE_VERSION_MAJOR << 16)|(NXE_VERSION_MINOR << 8)|(NXE_VERSION_BUILD))


/*
 * PCI configuration space registers
 */

#define NXE_PCI_BAR_MEM		0x10 /* bar 0 */
#define NXE_PCI_BAR_MEM_128MB		(128 * 1024 * 1024)
#define NXE_PCI_BAR_DOORBELL	0x20 /* bar 4 */

/*
 * doorbell register space
 */

#define NXE_DB			0x00000000
#define  NXE_DB_PEGID			0x00000003
#define  NXE_DB_PEGID_RX		0x00000001 /* rx unit */
#define  NXE_DB_PEGID_TX		0x00000002 /* tx unit */
#define  NXE_DB_PRIVID			0x00000004 /* must be set */
#define  NXE_DB_COUNT(_c)		((_c)<<3) /* count */
#define  NXE_DB_CTXID(_c)		((_c)<<18) /* context id */
#define  NXE_DB_OPCODE_RX_PROD		0x00000000
#define  NXE_DB_OPCODE_RX_JUMBO_PROD	0x10000000
#define  NXE_DB_OPCODE_RX_LRO_PROD	0x20000000
#define  NXE_DB_OPCODE_CMD_PROD		0x30000000
#define  NXE_DB_OPCODE_UPD_CONS		0x40000000
#define  NXE_DB_OPCODE_RESET_CTX	0x50000000

/*
 * register space
 */

/* different PCI functions use different registers sometimes */
#define _F(_f)			((_f) * 0x20)

/*
 * driver ref section 4.2
 *
 * All the hardware registers are mapped in memory. Apart from the registers
 * for the individual hardware blocks, the memory map includes a large number
 * of software definable registers.
 *
 * The following table gives the memory map in the PCI address space.
 */

#define NXE_MAP_DDR_NET		0x00000000
#define NXE_MAP_DDR_MD		0x02000000
#define NXE_MAP_QDR_NET		0x04000000
#define NXE_MAP_DIRECT_CRB	0x04400000
#define NXE_MAP_OCM0		0x05000000
#define NXE_MAP_OCM1		0x05100000
#define NXE_MAP_CRB		0x06000000

/*
 * Since there are a large number of registers they do not fit in a single
 * PCI addressing range. Hence two windows are defined. The window starts at
 * NXE_MAP_CRB, and extends to the end of the register map. The window is set
 * using the NXE_REG_WINDOW_CRB register. The format of the NXE_REG_WINDOW_CRB
 * register is as follows:
 */

#define NXE_WIN_CRB(_f)		(0x06110210 + _F(_f))
#define  NXE_WIN_CRB_0			(0<<25)
#define  NXE_WIN_CRB_1			(1<<25)

/*
 * The memory map inside the register windows are divided into a set of blocks.
 * Each register block is owned by one hardware agent. The following table
 * gives the memory map of the various register blocks in window 0. These
 * registers are all in the CRB register space, so the offsets given here are
 * relative to the base of the CRB offset region (NXE_MAP_CRB).
 */

#define NXE_W0_PCIE		0x00100000 /* PCI Express */
#define NXE_W0_NIU		0x00600000 /* Network Interface Unit */
#define NXE_W0_PPE_0		0x01100000 /* Protocol Processing Engine 0 */
#define NXE_W0_PPE_1		0x01200000 /* Protocol Processing Engine 1 */
#define NXE_W0_PPE_2		0x01300000 /* Protocol Processing Engine 2 */
#define NXE_W0_PPE_3		0x01400000 /* Protocol Processing Engine 3 */
#define NXE_W0_PPE_D		0x01500000 /* PPE D-cache */
#define NXE_W0_PPE_I		0x01600000 /* PPE I-cache */

/*
 * These are the register blocks inside window 1.
 */

#define NXE_W1_PCIE		0x00100000
#define NXE_W1_SW		0x00200000
#define NXE_W1_SIR		0x01200000
#define NXE_W1_ROMUSB		0x01300000

/*
 * Global registers
 */
#define NXE_BOOTLD_START	0x00010000


/*
 * driver ref section 5
 *
 * CRB Window Register Descriptions
 */

/*
 * PCI Express Registers
 *
 * Despite being in the CRB window space, they can be accessed via both
 * windows. This means they are accessible "globally" without going relative
 * to the start of the CRB window space.
 */

/* Interrupts */
#define NXE_ISR_VECTOR		0x06110100 /* Interrupt Vector */
#define NXE_ISR_MASK		0x06110104 /* Interrupt Mask */
#define NXE_ISR_TARGET_STATUS	0x06110118
#define NXE_ISR_TARGET_MASK	0x06110128
#define  NXE_ISR_MINE(_f)		(0x08 << (_f))

/* lock registers (semaphores between chipset and driver) */
#define NXE_SEM_ROM_LOCK	0x0611c010 /* ROM access lock */
#define NXE_SEM_ROM_UNLOCK	0x0611c014
#define NXE_SEM_PHY_LOCK	0x0611c018 /* PHY access lock */
#define NXE_SEM_PHY_UNLOCK	0x0611c01c
#define  NXE_SEM_DONE			0x1

/*
 * Network Interface Unit (NIU) Registers
 */

#define NXE_0_NIU_MODE		0x00600000
#define  NXE_0_NIU_MODE_XGE		(1<<2) /* XGE interface enabled */
#define  NXE_0_NIU_MODE_GBE		(1<<1) /* 4 GbE interfaces enabled */
#define NXE_0_NIU_SINGLE_TERM	0x00600004
#define NXE_0_NIU_INT_MASK	0x00600040

#define NXE_0_NIU_RESET_XG	0x0060001c /* reset XG */
#define NXE_0_NIU_RESET_FIFO	0x00600088 /* reset sys fifos */

#define _P(_p)			((_p) * 0x10000)

#define NXE_0_XG_CFG0(_p)	(0x00670000 + _P(_p))
#define  NXE_0_XG_CFG0_TX_EN		(1<<0) /* TX enable */
#define  NXE_0_XG_CFG0_TX_SYNC		(1<<1) /* TX synced */
#define  NXE_0_XG_CFG0_RX_EN		(1<<2) /* RX enable */
#define  NXE_0_XG_CFG0_RX_SYNC		(1<<3) /* RX synced */
#define  NXE_0_XG_CFG0_TX_FLOWCTL	(1<<4) /* enable pause frame gen */
#define  NXE_0_XG_CFG0_RX_FLOWCTL	(1<<5) /* act on rxed pause frames */
#define  NXE_0_XG_CFG0_LOOPBACK		(1<<8) /* tx appears on rx */
#define  NXE_0_XG_CFG0_TX_RST_PB	(1<<15) /* reset frm tx proto block */
#define  NXE_0_XG_CFG0_RX_RST_PB	(1<<16) /* reset frm rx proto block */
#define  NXE_0_XG_CFG0_TX_RST_MAC	(1<<17) /* reset frm tx multiplexer */
#define  NXE_0_XG_CFG0_RX_RST_MAC	(1<<18) /* reset ctl frms and timers */
#define  NXE_0_XG_CFG0_SOFT_RST		(1<<31) /* soft reset */
#define NXE_0_XG_CFG1(_p)	(0x00670004 + _P(_p))
#define  NXE_0_XG_CFG1_REM_CRC		(1<<0) /* enable crc removal */
#define  NXE_0_XG_CFG1_CRC_EN		(1<<1) /* append crc to tx frames */
#define  NXE_0_XG_CFG1_NO_MAX		(1<<5) /* rx all frames despite size */
#define  NXE_0_XG_CFG1_WIRE_LO_ERR	(1<<6) /* recognize local err */
#define  NXE_0_XG_CFG1_PAUSE_FR_DIS	(1<<8) /* disable pause frame detect */
#define  NXE_0_XG_CFG1_SEQ_ERR_EN	(1<<10) /* enable seq err detection */
#define  NXE_0_XG_CFG1_MULTICAST	(1<<12) /* accept all multicast */
#define  NXE_0_XG_CFG1_PROMISC		(1<<13) /* accept all frames */
#define NXE_0_XG_IPG(_p)	(0x00670008 + _P(_p))
#define NXE_0_XG_MAC_LO(_p)	(0x00670010 + _P(_p))
#define NXE_0_XG_MAC_HI(_p)	(0x0067000c + _P(_p))
#define NXE_0_XG_STATUS(_p)	(0x00670018 + _P(_p))
#define NXE_0_XG_MTU(_p)	(0x0067001c + _P(_p))
#define NXE_0_XG_PAUSE_FRM(_p)	(0x00670020 + _P(_p))
#define NXE_0_XG_TX_BYTES(_p)	(0x00670024 + _P(_p))
#define NXE_0_XG_TX_PKTS(_p)	(0x00670028 + _P(_p))
#define NXE_0_XG_RX_BYTES(_p)	(0x0067002c + _P(_p))
#define NXE_0_XG_RX_PKTS(_p)	(0x00670030 + _P(_p))
#define NXE_0_XG_AGGR_ERRS(_p)	(0x00670034 + _P(_p))
#define NXE_0_XG_MCAST_PKTS(_p)	(0x00670038 + _P(_p))
#define NXE_0_XG_UCAST_PKTS(_p)	(0x0067003c + _P(_p))
#define NXE_0_XG_CRC_ERRS(_p)	(0x00670040 + _P(_p))
#define NXE_0_XG_OVERSIZE(_p)	(0x00670044 + _P(_p))
#define NXE_0_XG_UNDERSIZE(_p)	(0x00670048 + _P(_p))
#define NXE_0_XG_LOCAL_ERRS(_p)	(0x0067004c + _P(_p))
#define NXE_0_XG_REMOTE_ERRS(_p) (0x00670050 + _P(_p))
#define NXE_0_XG_CNTL_CHARS(_p)	(0x00670054 + _P(_p))
#define NXE_0_XG_PAUSE_PKTS(_p)	(0x00670058 + _P(_p))

/*
 * Software Defined Registers
 */

/* chipset state registers */
#define NXE_1_SW_ROM_LOCK_ID	0x00202100
#define  NXE_1_SW_ROM_LOCK_ID_DRV	0x0d417340
#define NXE_1_SW_PHY_LOCK_ID	0x00202120
#define  NXE_1_SW_PHY_LOCK_ID_DRV	0x44524956

/* firmware version */
#define NXE_1_SW_FWVER_MAJOR	0x00202150 /* Major f/w version */
#define NXE_1_SW_FWVER_MINOR	0x00202154 /* Minor f/w version */
#define NXE_1_SW_FWVER_BUILD	0x00202158 /* Build/Sub f/w version */

/* misc */
#define NXE_1_SW_CMD_ADDR_HI	0x00202218 /* cmd ring phys addr */
#define NXE_1_SW_CMD_ADDR_LO	0x0020221c /* cmd ring phys addr */
#define NXE_1_SW_CMD_SIZE	0x002022c8 /* entries in the cmd ring */
#define NXE_1_SW_DUMMY_ADDR_HI	0x0020223c /* hi address of dummy buf */
#define NXE_1_SW_DUMMY_ADDR_LO	0x00202240 /* lo address of dummy buf */
#define  NXE_1_SW_DUMMY_ADDR_LEN	1024

static const u_int32_t nxe_regmap[][4] = {
#define NXE_1_SW_CMD_PRODUCER(_f)	(nxe_regmap[0][(_f)])
    { 0x00202208, 0x002023ac, 0x002023b8, 0x002023d0 },
#define NXE_1_SW_CMD_CONSUMER(_f)	(nxe_regmap[1][(_f)])
    { 0x0020220c, 0x002023b0, 0x002023bc, 0x002023d4 },

#define NXE_1_SW_CONTEXT(_p)		(nxe_regmap[2][(_p)])
#define NXE_1_SW_CONTEXT_SIG(_p)	(0xdee0 | (_p))
    { 0x0020238c, 0x00202390, 0x0020239c, 0x002023a4 },
#define NXE_1_SW_CONTEXT_ADDR_LO(_p)	(nxe_regmap[3][(_p)])
    { 0x00202388, 0x00202390, 0x00202398, 0x002023a0 },
#define NXE_1_SW_CONTEXT_ADDR_HI(_p)	(nxe_regmap[4][(_p)])
    { 0x002023c0, 0x002023c4, 0x002023c8, 0x002023cc },

#define NXE_1_SW_INT_MASK(_p)		(nxe_regmap[5][(_p)])
    { 0x002023d8, 0x002023e0, 0x002023e4, 0x002023e8 },

#define NXE_1_SW_RX_PRODUCER(_c)	(nxe_regmap[6][(_c)])
    { 0x00202300, 0x00202344, 0x002023d8, 0x0020242c },
#define NXE_1_SW_RX_CONSUMER(_c)	(nxe_regmap[7][(_c)])
    { 0x00202304, 0x00202348, 0x002023dc, 0x00202430 },
#define NXE_1_SW_RX_RING(_c)		(nxe_regmap[8][(_c)])
    { 0x00202308, 0x0020234c, 0x002023f0, 0x00202434 },
#define NXE_1_SW_RX_SIZE(_c)		(nxe_regmap[9][(_c)])
    { 0x0020230c, 0x00202350, 0x002023f4, 0x00202438 },

#define NXE_1_SW_RX_JUMBO_PRODUCER(_c)	(nxe_regmap[10][(_c)])
    { 0x00202310, 0x00202354, 0x002023f8, 0x0020243c },
#define NXE_1_SW_RX_JUMBO_CONSUMER(_c)	(nxe_regmap[11][(_c)])
    { 0x00202314, 0x00202358, 0x002023fc, 0x00202440 },
#define NXE_1_SW_RX_JUMBO_RING(_c)	(nxe_regmap[12][(_c)])
    { 0x00202318, 0x0020235c, 0x00202400, 0x00202444 },
#define NXE_1_SW_RX_JUMBO_SIZE(_c)	(nxe_regmap[13][(_c)])
    { 0x0020231c, 0x00202360, 0x00202404, 0x00202448 },

#define NXE_1_SW_RX_LRO_PRODUCER(_c)	(nxe_regmap[14][(_c)])
    { 0x00202320, 0x00202364, 0x00202408, 0x0020244c },
#define NXE_1_SW_RX_LRO_CONSUMER(_c)	(nxe_regmap[15][(_c)])
    { 0x00202324, 0x00202368, 0x0020240c, 0x00202450 },
#define NXE_1_SW_RX_LRO_RING(_c)	(nxe_regmap[16][(_c)])
    { 0x00202328, 0x0020236c, 0x00202410, 0x00202454 },
#define NXE_1_SW_RX_LRO_SIZE(_c)	(nxe_regmap[17][(_c)])
    { 0x0020232c, 0x00202370, 0x00202414, 0x00202458 },

#define NXE_1_SW_STATUS_RING(_c)	(nxe_regmap[18][(_c)])
    { 0x00202330, 0x00202374, 0x00202418, 0x0020245c },
#define NXE_1_SW_STATUS_PRODUCER(_c)	(nxe_regmap[19][(_c)])
    { 0x00202334, 0x00202378, 0x0020241c, 0x00202460 },
#define NXE_1_SW_STATUS_CONSUMER(_c)	(nxe_regmap[20][(_c)])
    { 0x00202338, 0x0020237c, 0x00202420, 0x00202464 },
#define NXE_1_SW_STATUS_STATE(_c)	(nxe_regmap[21][(_c)])
#define  NXE_1_SW_STATUS_STATE_READY		0x0000ff01
    { 0x0020233c, 0x00202380, 0x00202424, 0x00202468 },
#define NXE_1_SW_STATUS_SIZE(_c)	(nxe_regmap[22][(_c)])
    { 0x00202340, 0x00202384, 0x00202428, 0x0020246c }
};


#define NXE_1_SW_BOOTLD_CONFIG	0x002021fc
#define  NXE_1_SW_BOOTLD_CONFIG_ROM	0x00000000
#define  NXE_1_SW_BOOTLD_CONFIG_RAM	0x12345678

#define NXE_1_SW_CMDPEG_STATE	0x00202250 /* init status */
#define  NXE_1_SW_CMDPEG_STATE_START	0xff00 /* init starting */
#define  NXE_1_SW_CMDPEG_STATE_DONE	0xff01 /* init complete */
#define  NXE_1_SW_CMDPEG_STATE_ACK	0xf00f /* init ack */
#define  NXE_1_SW_CMDPEG_STATE_ERROR	0xffff /* init failed */

#define NXE_1_SW_XG_STATE	0x00202294 /* phy state */
#define  NXE_1_SW_XG_STATE_PORT(_r, _p)	(((_r)>>8*(_p))&0xff)
#define  NXE_1_SW_XG_STATE_UP		(1<<4)
#define  NXE_1_SW_XG_STATE_DOWN		(1<<5)

#define NXE_1_SW_MPORT_MODE	0x002022c4
#define  NXE_1_SW_MPORT_MODE_SINGLE	0x1111
#define  NXE_1_SW_MPORT_MODE_MULTI	0x2222

#define NXE_1_SW_INT_VECTOR	0x002022d4

#define NXE_1_SW_NIC_CAP_HOST	0x002023a8 /* host capabilities */
#define NXE_1_SW_NIC_CAP_FW	0x002023dc /* firmware capabilities */
#define  NXE_1_SW_NIC_CAP_PORTINTR	0x1 /* per port interrupts */
#define NXE_1_SW_DRIVER_VER	0x002024a0 /* host driver version */


#define NXE_1_SW_TEMP		0x002023b4 /* Temperature sensor */
#define  NXE_1_SW_TEMP_STATE(_x)	((_x)&0xffff) /* Temp state */
#define  NXE_1_SW_TEMP_STATE_NONE	0x0000
#define  NXE_1_SW_TEMP_STATE_OK		0x0001
#define  NXE_1_SW_TEMP_STATE_WARN	0x0002
#define  NXE_1_SW_TEMP_STATE_CRIT	0x0003
#define  NXE_1_SW_TEMP_VAL(_x)		(((_x)>>16)&0xffff) /* Temp value */

#define NXE_1_SW_V2P(_f)	(0x00202490+((_f)*4)) /* virtual to phys */

/*
 * ROMUSB Registers
 */
#define NXE_1_ROMUSB_STATUS	0x01300004 /* ROM Status */
#define  NXE_1_ROMUSB_STATUS_DONE	(1<<1)
#define NXE_1_ROMUSB_SW_RESET	0x01300008
#define NXE_1_ROMUSB_SW_RESET_DEF	0xffffffff
#define NXE_1_ROMUSB_SW_RESET_BOOT	0x0080000f

#define NXE_1_CASPER_RESET	0x01300038
#define  NXE_1_CASPER_RESET_ENABLE	0x1
#define  NXE_1_CASPER_RESET_DISABLE	0x1

#define NXE_1_GLB_PEGTUNE	0x0130005c /* reset register */
#define  NXE_1_GLB_PEGTUNE_DONE		0x00000001

#define NXE_1_GLB_CHIPCLKCTL	0x013000a8
#define NXE_1_GLB_CHIPCLKCTL_ON		0x00003fff

/* ROM Registers */
#define NXE_1_ROM_CONTROL	0x01310000
#define NXE_1_ROM_OPCODE	0x01310004
#define  NXE_1_ROM_OPCODE_READ		0x0000000b
#define NXE_1_ROM_ADDR		0x01310008
#define NXE_1_ROM_WDATA		0x0131000c
#define NXE_1_ROM_ABYTE_CNT	0x01310010
#define NXE_1_ROM_DBYTE_CNT	0x01310014 /* dummy byte count */
#define NXE_1_ROM_RDATA		0x01310018
#define NXE_1_ROM_AGT_TAG	0x0131001c
#define NXE_1_ROM_TIME_PARM	0x01310020
#define NXE_1_ROM_CLK_DIV	0x01310024
#define NXE_1_ROM_MISS_INSTR	0x01310028

/*
 * flash memory layout
 *
 * These are offsets of memory accessible via the ROM Registers above
 */
#define NXE_FLASH_CRBINIT	0x00000000 /* crb init section */
#define NXE_FLASH_BRDCFG	0x00004000 /* board config */
#define NXE_FLASH_INITCODE	0x00006000 /* pegtune code */
#define NXE_FLASH_BOOTLD	0x00010000 /* boot loader */
#define NXE_FLASH_IMAGE		0x00043000 /* compressed image */
#define NXE_FLASH_SECONDARY	0x00200000 /* backup image */
#define NXE_FLASH_PXE		0x003d0000 /* pxe image */
#define NXE_FLASH_USER		0x003e8000 /* user region for new boards */
#define NXE_FLASH_VPD		0x003e8c00 /* vendor private data */
#define NXE_FLASH_LICENSE	0x003e9000 /* firmware license */
#define NXE_FLASH_FIXED		0x003f0000 /* backup of crbinit */


/*
 * misc hardware details
 */
#define NXE_MAX_PORTS		4
#define NXE_MAX_PORT_LLADDRS	32
#define NXE_MAX_PKTLEN		(64 * 1024)


/*
 * hardware structures
 */

struct nxe_info {
	u_int32_t		ni_hdrver;
#define NXE_INFO_HDRVER_1		0x00000001

	u_int32_t		ni_board_mfg;
	u_int32_t		ni_board_type;
#define NXE_BRDTYPE_P1_BD		0x0000
#define NXE_BRDTYPE_P1_SB		0x0001
#define NXE_BRDTYPE_P1_SMAX		0x0002
#define NXE_BRDTYPE_P1_SOCK		0x0003
#define NXE_BRDTYPE_P2_SOCK_31		0x0008
#define NXE_BRDTYPE_P2_SOCK_35		0x0009
#define NXE_BRDTYPE_P2_SB35_4G		0x000a
#define NXE_BRDTYPE_P2_SB31_10G		0x000b
#define NXE_BRDTYPE_P2_SB31_2G		0x000c
#define NXE_BRDTYPE_P2_SB31_10G_IMEZ	0x000d
#define NXE_BRDTYPE_P2_SB31_10G_HMEZ	0x000e
#define NXE_BRDTYPE_P2_SB31_10G_CX4	0x000f
	u_int32_t		ni_board_num;

	u_int32_t		ni_chip_id;
	u_int32_t		ni_chip_minor;
	u_int32_t		ni_chip_major;
	u_int32_t		ni_chip_pkg;
	u_int32_t		ni_chip_lot;

	u_int32_t		ni_port_mask;
	u_int32_t		ni_peg_mask;
	u_int32_t		ni_icache;
	u_int32_t		ni_dcache;
	u_int32_t		ni_casper;

	u_int32_t		ni_lladdr0_low;
	u_int32_t		ni_lladdr1_low;
	u_int32_t		ni_lladdr2_low;
	u_int32_t		ni_lladdr3_low;

	u_int32_t		ni_mnsync_mode;
	u_int32_t		ni_mnsync_shift_cclk;
	u_int32_t		ni_mnsync_shift_mclk;
	u_int32_t		ni_mnwb_enable;
	u_int32_t		ni_mnfreq_crystal;
	u_int32_t		ni_mnfreq_speed;
	u_int32_t		ni_mnorg;
	u_int32_t		ni_mndepth;
	u_int32_t		ni_mnranks0;
	u_int32_t		ni_mnranks1;
	u_int32_t		ni_mnrd_latency0;
	u_int32_t		ni_mnrd_latency1;
	u_int32_t		ni_mnrd_latency2;
	u_int32_t		ni_mnrd_latency3;
	u_int32_t		ni_mnrd_latency4;
	u_int32_t		ni_mnrd_latency5;
	u_int32_t		ni_mnrd_latency6;
	u_int32_t		ni_mnrd_latency7;
	u_int32_t		ni_mnrd_latency8;
	u_int32_t		ni_mndll[18];
	u_int32_t		ni_mnddr_mode;
	u_int32_t		ni_mnddr_extmode;
	u_int32_t		ni_mntiming0;
	u_int32_t		ni_mntiming1;
	u_int32_t		ni_mntiming2;

	u_int32_t		ni_snsync_mode;
	u_int32_t		ni_snpt_mode;
	u_int32_t		ni_snecc_enable;
	u_int32_t		ni_snwb_enable;
	u_int32_t		ni_snfreq_crystal;
	u_int32_t		ni_snfreq_speed;
	u_int32_t		ni_snorg;
	u_int32_t		ni_sndepth;
	u_int32_t		ni_sndll;
	u_int32_t		ni_snrd_latency;

	u_int32_t		ni_lladdr0_high;
	u_int32_t		ni_lladdr1_high;
	u_int32_t		ni_lladdr2_high;
	u_int32_t		ni_lladdr3_high;

	u_int32_t		ni_magic;
#define NXE_INFO_MAGIC			0x12345678

	u_int32_t		ni_mnrd_imm;
	u_int32_t		ni_mndll_override;
} __packed;

struct nxe_imageinfo {
	u_int32_t		nim_bootld_ver;
	u_int32_t		nim_bootld_size;

	u_int8_t		nim_img_ver_major;
	u_int8_t		nim_img_ver_minor;
	u_int16_t		nim_img_ver_build;

	u_int32_t		min_img_size;
} __packed;

struct nxe_lladdr {
	u_int8_t		pad[2];
	u_int8_t		lladdr[6];
} __packed;

struct nxe_userinfo {
	u_int8_t		nu_flash_md5[1024];

	struct nxe_imageinfo	nu_imageinfo;

	u_int32_t		nu_primary;
	u_int32_t		nu_secondary;

	u_int64_t		nu_lladdr[NXE_MAX_PORTS][NXE_MAX_PORT_LLADDRS];

	u_int32_t		nu_subsys_id;

	u_int8_t		nu_serial[32];

	u_int32_t		nu_bios_ver;
} __packed;

/* hw structures actually used in the io path */

struct nxe_ctx_ring {
	u_int64_t		r_addr;
	u_int32_t		r_size;
	u_int32_t		r_reserved;
};

#define NXE_RING_RX		0
#define NXE_RING_RX_JUMBO	1
#define NXE_RING_RX_LRO		2
#define NXE_NRING		3

struct nxe_ctx {
	u_int64_t		ctx_cmd_consumer_addr;

	struct nxe_ctx_ring	ctx_cmd_ring;

	struct nxe_ctx_ring	ctx_rx_rings[NXE_NRING];

	u_int64_t		ctx_status_ring_addr;
	u_int32_t		ctx_status_ring_size;

	u_int32_t		ctx_id;
} __packed;

struct nxe_tx_desc {
	u_int8_t		tx_tcp_offset;
	u_int8_t		tx_ip_offset;
	u_int16_t		tx_flags;
#define NXE_TXD_F_OPCODE_TX		(0x01 << 7)

	u_int8_t		tx_nbufs;
	u_int16_t		tx_length; /* XXX who makes a 24bit field? */
	u_int8_t		tx_length_hi;

	u_int64_t		tx_addr_2;

	u_int16_t		tx_id;
	u_int16_t		tx_mss;

	u_int8_t		tx_port;
	u_int8_t		tx_tso_hdr_len;
	u_int16_t		tx_ipsec_id;

	u_int64_t		tx_addr_3;

	u_int64_t		tx_addr_1;

	u_int16_t		tx_slen_1;
	u_int16_t		tx_slen_2;
	u_int16_t		tx_slen_3;
	u_int16_t		tx_slen_4;

	u_int64_t		tx_addr_4;

	u_int64_t		tx_reserved;
} __packed;
#define NXE_TXD_SEGS		4
#define NXE_TXD_DESCS		8
#define NXE_TXD_MAX_SEGS	(NXE_TXD_SEGS * NXE_TXD_DESCS)

struct nxe_rx_desc {
	u_int16_t		rx_id;
	u_int16_t		rx_flags;
	u_int32_t		rx_len; /* packet length */
	u_int64_t		rx_addr;
} __packed;
#define NXE_RXD_MAX_SEGS		1

struct nxe_status_desc {
	u_int8_t		st_lro;
	u_int8_t		st_owner;
	u_int16_t		st_id;
	u_int16_t		st_len;
	u_int16_t		st_flags;
} __packed;

/*
 * driver definitions
 */

struct nxe_board {
	u_int32_t		brd_type;
	u_int			brd_mode;
};

struct nxe_dmamem {
	bus_dmamap_t		ndm_map;
	bus_dma_segment_t	ndm_seg;
	size_t			ndm_size;
	caddr_t			ndm_kva;
};
#define NXE_DMA_MAP(_ndm)	((_ndm)->ndm_map)
#define NXE_DMA_LEN(_ndm)	((_ndm)->ndm_size)
#define NXE_DMA_DVA(_ndm)	((_ndm)->ndm_map->dm_segs[0].ds_addr)
#define NXE_DMA_KVA(_ndm)	((void *)(_ndm)->ndm_kva)

struct nxe_pkt {
	int			pkt_id;
	bus_dmamap_t		pkt_dmap;
	struct mbuf		*pkt_m;
	TAILQ_ENTRY(nxe_pkt)	pkt_link;
};

struct nxe_pkt_list {
	struct nxe_pkt		*npl_pkts;
	TAILQ_HEAD(, nxe_pkt)	npl_free;
	TAILQ_HEAD(, nxe_pkt)	npl_used;
};

struct nxe_ring {
	struct nxe_dmamem	*nr_dmamem;
	u_int8_t		*nr_pos;

	u_int			nr_slot;
	int			nr_ready;

	size_t			nr_desclen;
	u_int			nr_nentries;
};

/*
 * autoconf glue
 */

struct nxe_softc {
	struct device		sc_dev;

	bus_dma_tag_t		sc_dmat;

	bus_space_tag_t		sc_memt;
	bus_space_handle_t	sc_memh;
	bus_size_t		sc_mems;
	bus_space_handle_t	sc_crbh;
	bus_space_tag_t		sc_dbt;
	bus_space_handle_t	sc_dbh;
	bus_size_t		sc_dbs;

	void			*sc_ih;

	int			sc_function;
	int			sc_port;
	int			sc_window;

	const struct nxe_board	*sc_board;
	u_int			sc_fw_major;
	u_int			sc_fw_minor;
	u_int			sc_fw_build;

	struct arpcom		sc_ac;
	struct ifmedia		sc_media;

	struct nxe_pkt_list	*sc_tx_pkts;
	struct nxe_pkt_list	*sc_rx_pkts;

	/* allocations for the hw */
	struct nxe_dmamem	*sc_dummy_dma;
	struct nxe_dmamem	*sc_dummy_rx;

	struct nxe_dmamem	*sc_ctx;
	u_int32_t		*sc_cmd_consumer;
	u_int32_t		sc_cmd_consumer_cur;

	struct nxe_ring		*sc_cmd_ring;
	struct nxe_ring		*sc_rx_rings[NXE_NRING];
	struct nxe_ring		*sc_status_ring;

	/* monitoring */
	struct timeout		sc_tick;
	struct ksensor		sc_sensor;
	struct ksensordev	sc_sensor_dev;

	/* ioctl lock */
	struct rwlock		sc_lock;
};

int			nxe_match(struct device *, void *, void *);
void			nxe_attach(struct device *, struct device *, void *);
int			nxe_intr(void *);

const struct cfattach nxe_ca = {
	sizeof(struct nxe_softc),
	nxe_match,
	nxe_attach
};

struct cfdriver nxe_cd = {
	NULL,
	"nxe",
	DV_IFNET
};

/* init code */
int			nxe_pci_map(struct nxe_softc *,
			    struct pci_attach_args *);
void			nxe_pci_unmap(struct nxe_softc *);

int			nxe_board_info(struct nxe_softc *);
int			nxe_user_info(struct nxe_softc *);
int			nxe_init(struct nxe_softc *);
void			nxe_uninit(struct nxe_softc *);
void			nxe_mountroot(struct device *);

/* chip state */
void			nxe_tick(void *);
void			nxe_link_state(struct nxe_softc *);

/* interface operations */
int			nxe_ioctl(struct ifnet *, u_long, caddr_t);
void			nxe_start(struct ifnet *);
int			nxe_complete(struct nxe_softc *);
void			nxe_watchdog(struct ifnet *);

void			nxe_rx_start(struct nxe_softc *);

void			nxe_up(struct nxe_softc *);
void			nxe_lladdr(struct nxe_softc *);
void			nxe_iff(struct nxe_softc *);
void			nxe_down(struct nxe_softc *);

int			nxe_up_fw(struct nxe_softc *);

/* ifmedia operations */
int			nxe_media_change(struct ifnet *);
void			nxe_media_status(struct ifnet *, struct ifmediareq *);


/* ring handling */
struct nxe_ring		*nxe_ring_alloc(struct nxe_softc *, size_t, u_int);
void			nxe_ring_sync(struct nxe_softc *, struct nxe_ring *,
			    int);
void			nxe_ring_free(struct nxe_softc *, struct nxe_ring *);
int			nxe_ring_readable(struct nxe_ring *, int);
int			nxe_ring_writeable(struct nxe_ring *, int);
void			*nxe_ring_cur(struct nxe_softc *, struct nxe_ring *);
void			*nxe_ring_next(struct nxe_softc *, struct nxe_ring *);

struct mbuf		*nxe_load_pkt(struct nxe_softc *, bus_dmamap_t,
			    struct mbuf *);
struct mbuf		*nxe_coalesce_m(struct mbuf *);

/* pkts */
struct nxe_pkt_list	*nxe_pkt_alloc(struct nxe_softc *, u_int, int);
void			nxe_pkt_free(struct nxe_softc *,
			    struct nxe_pkt_list *);
void			nxe_pkt_put(struct nxe_pkt_list *, struct nxe_pkt *);
struct nxe_pkt		*nxe_pkt_get(struct nxe_pkt_list *);
struct nxe_pkt		*nxe_pkt_used(struct nxe_pkt_list *);


/* wrapper around dmaable memory allocations */
struct nxe_dmamem	*nxe_dmamem_alloc(struct nxe_softc *, bus_size_t,
			    bus_size_t);
void			nxe_dmamem_free(struct nxe_softc *,
			    struct nxe_dmamem *);

/* low level hardware access goo */
u_int32_t		nxe_read(struct nxe_softc *, bus_size_t);
void			nxe_write(struct nxe_softc *, bus_size_t, u_int32_t);
int			nxe_wait(struct nxe_softc *, bus_size_t, u_int32_t,
			    u_int32_t, u_int);

void			nxe_doorbell(struct nxe_softc *, u_int32_t);

int			nxe_crb_set(struct nxe_softc *, int);
u_int32_t		nxe_crb_read(struct nxe_softc *, bus_size_t);
void			nxe_crb_write(struct nxe_softc *, bus_size_t,
			    u_int32_t);
int			nxe_crb_wait(struct nxe_softc *, bus_size_t,
			    u_int32_t, u_int32_t, u_int);

int			nxe_rom_lock(struct nxe_softc *);
void			nxe_rom_unlock(struct nxe_softc *);
int			nxe_rom_read(struct nxe_softc *, u_int32_t,
			    u_int32_t *);
int			nxe_rom_read_region(struct nxe_softc *, u_int32_t,
			    void *, size_t);


/* misc bits */
#define DEVNAME(_sc)	((_sc)->sc_dev.dv_xname)

/* let's go! */

const struct pci_matchid nxe_devices[] = {
	{ PCI_VENDOR_NETXEN, PCI_PRODUCT_NETXEN_NXB_10GXXR },
	{ PCI_VENDOR_NETXEN, PCI_PRODUCT_NETXEN_NXB_10GCX4 },
	{ PCI_VENDOR_NETXEN, PCI_PRODUCT_NETXEN_NXB_4GCU },
	{ PCI_VENDOR_NETXEN, PCI_PRODUCT_NETXEN_NXB_IMEZ },
	{ PCI_VENDOR_NETXEN, PCI_PRODUCT_NETXEN_NXB_HMEZ },
	{ PCI_VENDOR_NETXEN, PCI_PRODUCT_NETXEN_NXB_IMEZ_2 },
	{ PCI_VENDOR_NETXEN, PCI_PRODUCT_NETXEN_NXB_HMEZ_2 }
};

const struct nxe_board nxe_boards[] = {
	{ NXE_BRDTYPE_P2_SB35_4G,	NXE_0_NIU_MODE_GBE },
	{ NXE_BRDTYPE_P2_SB31_10G,	NXE_0_NIU_MODE_XGE },
	{ NXE_BRDTYPE_P2_SB31_2G,	NXE_0_NIU_MODE_GBE },
	{ NXE_BRDTYPE_P2_SB31_10G_IMEZ,	NXE_0_NIU_MODE_XGE },
	{ NXE_BRDTYPE_P2_SB31_10G_HMEZ,	NXE_0_NIU_MODE_XGE },
	{ NXE_BRDTYPE_P2_SB31_10G_CX4,	NXE_0_NIU_MODE_XGE }
};

int
nxe_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args		*pa = aux;

	if (PCI_CLASS(pa->pa_class) != PCI_CLASS_NETWORK)
		return (0);

	return (pci_matchbyid(pa, nxe_devices, nitems(nxe_devices)));
}

void
nxe_attach(struct device *parent, struct device *self, void *aux)
{
	struct nxe_softc		*sc = (struct nxe_softc *)self;
	struct pci_attach_args		*pa = aux;
	pci_intr_handle_t		ih;
	struct ifnet			*ifp;

	sc->sc_dmat = pa->pa_dmat;
	sc->sc_function = pa->pa_function;
	sc->sc_window = -1;

	rw_init(&sc->sc_lock, NULL);

	if (nxe_pci_map(sc, pa) != 0) {
		/* error already printed by nxe_pci_map() */
		return;
	}

	nxe_crb_set(sc, 1);

	if (nxe_board_info(sc) != 0) {
		/* error already printed by nxe_board_info() */
		goto unmap;
	}

	if (nxe_user_info(sc) != 0) {
		/* error already printed by nxe_board_info() */
		goto unmap;
	}

	if (nxe_init(sc) != 0) {
		/* error already printed by nxe_init() */
		goto unmap;
	}

	if (pci_intr_map(pa, &ih) != 0) {
		printf(": unable to map interrupt\n");
		goto uninit;
	}
	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_NET,
	    nxe_intr, sc, DEVNAME(sc));
	if (sc->sc_ih == NULL) {
		printf(": unable to establish interrupt\n");
		goto uninit;
	}

	ifp = &sc->sc_ac.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_capabilities = IFCAP_VLAN_MTU;
	ifp->if_ioctl = nxe_ioctl;
	ifp->if_start = nxe_start;
	ifp->if_watchdog = nxe_watchdog;
	ifp->if_hardmtu = MCLBYTES - ETHER_HDR_LEN - ETHER_CRC_LEN;
	strlcpy(ifp->if_xname, DEVNAME(sc), IFNAMSIZ);
	ifq_init_maxlen(&ifp->if_snd, 512); /* XXX */

	ifmedia_init(&sc->sc_media, 0, nxe_media_change, nxe_media_status);
	ifmedia_add(&sc->sc_media, IFM_ETHER|IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER|IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	printf(": %s firmware %d.%d.%d address %s\n",
	    pci_intr_string(pa->pa_pc, ih),
	    sc->sc_fw_major, sc->sc_fw_minor, sc->sc_fw_build,
	    ether_sprintf(sc->sc_ac.ac_enaddr));
	return;

uninit:
	nxe_uninit(sc);
unmap:
	nxe_pci_unmap(sc);
}

int
nxe_pci_map(struct nxe_softc *sc, struct pci_attach_args *pa)
{
	pcireg_t			memtype;

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, NXE_PCI_BAR_MEM);
	if (pci_mapreg_map(pa, NXE_PCI_BAR_MEM, memtype, 0, &sc->sc_memt,
	    &sc->sc_memh, NULL, &sc->sc_mems, 0) != 0) {
		printf(": unable to map host registers\n");
		return (1);
	}
	if (sc->sc_mems != NXE_PCI_BAR_MEM_128MB) {
		printf(": unexpected register map size\n");
		goto unmap_mem;
	}

	/* set up the CRB window */
	if (bus_space_subregion(sc->sc_memt, sc->sc_memh, NXE_MAP_CRB,
	    sc->sc_mems - NXE_MAP_CRB, &sc->sc_crbh) != 0) {
		printf(": unable to create CRB window\n");
		goto unmap_mem;
	}

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, NXE_PCI_BAR_DOORBELL);
	if (pci_mapreg_map(pa, NXE_PCI_BAR_DOORBELL, memtype, 0, &sc->sc_dbt,
	    &sc->sc_dbh, NULL, &sc->sc_dbs, 0) != 0) {
		printf(": unable to map doorbell registers\n");
		/* bus_space(9) says i dont have to unmap subregions */
		goto unmap_mem;
	}

	config_mountroot(&sc->sc_dev, nxe_mountroot);
	return (0);

unmap_mem:
	bus_space_unmap(sc->sc_memt, sc->sc_memh, sc->sc_mems);
	sc->sc_mems = 0;
	return (1);
}

void
nxe_pci_unmap(struct nxe_softc *sc)
{
	bus_space_unmap(sc->sc_dbt, sc->sc_dbh, sc->sc_dbs);
	sc->sc_dbs = 0;
	/* bus_space(9) says i dont have to unmap the crb subregion */
	bus_space_unmap(sc->sc_memt, sc->sc_memh, sc->sc_mems);
	sc->sc_mems = 0;
}

int
nxe_intr(void *xsc)
{
	struct nxe_softc		*sc = xsc;
	u_int32_t			vector;

	DASSERT(sc->sc_window == 1);

	vector = nxe_crb_read(sc, NXE_1_SW_INT_VECTOR);
	if (!ISSET(vector, NXE_ISR_MINE(sc->sc_function)))
		return (0);

	nxe_crb_write(sc, NXE_1_SW_INT_VECTOR, 0x80 << sc->sc_function);

	/* the interrupt is mine! we should do some work now */

	return (1);
}

int
nxe_ioctl(struct ifnet *ifp, u_long cmd, caddr_t addr)
{
	struct nxe_softc		*sc = ifp->if_softc;
	struct ifreq			*ifr = (struct ifreq *)addr;
	int				s, error = 0;

	rw_enter_write(&sc->sc_lock);
	s = splnet();

	timeout_del(&sc->sc_tick);

	switch (cmd) {
	case SIOCSIFADDR:
		SET(ifp->if_flags, IFF_UP);
		/* FALLTHROUGH */

	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = ENETRESET;
			else
				nxe_up(sc);
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				nxe_down(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, addr);
	}

	if (error == ENETRESET) {
		if (ISSET(ifp->if_flags, IFF_RUNNING)) {
			nxe_crb_set(sc, 0);
			nxe_iff(sc);
			nxe_crb_set(sc, 1);
		}
		error = 0;
	}

	nxe_tick(sc);

	splx(s);
	rw_exit_write(&sc->sc_lock);
	return (error);
}

void
nxe_up(struct nxe_softc *sc)
{
	struct ifnet			*ifp = &sc->sc_ac.ac_if;
	static const u_int		rx_ring_sizes[] = { 16384, 1024, 128 };
	struct {
		struct nxe_ctx			ctx;
		u_int32_t			cmd_consumer;
	} __packed			*dmamem;
	struct nxe_ctx			*ctx;
	struct nxe_ctx_ring		*ring;
	struct nxe_ring			*nr;
	u_int64_t			dva;
	u_int32_t			intr_scheme;
	int				i;

	if (nxe_up_fw(sc) != 0)
		return;

	/* allocate pkt lists */
	sc->sc_tx_pkts = nxe_pkt_alloc(sc, 128, NXE_TXD_MAX_SEGS);
	if (sc->sc_tx_pkts == NULL)
		return;
	sc->sc_rx_pkts = nxe_pkt_alloc(sc, 128, NXE_RXD_MAX_SEGS);
	if (sc->sc_rx_pkts == NULL)
		goto free_tx_pkts;

	/* allocate the context memory and the consumer field */
	sc->sc_ctx = nxe_dmamem_alloc(sc, sizeof(*dmamem), PAGE_SIZE);
	if (sc->sc_ctx == NULL)
		goto free_rx_pkts;

	dmamem = NXE_DMA_KVA(sc->sc_ctx);
	dva = NXE_DMA_DVA(sc->sc_ctx);

	ctx = &dmamem->ctx;
	ctx->ctx_cmd_consumer_addr = htole64(dva + sizeof(dmamem->ctx));
	ctx->ctx_id = htole32(sc->sc_function);

	sc->sc_cmd_consumer = &dmamem->cmd_consumer;
	sc->sc_cmd_consumer_cur = 0;

	/* allocate the cmd/tx ring */
	sc->sc_cmd_ring = nxe_ring_alloc(sc,
	    sizeof(struct nxe_tx_desc), 1024 /* XXX */);
	if (sc->sc_cmd_ring == NULL)
		goto free_ctx;

	ctx->ctx_cmd_ring.r_addr =
	    htole64(NXE_DMA_DVA(sc->sc_cmd_ring->nr_dmamem));
	ctx->ctx_cmd_ring.r_size = htole32(sc->sc_cmd_ring->nr_nentries);

	/* allocate the status ring */
	sc->sc_status_ring = nxe_ring_alloc(sc,
	    sizeof(struct nxe_status_desc), 16384 /* XXX */);
	if (sc->sc_status_ring == NULL)
		goto free_cmd_ring;

	ctx->ctx_status_ring_addr =
	    htole64(NXE_DMA_DVA(sc->sc_status_ring->nr_dmamem));
	ctx->ctx_status_ring_size = htole32(sc->sc_status_ring->nr_nentries);

	/* allocate something to point the jumbo and lro rings at */
	sc->sc_dummy_rx = nxe_dmamem_alloc(sc, NXE_MAX_PKTLEN, PAGE_SIZE);
	if (sc->sc_dummy_rx == NULL)
		goto free_status_ring;

	/* allocate the rx rings */
	for (i = 0; i < NXE_NRING; i++) {
		ring = &ctx->ctx_rx_rings[i];
		nr = nxe_ring_alloc(sc, sizeof(struct nxe_rx_desc),
		    rx_ring_sizes[i]);
		if (nr == NULL)
			goto free_rx_rings;

		ring->r_addr = htole64(NXE_DMA_DVA(nr->nr_dmamem));
		ring->r_size = htole32(nr->nr_nentries);

		sc->sc_rx_rings[i] = nr;
		nxe_ring_sync(sc, sc->sc_rx_rings[i], BUS_DMASYNC_PREWRITE);
	}

	/* nothing can possibly go wrong now */
	bus_dmamap_sync(sc->sc_dmat, NXE_DMA_MAP(sc->sc_dummy_rx),
	    0, NXE_DMA_LEN(sc->sc_dummy_rx), BUS_DMASYNC_PREREAD);
	nxe_ring_sync(sc, sc->sc_status_ring, BUS_DMASYNC_PREREAD);
	nxe_ring_sync(sc, sc->sc_cmd_ring, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, NXE_DMA_MAP(sc->sc_ctx),
	    0, NXE_DMA_LEN(sc->sc_ctx),
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	nxe_crb_write(sc, NXE_1_SW_CONTEXT_ADDR_LO(sc->sc_function),
	    (u_int32_t)dva);
	nxe_crb_write(sc, NXE_1_SW_CONTEXT_ADDR_HI(sc->sc_function),
	    (u_int32_t)(dva >> 32));
	nxe_crb_write(sc, NXE_1_SW_CONTEXT(sc->sc_port),
	    NXE_1_SW_CONTEXT_SIG(sc->sc_port));

	nxe_crb_set(sc, 0);
	nxe_crb_write(sc, NXE_0_XG_MTU(sc->sc_function),
	    MCLBYTES - ETHER_ALIGN);
	nxe_lladdr(sc);
	nxe_iff(sc);
	nxe_crb_set(sc, 1);

	SET(ifp->if_flags, IFF_RUNNING);
	ifq_clr_oactive(&ifp->if_snd);

	/* enable interrupts */
	intr_scheme = nxe_crb_read(sc, NXE_1_SW_NIC_CAP_FW);
	if (intr_scheme != NXE_1_SW_NIC_CAP_PORTINTR)
		nxe_write(sc, NXE_ISR_MASK, 0x77f);
	nxe_crb_write(sc, NXE_1_SW_INT_MASK(sc->sc_function), 0x1);
	if (intr_scheme != NXE_1_SW_NIC_CAP_PORTINTR)
		nxe_crb_write(sc, NXE_1_SW_INT_VECTOR, 0x0);
	nxe_write(sc, NXE_ISR_TARGET_MASK, 0xbff);

	return;

free_rx_rings:
	while (i > 0) {
		i--;
		nxe_ring_sync(sc, sc->sc_rx_rings[i], BUS_DMASYNC_POSTWRITE);
		nxe_ring_free(sc, sc->sc_rx_rings[i]);
	}

	nxe_dmamem_free(sc, sc->sc_dummy_rx);
free_status_ring:
	nxe_ring_free(sc, sc->sc_status_ring);
free_cmd_ring:
	nxe_ring_free(sc, sc->sc_cmd_ring);
free_ctx:
	nxe_dmamem_free(sc, sc->sc_ctx);
free_rx_pkts:
	nxe_pkt_free(sc, sc->sc_rx_pkts);
free_tx_pkts:
	nxe_pkt_free(sc, sc->sc_tx_pkts);
}

int
nxe_up_fw(struct nxe_softc *sc)
{
	u_int32_t			r;

	r = nxe_crb_read(sc, NXE_1_SW_CMDPEG_STATE);
	if (r == NXE_1_SW_CMDPEG_STATE_ACK)
		return (0);

	if (r != NXE_1_SW_CMDPEG_STATE_DONE)
		return (1);

	nxe_crb_write(sc, NXE_1_SW_NIC_CAP_HOST, NXE_1_SW_NIC_CAP_PORTINTR);
	nxe_crb_write(sc, NXE_1_SW_MPORT_MODE, NXE_1_SW_MPORT_MODE_MULTI);
	nxe_crb_write(sc, NXE_1_SW_CMDPEG_STATE, NXE_1_SW_CMDPEG_STATE_ACK);

	/* XXX busy wait in a process context is naughty */
	if (!nxe_crb_wait(sc, NXE_1_SW_STATUS_STATE(sc->sc_function),
	    0xffffffff, NXE_1_SW_STATUS_STATE_READY, 1000))
		return (1);

	return (0);
}

void
nxe_lladdr(struct nxe_softc *sc)
{
	u_int8_t			*lladdr = sc->sc_ac.ac_enaddr;

	DASSERT(sc->sc_window == 0);

	nxe_crb_write(sc, NXE_0_XG_MAC_LO(sc->sc_port),
	    (lladdr[0] << 16) | (lladdr[1] << 24));
	nxe_crb_write(sc, NXE_0_XG_MAC_HI(sc->sc_port),
	    (lladdr[2] << 0)  | (lladdr[3] << 8) |
	    (lladdr[4] << 16) | (lladdr[5] << 24));
}

void
nxe_iff(struct nxe_softc *sc)
{
	struct ifnet			*ifp = &sc->sc_ac.ac_if;
	u_int32_t			cfg1 = 0x1447; /* XXX */

	DASSERT(sc->sc_window == 0);

	CLR(ifp->if_flags, IFF_ALLMULTI);

	if (ISSET(ifp->if_flags, IFF_PROMISC) || sc->sc_ac.ac_multicnt > 0) {
		SET(ifp->if_flags, IFF_ALLMULTI);
		if (ISSET(ifp->if_flags, IFF_PROMISC))
			cfg1 |= NXE_0_XG_CFG1_PROMISC;
		else
			cfg1 |= NXE_0_XG_CFG1_MULTICAST;
	}

	nxe_crb_write(sc, NXE_0_XG_CFG0(sc->sc_port),
	    NXE_0_XG_CFG0_TX_EN | NXE_0_XG_CFG0_RX_EN);
	nxe_crb_write(sc, NXE_0_XG_CFG1(sc->sc_port), cfg1);
}

void
nxe_down(struct nxe_softc *sc)
{
	struct ifnet			*ifp = &sc->sc_ac.ac_if;
	int				i;

	CLR(ifp->if_flags, IFF_RUNNING | IFF_ALLMULTI);
	ifq_clr_oactive(&ifp->if_snd);

	/* XXX turn the chip off */

	bus_dmamap_sync(sc->sc_dmat, NXE_DMA_MAP(sc->sc_ctx),
	    0, NXE_DMA_LEN(sc->sc_ctx),
	    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
	nxe_ring_sync(sc, sc->sc_cmd_ring, BUS_DMASYNC_POSTWRITE);
	nxe_ring_sync(sc, sc->sc_status_ring, BUS_DMASYNC_POSTREAD);
	bus_dmamap_sync(sc->sc_dmat, NXE_DMA_MAP(sc->sc_dummy_rx),
	    0, NXE_DMA_LEN(sc->sc_dummy_rx), BUS_DMASYNC_POSTREAD);

	for (i = 0; i < NXE_NRING; i++) {
		nxe_ring_sync(sc, sc->sc_rx_rings[i], BUS_DMASYNC_POSTWRITE);
		nxe_ring_free(sc, sc->sc_rx_rings[i]);
	}
	nxe_dmamem_free(sc, sc->sc_dummy_rx);
	nxe_ring_free(sc, sc->sc_status_ring);
	nxe_ring_free(sc, sc->sc_cmd_ring);
	nxe_dmamem_free(sc, sc->sc_ctx);
	nxe_pkt_free(sc, sc->sc_rx_pkts);
	nxe_pkt_free(sc, sc->sc_tx_pkts);
}

void
nxe_start(struct ifnet *ifp)
{
	struct nxe_softc		*sc = ifp->if_softc;
	struct nxe_ring			*nr = sc->sc_cmd_ring;
	struct nxe_tx_desc		*txd;
	struct nxe_pkt			*pkt;
	struct mbuf			*m;
	bus_dmamap_t			dmap;
	bus_dma_segment_t		*segs;
	int				nsegs;

	if (!ISSET(ifp->if_flags, IFF_RUNNING) ||
	    ifq_is_oactive(&ifp->if_snd) ||
	    ifq_empty(&ifp->if_snd))
		return;

	if (nxe_ring_writeable(nr, sc->sc_cmd_consumer_cur) < NXE_TXD_DESCS) {
		ifq_set_oactive(&ifp->if_snd);
		return;
	}

	nxe_ring_sync(sc, nr, BUS_DMASYNC_POSTWRITE);
	txd = nxe_ring_cur(sc, nr);
	bzero(txd, sizeof(struct nxe_tx_desc));

	do {
		m = ifq_deq_begin(&ifp->if_snd);
		if (m == NULL)
			break;

		pkt = nxe_pkt_get(sc->sc_tx_pkts);
		if (pkt == NULL) {
			ifq_deq_rollback(&ifp->if_snd, m);
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		ifq_deq_commit(&ifp->if_snd, m);

		dmap = pkt->pkt_dmap;
		m = nxe_load_pkt(sc, dmap, m);
		if (m == NULL) {
			nxe_pkt_put(sc->sc_tx_pkts, pkt);
			ifp->if_oerrors++;
			break;
		}

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

		pkt->pkt_m = m;

		txd->tx_flags = htole16(NXE_TXD_F_OPCODE_TX);
		txd->tx_nbufs = dmap->dm_nsegs;
		txd->tx_length = htole16(dmap->dm_mapsize);
		txd->tx_port = sc->sc_port;

		segs = dmap->dm_segs;
		nsegs = dmap->dm_nsegs;
		do {
			switch ((nsegs > NXE_TXD_SEGS) ?
			    NXE_TXD_SEGS : nsegs) {
			case 4:
				txd->tx_addr_4 = htole64(segs[3].ds_addr);
				txd->tx_slen_4 = htole32(segs[3].ds_len);
			case 3:
				txd->tx_addr_3 = htole64(segs[2].ds_addr);
				txd->tx_slen_3 = htole32(segs[2].ds_len);
			case 2:
				txd->tx_addr_2 = htole64(segs[1].ds_addr);
				txd->tx_slen_2 = htole32(segs[1].ds_len);
			case 1:
				txd->tx_addr_1 = htole64(segs[0].ds_addr);
				txd->tx_slen_1 = htole32(segs[0].ds_len);
				break;
			default:
				panic("%s: unexpected segments in tx map",
				    DEVNAME(sc));
			}

			nsegs -= NXE_TXD_SEGS;
			segs += NXE_TXD_SEGS;

			pkt->pkt_id = nr->nr_slot;

			txd = nxe_ring_next(sc, nr);
			bzero(txd, sizeof(struct nxe_tx_desc));
		} while (nsegs > 0);

		bus_dmamap_sync(sc->sc_dmat, dmap, 0, dmap->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);

	} while (nr->nr_ready >= NXE_TXD_DESCS);

	nxe_ring_sync(sc, nr, BUS_DMASYNC_PREWRITE);
	nxe_crb_write(sc, NXE_1_SW_CMD_PRODUCER(sc->sc_function), nr->nr_slot);
}

int
nxe_complete(struct nxe_softc *sc)
{
	struct nxe_pkt			*pkt;
	int				new_cons, cur_cons;
	int				rv = 0;

	bus_dmamap_sync(sc->sc_dmat, NXE_DMA_MAP(sc->sc_ctx),
	    0, NXE_DMA_LEN(sc->sc_ctx),
	    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
	new_cons = letoh32(*sc->sc_cmd_consumer);
	bus_dmamap_sync(sc->sc_dmat, NXE_DMA_MAP(sc->sc_ctx),
	    0, NXE_DMA_LEN(sc->sc_ctx),
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	cur_cons = sc->sc_cmd_consumer_cur;
	pkt = nxe_pkt_used(sc->sc_tx_pkts);

	while (pkt != NULL && cur_cons != new_cons) {
		if (pkt->pkt_id == cur_cons) {
			bus_dmamap_sync(sc->sc_dmat, pkt->pkt_dmap,
			    0, pkt->pkt_dmap->dm_mapsize,
			    BUS_DMASYNC_POSTWRITE);
			    bus_dmamap_unload(sc->sc_dmat, pkt->pkt_dmap);

			m_freem(pkt->pkt_m);

			nxe_pkt_put(sc->sc_tx_pkts, pkt);

			pkt = nxe_pkt_used(sc->sc_tx_pkts);
		}

		cur_cons++;
		cur_cons %= sc->sc_cmd_ring->nr_nentries;

		rv = 1;
	}

	if (rv == 1) {
		sc->sc_cmd_consumer_cur = cur_cons;
		ifq_clr_oactive(&sc->sc_ac.ac_if.if_snd);
	}

	return (rv);
}

struct mbuf *
nxe_coalesce_m(struct mbuf *m)
{
	struct mbuf			*m0;

	MGETHDR(m0, M_DONTWAIT, MT_DATA);
	if (m0 == NULL)
		goto err;

	if (m->m_pkthdr.len > MHLEN) {
		MCLGET(m0, M_DONTWAIT);
		if (!(m0->m_flags & M_EXT)) {
			m_freem(m0);
			m0 = NULL;
			goto err;
		}
	}

	m_copydata(m, 0, m->m_pkthdr.len, mtod(m0, caddr_t));
	m0->m_pkthdr.len = m0->m_len = m->m_pkthdr.len;

err:
	m_freem(m);
	return (m0);
}

struct mbuf *
nxe_load_pkt(struct nxe_softc *sc, bus_dmamap_t dmap, struct mbuf *m)
{
	switch (bus_dmamap_load_mbuf(sc->sc_dmat, dmap, m, BUS_DMA_NOWAIT)) {
	case 0:
		break;

	case EFBIG:
		m = nxe_coalesce_m(m);
		if (m == NULL)
			break;

		if (bus_dmamap_load_mbuf(sc->sc_dmat, dmap, m,
		    BUS_DMA_NOWAIT) == 0)
			break;

		/* we get here on error */
		/* FALLTHROUGH */
	default:
		m_freem(m);
		m = NULL;
		break;
	}

	return (m);
}

void
nxe_rx_start(struct nxe_softc *sc)
{
	struct nxe_ring			*nr = sc->sc_rx_rings[NXE_RING_RX];
	struct nxe_rx_desc		*rxd;
	struct nxe_pkt			*pkt;
	struct mbuf			*m;

	if (nxe_ring_writeable(nr, 0) == 0)
		return;

	nxe_ring_sync(sc, nr, BUS_DMASYNC_POSTWRITE);
	rxd = nxe_ring_cur(sc, nr);

	for (;;) {
		pkt = nxe_pkt_get(sc->sc_rx_pkts);
		if (pkt == NULL)
			goto done;

		MGETHDR(m, M_DONTWAIT, MT_DATA);
		if (m == NULL)
			goto put_pkt;

		MCLGET(m, M_DONTWAIT);
		if (!ISSET(m->m_flags, M_EXT))
			goto free_m;

		m->m_data += ETHER_ALIGN;
		m->m_len = m->m_pkthdr.len = MCLBYTES - ETHER_ALIGN;

		if (bus_dmamap_load_mbuf(sc->sc_dmat, pkt->pkt_dmap, m,
		    BUS_DMA_NOWAIT) != 0)
			goto free_m;

		pkt->pkt_m = m;

		bzero(rxd, sizeof(struct nxe_rx_desc));
		rxd->rx_len = htole32(m->m_len);
		rxd->rx_id = pkt->pkt_id;
		rxd->rx_addr = htole64(pkt->pkt_dmap->dm_segs[0].ds_addr);

		bus_dmamap_sync(sc->sc_dmat, pkt->pkt_dmap, 0,
		    pkt->pkt_dmap->dm_mapsize, BUS_DMASYNC_PREREAD);

		rxd = nxe_ring_next(sc, nr);

		if (nr->nr_ready == 0)
			goto done;
	}

free_m:
	m_freem(m);
put_pkt:
	nxe_pkt_put(sc->sc_rx_pkts, pkt);
done:
	nxe_ring_sync(sc, nr, BUS_DMASYNC_PREWRITE);
	nxe_crb_write(sc, NXE_1_SW_RX_PRODUCER(sc->sc_function), nr->nr_slot);
	nxe_doorbell(sc, NXE_DB_PEGID_RX | NXE_DB_PRIVID |
	    NXE_DB_OPCODE_RX_PROD |
	    NXE_DB_COUNT(nr->nr_slot) | NXE_DB_CTXID(sc->sc_function));
}

void
nxe_watchdog(struct ifnet *ifp)
{
	/* do nothing */
}

int
nxe_media_change(struct ifnet *ifp)
{
	/* ignore for now */
	return (0);
}

void
nxe_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct nxe_softc		*sc = ifp->if_softc;

	imr->ifm_active = IFM_ETHER | IFM_AUTO;
	imr->ifm_status = IFM_AVALID;

	nxe_link_state(sc);
	if (LINK_STATE_IS_UP(ifp->if_link_state))
		imr->ifm_status |= IFM_ACTIVE;
}

void
nxe_link_state(struct nxe_softc *sc)
{
	struct ifnet			*ifp = &sc->sc_ac.ac_if;
	int				link_state = LINK_STATE_DOWN;
	u_int32_t			r;

	DASSERT(sc->sc_window == 1);

	r = nxe_crb_read(sc, NXE_1_SW_XG_STATE);
	if (NXE_1_SW_XG_STATE_PORT(r, sc->sc_function) & NXE_1_SW_XG_STATE_UP)
		link_state = LINK_STATE_UP;

	if (ifp->if_link_state != link_state) {
		ifp->if_link_state = link_state;
		if_link_state_change(ifp);
	}
}

int
nxe_board_info(struct nxe_softc *sc)
{
	struct nxe_info			*ni;
	int				rv = 1;
	int				i;

	ni = malloc(sizeof(struct nxe_info), M_TEMP, M_NOWAIT);
	if (ni == NULL) {
		printf(": unable to allocate temporary memory\n");
		return (1);
	}

	if (nxe_rom_read_region(sc, NXE_FLASH_BRDCFG, ni,
	    sizeof(struct nxe_info)) != 0) {
		printf(": unable to read board info\n");
		goto out;
	}

	if (ni->ni_hdrver != NXE_INFO_HDRVER_1) {
		printf(": unexpected board info header version 0x%08x\n",
		    ni->ni_hdrver);
		goto out;
	}
	if (ni->ni_magic != NXE_INFO_MAGIC) {
		printf(": board info magic is invalid\n");
		goto out;
	}

	for (i = 0; i < nitems(nxe_boards); i++) {
		if (ni->ni_board_type == nxe_boards[i].brd_type) {
			sc->sc_board = &nxe_boards[i];
			break;
		}
	}
	if (sc->sc_board == NULL) {
		printf(": unknown board type %04x\n", ni->ni_board_type);
		goto out;
	}

	rv = 0;
out:
	free(ni, M_TEMP, 0);
	return (rv);
}

int
nxe_user_info(struct nxe_softc *sc)
{
	struct nxe_userinfo		*nu;
	u_int64_t			lladdr;
	struct nxe_lladdr		*la;
	int				rv = 1;

	nu = malloc(sizeof(struct nxe_userinfo), M_TEMP, M_NOWAIT);
	if (nu == NULL) {
		printf(": unable to allocate temp memory\n");
		return (1);
	}
	if (nxe_rom_read_region(sc, NXE_FLASH_USER, nu,
	    sizeof(struct nxe_userinfo)) != 0) {
		printf(": unable to read user info\n");
		goto out;
	}

	sc->sc_fw_major = nu->nu_imageinfo.nim_img_ver_major;
	sc->sc_fw_minor = nu->nu_imageinfo.nim_img_ver_minor;
	sc->sc_fw_build = letoh16(nu->nu_imageinfo.nim_img_ver_build);

	if (sc->sc_fw_major > NXE_VERSION_MAJOR ||
	    sc->sc_fw_major < NXE_VERSION_MAJOR ||
	    sc->sc_fw_minor > NXE_VERSION_MINOR ||
	    sc->sc_fw_minor < NXE_VERSION_MINOR) {
		printf(": firmware %d.%d.%d is unsupported by this driver\n",
		    sc->sc_fw_major, sc->sc_fw_minor, sc->sc_fw_build);
		goto out;
	}

	lladdr = swap64(nu->nu_lladdr[sc->sc_function][0]);
	la = (struct nxe_lladdr *)&lladdr;
	bcopy(la->lladdr, sc->sc_ac.ac_enaddr, ETHER_ADDR_LEN);

	rv = 0;
out:
	free(nu, M_TEMP, 0);
	return (rv);
}

int
nxe_init(struct nxe_softc *sc)
{
	u_int64_t			dva;
	u_int32_t			r;

	/* stop the chip from processing */
	nxe_crb_write(sc, NXE_1_SW_CMD_PRODUCER(sc->sc_function), 0);
	nxe_crb_write(sc, NXE_1_SW_CMD_CONSUMER(sc->sc_function), 0);
	nxe_crb_write(sc, NXE_1_SW_CMD_ADDR_HI, 0);
	nxe_crb_write(sc, NXE_1_SW_CMD_ADDR_LO, 0);

	/*
	 * if this is the first port on the device it needs some special
	 * treatment to get things going.
	 */
	if (sc->sc_function == 0) {
		/* init adapter offload */
		sc->sc_dummy_dma = nxe_dmamem_alloc(sc,
		    NXE_1_SW_DUMMY_ADDR_LEN, PAGE_SIZE);
		if (sc->sc_dummy_dma == NULL) {
			printf(": unable to allocate dummy memory\n");
			return (1);
		}

		bus_dmamap_sync(sc->sc_dmat, NXE_DMA_MAP(sc->sc_dummy_dma),
		    0, NXE_DMA_LEN(sc->sc_dummy_dma), BUS_DMASYNC_PREREAD);

		dva = NXE_DMA_DVA(sc->sc_dummy_dma);
		nxe_crb_write(sc, NXE_1_SW_DUMMY_ADDR_HI, dva >> 32);
		nxe_crb_write(sc, NXE_1_SW_DUMMY_ADDR_LO, dva);

		r = nxe_crb_read(sc, NXE_1_SW_BOOTLD_CONFIG);
		if (r == 0x55555555) {
			r = nxe_crb_read(sc, NXE_1_ROMUSB_SW_RESET);
			if (r != NXE_1_ROMUSB_SW_RESET_BOOT) {
				printf(": unexpected boot state\n");
				goto err;
			}

			/* clear */
			nxe_crb_write(sc, NXE_1_SW_BOOTLD_CONFIG, 0);
		}

		/* start the device up */
		nxe_crb_write(sc, NXE_1_SW_DRIVER_VER, NXE_VERSION);
		nxe_crb_write(sc, NXE_1_GLB_PEGTUNE, NXE_1_GLB_PEGTUNE_DONE);

		/*
		 * the firmware takes a long time to boot, so we'll check
		 * it later on, and again when we want to bring a port up.
		 */
	}

	return (0);

err:
	bus_dmamap_sync(sc->sc_dmat, NXE_DMA_MAP(sc->sc_dummy_dma),
	    0, NXE_DMA_LEN(sc->sc_dummy_dma), BUS_DMASYNC_POSTREAD);
	nxe_dmamem_free(sc, sc->sc_dummy_dma);
	return (1);
}

void
nxe_uninit(struct nxe_softc *sc)
{
	if (sc->sc_function == 0) {
		bus_dmamap_sync(sc->sc_dmat, NXE_DMA_MAP(sc->sc_dummy_dma),
		    0, NXE_DMA_LEN(sc->sc_dummy_dma), BUS_DMASYNC_POSTREAD);
		nxe_dmamem_free(sc, sc->sc_dummy_dma);
	}
}

void
nxe_mountroot(struct device *self)
{
	struct nxe_softc		*sc = (struct nxe_softc *)self;

	DASSERT(sc->sc_window == 1);

	if (!nxe_crb_wait(sc, NXE_1_SW_CMDPEG_STATE, 0xffffffff,
	    NXE_1_SW_CMDPEG_STATE_DONE, 10000)) {
		printf("%s: firmware bootstrap failed, code 0x%08x\n",
		    DEVNAME(sc), nxe_crb_read(sc, NXE_1_SW_CMDPEG_STATE));
		return;
	}

	sc->sc_port = nxe_crb_read(sc, NXE_1_SW_V2P(sc->sc_function));
	if (sc->sc_port == 0x55555555)
		sc->sc_port = sc->sc_function;

	nxe_crb_write(sc, NXE_1_SW_NIC_CAP_HOST, NXE_1_SW_NIC_CAP_PORTINTR);
	nxe_crb_write(sc, NXE_1_SW_MPORT_MODE, NXE_1_SW_MPORT_MODE_MULTI);
	nxe_crb_write(sc, NXE_1_SW_CMDPEG_STATE, NXE_1_SW_CMDPEG_STATE_ACK);

	sc->sc_sensor.type = SENSOR_TEMP;
	strlcpy(sc->sc_sensor_dev.xname, DEVNAME(sc),
	    sizeof(sc->sc_sensor_dev.xname));
	sensor_attach(&sc->sc_sensor_dev, &sc->sc_sensor);
	sensordev_install(&sc->sc_sensor_dev);

	timeout_set(&sc->sc_tick, nxe_tick, sc);
	nxe_tick(sc);
}

void
nxe_tick(void *xsc)
{
	struct nxe_softc		*sc = xsc;
	u_int32_t			temp;
	int				window;
	int				s;

	s = splnet();
	window = nxe_crb_set(sc, 1);
	temp = nxe_crb_read(sc, NXE_1_SW_TEMP);
	nxe_link_state(sc);
	nxe_crb_set(sc, window);
	splx(s);

	sc->sc_sensor.value = NXE_1_SW_TEMP_VAL(temp) * 1000000 + 273150000;
	sc->sc_sensor.flags = 0;

	switch (NXE_1_SW_TEMP_STATE(temp)) {
	case NXE_1_SW_TEMP_STATE_NONE:
		sc->sc_sensor.status = SENSOR_S_UNSPEC;
		break;
	case NXE_1_SW_TEMP_STATE_OK:
		sc->sc_sensor.status = SENSOR_S_OK;
		break;
	case NXE_1_SW_TEMP_STATE_WARN:
		sc->sc_sensor.status = SENSOR_S_WARN;
		break;
	case NXE_1_SW_TEMP_STATE_CRIT:
		/* we should probably bring things down if this is true */
		sc->sc_sensor.status = SENSOR_S_CRIT;
		break;
	default:
		sc->sc_sensor.flags = SENSOR_FUNKNOWN;
		break;
	}

	timeout_add_sec(&sc->sc_tick, 5);
}


struct nxe_ring *
nxe_ring_alloc(struct nxe_softc *sc, size_t desclen, u_int nentries)
{
	struct nxe_ring			*nr;

	nr = malloc(sizeof(struct nxe_ring), M_DEVBUF, M_WAITOK);

	nr->nr_dmamem = nxe_dmamem_alloc(sc, desclen * nentries, PAGE_SIZE);
	if (nr->nr_dmamem == NULL) {
		free(nr, M_DEVBUF, 0);
		return (NULL);
	}

	nr->nr_pos = NXE_DMA_KVA(nr->nr_dmamem);
	nr->nr_slot = 0;
	nr->nr_desclen = desclen;
	nr->nr_nentries = nentries;

	return (nr);
}

void
nxe_ring_sync(struct nxe_softc *sc, struct nxe_ring *nr, int flags)
{
	bus_dmamap_sync(sc->sc_dmat, NXE_DMA_MAP(nr->nr_dmamem),
	    0, NXE_DMA_LEN(nr->nr_dmamem), flags);
}

void
nxe_ring_free(struct nxe_softc *sc, struct nxe_ring *nr)
{
	nxe_dmamem_free(sc, nr->nr_dmamem);
	free(nr, M_DEVBUF, 0);
}

int
nxe_ring_readable(struct nxe_ring *nr, int producer)
{
	nr->nr_ready = producer - nr->nr_slot;
	if (nr->nr_ready < 0)
		nr->nr_ready += nr->nr_nentries;

	return (nr->nr_ready);
}

int
nxe_ring_writeable(struct nxe_ring *nr, int consumer)
{
	nr->nr_ready = consumer - nr->nr_slot;
	if (nr->nr_ready <= 0)
		nr->nr_ready += nr->nr_nentries;

	return (nr->nr_ready);
}

void *
nxe_ring_cur(struct nxe_softc *sc, struct nxe_ring *nr)
{
	return (nr->nr_pos);
}

void *
nxe_ring_next(struct nxe_softc *sc, struct nxe_ring *nr)
{
	if (++nr->nr_slot >= nr->nr_nentries) {
		nr->nr_slot = 0;
		nr->nr_pos = NXE_DMA_KVA(nr->nr_dmamem);
	} else
		nr->nr_pos += nr->nr_desclen;

	nr->nr_ready--;

	return (nr->nr_pos);
}

struct nxe_pkt_list *
nxe_pkt_alloc(struct nxe_softc *sc, u_int npkts, int nsegs)
{
	struct nxe_pkt_list		*npl;
	struct nxe_pkt			*pkt;
	int				i;

	npl = malloc(sizeof(*npl), M_DEVBUF, M_WAITOK | M_ZERO);
	pkt = mallocarray(npkts, sizeof(*pkt), M_DEVBUF, M_WAITOK | M_ZERO);

	npl->npl_pkts = pkt;
	TAILQ_INIT(&npl->npl_free);
	TAILQ_INIT(&npl->npl_used);
	for (i = 0; i < npkts; i++) {
		pkt = &npl->npl_pkts[i];

		pkt->pkt_id = i;
		if (bus_dmamap_create(sc->sc_dmat, NXE_MAX_PKTLEN, nsegs,
		    NXE_MAX_PKTLEN, 0, BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW,
		    &pkt->pkt_dmap) != 0) {
			nxe_pkt_free(sc, npl);
			return (NULL);
		}

		TAILQ_INSERT_TAIL(&npl->npl_free, pkt, pkt_link);
	}

	return (npl);
}

void
nxe_pkt_free(struct nxe_softc *sc, struct nxe_pkt_list *npl)
{
	struct nxe_pkt			*pkt;

	while ((pkt = nxe_pkt_get(npl)) != NULL)
		bus_dmamap_destroy(sc->sc_dmat, pkt->pkt_dmap);

	free(npl->npl_pkts, M_DEVBUF, 0);
	free(npl, M_DEVBUF, sizeof *npl);
}

struct nxe_pkt *
nxe_pkt_get(struct nxe_pkt_list *npl)
{
	struct nxe_pkt			*pkt;

	pkt = TAILQ_FIRST(&npl->npl_free);
	if (pkt != NULL) {
		TAILQ_REMOVE(&npl->npl_free, pkt, pkt_link);
		TAILQ_INSERT_TAIL(&npl->npl_used, pkt, pkt_link);
	}

	return (pkt);
}

void
nxe_pkt_put(struct nxe_pkt_list *npl, struct nxe_pkt *pkt)
{
	TAILQ_REMOVE(&npl->npl_used, pkt, pkt_link);
	TAILQ_INSERT_TAIL(&npl->npl_free, pkt, pkt_link);

}

struct nxe_pkt *
nxe_pkt_used(struct nxe_pkt_list *npl)
{
	return (TAILQ_FIRST(&npl->npl_used));
}

struct nxe_dmamem *
nxe_dmamem_alloc(struct nxe_softc *sc, bus_size_t size, bus_size_t align)
{
	struct nxe_dmamem		*ndm;
	int				nsegs;

	ndm = malloc(sizeof(*ndm), M_DEVBUF, M_WAITOK | M_ZERO);
	ndm->ndm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, &ndm->ndm_map) != 0)
		goto ndmfree;

	if (bus_dmamem_alloc(sc->sc_dmat, size, align, 0, &ndm->ndm_seg, 1,
	    &nsegs, BUS_DMA_WAITOK |BUS_DMA_ZERO) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &ndm->ndm_seg, nsegs, size,
	    &ndm->ndm_kva, BUS_DMA_WAITOK) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, ndm->ndm_map, ndm->ndm_kva, size,
	    NULL, BUS_DMA_WAITOK) != 0)
		goto unmap;

	return (ndm);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, ndm->ndm_kva, size);
free:
	bus_dmamem_free(sc->sc_dmat, &ndm->ndm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, ndm->ndm_map);
ndmfree:
	free(ndm, M_DEVBUF, sizeof *ndm);

	return (NULL);
}

void
nxe_dmamem_free(struct nxe_softc *sc, struct nxe_dmamem *ndm)
{
	bus_dmamem_unmap(sc->sc_dmat, ndm->ndm_kva, ndm->ndm_size);
	bus_dmamem_free(sc->sc_dmat, &ndm->ndm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, ndm->ndm_map);
	free(ndm, M_DEVBUF, sizeof *ndm);
}

u_int32_t
nxe_read(struct nxe_softc *sc, bus_size_t r)
{
	bus_space_barrier(sc->sc_memt, sc->sc_memh, r, 4,
	    BUS_SPACE_BARRIER_READ);
	return (bus_space_read_4(sc->sc_memt, sc->sc_memh, r));
}

void
nxe_write(struct nxe_softc *sc, bus_size_t r, u_int32_t v)
{
	bus_space_write_4(sc->sc_memt, sc->sc_memh, r, v);
	bus_space_barrier(sc->sc_memt, sc->sc_memh, r, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

int
nxe_wait(struct nxe_softc *sc, bus_size_t r, u_int32_t m, u_int32_t v,
    u_int timeout)
{
	while ((nxe_read(sc, r) & m) != v) {
		if (timeout == 0)
			return (0);

		delay(1000);
		timeout--;
	}

	return (1);
}

void
nxe_doorbell(struct nxe_softc *sc, u_int32_t v)
{
	bus_space_write_4(sc->sc_memt, sc->sc_memh, NXE_DB, v);
	bus_space_barrier(sc->sc_memt, sc->sc_memh, NXE_DB, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

int
nxe_crb_set(struct nxe_softc *sc, int window)
{
	int			oldwindow = sc->sc_window;
	u_int32_t		r;

	if (sc->sc_window != window) {
		sc->sc_window = window;

		r = window ? NXE_WIN_CRB_1 : NXE_WIN_CRB_0;
		nxe_write(sc, NXE_WIN_CRB(sc->sc_function), r);

		if (nxe_read(sc, NXE_WIN_CRB(sc->sc_function)) != r)
			printf("%s: crb window hasn't moved\n", DEVNAME(sc));
	}

	return (oldwindow);
}

u_int32_t
nxe_crb_read(struct nxe_softc *sc, bus_size_t r)
{
	bus_space_barrier(sc->sc_memt, sc->sc_crbh, r, 4,
	    BUS_SPACE_BARRIER_READ);
	return (bus_space_read_4(sc->sc_memt, sc->sc_crbh, r));
}

void
nxe_crb_write(struct nxe_softc *sc, bus_size_t r, u_int32_t v)
{
	bus_space_write_4(sc->sc_memt, sc->sc_crbh, r, v);
	bus_space_barrier(sc->sc_memt, sc->sc_crbh, r, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

int
nxe_crb_wait(struct nxe_softc *sc, bus_size_t r, u_int32_t m, u_int32_t v,
    u_int timeout)
{
	while ((nxe_crb_read(sc, r) & m) != v) {
		if (timeout == 0)
			return (0);

		delay(1000);
		timeout--;
	}

	return (1);
}

int
nxe_rom_lock(struct nxe_softc *sc)
{
	if (!nxe_wait(sc, NXE_SEM_ROM_LOCK, 0xffffffff,
	    NXE_SEM_DONE, 10000))
		return (1);
	nxe_crb_write(sc, NXE_1_SW_ROM_LOCK_ID, NXE_1_SW_ROM_LOCK_ID);

	return (0);
}

void
nxe_rom_unlock(struct nxe_softc *sc)
{
	nxe_read(sc, NXE_SEM_ROM_UNLOCK);
}

int
nxe_rom_read(struct nxe_softc *sc, u_int32_t r, u_int32_t *v)
{
	int			rv = 1;

	DASSERT(sc->sc_window == 1);

	if (nxe_rom_lock(sc) != 0)
		return (1);

	/* set the rom address */
	nxe_crb_write(sc, NXE_1_ROM_ADDR, r);

	/* set the xfer len */
	nxe_crb_write(sc, NXE_1_ROM_ABYTE_CNT, 3);
	delay(100); /* used to prevent bursting on the chipset */
	nxe_crb_write(sc, NXE_1_ROM_DBYTE_CNT, 0);

	/* set opcode and wait for completion */
	nxe_crb_write(sc, NXE_1_ROM_OPCODE, NXE_1_ROM_OPCODE_READ);
	if (!nxe_crb_wait(sc, NXE_1_ROMUSB_STATUS, NXE_1_ROMUSB_STATUS_DONE,
	    NXE_1_ROMUSB_STATUS_DONE, 100))
		goto err;

	/* reset counters */
	nxe_crb_write(sc, NXE_1_ROM_ABYTE_CNT, 0);
	delay(100);
	nxe_crb_write(sc, NXE_1_ROM_DBYTE_CNT, 0);

	*v = nxe_crb_read(sc, NXE_1_ROM_RDATA);

	rv = 0;
err:
	nxe_rom_unlock(sc);
	return (rv);
}

int
nxe_rom_read_region(struct nxe_softc *sc, u_int32_t r, void *buf,
    size_t buflen)
{
	u_int32_t		*databuf = buf;
	int			i;

#ifdef NXE_DEBUG
	if ((buflen % 4) != 0)
		panic("nxe_read_rom_region: buflen is wrong (%d)", buflen);
#endif

	buflen = buflen / 4;
	for (i = 0; i < buflen; i++) {
		if (nxe_rom_read(sc, r, &databuf[i]) != 0)
			return (1);

		r += sizeof(u_int32_t);
	}

	return (0);
}
