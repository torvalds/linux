/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
 
#ifndef __SOUND_AU88X0_H
#define __SOUND_AU88X0_H

#ifdef __KERNEL__
#include <sound/driver.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/mpu401.h>
#include <sound/hwdep.h>
#include <sound/ac97_codec.h>

#endif

#ifndef CHIP_AU8820
#include "au88x0_eq.h"
#include "au88x0_a3d.h"
#endif
#ifndef CHIP_AU8810
#include "au88x0_wt.h"
#endif

#define	hwread(x,y) readl((x)+((y)>>2))
#define	hwwrite(x,y,z) writel((z),(x)+((y)>>2))

/* Vortex MPU401 defines. */
#define	MIDI_CLOCK_DIV		0x61
/* Standart MPU401 defines. */
#define	MPU401_RESET		0xff
#define	MPU401_ENTER_UART	0x3f
#define	MPU401_ACK		0xfe

// Get src register value to convert from x to y.
#define	SRC_RATIO(x,y)		((((x<<15)/y) + 1)/2)

/* FIFO software state constants. */
#define FIFO_STOP 0
#define FIFO_START 1
#define FIFO_PAUSE 2

/* IRQ flags */
#define IRQ_ERR_MASK	0x00ff
#define IRQ_FATAL	0x0001
#define IRQ_PARITY	0x0002
#define IRQ_REG		0x0004
#define IRQ_FIFO	0x0008
#define IRQ_DMA		0x0010
#define IRQ_PCMOUT	0x0020	/* PCM OUT page crossing */
#define IRQ_TIMER	0x1000
#define IRQ_MIDI	0x2000
#define IRQ_MODEM	0x4000

/* ADB Resource */
#define VORTEX_RESOURCE_DMA	0x00000000
#define VORTEX_RESOURCE_SRC	0x00000001
#define VORTEX_RESOURCE_MIXIN	0x00000002
#define VORTEX_RESOURCE_MIXOUT	0x00000003
#define VORTEX_RESOURCE_A3D	0x00000004
#define VORTEX_RESOURCE_LAST	0x00000005

/* codec io: VORTEX_CODEC_IO bits */
#define VORTEX_CODEC_ID_SHIFT	24
#define VORTEX_CODEC_WRITE	0x00800000
#define VORTEX_CODEC_ADDSHIFT 	16
#define VORTEX_CODEC_ADDMASK	0x7f0000
#define VORTEX_CODEC_DATSHIFT	0
#define VORTEX_CODEC_DATMASK	0xffff

/* Check for SDAC bit in "Extended audio ID" AC97 register */
//#define VORTEX_IS_QUAD(x) (((x)->codec == NULL) ?  0 : ((x)->codec->ext_id&0x80))
#define VORTEX_IS_QUAD(x) ((x)->isquad)
/* Check if chip has bug. */
#define IS_BAD_CHIP(x) (\
	(x->rev == 0xfe && x->device == PCI_DEVICE_ID_AUREAL_VORTEX_2) || \
	(x->rev == 0xfe && x->device == PCI_DEVICE_ID_AUREAL_ADVANTAGE))


/* PCM devices */
#define VORTEX_PCM_ADB		0
#define VORTEX_PCM_SPDIF	1
#define VORTEX_PCM_A3D		2
#define VORTEX_PCM_WT		3
#define VORTEX_PCM_I2S		4
#define VORTEX_PCM_LAST		5

#define MIX_CAPT(x) (vortex->mixcapt[x])
#define MIX_PLAYB(x) (vortex->mixplayb[x])
#define MIX_SPDIF(x) (vortex->mixspdif[x])

#define NR_WTPB 0x20		/* WT channels per eahc bank. */

/* Structs */
typedef struct {
	//int this_08;          /* Still unknown */
	int fifo_enabled;	/* this_24 */
	int fifo_status;	/* this_1c */
	int dma_ctrl;		/* this_78 (ADB), this_7c (WT) */
	int dma_unknown;	/* this_74 (ADB), this_78 (WT). WDM: +8 */
	int cfg0;
	int cfg1;

	int nr_ch;		/* Nr of PCM channels in use */
	int type;		/* Output type (ac97, a3d, spdif, i2s, dsp) */
	int dma;		/* Hardware DMA index. */
	int dir;		/* Stream Direction. */
	u32 resources[5];

	/* Virtual page extender stuff */
	int nr_periods;
	int period_bytes;
	struct snd_sg_buf *sgbuf;	/* DMA Scatter Gather struct */
	int period_real;
	int period_virt;

	struct snd_pcm_substream *substream;
} stream_t;

typedef struct snd_vortex vortex_t;
struct snd_vortex {
	/* ALSA structs. */
	struct snd_card *card;
	struct snd_pcm *pcm[VORTEX_PCM_LAST];

	struct snd_rawmidi *rmidi;	/* Legacy Midi interface. */
	struct snd_ac97 *codec;

	/* Stream structs. */
	stream_t dma_adb[NR_ADB];
	int spdif_sr;
#ifndef CHIP_AU8810
	stream_t dma_wt[NR_WT];
	wt_voice_t wt_voice[NR_WT];	/* WT register cache. */
	char mixwt[(NR_WT / NR_WTPB) * 6];	/* WT mixin objects */
#endif

	/* Global resources */
	s8 mixcapt[2];
	s8 mixplayb[4];
#ifndef CHIP_AU8820
	s8 mixspdif[2];
	s8 mixa3d[2];	/* mixers which collect all a3d streams. */
	s8 mixxtlk[2];	/* crosstalk canceler mixer inputs. */
#endif
	u32 fixed_res[5];

#ifndef CHIP_AU8820
	/* Hardware equalizer structs */
	eqlzr_t eq;
	/* A3D structs */
	a3dsrc_t a3d[NR_A3D];
	/* Xtalk canceler */
	int xt_mode;		/* 1: speakers, 0:headphones. */
#endif

	int isquad;		/* cache of extended ID codec flag. */

	/* Gameport stuff. */
	struct gameport *gameport;

	/* PCI hardware resources */
	unsigned long io;
	unsigned long __iomem *mmio;
	unsigned int irq;
	spinlock_t lock;

	/* PCI device */
	struct pci_dev *pci_dev;
	u16 vendor;
	u16 device;
	u8 rev;
};

/* Functions. */

/* SRC */
static void vortex_adb_setsrc(vortex_t * vortex, int adbdma,
			      unsigned int cvrt, int dir);

/* DMA Engines. */
static void vortex_adbdma_setbuffers(vortex_t * vortex, int adbdma,
				     struct snd_sg_buf * sgbuf, int size,
				     int count);
static void vortex_adbdma_setmode(vortex_t * vortex, int adbdma, int ie,
				  int dir, int fmt, int d,
				  unsigned long offset);
static void vortex_adbdma_setstartbuffer(vortex_t * vortex, int adbdma, int sb);
#ifndef CHIP_AU8810
static void vortex_wtdma_setbuffers(vortex_t * vortex, int wtdma,
				    struct snd_sg_buf * sgbuf, int size,
				    int count);
static void vortex_wtdma_setmode(vortex_t * vortex, int wtdma, int ie, int fmt, int d,	/*int e, */
				 unsigned long offset);
static void vortex_wtdma_setstartbuffer(vortex_t * vortex, int wtdma, int sb);
#endif

static void vortex_adbdma_startfifo(vortex_t * vortex, int adbdma);
//static void vortex_adbdma_stopfifo(vortex_t *vortex, int adbdma);
static void vortex_adbdma_pausefifo(vortex_t * vortex, int adbdma);
static void vortex_adbdma_resumefifo(vortex_t * vortex, int adbdma);
static int inline vortex_adbdma_getlinearpos(vortex_t * vortex, int adbdma);
static void vortex_adbdma_resetup(vortex_t *vortex, int adbdma);

#ifndef CHIP_AU8810
static void vortex_wtdma_startfifo(vortex_t * vortex, int wtdma);
static void vortex_wtdma_stopfifo(vortex_t * vortex, int wtdma);
static void vortex_wtdma_pausefifo(vortex_t * vortex, int wtdma);
static void vortex_wtdma_resumefifo(vortex_t * vortex, int wtdma);
static int inline vortex_wtdma_getlinearpos(vortex_t * vortex, int wtdma);
#endif

/* global stuff. */
static void vortex_codec_init(vortex_t * vortex);
static void vortex_codec_write(struct snd_ac97 * codec, unsigned short addr,
			       unsigned short data);
static unsigned short vortex_codec_read(struct snd_ac97 * codec, unsigned short addr);
static void vortex_spdif_init(vortex_t * vortex, int spdif_sr, int spdif_mode);

static int vortex_core_init(vortex_t * card);
static int vortex_core_shutdown(vortex_t * card);
static void vortex_enable_int(vortex_t * card);
static irqreturn_t vortex_interrupt(int irq, void *dev_id,
				    struct pt_regs *regs);
static int vortex_alsafmt_aspfmt(int alsafmt);

/* Connection  stuff. */
static void vortex_connect_default(vortex_t * vortex, int en);
static int vortex_adb_allocroute(vortex_t * vortex, int dma, int nr_ch,
				 int dir, int type);
static char vortex_adb_checkinout(vortex_t * vortex, int resmap[], int out,
				  int restype);
#ifndef CHIP_AU8810
static int vortex_wt_allocroute(vortex_t * vortex, int dma, int nr_ch);
static void vortex_wt_connect(vortex_t * vortex, int en);
static void vortex_wt_init(vortex_t * vortex);
#endif

static void vortex_route(vortex_t * vortex, int en, unsigned char channel,
			 unsigned char source, unsigned char dest);
#if 0
static void vortex_routes(vortex_t * vortex, int en, unsigned char channel,
			  unsigned char source, unsigned char dest0,
			  unsigned char dest1);
#endif
static void vortex_connection_mixin_mix(vortex_t * vortex, int en,
					unsigned char mixin,
					unsigned char mix, int a);
static void vortex_mix_setinputvolumebyte(vortex_t * vortex,
					  unsigned char mix, int mixin,
					  unsigned char vol);
static void vortex_mix_setvolumebyte(vortex_t * vortex, unsigned char mix,
				     unsigned char vol);

/* A3D functions. */
#ifndef CHIP_AU8820
static void vortex_Vort3D(vortex_t * v, int en);
static void vortex_Vort3D_connect(vortex_t * vortex, int en);
static void vortex_Vort3D_InitializeSource(a3dsrc_t * a, int en);
#endif

/* Driver stuff. */
static int __devinit vortex_gameport_register(vortex_t * card);
static void vortex_gameport_unregister(vortex_t * card);
#ifndef CHIP_AU8820
static int __devinit vortex_eq_init(vortex_t * vortex);
static int __devexit vortex_eq_free(vortex_t * vortex);
#endif
/* ALSA stuff. */
static int __devinit snd_vortex_new_pcm(vortex_t * vortex, int idx, int nr);
static int __devinit snd_vortex_mixer(vortex_t * vortex);
static int __devinit snd_vortex_midi(vortex_t * vortex);
#endif
