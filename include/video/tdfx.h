#ifndef _TDFX_H
#define _TDFX_H

/* membase0 register offsets */
#define STATUS		0x00
#define PCIINIT0	0x04
#define SIPMONITOR	0x08
#define LFBMEMORYCONFIG	0x0c
#define MISCINIT0	0x10
#define MISCINIT1	0x14
#define DRAMINIT0	0x18
#define DRAMINIT1	0x1c
#define AGPINIT		0x20
#define TMUGBEINIT	0x24
#define VGAINIT0	0x28
#define VGAINIT1	0x2c
#define DRAMCOMMAND	0x30
#define DRAMDATA	0x34
/* reserved	0x38 */
/* reserved	0x3c */
#define PLLCTRL0	0x40
#define PLLCTRL1	0x44
#define PLLCTRL2	0x48
#define DACMODE		0x4c
#define DACADDR		0x50
#define DACDATA		0x54
#define RGBMAXDELTA	0x58
#define VIDPROCCFG	0x5c
#define HWCURPATADDR	0x60
#define HWCURLOC	0x64
#define HWCURC0		0x68
#define HWCURC1		0x6c
#define VIDINFORMAT	0x70
#define VIDINSTATUS	0x74
#define VIDSERPARPORT	0x78
#define VIDINXDELTA	0x7c
#define VIDININITERR	0x80
#define VIDINYDELTA	0x84
#define VIDPIXBUFTHOLD	0x88
#define VIDCHRMIN	0x8c
#define VIDCHRMAX	0x90
#define VIDCURLIN	0x94
#define VIDSCREENSIZE	0x98
#define VIDOVRSTARTCRD	0x9c
#define VIDOVRENDCRD	0xa0
#define VIDOVRDUDX	0xa4
#define VIDOVRDUDXOFF	0xa8
#define VIDOVRDVDY	0xac
/* ... */
#define VIDOVRDVDYOFF	0xe0
#define VIDDESKSTART	0xe4
#define VIDDESKSTRIDE	0xe8
#define VIDINADDR0	0xec
#define VIDINADDR1	0xf0
#define VIDINADDR2	0xf4
#define VIDINSTRIDE	0xf8
#define VIDCUROVRSTART	0xfc

#define INTCTRL		(0x00100000 + 0x04)
#define CLIP0MIN	(0x00100000 + 0x08)
#define CLIP0MAX	(0x00100000 + 0x0c)
#define DSTBASE		(0x00100000 + 0x10)
#define DSTFORMAT	(0x00100000 + 0x14)
#define SRCBASE		(0x00100000 + 0x34)
#define COMMANDEXTRA_2D	(0x00100000 + 0x38)
#define CLIP1MIN	(0x00100000 + 0x4c)
#define CLIP1MAX	(0x00100000 + 0x50)
#define SRCFORMAT	(0x00100000 + 0x54)
#define SRCSIZE		(0x00100000 + 0x58)
#define SRCXY		(0x00100000 + 0x5c)
#define COLORBACK	(0x00100000 + 0x60)
#define COLORFORE	(0x00100000 + 0x64)
#define DSTSIZE		(0x00100000 + 0x68)
#define DSTXY		(0x00100000 + 0x6c)
#define COMMAND_2D	(0x00100000 + 0x70)
#define LAUNCH_2D	(0x00100000 + 0x80)

#define COMMAND_3D	(0x00200000 + 0x120)

/* register bitfields (not all, only as needed) */

#define BIT(x)	(1UL << (x))

/* COMMAND_2D reg. values */
#define TDFX_ROP_COPY		0xcc	/* src */
#define TDFX_ROP_INVERT		0x55	/* NOT dst */
#define TDFX_ROP_XOR		0x66	/* src XOR dst */

#define AUTOINC_DSTX			BIT(10)
#define AUTOINC_DSTY			BIT(11)
#define COMMAND_2D_FILLRECT		0x05
#define COMMAND_2D_S2S_BITBLT		0x01	/* screen to screen */
#define COMMAND_2D_H2S_BITBLT		0x03	/* host to screen */

#define COMMAND_3D_NOP			0x00
#define STATUS_RETRACE			BIT(6)
#define STATUS_BUSY			BIT(9)
#define MISCINIT1_CLUT_INV		BIT(0)
#define MISCINIT1_2DBLOCK_DIS		BIT(15)
#define DRAMINIT0_SGRAM_NUM		BIT(26)
#define DRAMINIT0_SGRAM_TYPE		BIT(27)
#define DRAMINIT0_SGRAM_TYPE_MASK       (BIT(27) | BIT(28) | BIT(29))
#define DRAMINIT0_SGRAM_TYPE_SHIFT      27
#define DRAMINIT1_MEM_SDRAM		BIT(30)
#define VGAINIT0_VGA_DISABLE		BIT(0)
#define VGAINIT0_EXT_TIMING		BIT(1)
#define VGAINIT0_8BIT_DAC		BIT(2)
#define VGAINIT0_EXT_ENABLE		BIT(6)
#define VGAINIT0_WAKEUP_3C3		BIT(8)
#define VGAINIT0_LEGACY_DISABLE		BIT(9)
#define VGAINIT0_ALT_READBACK		BIT(10)
#define VGAINIT0_FAST_BLINK		BIT(11)
#define VGAINIT0_EXTSHIFTOUT		BIT(12)
#define VGAINIT0_DECODE_3C6		BIT(13)
#define VGAINIT0_SGRAM_HBLANK_DISABLE	BIT(22)
#define VGAINIT1_MASK			0x1fffff
#define VIDCFG_VIDPROC_ENABLE		BIT(0)
#define VIDCFG_CURS_X11			BIT(1)
#define VIDCFG_INTERLACE		BIT(3)
#define VIDCFG_HALF_MODE		BIT(4)
#define VIDCFG_DESK_ENABLE		BIT(7)
#define VIDCFG_CLUT_BYPASS		BIT(10)
#define VIDCFG_2X			BIT(26)
#define VIDCFG_HWCURSOR_ENABLE		BIT(27)
#define VIDCFG_PIXFMT_SHIFT             18
#define DACMODE_2X			BIT(0)

/* VGA rubbish, need to change this for multihead support */
#define MISC_W		0x3c2
#define MISC_R		0x3cc
#define SEQ_I		0x3c4
#define SEQ_D		0x3c5
#define CRT_I		0x3d4
#define CRT_D		0x3d5
#define ATT_IW		0x3c0
#define IS1_R		0x3da
#define GRA_I		0x3ce
#define GRA_D		0x3cf

#ifdef __KERNEL__

struct banshee_reg {
	/* VGA rubbish */
	unsigned char att[21];
	unsigned char crt[25];
	unsigned char gra[9];
	unsigned char misc[1];
	unsigned char seq[5];

	/* Banshee extensions */
	unsigned char ext[2];
	unsigned long vidcfg;
	unsigned long vidpll;
	unsigned long mempll;
	unsigned long gfxpll;
	unsigned long dacmode;
	unsigned long vgainit0;
	unsigned long vgainit1;
	unsigned long screensize;
	unsigned long stride;
	unsigned long cursloc;
	unsigned long curspataddr;
	unsigned long cursc0;
	unsigned long cursc1;
	unsigned long startaddr;
	unsigned long clip0min;
	unsigned long clip0max;
	unsigned long clip1min;
	unsigned long clip1max;
	unsigned long miscinit0;
};

struct tdfx_par {
	u32 max_pixclock;
	u32 palette[16];
	void __iomem *regbase_virt;
	unsigned long iobase;

	struct {
		int w, u, d;
		unsigned long enable, disable;
		struct timer_list timer;
	} hwcursor;

	spinlock_t DAClock;
};

#endif	/* __KERNEL__ */

#endif	/* _TDFX_H */

