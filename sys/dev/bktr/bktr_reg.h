/*-
 * $FreeBSD$
 *
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1999 Roger Hardiman
 * Copyright (c) 1998 Amancio Hasty
 * Copyright (c) 1995 Mark Tinguely and Jim Lowe
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
 *	This product includes software developed by Mark Tinguely and Jim Lowe
 * 4. The name of the author may not be used to endorse or promote products 
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifdef __NetBSD__
#include <machine/bus.h>		/* device_t */
#include <sys/device.h>
#include <sys/select.h>			/* struct selinfo */
# ifdef DEBUG
#  define	bootverbose 1
# else
#  define	bootverbose 0
# endif
#endif

/*
 * The kernel options for the driver now all begin with BKTR.
 * Support the older kernel options on FreeBSD and OpenBSD.
 *
 */
#if defined(__FreeBSD__) || defined(__OpenBSD__)
#if defined(BROOKTREE_ALLOC_PAGES)
#define BKTR_ALLOC_PAGES BROOKTREE_ALLOC_PAGES
#endif

#if defined(BROOKTREE_SYSTEM_DEFAULT)
#define BKTR_SYSTEM_DEFAULT BROOKTREE_SYSTEM_DEFAULT
#endif

#if defined(OVERRIDE_CARD)
#define BKTR_OVERRIDE_CARD OVERRIDE_CARD
#endif

#if defined(OVERRIDE_TUNER)
#define BKTR_OVERRIDE_TUNER OVERRIDE_TUNER
#endif

#if defined(OVERRIDE_DBX)
#define BKTR_OVERRIDE_DBX OVERRIDE_DBX
#endif

#if defined(OVERRIDE_MSP)
#define BKTR_OVERRIDE_MSP OVERRIDE_MSP
#endif

#endif


#ifndef PCI_LATENCY_TIMER
#define	PCI_LATENCY_TIMER		0x0c	/* pci timer register */
#endif

/*
 * Definitions for the Brooktree 848/878 video capture to pci interface.
 */
#ifndef __NetBSD__
#define BKTR_PCI_VENDOR_SHIFT                        0
#define BKTR_PCI_VENDOR_MASK                         0xffff
#define BKTR_PCI_VENDOR(id) \
            (((id) >> BKTR_PCI_VENDOR_SHIFT) & BKTR_PCI_VENDOR_MASK)

#define BKTR_PCI_PRODUCT_SHIFT                       16
#define BKTR_PCI_PRODUCT_MASK                        0xffff
#define BKTR_PCI_PRODUCT(id) \
            (((id) >> BKTR_PCI_PRODUCT_SHIFT) & BKTR_PCI_PRODUCT_MASK)

/* PCI vendor ID */
#define PCI_VENDOR_BROOKTREE    0x109e                /* Brooktree */
/* Brooktree products */
#define PCI_PRODUCT_BROOKTREE_BT848     0x0350        /* Bt848 Video Capture */
#define PCI_PRODUCT_BROOKTREE_BT849     0x0351        /* Bt849 Video Capture */
#define PCI_PRODUCT_BROOKTREE_BT878     0x036e        /* Bt878 Video Capture */
#define PCI_PRODUCT_BROOKTREE_BT879     0x036f        /* Bt879 Video Capture */
#endif

#define BROOKTREE_848                   1
#define BROOKTREE_848A                  2
#define BROOKTREE_849A                  3
#define BROOKTREE_878                   4
#define BROOKTREE_879                   5

typedef volatile u_int 	bregister_t;
/*
 * if other persuasion endian, then compiler will probably require that
 * these next
 * macros be reversed
 */
#define	BTBYTE(what)	bregister_t  what:8; int :24
#define	BTWORD(what)	bregister_t  what:16; int: 16
#define BTLONG(what)	bregister_t  what:32

struct bt848_registers {
    BTBYTE (dstatus);		/* 0, 1,2,3 */
#define BT848_DSTATUS_PRES		(1<<7)
#define BT848_DSTATUS_HLOC		(1<<6)
#define BT848_DSTATUS_FIELD		(1<<5)
#define BT848_DSTATUS_NUML		(1<<4)
#define BT848_DSTATUS_CSEL		(1<<3)
#define BT848_DSTATUS_PLOCK		(1<<2)
#define BT848_DSTATUS_LOF		(1<<1)
#define BT848_DSTATUS_COF		(1<<0)
    BTBYTE (iform);		/* 4, 5,6,7 */
#define BT848_IFORM_MUXSEL		(0x3<<5)
# define BT848_IFORM_M_MUX1		(0x03<<5)
# define BT848_IFORM_M_MUX0		(0x02<<5)
# define BT848_IFORM_M_MUX2		(0x01<<5)
# define BT848_IFORM_M_MUX3		(0x0)
# define BT848_IFORM_M_RSVD		(0x00<<5)
#define BT848_IFORM_XTSEL		(0x3<<3)
# define BT848_IFORM_X_AUTO		(0x03<<3)
# define BT848_IFORM_X_XT1		(0x02<<3)
# define BT848_IFORM_X_XT0		(0x01<<3)
# define BT848_IFORM_X_RSVD		(0x00<<3)
    BTBYTE (tdec);		/* 8, 9,a,b */
    BTBYTE (e_crop);		/* c, d,e,f */
    BTBYTE (e_vdelay_lo);	/* 10, 11,12,13 */
    BTBYTE (e_vactive_lo);	/* 14, 15,16,17 */
    BTBYTE (e_delay_lo);	/* 18, 19,1a,1b */
    BTBYTE (e_hactive_lo);	/* 1c, 1d,1e,1f */
    BTBYTE (e_hscale_hi);	/* 20, 21,22,23 */
    BTBYTE (e_hscale_lo);	/* 24, 25,26,27 */
    BTBYTE (bright);		/* 28, 29,2a,2b */
    BTBYTE (e_control);		/* 2c, 2d,2e,2f */
#define BT848_E_CONTROL_LNOTCH		(1<<7)
#define BT848_E_CONTROL_COMP		(1<<6)
#define BT848_E_CONTROL_LDEC		(1<<5)
#define BT848_E_CONTROL_CBSENSE		(1<<4)
#define BT848_E_CONTROL_RSVD		(1<<3)
#define BT848_E_CONTROL_CON_MSB		(1<<2)
#define BT848_E_CONTROL_SAT_U_MSB	(1<<1)
#define BT848_E_CONTROL_SAT_V_MSB	(1<<0)
    BTBYTE (contrast_lo);	/* 30, 31,32,33 */
    BTBYTE (sat_u_lo);		/* 34, 35,36,37 */
    BTBYTE (sat_v_lo);		/* 38, 39,3a,3b */
    BTBYTE (hue);		/* 3c, 3d,3e,3f */
    BTBYTE (e_scloop);		/* 40, 41,42,43 */
#define BT848_E_SCLOOP_RSVD1		(1<<7)
#define BT848_E_SCLOOP_CAGC		(1<<6)
#define BT848_E_SCLOOP_CKILL		(1<<5)
#define BT848_E_SCLOOP_HFILT		(0x3<<3)
# define BT848_E_SCLOOP_HFILT_ICON	(0x3<<3)
# define BT848_E_SCLOOP_HFILT_QCIF	(0x2<<3)
# define BT848_E_SCLOOP_HFILT_CIF	(0x1<<3)
# define BT848_E_SCLOOP_HFILT_AUTO	(0x0<<3)
#define BT848_E_SCLOOP_RSVD0		(0x7<<0)
    int		:32;		/* 44, 45,46,47 */
    BTBYTE (oform);		/* 48, 49,4a,4b */
    BTBYTE (e_vscale_hi);	/* 4c, 4d,4e,4f */
    BTBYTE (e_vscale_lo);	/* 50, 51,52,53 */
    BTBYTE (test);		/* 54, 55,56,57 */
    int		:32;		/* 58, 59,5a,5b */
    int		:32;		/* 5c, 5d,5e,5f */
    BTLONG (adelay);		/* 60, 61,62,63 */
    BTBYTE (bdelay);		/* 64, 65,66,67 */
    BTBYTE (adc);		/* 68, 69,6a,6b */
#define BT848_ADC_RESERVED		(0x80)	/* required pattern */
#define BT848_ADC_SYNC_T		(1<<5)
#define BT848_ADC_AGC_EN		(1<<4)
#define BT848_ADC_CLK_SLEEP		(1<<3)
#define BT848_ADC_Y_SLEEP		(1<<2)
#define BT848_ADC_C_SLEEP		(1<<1)
#define BT848_ADC_CRUSH			(1<<0)
    BTBYTE (e_vtc);		/* 6c, 6d,6e,6f */
    int		:32;		/* 70, 71,72,73 */
    int 	:32;		/* 74, 75,76,77 */
    int		:32;		/* 78, 79,7a,7b */
    BTLONG (sreset);		/* 7c, 7d,7e,7f */
    u_char 	filler1[0x84-0x80];
    BTBYTE (tgctrl);		/* 84, 85,86,87 */
#define BT848_TGCTRL_TGCKI		(3<<3)
#define BT848_TGCTRL_TGCKI_XTAL		(0<<3)
#define BT848_TGCTRL_TGCKI_PLL		(1<<3)
#define BT848_TGCTRL_TGCKI_GPCLK	(2<<3)
#define BT848_TGCTRL_TGCKI_GPCLK_I	(3<<3)
    u_char 	filler[0x8c-0x88];
    BTBYTE (o_crop);		/* 8c, 8d,8e,8f */
    BTBYTE (o_vdelay_lo);	/* 90, 91,92,93 */
    BTBYTE (o_vactive_lo);	/* 94, 95,96,97 */
    BTBYTE (o_delay_lo);	/* 98, 99,9a,9b */
    BTBYTE (o_hactive_lo);	/* 9c, 9d,9e,9f */
    BTBYTE (o_hscale_hi);	/* a0, a1,a2,a3 */
    BTBYTE (o_hscale_lo);	/* a4, a5,a6,a7 */
    int		:32;		/* a8, a9,aa,ab */
    BTBYTE (o_control);		/* ac, ad,ae,af */
#define BT848_O_CONTROL_LNOTCH		(1<<7)
#define BT848_O_CONTROL_COMP		(1<<6)
#define BT848_O_CONTROL_LDEC		(1<<5)
#define BT848_O_CONTROL_CBSENSE		(1<<4)
#define BT848_O_CONTROL_RSVD		(1<<3)
#define BT848_O_CONTROL_CON_MSB		(1<<2)
#define BT848_O_CONTROL_SAT_U_MSB	(1<<1)
#define BT848_O_CONTROL_SAT_V_MSB	(1<<0)
    u_char	fillter4[16];
    BTBYTE (o_scloop);		/* c0, c1,c2,c3 */
#define BT848_O_SCLOOP_RSVD1		(1<<7)
#define BT848_O_SCLOOP_CAGC		(1<<6)
#define BT848_O_SCLOOP_CKILL		(1<<5)
#define BT848_O_SCLOOP_HFILT		(0x3<<3)
#define BT848_O_SCLOOP_HFILT_ICON	(0x3<<3)
#define BT848_O_SCLOOP_HFILT_QCIF	(0x2<<3)
#define BT848_O_SCLOOP_HFILT_CIF	(0x1<<3)
#define BT848_O_SCLOOP_HFILT_AUTO	(0x0<<3)
#define BT848_O_SCLOOP_RSVD0		(0x7<<0)
    int		:32;		/* c4, c5,c6,c7 */
    int		:32;		/* c8, c9,ca,cb */
    BTBYTE (o_vscale_hi);	/* cc, cd,ce,cf */
    BTBYTE (o_vscale_lo);	/* d0, d1,d2,d3 */
    BTBYTE (color_fmt);		/* d4, d5,d6,d7 */
    bregister_t color_ctl_swap		:4; /* d8 */
#define BT848_COLOR_CTL_WSWAP_ODD	(1<<3)
#define BT848_COLOR_CTL_WSWAP_EVEN	(1<<2)
#define BT848_COLOR_CTL_BSWAP_ODD	(1<<1)
#define BT848_COLOR_CTL_BSWAP_EVEN	(1<<0)
    bregister_t color_ctl_gamma		:1;
    bregister_t color_ctl_rgb_ded	:1;
    bregister_t color_ctl_color_bars	:1;
    bregister_t color_ctl_ext_frmrate	:1;
#define BT848_COLOR_CTL_GAMMA		(1<<4)
#define BT848_COLOR_CTL_RGB_DED		(1<<5)
#define BT848_COLOR_CTL_COLOR_BARS	(1<<6)
#define BT848_COLOR_CTL_EXT_FRMRATE     (1<<7)
    int		:24;		/* d9,da,db */
    BTBYTE (cap_ctl);		/* dc, dd,de,df */
#define BT848_CAP_CTL_DITH_FRAME	(1<<4)
#define BT848_CAP_CTL_VBI_ODD		(1<<3)
#define BT848_CAP_CTL_VBI_EVEN		(1<<2)
#define BT848_CAP_CTL_ODD		(1<<1)
#define BT848_CAP_CTL_EVEN		(1<<0)
    BTBYTE (vbi_pack_size);	/* e0, e1,e2,e3 */
    BTBYTE (vbi_pack_del);	/* e4, e5,e6,e7 */
    int		:32;		/* e8, e9,ea,eb */
    BTBYTE (o_vtc);		/* ec, ed,ee,ef */
    BTBYTE (pll_f_lo);		/* f0, f1,f2,f3 */
    BTBYTE (pll_f_hi);		/* f4, f5,f6,f7 */
    BTBYTE (pll_f_xci);		/* f8, f9,fa,fb */
#define BT848_PLL_F_C			(1<<6)
#define BT848_PLL_F_X			(1<<7)
    u_char	filler2[0x100-0xfc];
    BTLONG (int_stat);		/* 100, 101,102,103 */
    BTLONG (int_mask);		/* 104, 105,106,107 */
#define BT848_INT_RISCS			(0xf<<28)
#define BT848_INT_RISC_EN		(1<<27)
#define BT848_INT_RACK			(1<<25)
#define BT848_INT_FIELD			(1<<24)
#define BT848_INT_MYSTERYBIT		(1<<23)
#define BT848_INT_SCERR			(1<<19)
#define BT848_INT_OCERR			(1<<18)
#define BT848_INT_PABORT		(1<<17)
#define BT848_INT_RIPERR		(1<<16)
#define BT848_INT_PPERR			(1<<15)
#define BT848_INT_FDSR			(1<<14)
#define BT848_INT_FTRGT			(1<<13)
#define BT848_INT_FBUS			(1<<12)
#define BT848_INT_RISCI			(1<<11)
#define BT848_INT_GPINT			(1<<9)
#define BT848_INT_I2CDONE		(1<<8)
#define BT848_INT_RSV1			(1<<7)
#define BT848_INT_RSV0			(1<<6)
#define BT848_INT_VPRES			(1<<5)
#define BT848_INT_HLOCK			(1<<4)
#define BT848_INT_OFLOW			(1<<3)
#define BT848_INT_HSYNC			(1<<2)
#define BT848_INT_VSYNC			(1<<1)
#define BT848_INT_FMTCHG		(1<<0)
    int		:32;		/* 108, 109,10a,10b */
    BTWORD (gpio_dma_ctl);	/* 10c, 10d,10e,10f */
#define BT848_DMA_CTL_PL23TP4		(0<<6)	/* planar1 trigger 4 */
#define BT848_DMA_CTL_PL23TP8		(1<<6)	/* planar1 trigger 8 */
#define BT848_DMA_CTL_PL23TP16		(2<<6)	/* planar1 trigger 16 */
#define BT848_DMA_CTL_PL23TP32		(3<<6)	/* planar1 trigger 32 */
#define BT848_DMA_CTL_PL1TP4		(0<<4)	/* planar1 trigger 4 */
#define BT848_DMA_CTL_PL1TP8		(1<<4)	/* planar1 trigger 8 */
#define BT848_DMA_CTL_PL1TP16		(2<<4)	/* planar1 trigger 16 */
#define BT848_DMA_CTL_PL1TP32		(3<<4)	/* planar1 trigger 32 */
#define BT848_DMA_CTL_PKTP4		(0<<2)	/* packed trigger 4 */
#define BT848_DMA_CTL_PKTP8		(1<<2)	/* packed trigger 8 */
#define BT848_DMA_CTL_PKTP16		(2<<2)	/* packed trigger 16 */
#define BT848_DMA_CTL_PKTP32		(3<<2)	/* packed trigger 32 */
#define BT848_DMA_CTL_RISC_EN		(1<<1)
#define BT848_DMA_CTL_FIFO_EN		(1<<0)
    BTLONG (i2c_data_ctl);	/* 110, 111,112,113 */
#define BT848_DATA_CTL_I2CDIV		(0xf<<4)
#define BT848_DATA_CTL_I2CSYNC		(1<<3)
#define BT848_DATA_CTL_I2CW3B		(1<<2)
#define BT848_DATA_CTL_I2CSCL		(1<<1)
#define BT848_DATA_CTL_I2CSDA		(1<<0)
    BTLONG (risc_strt_add);	/* 114, 115,116,117 */
    BTLONG (gpio_out_en);	/* 118, 119,11a,11b */	/* really 24 bits */
    BTLONG (gpio_reg_inp);	/* 11c, 11d,11e,11f */	/* really 24 bits */
    BTLONG (risc_count);	/* 120, 121,122,123 */
    u_char	filler3[0x200-0x124];
    BTLONG (gpio_data);		/* 200, 201,202,203 */	/* really 24 bits */
};


#define BKTR_DSTATUS			0x000
#define BKTR_IFORM			0x004
#define BKTR_TDEC			0x008
#define BKTR_E_CROP			0x00C
#define BKTR_O_CROP			0x08C
#define BKTR_E_VDELAY_LO		0x010
#define BKTR_O_VDELAY_LO		0x090
#define BKTR_E_VACTIVE_LO		0x014
#define BKTR_O_VACTIVE_LO		0x094
#define BKTR_E_DELAY_LO			0x018
#define BKTR_O_DELAY_LO			0x098
#define BKTR_E_HACTIVE_LO		0x01C
#define BKTR_O_HACTIVE_LO		0x09C
#define BKTR_E_HSCALE_HI		0x020
#define BKTR_O_HSCALE_HI		0x0A0
#define BKTR_E_HSCALE_LO		0x024
#define BKTR_O_HSCALE_LO		0x0A4
#define BKTR_BRIGHT			0x028
#define BKTR_E_CONTROL			0x02C
#define BKTR_O_CONTROL			0x0AC
#define BKTR_CONTRAST_LO		0x030
#define BKTR_SAT_U_LO			0x034
#define BKTR_SAT_V_LO			0x038
#define BKTR_HUE			0x03C
#define BKTR_E_SCLOOP			0x040
#define BKTR_O_SCLOOP			0x0C0
#define BKTR_OFORM			0x048
#define BKTR_E_VSCALE_HI		0x04C
#define BKTR_O_VSCALE_HI		0x0CC
#define BKTR_E_VSCALE_LO		0x050
#define BKTR_O_VSCALE_LO		0x0D0
#define BKTR_TEST			0x054
#define BKTR_ADELAY			0x060
#define BKTR_BDELAY			0x064
#define BKTR_ADC			0x068
#define BKTR_E_VTC			0x06C
#define BKTR_O_VTC			0x0EC
#define BKTR_SRESET			0x07C
#define BKTR_COLOR_FMT			0x0D4
#define BKTR_COLOR_CTL			0x0D8
#define BKTR_CAP_CTL			0x0DC
#define BKTR_VBI_PACK_SIZE		0x0E0
#define BKTR_VBI_PACK_DEL		0x0E4
#define BKTR_INT_STAT			0x100
#define BKTR_INT_MASK			0x104
#define BKTR_RISC_COUNT			0x120
#define BKTR_RISC_STRT_ADD		0x114
#define BKTR_GPIO_DMA_CTL		0x10C
#define BKTR_GPIO_OUT_EN		0x118
#define BKTR_GPIO_REG_INP		0x11C
#define BKTR_GPIO_DATA			0x200
#define BKTR_I2C_DATA_CTL		0x110
#define BKTR_TGCTRL			0x084
#define BKTR_PLL_F_LO			0x0F0 
#define BKTR_PLL_F_HI			0x0F4 
#define BKTR_PLL_F_XCI			0x0F8

/*
 * device support for onboard tv tuners
 */

/* description of the LOGICAL tuner */
struct TVTUNER {
	int		frequency;
	u_char		chnlset;
	u_char		channel;
	u_char		band;
	u_char		afc;
 	u_char		radio_mode;	/* current mode of the radio mode */
};

/* description of the PHYSICAL tuner */
struct TUNER {
	char*		name;
	u_char		type;
	u_char		pllControl[4];
	u_char		bandLimits[ 2 ];
	u_char		bandAddrs[ 4 ];        /* 3 first for the 3 TV 
					       ** bands. Last for radio 
					       ** band (0x00=NoRadio).
					       */

};

/* description of the card */
#define EEPROMBLOCKSIZE		32
struct CARDTYPE {
	unsigned int		card_id;	/* card id (from #define's) */
	char*			name;
	const struct TUNER*	tuner;		/* Tuner details */
	u_char			tuner_pllAddr;	/* Tuner i2c address */
	u_char			dbx;		/* Has DBX chip? */
	u_char			msp3400c;	/* Has msp3400c chip? */
	u_char			dpl3518a;	/* Has dpl3518a chip? */
	u_char			eepromAddr;
	u_char			eepromSize;	/* bytes / EEPROMBLOCKSIZE */
	u_int			audiomuxs[ 5 ];	/* tuner, ext (line-in) */
						/* int/unused (radio) */
						/* mute, present */
	u_int			gpio_mux_bits;	/* GPIO mask for audio mux */
};

struct format_params {
  /* Total lines, lines before image, image lines */
  int vtotal, vdelay, vactive;
  /* Total unscaled horizontal pixels, pixels before image, image pixels */
  int htotal, hdelay, hactive;
  /* Scaled horizontal image pixels, Total Scaled horizontal pixels */
  int  scaled_hactive, scaled_htotal;
  /* frame rate . for ntsc is 30 frames per second */
  int frame_rate;
  /* A-delay and B-delay */
  u_char adelay, bdelay;
  /* Iform XTSEL value */
  int iform_xtsel;
  /* VBI number of lines per field, and number of samples per line */
  int vbi_num_lines, vbi_num_samples;
};

#if defined(BKTR_USE_FREEBSD_SMBUS)
struct bktr_i2c_softc {
	int bus_owned;

	device_t iicbb;
	device_t smbus;
};
#endif


/* Bt848/878 register access
 * The registers can either be access via a memory mapped structure
 * or accessed via bus_space.
 * bus_0pace access allows cross platform support, where as the
 * memory mapped structure method only works on 32 bit processors
 * with the right type of endianness.
 */
#if defined(__NetBSD__) || defined(__FreeBSD__)
#define INB(bktr,offset)	bus_space_read_1((bktr)->memt,(bktr)->memh,(offset))
#define INW(bktr,offset)	bus_space_read_2((bktr)->memt,(bktr)->memh,(offset))
#define INL(bktr,offset)	bus_space_read_4((bktr)->memt,(bktr)->memh,(offset))
#define OUTB(bktr,offset,value) bus_space_write_1((bktr)->memt,(bktr)->memh,(offset),(value))
#define OUTW(bktr,offset,value) bus_space_write_2((bktr)->memt,(bktr)->memh,(offset),(value))
#define OUTL(bktr,offset,value) bus_space_write_4((bktr)->memt,(bktr)->memh,(offset),(value))
#else
#define INB(bktr,offset)	*(volatile unsigned char*) ((int)((bktr)->memh)+(offset))
#define INW(bktr,offset)	*(volatile unsigned short*)((int)((bktr)->memh)+(offset))
#define INL(bktr,offset)	*(volatile unsigned int*)  ((int)((bktr)->memh)+(offset))
#define OUTB(bktr,offset,value)	*(volatile unsigned char*) ((int)((bktr)->memh)+(offset)) = (value)
#define OUTW(bktr,offset,value)	*(volatile unsigned short*)((int)((bktr)->memh)+(offset)) = (value)
#define OUTL(bktr,offset,value)	*(volatile unsigned int*)  ((int)((bktr)->memh)+(offset)) = (value)
#endif


typedef struct bktr_clip bktr_clip_t;

/*
 * BrookTree 848  info structure, one per bt848 card installed.
 */
struct bktr_softc {

#if defined (__bsdi__)
    struct device bktr_dev;	/* base device */
    struct isadev bktr_id;	/* ISA device */
    struct intrhand bktr_ih;	/* interrupt vectoring */
    #define pcici_t pci_devaddr_t
#endif

#if defined(__NetBSD__)
    struct device bktr_dev;     /* base device */
    bus_dma_tag_t	dmat;   /* DMA tag */
    bus_space_tag_t	memt;
    bus_space_handle_t	memh;
    bus_size_t		obmemsz;        /* size of en card (bytes) */
    void		*ih;
    bus_dmamap_t	dm_prog;
    bus_dmamap_t	dm_oprog;
    bus_dmamap_t	dm_mem;
    bus_dmamap_t	dm_vbidata;
    bus_dmamap_t	dm_vbibuffer;
#endif

#if defined(__OpenBSD__)
    struct device bktr_dev;     /* base device */
    bus_dma_tag_t	dmat;   /* DMA tag */
    bus_space_tag_t	memt;
    bus_space_handle_t	memh;
    bus_size_t		obmemsz;        /* size of en card (bytes) */
    void		*ih;
    bus_dmamap_t	dm_prog;
    bus_dmamap_t	dm_oprog;
    bus_dmamap_t	dm_mem;
    bus_dmamap_t	dm_vbidata;
    bus_dmamap_t	dm_vbibuffer;
    size_t		dm_mapsize;
    pci_chipset_tag_t	pc;	/* Opaque PCI chipset tag */
    pcitag_t		tag;	/* PCI tag, for doing PCI commands */
    vm_offset_t		phys_base;	/* Bt848 register physical address */
#endif

#if defined (__FreeBSD__)
    int             mem_rid;	/* 4.x resource id */
    struct resource *res_mem;	/* 4.x resource descriptor for registers */
    int             irq_rid;	/* 4.x resource id */
    struct resource *res_irq;	/* 4.x resource descriptor for interrupt */
    void            *res_ih;	/* 4.x newbus interrupt handler cookie */
    struct cdev     *bktrdev;	/* 4.x device entry for /dev/bktrN */
    struct cdev     *tunerdev;	/* 4.x device entry for /dev/tunerN */
    struct cdev     *vbidev;	/* 4.x device entry for /dev/vbiN */
    struct cdev     *bktrdev_alias;	/* alias /dev/bktr to /dev/bktr0 */
    struct cdev     *tunerdev_alias;	/* alias /dev/tuner to /dev/tuner0 */
    struct cdev     *vbidev_alias;	/* alias /dev/vbi to /dev/vbi0 */
    #if (__FreeBSD_version >= 500000)
    struct mtx      vbimutex;  /* Mutex protecting vbi buffer */
    #endif
    bus_space_tag_t	memt;	/* Bus space register access functions */
    bus_space_handle_t	memh;	/* Bus space register access functions */
    bus_size_t		obmemsz;/* Size of card (bytes) */
#if defined(BKTR_USE_FREEBSD_SMBUS)
      struct bktr_i2c_softc i2c_sc;	/* bt848_i2c device */
#endif
    char	bktr_xname[7];	/* device name and unit number */
#endif


    /* The following definitions are for the contiguous memory */
#ifdef __NetBSD__
    vaddr_t bigbuf;          /* buffer that holds the captured image */
    vaddr_t vbidata;         /* RISC program puts VBI data from the current frame here */
    vaddr_t vbibuffer;       /* Circular buffer holding VBI data for the user */
    vaddr_t dma_prog;        /* RISC prog for single and/or even field capture*/
    vaddr_t odd_dma_prog;    /* RISC program for Odd field capture */
#else
    vm_offset_t bigbuf;	     /* buffer that holds the captured image */
    vm_offset_t vbidata;     /* RISC program puts VBI data from the current frame here */
    vm_offset_t vbibuffer;   /* Circular buffer holding VBI data for the user */
    vm_offset_t dma_prog;    /* RISC prog for single and/or even field capture*/
    vm_offset_t odd_dma_prog;/* RISC program for Odd field capture */
#endif


    /* the following definitions are common over all platforms */
    int		alloc_pages;	/* number of pages in bigbuf */
    int         vbiinsert;      /* Position for next write into circular buffer */
    int         vbistart;       /* Position of last read from circular buffer */
    int         vbisize;        /* Number of bytes in the circular buffer */
    uint32_t	vbi_sequence_number;	/* sequence number for VBI */
    int		vbi_read_blocked;	/* user process blocked on read() from /dev/vbi */
    struct selinfo vbi_select;	/* Data used by select() on /dev/vbi */
    

    struct proc	*proc;		/* process to receive raised signal */
    int		signal;		/* signal to send to process */
    int		clr_on_start;	/* clear cap buf on capture start? */
#define	METEOR_SIG_MODE_MASK	0xffff0000
#define	METEOR_SIG_FIELD_MODE	0x00010000
#define	METEOR_SIG_FRAME_MODE	0x00000000
    char         dma_prog_loaded;
    struct meteor_mem *mem;	/* used to control sync. multi-frame output */
    u_long	synch_wait;	/* wait for free buffer before continuing */
    short	current;	/* frame number in buffer (1-frames) */
    short	rows;		/* number of rows in a frame */
    short	cols;		/* number of columns in a frame */
    int		capture_area_x_offset; /* Usually the full 640x480(NTSC) image is */
    int		capture_area_y_offset; /* captured. The capture area allows for */
    int		capture_area_x_size;   /* example 320x200 pixels from the centre */
    int		capture_area_y_size;   /* of the video image to be captured. */
    char	capture_area_enabled;  /* When TRUE use user's capture area. */
    int		pixfmt;         /* active pixel format (idx into fmt tbl) */
    int		pixfmt_compat;  /* Y/N - in meteor pix fmt compat mode */
    u_long	format;		/* frame format rgb, yuv, etc.. */
    short	frames;		/* number of frames allocated */
    int		frame_size;	/* number of bytes in a frame */
    u_long	fifo_errors;	/* number of fifo capture errors since open */
    u_long	dma_errors;	/* number of DMA capture errors since open */
    u_long	frames_captured;/* number of frames captured since open */
    u_long	even_fields_captured; /* number of even fields captured */
    u_long	odd_fields_captured; /* number of odd fields captured */
    u_long	range_enable;	/* enable range checking ?? */
    u_short     capcontrol;     /* reg 0xdc capture control */
    u_short     bktr_cap_ctl;
    volatile u_int	flags;
#define	METEOR_INITALIZED	0x00000001
#define	METEOR_OPEN		0x00000002 
#define	METEOR_MMAP		0x00000004
#define	METEOR_INTR		0x00000008
#define	METEOR_READ		0x00000010	/* XXX never gets referenced */
#define	METEOR_SINGLE		0x00000020	/* get single frame */
#define	METEOR_CONTIN		0x00000040	/* continuously get frames */
#define	METEOR_SYNCAP		0x00000080	/* synchronously get frames */
#define	METEOR_CAP_MASK		0x000000f0
#define	METEOR_NTSC		0x00000100
#define	METEOR_PAL		0x00000200
#define	METEOR_SECAM		0x00000400
#define	BROOKTREE_NTSC		0x00000100	/* used in video open() and */
#define	BROOKTREE_PAL		0x00000200	/* in the kernel config */
#define	BROOKTREE_SECAM		0x00000400	/* file */
#define	METEOR_AUTOMODE		0x00000800
#define	METEOR_FORM_MASK	0x00000f00
#define	METEOR_DEV0		0x00001000
#define	METEOR_DEV1		0x00002000
#define	METEOR_DEV2		0x00004000
#define	METEOR_DEV3		0x00008000
#define METEOR_DEV_SVIDEO	0x00006000
#define METEOR_DEV_RGB		0x0000a000
#define	METEOR_DEV_MASK		0x0000f000
#define	METEOR_RGB16		0x00010000
#define	METEOR_RGB24		0x00020000
#define	METEOR_YUV_PACKED	0x00040000
#define	METEOR_YUV_PLANAR	0x00080000
#define	METEOR_WANT_EVEN	0x00100000	/* want even frame */
#define	METEOR_WANT_ODD		0x00200000	/* want odd frame */
#define	METEOR_WANT_MASK	0x00300000
#define METEOR_ONLY_EVEN_FIELDS	0x01000000
#define METEOR_ONLY_ODD_FIELDS	0x02000000
#define METEOR_ONLY_FIELDS_MASK 0x03000000
#define METEOR_YUV_422		0x04000000
#define	METEOR_OUTPUT_FMT_MASK	0x040f0000
#define	METEOR_WANT_TS		0x08000000	/* time-stamp a frame */
#define METEOR_RGB		0x20000000	/* meteor rgb unit */
#define METEOR_FIELD_MODE	0x80000000
    u_char	tflags;				/* Tuner flags (/dev/tuner) */
#define	TUNER_INITALIZED	0x00000001
#define	TUNER_OPEN		0x00000002 
    u_char      vbiflags;			/* VBI flags (/dev/vbi) */
#define VBI_INITALIZED          0x00000001
#define VBI_OPEN                0x00000002
#define VBI_CAPTURE             0x00000004
    u_short	fps;		/* frames per second */
    struct meteor_video video;
    struct TVTUNER	tuner;
    struct CARDTYPE	card;
    u_char		audio_mux_select;	/* current mode of the audio */
    u_char		audio_mute_state;	/* mute state of the audio */
    u_char		format_params;
    u_long              current_sol;
    u_long              current_col;
    int                 clip_start;
    int                 line_length;
    int                 last_y;
    int                 y;
    int                 y2;
    int                 yclip;
    int                 yclip2;
    int                 max_clip_node;
    bktr_clip_t		clip_list[100];
    int                 reverse_mute;		/* Swap the GPIO values for Mute and TV Audio */
    int                 bt848_tuner;
    int                 bt848_card;
    u_long              id;
#define BT848_USE_XTALS 0
#define BT848_USE_PLL   1
    int			xtal_pll_mode;	/* Use XTAL or PLL mode for PAL/SECAM */
    int			remote_control;      /* remote control detected */
    int			remote_control_addr;   /* remote control i2c address */
    char		msp_version_string[9]; /* MSP version string 34xxx-xx */
    int			msp_addr;	       /* MSP i2c address */
    char		dpl_version_string[9]; /* DPL version string 35xxx-xx */
    int			dpl_addr;	       /* DPL i2c address */
    int                 slow_msp_audio;	       /* 0 = use fast MSP3410/3415 programming sequence */
					       /* 1 = use slow MSP3410/3415 programming sequence */
					       /* 2 = use Tuner's Mono audio output via the MSP chip */
    int                 msp_use_mono_source;   /* use Tuner's Mono audio output via the MSP chip */
    int                 audio_mux_present;     /* 1 = has audio mux on GPIO lines, 0 = no audio mux */
    int                 msp_source_selected;   /* 0 = TV source, 1 = Line In source, 2 = FM Radio Source */

#ifdef BKTR_NEW_MSP34XX_DRIVER
    /* msp3400c related data */
    void *		msp3400c_info;
    int			stereo_once;
    int			amsound;
    int			mspsimple;
    int			dolby;
#endif

};

typedef struct bktr_softc bktr_reg_t;
typedef struct bktr_softc* bktr_ptr_t;

#define Bt848_MAX_SIGN 16

struct bt848_card_sig {
  int card;
  int tuner;
  u_char signature[Bt848_MAX_SIGN];
};


/***********************************************************/
/* ioctl_cmd_t int on old versions, u_long on new versions */
/***********************************************************/

#if defined(__FreeBSD__)
typedef u_long ioctl_cmd_t;
#endif

#if defined(__NetBSD__) || defined(__OpenBSD__)
typedef u_long ioctl_cmd_t;
#endif


