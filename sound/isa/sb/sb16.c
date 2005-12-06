/*
 *  Driver for SoundBlaster 16/AWE32/AWE64 soundcards
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
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

#include <sound/driver.h>
#include <asm/dma.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pnp.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/sb.h>
#include <sound/sb16_csp.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#include <sound/emu8000.h>
#include <sound/seq_device.h>
#define SNDRV_LEGACY_AUTO_PROBE
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#include <sound/initval.h>

#ifdef SNDRV_SBAWE
#define PFX "sbawe: "
#else
#define PFX "sb16: "
#endif

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_LICENSE("GPL");
#ifndef SNDRV_SBAWE
MODULE_DESCRIPTION("Sound Blaster 16");
MODULE_SUPPORTED_DEVICE("{{Creative Labs,SB 16},"
		"{Creative Labs,SB Vibra16S},"
		"{Creative Labs,SB Vibra16C},"
		"{Creative Labs,SB Vibra16CL},"
		"{Creative Labs,SB Vibra16X}}");
#else
MODULE_DESCRIPTION("Sound Blaster AWE");
MODULE_SUPPORTED_DEVICE("{{Creative Labs,SB AWE 32},"
		"{Creative Labs,SB AWE 64},"
		"{Creative Labs,SB AWE 64 Gold}}");
#endif

#if 0
#define SNDRV_DEBUG_IRQ
#endif

#if defined(SNDRV_SBAWE) && (defined(CONFIG_SND_SEQUENCER) || (defined(MODULE) && defined(CONFIG_SND_SEQUENCER_MODULE)))
#define SNDRV_SBAWE_EMU8000
#endif

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP; /* Enable this card */
#ifdef CONFIG_PNP
static int isapnp[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};
#endif
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x240,0x260,0x280 */
static long mpu_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x330,0x300 */
static long fm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
#ifdef SNDRV_SBAWE_EMU8000
static long awe_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
#endif
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,10 */
static int dma8[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3 */
static int dma16[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 5,6,7 */
static int mic_agc[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};
#ifdef CONFIG_SND_SB16_CSP
static int csp[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0};
#endif
#ifdef SNDRV_SBAWE_EMU8000
static int seq_ports[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 4};
#endif

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for SoundBlaster 16 soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for SoundBlaster 16 soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable SoundBlaster 16 soundcard.");
#ifdef CONFIG_PNP
module_param_array(isapnp, bool, NULL, 0444);
MODULE_PARM_DESC(isapnp, "PnP detection for specified soundcard.");
#endif
module_param_array(port, long, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for SB16 driver.");
module_param_array(mpu_port, long, NULL, 0444);
MODULE_PARM_DESC(mpu_port, "MPU-401 port # for SB16 driver.");
module_param_array(fm_port, long, NULL, 0444);
MODULE_PARM_DESC(fm_port, "FM port # for SB16 PnP driver.");
#ifdef SNDRV_SBAWE_EMU8000
module_param_array(awe_port, long, NULL, 0444);
MODULE_PARM_DESC(awe_port, "AWE port # for SB16 PnP driver.");
#endif
module_param_array(irq, int, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for SB16 driver.");
module_param_array(dma8, int, NULL, 0444);
MODULE_PARM_DESC(dma8, "8-bit DMA # for SB16 driver.");
module_param_array(dma16, int, NULL, 0444);
MODULE_PARM_DESC(dma16, "16-bit DMA # for SB16 driver.");
module_param_array(mic_agc, int, NULL, 0444);
MODULE_PARM_DESC(mic_agc, "Mic Auto-Gain-Control switch.");
#ifdef CONFIG_SND_SB16_CSP
module_param_array(csp, int, NULL, 0444);
MODULE_PARM_DESC(csp, "ASP/CSP chip support.");
#endif
#ifdef SNDRV_SBAWE_EMU8000
module_param_array(seq_ports, int, NULL, 0444);
MODULE_PARM_DESC(seq_ports, "Number of sequencer ports for WaveTable synth.");
#endif

struct snd_card_sb16 {
	struct resource *fm_res;	/* used to block FM i/o region for legacy cards */
#ifdef CONFIG_PNP
	int dev_no;
	struct pnp_dev *dev;
#ifdef SNDRV_SBAWE_EMU8000
	struct pnp_dev *devwt;
#endif
#endif
};

static snd_card_t *snd_sb16_legacy[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

#ifdef CONFIG_PNP

static struct pnp_card_device_id snd_sb16_pnpids[] = {
#ifndef SNDRV_SBAWE
	/* Sound Blaster 16 PnP */
	{ .id = "CTL0024", .devs = { { "CTL0031" } } },
	/* Sound Blaster 16 PnP */
	{ .id = "CTL0025", .devs = { { "CTL0031" } } },
	/* Sound Blaster 16 PnP */
	{ .id = "CTL0026", .devs = { { "CTL0031" } } },
	/* Sound Blaster 16 PnP */
	{ .id = "CTL0027", .devs = { { "CTL0031" } } },
	/* Sound Blaster 16 PnP */
	{ .id = "CTL0028", .devs = { { "CTL0031" } } },
	/* Sound Blaster 16 PnP */
	{ .id = "CTL0029", .devs = { { "CTL0031" } } },
	/* Sound Blaster 16 PnP */
	{ .id = "CTL002a", .devs = { { "CTL0031" } } },
	/* Sound Blaster 16 PnP */
	/* Note: This card has also a CTL0051:StereoEnhance device!!! */
	{ .id = "CTL002b", .devs = { { "CTL0031" } } },
	/* Sound Blaster 16 PnP */
	{ .id = "CTL002c", .devs = { { "CTL0031" } } },
	/* Sound Blaster Vibra16S */
	{ .id = "CTL0051", .devs = { { "CTL0001" } } },
	/* Sound Blaster Vibra16C */
	{ .id = "CTL0070", .devs = { { "CTL0001" } } },
	/* Sound Blaster Vibra16CL - added by ctm@ardi.com */
	{ .id = "CTL0080", .devs = { { "CTL0041" } } },
	/* Sound Blaster 16 'value' PnP. It says model ct4130 on the pcb, */
	/* but ct4131 on a sticker on the board.. */
	{ .id = "CTL0086", .devs = { { "CTL0041" } } },
	/* Sound Blaster Vibra16X */
	{ .id = "CTL00f0", .devs = { { "CTL0043" } } },
#else  /* SNDRV_SBAWE defined */
	/* Sound Blaster AWE 32 PnP */
	{ .id = "CTL0035", .devs = { { "CTL0031" }, { "CTL0021" } } },
	/* Sound Blaster AWE 32 PnP */
	{ .id = "CTL0039", .devs = { { "CTL0031" }, { "CTL0021" } } },
	/* Sound Blaster AWE 32 PnP */
	{ .id = "CTL0042", .devs = { { "CTL0031" }, { "CTL0021" } } },
	/* Sound Blaster AWE 32 PnP */
	{ .id = "CTL0043", .devs = { { "CTL0031" }, { "CTL0021" } } },
	/* Sound Blaster AWE 32 PnP */
	/* Note: This card has also a CTL0051:StereoEnhance device!!! */
	{ .id = "CTL0044", .devs = { { "CTL0031" }, { "CTL0021" } } },
	/* Sound Blaster AWE 32 PnP */
	/* Note: This card has also a CTL0051:StereoEnhance device!!! */
	{ .id = "CTL0045", .devs = { { "CTL0031" }, { "CTL0021" } } },
	/* Sound Blaster AWE 32 PnP */
	{ .id = "CTL0046", .devs = { { "CTL0031" }, { "CTL0021" } } },
	/* Sound Blaster AWE 32 PnP */
	{ .id = "CTL0047", .devs = { { "CTL0031" }, { "CTL0021" } } },
	/* Sound Blaster AWE 32 PnP */
	{ .id = "CTL0048", .devs = { { "CTL0031" }, { "CTL0021" } } },
	/* Sound Blaster AWE 32 PnP */
	{ .id = "CTL0054", .devs = { { "CTL0031" }, { "CTL0021" } } },
	/* Sound Blaster AWE 32 PnP */
	{ .id = "CTL009a", .devs = { { "CTL0041" }, { "CTL0021" } } },
	/* Sound Blaster AWE 32 PnP */
	{ .id = "CTL009c", .devs = { { "CTL0041" }, { "CTL0021" } } },
	/* Sound Blaster 32 PnP */
	{ .id = "CTL009f", .devs = { { "CTL0041" }, { "CTL0021" } } },
	/* Sound Blaster AWE 64 PnP */
	{ .id = "CTL009d", .devs = { { "CTL0042" }, { "CTL0022" } } },
	/* Sound Blaster AWE 64 PnP Gold */
	{ .id = "CTL009e", .devs = { { "CTL0044" }, { "CTL0023" } } },
	/* Sound Blaster AWE 64 PnP Gold */
	{ .id = "CTL00b2", .devs = { { "CTL0044" }, { "CTL0023" } } },
	/* Sound Blaster AWE 64 PnP */
	{ .id = "CTL00c1", .devs = { { "CTL0042" }, { "CTL0022" } } },
	/* Sound Blaster AWE 64 PnP */
	{ .id = "CTL00c3", .devs = { { "CTL0045" }, { "CTL0022" } } },
	/* Sound Blaster AWE 64 PnP */
	{ .id = "CTL00c5", .devs = { { "CTL0045" }, { "CTL0022" } } },
	/* Sound Blaster AWE 64 PnP */
	{ .id = "CTL00c7", .devs = { { "CTL0045" }, { "CTL0022" } } },
	/* Sound Blaster AWE 64 PnP */
	{ .id = "CTL00e4", .devs = { { "CTL0045" }, { "CTL0022" } } },
	/* Sound Blaster AWE 64 PnP */
	{ .id = "CTL00e9", .devs = { { "CTL0045" }, { "CTL0022" } } },
	/* Sound Blaster 16 PnP (AWE) */
	{ .id = "CTL00ed", .devs = { { "CTL0041" }, { "CTL0070" } } },
	/* Generic entries */
	{ .id = "CTLXXXX" , .devs = { { "CTL0031" }, { "CTL0021" } } },
	{ .id = "CTLXXXX" , .devs = { { "CTL0041" }, { "CTL0021" } } },
	{ .id = "CTLXXXX" , .devs = { { "CTL0042" }, { "CTL0022" } } },
	{ .id = "CTLXXXX" , .devs = { { "CTL0044" }, { "CTL0023" } } },
	{ .id = "CTLXXXX" , .devs = { { "CTL0045" }, { "CTL0022" } } },
#endif /* SNDRV_SBAWE */
	/* Sound Blaster 16 PnP (Virtual PC 2004)*/
	{ .id = "tBA03b0", .devs = { { "PNPb003" } } },
	{ .id = "", }
};

MODULE_DEVICE_TABLE(pnp_card, snd_sb16_pnpids);

#endif /* CONFIG_PNP */

#ifdef SNDRV_SBAWE_EMU8000
#define DRIVER_NAME	"snd-card-sbawe"
#else
#define DRIVER_NAME	"snd-card-sb16"
#endif

#ifdef CONFIG_PNP

static int __devinit snd_card_sb16_pnp(int dev, struct snd_card_sb16 *acard,
				       struct pnp_card_link *card,
				       const struct pnp_card_device_id *id)
{
	struct pnp_dev *pdev;
	struct pnp_resource_table * cfg = kmalloc(sizeof(struct pnp_resource_table), GFP_KERNEL);
	int err;

	if (!cfg) 
		return -ENOMEM; 
	acard->dev = pnp_request_card_device(card, id->devs[0].id, NULL);
	if (acard->dev == NULL) { 
		kfree(cfg); 
		return -ENODEV; 
	} 
#ifdef SNDRV_SBAWE_EMU8000
	acard->devwt = pnp_request_card_device(card, id->devs[1].id, acard->dev);
#endif
	/* Audio initialization */
	pdev = acard->dev;

	pnp_init_resource_table(cfg); 
	 
	/* override resources */ 

	if (port[dev] != SNDRV_AUTO_PORT)
		pnp_resource_change(&cfg->port_resource[0], port[dev], 16);
	if (mpu_port[dev] != SNDRV_AUTO_PORT)
		pnp_resource_change(&cfg->port_resource[1], mpu_port[dev], 2);
	if (fm_port[dev] != SNDRV_AUTO_PORT)
		pnp_resource_change(&cfg->port_resource[2], fm_port[dev], 4);
	if (dma8[dev] != SNDRV_AUTO_DMA)
		pnp_resource_change(&cfg->dma_resource[0], dma8[dev], 1);
	if (dma16[dev] != SNDRV_AUTO_DMA)
		pnp_resource_change(&cfg->dma_resource[1], dma16[dev], 1);
	if (irq[dev] != SNDRV_AUTO_IRQ)
		pnp_resource_change(&cfg->irq_resource[0], irq[dev], 1);
	if (pnp_manual_config_dev(pdev, cfg, 0) < 0) 
		snd_printk(KERN_ERR PFX "AUDIO the requested resources are invalid, using auto config\n"); 
	err = pnp_activate_dev(pdev); 
	if (err < 0) { 
		snd_printk(KERN_ERR PFX "AUDIO pnp configure failure\n"); 
		kfree(cfg);
		return err; 
	} 
	port[dev] = pnp_port_start(pdev, 0);
	mpu_port[dev] = pnp_port_start(pdev, 1);
	fm_port[dev] = pnp_port_start(pdev, 2);
	dma8[dev] = pnp_dma(pdev, 0);
	dma16[dev] = pnp_dma(pdev, 1);
	irq[dev] = pnp_irq(pdev, 0);
	snd_printdd("pnp SB16: port=0x%lx, mpu port=0x%lx, fm port=0x%lx\n",
			port[dev], mpu_port[dev], fm_port[dev]);
	snd_printdd("pnp SB16: dma1=%i, dma2=%i, irq=%i\n",
			dma8[dev], dma16[dev], irq[dev]);
#ifdef SNDRV_SBAWE_EMU8000
	/* WaveTable initialization */
	pdev = acard->devwt;
	if (pdev != NULL) {
		pnp_init_resource_table(cfg); 
	 
		/* override resources */ 

		if (awe_port[dev] != SNDRV_AUTO_PORT) {
			pnp_resource_change(&cfg->port_resource[0], awe_port[dev], 4);
			pnp_resource_change(&cfg->port_resource[1], awe_port[dev] + 0x400, 4);
			pnp_resource_change(&cfg->port_resource[2], awe_port[dev] + 0x800, 4);
		}
		if ((pnp_manual_config_dev(pdev, cfg, 0)) < 0) 
			snd_printk(KERN_ERR PFX "WaveTable the requested resources are invalid, using auto config\n"); 
		err = pnp_activate_dev(pdev); 
		if (err < 0) { 
			goto __wt_error; 
		} 
		awe_port[dev] = pnp_port_start(pdev, 0);
		snd_printdd("pnp SB16: wavetable port=0x%lx\n", pnp_port_start(pdev, 0));
	} else {
__wt_error:
		if (pdev) {
			pnp_release_card_device(pdev);
			snd_printk(KERN_ERR PFX "WaveTable pnp configure failure\n");
		}
		acard->devwt = NULL;
		awe_port[dev] = -1;
	}
#endif
	kfree(cfg);
	return 0;
}

#endif /* CONFIG_PNP */

static void snd_sb16_free(snd_card_t *card)
{
	struct snd_card_sb16 *acard = (struct snd_card_sb16 *)card->private_data;
        
	if (acard == NULL)
		return;
	release_and_free_resource(acard->fm_res);
}

#ifdef CONFIG_PNP
#define is_isapnp_selected(dev)		isapnp[dev]
#else
#define is_isapnp_selected(dev)		0
#endif

static int __init snd_sb16_probe(int dev,
				 struct pnp_card_link *pcard,
				 const struct pnp_card_device_id *pid)
{
	static int possible_irqs[] = {5, 9, 10, 7, -1};
	static int possible_dmas8[] = {1, 3, 0, -1};
	static int possible_dmas16[] = {5, 6, 7, -1};
	int xirq, xdma8, xdma16;
	sb_t *chip;
	snd_card_t *card;
	struct snd_card_sb16 *acard;
	opl3_t *opl3;
	snd_hwdep_t *synth = NULL;
#ifdef CONFIG_SND_SB16_CSP
	snd_hwdep_t *xcsp = NULL;
#endif
	unsigned long flags;
	int err;

	card = snd_card_new(index[dev], id[dev], THIS_MODULE,
			    sizeof(struct snd_card_sb16));
	if (card == NULL)
		return -ENOMEM;
	acard = (struct snd_card_sb16 *) card->private_data;
	card->private_free = snd_sb16_free;
#ifdef CONFIG_PNP
	if (isapnp[dev]) {
		if ((err = snd_card_sb16_pnp(dev, acard, pcard, pid)))
			goto _err;
		snd_card_set_dev(card, &pcard->card->dev);
	}
#endif

	xirq = irq[dev];
	xdma8 = dma8[dev];
	xdma16 = dma16[dev];
	if (! is_isapnp_selected(dev)) {
		if (xirq == SNDRV_AUTO_IRQ) {
			if ((xirq = snd_legacy_find_free_irq(possible_irqs)) < 0) {
				snd_printk(KERN_ERR PFX "unable to find a free IRQ\n");
				err = -EBUSY;
				goto _err;
			}
		}
		if (xdma8 == SNDRV_AUTO_DMA) {
			if ((xdma8 = snd_legacy_find_free_dma(possible_dmas8)) < 0) {
				snd_printk(KERN_ERR PFX "unable to find a free 8-bit DMA\n");
				err = -EBUSY;
				goto _err;
			}
		}
		if (xdma16 == SNDRV_AUTO_DMA) {
			if ((xdma16 = snd_legacy_find_free_dma(possible_dmas16)) < 0) {
				snd_printk(KERN_ERR PFX "unable to find a free 16-bit DMA\n");
				err = -EBUSY;
				goto _err;
			}
		}
		/* non-PnP FM port address is hardwired with base port address */
		fm_port[dev] = port[dev];
		/* block the 0x388 port to avoid PnP conflicts */
		acard->fm_res = request_region(0x388, 4, "SoundBlaster FM");
#ifdef SNDRV_SBAWE_EMU8000
		/* non-PnP AWE port address is hardwired with base port address */
		awe_port[dev] = port[dev] + 0x400;
#endif
	}

	if ((err = snd_sbdsp_create(card,
				    port[dev],
				    xirq,
				    snd_sb16dsp_interrupt,
				    xdma8,
				    xdma16,
				    SB_HW_AUTO,
				    &chip)) < 0)
		goto _err;

	if (chip->hardware != SB_HW_16) {
		snd_printk(KERN_ERR PFX "SB 16 chip was not detected at 0x%lx\n", port[dev]);
		err = -ENODEV;
		goto _err;
	}
	chip->mpu_port = mpu_port[dev];
	if (! is_isapnp_selected(dev) && (err = snd_sb16dsp_configure(chip)) < 0)
		goto _err;

	if ((err = snd_sb16dsp_pcm(chip, 0, NULL)) < 0)
		goto _err;

	strcpy(card->driver,
#ifdef SNDRV_SBAWE_EMU8000
			awe_port[dev] > 0 ? "SB AWE" :
#endif
			"SB16");
	strcpy(card->shortname, chip->name);
	sprintf(card->longname, "%s at 0x%lx, irq %i, dma ",
		chip->name,
		chip->port,
		xirq);
	if (xdma8 >= 0)
		sprintf(card->longname + strlen(card->longname), "%d", xdma8);
	if (xdma16 >= 0)
		sprintf(card->longname + strlen(card->longname), "%s%d",
			xdma8 >= 0 ? "&" : "", xdma16);

	if (chip->mpu_port > 0 && chip->mpu_port != SNDRV_AUTO_PORT) {
		if ((err = snd_mpu401_uart_new(card, 0, MPU401_HW_SB,
					       chip->mpu_port, 0,
					       xirq, 0, &chip->rmidi)) < 0)
			goto _err;
		chip->rmidi_callback = snd_mpu401_uart_interrupt;
	}

#ifdef SNDRV_SBAWE_EMU8000
	if (awe_port[dev] == SNDRV_AUTO_PORT)
		awe_port[dev] = 0; /* disable */
#endif

	if (fm_port[dev] > 0 && fm_port[dev] != SNDRV_AUTO_PORT) {
		if (snd_opl3_create(card, fm_port[dev], fm_port[dev] + 2,
				    OPL3_HW_OPL3,
				    acard->fm_res != NULL || fm_port[dev] == port[dev],
				    &opl3) < 0) {
			snd_printk(KERN_ERR PFX "no OPL device at 0x%lx-0x%lx\n",
				   fm_port[dev], fm_port[dev] + 2);
		} else {
#ifdef SNDRV_SBAWE_EMU8000
			int seqdev = awe_port[dev] > 0 ? 2 : 1;
#else
			int seqdev = 1;
#endif
			if ((err = snd_opl3_hwdep_new(opl3, 0, seqdev, &synth)) < 0)
				goto _err;
		}
	}

	if ((err = snd_sbmixer_new(chip)) < 0)
		goto _err;

#ifdef CONFIG_SND_SB16_CSP
	/* CSP chip on SB16ASP/AWE32 */
	if ((chip->hardware == SB_HW_16) && csp[dev]) {
		snd_sb_csp_new(chip, synth != NULL ? 1 : 0, &xcsp);
		if (xcsp) {
			chip->csp = xcsp->private_data;
			chip->hardware = SB_HW_16CSP;
		} else {
			snd_printk(KERN_INFO PFX "warning - CSP chip not detected on soundcard #%i\n", dev + 1);
		}
	}
#endif
#ifdef SNDRV_SBAWE_EMU8000
	if (awe_port[dev] > 0) {
		if ((err = snd_emu8000_new(card, 1, awe_port[dev],
					   seq_ports[dev], NULL)) < 0) {
			snd_printk(KERN_ERR PFX "fatal error - EMU-8000 synthesizer not detected at 0x%lx\n", awe_port[dev]);

			goto _err;
		}
	}
#endif

	/* setup Mic AGC */
	spin_lock_irqsave(&chip->mixer_lock, flags);
	snd_sbmixer_write(chip, SB_DSP4_MIC_AGC,
		(snd_sbmixer_read(chip, SB_DSP4_MIC_AGC) & 0x01) |
		(mic_agc[dev] ? 0x00 : 0x01));
	spin_unlock_irqrestore(&chip->mixer_lock, flags);

	if ((err = snd_card_set_generic_dev(card)) < 0)
		goto _err;

	if ((err = snd_card_register(card)) < 0)
		goto _err;

	if (pcard)
		pnp_set_card_drvdata(pcard, card);
	else
		snd_sb16_legacy[dev] = card;
	return 0;

 _err:
	snd_card_free(card);
	return err;
}

static int __init snd_sb16_probe_legacy_port(unsigned long xport)
{
	static int dev;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev] || port[dev] != SNDRV_AUTO_PORT)
			continue;
		if (is_isapnp_selected(dev))
			continue;
		port[dev] = xport;
		res = snd_sb16_probe(dev, NULL, NULL);
		if (res < 0)
			port[dev] = SNDRV_AUTO_PORT;
		return res;
	}
	return -ENODEV;
}

#ifdef CONFIG_PNP

static int __devinit snd_sb16_pnp_detect(struct pnp_card_link *card,
					 const struct pnp_card_device_id *id)
{
	static int dev;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev] || !isapnp[dev])
			continue;
		res = snd_sb16_probe(dev, card, id);
		if (res < 0)
			return res;
		dev++;
		return 0;
	}

	return -ENODEV;
}

static void __devexit snd_sb16_pnp_remove(struct pnp_card_link * pcard)
{
	snd_card_t *card = (snd_card_t *) pnp_get_card_drvdata(pcard);

	snd_card_disconnect(card);
	snd_card_free_in_thread(card);
}

static struct pnp_card_driver sb16_pnpc_driver = {
	.flags = PNP_DRIVER_RES_DISABLE,
	.name = "sb16",
	.id_table = snd_sb16_pnpids,
	.probe = snd_sb16_pnp_detect,
	.remove = __devexit_p(snd_sb16_pnp_remove),
};

#endif /* CONFIG_PNP */

static int __init alsa_card_sb16_init(void)
{
	int dev, cards = 0, i;
	static unsigned long possible_ports[] = {0x220, 0x240, 0x260, 0x280, -1};

	/* legacy non-auto cards at first */
	for (dev = 0; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev] || port[dev] == SNDRV_AUTO_PORT)
			continue;
		if (is_isapnp_selected(dev))
			continue;
		if (!snd_sb16_probe(dev, NULL, NULL)) {
			cards++;
			continue;
		}
#ifdef MODULE
		snd_printk(KERN_ERR "Sound Blaster 16+ soundcard #%i not found at 0x%lx or device busy\n", dev, port[dev]);
#endif
	}
	/* legacy auto configured cards */
	i = snd_legacy_auto_probe(possible_ports, snd_sb16_probe_legacy_port);
	if (i > 0)
		cards += i;

#ifdef CONFIG_PNP
	/* PnP cards at last */
	i = pnp_register_card_driver(&sb16_pnpc_driver);
	if (i >0)
		cards += i;
#endif

	if (!cards) {
#ifdef CONFIG_PNP
		pnp_unregister_card_driver(&sb16_pnpc_driver);
#endif
#ifdef MODULE
		snd_printk(KERN_ERR "Sound Blaster 16 soundcard not found or device busy\n");
#ifdef SNDRV_SBAWE_EMU8000
		snd_printk(KERN_ERR "In case, if you have non-AWE card, try snd-sb16 module\n");
#else
		snd_printk(KERN_ERR "In case, if you have AWE card, try snd-sbawe module\n");
#endif
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_sb16_exit(void)
{
	int dev;

#ifdef CONFIG_PNP
	/* PnP cards first */
	pnp_unregister_card_driver(&sb16_pnpc_driver);
#endif
	for (dev = 0; dev < SNDRV_CARDS; dev++)
		snd_card_free(snd_sb16_legacy[dev]);
}

module_init(alsa_card_sb16_init)
module_exit(alsa_card_sb16_exit)
