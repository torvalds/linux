#ifndef __SOUND_YMFPCI_H
#define __SOUND_YMFPCI_H

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
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include "pcm.h"
#include "rawmidi.h"
#include "ac97_codec.h"
#include "timer.h"
#include <linux/gameport.h>

/*
 *  Direct registers
 */

#define YMFREG(chip, reg)		(chip->port + YDSXGR_##reg)

#define	YDSXGR_INTFLAG			0x0004
#define	YDSXGR_ACTIVITY			0x0006
#define	YDSXGR_GLOBALCTRL		0x0008
#define	YDSXGR_ZVCTRL			0x000A
#define	YDSXGR_TIMERCTRL		0x0010
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
#define	YDSXGR_ZVOUTVOL			0x0088
#define	YDSXGR_ZVOUTVOLL		0x0088
#define	YDSXGR_ZVOUTVOLR		0x008A
#define	YDSXGR_SECADCOUTVOL		0x008C
#define	YDSXGR_SECADCOUTVOLL		0x008C
#define	YDSXGR_SECADCOUTVOLR		0x008E
#define	YDSXGR_PRIADCOUTVOL		0x0090
#define	YDSXGR_PRIADCOUTVOLL		0x0090
#define	YDSXGR_PRIADCOUTVOLR		0x0092
#define	YDSXGR_LEGACYLOOPVOL		0x0094
#define	YDSXGR_LEGACYLOOPVOLL		0x0094
#define	YDSXGR_LEGACYLOOPVOLR		0x0096
#define	YDSXGR_NATIVEDACLOOPVOL		0x0098
#define	YDSXGR_NATIVEDACLOOPVOLL	0x0098
#define	YDSXGR_NATIVEDACLOOPVOLR	0x009A
#define	YDSXGR_ZVLOOPVOL		0x009C
#define	YDSXGR_ZVLOOPVOLL		0x009E
#define	YDSXGR_ZVLOOPVOLR		0x009E
#define	YDSXGR_SECADCLOOPVOL		0x00A0
#define	YDSXGR_SECADCLOOPVOLL		0x00A0
#define	YDSXGR_SECADCLOOPVOLR		0x00A2
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
#define	YDSXGR_SPDIFOUTVOL		0x00B8
#define	YDSXGR_SPDIFOUTVOLL		0x00B8
#define	YDSXGR_SPDIFOUTVOLR		0x00BA
#define	YDSXGR_SPDIFLOOPVOL		0x00BC
#define	YDSXGR_SPDIFLOOPVOLL		0x00BC
#define	YDSXGR_SPDIFLOOPVOLR		0x00BE
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

#define PCIR_DSXG_LEGACY		0x40
#define PCIR_DSXG_ELEGACY		0x42
#define PCIR_DSXG_CTRL			0x48
#define PCIR_DSXG_PWRCTRL1		0x4a
#define PCIR_DSXG_PWRCTRL2		0x4e
#define PCIR_DSXG_FMBASE		0x60
#define PCIR_DSXG_SBBASE		0x62
#define PCIR_DSXG_MPU401BASE		0x64
#define PCIR_DSXG_JOYBASE		0x66

#define YDSXG_DSPLENGTH			0x0080
#define YDSXG_CTRLLENGTH		0x3000

#define YDSXG_DEFAULT_WORK_SIZE		0x0400

#define YDSXG_PLAYBACK_VOICES		64
#define YDSXG_CAPTURE_VOICES		2
#define YDSXG_EFFECT_VOICES		5

#define YMFPCI_LEGACY_SBEN	(1 << 0)	/* soundblaster enable */
#define YMFPCI_LEGACY_FMEN	(1 << 1)	/* OPL3 enable */
#define YMFPCI_LEGACY_JPEN	(1 << 2)	/* joystick enable */
#define YMFPCI_LEGACY_MEN	(1 << 3)	/* MPU401 enable */
#define YMFPCI_LEGACY_MIEN	(1 << 4)	/* MPU RX irq enable */
#define YMFPCI_LEGACY_IOBITS	(1 << 5)	/* i/o bits range, 0 = 16bit, 1 =10bit */
#define YMFPCI_LEGACY_SDMA	(3 << 6)	/* SB DMA select */
#define YMFPCI_LEGACY_SBIRQ	(7 << 8)	/* SB IRQ select */
#define YMFPCI_LEGACY_MPUIRQ	(7 << 11)	/* MPU IRQ select */
#define YMFPCI_LEGACY_SIEN	(1 << 14)	/* serialized IRQ */
#define YMFPCI_LEGACY_LAD	(1 << 15)	/* legacy audio disable */

#define YMFPCI_LEGACY2_FMIO	(3 << 0)	/* OPL3 i/o address (724/740) */
#define YMFPCI_LEGACY2_SBIO	(3 << 2)	/* SB i/o address (724/740) */
#define YMFPCI_LEGACY2_MPUIO	(3 << 4)	/* MPU401 i/o address (724/740) */
#define YMFPCI_LEGACY2_JSIO	(3 << 6)	/* joystick i/o address (724/740) */
#define YMFPCI_LEGACY2_MAIM	(1 << 8)	/* MPU401 ack intr mask */
#define YMFPCI_LEGACY2_SMOD	(3 << 11)	/* SB DMA mode */
#define YMFPCI_LEGACY2_SBVER	(3 << 13)	/* SB version select */
#define YMFPCI_LEGACY2_IMOD	(1 << 15)	/* legacy IRQ mode */
/* SIEN:IMOD 0:0 = legacy irq, 0:1 = INTA, 1:0 = serialized IRQ */

#if defined(CONFIG_GAMEPORT) || (defined(MODULE) && defined(CONFIG_GAMEPORT_MODULE))
#define SUPPORT_JOYSTICK
#endif

/*
 *
 */

struct snd_ymfpci_playback_bank {
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
	u32 status;
	u32 num_of_frames;
	u32 loop_count;
	u32 start;
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
 };

struct snd_ymfpci_capture_bank {
	u32 base;			/* 32-bit address */
	u32 loop_end;			/* 32-bit offset */
	u32 start;			/* 32-bit offset */
	u32 num_of_loops;		/* counter */
};

struct snd_ymfpci_effect_bank {
	u32 base;			/* 32-bit address */
	u32 loop_end;			/* 32-bit offset */
	u32 start;			/* 32-bit offset */
	u32 temp;
};

struct snd_ymfpci_pcm;
struct snd_ymfpci;

enum snd_ymfpci_voice_type {
	YMFPCI_PCM,
	YMFPCI_SYNTH,
	YMFPCI_MIDI
};

struct snd_ymfpci_voice {
	struct snd_ymfpci *chip;
	int number;
	unsigned int use: 1,
	    pcm: 1,
	    synth: 1,
	    midi: 1;
	struct snd_ymfpci_playback_bank *bank;
	dma_addr_t bank_addr;
	void (*interrupt)(struct snd_ymfpci *chip, struct snd_ymfpci_voice *voice);
	struct snd_ymfpci_pcm *ypcm;
};

enum snd_ymfpci_pcm_type {
	PLAYBACK_VOICE,
	CAPTURE_REC,
	CAPTURE_AC97,
	EFFECT_DRY_LEFT,
	EFFECT_DRY_RIGHT,
	EFFECT_EFF1,
	EFFECT_EFF2,
	EFFECT_EFF3
};

struct snd_ymfpci_pcm {
	struct snd_ymfpci *chip;
	enum snd_ymfpci_pcm_type type;
	struct snd_pcm_substream *substream;
	struct snd_ymfpci_voice *voices[2];	/* playback only */
	unsigned int running: 1,
	             output_front: 1,
	             output_rear: 1,
	             swap_rear: 1;
	unsigned int update_pcm_vol;
	u32 period_size;		/* cached from runtime->period_size */
	u32 buffer_size;		/* cached from runtime->buffer_size */
	u32 period_pos;
	u32 last_pos;
	u32 capture_bank_number;
	u32 shift;
};

struct snd_ymfpci {
	int irq;

	unsigned int device_id;	/* PCI device ID */
	unsigned int rev;	/* PCI revision */
	unsigned long reg_area_phys;
	void __iomem *reg_area_virt;
	struct resource *res_reg_area;
	struct resource *fm_res;
	struct resource *mpu_res;

	unsigned short old_legacy_ctrl;
#ifdef SUPPORT_JOYSTICK
	struct gameport *gameport;
#endif

	struct snd_dma_buffer work_ptr;

	unsigned int bank_size_playback;
	unsigned int bank_size_capture;
	unsigned int bank_size_effect;
	unsigned int work_size;

	void *bank_base_playback;
	void *bank_base_capture;
	void *bank_base_effect;
	void *work_base;
	dma_addr_t bank_base_playback_addr;
	dma_addr_t bank_base_capture_addr;
	dma_addr_t bank_base_effect_addr;
	dma_addr_t work_base_addr;
	struct snd_dma_buffer ac3_tmp_base;

	u32 *ctrl_playback;
	struct snd_ymfpci_playback_bank *bank_playback[YDSXG_PLAYBACK_VOICES][2];
	struct snd_ymfpci_capture_bank *bank_capture[YDSXG_CAPTURE_VOICES][2];
	struct snd_ymfpci_effect_bank *bank_effect[YDSXG_EFFECT_VOICES][2];

	int start_count;

	u32 active_bank;
	struct snd_ymfpci_voice voices[64];

	struct snd_ac97_bus *ac97_bus;
	struct snd_ac97 *ac97;
	struct snd_rawmidi *rawmidi;
	struct snd_timer *timer;

	struct pci_dev *pci;
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_pcm *pcm2;
	struct snd_pcm *pcm_spdif;
	struct snd_pcm *pcm_4ch;
	struct snd_pcm_substream *capture_substream[YDSXG_CAPTURE_VOICES];
	struct snd_pcm_substream *effect_substream[YDSXG_EFFECT_VOICES];
	struct snd_kcontrol *ctl_vol_recsrc;
	struct snd_kcontrol *ctl_vol_adcrec;
	struct snd_kcontrol *ctl_vol_spdifrec;
	unsigned short spdif_bits, spdif_pcm_bits;
	struct snd_kcontrol *spdif_pcm_ctl;
	int mode_dup4ch;
	int rear_opened;
	int rear_swap;
	int spdif_opened;
	struct {
		u16 left;
		u16 right;
		struct snd_kcontrol *ctl;
	} pcm_mixer[32];

	spinlock_t reg_lock;
	spinlock_t voice_lock;
	wait_queue_head_t interrupt_sleep;
	atomic_t interrupt_sleep_count;
	struct snd_info_entry *proc_entry;

#ifdef CONFIG_PM
	u32 *saved_regs;
	u32 saved_ydsxgr_mode;
#endif
};

int snd_ymfpci_create(struct snd_card *card,
		      struct pci_dev *pci,
		      unsigned short old_legacy_ctrl,
		      struct snd_ymfpci ** rcodec);
void snd_ymfpci_free_gameport(struct snd_ymfpci *chip);

int snd_ymfpci_suspend(struct pci_dev *pci, pm_message_t state);
int snd_ymfpci_resume(struct pci_dev *pci);

int snd_ymfpci_pcm(struct snd_ymfpci *chip, int device, struct snd_pcm **rpcm);
int snd_ymfpci_pcm2(struct snd_ymfpci *chip, int device, struct snd_pcm **rpcm);
int snd_ymfpci_pcm_spdif(struct snd_ymfpci *chip, int device, struct snd_pcm **rpcm);
int snd_ymfpci_pcm_4ch(struct snd_ymfpci *chip, int device, struct snd_pcm **rpcm);
int snd_ymfpci_mixer(struct snd_ymfpci *chip, int rear_switch, int rear_swap);
int snd_ymfpci_timer(struct snd_ymfpci *chip, int device);

#endif /* __SOUND_YMFPCI_H */
