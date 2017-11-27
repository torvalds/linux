/* SPDX-License-Identifier: GPL-2.0 */
/* $Id: newport.h,v 1.5 1999/08/04 06:01:51 ulfc Exp $
 *
 * newport.h: Defines and register layout for NEWPORT graphics
 *            hardware.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 * 
 * Ulf Carlsson - Compatibility with the IRIX structures added
 */

#ifndef _SGI_NEWPORT_H
#define _SGI_NEWPORT_H


typedef volatile unsigned int npireg_t;

union npfloat {
	volatile float flt;
	npireg_t       word;
};

typedef union npfloat npfreg_t;

union np_dcb {
	npireg_t byword;
	struct { volatile unsigned short s0, s1; } byshort;
	struct { volatile unsigned char b0, b1, b2, b3; } bybytes;
};

struct newport_rexregs {
	npireg_t drawmode1;      /* GL extra mode bits */
	
#define DM1_PLANES         0x00000007
#define    DM1_NOPLANES    0x00000000
#define    DM1_RGBPLANES   0x00000001
#define    DM1_RGBAPLANES  0x00000002
#define    DM1_OLAYPLANES  0x00000004
#define    DM1_PUPPLANES   0x00000005
#define    DM1_CIDPLANES   0x00000006
	
#define NPORT_DMODE1_DDMASK      0x00000018
#define NPORT_DMODE1_DD4         0x00000000
#define NPORT_DMODE1_DD8         0x00000008
#define NPORT_DMODE1_DD12        0x00000010
#define NPORT_DMODE1_DD24        0x00000018
#define NPORT_DMODE1_DSRC        0x00000020
#define NPORT_DMODE1_YFLIP       0x00000040
#define NPORT_DMODE1_RWPCKD      0x00000080
#define NPORT_DMODE1_HDMASK      0x00000300
#define NPORT_DMODE1_HD4         0x00000000
#define NPORT_DMODE1_HD8         0x00000100
#define NPORT_DMODE1_HD12        0x00000200
#define NPORT_DMODE1_HD32        0x00000300
#define NPORT_DMODE1_RWDBL       0x00000400
#define NPORT_DMODE1_ESWAP       0x00000800 /* Endian swap */
#define NPORT_DMODE1_CCMASK      0x00007000
#define NPORT_DMODE1_CCLT        0x00001000
#define NPORT_DMODE1_CCEQ        0x00002000
#define NPORT_DMODE1_CCGT        0x00004000
#define NPORT_DMODE1_RGBMD       0x00008000
#define NPORT_DMODE1_DENAB       0x00010000 /* Dither enable */
#define NPORT_DMODE1_FCLR        0x00020000 /* Fast clear */
#define NPORT_DMODE1_BENAB       0x00040000 /* Blend enable */
#define NPORT_DMODE1_SFMASK      0x00380000
#define NPORT_DMODE1_SF0         0x00000000
#define NPORT_DMODE1_SF1         0x00080000
#define NPORT_DMODE1_SFDC        0x00100000
#define NPORT_DMODE1_SFMDC       0x00180000
#define NPORT_DMODE1_SFSA        0x00200000
#define NPORT_DMODE1_SFMSA       0x00280000
#define NPORT_DMODE1_DFMASK      0x01c00000
#define NPORT_DMODE1_DF0         0x00000000
#define NPORT_DMODE1_DF1         0x00400000
#define NPORT_DMODE1_DFSC        0x00800000
#define NPORT_DMODE1_DFMSC       0x00c00000
#define NPORT_DMODE1_DFSA        0x01000000
#define NPORT_DMODE1_DFMSA       0x01400000
#define NPORT_DMODE1_BBENAB      0x02000000 /* Back blend enable */
#define NPORT_DMODE1_PFENAB      0x04000000 /* Pre-fetch enable */
#define NPORT_DMODE1_ABLEND      0x08000000 /* Alpha blend */
#define NPORT_DMODE1_LOMASK      0xf0000000
#define NPORT_DMODE1_LOZERO      0x00000000
#define NPORT_DMODE1_LOAND       0x10000000
#define NPORT_DMODE1_LOANDR      0x20000000
#define NPORT_DMODE1_LOSRC       0x30000000
#define NPORT_DMODE1_LOANDI      0x40000000
#define NPORT_DMODE1_LODST       0x50000000
#define NPORT_DMODE1_LOXOR       0x60000000
#define NPORT_DMODE1_LOOR        0x70000000
#define NPORT_DMODE1_LONOR       0x80000000
#define NPORT_DMODE1_LOXNOR      0x90000000
#define NPORT_DMODE1_LONDST      0xa0000000
#define NPORT_DMODE1_LOORR       0xb0000000
#define NPORT_DMODE1_LONSRC      0xc0000000
#define NPORT_DMODE1_LOORI       0xd0000000
#define NPORT_DMODE1_LONAND      0xe0000000
#define NPORT_DMODE1_LOONE       0xf0000000

	npireg_t drawmode0;      /* REX command register */

	/* These bits define the graphics opcode being performed. */
#define NPORT_DMODE0_OPMASK   0x00000003 /* Opcode mask */
#define NPORT_DMODE0_NOP      0x00000000 /* No operation */
#define NPORT_DMODE0_RD       0x00000001 /* Read operation */
#define NPORT_DMODE0_DRAW     0x00000002 /* Draw operation */
#define NPORT_DMODE0_S2S      0x00000003 /* Screen to screen operation */

	/* The following decide what addressing mode(s) are to be used */
#define NPORT_DMODE0_AMMASK   0x0000001c /* Address mode mask */
#define NPORT_DMODE0_SPAN     0x00000000 /* Spanning address mode */
#define NPORT_DMODE0_BLOCK    0x00000004 /* Block address mode */
#define NPORT_DMODE0_ILINE    0x00000008 /* Iline address mode */
#define NPORT_DMODE0_FLINE    0x0000000c /* Fline address mode */
#define NPORT_DMODE0_ALINE    0x00000010 /* Aline address mode */
#define NPORT_DMODE0_TLINE    0x00000014 /* Tline address mode */
#define NPORT_DMODE0_BLINE    0x00000018 /* Bline address mode */

	/* And now some misc. operation control bits. */
#define NPORT_DMODE0_DOSETUP  0x00000020
#define NPORT_DMODE0_CHOST    0x00000040
#define NPORT_DMODE0_AHOST    0x00000080
#define NPORT_DMODE0_STOPX    0x00000100
#define NPORT_DMODE0_STOPY    0x00000200
#define NPORT_DMODE0_SK1ST    0x00000400
#define NPORT_DMODE0_SKLST    0x00000800
#define NPORT_DMODE0_ZPENAB   0x00001000
#define NPORT_DMODE0_LISPENAB 0x00002000
#define NPORT_DMODE0_LISLST   0x00004000
#define NPORT_DMODE0_L32      0x00008000
#define NPORT_DMODE0_ZOPQ     0x00010000
#define NPORT_DMODE0_LISOPQ   0x00020000
#define NPORT_DMODE0_SHADE    0x00040000
#define NPORT_DMODE0_LRONLY   0x00080000
#define NPORT_DMODE0_XYOFF    0x00100000
#define NPORT_DMODE0_CLAMP    0x00200000
#define NPORT_DMODE0_ENDPF    0x00400000
#define NPORT_DMODE0_YSTR     0x00800000

	npireg_t lsmode;      /* Mode for line stipple ops */
	npireg_t lspattern;   /* Pattern for line stipple ops */
	npireg_t lspatsave;   /* Backup save pattern */
	npireg_t zpattern;    /* Pixel zpattern */
	npireg_t colorback;   /* Background color */
	npireg_t colorvram;   /* Clear color for fast vram */
	npireg_t alpharef;    /* Reference value for afunctions */
	unsigned int pad0;
	npireg_t smask0x;     /* Window GL relative screen mask 0 */
	npireg_t smask0y;     /* Window GL relative screen mask 0 */
	npireg_t _setup;
	npireg_t _stepz;
	npireg_t _lsrestore;
	npireg_t _lssave;

	unsigned int _pad1[0x30];

	/* Iterators, full state for context switch */
	npfreg_t _xstart;	/* X-start point (current) */
	npfreg_t _ystart;	/* Y-start point (current) */
	npfreg_t _xend;		/* x-end point */
	npfreg_t _yend;		/* y-end point */
	npireg_t xsave;		/* copy of xstart integer value for BLOCk addressing MODE */
	npireg_t xymove;	/* x.y offset from xstart, ystart for relative operations */
	npfreg_t bresd;
	npfreg_t bress1;
	npireg_t bresoctinc1;
	volatile int bresrndinc2;
	npireg_t brese1;
	npireg_t bress2;
	npireg_t aweight0;
	npireg_t aweight1;
	npfreg_t xstartf;
	npfreg_t ystartf;
	npfreg_t xendf;
	npfreg_t yendf;
	npireg_t xstarti;
	npfreg_t xendf1;
	npireg_t xystarti;
	npireg_t xyendi;
	npireg_t xstartendi;

	unsigned int _unused2[0x29];

	npfreg_t colorred;
	npfreg_t coloralpha;
	npfreg_t colorgrn;
	npfreg_t colorblue;
	npfreg_t slopered;
	npfreg_t slopealpha;
	npfreg_t slopegrn;
	npfreg_t slopeblue;
	npireg_t wrmask;
	npireg_t colori;
	npfreg_t colorx;
	npfreg_t slopered1;
	npireg_t hostrw0;
	npireg_t hostrw1;
	npireg_t dcbmode;
#define NPORT_DMODE_WMASK   0x00000003
#define NPORT_DMODE_W4      0x00000000
#define NPORT_DMODE_W1      0x00000001
#define NPORT_DMODE_W2      0x00000002
#define NPORT_DMODE_W3      0x00000003
#define NPORT_DMODE_EDPACK  0x00000004
#define NPORT_DMODE_ECINC   0x00000008
#define NPORT_DMODE_CMASK   0x00000070
#define NPORT_DMODE_AMASK   0x00000780
#define NPORT_DMODE_AVC2    0x00000000
#define NPORT_DMODE_ACMALL  0x00000080
#define NPORT_DMODE_ACM0    0x00000100
#define NPORT_DMODE_ACM1    0x00000180
#define NPORT_DMODE_AXMALL  0x00000200
#define NPORT_DMODE_AXM0    0x00000280
#define NPORT_DMODE_AXM1    0x00000300
#define NPORT_DMODE_ABT     0x00000380
#define NPORT_DMODE_AVCC1   0x00000400
#define NPORT_DMODE_AVAB1   0x00000480
#define NPORT_DMODE_ALG3V0  0x00000500
#define NPORT_DMODE_A1562   0x00000580
#define NPORT_DMODE_ESACK   0x00000800
#define NPORT_DMODE_EASACK  0x00001000
#define NPORT_DMODE_CWMASK  0x0003e000
#define NPORT_DMODE_CHMASK  0x007c0000
#define NPORT_DMODE_CSMASK  0x0f800000
#define NPORT_DMODE_SENDIAN 0x10000000

	unsigned int _unused3;

	union np_dcb dcbdata0;
	npireg_t dcbdata1;
};

struct newport_cregs {
	npireg_t smask1x;
	npireg_t smask1y;
	npireg_t smask2x;
	npireg_t smask2y;
	npireg_t smask3x;
	npireg_t smask3y;
	npireg_t smask4x;
	npireg_t smask4y;
	npireg_t topscan;
	npireg_t xywin;
	npireg_t clipmode;
#define NPORT_CMODE_SM0   0x00000001
#define NPORT_CMODE_SM1   0x00000002
#define NPORT_CMODE_SM2   0x00000004
#define NPORT_CMODE_SM3   0x00000008
#define NPORT_CMODE_SM4   0x00000010
#define NPORT_CMODE_CMSK  0x00001e00

	unsigned int _unused0;
	unsigned int config;
#define NPORT_CFG_G32MD   0x00000001
#define NPORT_CFG_BWIDTH  0x00000002
#define NPORT_CFG_ERCVR   0x00000004
#define NPORT_CFG_BDMSK   0x00000078
#define NPORT_CFG_BFAINT  0x00000080
#define NPORT_CFG_GDMSK   0x00001f80
#define NPORT_CFG_GD0     0x00000100
#define NPORT_CFG_GD1     0x00000200
#define NPORT_CFG_GD2     0x00000400
#define NPORT_CFG_GD3     0x00000800
#define NPORT_CFG_GD4     0x00001000
#define NPORT_CFG_GFAINT  0x00002000
#define NPORT_CFG_TOMSK   0x0001c000
#define NPORT_CFG_VRMSK   0x000e0000
#define NPORT_CFG_FBTYP   0x00100000

	npireg_t _unused1;
	npireg_t status;
#define NPORT_STAT_VERS   0x00000007
#define NPORT_STAT_GBUSY  0x00000008
#define NPORT_STAT_BBUSY  0x00000010
#define NPORT_STAT_VRINT  0x00000020
#define NPORT_STAT_VIDINT 0x00000040
#define NPORT_STAT_GLMSK  0x00001f80
#define NPORT_STAT_BLMSK  0x0007e000
#define NPORT_STAT_BFIRQ  0x00080000
#define NPORT_STAT_GFIRQ  0x00100000

	npireg_t ustatus;
	npireg_t dcbreset;
};

struct newport_regs {
	struct newport_rexregs set;
	unsigned int _unused0[0x16e];
	struct newport_rexregs go;
	unsigned int _unused1[0x22e];
	struct newport_cregs cset;
	unsigned int _unused2[0x1ef];
	struct newport_cregs cgo;
};

typedef struct {
	unsigned int drawmode1;
	unsigned int drawmode0;
	unsigned int lsmode;   
	unsigned int lspattern;
	unsigned int lspatsave;
	unsigned int zpattern; 
	unsigned int colorback;
	unsigned int colorvram;
	unsigned int alpharef; 
	unsigned int smask0x;  
	unsigned int smask0y;  
	unsigned int _xstart;  
	unsigned int _ystart;  
	unsigned int _xend;    
	unsigned int _yend;    
	unsigned int xsave;    
	unsigned int xymove;   
	unsigned int bresd;    
	unsigned int bress1;   
	unsigned int bresoctinc1;
	unsigned int bresrndinc2;
	unsigned int brese1;     
	unsigned int bress2;     
	
	unsigned int aweight0;    
	unsigned int aweight1;    
	unsigned int colorred;    
	unsigned int coloralpha;  
	unsigned int colorgrn;    
	unsigned int colorblue;   
	unsigned int slopered;    
	unsigned int slopealpha;  
	unsigned int slopegrn;    
	unsigned int slopeblue;   
	unsigned int wrmask;      
	unsigned int hostrw0;     
	unsigned int hostrw1;     
	
        /* configregs */
	
	unsigned int smask1x;    
	unsigned int smask1y;    
	unsigned int smask2x;    
	unsigned int smask2y;    
	unsigned int smask3x;    
	unsigned int smask3y;    
	unsigned int smask4x;    
	unsigned int smask4y;    
	unsigned int topscan;    
	unsigned int xywin;      
	unsigned int clipmode;   
	unsigned int config;     
	
        /* dcb registers */
	unsigned int dcbmode;   
	unsigned int dcbdata0;  
	unsigned int dcbdata1;
} newport_ctx;

/* Reading/writing VC2 registers. */
#define VC2_REGADDR_INDEX      0x00000000
#define VC2_REGADDR_IREG       0x00000010
#define VC2_REGADDR_RAM        0x00000030
#define VC2_PROTOCOL           (NPORT_DMODE_EASACK | 0x00800000 | 0x00040000)

#define VC2_VLINET_ADDR        0x000
#define VC2_VFRAMET_ADDR       0x400
#define VC2_CGLYPH_ADDR        0x500

/* Now the Indexed registers of the VC2. */
#define VC2_IREG_VENTRY        0x00
#define VC2_IREG_CENTRY        0x01
#define VC2_IREG_CURSX         0x02
#define VC2_IREG_CURSY         0x03
#define VC2_IREG_CCURSX        0x04
#define VC2_IREG_DENTRY        0x05
#define VC2_IREG_SLEN          0x06
#define VC2_IREG_RADDR         0x07
#define VC2_IREG_VFPTR         0x08
#define VC2_IREG_VLSPTR        0x09
#define VC2_IREG_VLIR          0x0a
#define VC2_IREG_VLCTR         0x0b
#define VC2_IREG_CTPTR         0x0c
#define VC2_IREG_WCURSY        0x0d
#define VC2_IREG_DFPTR         0x0e
#define VC2_IREG_DLTPTR        0x0f
#define VC2_IREG_CONTROL       0x10
#define VC2_IREG_CONFIG        0x20

static inline void newport_vc2_set(struct newport_regs *regs,
				   unsigned char vc2ireg,
				   unsigned short val)
{
	regs->set.dcbmode = (NPORT_DMODE_AVC2 | VC2_REGADDR_INDEX | NPORT_DMODE_W3 |
			   NPORT_DMODE_ECINC | VC2_PROTOCOL);
	regs->set.dcbdata0.byword = (vc2ireg << 24) | (val << 8);
}

static inline unsigned short newport_vc2_get(struct newport_regs *regs,
					     unsigned char vc2ireg)
{
	regs->set.dcbmode = (NPORT_DMODE_AVC2 | VC2_REGADDR_INDEX | NPORT_DMODE_W1 |
			   NPORT_DMODE_ECINC | VC2_PROTOCOL);
	regs->set.dcbdata0.bybytes.b3 = vc2ireg;
	regs->set.dcbmode = (NPORT_DMODE_AVC2 | VC2_REGADDR_IREG | NPORT_DMODE_W2 |
			   NPORT_DMODE_ECINC | VC2_PROTOCOL);
	return regs->set.dcbdata0.byshort.s1;
}

/* VC2 Control register bits */
#define VC2_CTRL_EVIRQ     0x0001
#define VC2_CTRL_EDISP     0x0002
#define VC2_CTRL_EVIDEO    0x0004
#define VC2_CTRL_EDIDS     0x0008
#define VC2_CTRL_ECURS     0x0010
#define VC2_CTRL_EGSYNC    0x0020
#define VC2_CTRL_EILACE    0x0040
#define VC2_CTRL_ECDISP    0x0080
#define VC2_CTRL_ECCURS    0x0100
#define VC2_CTRL_ECG64     0x0200
#define VC2_CTRL_GLSEL     0x0400

/* Controlling the color map on NEWPORT. */
#define NCMAP_REGADDR_AREG   0x00000000
#define NCMAP_REGADDR_ALO    0x00000000
#define NCMAP_REGADDR_AHI    0x00000010
#define NCMAP_REGADDR_PBUF   0x00000020
#define NCMAP_REGADDR_CREG   0x00000030
#define NCMAP_REGADDR_SREG   0x00000040
#define NCMAP_REGADDR_RREG   0x00000060
#define NCMAP_PROTOCOL       (0x00008000 | 0x00040000 | 0x00800000)

static __inline__ void newport_cmap_setaddr(struct newport_regs *regs,
					unsigned short addr)
{
	regs->set.dcbmode = (NPORT_DMODE_ACMALL | NCMAP_PROTOCOL |
			   NPORT_DMODE_SENDIAN | NPORT_DMODE_ECINC |
			   NCMAP_REGADDR_AREG | NPORT_DMODE_W2);
	regs->set.dcbdata0.byshort.s1 = addr;
	regs->set.dcbmode = (NPORT_DMODE_ACMALL | NCMAP_PROTOCOL |
			   NCMAP_REGADDR_PBUF | NPORT_DMODE_W3);
}

static __inline__ void newport_cmap_setrgb(struct newport_regs *regs,
				       unsigned char red,
				       unsigned char green,
				       unsigned char blue)
{
	regs->set.dcbdata0.byword =
		(red << 24) |
		(green << 16) |
		(blue << 8);
}

/* Miscellaneous NEWPORT routines. */
#define BUSY_TIMEOUT 100000
static __inline__ int newport_wait(struct newport_regs *regs)
{
	int t = BUSY_TIMEOUT;

	while (--t)
		if (!(regs->cset.status & NPORT_STAT_GBUSY))
			break;
	return !t;
}

static __inline__ int newport_bfwait(struct newport_regs *regs)
{
	int t = BUSY_TIMEOUT;

	while (--t)
		if(!(regs->cset.status & NPORT_STAT_BBUSY))
			break;
	return !t;
}

/*
 * DCBMODE register defines:
 */

/* Width of the data being transferred for each DCBDATA[01] word */
#define DCB_DATAWIDTH_4 0x0
#define DCB_DATAWIDTH_1 0x1
#define DCB_DATAWIDTH_2 0x2
#define DCB_DATAWIDTH_3 0x3

/* If set, all of DCBDATA will be moved, otherwise only DATAWIDTH bytes */
#define DCB_ENDATAPACK   (1 << 2)

/* Enables DCBCRS auto increment after each DCB transfer */
#define DCB_ENCRSINC     (1 << 3)

/* shift for accessing the control register select address (DBCCRS, 3 bits) */
#define DCB_CRS_SHIFT    4

/* DCBADDR (4 bits): display bus slave address */
#define DCB_ADDR_SHIFT   7
#define DCB_VC2          (0 <<  DCB_ADDR_SHIFT)
#define DCB_CMAP_ALL     (1 <<  DCB_ADDR_SHIFT)
#define DCB_CMAP0        (2 <<  DCB_ADDR_SHIFT)
#define DCB_CMAP1        (3 <<  DCB_ADDR_SHIFT)
#define DCB_XMAP_ALL     (4 <<  DCB_ADDR_SHIFT)
#define DCB_XMAP0        (5 <<  DCB_ADDR_SHIFT)
#define DCB_XMAP1        (6 <<  DCB_ADDR_SHIFT)
#define DCB_BT445        (7 <<  DCB_ADDR_SHIFT)
#define DCB_VCC1         (8 <<  DCB_ADDR_SHIFT)
#define DCB_VAB1         (9 <<  DCB_ADDR_SHIFT)
#define DCB_LG3_BDVERS0  (10 << DCB_ADDR_SHIFT)
#define DCB_LG3_ICS1562  (11 << DCB_ADDR_SHIFT)
#define DCB_RESERVED     (15 << DCB_ADDR_SHIFT)

/* DCB protocol ack types */
#define DCB_ENSYNCACK    (1 << 11)
#define DCB_ENASYNCACK   (1 << 12)

#define DCB_CSWIDTH_SHIFT 13
#define DCB_CSHOLD_SHIFT  18
#define DCB_CSSETUP_SHIFT 23

/* XMAP9 specific defines */
/*   XMAP9 -- registers as seen on the DCBMODE register*/
#   define XM9_CRS_CONFIG            (0 << DCB_CRS_SHIFT)
#       define XM9_PUPMODE           (1 << 0)
#       define XM9_ODD_PIXEL         (1 << 1)
#       define XM9_8_BITPLANES       (1 << 2)
#       define XM9_SLOW_DCB          (1 << 3)
#       define XM9_VIDEO_RGBMAP_MASK (3 << 4)
#       define XM9_EXPRESS_VIDEO     (1 << 6)
#       define XM9_VIDEO_OPTION      (1 << 7)
#   define XM9_CRS_REVISION          (1 << DCB_CRS_SHIFT)
#   define XM9_CRS_FIFO_AVAIL        (2 << DCB_CRS_SHIFT)
#       define XM9_FIFO_0_AVAIL      0
#       define XM9_FIFO_1_AVAIL      1
#       define XM9_FIFO_2_AVAIL      3
#       define XM9_FIFO_3_AVAIL      2
#       define XM9_FIFO_FULL         XM9_FIFO_0_AVAIL
#       define XM9_FIFO_EMPTY        XM9_FIFO_3_AVAIL
#   define XM9_CRS_CURS_CMAP_MSB     (3 << DCB_CRS_SHIFT)
#   define XM9_CRS_PUP_CMAP_MSB      (4 << DCB_CRS_SHIFT)
#   define XM9_CRS_MODE_REG_DATA     (5 << DCB_CRS_SHIFT)
#   define XM9_CRS_MODE_REG_INDEX    (7 << DCB_CRS_SHIFT)


#define DCB_CYCLES(setup,hold,width)                \
                  ((hold << DCB_CSHOLD_SHIFT)  |    \
		   (setup << DCB_CSSETUP_SHIFT)|    \
		   (width << DCB_CSWIDTH_SHIFT))

#define W_DCB_XMAP9_PROTOCOL       DCB_CYCLES (2, 1, 0)
#define WSLOW_DCB_XMAP9_PROTOCOL   DCB_CYCLES (5, 5, 0)
#define WAYSLOW_DCB_XMAP9_PROTOCOL DCB_CYCLES (12, 12, 0)
#define R_DCB_XMAP9_PROTOCOL       DCB_CYCLES (2, 1, 3)

static __inline__ void
xmap9FIFOWait (struct newport_regs *rex)
{
        rex->set.dcbmode = DCB_XMAP0 | XM9_CRS_FIFO_AVAIL |
		DCB_DATAWIDTH_1 | R_DCB_XMAP9_PROTOCOL;
        newport_bfwait (rex);
	
        while ((rex->set.dcbdata0.bybytes.b3 & 3) != XM9_FIFO_EMPTY)
		;
}

static __inline__ void
xmap9SetModeReg (struct newport_regs *rex, unsigned int modereg, unsigned int data24, int cfreq)
{
        if (cfreq > 119)
            rex->set.dcbmode = DCB_XMAP_ALL | XM9_CRS_MODE_REG_DATA |
                        DCB_DATAWIDTH_4 | W_DCB_XMAP9_PROTOCOL;
        else if (cfreq > 59)
            rex->set.dcbmode = DCB_XMAP_ALL | XM9_CRS_MODE_REG_DATA |
		    DCB_DATAWIDTH_4 | WSLOW_DCB_XMAP9_PROTOCOL;    
        else
            rex->set.dcbmode = DCB_XMAP_ALL | XM9_CRS_MODE_REG_DATA |
                        DCB_DATAWIDTH_4 | WAYSLOW_DCB_XMAP9_PROTOCOL; 
        rex->set.dcbdata0.byword = ((modereg) << 24) | (data24 & 0xffffff);
}

#define BT445_PROTOCOL		DCB_CYCLES(1,1,3)

#define BT445_CSR_ADDR_REG	(0 << DCB_CRS_SHIFT)
#define BT445_CSR_REVISION	(2 << DCB_CRS_SHIFT)

#define BT445_REVISION_REG	0x01

#endif /* !(_SGI_NEWPORT_H) */

