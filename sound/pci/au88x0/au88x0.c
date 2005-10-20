/*
 * ALSA driver for the Aureal Vortex family of soundprocessors.
 * Author: Manuel Jander (mjander@embedded.cl)
 *
 *   This driver is the result of the OpenVortex Project from Savannah
 * (savannah.nongnu.org/projects/openvortex). I would like to thank
 * the developers of OpenVortex, Jeff Muizelaar and Kester Maddock, from
 * whom i got plenty of help, and their codebase was invaluable.
 *   Thanks to the ALSA developers, they helped a lot working out
 * the ALSA part.
 *   Thanks also to Sourceforge for maintaining the old binary drivers,
 * and the forum, where developers could comunicate.
 *
 * Now at least i can play Legacy DOOM with MIDI music :-)
 */

#include "au88x0.h"
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <sound/initval.h>

// module parameters (see "Module Parameters")
static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;
static int pcifix[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 255 };

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for " CARD_NAME " soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for " CARD_NAME " soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable " CARD_NAME " soundcard.");
module_param_array(pcifix, int, NULL, 0444);
MODULE_PARM_DESC(pcifix, "Enable VIA-workaround for " CARD_NAME " soundcard.");

MODULE_DESCRIPTION("Aureal vortex");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Aureal Semiconductor Inc., Aureal Vortex Sound Processor}}");

MODULE_DEVICE_TABLE(pci, snd_vortex_ids);

static void vortex_fix_latency(struct pci_dev *vortex)
{
	int rc;
	if (!(rc = pci_write_config_byte(vortex, 0x40, 0xff))) {
			printk(KERN_INFO CARD_NAME
			       ": vortex latency is 0xff\n");
	} else {
		printk(KERN_WARNING CARD_NAME
				": could not set vortex latency: pci error 0x%x\n", rc);
	}
}

static void vortex_fix_agp_bridge(struct pci_dev *via)
{
	int rc;
	u8 value;

	/*
	 * only set the bit (Extend PCI#2 Internal Master for
	 * Efficient Handling of Dummy Requests) if the can
	 * read the config and it is not already set
	 */

	if (!(rc = pci_read_config_byte(via, 0x42, &value))
			&& ((value & 0x10)
				|| !(rc = pci_write_config_byte(via, 0x42, value | 0x10)))) {
		printk(KERN_INFO CARD_NAME
				": bridge config is 0x%x\n", value | 0x10);
	} else {
		printk(KERN_WARNING CARD_NAME
				": could not set vortex latency: pci error 0x%x\n", rc);
	}
}

static void __devinit snd_vortex_workaround(struct pci_dev *vortex, int fix)
{
	struct pci_dev *via = NULL;

	/* autodetect if workarounds are required */
	if (fix == 255) {
		/* VIA KT133 */
		via = pci_get_device(PCI_VENDOR_ID_VIA,
			PCI_DEVICE_ID_VIA_8365_1, NULL);
		/* VIA Apollo */
		if (via == NULL) {
			via = pci_get_device(PCI_VENDOR_ID_VIA,
				PCI_DEVICE_ID_VIA_82C598_1, NULL);
			/* AMD Irongate */
			if (via == NULL)
				via = pci_get_device(PCI_VENDOR_ID_AMD,
					PCI_DEVICE_ID_AMD_FE_GATE_7007, NULL);
		}
		if (via) {
			printk(KERN_INFO CARD_NAME ": Activating latency workaround...\n");
			vortex_fix_latency(vortex);
			vortex_fix_agp_bridge(via);
		}
	} else {
		if (fix & 0x1)
			vortex_fix_latency(vortex);
		if ((fix & 0x2) && (via = pci_get_device(PCI_VENDOR_ID_VIA,
				PCI_DEVICE_ID_VIA_8365_1, NULL)))
			vortex_fix_agp_bridge(via);
		if ((fix & 0x4) && (via = pci_get_device(PCI_VENDOR_ID_VIA,
				PCI_DEVICE_ID_VIA_82C598_1, NULL)))
			vortex_fix_agp_bridge(via);
		if ((fix & 0x8) && (via = pci_get_device(PCI_VENDOR_ID_AMD,
				PCI_DEVICE_ID_AMD_FE_GATE_7007, NULL)))
			vortex_fix_agp_bridge(via);
	}
	pci_dev_put(via);
}

// component-destructor
// (see "Management of Cards and Components")
static int snd_vortex_dev_free(snd_device_t * device)
{
	vortex_t *vortex = device->device_data;

	vortex_gameport_unregister(vortex);
	vortex_core_shutdown(vortex);
	// Take down PCI interface.
	synchronize_irq(vortex->irq);
	free_irq(vortex->irq, vortex);
	pci_release_regions(vortex->pci_dev);
	pci_disable_device(vortex->pci_dev);
	kfree(vortex);

	return 0;
}

// chip-specific constructor
// (see "Management of Cards and Components")
static int __devinit
snd_vortex_create(snd_card_t * card, struct pci_dev *pci, vortex_t ** rchip)
{
	vortex_t *chip;
	int err;
	static snd_device_ops_t ops = {
		.dev_free = snd_vortex_dev_free,
	};

	*rchip = NULL;

	// check PCI availability (DMA).
	if ((err = pci_enable_device(pci)) < 0)
		return err;
	if (!pci_dma_supported(pci, VORTEX_DMA_MASK)) {
		printk(KERN_ERR "error to set DMA mask\n");
		return -ENXIO;
	}
	pci_set_dma_mask(pci, VORTEX_DMA_MASK);

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	chip->card = card;

	// initialize the stuff
	chip->pci_dev = pci;
	chip->io = pci_resource_start(pci, 0);
	chip->vendor = pci->vendor;
	chip->device = pci->device;
	chip->card = card;
	chip->irq = -1;

	// (1) PCI resource allocation
	// Get MMIO area
	//
	if ((err = pci_request_regions(pci, CARD_NAME_SHORT)) != 0)
		goto regions_out;

	chip->mmio = ioremap_nocache(pci_resource_start(pci, 0),
	                             pci_resource_len(pci, 0));
	if (!chip->mmio) {
		printk(KERN_ERR "MMIO area remap failed.\n");
		err = -ENOMEM;
		goto ioremap_out;
	}

	/* Init audio core.
	 * This must be done before we do request_irq otherwise we can get spurious
	 * interupts that we do not handle properly and make a mess of things */
	if ((err = vortex_core_init(chip)) != 0) {
		printk(KERN_ERR "hw core init failed\n");
		goto core_out;
	}

	if ((err = request_irq(pci->irq, vortex_interrupt,
	                       SA_INTERRUPT | SA_SHIRQ, CARD_NAME_SHORT,
	                       chip)) != 0) {
		printk(KERN_ERR "cannot grab irq\n");
		goto irq_out;
	}
	chip->irq = pci->irq;

	pci_set_master(pci);
	// End of PCI setup.

	// Register alsa root device.
	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		goto alloc_out;
	}

	*rchip = chip;

	return 0;

      alloc_out:
	synchronize_irq(chip->irq);
	free_irq(chip->irq, chip);
      irq_out:
	vortex_core_shutdown(chip);
      core_out:
	iounmap(chip->mmio);
      ioremap_out:
	pci_release_regions(chip->pci_dev);
      regions_out:
	pci_disable_device(chip->pci_dev);
	//FIXME: this not the right place to unregister the gameport
	vortex_gameport_unregister(chip);
	return err;
}

// constructor -- see "Constructor" sub-section
static int __devinit
snd_vortex_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
	static int dev;
	snd_card_t *card;
	vortex_t *chip;
	int err;

	// (1)
	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}
	// (2)
	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	// (3)
	if ((err = snd_vortex_create(card, pci, &chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_vortex_workaround(pci, pcifix[dev]);
	// (4) Alloc components.
	// ADB pcm.
	if ((err = snd_vortex_new_pcm(chip, VORTEX_PCM_ADB, NR_ADB)) < 0) {
		snd_card_free(card);
		return err;
	}
#ifndef CHIP_AU8820
	// ADB SPDIF
	if ((err = snd_vortex_new_pcm(chip, VORTEX_PCM_SPDIF, 1)) < 0) {
		snd_card_free(card);
		return err;
	}
	// A3D
	if ((err = snd_vortex_new_pcm(chip, VORTEX_PCM_A3D, NR_A3D)) < 0) {
		snd_card_free(card);
		return err;
	}
#endif
	/*
	   // ADB I2S
	   if ((err = snd_vortex_new_pcm(chip, VORTEX_PCM_I2S, 1)) < 0) {
	   snd_card_free(card);
	   return err;
	   }
	 */
#ifndef CHIP_AU8810
	// WT pcm.
	if ((err = snd_vortex_new_pcm(chip, VORTEX_PCM_WT, NR_WT)) < 0) {
		snd_card_free(card);
		return err;
	}
#endif
	// snd_ac97_mixer and Vortex mixer.
	if ((err = snd_vortex_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_vortex_midi(chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	vortex_gameport_register(chip);

#if 0
	if (snd_seq_device_new(card, 1, SNDRV_SEQ_DEV_ID_VORTEX_SYNTH,
			       sizeof(snd_vortex_synth_arg_t), &wave) < 0
	    || wave == NULL) {
		snd_printk(KERN_ERR "Can't initialize Aureal wavetable synth\n");
	} else {
		snd_vortex_synth_arg_t *arg;

		arg = SNDRV_SEQ_DEVICE_ARGPTR(wave);
		strcpy(wave->name, "Aureal Synth");
		arg->hwptr = vortex;
		arg->index = 1;
		arg->seq_ports = seq_ports[dev];
		arg->max_voices = max_synth_voices[dev];
	}
#endif

	// (5)
	strcpy(card->driver, CARD_NAME_SHORT);
	strcpy(card->shortname, CARD_NAME_SHORT);
	sprintf(card->longname, "%s at 0x%lx irq %i",
		card->shortname, chip->io, chip->irq);

	if ((err = pci_read_config_word(pci, PCI_DEVICE_ID,
				  &(chip->device))) < 0) {
		snd_card_free(card);
		return err;
	}	
	if ((err = pci_read_config_word(pci, PCI_VENDOR_ID,
				  &(chip->vendor))) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = pci_read_config_byte(pci, PCI_REVISION_ID,
				  &(chip->rev))) < 0) {
		snd_card_free(card);
		return err;
	}
#ifdef CHIP_AU8830
	if ((chip->rev) != 0xfe && (chip->rev) != 0xfa) {
		printk(KERN_ALERT
		       "vortex: The revision (%x) of your card has not been seen before.\n",
		       chip->rev);
		printk(KERN_ALERT
		       "vortex: Please email the results of 'lspci -vv' to openvortex-dev@nongnu.org.\n");
		snd_card_free(card);
		err = -ENODEV;
		return err;
	}
#endif

	// (6)
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	// (7)
	pci_set_drvdata(pci, card);
	dev++;
	vortex_connect_default(chip, 1);
	vortex_enable_int(chip);
	return 0;
}

// destructor -- see "Destructor" sub-section
static void __devexit snd_vortex_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

// pci_driver definition
static struct pci_driver driver = {
	.name = CARD_NAME_SHORT,
	.owner = THIS_MODULE,
	.id_table = snd_vortex_ids,
	.probe = snd_vortex_probe,
	.remove = __devexit_p(snd_vortex_remove),
};

// initialization of the module
static int __init alsa_card_vortex_init(void)
{
	return pci_register_driver(&driver);
}

// clean up the module
static void __exit alsa_card_vortex_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_vortex_init)
module_exit(alsa_card_vortex_exit)
