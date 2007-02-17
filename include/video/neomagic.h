/*
 * linux/include/video/neo_reg.h -- NeoMagic Framebuffer Driver
 *
 * Copyright (c) 2001  Denis Oliver Kropp <dok@convergence.de>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 */

#define NEO_BS0_BLT_BUSY        0x00000001
#define NEO_BS0_FIFO_AVAIL      0x00000002
#define NEO_BS0_FIFO_PEND       0x00000004

#define NEO_BC0_DST_Y_DEC       0x00000001
#define NEO_BC0_X_DEC           0x00000002
#define NEO_BC0_SRC_TRANS       0x00000004
#define NEO_BC0_SRC_IS_FG       0x00000008
#define NEO_BC0_SRC_Y_DEC       0x00000010
#define NEO_BC0_FILL_PAT        0x00000020
#define NEO_BC0_SRC_MONO        0x00000040
#define NEO_BC0_SYS_TO_VID      0x00000080

#define NEO_BC1_DEPTH8          0x00000100
#define NEO_BC1_DEPTH16         0x00000200
#define NEO_BC1_X_320           0x00000400
#define NEO_BC1_X_640           0x00000800
#define NEO_BC1_X_800           0x00000c00
#define NEO_BC1_X_1024          0x00001000
#define NEO_BC1_X_1152          0x00001400
#define NEO_BC1_X_1280          0x00001800
#define NEO_BC1_X_1600          0x00001c00
#define NEO_BC1_DST_TRANS       0x00002000
#define NEO_BC1_MSTR_BLT        0x00004000
#define NEO_BC1_FILTER_Z        0x00008000

#define NEO_BC2_WR_TR_DST       0x00800000

#define NEO_BC3_SRC_XY_ADDR     0x01000000
#define NEO_BC3_DST_XY_ADDR     0x02000000
#define NEO_BC3_CLIP_ON         0x04000000
#define NEO_BC3_FIFO_EN         0x08000000
#define NEO_BC3_BLT_ON_ADDR     0x10000000
#define NEO_BC3_SKIP_MAPPING    0x80000000

#define NEO_MODE1_DEPTH8        0x0100
#define NEO_MODE1_DEPTH16       0x0200
#define NEO_MODE1_DEPTH24       0x0300
#define NEO_MODE1_X_320         0x0400
#define NEO_MODE1_X_640         0x0800
#define NEO_MODE1_X_800         0x0c00
#define NEO_MODE1_X_1024        0x1000
#define NEO_MODE1_X_1152        0x1400
#define NEO_MODE1_X_1280        0x1800
#define NEO_MODE1_X_1600        0x1c00
#define NEO_MODE1_BLT_ON_ADDR   0x2000

/* These are offseted in MMIO space by par->CursorOff */
#define NEOREG_CURSCNTL		0x00
#define NEOREG_CURSX		0x04
#define NEOREG_CURSY		0x08
#define NEOREG_CURSBGCOLOR	0x0C
#define NEOREG_CURSFGCOLOR	0x10
#define NEOREG_CURSMEMPOS	0x14

#define NEO_CURS_DISABLE	0x00000000
#define NEO_CURS_ENABLE		0x00000001
#define NEO_ICON64_ENABLE	0x00000008
#define NEO_ICON128_ENABLE	0x0000000C
#define NEO_ICON_BLANK		0x00000010

#define NEO_GR01_SUPPRESS_VSYNC 0x10
#define NEO_GR01_SUPPRESS_HSYNC 0x20

#ifdef __KERNEL__

#ifdef NEOFB_DEBUG
# define DBG(x)		printk (KERN_DEBUG "neofb: %s\n", (x));
#else
# define DBG(x)
#endif

#define PCI_CHIP_NM2070 0x0001
#define PCI_CHIP_NM2090 0x0002
#define PCI_CHIP_NM2093 0x0003
#define PCI_CHIP_NM2097 0x0083
#define PCI_CHIP_NM2160 0x0004
#define PCI_CHIP_NM2200 0x0005
#define PCI_CHIP_NM2230 0x0025
#define PCI_CHIP_NM2360 0x0006
#define PCI_CHIP_NM2380 0x0016


struct xtimings {
	unsigned int pixclock;
	unsigned int HDisplay;
	unsigned int HSyncStart;
	unsigned int HSyncEnd;
	unsigned int HTotal;
	unsigned int VDisplay;
	unsigned int VSyncStart;
	unsigned int VSyncEnd;
	unsigned int VTotal;
	unsigned int sync;
	int dblscan;
	int interlaced;
};


/* --------------------------------------------------------------------- */

typedef volatile struct {
	__u32 bltStat;
	__u32 bltCntl;
	__u32 xpColor;
	__u32 fgColor;
	__u32 bgColor;
	__u32 pitch;
	__u32 clipLT;
	__u32 clipRB;
	__u32 srcBitOffset;
	__u32 srcStart;
	__u32 reserved0;
	__u32 dstStart;
	__u32 xyExt;

	__u32 reserved1[19];

	__u32 pageCntl;
	__u32 pageBase;
	__u32 postBase;
	__u32 postPtr;
	__u32 dataPtr;
} Neo2200;

#define MMIO_SIZE 0x200000

#define NEO_EXT_CR_MAX 0x85
#define NEO_EXT_GR_MAX 0xC7

struct neofb_par {
	struct vgastate state;
	struct mutex open_lock;
	unsigned int ref_count;

	unsigned char MiscOutReg;	/* Misc */
	unsigned char CRTC[25];		/* Crtc Controller */
	unsigned char Sequencer[5];	/* Video Sequencer */
	unsigned char Graphics[9];	/* Video Graphics */
	unsigned char Attribute[21];	/* Video Atribute */

	unsigned char GeneralLockReg;
	unsigned char ExtCRTDispAddr;
	unsigned char ExtCRTOffset;
	unsigned char SysIfaceCntl1;
	unsigned char SysIfaceCntl2;
	unsigned char ExtColorModeSelect;
	unsigned char biosMode;

	unsigned char PanelDispCntlReg1;
	unsigned char PanelDispCntlReg2;
	unsigned char PanelDispCntlReg3;
	unsigned char PanelDispCntlRegRead;
	unsigned char PanelVertCenterReg1;
	unsigned char PanelVertCenterReg2;
	unsigned char PanelVertCenterReg3;
	unsigned char PanelVertCenterReg4;
	unsigned char PanelVertCenterReg5;
	unsigned char PanelHorizCenterReg1;
	unsigned char PanelHorizCenterReg2;
	unsigned char PanelHorizCenterReg3;
	unsigned char PanelHorizCenterReg4;
	unsigned char PanelHorizCenterReg5;

	int ProgramVCLK;
	unsigned char VCLK3NumeratorLow;
	unsigned char VCLK3NumeratorHigh;
	unsigned char VCLK3Denominator;
	unsigned char VerticalExt;

#ifdef CONFIG_MTRR
	int mtrr;
#endif
	u8 __iomem *mmio_vbase;
	u8 cursorOff;
	u8 *cursorPad;		/* Must die !! */

	Neo2200 __iomem *neo2200;

	/* Panels size */
	int NeoPanelWidth;
	int NeoPanelHeight;

	int maxClock;

	int pci_burst;
	int lcd_stretch;
	int internal_display;
	int external_display;
	int libretto;
	u32 palette[16];
};

typedef struct {
	int x_res;
	int y_res;
	int mode;
} biosMode;

#endif
