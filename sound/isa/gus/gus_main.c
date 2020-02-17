// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Routines for Gravis UltraSound soundcards
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/gus.h>
#include <sound/control.h>

#include <asm/dma.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Routines for Gravis UltraSound soundcards");
MODULE_LICENSE("GPL");

static int snd_gus_init_dma_irq(struct snd_gus_card * gus, int latches);

int snd_gus_use_inc(struct snd_gus_card * gus)
{
	if (!try_module_get(gus->card->module))
		return 0;
	return 1;
}

void snd_gus_use_dec(struct snd_gus_card * gus)
{
	module_put(gus->card->module);
}

static int snd_gus_joystick_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 31;
	return 0;
}

static int snd_gus_joystick_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_gus_card *gus = snd_kcontrol_chip(kcontrol);
	
	ucontrol->value.integer.value[0] = gus->joystick_dac & 31;
	return 0;
}

static int snd_gus_joystick_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_gus_card *gus = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	unsigned char nval;
	
	nval = ucontrol->value.integer.value[0] & 31;
	spin_lock_irqsave(&gus->reg_lock, flags);
	change = gus->joystick_dac != nval;
	gus->joystick_dac = nval;
	snd_gf1_write8(gus, SNDRV_GF1_GB_JOYSTICK_DAC_LEVEL, gus->joystick_dac);
	spin_unlock_irqrestore(&gus->reg_lock, flags);
	return change;
}

static const struct snd_kcontrol_new snd_gus_joystick_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.name = "Joystick Speed",
	.info = snd_gus_joystick_info,
	.get = snd_gus_joystick_get,
	.put = snd_gus_joystick_put
};

static void snd_gus_init_control(struct snd_gus_card *gus)
{
	int ret;

	if (!gus->ace_flag) {
		ret =
			snd_ctl_add(gus->card,
					snd_ctl_new1(&snd_gus_joystick_control,
						gus));
		if (ret)
			snd_printk(KERN_ERR "gus: snd_ctl_add failed: %d\n",
					ret);
	}
}

/*
 *
 */

static int snd_gus_free(struct snd_gus_card *gus)
{
	if (gus->gf1.res_port2 == NULL)
		goto __hw_end;
	snd_gf1_stop(gus);
	snd_gus_init_dma_irq(gus, 0);
      __hw_end:
	release_and_free_resource(gus->gf1.res_port1);
	release_and_free_resource(gus->gf1.res_port2);
	if (gus->gf1.irq >= 0)
		free_irq(gus->gf1.irq, (void *) gus);
	if (gus->gf1.dma1 >= 0) {
		disable_dma(gus->gf1.dma1);
		free_dma(gus->gf1.dma1);
	}
	if (!gus->equal_dma && gus->gf1.dma2 >= 0) {
		disable_dma(gus->gf1.dma2);
		free_dma(gus->gf1.dma2);
	}
	kfree(gus);
	return 0;
}

static int snd_gus_dev_free(struct snd_device *device)
{
	struct snd_gus_card *gus = device->device_data;
	return snd_gus_free(gus);
}

int snd_gus_create(struct snd_card *card,
		   unsigned long port,
		   int irq, int dma1, int dma2,
		   int timer_dev,
		   int voices,
		   int pcm_channels,
		   int effect,
		   struct snd_gus_card **rgus)
{
	struct snd_gus_card *gus;
	int err;
	static const struct snd_device_ops ops = {
		.dev_free =	snd_gus_dev_free,
	};

	*rgus = NULL;
	gus = kzalloc(sizeof(*gus), GFP_KERNEL);
	if (gus == NULL)
		return -ENOMEM;
	spin_lock_init(&gus->reg_lock);
	spin_lock_init(&gus->voice_alloc);
	spin_lock_init(&gus->active_voice_lock);
	spin_lock_init(&gus->event_lock);
	spin_lock_init(&gus->dma_lock);
	spin_lock_init(&gus->pcm_volume_level_lock);
	spin_lock_init(&gus->uart_cmd_lock);
	mutex_init(&gus->dma_mutex);
	gus->gf1.irq = -1;
	gus->gf1.dma1 = -1;
	gus->gf1.dma2 = -1;
	gus->card = card;
	gus->gf1.port = port;
	/* fill register variables for speedup */
	gus->gf1.reg_page = GUSP(gus, GF1PAGE);
	gus->gf1.reg_regsel = GUSP(gus, GF1REGSEL);
	gus->gf1.reg_data8 = GUSP(gus, GF1DATAHIGH);
	gus->gf1.reg_data16 = GUSP(gus, GF1DATALOW);
	gus->gf1.reg_irqstat = GUSP(gus, IRQSTAT);
	gus->gf1.reg_dram = GUSP(gus, DRAM);
	gus->gf1.reg_timerctrl = GUSP(gus, TIMERCNTRL);
	gus->gf1.reg_timerdata = GUSP(gus, TIMERDATA);
	/* allocate resources */
	if ((gus->gf1.res_port1 = request_region(port, 16, "GUS GF1 (Adlib/SB)")) == NULL) {
		snd_printk(KERN_ERR "gus: can't grab SB port 0x%lx\n", port);
		snd_gus_free(gus);
		return -EBUSY;
	}
	if ((gus->gf1.res_port2 = request_region(port + 0x100, 12, "GUS GF1 (Synth)")) == NULL) {
		snd_printk(KERN_ERR "gus: can't grab synth port 0x%lx\n", port + 0x100);
		snd_gus_free(gus);
		return -EBUSY;
	}
	if (irq >= 0 && request_irq(irq, snd_gus_interrupt, 0, "GUS GF1", (void *) gus)) {
		snd_printk(KERN_ERR "gus: can't grab irq %d\n", irq);
		snd_gus_free(gus);
		return -EBUSY;
	}
	gus->gf1.irq = irq;
	card->sync_irq = irq;
	if (request_dma(dma1, "GUS - 1")) {
		snd_printk(KERN_ERR "gus: can't grab DMA1 %d\n", dma1);
		snd_gus_free(gus);
		return -EBUSY;
	}
	gus->gf1.dma1 = dma1;
	if (dma2 >= 0 && dma1 != dma2) {
		if (request_dma(dma2, "GUS - 2")) {
			snd_printk(KERN_ERR "gus: can't grab DMA2 %d\n", dma2);
			snd_gus_free(gus);
			return -EBUSY;
		}
		gus->gf1.dma2 = dma2;
	} else {
		gus->gf1.dma2 = gus->gf1.dma1;
		gus->equal_dma = 1;
	}
	gus->timer_dev = timer_dev;
	if (voices < 14)
		voices = 14;
	if (voices > 32)
		voices = 32;
	if (pcm_channels < 0)
		pcm_channels = 0;
	if (pcm_channels > 8)
		pcm_channels = 8;
	pcm_channels++;
	pcm_channels &= ~1;
	gus->gf1.effect = effect ? 1 : 0;
	gus->gf1.active_voices = voices;
	gus->gf1.pcm_channels = pcm_channels;
	gus->gf1.volume_ramp = 25;
	gus->gf1.smooth_pan = 1;
	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, gus, &ops)) < 0) {
		snd_gus_free(gus);
		return err;
	}
	*rgus = gus;
	return 0;
}

/*
 *  Memory detection routine for plain GF1 soundcards
 */

static int snd_gus_detect_memory(struct snd_gus_card * gus)
{
	int l, idx, local;
	unsigned char d;

	snd_gf1_poke(gus, 0L, 0xaa);
	snd_gf1_poke(gus, 1L, 0x55);
	if (snd_gf1_peek(gus, 0L) != 0xaa || snd_gf1_peek(gus, 1L) != 0x55) {
		snd_printk(KERN_ERR "plain GF1 card at 0x%lx without onboard DRAM?\n", gus->gf1.port);
		return -ENOMEM;
	}
	for (idx = 1, d = 0xab; idx < 4; idx++, d++) {
		local = idx << 18;
		snd_gf1_poke(gus, local, d);
		snd_gf1_poke(gus, local + 1, d + 1);
		if (snd_gf1_peek(gus, local) != d ||
		    snd_gf1_peek(gus, local + 1) != d + 1 ||
		    snd_gf1_peek(gus, 0L) != 0xaa)
			break;
	}
#if 1
	gus->gf1.memory = idx << 18;
#else
	gus->gf1.memory = 256 * 1024;
#endif
	for (l = 0, local = gus->gf1.memory; l < 4; l++, local -= 256 * 1024) {
		gus->gf1.mem_alloc.banks_8[l].address =
		    gus->gf1.mem_alloc.banks_8[l].size = 0;
		gus->gf1.mem_alloc.banks_16[l].address = l << 18;
		gus->gf1.mem_alloc.banks_16[l].size = local > 0 ? 256 * 1024 : 0;
	}
	gus->gf1.mem_alloc.banks_8[0].size = gus->gf1.memory;
	return 0;		/* some memory were detected */
}

static int snd_gus_init_dma_irq(struct snd_gus_card * gus, int latches)
{
	struct snd_card *card;
	unsigned long flags;
	int irq, dma1, dma2;
	static const unsigned char irqs[16] =
		{0, 0, 1, 3, 0, 2, 0, 4, 0, 1, 0, 5, 6, 0, 0, 7};
	static const unsigned char dmas[8] =
		{6, 1, 0, 2, 0, 3, 4, 5};

	if (snd_BUG_ON(!gus))
		return -EINVAL;
	card = gus->card;
	if (snd_BUG_ON(!card))
		return -EINVAL;

	gus->mix_cntrl_reg &= 0xf8;
	gus->mix_cntrl_reg |= 0x01;	/* disable MIC, LINE IN, enable LINE OUT */
	if (gus->codec_flag || gus->ess_flag) {
		gus->mix_cntrl_reg &= ~1;	/* enable LINE IN */
		gus->mix_cntrl_reg |= 4;	/* enable MIC */
	}
	dma1 = gus->gf1.dma1;
	dma1 = abs(dma1);
	dma1 = dmas[dma1 & 7];
	dma2 = gus->gf1.dma2;
	dma2 = abs(dma2);
	dma2 = dmas[dma2 & 7];
	dma1 |= gus->equal_dma ? 0x40 : (dma2 << 3);

	if ((dma1 & 7) == 0 || (dma2 & 7) == 0) {
		snd_printk(KERN_ERR "Error! DMA isn't defined.\n");
		return -EINVAL;
	}
	irq = gus->gf1.irq;
	irq = abs(irq);
	irq = irqs[irq & 0x0f];
	if (irq == 0) {
		snd_printk(KERN_ERR "Error! IRQ isn't defined.\n");
		return -EINVAL;
	}
	irq |= 0x40;
#if 0
	card->mixer.mix_ctrl_reg |= 0x10;
#endif

	spin_lock_irqsave(&gus->reg_lock, flags);
	outb(5, GUSP(gus, REGCNTRLS));
	outb(gus->mix_cntrl_reg, GUSP(gus, MIXCNTRLREG));
	outb(0x00, GUSP(gus, IRQDMACNTRLREG));
	outb(0, GUSP(gus, REGCNTRLS));
	spin_unlock_irqrestore(&gus->reg_lock, flags);

	udelay(100);

	spin_lock_irqsave(&gus->reg_lock, flags);
	outb(0x00 | gus->mix_cntrl_reg, GUSP(gus, MIXCNTRLREG));
	outb(dma1, GUSP(gus, IRQDMACNTRLREG));
	if (latches) {
		outb(0x40 | gus->mix_cntrl_reg, GUSP(gus, MIXCNTRLREG));
		outb(irq, GUSP(gus, IRQDMACNTRLREG));
	}
	spin_unlock_irqrestore(&gus->reg_lock, flags);

	udelay(100);

	spin_lock_irqsave(&gus->reg_lock, flags);
	outb(0x00 | gus->mix_cntrl_reg, GUSP(gus, MIXCNTRLREG));
	outb(dma1, GUSP(gus, IRQDMACNTRLREG));
	if (latches) {
		outb(0x40 | gus->mix_cntrl_reg, GUSP(gus, MIXCNTRLREG));
		outb(irq, GUSP(gus, IRQDMACNTRLREG));
	}
	spin_unlock_irqrestore(&gus->reg_lock, flags);

	snd_gf1_delay(gus);

	if (latches)
		gus->mix_cntrl_reg |= 0x08;	/* enable latches */
	else
		gus->mix_cntrl_reg &= ~0x08;	/* disable latches */
	spin_lock_irqsave(&gus->reg_lock, flags);
	outb(gus->mix_cntrl_reg, GUSP(gus, MIXCNTRLREG));
	outb(0, GUSP(gus, GF1PAGE));
	spin_unlock_irqrestore(&gus->reg_lock, flags);

	return 0;
}

static int snd_gus_check_version(struct snd_gus_card * gus)
{
	unsigned long flags;
	unsigned char val, rev;
	struct snd_card *card;

	card = gus->card;
	spin_lock_irqsave(&gus->reg_lock, flags);
	outb(0x20, GUSP(gus, REGCNTRLS));
	val = inb(GUSP(gus, REGCNTRLS));
	rev = inb(GUSP(gus, BOARDVERSION));
	spin_unlock_irqrestore(&gus->reg_lock, flags);
	snd_printdd("GF1 [0x%lx] init - val = 0x%x, rev = 0x%x\n", gus->gf1.port, val, rev);
	strcpy(card->driver, "GUS");
	strcpy(card->longname, "Gravis UltraSound Classic (2.4)");
	if ((val != 255 && (val & 0x06)) || (rev >= 5 && rev != 255)) {
		if (rev >= 5 && rev <= 9) {
			gus->ics_flag = 1;
			if (rev == 5)
				gus->ics_flipped = 1;
			card->longname[27] = '3';
			card->longname[29] = rev == 5 ? '5' : '7';
		}
		if (rev >= 10 && rev != 255) {
			if (rev >= 10 && rev <= 11) {
				strcpy(card->driver, "GUS MAX");
				strcpy(card->longname, "Gravis UltraSound MAX");
				gus->max_flag = 1;
			} else if (rev == 0x30) {
				strcpy(card->driver, "GUS ACE");
				strcpy(card->longname, "Gravis UltraSound Ace");
				gus->ace_flag = 1;
			} else if (rev == 0x50) {
				strcpy(card->driver, "GUS Extreme");
				strcpy(card->longname, "Gravis UltraSound Extreme");
				gus->ess_flag = 1;
			} else {
				snd_printk(KERN_ERR "unknown GF1 revision number at 0x%lx - 0x%x (0x%x)\n", gus->gf1.port, rev, val);
				snd_printk(KERN_ERR "  please - report to <perex@perex.cz>\n");
			}
		}
	}
	strcpy(card->shortname, card->longname);
	gus->uart_enable = 1;	/* standard GUSes doesn't have midi uart trouble */
	snd_gus_init_control(gus);
	return 0;
}

int snd_gus_initialize(struct snd_gus_card *gus)
{
	int err;

	if (!gus->interwave) {
		if ((err = snd_gus_check_version(gus)) < 0) {
			snd_printk(KERN_ERR "version check failed\n");
			return err;
		}
		if ((err = snd_gus_detect_memory(gus)) < 0)
			return err;
	}
	if ((err = snd_gus_init_dma_irq(gus, 1)) < 0)
		return err;
	snd_gf1_start(gus);
	gus->initialized = 1;
	return 0;
}

  /* gus_io.c */
EXPORT_SYMBOL(snd_gf1_delay);
EXPORT_SYMBOL(snd_gf1_write8);
EXPORT_SYMBOL(snd_gf1_look8);
EXPORT_SYMBOL(snd_gf1_write16);
EXPORT_SYMBOL(snd_gf1_look16);
EXPORT_SYMBOL(snd_gf1_i_write8);
EXPORT_SYMBOL(snd_gf1_i_look8);
EXPORT_SYMBOL(snd_gf1_i_look16);
EXPORT_SYMBOL(snd_gf1_dram_addr);
EXPORT_SYMBOL(snd_gf1_write_addr);
EXPORT_SYMBOL(snd_gf1_poke);
EXPORT_SYMBOL(snd_gf1_peek);
  /* gus_reset.c */
EXPORT_SYMBOL(snd_gf1_alloc_voice);
EXPORT_SYMBOL(snd_gf1_free_voice);
EXPORT_SYMBOL(snd_gf1_ctrl_stop);
EXPORT_SYMBOL(snd_gf1_stop_voice);
  /* gus_mixer.c */
EXPORT_SYMBOL(snd_gf1_new_mixer);
  /* gus_pcm.c */
EXPORT_SYMBOL(snd_gf1_pcm_new);
  /* gus.c */
EXPORT_SYMBOL(snd_gus_use_inc);
EXPORT_SYMBOL(snd_gus_use_dec);
EXPORT_SYMBOL(snd_gus_create);
EXPORT_SYMBOL(snd_gus_initialize);
  /* gus_irq.c */
EXPORT_SYMBOL(snd_gus_interrupt);
  /* gus_uart.c */
EXPORT_SYMBOL(snd_gf1_rawmidi_new);
  /* gus_dram.c */
EXPORT_SYMBOL(snd_gus_dram_write);
EXPORT_SYMBOL(snd_gus_dram_read);
  /* gus_volume.c */
EXPORT_SYMBOL(snd_gf1_lvol_to_gvol_raw);
EXPORT_SYMBOL(snd_gf1_translate_freq);
  /* gus_mem.c */
EXPORT_SYMBOL(snd_gf1_mem_alloc);
EXPORT_SYMBOL(snd_gf1_mem_xfree);
EXPORT_SYMBOL(snd_gf1_mem_free);
EXPORT_SYMBOL(snd_gf1_mem_lock);
