#ifndef __YMFPCI_H
#define __YMFPCI_H

/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Definitions for Yahama YMF724/740/744/754 chips
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/config.h>
#include <linux/mutex.h>

/*
 *  Direct registers
 */

/* #define YMFREG(codec, reg)		(codec->port + YDSXGR_##reg) */

#define	YDSXGR_INTFLAG			0x0004
#define	YDSXGR_ACTIVITY			0x0006
#define	YDSXGR_GLOBALCTRL		0x0008
#define	YDSXGR_ZVCTRL			0x000A
#define	YDSXGR_TIMERCTRL		0x0010
#define	YDSXGR_TIMERCTRL_TEN		 0x0001
#define	YDSXGR_TIMERCTRL_TIEN		 0x0002
#define	YDSXGR_TIMERCOUNT		0x0012
#define	YDSXGR_SPDIFOUTCTRL		0x0018
#define	YDSXGR_SPDIFOUTSTATUS		0x001C
#define	YDSXGR_EEPROMCTRL		0x0020
#define	YDSXGR_SPDIFINCTRL		0x0034
#define	YDSXGR_SPDIFINSTATUS		0x0038
#define	YDSXGR_DSPPROGRAMDL		0x0048
#define	YDSXGR_DLCNTRL			0x004C
#define	YDSXGR_GPIOININTFLAG		0x0050
#define	YDSXGR_GPIOININTENABLE		0x0052
#define	YDSXGR_GPIOINSTATUS		0x0054
#define	YDSXGR_GPIOOUTCTRL		0x0056
#define	YDSXGR_GPIOFUNCENABLE		0x0058
#define	YDSXGR_GPIOTYPECONFIG		0x005A
#define	YDSXGR_AC97CMDDATA		0x0060
#define	YDSXGR_AC97CMDADR		0x0062
#define	YDSXGR_PRISTATUSDATA		0x0064
#define	YDSXGR_PRISTATUSADR		0x0066
#define	YDSXGR_SECSTATUSDATA		0x0068
#define	YDSXGR_SECSTATUSADR		0x006A
#define	YDSXGR_SECCONFIG		0x0070
#define	YDSXGR_LEGACYOUTVOL		0x0080
#define	YDSXGR_LEGACYOUTVOLL		0x0080
#define	YDSXGR_LEGACYOUTVOLR		0x0082
#define	YDSXGR_NATIVEDACOUTVOL		0x0084
#define	YDSXGR_NATIVEDACOUTVOLL		0x0084
#define	YDSXGR_NATIVEDACOUTVOLR		0x0086
#define	YDSXGR_SPDIFOUTVOL		0x0088
#define	YDSXGR_SPDIFOUTVOLL		0x0088
#define	YDSXGR_SPDIFOUTVOLR		0x008A
#define	YDSXGR_AC3OUTVOL		0x008C
#define	YDSXGR_AC3OUTVOLL		0x008C
#define	YDSXGR_AC3OUTVOLR		0x008E
#define	YDSXGR_PRIADCOUTVOL		0x0090
#define	YDSXGR_PRIADCOUTVOLL		0x0090
#define	YDSXGR_PRIADCOUTVOLR		0x0092
#define	YDSXGR_LEGACYLOOPVOL		0x0094
#define	YDSXGR_LEGACYLOOPVOLL		0x0094
#define	YDSXGR_LEGACYLOOPVOLR		0x0096
#define	YDSXGR_NATIVEDACLOOPVOL		0x0098
#define	YDSXGR_NATIVEDACLOOPVOLL	0x0098
#define	YDSXGR_NATIVEDACLOOPVOLR	0x009A
#define	YDSXGR_SPDIFLOOPVOL		0x009C
#define	YDSXGR_SPDIFLOOPVOLL		0x009E
#define	YDSXGR_SPDIFLOOPVOLR		0x009E
#define	YDSXGR_AC3LOOPVOL		0x00A0
#define	YDSXGR_AC3LOOPVOLL		0x00A0
#define	YDSXGR_AC3LOOPVOLR		0x00A2
#define	YDSXGR_PRIADCLOOPVOL		0x00A4
#define	YDSXGR_PRIADCLOOPVOLL		0x00A4
#define	YDSXGR_PRIADCLOOPVOLR		0x00A6
#define	YDSXGR_NATIVEADCINVOL		0x00A8
#define	YDSXGR_NATIVEADCINVOLL		0x00A8
#define	YDSXGR_NATIVEADCINVOLR		0x00AA
#define	YDSXGR_NATIVEDACINVOL		0x00AC
#define	YDSXGR_NATIVEDACINVOLL		0x00AC
#define	YDSXGR_NATIVEDACINVOLR		0x00AE
#define	YDSXGR_BUF441OUTVOL		0x00B0
#define	YDSXGR_BUF441OUTVOLL		0x00B0
#define	YDSXGR_BUF441OUTVOLR		0x00B2
#define	YDSXGR_BUF441LOOPVOL		0x00B4
#define	YDSXGR_BUF441LOOPVOLL		0x00B4
#define	YDSXGR_BUF441LOOPVOLR		0x00B6
#define	YDSXGR_SPDIFOUTVOL2		0x00B8
#define	YDSXGR_SPDIFOUTVOL2L		0x00B8
#define	YDSXGR_SPDIFOUTVOL2R		0x00BA
#define	YDSXGR_SPDIFLOOPVOL2		0x00BC
#define	YDSXGR_SPDIFLOOPVOL2L		0x00BC
#define	YDSXGR_SPDIFLOOPVOL2R		0x00BE
#define	YDSXGR_ADCSLOTSR		0x00C0
#define	YDSXGR_RECSLOTSR		0x00C4
#define	YDSXGR_ADCFORMAT		0x00C8
#define	YDSXGR_RECFORMAT		0x00CC
#define	YDSXGR_P44SLOTSR		0x00D0
#define	YDSXGR_STATUS			0x0100
#define	YDSXGR_CTRLSELECT		0x0104
#define	YDSXGR_MODE			0x0108
#define	YDSXGR_SAMPLECOUNT		0x010C
#define	YDSXGR_NUMOFSAMPLES		0x0110
#define	YDSXGR_CONFIG			0x0114
#define	YDSXGR_PLAYCTRLSIZE		0x0140
#define	YDSXGR_RECCTRLSIZE		0x0144
#define	YDSXGR_EFFCTRLSIZE		0x0148
#define	YDSXGR_WORKSIZE			0x014C
#define	YDSXGR_MAPOFREC			0x0150
#define	YDSXGR_MAPOFEFFECT		0x0154
#define	YDSXGR_PLAYCTRLBASE		0x0158
#define	YDSXGR_RECCTRLBASE		0x015C
#define	YDSXGR_EFFCTRLBASE		0x0160
#define	YDSXGR_WORKBASE			0x0164
#define	YDSXGR_DSPINSTRAM		0x1000
#define	YDSXGR_CTRLINSTRAM		0x4000

#define YDSXG_AC97READCMD		0x8000
#define YDSXG_AC97WRITECMD		0x0000

#define PCIR_LEGCTRL			0x40
#define PCIR_ELEGCTRL			0x42
#define PCIR_DSXGCTRL			0x48
#define PCIR_DSXPWRCTRL1		0x4a
#define PCIR_DSXPWRCTRL2		0x4e
#define PCIR_OPLADR			0x60
#define PCIR_SBADR			0x62
#define PCIR_MPUADR			0x64

#define YDSXG_DSPLENGTH			0x0080
#define YDSXG_CTRLLENGTH		0x3000

#define YDSXG_DEFAULT_WORK_SIZE		0x0400

#define YDSXG_PLAYBACK_VOICES		64
#define YDSXG_CAPTURE_VOICES		2
#define YDSXG_EFFECT_VOICES		5

/* maxinum number of AC97 codecs connected, AC97 2.0 defined 4 */
#define NR_AC97		2

#define YMF_SAMPF			256	/* Samples per frame @48000 */

/*
 * The slot/voice control bank (2 of these per voice)
 */

typedef struct stru_ymfpci_playback_bank {
	u32 format;
	u32 loop_default;
	u32 base;			/* 32-bit address */
	u32 loop_start;			/* 32-bit offset */
	u32 loop_end;			/* 32-bit offset */
	u32 loop_frac;			/* 8-bit fraction - loop_start */
	u32 delta_end;			/* pitch delta end */
	u32 lpfK_end;
	u32 eg_gain_end;
	u32 left_gain_end;
	u32 right_gain_end;
	u32 eff1_gain_end;
	u32 eff2_gain_end;
	u32 eff3_gain_end;
	u32 lpfQ;
	u32 status;		/* P3: Always 0 for some reason. */
	u32 num_of_frames;
	u32 loop_count;
	u32 start;		/* P3: J. reads this to know where chip is. */
	u32 start_frac;
	u32 delta;
	u32 lpfK;
	u32 eg_gain;
	u32 left_gain;
	u32 right_gain;
	u32 eff1_gain;
	u32 eff2_gain;
	u32 eff3_gain;
	u32 lpfD1;
	u32 lpfD2;
} ymfpci_playback_bank_t;

typedef struct stru_ymfpci_capture_bank {
	u32 base;			/* 32-bit address (aligned at 4) */
	u32 loop_end;			/* size in BYTES (aligned at 4) */
	u32 start;			/* 32-bit offset */
	u32 num_of_loops;		/* counter */
} ymfpci_capture_bank_t;

typedef struct stru_ymfpci_effect_bank {
	u32 base;			/* 32-bit address */
	u32 loop_end;			/* 32-bit offset */
	u32 start;			/* 32-bit offset */
	u32 temp;
} ymfpci_effect_bank_t;

typedef struct ymf_voice ymfpci_voice_t;
/*
 * Throughout the code Yaroslav names YMF unit pointer "codec"
 * even though it does not correspond to any codec. Must be historic.
 * We replace it with "unit" over time.
 * AC97 parts use "codec" to denote a codec, naturally.
 */
typedef struct ymf_unit ymfpci_t;

typedef enum {
	YMFPCI_PCM,
	YMFPCI_SYNTH,
	YMFPCI_MIDI
} ymfpci_voice_type_t;

struct ymf_voice {
	// ymfpci_t *codec;
	int number;
	char use, pcm, synth, midi;	// bool
	ymfpci_playback_bank_t *bank;
	struct ymf_pcm *ypcm;
	dma_addr_t bank_ba;
};

struct ymf_capture {
	// struct ymf_unit *unit;
	int use;
	ymfpci_capture_bank_t *bank;
	struct ymf_pcm *ypcm;
};

struct ymf_unit {
	u8 rev;				/* PCI revision */
	void __iomem *reg_area_virt;
	void *dma_area_va;
	dma_addr_t dma_area_ba;
	unsigned int dma_area_size;

	dma_addr_t bank_base_capture;
	dma_addr_t bank_base_effect;
	dma_addr_t work_base;
	unsigned int work_size;

	u32 *ctrl_playback;
	dma_addr_t ctrl_playback_ba;
	ymfpci_playback_bank_t *bank_playback[YDSXG_PLAYBACK_VOICES][2];
	ymfpci_capture_bank_t *bank_capture[YDSXG_CAPTURE_VOICES][2];
	ymfpci_effect_bank_t *bank_effect[YDSXG_EFFECT_VOICES][2];

	int start_count;
	int suspended;

	u32 active_bank;
	struct ymf_voice voices[YDSXG_PLAYBACK_VOICES];
	struct ymf_capture capture[YDSXG_CAPTURE_VOICES];

	struct ac97_codec *ac97_codec[NR_AC97];
	u16 ac97_features;

	struct pci_dev *pci;

#ifdef CONFIG_SOUND_YMFPCI_LEGACY
	/* legacy hardware resources */
	unsigned int iosynth, iomidi;
	struct address_info opl3_data, mpu_data;
#endif

	spinlock_t reg_lock;
	spinlock_t voice_lock;
	spinlock_t ac97_lock;

	/* soundcore stuff */
	int dev_audio;
	struct mutex open_mutex;

	struct list_head ymf_devs;
	struct list_head states;	/* List of states for this unit */
};

struct ymf_dmabuf {
	dma_addr_t dma_addr;
	void *rawbuf;
	unsigned buforder;

	/* OSS buffer management stuff */
	unsigned numfrag;
	unsigned fragshift;

	/* our buffer acts like a circular ring */
	unsigned hwptr;		/* where dma last started */
	unsigned swptr;		/* where driver last clear/filled */
	int count;		/* fill count */
	unsigned total_bytes;	/* total bytes dmaed by hardware */

	wait_queue_head_t wait;	/* put process on wait queue when no more space in buffer */

	/* redundant, but makes calculations easier */
	unsigned fragsize;
	unsigned dmasize;	/* Total rawbuf[] size */

	/* OSS stuff */
	unsigned mapped:1;
	unsigned ready:1;
	unsigned ossfragshift;
	int ossmaxfrags;
	unsigned subdivision;
};

struct ymf_pcm_format {
	int format;			/* OSS format */
	int rate;			/* rate in Hz */
	int voices;			/* number of voices */
	int shift;			/* redundant, computed from the above */
};

typedef enum {
	PLAYBACK_VOICE,
	CAPTURE_REC,
	CAPTURE_AC97,
	EFFECT_DRY_LEFT,
	EFFECT_DRY_RIGHT,
	EFFECT_EFF1,
	EFFECT_EFF2,
	EFFECT_EFF3
} ymfpci_pcm_type_t;

/* This is variant record, but we hate unions. Little waste on pointers []. */
struct ymf_pcm {
	ymfpci_pcm_type_t type;
	struct ymf_state *state;

	ymfpci_voice_t *voices[2];
	int capture_bank_number;

	struct ymf_dmabuf dmabuf;
	int running;
	int spdif;
};

/*
 * "Software" or virtual channel, an instance of opened /dev/dsp.
 * It may have two physical channels (pcms) for duplex operations.
 */

struct ymf_state {
	struct list_head chain;
	struct ymf_unit *unit;			/* backpointer */
	struct ymf_pcm rpcm, wpcm;
	struct ymf_pcm_format format;
};

#endif				/* __YMFPCI_H */
