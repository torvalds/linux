/*****************************************************************************
 *
 * Copyright (C) 2008 Cedric Bregardis <cedric.bregardis@free.fr> and
 * Jean-Christian Hassler <jhassler@free.fr>
 *
 * This file is part of the Audiowerk2 ALSA driver
 *
 * The Audiowerk2 ALSA driver is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 * The Audiowerk2 ALSA driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the Audiowerk2 ALSA driver; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 *
 *****************************************************************************/
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/control.h>

#include "saa7146.h"
#include "aw2-saa7146.h"

MODULE_AUTHOR("Cedric Bregardis <cedric.bregardis@free.fr>, "
	      "Jean-Christian Hassler <jhassler@free.fr>");
MODULE_DESCRIPTION("Emagic Audiowerk 2 sound driver");
MODULE_LICENSE("GPL");

/*********************************
 * DEFINES
 ********************************/
#define PCI_VENDOR_ID_SAA7146		  0x1131
#define PCI_DEVICE_ID_SAA7146		  0x7146

#define CTL_ROUTE_ANALOG 0
#define CTL_ROUTE_DIGITAL 1

/*********************************
 * TYPEDEFS
 ********************************/
  /* hardware definition */
static struct snd_pcm_hardware snd_aw2_playback_hw = {
	.info = (SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP_VALID),
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_44100,
	.rate_min = 44100,
	.rate_max = 44100,
	.channels_min = 2,
	.channels_max = 4,
	.buffer_bytes_max = 32768,
	.period_bytes_min = 4096,
	.period_bytes_max = 32768,
	.periods_min = 1,
	.periods_max = 1024,
};

static struct snd_pcm_hardware snd_aw2_capture_hw = {
	.info = (SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_MMAP_VALID),
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_44100,
	.rate_min = 44100,
	.rate_max = 44100,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = 32768,
	.period_bytes_min = 4096,
	.period_bytes_max = 32768,
	.periods_min = 1,
	.periods_max = 1024,
};

struct aw2_pcm_device {
	struct snd_pcm *pcm;
	unsigned int stream_number;
	struct aw2 *chip;
};

struct aw2 {
	struct snd_aw2_saa7146 saa7146;

	struct pci_dev *pci;
	int irq;
	spinlock_t reg_lock;
	struct mutex mtx;

	unsigned long iobase_phys;
	void __iomem *iobase_virt;

	struct snd_card *card;

	struct aw2_pcm_device device_playback[NB_STREAM_PLAYBACK];
	struct aw2_pcm_device device_capture[NB_STREAM_CAPTURE];
};

/*********************************
 * FUNCTION DECLARATIONS
 ********************************/
static int __init alsa_card_aw2_init(void);
static void __exit alsa_card_aw2_exit(void);
static int snd_aw2_dev_free(struct snd_device *device);
static int __devinit snd_aw2_create(struct snd_card *card,
				    struct pci_dev *pci, struct aw2 **rchip);
static int __devinit snd_aw2_probe(struct pci_dev *pci,
				   const struct pci_device_id *pci_id);
static void __devexit snd_aw2_remove(struct pci_dev *pci);
static int snd_aw2_pcm_playback_open(struct snd_pcm_substream *substream);
static int snd_aw2_pcm_playback_close(struct snd_pcm_substream *substream);
static int snd_aw2_pcm_capture_open(struct snd_pcm_substream *substream);
static int snd_aw2_pcm_capture_close(struct snd_pcm_substream *substream);
static int snd_aw2_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params);
static int snd_aw2_pcm_hw_free(struct snd_pcm_substream *substream);
static int snd_aw2_pcm_prepare_playback(struct snd_pcm_substream *substream);
static int snd_aw2_pcm_prepare_capture(struct snd_pcm_substream *substream);
static int snd_aw2_pcm_trigger_playback(struct snd_pcm_substream *substream,
					int cmd);
static int snd_aw2_pcm_trigger_capture(struct snd_pcm_substream *substream,
				       int cmd);
static snd_pcm_uframes_t snd_aw2_pcm_pointer_playback(struct snd_pcm_substream
						      *substream);
static snd_pcm_uframes_t snd_aw2_pcm_pointer_capture(struct snd_pcm_substream
						     *substream);
static int __devinit snd_aw2_new_pcm(struct aw2 *chip);

static int snd_aw2_control_switch_capture_info(struct snd_kcontrol *kcontrol,
					       struct snd_ctl_elem_info *uinfo);
static int snd_aw2_control_switch_capture_get(struct snd_kcontrol *kcontrol,
					      struct snd_ctl_elem_value
					      *ucontrol);
static int snd_aw2_control_switch_capture_put(struct snd_kcontrol *kcontrol,
					      struct snd_ctl_elem_value
					      *ucontrol);

/*********************************
 * VARIABLES
 ********************************/
static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Audiowerk2 soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for the Audiowerk2 soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Audiowerk2 soundcard.");

static struct pci_device_id snd_aw2_ids[] = {
	{PCI_VENDOR_ID_SAA7146, PCI_DEVICE_ID_SAA7146, 0, 0,
	 0, 0, 0},
	{0}
};

MODULE_DEVICE_TABLE(pci, snd_aw2_ids);

/* pci_driver definition */
static struct pci_driver driver = {
	.name = "Emagic Audiowerk 2",
	.id_table = snd_aw2_ids,
	.probe = snd_aw2_probe,
	.remove = __devexit_p(snd_aw2_remove),
};

/* operators for playback PCM alsa interface */
static struct snd_pcm_ops snd_aw2_playback_ops = {
	.open = snd_aw2_pcm_playback_open,
	.close = snd_aw2_pcm_playback_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_aw2_pcm_hw_params,
	.hw_free = snd_aw2_pcm_hw_free,
	.prepare = snd_aw2_pcm_prepare_playback,
	.trigger = snd_aw2_pcm_trigger_playback,
	.pointer = snd_aw2_pcm_pointer_playback,
};

/* operators for capture PCM alsa interface */
static struct snd_pcm_ops snd_aw2_capture_ops = {
	.open = snd_aw2_pcm_capture_open,
	.close = snd_aw2_pcm_capture_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_aw2_pcm_hw_params,
	.hw_free = snd_aw2_pcm_hw_free,
	.prepare = snd_aw2_pcm_prepare_capture,
	.trigger = snd_aw2_pcm_trigger_capture,
	.pointer = snd_aw2_pcm_pointer_capture,
};

static struct snd_kcontrol_new aw2_control __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "PCM Capture Route",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.private_value = 0xffff,
	.info = snd_aw2_control_switch_capture_info,
	.get = snd_aw2_control_switch_capture_get,
	.put = snd_aw2_control_switch_capture_put
};

/*********************************
 * FUNCTION IMPLEMENTATIONS
 ********************************/

/* initialization of the module */
static int __init alsa_card_aw2_init(void)
{
	snd_printdd(KERN_DEBUG "aw2: Load aw2 module\n");
	return pci_register_driver(&driver);
}

/* clean up the module */
static void __exit alsa_card_aw2_exit(void)
{
	snd_printdd(KERN_DEBUG "aw2: Unload aw2 module\n");
	pci_unregister_driver(&driver);
}

module_init(alsa_card_aw2_init);
module_exit(alsa_card_aw2_exit);

/* component-destructor */
static int snd_aw2_dev_free(struct snd_device *device)
{
	struct aw2 *chip = device->device_data;

	/* Free hardware */
	snd_aw2_saa7146_free(&chip->saa7146);

	/* release the irq */
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *)chip);
	/* release the i/o ports & memory */
	if (chip->iobase_virt)
		iounmap(chip->iobase_virt);

	pci_release_regions(chip->pci);
	/* disable the PCI entry */
	pci_disable_device(chip->pci);
	/* release the data */
	kfree(chip);

	return 0;
}

/* chip-specific constructor */
static int __devinit snd_aw2_create(struct snd_card *card,
				    struct pci_dev *pci, struct aw2 **rchip)
{
	struct aw2 *chip;
	int err;
	static struct snd_device_ops ops = {
		.dev_free = snd_aw2_dev_free,
	};

	*rchip = NULL;

	/* initialize the PCI entry */
	err = pci_enable_device(pci);
	if (err < 0)
		return err;
	pci_set_master(pci);

	/* check PCI availability (32bit DMA) */
	if ((pci_set_dma_mask(pci, DMA_BIT_MASK(32)) < 0) ||
	    (pci_set_consistent_dma_mask(pci, DMA_BIT_MASK(32)) < 0)) {
		printk(KERN_ERR "aw2: Impossible to set 32bit mask DMA\n");
		pci_disable_device(pci);
		return -ENXIO;
	}
	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}

	/* initialize the stuff */
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;

	/* (1) PCI resource allocation */
	err = pci_request_regions(pci, "Audiowerk2");
	if (err < 0) {
		pci_disable_device(pci);
		kfree(chip);
		return err;
	}
	chip->iobase_phys = pci_resource_start(pci, 0);
	chip->iobase_virt =
		ioremap_nocache(chip->iobase_phys,
				pci_resource_len(pci, 0));

	if (chip->iobase_virt == NULL) {
		printk(KERN_ERR "aw2: unable to remap memory region");
		pci_release_regions(pci);
		pci_disable_device(pci);
		kfree(chip);
		return -ENOMEM;
	}

	/* (2) initialization of the chip hardware */
	snd_aw2_saa7146_setup(&chip->saa7146, chip->iobase_virt);

	if (request_irq(pci->irq, snd_aw2_saa7146_interrupt,
			IRQF_SHARED, "Audiowerk2", chip)) {
		printk(KERN_ERR "aw2: Cannot grab irq %d\n", pci->irq);

		iounmap(chip->iobase_virt);
		pci_release_regions(chip->pci);
		pci_disable_device(chip->pci);
		kfree(chip);
		return -EBUSY;
	}
	chip->irq = pci->irq;

	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
	if (err < 0) {
		free_irq(chip->irq, (void *)chip);
		iounmap(chip->iobase_virt);
		pci_release_regions(chip->pci);
		pci_disable_device(chip->pci);
		kfree(chip);
		return err;
	}

	snd_card_set_dev(card, &pci->dev);
	*rchip = chip;

	printk(KERN_INFO
	       "Audiowerk 2 sound card (saa7146 chipset) detected and "
	       "managed\n");
	return 0;
}

/* constructor */
static int __devinit snd_aw2_probe(struct pci_dev *pci,
				   const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct aw2 *chip;
	int err;

	/* (1) Continue if device is not enabled, else inc dev */
	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	/* (2) Create card instance */
	err = snd_card_create(index[dev], id[dev], THIS_MODULE, 0, &card);
	if (err < 0)
		return err;

	/* (3) Create main component */
	err = snd_aw2_create(card, pci, &chip);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	/* initialize mutex */
	mutex_init(&chip->mtx);
	/* init spinlock */
	spin_lock_init(&chip->reg_lock);
	/* (4) Define driver ID and name string */
	strcpy(card->driver, "aw2");
	strcpy(card->shortname, "Audiowerk2");

	sprintf(card->longname, "%s with SAA7146 irq %i",
		card->shortname, chip->irq);

	/* (5) Create other components */
	snd_aw2_new_pcm(chip);

	/* (6) Register card instance */
	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	/* (7) Set PCI driver data */
	pci_set_drvdata(pci, card);

	dev++;
	return 0;
}

/* destructor */
static void __devexit snd_aw2_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

/* open callback */
static int snd_aw2_pcm_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_printdd(KERN_DEBUG "aw2: Playback_open \n");
	runtime->hw = snd_aw2_playback_hw;
	return 0;
}

/* close callback */
static int snd_aw2_pcm_playback_close(struct snd_pcm_substream *substream)
{
	return 0;

}

static int snd_aw2_pcm_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_printdd(KERN_DEBUG "aw2: Capture_open \n");
	runtime->hw = snd_aw2_capture_hw;
	return 0;
}

/* close callback */
static int snd_aw2_pcm_capture_close(struct snd_pcm_substream *substream)
{
	/* TODO: something to do ? */
	return 0;
}

 /* hw_params callback */
static int snd_aw2_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

/* hw_free callback */
static int snd_aw2_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

/* prepare callback for playback */
static int snd_aw2_pcm_prepare_playback(struct snd_pcm_substream *substream)
{
	struct aw2_pcm_device *pcm_device = snd_pcm_substream_chip(substream);
	struct aw2 *chip = pcm_device->chip;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long period_size, buffer_size;

	mutex_lock(&chip->mtx);

	period_size = snd_pcm_lib_period_bytes(substream);
	buffer_size = snd_pcm_lib_buffer_bytes(substream);

	snd_aw2_saa7146_pcm_init_playback(&chip->saa7146,
					  pcm_device->stream_number,
					  runtime->dma_addr, period_size,
					  buffer_size);

	/* Define Interrupt callback */
	snd_aw2_saa7146_define_it_playback_callback(pcm_device->stream_number,
						    (snd_aw2_saa7146_it_cb)
						    snd_pcm_period_elapsed,
						    (void *)substream);

	mutex_unlock(&chip->mtx);

	return 0;
}

/* prepare callback for capture */
static int snd_aw2_pcm_prepare_capture(struct snd_pcm_substream *substream)
{
	struct aw2_pcm_device *pcm_device = snd_pcm_substream_chip(substream);
	struct aw2 *chip = pcm_device->chip;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long period_size, buffer_size;

	mutex_lock(&chip->mtx);

	period_size = snd_pcm_lib_period_bytes(substream);
	buffer_size = snd_pcm_lib_buffer_bytes(substream);

	snd_aw2_saa7146_pcm_init_capture(&chip->saa7146,
					 pcm_device->stream_number,
					 runtime->dma_addr, period_size,
					 buffer_size);

	/* Define Interrupt callback */
	snd_aw2_saa7146_define_it_capture_callback(pcm_device->stream_number,
						   (snd_aw2_saa7146_it_cb)
						   snd_pcm_period_elapsed,
						   (void *)substream);

	mutex_unlock(&chip->mtx);

	return 0;
}

/* playback trigger callback */
static int snd_aw2_pcm_trigger_playback(struct snd_pcm_substream *substream,
					int cmd)
{
	int status = 0;
	struct aw2_pcm_device *pcm_device = snd_pcm_substream_chip(substream);
	struct aw2 *chip = pcm_device->chip;
	spin_lock(&chip->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_aw2_saa7146_pcm_trigger_start_playback(&chip->saa7146,
							   pcm_device->
							   stream_number);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		snd_aw2_saa7146_pcm_trigger_stop_playback(&chip->saa7146,
							  pcm_device->
							  stream_number);
		break;
	default:
		status = -EINVAL;
	}
	spin_unlock(&chip->reg_lock);
	return status;
}

/* capture trigger callback */
static int snd_aw2_pcm_trigger_capture(struct snd_pcm_substream *substream,
				       int cmd)
{
	int status = 0;
	struct aw2_pcm_device *pcm_device = snd_pcm_substream_chip(substream);
	struct aw2 *chip = pcm_device->chip;
	spin_lock(&chip->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_aw2_saa7146_pcm_trigger_start_capture(&chip->saa7146,
							  pcm_device->
							  stream_number);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		snd_aw2_saa7146_pcm_trigger_stop_capture(&chip->saa7146,
							 pcm_device->
							 stream_number);
		break;
	default:
		status = -EINVAL;
	}
	spin_unlock(&chip->reg_lock);
	return status;
}

/* playback pointer callback */
static snd_pcm_uframes_t snd_aw2_pcm_pointer_playback(struct snd_pcm_substream
						      *substream)
{
	struct aw2_pcm_device *pcm_device = snd_pcm_substream_chip(substream);
	struct aw2 *chip = pcm_device->chip;
	unsigned int current_ptr;

	/* get the current hardware pointer */
	struct snd_pcm_runtime *runtime = substream->runtime;
	current_ptr =
		snd_aw2_saa7146_get_hw_ptr_playback(&chip->saa7146,
						    pcm_device->stream_number,
						    runtime->dma_area,
						    runtime->buffer_size);

	return bytes_to_frames(substream->runtime, current_ptr);
}

/* capture pointer callback */
static snd_pcm_uframes_t snd_aw2_pcm_pointer_capture(struct snd_pcm_substream
						     *substream)
{
	struct aw2_pcm_device *pcm_device = snd_pcm_substream_chip(substream);
	struct aw2 *chip = pcm_device->chip;
	unsigned int current_ptr;

	/* get the current hardware pointer */
	struct snd_pcm_runtime *runtime = substream->runtime;
	current_ptr =
		snd_aw2_saa7146_get_hw_ptr_capture(&chip->saa7146,
						   pcm_device->stream_number,
						   runtime->dma_area,
						   runtime->buffer_size);

	return bytes_to_frames(substream->runtime, current_ptr);
}

/* create a pcm device */
static int __devinit snd_aw2_new_pcm(struct aw2 *chip)
{
	struct snd_pcm *pcm_playback_ana;
	struct snd_pcm *pcm_playback_num;
	struct snd_pcm *pcm_capture;
	struct aw2_pcm_device *pcm_device;
	int err = 0;

	/* Create new Alsa PCM device */

	err = snd_pcm_new(chip->card, "Audiowerk2 analog playback", 0, 1, 0,
			  &pcm_playback_ana);
	if (err < 0) {
		printk(KERN_ERR "aw2: snd_pcm_new error (0x%X)\n", err);
		return err;
	}

	/* Creation ok */
	pcm_device = &chip->device_playback[NUM_STREAM_PLAYBACK_ANA];

	/* Set PCM device name */
	strcpy(pcm_playback_ana->name, "Analog playback");
	/* Associate private data to PCM device */
	pcm_playback_ana->private_data = pcm_device;
	/* set operators of PCM device */
	snd_pcm_set_ops(pcm_playback_ana, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_aw2_playback_ops);
	/* store PCM device */
	pcm_device->pcm = pcm_playback_ana;
	/* give base chip pointer to our internal pcm device
	   structure */
	pcm_device->chip = chip;
	/* Give stream number to PCM device */
	pcm_device->stream_number = NUM_STREAM_PLAYBACK_ANA;

	/* pre-allocation of buffers */
	/* Preallocate continuous pages. */
	err = snd_pcm_lib_preallocate_pages_for_all(pcm_playback_ana,
						    SNDRV_DMA_TYPE_DEV,
						    snd_dma_pci_data
						    (chip->pci),
						    64 * 1024, 64 * 1024);
	if (err)
		printk(KERN_ERR "aw2: snd_pcm_lib_preallocate_pages_for_all "
		       "error (0x%X)\n", err);

	err = snd_pcm_new(chip->card, "Audiowerk2 digital playback", 1, 1, 0,
			  &pcm_playback_num);

	if (err < 0) {
		printk(KERN_ERR "aw2: snd_pcm_new error (0x%X)\n", err);
		return err;
	}
	/* Creation ok */
	pcm_device = &chip->device_playback[NUM_STREAM_PLAYBACK_DIG];

	/* Set PCM device name */
	strcpy(pcm_playback_num->name, "Digital playback");
	/* Associate private data to PCM device */
	pcm_playback_num->private_data = pcm_device;
	/* set operators of PCM device */
	snd_pcm_set_ops(pcm_playback_num, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_aw2_playback_ops);
	/* store PCM device */
	pcm_device->pcm = pcm_playback_num;
	/* give base chip pointer to our internal pcm device
	   structure */
	pcm_device->chip = chip;
	/* Give stream number to PCM device */
	pcm_device->stream_number = NUM_STREAM_PLAYBACK_DIG;

	/* pre-allocation of buffers */
	/* Preallocate continuous pages. */
	err = snd_pcm_lib_preallocate_pages_for_all(pcm_playback_num,
						    SNDRV_DMA_TYPE_DEV,
						    snd_dma_pci_data
						    (chip->pci),
						    64 * 1024, 64 * 1024);
	if (err)
		printk(KERN_ERR
		       "aw2: snd_pcm_lib_preallocate_pages_for_all error "
		       "(0x%X)\n", err);



	err = snd_pcm_new(chip->card, "Audiowerk2 capture", 2, 0, 1,
			  &pcm_capture);

	if (err < 0) {
		printk(KERN_ERR "aw2: snd_pcm_new error (0x%X)\n", err);
		return err;
	}

	/* Creation ok */
	pcm_device = &chip->device_capture[NUM_STREAM_CAPTURE_ANA];

	/* Set PCM device name */
	strcpy(pcm_capture->name, "Capture");
	/* Associate private data to PCM device */
	pcm_capture->private_data = pcm_device;
	/* set operators of PCM device */
	snd_pcm_set_ops(pcm_capture, SNDRV_PCM_STREAM_CAPTURE,
			&snd_aw2_capture_ops);
	/* store PCM device */
	pcm_device->pcm = pcm_capture;
	/* give base chip pointer to our internal pcm device
	   structure */
	pcm_device->chip = chip;
	/* Give stream number to PCM device */
	pcm_device->stream_number = NUM_STREAM_CAPTURE_ANA;

	/* pre-allocation of buffers */
	/* Preallocate continuous pages. */
	err = snd_pcm_lib_preallocate_pages_for_all(pcm_capture,
						    SNDRV_DMA_TYPE_DEV,
						    snd_dma_pci_data
						    (chip->pci),
						    64 * 1024, 64 * 1024);
	if (err)
		printk(KERN_ERR
		       "aw2: snd_pcm_lib_preallocate_pages_for_all error "
		       "(0x%X)\n", err);


	/* Create control */
	err = snd_ctl_add(chip->card, snd_ctl_new1(&aw2_control, chip));
	if (err < 0) {
		printk(KERN_ERR "aw2: snd_ctl_add error (0x%X)\n", err);
		return err;
	}

	return 0;
}

static int snd_aw2_control_switch_capture_info(struct snd_kcontrol *kcontrol,
					       struct snd_ctl_elem_info *uinfo)
{
	static char *texts[2] = {
		"Analog", "Digital"
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items) {
		uinfo->value.enumerated.item =
		    uinfo->value.enumerated.items - 1;
	}
	strcpy(uinfo->value.enumerated.name,
	       texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_aw2_control_switch_capture_get(struct snd_kcontrol *kcontrol,
					      struct snd_ctl_elem_value
					      *ucontrol)
{
	struct aw2 *chip = snd_kcontrol_chip(kcontrol);
	if (snd_aw2_saa7146_is_using_digital_input(&chip->saa7146))
		ucontrol->value.enumerated.item[0] = CTL_ROUTE_DIGITAL;
	else
		ucontrol->value.enumerated.item[0] = CTL_ROUTE_ANALOG;
	return 0;
}

static int snd_aw2_control_switch_capture_put(struct snd_kcontrol *kcontrol,
					      struct snd_ctl_elem_value
					      *ucontrol)
{
	struct aw2 *chip = snd_kcontrol_chip(kcontrol);
	int changed = 0;
	int is_disgital =
	    snd_aw2_saa7146_is_using_digital_input(&chip->saa7146);

	if (((ucontrol->value.integer.value[0] == CTL_ROUTE_DIGITAL)
	     && !is_disgital)
	    || ((ucontrol->value.integer.value[0] == CTL_ROUTE_ANALOG)
		&& is_disgital)) {
		snd_aw2_saa7146_use_digital_input(&chip->saa7146, !is_disgital);
		changed = 1;
	}
	return changed;
}
