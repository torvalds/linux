/*
 *  Support for Digigram Lola PCI-e boards
 *
 *  Copyright (c) 2011 Takashi Iwai <tiwai@suse.de>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include "lola.h"

/* Standard options */
static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Digigram Lola driver.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Digigram Lola driver.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Digigram Lola driver.");

/* Lola-specific options */

/* for instance use always max granularity which is compatible
 * with all sample rates
 */
static int granularity[SNDRV_CARDS] = {
	[0 ... (SNDRV_CARDS - 1)] = LOLA_GRANULARITY_MAX
};

/* below a sample_rate of 16kHz the analogue audio quality is NOT excellent */
static int sample_rate_min[SNDRV_CARDS] = {
	[0 ... (SNDRV_CARDS - 1) ] = 16000
};

module_param_array(granularity, int, NULL, 0444);
MODULE_PARM_DESC(granularity, "Granularity value");
module_param_array(sample_rate_min, int, NULL, 0444);
MODULE_PARM_DESC(sample_rate_min, "Minimal sample rate");

/*
 */

MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Digigram, Lola}}");
MODULE_DESCRIPTION("Digigram Lola driver");
MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");

#ifdef CONFIG_SND_DEBUG_VERBOSE
static int debug;
module_param(debug, int, 0644);
#define verbose_debug(fmt, args...)			\
	do { if (debug > 1) printk(KERN_DEBUG SFX fmt, ##args); } while (0)
#else
#define verbose_debug(fmt, args...)
#endif

/*
 * pseudo-codec read/write via CORB/RIRB
 */

static int corb_send_verb(struct lola *chip, unsigned int nid,
			  unsigned int verb, unsigned int data,
			  unsigned int extdata)
{
	unsigned long flags;
	int ret = -EIO;

	chip->last_cmd_nid = nid;
	chip->last_verb = verb;
	chip->last_data = data;
	chip->last_extdata = extdata;
	data |= (nid << 20) | (verb << 8);

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (chip->rirb.cmds < LOLA_CORB_ENTRIES - 1) {
		unsigned int wp = chip->corb.wp + 1;
		wp %= LOLA_CORB_ENTRIES;
		chip->corb.wp = wp;
		chip->corb.buf[wp * 2] = cpu_to_le32(data);
		chip->corb.buf[wp * 2 + 1] = cpu_to_le32(extdata);
		lola_writew(chip, BAR0, CORBWP, wp);
		chip->rirb.cmds++;
		smp_wmb();
		ret = 0;
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return ret;
}

static void lola_queue_unsol_event(struct lola *chip, unsigned int res,
				   unsigned int res_ex)
{
	lola_update_ext_clock_freq(chip, res);
}

/* retrieve RIRB entry - called from interrupt handler */
static void lola_update_rirb(struct lola *chip)
{
	unsigned int rp, wp;
	u32 res, res_ex;

	wp = lola_readw(chip, BAR0, RIRBWP);
	if (wp == chip->rirb.wp)
		return;
	chip->rirb.wp = wp;

	while (chip->rirb.rp != wp) {
		chip->rirb.rp++;
		chip->rirb.rp %= LOLA_CORB_ENTRIES;

		rp = chip->rirb.rp << 1; /* an RIRB entry is 8-bytes */
		res_ex = le32_to_cpu(chip->rirb.buf[rp + 1]);
		res = le32_to_cpu(chip->rirb.buf[rp]);
		if (res_ex & LOLA_RIRB_EX_UNSOL_EV)
			lola_queue_unsol_event(chip, res, res_ex);
		else if (chip->rirb.cmds) {
			chip->res = res;
			chip->res_ex = res_ex;
			smp_wmb();
			chip->rirb.cmds--;
		}
	}
}

static int rirb_get_response(struct lola *chip, unsigned int *val,
			     unsigned int *extval)
{
	unsigned long timeout;

 again:
	timeout = jiffies + msecs_to_jiffies(1000);
	for (;;) {
		if (chip->polling_mode) {
			spin_lock_irq(&chip->reg_lock);
			lola_update_rirb(chip);
			spin_unlock_irq(&chip->reg_lock);
		}
		if (!chip->rirb.cmds) {
			*val = chip->res;
			if (extval)
				*extval = chip->res_ex;
			verbose_debug("get_response: %x, %x\n",
				      chip->res, chip->res_ex);
			if (chip->res_ex & LOLA_RIRB_EX_ERROR) {
				printk(KERN_WARNING SFX "RIRB ERROR: "
				       "NID=%x, verb=%x, data=%x, ext=%x\n",
				       chip->last_cmd_nid,
				       chip->last_verb, chip->last_data,
				       chip->last_extdata);
				return -EIO;
			}
			return 0;
		}
		if (time_after(jiffies, timeout))
			break;
		udelay(20);
		cond_resched();
	}
	printk(KERN_WARNING SFX "RIRB response error\n");
	if (!chip->polling_mode) {
		printk(KERN_WARNING SFX "switching to polling mode\n");
		chip->polling_mode = 1;
		goto again;
	}
	return -EIO;
}

/* aynchronous write of a codec verb with data */
int lola_codec_write(struct lola *chip, unsigned int nid, unsigned int verb,
		     unsigned int data, unsigned int extdata)
{
	verbose_debug("codec_write NID=%x, verb=%x, data=%x, ext=%x\n",
		      nid, verb, data, extdata);
	return corb_send_verb(chip, nid, verb, data, extdata);
}

/* write a codec verb with data and read the returned status */
int lola_codec_read(struct lola *chip, unsigned int nid, unsigned int verb,
		    unsigned int data, unsigned int extdata,
		    unsigned int *val, unsigned int *extval)
{
	int err;

	verbose_debug("codec_read NID=%x, verb=%x, data=%x, ext=%x\n",
		      nid, verb, data, extdata);
	err = corb_send_verb(chip, nid, verb, data, extdata);
	if (err < 0)
		return err;
	err = rirb_get_response(chip, val, extval);
	return err;
}

/* flush all pending codec writes */
int lola_codec_flush(struct lola *chip)
{
	unsigned int tmp;
	return rirb_get_response(chip, &tmp, NULL);
}

/*
 * interrupt handler
 */
static irqreturn_t lola_interrupt(int irq, void *dev_id)
{
	struct lola *chip = dev_id;
	unsigned int notify_ins, notify_outs, error_ins, error_outs;
	int handled = 0;
	int i;

	notify_ins = notify_outs = error_ins = error_outs = 0;
	spin_lock(&chip->reg_lock);
	for (;;) {
		unsigned int status, in_sts, out_sts;
		unsigned int reg;

		status = lola_readl(chip, BAR1, DINTSTS);
		if (!status || status == -1)
			break;

		in_sts = lola_readl(chip, BAR1, DIINTSTS);
		out_sts = lola_readl(chip, BAR1, DOINTSTS);

		/* clear Input Interrupts */
		for (i = 0; in_sts && i < chip->pcm[CAPT].num_streams; i++) {
			if (!(in_sts & (1 << i)))
				continue;
			in_sts &= ~(1 << i);
			reg = lola_dsd_read(chip, i, STS);
			if (reg & LOLA_DSD_STS_DESE) /* error */
				error_ins |= (1 << i);
			if (reg & LOLA_DSD_STS_BCIS) /* notify */
				notify_ins |= (1 << i);
			/* clear */
			lola_dsd_write(chip, i, STS, reg);
		}

		/* clear Output Interrupts */
		for (i = 0; out_sts && i < chip->pcm[PLAY].num_streams; i++) {
			if (!(out_sts & (1 << i)))
				continue;
			out_sts &= ~(1 << i);
			reg = lola_dsd_read(chip, i + MAX_STREAM_IN_COUNT, STS);
			if (reg & LOLA_DSD_STS_DESE) /* error */
				error_outs |= (1 << i);
			if (reg & LOLA_DSD_STS_BCIS) /* notify */
				notify_outs |= (1 << i);
			lola_dsd_write(chip, i + MAX_STREAM_IN_COUNT, STS, reg);
		}

		if (status & LOLA_DINT_CTRL) {
			unsigned char rbsts; /* ring status is byte access */
			rbsts = lola_readb(chip, BAR0, RIRBSTS);
			rbsts &= LOLA_RIRB_INT_MASK;
			if (rbsts)
				lola_writeb(chip, BAR0, RIRBSTS, rbsts);
			rbsts = lola_readb(chip, BAR0, CORBSTS);
			rbsts &= LOLA_CORB_INT_MASK;
			if (rbsts)
				lola_writeb(chip, BAR0, CORBSTS, rbsts);

			lola_update_rirb(chip);
		}

		if (status & (LOLA_DINT_FIFOERR | LOLA_DINT_MUERR)) {
			/* clear global fifo error interrupt */
			lola_writel(chip, BAR1, DINTSTS,
				    (status & (LOLA_DINT_FIFOERR | LOLA_DINT_MUERR)));
		}
		handled = 1;
	}
	spin_unlock(&chip->reg_lock);

	lola_pcm_update(chip, &chip->pcm[CAPT], notify_ins);
	lola_pcm_update(chip, &chip->pcm[PLAY], notify_outs);

	return IRQ_RETVAL(handled);
}


/*
 * controller
 */
static int reset_controller(struct lola *chip)
{
	unsigned int gctl = lola_readl(chip, BAR0, GCTL);
	unsigned long end_time;

	if (gctl) {
		/* to be sure */
		lola_writel(chip, BAR1, BOARD_MODE, 0);
		return 0;
	}

	chip->cold_reset = 1;
	lola_writel(chip, BAR0, GCTL, LOLA_GCTL_RESET);
	end_time = jiffies + msecs_to_jiffies(200);
	do {
		msleep(1);
		gctl = lola_readl(chip, BAR0, GCTL);
		if (gctl)
			break;
	} while (time_before(jiffies, end_time));
	if (!gctl) {
		printk(KERN_ERR SFX "cannot reset controller\n");
		return -EIO;
	}
	return 0;
}

static void lola_irq_enable(struct lola *chip)
{
	unsigned int val;

	/* enalbe all I/O streams */
	val = (1 << chip->pcm[PLAY].num_streams) - 1;
	lola_writel(chip, BAR1, DOINTCTL, val);
	val = (1 << chip->pcm[CAPT].num_streams) - 1;
	lola_writel(chip, BAR1, DIINTCTL, val);

	/* enable global irqs */
	val = LOLA_DINT_GLOBAL | LOLA_DINT_CTRL | LOLA_DINT_FIFOERR |
		LOLA_DINT_MUERR;
	lola_writel(chip, BAR1, DINTCTL, val);
}

static void lola_irq_disable(struct lola *chip)
{
	lola_writel(chip, BAR1, DINTCTL, 0);
	lola_writel(chip, BAR1, DIINTCTL, 0);
	lola_writel(chip, BAR1, DOINTCTL, 0);
}

static int setup_corb_rirb(struct lola *chip)
{
	int err;
	unsigned char tmp;
	unsigned long end_time;

	err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV,
				  snd_dma_pci_data(chip->pci),
				  PAGE_SIZE, &chip->rb);
	if (err < 0)
		return err;

	chip->corb.addr = chip->rb.addr;
	chip->corb.buf = (u32 *)chip->rb.area;
	chip->rirb.addr = chip->rb.addr + 2048;
	chip->rirb.buf = (u32 *)(chip->rb.area + 2048);

	/* disable ringbuffer DMAs */
	lola_writeb(chip, BAR0, RIRBCTL, 0);
	lola_writeb(chip, BAR0, CORBCTL, 0);

	end_time = jiffies + msecs_to_jiffies(200);
	do {
		if (!lola_readb(chip, BAR0, RIRBCTL) &&
		    !lola_readb(chip, BAR0, CORBCTL))
			break;
		msleep(1);
	} while (time_before(jiffies, end_time));

	/* CORB set up */
	lola_writel(chip, BAR0, CORBLBASE, (u32)chip->corb.addr);
	lola_writel(chip, BAR0, CORBUBASE, upper_32_bits(chip->corb.addr));
	/* set the corb size to 256 entries */
	lola_writeb(chip, BAR0, CORBSIZE, 0x02);
	/* set the corb write pointer to 0 */
	lola_writew(chip, BAR0, CORBWP, 0);
	/* reset the corb hw read pointer */
	lola_writew(chip, BAR0, CORBRP, LOLA_RBRWP_CLR);
	/* enable corb dma */
	lola_writeb(chip, BAR0, CORBCTL, LOLA_RBCTL_DMA_EN);
	/* clear flags if set */
	tmp = lola_readb(chip, BAR0, CORBSTS) & LOLA_CORB_INT_MASK;
	if (tmp)
		lola_writeb(chip, BAR0, CORBSTS, tmp);
	chip->corb.wp = 0;

	/* RIRB set up */
	lola_writel(chip, BAR0, RIRBLBASE, (u32)chip->rirb.addr);
	lola_writel(chip, BAR0, RIRBUBASE, upper_32_bits(chip->rirb.addr));
	/* set the rirb size to 256 entries */
	lola_writeb(chip, BAR0, RIRBSIZE, 0x02);
	/* reset the rirb hw write pointer */
	lola_writew(chip, BAR0, RIRBWP, LOLA_RBRWP_CLR);
	/* set N=1, get RIRB response interrupt for new entry */
	lola_writew(chip, BAR0, RINTCNT, 1);
	/* enable rirb dma and response irq */
	lola_writeb(chip, BAR0, RIRBCTL, LOLA_RBCTL_DMA_EN | LOLA_RBCTL_IRQ_EN);
	/* clear flags if set */
	tmp =  lola_readb(chip, BAR0, RIRBSTS) & LOLA_RIRB_INT_MASK;
	if (tmp)
		lola_writeb(chip, BAR0, RIRBSTS, tmp);
	chip->rirb.rp = chip->rirb.cmds = 0;

	return 0;
}

static void stop_corb_rirb(struct lola *chip)
{
	/* disable ringbuffer DMAs */
	lola_writeb(chip, BAR0, RIRBCTL, 0);
	lola_writeb(chip, BAR0, CORBCTL, 0);
}

static void lola_reset_setups(struct lola *chip)
{
	/* update the granularity */
	lola_set_granularity(chip, chip->granularity, true);
	/* update the sample clock */
	lola_set_clock_index(chip, chip->clock.cur_index);
	/* enable unsolicited events of the clock widget */
	lola_enable_clock_events(chip);
	/* update the analog gains */
	lola_setup_all_analog_gains(chip, CAPT, false); /* input, update */
	/* update SRC configuration if applicable */
	lola_set_src_config(chip, chip->input_src_mask, false);
	/* update the analog outputs */
	lola_setup_all_analog_gains(chip, PLAY, false); /* output, update */
}

static int __devinit lola_parse_tree(struct lola *chip)
{
	unsigned int val;
	int nid, err;

	err = lola_read_param(chip, 0, LOLA_PAR_VENDOR_ID, &val);
	if (err < 0) {
		printk(KERN_ERR SFX "Can't read VENDOR_ID\n");
		return err;
	}
	val >>= 16;
	if (val != 0x1369) {
		printk(KERN_ERR SFX "Unknown codec vendor 0x%x\n", val);
		return -EINVAL;
	}

	err = lola_read_param(chip, 1, LOLA_PAR_FUNCTION_TYPE, &val);
	if (err < 0) {
		printk(KERN_ERR SFX "Can't read FUNCTION_TYPE for 0x%x\n", nid);
		return err;
	}
	if (val != 1) {
		printk(KERN_ERR SFX "Unknown function type %d\n", val);
		return -EINVAL;
	}

	err = lola_read_param(chip, 1, LOLA_PAR_SPECIFIC_CAPS, &val);
	if (err < 0) {
		printk(KERN_ERR SFX "Can't read SPECCAPS\n");
		return err;
	}
	chip->lola_caps = val;
	chip->pin[CAPT].num_pins = LOLA_AFG_INPUT_PIN_COUNT(chip->lola_caps);
	chip->pin[PLAY].num_pins = LOLA_AFG_OUTPUT_PIN_COUNT(chip->lola_caps);
	snd_printdd(SFX "speccaps=0x%x, pins in=%d, out=%d\n",
		    chip->lola_caps,
		    chip->pin[CAPT].num_pins, chip->pin[PLAY].num_pins);

	if (chip->pin[CAPT].num_pins > MAX_AUDIO_INOUT_COUNT ||
	    chip->pin[PLAY].num_pins > MAX_AUDIO_INOUT_COUNT) {
		printk(KERN_ERR SFX "Invalid Lola-spec caps 0x%x\n", val);
		return -EINVAL;
	}

	nid = 0x02;
	err = lola_init_pcm(chip, CAPT, &nid);
	if (err < 0)
		return err;
	err = lola_init_pcm(chip, PLAY, &nid);
	if (err < 0)
		return err;

	err = lola_init_pins(chip, CAPT, &nid);
	if (err < 0)
		return err;
	err = lola_init_pins(chip, PLAY, &nid);
	if (err < 0)
		return err;

	if (LOLA_AFG_CLOCK_WIDGET_PRESENT(chip->lola_caps)) {
		err = lola_init_clock_widget(chip, nid);
		if (err < 0)
			return err;
		nid++;
	}
	if (LOLA_AFG_MIXER_WIDGET_PRESENT(chip->lola_caps)) {
		err = lola_init_mixer_widget(chip, nid);
		if (err < 0)
			return err;
		nid++;
	}

	/* enable unsolicited events of the clock widget */
	err = lola_enable_clock_events(chip);
	if (err < 0)
		return err;

	/* if last ResetController was not a ColdReset, we don't know
	 * the state of the card; initialize here again
	 */
	if (!chip->cold_reset) {
		lola_reset_setups(chip);
		chip->cold_reset = 1;
	} else {
		/* set the granularity if it is not the default */
		if (chip->granularity != LOLA_GRANULARITY_MIN)
			lola_set_granularity(chip, chip->granularity, true);
	}

	return 0;
}

static void lola_stop_hw(struct lola *chip)
{
	stop_corb_rirb(chip);
	lola_irq_disable(chip);
}

static void lola_free(struct lola *chip)
{
	if (chip->initialized)
		lola_stop_hw(chip);
	lola_free_pcm(chip);
	lola_free_mixer(chip);
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *)chip);
	if (chip->bar[0].remap_addr)
		iounmap(chip->bar[0].remap_addr);
	if (chip->bar[1].remap_addr)
		iounmap(chip->bar[1].remap_addr);
	if (chip->rb.area)
		snd_dma_free_pages(&chip->rb);
	pci_release_regions(chip->pci);
	pci_disable_device(chip->pci);
	kfree(chip);
}

static int lola_dev_free(struct snd_device *device)
{
	lola_free(device->device_data);
	return 0;
}

static int __devinit lola_create(struct snd_card *card, struct pci_dev *pci,
				 int dev, struct lola **rchip)
{
	struct lola *chip;
	int err;
	unsigned int dever;
	static struct snd_device_ops ops = {
		.dev_free = lola_dev_free,
	};

	*rchip = NULL;

	err = pci_enable_device(pci);
	if (err < 0)
		return err;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		snd_printk(KERN_ERR SFX "cannot allocate chip\n");
		pci_disable_device(pci);
		return -ENOMEM;
	}

	spin_lock_init(&chip->reg_lock);
	mutex_init(&chip->open_mutex);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;

	chip->granularity = granularity[dev];
	switch (chip->granularity) {
	case 8:
		chip->sample_rate_max = 48000;
		break;
	case 16:
		chip->sample_rate_max = 96000;
		break;
	case 32:
		chip->sample_rate_max = 192000;
		break;
	default:
		snd_printk(KERN_WARNING SFX
			   "Invalid granularity %d, reset to %d\n",
			   chip->granularity, LOLA_GRANULARITY_MAX);
		chip->granularity = LOLA_GRANULARITY_MAX;
		chip->sample_rate_max = 192000;
		break;
	}
	chip->sample_rate_min = sample_rate_min[dev];
	if (chip->sample_rate_min > chip->sample_rate_max) {
		snd_printk(KERN_WARNING SFX
			   "Invalid sample_rate_min %d, reset to 16000\n",
			   chip->sample_rate_min);
		chip->sample_rate_min = 16000;
	}

	err = pci_request_regions(pci, DRVNAME);
	if (err < 0) {
		kfree(chip);
		pci_disable_device(pci);
		return err;
	}

	chip->bar[0].addr = pci_resource_start(pci, 0);
	chip->bar[0].remap_addr = pci_ioremap_bar(pci, 0);
	chip->bar[1].addr = pci_resource_start(pci, 2);
	chip->bar[1].remap_addr = pci_ioremap_bar(pci, 2);
	if (!chip->bar[0].remap_addr || !chip->bar[1].remap_addr) {
		snd_printk(KERN_ERR SFX "ioremap error\n");
		err = -ENXIO;
		goto errout;
	}

	pci_set_master(pci);

	err = reset_controller(chip);
	if (err < 0)
		goto errout;

	if (request_irq(pci->irq, lola_interrupt, IRQF_SHARED,
			KBUILD_MODNAME, chip)) {
		printk(KERN_ERR SFX "unable to grab IRQ %d\n", pci->irq);
		err = -EBUSY;
		goto errout;
	}
	chip->irq = pci->irq;
	synchronize_irq(chip->irq);

	dever = lola_readl(chip, BAR1, DEVER);
	chip->pcm[CAPT].num_streams = (dever >> 0) & 0x3ff;
	chip->pcm[PLAY].num_streams = (dever >> 10) & 0x3ff;
	chip->version = (dever >> 24) & 0xff;
	snd_printdd(SFX "streams in=%d, out=%d, version=0x%x\n",
		    chip->pcm[CAPT].num_streams, chip->pcm[PLAY].num_streams,
		    chip->version);

	/* Test LOLA_BAR1_DEVER */
	if (chip->pcm[CAPT].num_streams > MAX_STREAM_IN_COUNT ||
	    chip->pcm[PLAY].num_streams > MAX_STREAM_OUT_COUNT ||
	    (!chip->pcm[CAPT].num_streams &&
	     !chip->pcm[PLAY].num_streams)) {
		printk(KERN_ERR SFX "invalid DEVER = %x\n", dever);
		err = -EINVAL;
		goto errout;
	}

	err = setup_corb_rirb(chip);
	if (err < 0)
		goto errout;

	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
	if (err < 0) {
		snd_printk(KERN_ERR SFX "Error creating device [card]!\n");
		goto errout;
	}

	strcpy(card->driver, "Lola");
	strlcpy(card->shortname, "Digigram Lola", sizeof(card->shortname));
	snprintf(card->longname, sizeof(card->longname),
		 "%s at 0x%lx irq %i",
		 card->shortname, chip->bar[0].addr, chip->irq);
	strcpy(card->mixername, card->shortname);

	lola_irq_enable(chip);

	chip->initialized = 1;
	*rchip = chip;
	return 0;

 errout:
	lola_free(chip);
	return err;
}

static int __devinit lola_probe(struct pci_dev *pci,
				const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct lola *chip;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	err = snd_card_create(index[dev], id[dev], THIS_MODULE, 0, &card);
	if (err < 0) {
		snd_printk(KERN_ERR SFX "Error creating card!\n");
		return err;
	}

	snd_card_set_dev(card, &pci->dev);

	err = lola_create(card, pci, dev, &chip);
	if (err < 0)
		goto out_free;
	card->private_data = chip;

	err = lola_parse_tree(chip);
	if (err < 0)
		goto out_free;

	err = lola_create_pcm(chip);
	if (err < 0)
		goto out_free;

	err = lola_create_mixer(chip);
	if (err < 0)
		goto out_free;

	lola_proc_debug_new(chip);

	err = snd_card_register(card);
	if (err < 0)
		goto out_free;

	pci_set_drvdata(pci, card);
	dev++;
	return err;
out_free:
	snd_card_free(card);
	return err;
}

static void __devexit lola_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

/* PCI IDs */
static DEFINE_PCI_DEVICE_TABLE(lola_ids) = {
	{ PCI_VDEVICE(DIGIGRAM, 0x0001) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, lola_ids);

/* pci_driver definition */
static struct pci_driver lola_driver = {
	.name = KBUILD_MODNAME,
	.id_table = lola_ids,
	.probe = lola_probe,
	.remove = __devexit_p(lola_remove),
};

module_pci_driver(lola_driver);
