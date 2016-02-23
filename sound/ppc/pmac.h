/*
 * Driver for PowerMac onboard soundchips
 * Copyright (c) 2001 by Takashi Iwai <tiwai@suse.de>
 *   based on dmasound.c.
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
 */


#ifndef __PMAC_H
#define __PMAC_H

#include <sound/control.h>
#include <sound/pcm.h>
#include "awacs.h"

#include <linux/adb.h>
#ifdef CONFIG_ADB_CUDA
#include <linux/cuda.h>
#endif
#ifdef CONFIG_ADB_PMU
#include <linux/pmu.h>
#endif
#include <linux/nvram.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>
#include <asm/dbdma.h>
#include <asm/prom.h>
#include <asm/machdep.h>

/* maximum number of fragments */
#define PMAC_MAX_FRAGS		32


#define PMAC_SUPPORT_AUTOMUTE

/*
 * DBDMA space
 */
struct pmac_dbdma {
	dma_addr_t dma_base;
	dma_addr_t addr;
	struct dbdma_cmd __iomem *cmds;
	void *space;
	int size;
};

/*
 * playback/capture stream
 */
struct pmac_stream {
	int running;	/* boolean */

	int stream;	/* PLAYBACK/CAPTURE */

	int dma_size; /* in bytes */
	int period_size; /* in bytes */
	int buffer_size; /* in kbytes */
	int nperiods, cur_period;

	struct pmac_dbdma cmd;
	volatile struct dbdma_regs __iomem *dma;

	struct snd_pcm_substream *substream;

	unsigned int cur_freqs;		/* currently available frequencies */
	unsigned int cur_formats;	/* currently available formats */
};


/*
 */

enum snd_pmac_model {
	PMAC_AWACS, PMAC_SCREAMER, PMAC_BURGUNDY, PMAC_DACA, PMAC_TUMBLER,
	PMAC_SNAPPER
};

struct snd_pmac {
	struct snd_card *card;

	/* h/w info */
	struct device_node *node;
	struct pci_dev *pdev;
	unsigned int revision;
	unsigned int manufacturer;
	unsigned int subframe;
	unsigned int device_id;
	enum snd_pmac_model model;

	unsigned int has_iic : 1;
	unsigned int is_pbook_3400 : 1;
	unsigned int is_pbook_G3 : 1;
	unsigned int is_k2 : 1;

	unsigned int can_byte_swap : 1;
	unsigned int can_duplex : 1;
	unsigned int can_capture : 1;

	unsigned int auto_mute : 1;
	unsigned int initialized : 1;
	unsigned int feature_is_set : 1;

	unsigned int requested;
	struct resource rsrc[3];

	int num_freqs;
	int *freq_table;
	unsigned int freqs_ok;		/* bit flags */
	unsigned int formats_ok;	/* pcm hwinfo */
	int active;
	int rate_index;
	int format;			/* current format */

	spinlock_t reg_lock;
	volatile struct awacs_regs __iomem *awacs;
	int awacs_reg[8]; /* register cache */
	unsigned int hp_stat_mask;

	unsigned char __iomem *latch_base;
	unsigned char __iomem *macio_base;

	struct pmac_stream playback;
	struct pmac_stream capture;

	struct pmac_dbdma extra_dma;

	int irq, tx_irq, rx_irq;

	struct snd_pcm *pcm;

	struct pmac_beep *beep;

	unsigned int control_mask;	/* control mask */

	/* mixer stuffs */
	void *mixer_data;
	void (*mixer_free)(struct snd_pmac *);
	struct snd_kcontrol *master_sw_ctl;
	struct snd_kcontrol *speaker_sw_ctl;
	struct snd_kcontrol *drc_sw_ctl;	/* only used for tumbler -ReneR */
	struct snd_kcontrol *hp_detect_ctl;
	struct snd_kcontrol *lineout_sw_ctl;

	/* lowlevel callbacks */
	void (*set_format)(struct snd_pmac *chip);
	void (*update_automute)(struct snd_pmac *chip, int do_notify);
	int (*detect_headphone)(struct snd_pmac *chip);
#ifdef CONFIG_PM
	void (*suspend)(struct snd_pmac *chip);
	void (*resume)(struct snd_pmac *chip);
#endif

};


/* exported functions */
int snd_pmac_new(struct snd_card *card, struct snd_pmac **chip_return);
int snd_pmac_pcm_new(struct snd_pmac *chip);
int snd_pmac_attach_beep(struct snd_pmac *chip);
void snd_pmac_detach_beep(struct snd_pmac *chip);
void snd_pmac_beep_stop(struct snd_pmac *chip);
unsigned int snd_pmac_rate_index(struct snd_pmac *chip, struct pmac_stream *rec, unsigned int rate);

void snd_pmac_beep_dma_start(struct snd_pmac *chip, int bytes, unsigned long addr, int speed);
void snd_pmac_beep_dma_stop(struct snd_pmac *chip);

#ifdef CONFIG_PM
void snd_pmac_suspend(struct snd_pmac *chip);
void snd_pmac_resume(struct snd_pmac *chip);
#endif

/* initialize mixer */
int snd_pmac_awacs_init(struct snd_pmac *chip);
int snd_pmac_burgundy_init(struct snd_pmac *chip);
int snd_pmac_daca_init(struct snd_pmac *chip);
int snd_pmac_tumbler_init(struct snd_pmac *chip);
int snd_pmac_tumbler_post_init(void);

/* i2c functions */
struct pmac_keywest {
	int addr;
	struct i2c_client *client;
	int id;
	int (*init_client)(struct pmac_keywest *i2c);
	char *name;
};

int snd_pmac_keywest_init(struct pmac_keywest *i2c);
void snd_pmac_keywest_cleanup(struct pmac_keywest *i2c);

/* misc */
#define snd_pmac_boolean_stereo_info	snd_ctl_boolean_stereo_info
#define snd_pmac_boolean_mono_info	snd_ctl_boolean_mono_info

int snd_pmac_add_automute(struct snd_pmac *chip);

#endif /* __PMAC_H */
