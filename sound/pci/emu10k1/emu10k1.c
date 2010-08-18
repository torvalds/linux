/*
 *  The driver for the EMU10K1 (SB Live!) based soundcards
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *
 *  Copyright (c) by James Courtier-Dutton <James@superbug.demon.co.uk>
 *      Added support for Audigy 2 Value.
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
 * 
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/time.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/emu10k1.h>
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("EMU10K1");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Creative Labs,SB Live!/PCI512/E-mu APS},"
	       "{Creative Labs,SB Audigy}}");

#if defined(CONFIG_SND_SEQUENCER) || (defined(MODULE) && defined(CONFIG_SND_SEQUENCER_MODULE))
#define ENABLE_SYNTH
#include <sound/emu10k1_synth.h>
#endif

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static int extin[SNDRV_CARDS];
static int extout[SNDRV_CARDS];
static int seq_ports[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 4};
static int max_synth_voices[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 64};
static int max_buffer_size[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 128};
static int enable_ir[SNDRV_CARDS];
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
static struct pci_device_id snd_emu10k1_ids[] = {
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

static int __devinit snd_card_emu10k1_probe(struct pci_dev *pci,
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

	err = snd_card_create(index[dev], id[dev], THIS_MODULE, 0, &card);
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
	if ((err = snd_emu10k1_pcm(emu, 0, NULL)) < 0)
		goto error;
	if ((err = snd_emu10k1_pcm_mic(emu, 1, NULL)) < 0)
		goto error;
	if ((err = snd_emu10k1_pcm_efx(emu, 2, NULL)) < 0)
		goto error;
	/* This stores the periods table. */
	if (emu->card_capabilities->ca0151_chip) { /* P16V */	
		if ((err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(pci),
					       1024, &emu->p16v_buffer)) < 0)
			goto error;
	}

	if ((err = snd_emu10k1_mixer(emu, 0, 3)) < 0)
		goto error;
	
	if ((err = snd_emu10k1_timer(emu, 0)) < 0)
		goto error;

	if ((err = snd_emu10k1_pcm_multi(emu, 3, NULL)) < 0)
		goto error;
	if (emu->card_capabilities->ca0151_chip) { /* P16V */
		if ((err = snd_p16v_pcm(emu, 4, NULL)) < 0)
			goto error;
	}
	if (emu->audigy) {
		if ((err = snd_emu10k1_audigy_midi(emu)) < 0)
			goto error;
	} else {
		if ((err = snd_emu10k1_midi(emu)) < 0)
			goto error;
	}
	if ((err = snd_emu10k1_fx8010_new(emu, 0, NULL)) < 0)
		goto error;
#ifdef ENABLE_SYNTH
	if (snd_seq_device_new(card, 1, SNDRV_SEQ_DEV_ID_EMU10K1_SYNTH,
			       sizeof(struct snd_emu10k1_synth_arg), &wave) < 0 ||
	    wave == NULL) {
		snd_printk(KERN_WARNING "can't initialize Emu10k1 wavetable synth\n");
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
 
	strcpy(card->driver, emu->card_capabilities->driver);
	strcpy(card->shortname, emu->card_capabilities->name);
	snprintf(card->longname, sizeof(card->longname),
		 "%s (rev.%d, serial:0x%x) at 0x%lx, irq %i",
		 card->shortname, emu->revision, emu->serial, emu->port, emu->irq);

	if ((err = snd_card_register(card)) < 0)
		goto error;

	pci_set_drvdata(pci, card);
	dev++;
	return 0;

 error:
	snd_card_free(card);
	return err;
}

static void __devexit snd_card_emu10k1_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}


#ifdef CONFIG_PM
static int snd_emu10k1_suspend(struct pci_dev *pci, pm_message_t state)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct snd_emu10k1 *emu = card->private_data;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);

	snd_pcm_suspend_all(emu->pcm);
	snd_pcm_suspend_all(emu->pcm_mic);
	snd_pcm_suspend_all(emu->pcm_efx);
	snd_pcm_suspend_all(emu->pcm_multi);
	snd_pcm_suspend_all(emu->pcm_p16v);

	snd_ac97_suspend(emu->ac97);

	snd_emu10k1_efx_suspend(emu);
	snd_emu10k1_suspend_regs(emu);
	if (emu->card_capabilities->ca0151_chip)
		snd_p16v_suspend(emu);

	snd_emu10k1_done(emu);

	pci_disable_device(pci);
	pci_save_state(pci);
	pci_set_power_state(pci, pci_choose_state(pci, state));
	return 0;
}

static int snd_emu10k1_resume(struct pci_dev *pci)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct snd_emu10k1 *emu = card->private_data;

	pci_set_power_state(pci, PCI_D0);
	pci_restore_state(pci);
	if (pci_enable_device(pci) < 0) {
		printk(KERN_ERR "emu10k1: pci_enable_device failed, "
		       "disabling device\n");
		snd_card_disconnect(card);
		return -EIO;
	}
	pci_set_master(pci);

	snd_emu10k1_resume_init(emu);
	snd_emu10k1_efx_resume(emu);
	snd_ac97_resume(emu->ac97);
	snd_emu10k1_resume_regs(emu);

	if (emu->card_capabilities->ca0151_chip)
		snd_p16v_resume(emu);

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif

static struct pci_driver driver = {
	.name = "EMU10K1_Audigy",
	.id_table = snd_emu10k1_ids,
	.probe = snd_card_emu10k1_probe,
	.remove = __devexit_p(snd_card_emu10k1_remove),
#ifdef CONFIG_PM
	.suspend = snd_emu10k1_suspend,
	.resume = snd_emu10k1_resume,
#endif
};

static int __init alsa_card_emu10k1_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit alsa_card_emu10k1_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_emu10k1_init)
module_exit(alsa_card_emu10k1_exit)
