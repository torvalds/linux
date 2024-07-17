// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  The driver for the EMU10K1 (SB Live!) based soundcards
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *                   James Courtier-Dutton <James@superbug.co.uk>
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/time.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/emu10k1.h>
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("EMU10K1");
MODULE_LICENSE("GPL");

#if IS_ENABLED(CONFIG_SND_SEQUENCER)
#define ENABLE_SYNTH
#include <sound/emu10k1_synth.h>
#endif

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static int extin[SNDRV_CARDS];
static int extout[SNDRV_CARDS];
static int seq_ports[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 4};
static int max_synth_voices[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 64};
static int max_buffer_size[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 128};
static bool enable_ir[SNDRV_CARDS];
static uint subsystem[SNDRV_CARDS]; /* Force card subsystem model */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for the EMU10K1 soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for the EMU10K1 soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable the EMU10K1 soundcard.");
module_param_array(extin, int, NULL, 0444);
MODULE_PARM_DESC(extin, "Available external inputs for FX8010. Zero=default.");
module_param_array(extout, int, NULL, 0444);
MODULE_PARM_DESC(extout, "Available external outputs for FX8010. Zero=default.");
module_param_array(seq_ports, int, NULL, 0444);
MODULE_PARM_DESC(seq_ports, "Allocated sequencer ports for internal synthesizer.");
module_param_array(max_synth_voices, int, NULL, 0444);
MODULE_PARM_DESC(max_synth_voices, "Maximum number of voices for WaveTable.");
module_param_array(max_buffer_size, int, NULL, 0444);
MODULE_PARM_DESC(max_buffer_size, "Maximum sample buffer size in MB.");
module_param_array(enable_ir, bool, NULL, 0444);
MODULE_PARM_DESC(enable_ir, "Enable IR.");
module_param_array(subsystem, uint, NULL, 0444);
MODULE_PARM_DESC(subsystem, "Force card subsystem model.");
/*
 * Class 0401: 1102:0008 (rev 00) Subsystem: 1102:1001 -> Audigy2 Value  Model:SB0400
 */
static const struct pci_device_id snd_emu10k1_ids[] = {
	{ PCI_VDEVICE(CREATIVE, 0x0002), 0 },	/* EMU10K1 */
	{ PCI_VDEVICE(CREATIVE, 0x0004), 1 },	/* Audigy */
	{ PCI_VDEVICE(CREATIVE, 0x0008), 1 },	/* Audigy 2 Value SB0400 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_emu10k1_ids);

static int snd_card_emu10k1_probe(struct pci_dev *pci,
				  const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct snd_emu10k1 *emu;
#ifdef ENABLE_SYNTH
	struct snd_seq_device *wave = NULL;
#endif
	int err;

	if (dev >= SNDRV_CARDS)
        	return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	err = snd_devm_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
				sizeof(*emu), &card);
	if (err < 0)
		return err;
	emu = card->private_data;

	if (max_buffer_size[dev] < 32)
		max_buffer_size[dev] = 32;
	else if (max_buffer_size[dev] > 1024)
		max_buffer_size[dev] = 1024;
	err = snd_emu10k1_create(card, pci, extin[dev], extout[dev],
				 (long)max_buffer_size[dev] * 1024 * 1024,
				 enable_ir[dev], subsystem[dev]);
	if (err < 0)
		return err;
	err = snd_emu10k1_pcm(emu, 0);
	if (err < 0)
		return err;
	if (emu->card_capabilities->ac97_chip) {
		err = snd_emu10k1_pcm_mic(emu, 1);
		if (err < 0)
			return err;
	}
	err = snd_emu10k1_pcm_efx(emu, 2);
	if (err < 0)
		return err;
	/* This stores the periods table. */
	if (emu->card_capabilities->ca0151_chip) { /* P16V */	
		emu->p16v_buffer =
			snd_devm_alloc_pages(&pci->dev, SNDRV_DMA_TYPE_DEV, 1024);
		if (!emu->p16v_buffer)
			return -ENOMEM;
	}

	err = snd_emu10k1_mixer(emu, 0, 3);
	if (err < 0)
		return err;
	
	err = snd_emu10k1_timer(emu, 0);
	if (err < 0)
		return err;

	err = snd_emu10k1_pcm_multi(emu, 3);
	if (err < 0)
		return err;
	if (emu->card_capabilities->ca0151_chip) { /* P16V */
		err = snd_p16v_pcm(emu, 4);
		if (err < 0)
			return err;
	}
	if (emu->audigy) {
		err = snd_emu10k1_audigy_midi(emu);
		if (err < 0)
			return err;
	} else {
		err = snd_emu10k1_midi(emu);
		if (err < 0)
			return err;
	}
	err = snd_emu10k1_fx8010_new(emu, 0);
	if (err < 0)
		return err;
#ifdef ENABLE_SYNTH
	if (snd_seq_device_new(card, 1, SNDRV_SEQ_DEV_ID_EMU10K1_SYNTH,
			       sizeof(struct snd_emu10k1_synth_arg), &wave) < 0 ||
	    wave == NULL) {
		dev_warn(emu->card->dev,
			 "can't initialize Emu10k1 wavetable synth\n");
	} else {
		struct snd_emu10k1_synth_arg *arg;
		arg = SNDRV_SEQ_DEVICE_ARGPTR(wave);
		strcpy(wave->name, "Emu-10k1 Synth");
		arg->hwptr = emu;
		arg->index = 1;
		arg->seq_ports = seq_ports[dev];
		arg->max_voices = max_synth_voices[dev];
	}
#endif
 
	strscpy(card->driver, emu->card_capabilities->driver,
		sizeof(card->driver));
	strscpy(card->shortname, emu->card_capabilities->name,
		sizeof(card->shortname));
	snprintf(card->longname, sizeof(card->longname),
		 "%s (rev.%d, serial:0x%x) at 0x%lx, irq %i",
		 card->shortname, emu->revision, emu->serial, emu->port, emu->irq);

	err = snd_card_register(card);
	if (err < 0)
		return err;

	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int snd_emu10k1_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_emu10k1 *emu = card->private_data;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);

	emu->suspend = 1;

	cancel_work_sync(&emu->emu1010.work);

	snd_ac97_suspend(emu->ac97);

	snd_emu10k1_efx_suspend(emu);
	snd_emu10k1_suspend_regs(emu);
	if (emu->card_capabilities->ca0151_chip)
		snd_p16v_suspend(emu);

	snd_emu10k1_done(emu);
	return 0;
}

static int snd_emu10k1_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_emu10k1 *emu = card->private_data;

	snd_emu10k1_resume_init(emu);
	snd_emu10k1_efx_resume(emu);
	snd_ac97_resume(emu->ac97);
	snd_emu10k1_resume_regs(emu);

	if (emu->card_capabilities->ca0151_chip)
		snd_p16v_resume(emu);

	emu->suspend = 0;

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);

	return 0;
}

static SIMPLE_DEV_PM_OPS(snd_emu10k1_pm, snd_emu10k1_suspend, snd_emu10k1_resume);
#define SND_EMU10K1_PM_OPS	&snd_emu10k1_pm
#else
#define SND_EMU10K1_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct pci_driver emu10k1_driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_emu10k1_ids,
	.probe = snd_card_emu10k1_probe,
	.driver = {
		.pm = SND_EMU10K1_PM_OPS,
	},
};

module_pci_driver(emu10k1_driver);
