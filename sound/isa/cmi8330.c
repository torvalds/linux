/*
 *  Driver for C-Media's CMI8330 and CMI8329 soundcards.
 *  Copyright (c) by George Talusan <gstalusan@uwaterloo.ca>
 *    http://www.undergrad.math.uwaterloo.ca/~gstalusa
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

/*
 * NOTES
 *
 *  The extended registers contain mixer settings which are largely
 *  untapped for the time being.
 *
 *  MPU401 and SPDIF are not supported yet.  I don't have the hardware
 *  to aid in coding and testing, so I won't bother.
 *
 *  To quickly load the module,
 *
 *  modprobe -a snd-cmi8330 sbport=0x220 sbirq=5 sbdma8=1
 *    sbdma16=5 wssport=0x530 wssirq=11 wssdma=0 fmport=0x388
 *
 *  This card has two mixers and two PCM devices.  I've cheesed it such
 *  that recording and playback can be done through the same device.
 *  The driver "magically" routes the capturing to the AD1848 codec,
 *  and playback to the SB16 codec.  This allows for full-duplex mode
 *  to some extent.
 *  The utilities in alsa-utils are aware of both devices, so passing
 *  the appropriate parameters to amixer and alsactl will give you
 *  full control over both mixers.
 */

#include <linux/init.h>
#include <linux/err.h>
#include <linux/isa.h>
#include <linux/slab.h>
#include <linux/pnp.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/wss.h>
#include <sound/opl3.h>
#include <sound/mpu401.h>
#include <sound/sb.h>
#include <sound/initval.h>

/*
 */
/* #define ENABLE_SB_MIXER */
#define PLAYBACK_ON_SB

/*
 */
MODULE_AUTHOR("George Talusan <gstalusan@uwaterloo.ca>");
MODULE_DESCRIPTION("C-Media CMI8330/CMI8329");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{C-Media,CMI8330,isapnp:{CMI0001,@@@0001,@X@0001}}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP;
#ifdef CONFIG_PNP
static int isapnp[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};
#endif
static long sbport[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
static int sbirq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;
static int sbdma8[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;
static int sbdma16[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;
static long wssport[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
static int wssirq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;
static int wssdma[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;
static long fmport[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
static long mpuport[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
static int mpuirq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for CMI8330/CMI8329 soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string  for CMI8330/CMI8329 soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable CMI8330/CMI8329 soundcard.");
#ifdef CONFIG_PNP
module_param_array(isapnp, bool, NULL, 0444);
MODULE_PARM_DESC(isapnp, "PnP detection for specified soundcard.");
#endif

module_param_array(sbport, long, NULL, 0444);
MODULE_PARM_DESC(sbport, "Port # for CMI8330/CMI8329 SB driver.");
module_param_array(sbirq, int, NULL, 0444);
MODULE_PARM_DESC(sbirq, "IRQ # for CMI8330/CMI8329 SB driver.");
module_param_array(sbdma8, int, NULL, 0444);
MODULE_PARM_DESC(sbdma8, "DMA8 for CMI8330/CMI8329 SB driver.");
module_param_array(sbdma16, int, NULL, 0444);
MODULE_PARM_DESC(sbdma16, "DMA16 for CMI8330/CMI8329 SB driver.");

module_param_array(wssport, long, NULL, 0444);
MODULE_PARM_DESC(wssport, "Port # for CMI8330/CMI8329 WSS driver.");
module_param_array(wssirq, int, NULL, 0444);
MODULE_PARM_DESC(wssirq, "IRQ # for CMI8330/CMI8329 WSS driver.");
module_param_array(wssdma, int, NULL, 0444);
MODULE_PARM_DESC(wssdma, "DMA for CMI8330/CMI8329 WSS driver.");

module_param_array(fmport, long, NULL, 0444);
MODULE_PARM_DESC(fmport, "FM port # for CMI8330/CMI8329 driver.");
module_param_array(mpuport, long, NULL, 0444);
MODULE_PARM_DESC(mpuport, "MPU-401 port # for CMI8330/CMI8329 driver.");
module_param_array(mpuirq, int, NULL, 0444);
MODULE_PARM_DESC(mpuirq, "IRQ # for CMI8330/CMI8329 MPU-401 port.");
#ifdef CONFIG_PNP
static int isa_registered;
static int pnp_registered;
#endif

#define CMI8330_RMUX3D    16
#define CMI8330_MUTEMUX   17
#define CMI8330_OUTPUTVOL 18
#define CMI8330_MASTVOL   19
#define CMI8330_LINVOL    20
#define CMI8330_CDINVOL   21
#define CMI8330_WAVVOL    22
#define CMI8330_RECMUX    23
#define CMI8330_WAVGAIN   24
#define CMI8330_LINGAIN   25
#define CMI8330_CDINGAIN  26

static unsigned char snd_cmi8330_image[((CMI8330_CDINGAIN)-16) + 1] =
{
	0x40,			/* 16 - recording mux (SB-mixer-enabled) */
#ifdef ENABLE_SB_MIXER
	0x40,			/* 17 - mute mux (Mode2) */
#else
	0x0,			/* 17 - mute mux */
#endif
	0x0,			/* 18 - vol */
	0x0,			/* 19 - master volume */
	0x0,			/* 20 - line-in volume */
	0x0,			/* 21 - cd-in volume */
	0x0,			/* 22 - wave volume */
	0x0,			/* 23 - mute/rec mux */
	0x0,			/* 24 - wave rec gain */
	0x0,			/* 25 - line-in rec gain */
	0x0			/* 26 - cd-in rec gain */
};

typedef int (*snd_pcm_open_callback_t)(struct snd_pcm_substream *);

enum card_type {
	CMI8330,
	CMI8329
};

struct snd_cmi8330 {
#ifdef CONFIG_PNP
	struct pnp_dev *cap;
	struct pnp_dev *play;
	struct pnp_dev *mpu;
#endif
	struct snd_card *card;
	struct snd_wss *wss;
	struct snd_sb *sb;

	struct snd_pcm *pcm;
	struct snd_cmi8330_stream {
		struct snd_pcm_ops ops;
		snd_pcm_open_callback_t open;
		void *private_data; /* sb or wss */
	} streams[2];

	enum card_type type;
};

#ifdef CONFIG_PNP

static struct pnp_card_device_id snd_cmi8330_pnpids[] = {
	{ .id = "CMI0001", .devs = { { "@X@0001" }, { "@@@0001" }, { "@H@0001" }, { "A@@0001" } } },
	{ .id = "CMI0001", .devs = { { "@@@0001" }, { "@X@0001" }, { "@H@0001" } } },
	{ .id = "" }
};

MODULE_DEVICE_TABLE(pnp_card, snd_cmi8330_pnpids);

#endif


static struct snd_kcontrol_new snd_cmi8330_controls[] __devinitdata = {
WSS_DOUBLE("Master Playback Volume", 0,
		CMI8330_MASTVOL, CMI8330_MASTVOL, 4, 0, 15, 0),
WSS_SINGLE("Loud Playback Switch", 0,
		CMI8330_MUTEMUX, 6, 1, 1),
WSS_DOUBLE("PCM Playback Switch", 0,
		CS4231_LEFT_OUTPUT, CS4231_RIGHT_OUTPUT, 7, 7, 1, 1),
WSS_DOUBLE("PCM Playback Volume", 0,
		CS4231_LEFT_OUTPUT, CS4231_RIGHT_OUTPUT, 0, 0, 63, 1),
WSS_DOUBLE("Line Playback Switch", 0,
		CMI8330_MUTEMUX, CMI8330_MUTEMUX, 4, 3, 1, 0),
WSS_DOUBLE("Line Playback Volume", 0,
		CMI8330_LINVOL, CMI8330_LINVOL, 4, 0, 15, 0),
WSS_DOUBLE("Line Capture Switch", 0,
		CMI8330_RMUX3D, CMI8330_RMUX3D, 2, 1, 1, 0),
WSS_DOUBLE("Line Capture Volume", 0,
		CMI8330_LINGAIN, CMI8330_LINGAIN, 4, 0, 15, 0),
WSS_DOUBLE("CD Playback Switch", 0,
		CMI8330_MUTEMUX, CMI8330_MUTEMUX, 2, 1, 1, 0),
WSS_DOUBLE("CD Capture Switch", 0,
		CMI8330_RMUX3D, CMI8330_RMUX3D, 4, 3, 1, 0),
WSS_DOUBLE("CD Playback Volume", 0,
		CMI8330_CDINVOL, CMI8330_CDINVOL, 4, 0, 15, 0),
WSS_DOUBLE("CD Capture Volume", 0,
		CMI8330_CDINGAIN, CMI8330_CDINGAIN, 4, 0, 15, 0),
WSS_SINGLE("Mic Playback Switch", 0,
		CMI8330_MUTEMUX, 0, 1, 0),
WSS_SINGLE("Mic Playback Volume", 0,
		CMI8330_OUTPUTVOL, 0, 7, 0),
WSS_SINGLE("Mic Capture Switch", 0,
		CMI8330_RMUX3D, 0, 1, 0),
WSS_SINGLE("Mic Capture Volume", 0,
		CMI8330_OUTPUTVOL, 5, 7, 0),
WSS_DOUBLE("Wavetable Playback Switch", 0,
		CMI8330_RECMUX, CMI8330_RECMUX, 1, 0, 1, 0),
WSS_DOUBLE("Wavetable Playback Volume", 0,
		CMI8330_WAVVOL, CMI8330_WAVVOL, 4, 0, 15, 0),
WSS_DOUBLE("Wavetable Capture Switch", 0,
		CMI8330_RECMUX, CMI8330_RECMUX, 5, 4, 1, 0),
WSS_DOUBLE("Wavetable Capture Volume", 0,
		CMI8330_WAVGAIN, CMI8330_WAVGAIN, 4, 0, 15, 0),
WSS_SINGLE("3D Control - Switch", 0,
		CMI8330_RMUX3D, 5, 1, 1),
WSS_SINGLE("PC Speaker Playback Volume", 0,
		CMI8330_OUTPUTVOL, 3, 3, 0),
WSS_DOUBLE("FM Playback Switch", 0,
		CS4231_AUX2_LEFT_INPUT, CS4231_AUX2_RIGHT_INPUT, 7, 7, 1, 1),
WSS_DOUBLE("FM Playback Volume", 0,
		CS4231_AUX2_LEFT_INPUT, CS4231_AUX2_RIGHT_INPUT, 0, 0, 31, 1),
WSS_SINGLE(SNDRV_CTL_NAME_IEC958("Input ", CAPTURE, SWITCH), 0,
		CMI8330_RMUX3D, 7, 1, 1),
WSS_SINGLE(SNDRV_CTL_NAME_IEC958("Input ", PLAYBACK, SWITCH), 0,
		CMI8330_MUTEMUX, 7, 1, 1),
};

#ifdef ENABLE_SB_MIXER
static struct sbmix_elem cmi8330_sb_mixers[] __devinitdata = {
SB_DOUBLE("SB Master Playback Volume", SB_DSP4_MASTER_DEV, (SB_DSP4_MASTER_DEV + 1), 3, 3, 31),
SB_DOUBLE("Tone Control - Bass", SB_DSP4_BASS_DEV, (SB_DSP4_BASS_DEV + 1), 4, 4, 15),
SB_DOUBLE("Tone Control - Treble", SB_DSP4_TREBLE_DEV, (SB_DSP4_TREBLE_DEV + 1), 4, 4, 15),
SB_DOUBLE("SB PCM Playback Volume", SB_DSP4_PCM_DEV, (SB_DSP4_PCM_DEV + 1), 3, 3, 31),
SB_DOUBLE("SB Synth Playback Volume", SB_DSP4_SYNTH_DEV, (SB_DSP4_SYNTH_DEV + 1), 3, 3, 31),
SB_DOUBLE("SB CD Playback Switch", SB_DSP4_OUTPUT_SW, SB_DSP4_OUTPUT_SW, 2, 1, 1),
SB_DOUBLE("SB CD Playback Volume", SB_DSP4_CD_DEV, (SB_DSP4_CD_DEV + 1), 3, 3, 31),
SB_DOUBLE("SB Line Playback Switch", SB_DSP4_OUTPUT_SW, SB_DSP4_OUTPUT_SW, 4, 3, 1),
SB_DOUBLE("SB Line Playback Volume", SB_DSP4_LINE_DEV, (SB_DSP4_LINE_DEV + 1), 3, 3, 31),
SB_SINGLE("SB Mic Playback Switch", SB_DSP4_OUTPUT_SW, 0, 1),
SB_SINGLE("SB Mic Playback Volume", SB_DSP4_MIC_DEV, 3, 31),
SB_SINGLE("SB PC Speaker Volume", SB_DSP4_SPEAKER_DEV, 6, 3),
SB_DOUBLE("SB Capture Volume", SB_DSP4_IGAIN_DEV, (SB_DSP4_IGAIN_DEV + 1), 6, 6, 3),
SB_DOUBLE("SB Playback Volume", SB_DSP4_OGAIN_DEV, (SB_DSP4_OGAIN_DEV + 1), 6, 6, 3),
SB_SINGLE("SB Mic Auto Gain", SB_DSP4_MIC_AGC, 0, 1),
};

static unsigned char cmi8330_sb_init_values[][2] __devinitdata = {
	{ SB_DSP4_MASTER_DEV + 0, 0 },
	{ SB_DSP4_MASTER_DEV + 1, 0 },
	{ SB_DSP4_PCM_DEV + 0, 0 },
	{ SB_DSP4_PCM_DEV + 1, 0 },
	{ SB_DSP4_SYNTH_DEV + 0, 0 },
	{ SB_DSP4_SYNTH_DEV + 1, 0 },
	{ SB_DSP4_INPUT_LEFT, 0 },
	{ SB_DSP4_INPUT_RIGHT, 0 },
	{ SB_DSP4_OUTPUT_SW, 0 },
	{ SB_DSP4_SPEAKER_DEV, 0 },
};


static int __devinit cmi8330_add_sb_mixers(struct snd_sb *chip)
{
	int idx, err;
	unsigned long flags;

	spin_lock_irqsave(&chip->mixer_lock, flags);
	snd_sbmixer_write(chip, 0x00, 0x00);		/* mixer reset */
	spin_unlock_irqrestore(&chip->mixer_lock, flags);

	/* mute and zero volume channels */
	for (idx = 0; idx < ARRAY_SIZE(cmi8330_sb_init_values); idx++) {
		spin_lock_irqsave(&chip->mixer_lock, flags);
		snd_sbmixer_write(chip, cmi8330_sb_init_values[idx][0],
				  cmi8330_sb_init_values[idx][1]);
		spin_unlock_irqrestore(&chip->mixer_lock, flags);
	}

	for (idx = 0; idx < ARRAY_SIZE(cmi8330_sb_mixers); idx++) {
		if ((err = snd_sbmixer_add_ctl_elem(chip, &cmi8330_sb_mixers[idx])) < 0)
			return err;
	}
	return 0;
}
#endif

static int __devinit snd_cmi8330_mixer(struct snd_card *card, struct snd_cmi8330 *acard)
{
	unsigned int idx;
	int err;

	strcpy(card->mixername, (acard->type == CMI8329) ? "CMI8329" : "CMI8330/C3D");

	for (idx = 0; idx < ARRAY_SIZE(snd_cmi8330_controls); idx++) {
		err = snd_ctl_add(card,
				snd_ctl_new1(&snd_cmi8330_controls[idx],
					     acard->wss));
		if (err < 0)
			return err;
	}

#ifdef ENABLE_SB_MIXER
	if ((err = cmi8330_add_sb_mixers(acard->sb)) < 0)
		return err;
#endif
	return 0;
}

#ifdef CONFIG_PNP
static int __devinit snd_cmi8330_pnp(int dev, struct snd_cmi8330 *acard,
				     struct pnp_card_link *card,
				     const struct pnp_card_device_id *id)
{
	struct pnp_dev *pdev;
	int err;

	/* CMI8329 has a device with ID A@@0001, CMI8330 does not */
	acard->type = (id->devs[3].id[0]) ? CMI8329 : CMI8330;

	acard->cap = pnp_request_card_device(card, id->devs[0].id, NULL);
	if (acard->cap == NULL)
		return -EBUSY;

	acard->play = pnp_request_card_device(card, id->devs[1].id, NULL);
	if (acard->play == NULL)
		return -EBUSY;

	acard->mpu = pnp_request_card_device(card, id->devs[2].id, NULL);
	if (acard->mpu == NULL)
		return -EBUSY;

	pdev = acard->cap;

	err = pnp_activate_dev(pdev);
	if (err < 0) {
		snd_printk(KERN_ERR "AD1848 PnP configure failure\n");
		return -EBUSY;
	}
	wssport[dev] = pnp_port_start(pdev, 0);
	wssdma[dev] = pnp_dma(pdev, 0);
	wssirq[dev] = pnp_irq(pdev, 0);
	if (pnp_port_start(pdev, 1))
		fmport[dev] = pnp_port_start(pdev, 1);

	/* allocate SB16 resources */
	pdev = acard->play;

	err = pnp_activate_dev(pdev);
	if (err < 0) {
		snd_printk(KERN_ERR "SB16 PnP configure failure\n");
		return -EBUSY;
	}
	sbport[dev] = pnp_port_start(pdev, 0);
	sbdma8[dev] = pnp_dma(pdev, 0);
	sbdma16[dev] = pnp_dma(pdev, 1);
	sbirq[dev] = pnp_irq(pdev, 0);
	/* On CMI8239, the OPL3 port might be present in SB16 PnP resources */
	if (fmport[dev] == SNDRV_AUTO_PORT) {
		if (pnp_port_start(pdev, 1))
			fmport[dev] = pnp_port_start(pdev, 1);
		else
			fmport[dev] = 0x388;	/* Or hardwired */
	}

	/* allocate MPU-401 resources */
	pdev = acard->mpu;

	err = pnp_activate_dev(pdev);
	if (err < 0)
		snd_printk(KERN_ERR "MPU-401 PnP configure failure: will be disabled\n");
	else {
		mpuport[dev] = pnp_port_start(pdev, 0);
		mpuirq[dev] = pnp_irq(pdev, 0);
	}
	return 0;
}
#endif

/*
 * PCM interface
 *
 * since we call the different chip interfaces for playback and capture
 * directions, we need a trick.
 *
 * - copy the ops for each direction into a local record.
 * - replace the open callback with the new one, which replaces the
 *   substream->private_data with the corresponding chip instance
 *   and calls again the original open callback of the chip.
 *
 */

#ifdef PLAYBACK_ON_SB
#define CMI_SB_STREAM	SNDRV_PCM_STREAM_PLAYBACK
#define CMI_AD_STREAM	SNDRV_PCM_STREAM_CAPTURE
#else
#define CMI_SB_STREAM	SNDRV_PCM_STREAM_CAPTURE
#define CMI_AD_STREAM	SNDRV_PCM_STREAM_PLAYBACK
#endif

static int snd_cmi8330_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_cmi8330 *chip = snd_pcm_substream_chip(substream);

	/* replace the private_data and call the original open callback */
	substream->private_data = chip->streams[SNDRV_PCM_STREAM_PLAYBACK].private_data;
	return chip->streams[SNDRV_PCM_STREAM_PLAYBACK].open(substream);
}

static int snd_cmi8330_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_cmi8330 *chip = snd_pcm_substream_chip(substream);

	/* replace the private_data and call the original open callback */
	substream->private_data = chip->streams[SNDRV_PCM_STREAM_CAPTURE].private_data;
	return chip->streams[SNDRV_PCM_STREAM_CAPTURE].open(substream);
}

static int __devinit snd_cmi8330_pcm(struct snd_card *card, struct snd_cmi8330 *chip)
{
	struct snd_pcm *pcm;
	const struct snd_pcm_ops *ops;
	int err;
	static snd_pcm_open_callback_t cmi_open_callbacks[2] = {
		snd_cmi8330_playback_open,
		snd_cmi8330_capture_open
	};

	if ((err = snd_pcm_new(card, (chip->type == CMI8329) ? "CMI8329" : "CMI8330", 0, 1, 1, &pcm)) < 0)
		return err;
	strcpy(pcm->name, (chip->type == CMI8329) ? "CMI8329" : "CMI8330");
	pcm->private_data = chip;
	
	/* SB16 */
	ops = snd_sb16dsp_get_pcm_ops(CMI_SB_STREAM);
	chip->streams[CMI_SB_STREAM].ops = *ops;
	chip->streams[CMI_SB_STREAM].open = ops->open;
	chip->streams[CMI_SB_STREAM].ops.open = cmi_open_callbacks[CMI_SB_STREAM];
	chip->streams[CMI_SB_STREAM].private_data = chip->sb;

	/* AD1848 */
	ops = snd_wss_get_pcm_ops(CMI_AD_STREAM);
	chip->streams[CMI_AD_STREAM].ops = *ops;
	chip->streams[CMI_AD_STREAM].open = ops->open;
	chip->streams[CMI_AD_STREAM].ops.open = cmi_open_callbacks[CMI_AD_STREAM];
	chip->streams[CMI_AD_STREAM].private_data = chip->wss;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &chip->streams[SNDRV_PCM_STREAM_PLAYBACK].ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &chip->streams[SNDRV_PCM_STREAM_CAPTURE].ops);

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_isa_data(),
					      64*1024, 128*1024);
	chip->pcm = pcm;

	return 0;
}


#ifdef CONFIG_PM
static int snd_cmi8330_suspend(struct snd_card *card)
{
	struct snd_cmi8330 *acard = card->private_data;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	snd_pcm_suspend_all(acard->pcm);
	acard->wss->suspend(acard->wss);
	snd_sbmixer_suspend(acard->sb);
	return 0;
}

static int snd_cmi8330_resume(struct snd_card *card)
{
	struct snd_cmi8330 *acard = card->private_data;

	snd_sbdsp_reset(acard->sb);
	snd_sbmixer_suspend(acard->sb);
	acard->wss->resume(acard->wss);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif


/*
 */

#ifdef CONFIG_PNP
#define is_isapnp_selected(dev)		isapnp[dev]
#else
#define is_isapnp_selected(dev)		0
#endif

#define PFX	"cmi8330: "

static int snd_cmi8330_card_new(int dev, struct snd_card **cardp)
{
	struct snd_card *card;
	struct snd_cmi8330 *acard;
	int err;

	err = snd_card_create(index[dev], id[dev], THIS_MODULE,
			      sizeof(struct snd_cmi8330), &card);
	if (err < 0) {
		snd_printk(KERN_ERR PFX "could not get a new card\n");
		return err;
	}
	acard = card->private_data;
	acard->card = card;
	*cardp = card;
	return 0;
}

static int __devinit snd_cmi8330_probe(struct snd_card *card, int dev)
{
	struct snd_cmi8330 *acard;
	int i, err;
	struct snd_opl3 *opl3;

	acard = card->private_data;
	err = snd_wss_create(card, wssport[dev] + 4, -1,
			     wssirq[dev],
			     wssdma[dev], -1,
			     WSS_HW_DETECT, 0, &acard->wss);
	if (err < 0) {
		snd_printk(KERN_ERR PFX "AD1848 device busy??\n");
		return err;
	}
	if (acard->wss->hardware != WSS_HW_CMI8330) {
		snd_printk(KERN_ERR PFX "AD1848 not found during probe\n");
		return -ENODEV;
	}

	if ((err = snd_sbdsp_create(card, sbport[dev],
				    sbirq[dev],
				    snd_sb16dsp_interrupt,
				    sbdma8[dev],
				    sbdma16[dev],
				    SB_HW_AUTO, &acard->sb)) < 0) {
		snd_printk(KERN_ERR PFX "SB16 device busy??\n");
		return err;
	}
	if (acard->sb->hardware != SB_HW_16) {
		snd_printk(KERN_ERR PFX "SB16 not found during probe\n");
		return err;
	}

	snd_wss_out(acard->wss, CS4231_MISC_INFO, 0x40); /* switch on MODE2 */
	for (i = CMI8330_RMUX3D; i <= CMI8330_CDINGAIN; i++)
		snd_wss_out(acard->wss, i,
			    snd_cmi8330_image[i - CMI8330_RMUX3D]);

	if ((err = snd_cmi8330_mixer(card, acard)) < 0) {
		snd_printk(KERN_ERR PFX "failed to create mixers\n");
		return err;
	}

	if ((err = snd_cmi8330_pcm(card, acard)) < 0) {
		snd_printk(KERN_ERR PFX "failed to create pcms\n");
		return err;
	}
	if (fmport[dev] != SNDRV_AUTO_PORT) {
		if (snd_opl3_create(card,
				    fmport[dev], fmport[dev] + 2,
				    OPL3_HW_AUTO, 0, &opl3) < 0) {
			snd_printk(KERN_ERR PFX
				   "no OPL device at 0x%lx-0x%lx ?\n",
				   fmport[dev], fmport[dev] + 2);
		} else {
			err = snd_opl3_hwdep_new(opl3, 0, 1, NULL);
			if (err < 0)
				return err;
		}
	}

	if (mpuport[dev] != SNDRV_AUTO_PORT) {
		if (snd_mpu401_uart_new(card, 0, MPU401_HW_MPU401,
					mpuport[dev], 0, mpuirq[dev],
					IRQF_DISABLED, NULL) < 0)
			printk(KERN_ERR PFX "no MPU-401 device at 0x%lx.\n",
				mpuport[dev]);
	}

	strcpy(card->driver, (acard->type == CMI8329) ? "CMI8329" : "CMI8330/C3D");
	strcpy(card->shortname, (acard->type == CMI8329) ? "C-Media CMI8329" : "C-Media CMI8330/C3D");
	sprintf(card->longname, "%s at 0x%lx, irq %d, dma %d",
		card->shortname,
		acard->wss->port,
		wssirq[dev],
		wssdma[dev]);

	return snd_card_register(card);
}

static int __devinit snd_cmi8330_isa_match(struct device *pdev,
					   unsigned int dev)
{
	if (!enable[dev] || is_isapnp_selected(dev))
		return 0;
	if (wssport[dev] == SNDRV_AUTO_PORT) {
		snd_printk(KERN_ERR PFX "specify wssport\n");
		return 0;
	}
	if (sbport[dev] == SNDRV_AUTO_PORT) {
		snd_printk(KERN_ERR PFX "specify sbport\n");
		return 0;
	}
	return 1;
}

static int __devinit snd_cmi8330_isa_probe(struct device *pdev,
					   unsigned int dev)
{
	struct snd_card *card;
	int err;

	err = snd_cmi8330_card_new(dev, &card);
	if (err < 0)
		return err;
	snd_card_set_dev(card, pdev);
	if ((err = snd_cmi8330_probe(card, dev)) < 0) {
		snd_card_free(card);
		return err;
	}
	dev_set_drvdata(pdev, card);
	return 0;
}

static int __devexit snd_cmi8330_isa_remove(struct device *devptr,
					    unsigned int dev)
{
	snd_card_free(dev_get_drvdata(devptr));
	dev_set_drvdata(devptr, NULL);
	return 0;
}

#ifdef CONFIG_PM
static int snd_cmi8330_isa_suspend(struct device *dev, unsigned int n,
				   pm_message_t state)
{
	return snd_cmi8330_suspend(dev_get_drvdata(dev));
}

static int snd_cmi8330_isa_resume(struct device *dev, unsigned int n)
{
	return snd_cmi8330_resume(dev_get_drvdata(dev));
}
#endif

#define DEV_NAME	"cmi8330"

static struct isa_driver snd_cmi8330_driver = {
	.match		= snd_cmi8330_isa_match,
	.probe		= snd_cmi8330_isa_probe,
	.remove		= __devexit_p(snd_cmi8330_isa_remove),
#ifdef CONFIG_PM
	.suspend	= snd_cmi8330_isa_suspend,
	.resume		= snd_cmi8330_isa_resume,
#endif
	.driver		= {
		.name	= DEV_NAME
	},
};


#ifdef CONFIG_PNP
static int __devinit snd_cmi8330_pnp_detect(struct pnp_card_link *pcard,
					    const struct pnp_card_device_id *pid)
{
	static int dev;
	struct snd_card *card;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (enable[dev] && isapnp[dev])
			break;
	}
	if (dev >= SNDRV_CARDS)
		return -ENODEV;
			       
	res = snd_cmi8330_card_new(dev, &card);
	if (res < 0)
		return res;
	if ((res = snd_cmi8330_pnp(dev, card->private_data, pcard, pid)) < 0) {
		snd_printk(KERN_ERR PFX "PnP detection failed\n");
		snd_card_free(card);
		return res;
	}
	snd_card_set_dev(card, &pcard->card->dev);
	if ((res = snd_cmi8330_probe(card, dev)) < 0) {
		snd_card_free(card);
		return res;
	}
	pnp_set_card_drvdata(pcard, card);
	dev++;
	return 0;
}

static void __devexit snd_cmi8330_pnp_remove(struct pnp_card_link * pcard)
{
	snd_card_free(pnp_get_card_drvdata(pcard));
	pnp_set_card_drvdata(pcard, NULL);
}

#ifdef CONFIG_PM
static int snd_cmi8330_pnp_suspend(struct pnp_card_link *pcard, pm_message_t state)
{
	return snd_cmi8330_suspend(pnp_get_card_drvdata(pcard));
}

static int snd_cmi8330_pnp_resume(struct pnp_card_link *pcard)
{
	return snd_cmi8330_resume(pnp_get_card_drvdata(pcard));
}
#endif

static struct pnp_card_driver cmi8330_pnpc_driver = {
	.flags = PNP_DRIVER_RES_DISABLE,
	.name = "cmi8330",
	.id_table = snd_cmi8330_pnpids,
	.probe = snd_cmi8330_pnp_detect,
	.remove = __devexit_p(snd_cmi8330_pnp_remove),
#ifdef CONFIG_PM
	.suspend	= snd_cmi8330_pnp_suspend,
	.resume		= snd_cmi8330_pnp_resume,
#endif
};
#endif /* CONFIG_PNP */

static int __init alsa_card_cmi8330_init(void)
{
	int err;

	err = isa_register_driver(&snd_cmi8330_driver, SNDRV_CARDS);
#ifdef CONFIG_PNP
	if (!err)
		isa_registered = 1;

	err = pnp_register_card_driver(&cmi8330_pnpc_driver);
	if (!err)
		pnp_registered = 1;

	if (isa_registered)
		err = 0;
#endif
	return err;
}

static void __exit alsa_card_cmi8330_exit(void)
{
#ifdef CONFIG_PNP
	if (pnp_registered)
		pnp_unregister_card_driver(&cmi8330_pnpc_driver);

	if (isa_registered)
#endif
		isa_unregister_driver(&snd_cmi8330_driver);
}

module_init(alsa_card_cmi8330_init)
module_exit(alsa_card_cmi8330_exit)
