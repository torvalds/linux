// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  The driver for the EMU10K1 (SB Live!) based soundcards
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *
 *  Copyright (c) by James Courtier-Dutton <James@superbug.demon.co.uk>
 *      Added support for Audigy 2 Value.
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
MODULE_SUPPORTED_DEVICE("{{Creative Labs,SB Live!/PCI512/E-mu APS},"
	       "{Creative Labs,SB Audigy}}");

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
static uint delay_pcm_irq[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 2};

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
module_param_array(delay_pcm_irq, uint, NULL, 0444);
MODULE_PARM_DESC(delay_pcm_irq, "Delay PCM interrupt by specified number of samples (default 0).");
/*
 * Class 0401: 1102:0008 (rev 00) Subsystem: 1102:1001 -> Audigy2 Value  Model:SB0400
 */
static const struct pci_device_id snd_emu10k1_ids[] = {
	{ PCI_VDEVICE(CREATIVE, 0x0002), 0 },	/* EMU10K1 */
	{ PCI_VDEVICE(CREATIVE, 0x0004), 1 },	/* Audigy */
	{ PCI_VDEVICE(CREATIVE, 0x0008), 1 },	/* Audigy 2 Value SB0400 */
	{ 0, }
};

/*
 * Audigy 2 Value notes:
 * A_IOCFG Input (GPIO)
 * 0x400  = Front analog jack plugged in. (Green socket)
 * 0x1000 = Read analog jack plugged in. (Black socket)
 * 0x2000 = Center/LFE analog jack plugged in. (Orange socket)
 * A_IOCFG Output (GPIO)
 * 0x60 = Sound out of front Left.
 * Win sets it to 0xXX61
 */

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

	err = snd_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
			   0, &card);
	if (err < 0)
		return err;
	if (max_buffer_size[dev] < 32)
		max_buffer_size[dev] = 32;
	else if (max_buffer_size[dev] > 1024)
		max_buffer_size[dev] = 1024;
	if ((err = snd_emu10k1_create(card, pci, extin[dev], extout[dev],
				      (long)max_buffer_size[dev] * 1024 * 1024,
				      enable_ir[dev], subsystem[dev],
				      &emu)) < 0)
		goto error;
	card->private_data = emu;
	emu->delay_pcm_irq = delay_pcm_irq[dev] & 0x1f;
	if ((err = snd_emu10k1_pcm(emu, 0)) < 0)
		goto error;
	if ((err = snd_emu10k1_pcm_mic(emu, 1)) < 0)
		goto error;
	if ((err = snd_emu10k1_pcm_efx(emu, 2)) < 0)
		goto error;
	/* This stores the periods table. */
	if (emu->card_capabilities->ca0151_chip) { /* P16V */	
		err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, &pci->dev,
					  1024, &emu->p16v_buffer);
		if (err < 0)
			goto error;
	}

	if ((err = snd_emu10k1_mixer(emu, 0, 3)) < 0)
		goto error;
	
	if ((err = snd_emu10k1_timer(emu, 0)) < 0)
		goto error;

	if ((err = snd_emu10k1_pcm_multi(emu, 3)) < 0)
		goto error;
	if (emu->card_capabilities->ca0151_chip) { /* P16V */
		if ((err = snd_p16v_pcm(emu, 4)) < 0)
			goto error;
	}
	if (emu->audigy) {
		if ((err = snd_emu10k1_audigy_midi(emu)) < 0)
			goto error;
	} else {
		if ((err = snd_emu10k1_midi(emu)) < 0)
			goto error;
	}
	if ((err = snd_emu10k1_fx8010_new(emu, 0)) < 0)
		goto error;
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

	if ((err = snd_card_register(card)) < 0)
		goto error;

	if (emu->card_capabilities->emu_model)
		schedule_delayed_work(&emu->emu1010.firmware_work, 0);

	pci_set_drvdata(pci, card);
	dev++;
	return 0;

 error:
	snd_card_free(card);
	return err;
}

static void snd_card_emu10k1_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
}


#ifdef CONFIG_PM_SLEEP
static int snd_emu10k1_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_emu10k1 *emu = card->private_data;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);

	emu->suspend = 1;

	cancel_delayed_work_sync(&emu->emu1010.firmware_work);

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

	if (emu->card_capabilities->emu_model)
		schedule_delayed_work(&emu->emu1010.firmware_work, 0);

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
	.remove = snd_card_emu10k1_remove,
	.driver = {
		.pm = SND_EMU10K1_PM_OPS,
	},
};

module_pci_driver(emu10k1_driver);
