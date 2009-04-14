/*
 *  ALSA card-level driver for Turtle Beach Wavefront cards 
 *						(Maui,Tropez,Tropez+)
 *
 *  Copyright (c) 1997-1999 by Paul Barton-Davis <pbd@op.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/isa.h>
#include <linux/pnp.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/opl3.h>
#include <sound/wss.h>
#include <sound/snd_wavefront.h>

MODULE_AUTHOR("Paul Barton-Davis <pbd@op.net>");
MODULE_DESCRIPTION("Turtle Beach Wavefront");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Turtle Beach,Maui/Tropez/Tropez+}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	    /* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	    /* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	    /* Enable this card */
#ifdef CONFIG_PNP
static int isapnp[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};
#endif
static long cs4232_pcm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static int cs4232_pcm_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ; /* 5,7,9,11,12,15 */
static long cs4232_mpu_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT; /* PnP setup */
static int cs4232_mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ; /* 9,11,12,15 */
static long ics2115_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT; /* PnP setup */
static int ics2115_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;    /* 2,9,11,12,15 */
static long fm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	    /* PnP setup */
static int dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	    /* 0,1,3,5,6,7 */
static int dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	    /* 0,1,3,5,6,7 */
static int use_cs4232_midi[SNDRV_CARDS];

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for WaveFront soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for WaveFront soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable WaveFront soundcard.");
#ifdef CONFIG_PNP
module_param_array(isapnp, bool, NULL, 0444);
MODULE_PARM_DESC(isapnp, "ISA PnP detection for WaveFront soundcards.");
#endif
module_param_array(cs4232_pcm_port, long, NULL, 0444);
MODULE_PARM_DESC(cs4232_pcm_port, "Port # for CS4232 PCM interface.");
module_param_array(cs4232_pcm_irq, int, NULL, 0444);
MODULE_PARM_DESC(cs4232_pcm_irq, "IRQ # for CS4232 PCM interface.");
module_param_array(dma1, int, NULL, 0444);
MODULE_PARM_DESC(dma1, "DMA1 # for CS4232 PCM interface.");
module_param_array(dma2, int, NULL, 0444);
MODULE_PARM_DESC(dma2, "DMA2 # for CS4232 PCM interface.");
module_param_array(cs4232_mpu_port, long, NULL, 0444);
MODULE_PARM_DESC(cs4232_mpu_port, "port # for CS4232 MPU-401 interface.");
module_param_array(cs4232_mpu_irq, int, NULL, 0444);
MODULE_PARM_DESC(cs4232_mpu_irq, "IRQ # for CS4232 MPU-401 interface.");
module_param_array(ics2115_irq, int, NULL, 0444);
MODULE_PARM_DESC(ics2115_irq, "IRQ # for ICS2115.");
module_param_array(ics2115_port, long, NULL, 0444);
MODULE_PARM_DESC(ics2115_port, "Port # for ICS2115.");
module_param_array(fm_port, long, NULL, 0444);
MODULE_PARM_DESC(fm_port, "FM port #.");
module_param_array(use_cs4232_midi, bool, NULL, 0444);
MODULE_PARM_DESC(use_cs4232_midi, "Use CS4232 MPU-401 interface (inaccessibly located inside your computer)");

#ifdef CONFIG_PNP
static int isa_registered;
static int pnp_registered;

static struct pnp_card_device_id snd_wavefront_pnpids[] = {
	/* Tropez */
	{ .id = "CSC7532", .devs = { { "CSC0000" }, { "CSC0010" }, { "PnPb006" }, { "CSC0004" } } },
	/* Tropez+ */
	{ .id = "CSC7632", .devs = { { "CSC0000" }, { "CSC0010" }, { "PnPb006" }, { "CSC0004" } } },
	{ .id = "" }
};

MODULE_DEVICE_TABLE(pnp_card, snd_wavefront_pnpids);

static int __devinit
snd_wavefront_pnp (int dev, snd_wavefront_card_t *acard, struct pnp_card_link *card,
		   const struct pnp_card_device_id *id)
{
	struct pnp_dev *pdev;
	int err;

	/* Check for each logical device. */

	/* CS4232 chip (aka "windows sound system") is logical device 0 */

	acard->wss = pnp_request_card_device(card, id->devs[0].id, NULL);
	if (acard->wss == NULL)
		return -EBUSY;

	/* there is a game port at logical device 1, but we ignore it completely */

	/* the control interface is logical device 2, but we ignore it
	   completely. in fact, nobody even seems to know what it
	   does.
	*/

	/* Only configure the CS4232 MIDI interface if its been
	   specifically requested. It is logical device 3.
	*/

	if (use_cs4232_midi[dev]) {
		acard->mpu = pnp_request_card_device(card, id->devs[2].id, NULL);
		if (acard->mpu == NULL)
			return -EBUSY;
	}

	/* The ICS2115 synth is logical device 4 */

	acard->synth = pnp_request_card_device(card, id->devs[3].id, NULL);
	if (acard->synth == NULL)
		return -EBUSY;

	/* PCM/FM initialization */

	pdev = acard->wss;

	/* An interesting note from the Tropez+ FAQ:

	   Q. [Ports] Why is the base address of the WSS I/O ports off by 4?

	   A. WSS I/O requires a block of 8 I/O addresses ("ports"). Of these, the first
	   4 are used to identify and configure the board. With the advent of PnP,
	   these first 4 addresses have become obsolete, and software applications
	   only use the last 4 addresses to control the codec chip. Therefore, the
	   base address setting "skips past" the 4 unused addresses.

	*/

	err = pnp_activate_dev(pdev);
	if (err < 0) {
		snd_printk(KERN_ERR "PnP WSS pnp configure failure\n");
		return err;
	}

	cs4232_pcm_port[dev] = pnp_port_start(pdev, 0);
	fm_port[dev] = pnp_port_start(pdev, 1);
	dma1[dev] = pnp_dma(pdev, 0);
	dma2[dev] = pnp_dma(pdev, 1);
	cs4232_pcm_irq[dev] = pnp_irq(pdev, 0);

	/* Synth initialization */

	pdev = acard->synth;
	
	err = pnp_activate_dev(pdev);
	if (err < 0) {
		snd_printk(KERN_ERR "PnP ICS2115 pnp configure failure\n");
		return err;
	}

	ics2115_port[dev] = pnp_port_start(pdev, 0);
	ics2115_irq[dev] = pnp_irq(pdev, 0);

	/* CS4232 MPU initialization. Configure this only if
	   explicitly requested, since its physically inaccessible and
	   consumes another IRQ.
	*/

	if (use_cs4232_midi[dev]) {

		pdev = acard->mpu;

		err = pnp_activate_dev(pdev);
		if (err < 0) {
			snd_printk(KERN_ERR "PnP MPU401 pnp configure failure\n");
			cs4232_mpu_port[dev] = SNDRV_AUTO_PORT;
		} else {
			cs4232_mpu_port[dev] = pnp_port_start(pdev, 0);
			cs4232_mpu_irq[dev] = pnp_irq(pdev, 0);
		}

		snd_printk (KERN_INFO "CS4232 MPU: port=0x%lx, irq=%i\n", 
			    cs4232_mpu_port[dev], 
			    cs4232_mpu_irq[dev]);
	}

	snd_printdd ("CS4232: pcm port=0x%lx, fm port=0x%lx, dma1=%i, dma2=%i, irq=%i\nICS2115: port=0x%lx, irq=%i\n", 
		    cs4232_pcm_port[dev], 
		    fm_port[dev],
		    dma1[dev], 
		    dma2[dev], 
		    cs4232_pcm_irq[dev],
		    ics2115_port[dev], 
		    ics2115_irq[dev]);
	
	return 0;
}

#endif /* CONFIG_PNP */

static irqreturn_t snd_wavefront_ics2115_interrupt(int irq, void *dev_id)
{
	snd_wavefront_card_t *acard;

	acard = (snd_wavefront_card_t *) dev_id;

	if (acard == NULL) 
		return IRQ_NONE;

	if (acard->wavefront.interrupts_are_midi) {
		snd_wavefront_midi_interrupt (acard);
	} else {
		snd_wavefront_internal_interrupt (acard);
	}
	return IRQ_HANDLED;
}

static struct snd_hwdep * __devinit
snd_wavefront_new_synth (struct snd_card *card,
			 int hw_dev,
			 snd_wavefront_card_t *acard)
{
	struct snd_hwdep *wavefront_synth;

	if (snd_wavefront_detect (acard) < 0) {
		return NULL;
	}

	if (snd_wavefront_start (&acard->wavefront) < 0) {
		return NULL;
	}

	if (snd_hwdep_new(card, "WaveFront", hw_dev, &wavefront_synth) < 0)
		return NULL;
	strcpy (wavefront_synth->name, 
		"WaveFront (ICS2115) wavetable synthesizer");
	wavefront_synth->ops.open = snd_wavefront_synth_open;
	wavefront_synth->ops.release = snd_wavefront_synth_release;
	wavefront_synth->ops.ioctl = snd_wavefront_synth_ioctl;

	return wavefront_synth;
}

static struct snd_hwdep * __devinit
snd_wavefront_new_fx (struct snd_card *card,
		      int hw_dev,
		      snd_wavefront_card_t *acard,
		      unsigned long port)

{
	struct snd_hwdep *fx_processor;

	if (snd_wavefront_fx_start (&acard->wavefront)) {
		snd_printk (KERN_ERR "cannot initialize YSS225 FX processor");
		return NULL;
	}

	if (snd_hwdep_new (card, "YSS225", hw_dev, &fx_processor) < 0)
		return NULL;
	sprintf (fx_processor->name, "YSS225 FX Processor at 0x%lx", port);
	fx_processor->ops.open = snd_wavefront_fx_open;
	fx_processor->ops.release = snd_wavefront_fx_release;
	fx_processor->ops.ioctl = snd_wavefront_fx_ioctl;
	
	return fx_processor;
}

static snd_wavefront_mpu_id internal_id = internal_mpu;
static snd_wavefront_mpu_id external_id = external_mpu;

static struct snd_rawmidi *__devinit
snd_wavefront_new_midi (struct snd_card *card,
			int midi_dev,
			snd_wavefront_card_t *acard,
			unsigned long port,
			snd_wavefront_mpu_id mpu)

{
	struct snd_rawmidi *rmidi;
	static int first = 1;

	if (first) {
		first = 0;
		acard->wavefront.midi.base = port;
		if (snd_wavefront_midi_start (acard)) {
			snd_printk (KERN_ERR "cannot initialize MIDI interface\n");
			return NULL;
		}
	}

	if (snd_rawmidi_new (card, "WaveFront MIDI", midi_dev, 1, 1, &rmidi) < 0)
		return NULL;

	if (mpu == internal_mpu) {
		strcpy(rmidi->name, "WaveFront MIDI (Internal)");
		rmidi->private_data = &internal_id;
	} else {
		strcpy(rmidi->name, "WaveFront MIDI (External)");
		rmidi->private_data = &external_id;
	}

	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_wavefront_midi_output);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_wavefront_midi_input);

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT |
			     SNDRV_RAWMIDI_INFO_INPUT |
			     SNDRV_RAWMIDI_INFO_DUPLEX;

	return rmidi;
}

static void
snd_wavefront_free(struct snd_card *card)
{
	snd_wavefront_card_t *acard = (snd_wavefront_card_t *)card->private_data;
	
	if (acard) {
		release_and_free_resource(acard->wavefront.res_base);
		if (acard->wavefront.irq > 0)
			free_irq(acard->wavefront.irq, (void *)acard);
	}
}

static int snd_wavefront_card_new(int dev, struct snd_card **cardp)
{
	struct snd_card *card;
	snd_wavefront_card_t *acard;
	int err;

	err = snd_card_create(index[dev], id[dev], THIS_MODULE,
			      sizeof(snd_wavefront_card_t), &card);
	if (err < 0)
		return err;

	acard = card->private_data;
	acard->wavefront.irq = -1;
	spin_lock_init(&acard->wavefront.irq_lock);
	init_waitqueue_head(&acard->wavefront.interrupt_sleeper);
	spin_lock_init(&acard->wavefront.midi.open);
	spin_lock_init(&acard->wavefront.midi.virtual);
	acard->wavefront.card = card;
	card->private_free = snd_wavefront_free;

	*cardp = card;
	return 0;
}

static int __devinit
snd_wavefront_probe (struct snd_card *card, int dev)
{
	snd_wavefront_card_t *acard = card->private_data;
	struct snd_wss *chip;
	struct snd_hwdep *wavefront_synth;
	struct snd_rawmidi *ics2115_internal_rmidi = NULL;
	struct snd_rawmidi *ics2115_external_rmidi = NULL;
	struct snd_hwdep *fx_processor;
	int hw_dev = 0, midi_dev = 0, err;

	/* --------- PCM --------------- */

	err = snd_wss_create(card, cs4232_pcm_port[dev], -1,
			     cs4232_pcm_irq[dev], dma1[dev], dma2[dev],
			     WSS_HW_DETECT, 0, &chip);
	if (err < 0) {
		snd_printk(KERN_ERR "can't allocate WSS device\n");
		return err;
	}

	err = snd_wss_pcm(chip, 0, NULL);
	if (err < 0)
		return err;

	err = snd_wss_timer(chip, 0, NULL);
	if (err < 0)
		return err;

	/* ---------- OPL3 synth --------- */

	if (fm_port[dev] > 0 && fm_port[dev] != SNDRV_AUTO_PORT) {
		struct snd_opl3 *opl3;

		err = snd_opl3_create(card, fm_port[dev], fm_port[dev] + 2,
				      OPL3_HW_OPL3_CS, 0, &opl3);
		if (err < 0) {
			snd_printk (KERN_ERR "can't allocate or detect OPL3 synth\n");
			return err;
		}

		err = snd_opl3_hwdep_new(opl3, hw_dev, 1, NULL);
		if (err < 0)
			return err;
		hw_dev++;
	}

	/* ------- ICS2115 Wavetable synth ------- */

	acard->wavefront.res_base = request_region(ics2115_port[dev], 16,
						   "ICS2115");
	if (acard->wavefront.res_base == NULL) {
		snd_printk(KERN_ERR "unable to grab ICS2115 i/o region 0x%lx-0x%lx\n",
			   ics2115_port[dev], ics2115_port[dev] + 16 - 1);
		return -EBUSY;
	}
	if (request_irq(ics2115_irq[dev], snd_wavefront_ics2115_interrupt,
			IRQF_DISABLED, "ICS2115", acard)) {
		snd_printk(KERN_ERR "unable to use ICS2115 IRQ %d\n", ics2115_irq[dev]);
		return -EBUSY;
	}
	
	acard->wavefront.irq = ics2115_irq[dev];
	acard->wavefront.base = ics2115_port[dev];

	wavefront_synth = snd_wavefront_new_synth(card, hw_dev, acard);
	if (wavefront_synth == NULL) {
		snd_printk (KERN_ERR "can't create WaveFront synth device\n");
		return -ENOMEM;
	}

	strcpy (wavefront_synth->name, "ICS2115 Wavetable MIDI Synthesizer");
	wavefront_synth->iface = SNDRV_HWDEP_IFACE_ICS2115;
	hw_dev++;

	/* --------- Mixer ------------ */

	err = snd_wss_mixer(chip);
	if (err < 0) {
		snd_printk (KERN_ERR "can't allocate mixer device\n");
		return err;
	}

	/* -------- CS4232 MPU-401 interface -------- */

	if (cs4232_mpu_port[dev] > 0 && cs4232_mpu_port[dev] != SNDRV_AUTO_PORT) {
		err = snd_mpu401_uart_new(card, midi_dev, MPU401_HW_CS4232,
					  cs4232_mpu_port[dev], 0,
					  cs4232_mpu_irq[dev], IRQF_DISABLED,
					  NULL);
		if (err < 0) {
			snd_printk (KERN_ERR "can't allocate CS4232 MPU-401 device\n");
			return err;
		}
		midi_dev++;
	}

	/* ------ ICS2115 internal MIDI ------------ */

	if (ics2115_port[dev] > 0 && ics2115_port[dev] != SNDRV_AUTO_PORT) {
		ics2115_internal_rmidi = 
			snd_wavefront_new_midi (card, 
						midi_dev,
						acard,
						ics2115_port[dev],
						internal_mpu);
		if (ics2115_internal_rmidi == NULL) {
			snd_printk (KERN_ERR "can't setup ICS2115 internal MIDI device\n");
			return -ENOMEM;
		}
		midi_dev++;
	}

	/* ------ ICS2115 external MIDI ------------ */

	if (ics2115_port[dev] > 0 && ics2115_port[dev] != SNDRV_AUTO_PORT) {
		ics2115_external_rmidi = 
			snd_wavefront_new_midi (card, 
						midi_dev,
						acard,
						ics2115_port[dev],
						external_mpu);
		if (ics2115_external_rmidi == NULL) {
			snd_printk (KERN_ERR "can't setup ICS2115 external MIDI device\n");
			return -ENOMEM;
		}
		midi_dev++;
	}

	/* FX processor for Tropez+ */

	if (acard->wavefront.has_fx) {
		fx_processor = snd_wavefront_new_fx (card,
						     hw_dev,
						     acard,
						     ics2115_port[dev]);
		if (fx_processor == NULL) {
			snd_printk (KERN_ERR "can't setup FX device\n");
			return -ENOMEM;
		}

		hw_dev++;

		strcpy(card->driver, "Tropez+");
		strcpy(card->shortname, "Turtle Beach Tropez+");
	} else {
		/* Need a way to distinguish between Maui and Tropez */
		strcpy(card->driver, "WaveFront");
		strcpy(card->shortname, "Turtle Beach WaveFront");
	}

	/* ----- Register the card --------- */

	/* Not safe to include "Turtle Beach" in longname, due to 
	   length restrictions
	*/

	sprintf(card->longname, "%s PCM 0x%lx irq %d dma %d",
		card->driver,
		chip->port,
		cs4232_pcm_irq[dev],
		dma1[dev]);

	if (dma2[dev] >= 0 && dma2[dev] < 8)
		sprintf(card->longname + strlen(card->longname), "&%d", dma2[dev]);

	if (cs4232_mpu_port[dev] > 0 && cs4232_mpu_port[dev] != SNDRV_AUTO_PORT) {
		sprintf (card->longname + strlen (card->longname), 
			 " MPU-401 0x%lx irq %d",
			 cs4232_mpu_port[dev],
			 cs4232_mpu_irq[dev]);
	}

	sprintf (card->longname + strlen (card->longname), 
		 " SYNTH 0x%lx irq %d",
		 ics2115_port[dev],
		 ics2115_irq[dev]);

	return snd_card_register(card);
}	

static int __devinit snd_wavefront_isa_match(struct device *pdev,
					     unsigned int dev)
{
	if (!enable[dev])
		return 0;
#ifdef CONFIG_PNP
	if (isapnp[dev])
		return 0;
#endif
	if (cs4232_pcm_port[dev] == SNDRV_AUTO_PORT) {
		snd_printk(KERN_ERR "specify CS4232 port\n");
		return 0;
	}
	if (ics2115_port[dev] == SNDRV_AUTO_PORT) {
		snd_printk(KERN_ERR "specify ICS2115 port\n");
		return 0;
	}
	return 1;
}

static int __devinit snd_wavefront_isa_probe(struct device *pdev,
					     unsigned int dev)
{
	struct snd_card *card;
	int err;

	err = snd_wavefront_card_new(dev, &card);
	if (err < 0)
		return err;
	snd_card_set_dev(card, pdev);
	if ((err = snd_wavefront_probe(card, dev)) < 0) {
		snd_card_free(card);
		return err;
	}
	
	dev_set_drvdata(pdev, card);
	return 0;
}

static int __devexit snd_wavefront_isa_remove(struct device *devptr,
					      unsigned int dev)
{
	snd_card_free(dev_get_drvdata(devptr));
	dev_set_drvdata(devptr, NULL);
	return 0;
}

#define DEV_NAME "wavefront"

static struct isa_driver snd_wavefront_driver = {
	.match		= snd_wavefront_isa_match,
	.probe		= snd_wavefront_isa_probe,
	.remove		= __devexit_p(snd_wavefront_isa_remove),
	/* FIXME: suspend, resume */
	.driver		= {
		.name	= DEV_NAME
	},
};


#ifdef CONFIG_PNP
static int __devinit snd_wavefront_pnp_detect(struct pnp_card_link *pcard,
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

	res = snd_wavefront_card_new(dev, &card);
	if (res < 0)
		return res;

	if (snd_wavefront_pnp (dev, card->private_data, pcard, pid) < 0) {
		if (cs4232_pcm_port[dev] == SNDRV_AUTO_PORT) {
			snd_printk (KERN_ERR "isapnp detection failed\n");
			snd_card_free (card);
			return -ENODEV;
		}
	}
	snd_card_set_dev(card, &pcard->card->dev);

	if ((res = snd_wavefront_probe(card, dev)) < 0)
		return res;

	pnp_set_card_drvdata(pcard, card);
	dev++;
	return 0;
}

static void __devexit snd_wavefront_pnp_remove(struct pnp_card_link * pcard)
{
	snd_card_free(pnp_get_card_drvdata(pcard));
	pnp_set_card_drvdata(pcard, NULL);
}

static struct pnp_card_driver wavefront_pnpc_driver = {
	.flags		= PNP_DRIVER_RES_DISABLE,
	.name		= "wavefront",
	.id_table	= snd_wavefront_pnpids,
	.probe		= snd_wavefront_pnp_detect,
	.remove		= __devexit_p(snd_wavefront_pnp_remove),
	/* FIXME: suspend,resume */
};

#endif /* CONFIG_PNP */

static int __init alsa_card_wavefront_init(void)
{
	int err;

	err = isa_register_driver(&snd_wavefront_driver, SNDRV_CARDS);
#ifdef CONFIG_PNP
	if (!err)
		isa_registered = 1;

	err = pnp_register_card_driver(&wavefront_pnpc_driver);
	if (!err)
		pnp_registered = 1;

	if (isa_registered)
		err = 0;
#endif
	return err;
}

static void __exit alsa_card_wavefront_exit(void)
{
#ifdef CONFIG_PNP
	if (pnp_registered)
		pnp_unregister_card_driver(&wavefront_pnpc_driver);
	if (isa_registered)
#endif
		isa_unregister_driver(&snd_wavefront_driver);
}

module_init(alsa_card_wavefront_init)
module_exit(alsa_card_wavefront_exit)
