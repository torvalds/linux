/* SPDX-License-Identifier: GPL-2.0 */
/*
    Aureal Advantage Soundcard driver.
 */

#define CHIP_AU8810

#define CARD_NAME "Aureal Advantage"
#define CARD_NAME_SHORT "au8810"

#define NR_ADB		0x10
#define NR_WT		0x00
#define NR_SRC		0x10
#define NR_A3D		0x10
#define NR_MIXIN	0x20
#define NR_MIXOUT	0x10


/* ADBDMA */
#define VORTEX_ADBDMA_STAT 0x27e00	/* read only, subbuffer, DMA pos */
#define		POS_MASK 0x00000fff
#define     POS_SHIFT 0x0
#define 	ADB_SUBBUF_MASK 0x00003000	/* ADB only. */
#define     ADB_SUBBUF_SHIFT 0xc	/* ADB only. */
#define VORTEX_ADBDMA_CTRL 0x27180	/* write only; format, flags, DMA pos */
#define		OFFSET_MASK 0x00000fff
#define     OFFSET_SHIFT 0x0
#define		IE_MASK 0x00001000	/* interrupt enable. */
#define     IE_SHIFT 0xc
#define     DIR_MASK 0x00002000	/* Direction */
#define     DIR_SHIFT 0xd
#define		FMT_MASK 0x0003c000
#define		FMT_SHIFT 0xe
// The ADB masks and shift also are valid for the wtdma, except if specified otherwise.
#define VORTEX_ADBDMA_BUFCFG0 0x27100
#define VORTEX_ADBDMA_BUFCFG1 0x27104
#define VORTEX_ADBDMA_BUFBASE 0x27000
#define VORTEX_ADBDMA_START 0x27c00	/* Which subbuffer starts */

#define VORTEX_ADBDMA_STATUS 0x27A90	/* stored at AdbDma->this_10 / 2 DWORD in size. */

/* WTDMA */
#define VORTEX_WTDMA_CTRL 0x27fd8	/* format, DMA pos */
#define VORTEX_WTDMA_STAT 0x27fe8	/* DMA subbuf, DMA pos */
#define     WT_SUBBUF_MASK 0x3
#define     WT_SUBBUF_SHIFT 0xc
#define VORTEX_WTDMA_BUFBASE 0x27fc0
#define VORTEX_WTDMA_BUFCFG0 0x27fd0
#define VORTEX_WTDMA_BUFCFG1 0x27fd4
#define VORTEX_WTDMA_START 0x27fe4	/* which subbuffer is first */

/* ADB */
#define VORTEX_ADB_SR 0x28400	/* Samplerates enable/disable */
#define VORTEX_ADB_RTBASE 0x28000
#define VORTEX_ADB_RTBASE_COUNT 173
#define VORTEX_ADB_CHNBASE 0x282b4
#define VORTEX_ADB_CHNBASE_COUNT 24
#define 	ROUTE_MASK	0xffff
#define		SOURCE_MASK	0xff00
#define     ADB_MASK   0xff
#define		ADB_SHIFT 0x8

/* ADB address */
#define		OFFSET_ADBDMA	0x00
#define		OFFSET_SRCIN	0x40
#define		OFFSET_SRCOUT	0x20
#define		OFFSET_MIXIN	0x50
#define		OFFSET_MIXOUT	0x30
#define		OFFSET_CODECIN	0x70
#define		OFFSET_CODECOUT	0x88
#define		OFFSET_SPORTIN	0x78	/* ch 0x13 */
#define		OFFSET_SPORTOUT	0x90
#define		OFFSET_SPDIFOUT	0x92	/* ch 0x14 check this! */
#define		OFFSET_EQIN	0xa0
#define		OFFSET_EQOUT	0x7e	/* 2 routes on ch 0x11 */
#define		OFFSET_XTALKOUT	0x66	/* crosstalk canceller (source) */
#define		OFFSET_XTALKIN	0x96	/* crosstalk canceller (sink) */
#define		OFFSET_A3DIN	0x70	/* ADB sink. */
#define		OFFSET_A3DOUT	0xA6	/* ADB source. 2 routes per slice = 8 */
#define		OFFSET_EFXIN	0x80	/* ADB sink. */
#define		OFFSET_EFXOUT	0x68	/* ADB source. */

/* ADB route translate helper */
#define ADB_DMA(x) (x)
#define ADB_SRCOUT(x) (x + OFFSET_SRCOUT)
#define ADB_SRCIN(x) (x + OFFSET_SRCIN)
#define ADB_MIXOUT(x) (x + OFFSET_MIXOUT)
#define ADB_MIXIN(x) (x + OFFSET_MIXIN)
#define ADB_CODECIN(x) (x + OFFSET_CODECIN)
#define ADB_CODECOUT(x) (x + OFFSET_CODECOUT)
#define ADB_SPORTIN(x) (x + OFFSET_SPORTIN)
#define ADB_SPORTOUT(x) (x + OFFSET_SPORTOUT)
#define ADB_SPDIFOUT(x)	(x + OFFSET_SPDIFOUT)
#define ADB_EQIN(x) (x + OFFSET_EQIN)
#define ADB_EQOUT(x) (x + OFFSET_EQOUT)
#define ADB_A3DOUT(x) (x + OFFSET_A3DOUT)	/* 0x10 A3D blocks */
#define ADB_A3DIN(x) (x + OFFSET_A3DIN)
#define ADB_XTALKIN(x) (x + OFFSET_XTALKIN)
#define ADB_XTALKOUT(x) (x + OFFSET_XTALKOUT)

#define MIX_OUTL    0xe
#define MIX_OUTR    0xf
#define MIX_INL     0x1e
#define MIX_INR     0x1f
#define MIX_DEFIGAIN 0x08	/* 0x8 => 6dB */
#define MIX_DEFOGAIN 0x08

/* MIXER */
#define VORTEX_MIXER_SR 0x21f00
#define VORTEX_MIXER_CLIP 0x21f80
#define VORTEX_MIXER_CHNBASE 0x21e40
#define VORTEX_MIXER_RTBASE 0x21e00
#define 	MIXER_RTBASE_SIZE 0x38
#define VORTEX_MIX_ENIN 0x21a00	/* Input enable bits. 4 bits wide. */
#define VORTEX_MIX_SMP 0x21c00	/* AU8820: 0x9c00 */

/* MIX */
#define VORTEX_MIX_INVOL_A 0x21000	/* in? */
#define VORTEX_MIX_INVOL_B 0x20000	/* out? */
#define VORTEX_MIX_VOL_A 0x21800
#define VORTEX_MIX_VOL_B 0x20800

#define 	VOL_MIN 0x80	/* Input volume when muted. */
#define		VOL_MAX 0x7f	/* FIXME: Not confirmed! Just guessed. */

/* SRC */
#define VORTEX_SRC_CHNBASE		0x26c40
#define VORTEX_SRC_RTBASE		0x26c00
#define VORTEX_SRCBLOCK_SR		0x26cc0
#define VORTEX_SRC_SOURCE		0x26cc4
#define VORTEX_SRC_SOURCESIZE	0x26cc8
/* Params
	0x26e00	: 1 U0
	0x26e40	: 2 CR
	0x26e80	: 3 U3
	0x26ec0	: 4 DRIFT1
	0x26f00 : 5 U1
	0x26f40	: 6 DRIFT2
	0x26f80	: 7 U2 : Target rate, direction
*/

#define VORTEX_SRC_CONVRATIO	0x26e40
#define VORTEX_SRC_DRIFT0		0x26e80
#define VORTEX_SRC_DRIFT1		0x26ec0
#define VORTEX_SRC_DRIFT2		0x26f40
#define VORTEX_SRC_U0			0x26e00
#define		U0_SLOWLOCK		0x200
#define VORTEX_SRC_U1			0x26f00
#define VORTEX_SRC_U2			0x26f80
#define VORTEX_SRC_DATA			0x26800	/* 0xc800 */
#define VORTEX_SRC_DATA0		0x26000

/* FIFO */
#define VORTEX_FIFO_ADBCTRL 0x16100	/* Control bits. */
#define VORTEX_FIFO_WTCTRL 0x16000
#define		FIFO_RDONLY	0x00000001
#define		FIFO_CTRL	0x00000002	/* Allow ctrl. ? */
#define		FIFO_VALID	0x00000010
#define 	FIFO_EMPTY	0x00000020
#define		FIFO_U0		0x00001000	/* Unknown. */
#define		FIFO_U1		0x00010000
#define		FIFO_SIZE_BITS 5
#define		FIFO_SIZE	(1<<FIFO_SIZE_BITS)	// 0x20
#define 	FIFO_MASK	(FIFO_SIZE-1)	//0x1f    /* at shift left 0xc */
//#define       FIFO_MASK       0x1f    /* at shift left 0xb */
//#define               FIFO_SIZE       0x20
#define 	FIFO_BITS	0x03880000
#define VORTEX_FIFO_ADBDATA	0x14000
#define VORTEX_FIFO_WTDATA	0x10000

/* CODEC */
#define VORTEX_CODEC_CTRL	0x29184
#define VORTEX_CODEC_EN		0x29190
#define		EN_CODEC0	0x00000300
#define 	EN_AC98		0x00000c00 /* Modem AC98 slots. */
#define		EN_CODEC1	0x00003000
#define		EN_CODEC	(EN_CODEC0 | EN_CODEC1)
#define		EN_SPORT	0x00030000
#define		EN_SPDIF	0x000c0000

#define VORTEX_CODEC_CHN 	0x29080
#define VORTEX_CODEC_IO		0x29188

/* SPDIF */
#define VORTEX_SPDIF_FLAGS	0x2205c
#define VORTEX_SPDIF_CFG0	0x291D0
#define VORTEX_SPDIF_CFG1	0x291D4
#define VORTEX_SPDIF_SMPRATE	0x29194

/* Sample timer */
#define VORTEX_SMP_TIME		0x29198

#define VORTEX_MODEM_CTRL	0x291ac

/* IRQ */
#define VORTEX_IRQ_SOURCE 0x2a000	/* Interrupt source flags. */
#define VORTEX_IRQ_CTRL 0x2a004	/* Interrupt source mask. */

#define VORTEX_STAT	0x2a008	/* Status */

#define VORTEX_CTRL		0x2a00c
#define 	CTRL_MIDI_EN	0x00000001
#define 	CTRL_MIDI_PORT	0x00000060
#define 	CTRL_GAME_EN	0x00000008
#define 	CTRL_GAME_PORT	0x00000e00
//#define       CTRL_IRQ_ENABLE 0x01004000
#define 	CTRL_IRQ_ENABLE	0x00004000

/* write: Timer period config / read: TIMER IRQ ack. */
#define VORTEX_IRQ_STAT		0x2919c

/* DMA */
#define VORTEX_ENGINE_CTRL	0x27ae8
#define 	ENGINE_INIT	0x1380000

/* MIDI *//* GAME. */
#define VORTEX_MIDI_DATA	0x28800
#define VORTEX_MIDI_CMD		0x28804	/* Write command / Read status */

#define VORTEX_CTRL2		0x2880c
#define		CTRL2_GAME_ADCMODE 0x40
#define VORTEX_GAME_LEGACY	0x28808
#define VORTEX_GAME_AXIS	0x28810
#define		AXIS_SIZE 4
#define		AXIS_RANGE 0x1fff
