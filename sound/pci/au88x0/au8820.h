/*
    Aureal Vortex Soundcard driver.

    IO addr collected from asp4core.vxd:
    function    address
    0005D5A0    13004
    00080674    14004
    00080AFF    12818

 */

#define CHIP_AU8820

#define CARD_NAME "Aureal Vortex"
#define CARD_NAME_SHORT "au8820"

/* Number of ADB and WT channels */
#define NR_ADB		0x10
#define NR_WT		0x20
#define NR_SRC		0x10
#define NR_A3D		0x00
#define NR_MIXIN	0x10
#define NR_MIXOUT 	0x10


/* ADBDMA */
#define VORTEX_ADBDMA_STAT 0x105c0	/* read only, subbuffer, DMA pos */
#define		POS_MASK 0x00000fff
#define     POS_SHIFT 0x0
#define 	ADB_SUBBUF_MASK 0x00003000	/* ADB only. */
#define     ADB_SUBBUF_SHIFT 0xc	/* ADB only. */
#define VORTEX_ADBDMA_CTRL 0x10580	/* write only, format, flags, DMA pos */
#define		OFFSET_MASK 0x00000fff
#define     OFFSET_SHIFT 0x0
#define		IE_MASK 0x00001000	/* interrupt enable. */
#define     IE_SHIFT 0xc
#define     DIR_MASK 0x00002000	/* Direction. */
#define     DIR_SHIFT 0xd
#define		FMT_MASK 0x0003c000
#define		FMT_SHIFT 0xe
// The masks and shift also work for the wtdma, if not specified otherwise.
#define VORTEX_ADBDMA_BUFCFG0 0x10400
#define VORTEX_ADBDMA_BUFCFG1 0x10404
#define VORTEX_ADBDMA_BUFBASE 0x10200
#define VORTEX_ADBDMA_START 0x106c0	/* Which subbuffer starts */
#define VORTEX_ADBDMA_STATUS 0x10600	/* stored at AdbDma->this_10 / 2 DWORD in size. */

/* ADB */
#define VORTEX_ADB_SR 0x10a00	/* Samplerates enable/disable */
#define VORTEX_ADB_RTBASE 0x10800
#define VORTEX_ADB_RTBASE_COUNT 103
#define VORTEX_ADB_CHNBASE 0x1099c
#define VORTEX_ADB_CHNBASE_COUNT 22
#define 	ROUTE_MASK	0x3fff
#define     ADB_MASK   0x7f
#define		ADB_SHIFT 0x7
//#define     ADB_MIX_MASK 0xf
/* ADB address */
#define		OFFSET_ADBDMA	0x00
#define		OFFSET_SRCOUT	0x10	/* on channel 0x11 */
#define		OFFSET_SRCIN	0x10	/* on channel < 0x11 */
#define		OFFSET_MIXOUT	0x20	/* source */
#define		OFFSET_MIXIN	0x30	/* sink */
#define		OFFSET_CODECIN	0x48	/* ADB source */
#define		OFFSET_CODECOUT	0x58	/* ADB sink/target */
#define		OFFSET_SPORTOUT	0x60	/* sink */
#define		OFFSET_SPORTIN	0x50	/* source */
#define		OFFSET_EFXOUT	0x50	/* sink */
#define		OFFSET_EFXIN	0x40	/* source */
#define		OFFSET_A3DOUT	0x00	/* This card has no HRTF :( */
#define		OFFSET_A3DIN	0x00
#define		OFFSET_WTOUT	0x58	/*  */

/* ADB route translate helper */
#define ADB_DMA(x) (x + OFFSET_ADBDMA)
#define ADB_SRCOUT(x) (x + OFFSET_SRCOUT)
#define ADB_SRCIN(x) (x + OFFSET_SRCIN)
#define ADB_MIXOUT(x) (x + OFFSET_MIXOUT)
#define ADB_MIXIN(x) (x + OFFSET_MIXIN)
#define ADB_CODECIN(x) (x + OFFSET_CODECIN)
#define ADB_CODECOUT(x) (x + OFFSET_CODECOUT)
#define ADB_SPORTOUT(x) (x + OFFSET_SPORTOUT)
#define ADB_SPORTIN(x) (x + OFFSET_SPORTIN)	/*  */
#define ADB_A3DOUT(x) (x + OFFSET_A3DOUT)	/* 8 A3D blocks */
#define ADB_A3DIN(x) (x + OFFSET_A3DIN)
#define ADB_WTOUT(x,y) (y + OFFSET_WTOUT)

/* WTDMA */
#define VORTEX_WTDMA_CTRL 0x10500	/* format, DMA pos */
#define VORTEX_WTDMA_STAT 0x10500	/* DMA subbuf, DMA pos */
#define     WT_SUBBUF_MASK (0x3 << WT_SUBBUF_SHIFT)
#define     WT_SUBBUF_SHIFT 0x15
#define VORTEX_WTDMA_BUFBASE 0x10000
#define VORTEX_WTDMA_BUFCFG0 0x10300
#define VORTEX_WTDMA_BUFCFG1 0x10304
#define VORTEX_WTDMA_START 0x10640	/* which subbuffer is first */

#define VORTEX_WT_BASE 0x9000

/* MIXER */
#define VORTEX_MIXER_SR 0x9f00
#define VORTEX_MIXER_CLIP 0x9f80
#define VORTEX_MIXER_CHNBASE 0x9e40
#define VORTEX_MIXER_RTBASE 0x9e00
#define 	MIXER_RTBASE_SIZE 0x26
#define VORTEX_MIX_ENIN 0x9a00	/* Input enable bits. 4 bits wide. */
#define VORTEX_MIX_SMP 0x9c00

/* MIX */
#define VORTEX_MIX_INVOL_A 0x9000	/* in? */
#define VORTEX_MIX_INVOL_B 0x8000	/* out? */
#define VORTEX_MIX_VOL_A 0x9800
#define VORTEX_MIX_VOL_B 0x8800

#define 	VOL_MIN 0x80	/* Input volume when muted. */
#define		VOL_MAX 0x7f	/* FIXME: Not confirmed! Just guessed. */

//#define MIX_OUTL    0xe
//#define MIX_OUTR    0xf
//#define MIX_INL     0xe
//#define MIX_INR     0xf
#define MIX_DEFIGAIN 0x08	/* 0x8 => 6dB */
#define MIX_DEFOGAIN 0x08

/* SRC */
#define VORTEX_SRCBLOCK_SR	0xccc0
#define VORTEX_SRC_CHNBASE	0xcc40
#define VORTEX_SRC_RTBASE	0xcc00
#define VORTEX_SRC_SOURCE	0xccc4
#define VORTEX_SRC_SOURCESIZE 0xccc8
#define VORTEX_SRC_U0		0xce00
#define VORTEX_SRC_DRIFT0	0xce80
#define VORTEX_SRC_DRIFT1	0xcec0
#define VORTEX_SRC_U1		0xcf00
#define VORTEX_SRC_DRIFT2	0xcf40
#define VORTEX_SRC_U2		0xcf80
#define VORTEX_SRC_DATA		0xc800
#define VORTEX_SRC_DATA0	0xc000
#define VORTEX_SRC_CONVRATIO 0xce40
//#define     SRC_RATIO(x) ((((x<<15)/48000) + 1)/2) /* Playback */
//#define     SRC_RATIO2(x) ((((48000<<15)/x) + 1)/2) /* Recording */

/* FIFO */
#define VORTEX_FIFO_ADBCTRL 0xf800	/* Control bits. */
#define VORTEX_FIFO_WTCTRL 0xf840
#define		FIFO_RDONLY	0x00000001
#define		FIFO_CTRL	0x00000002	/* Allow ctrl. ? */
#define		FIFO_VALID	0x00000010
#define 	FIFO_EMPTY	0x00000020
#define		FIFO_U0		0x00001000	/* Unknown. */
#define		FIFO_U1		0x00010000
#define		FIFO_SIZE_BITS 5
#define		FIFO_SIZE	(1<<FIFO_SIZE_BITS)	// 0x20
#define 	FIFO_MASK	(FIFO_SIZE-1)	//0x1f    /* at shift left 0xc */
#define VORTEX_FIFO_ADBDATA 0xe000
#define VORTEX_FIFO_WTDATA 0xe800

/* CODEC */
#define VORTEX_CODEC_CTRL 0x11984
#define VORTEX_CODEC_EN 0x11990
#define		EN_CODEC	0x00000300
#define		EN_SPORT	0x00030000
#define		EN_SPDIF	0x000c0000
#define VORTEX_CODEC_CHN 0x11880
#define VORTEX_CODEC_IO 0x11988

#define VORTEX_SPDIF_FLAGS		0x1005c	/* FIXME */
#define VORTEX_SPDIF_CFG0		0x119D0
#define VORTEX_SPDIF_CFG1		0x119D4
#define VORTEX_SPDIF_SMPRATE	0x11994

/* Sample timer */
#define VORTEX_SMP_TIME 0x11998

/* IRQ */
#define VORTEX_IRQ_SOURCE 0x12800	/* Interrupt source flags. */
#define VORTEX_IRQ_CTRL 0x12804	/* Interrupt source mask. */

#define VORTEX_STAT		0x12808	/* ?? */

#define VORTEX_CTRL 0x1280c
#define 	CTRL_MIDI_EN 0x00000001
#define 	CTRL_MIDI_PORT 0x00000060
#define 	CTRL_GAME_EN 0x00000008
#define 	CTRL_GAME_PORT 0x00000e00
#define 	CTRL_IRQ_ENABLE 0x4000

/* write: Timer period config / read: TIMER IRQ ack. */
#define VORTEX_IRQ_STAT 0x1199c

/* DMA */
#define VORTEX_DMA_BUFFER 0x10200
#define VORTEX_ENGINE_CTRL 0x1060c
#define 	ENGINE_INIT 0x0L

		     /* MIDI *//* GAME. */
#define VORTEX_MIDI_DATA 0x11000
#define VORTEX_MIDI_CMD 0x11004	/* Write command / Read status */
#define VORTEX_GAME_LEGACY 0x11008
#define VORTEX_CTRL2 0x1100c
#define 	CTRL2_GAME_ADCMODE 0x40
#define VORTEX_GAME_AXIS 0x11010
#define 	AXIS_SIZE 4
#define		AXIS_RANGE 0x1fff
