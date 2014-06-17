/*
 *
 *  hda_intel.c - Implementation of primary alsa driver code base
 *                for Intel HD Audio.
 *
 *  Copyright(c) 2004 Intel Corporation. All rights reserved.
 *
 *  Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 *                     PeiSen Hou <pshou@realtek.com.tw>
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
 *
 *  CONTACTS:
 *
 *  Matt Jared		matt.jared@intel.com
 *  Andy Kopp		andy.kopp@intel.com
 *  Dan Kogan		dan.d.kogan@intel.com
 *
 *  CHANGES:
 *
 *  2004.12.01	Major rewrite by tiwai, merged the work of pshou
 * 
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/clocksource.h>
#include <linux/time.h>
#include <linux/completion.h>

#ifdef CONFIG_X86
/* for snoop control */
#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#endif
#include <sound/core.h>
#include <sound/initval.h>
#include <linux/vgaarb.h>
#include <linux/vga_switcheroo.h>
#include <linux/firmware.h>
#include "hda_codec.h"
#include "hda_i915.h"
#include "hda_controller.h"
#include "hda_priv.h"


static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;
static char *model[SNDRV_CARDS];
static int position_fix[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] = -1};
static int bdl_pos_adj[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] = -1};
static int probe_mask[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] = -1};
static int probe_only[SNDRV_CARDS];
static int jackpoll_ms[SNDRV_CARDS];
static bool single_cmd;
static int enable_msi = -1;
#ifdef CONFIG_SND_HDA_PATCH_LOADER
static char *patch[SNDRV_CARDS];
#endif
#ifdef CONFIG_SND_HDA_INPUT_BEEP
static bool beep_mode[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] =
					CONFIG_SND_HDA_INPUT_BEEP_MODE};
#endif

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Intel HD audio interface.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Intel HD audio interface.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Intel HD audio interface.");
module_param_array(model, charp, NULL, 0444);
MODULE_PARM_DESC(model, "Use the given board model.");
module_param_array(position_fix, int, NULL, 0444);
MODULE_PARM_DESC(position_fix, "DMA pointer read method."
		 "(-1 = system default, 0 = auto, 1 = LPIB, 2 = POSBUF, 3 = VIACOMBO, 4 = COMBO).");
module_param_array(bdl_pos_adj, int, NULL, 0644);
MODULE_PARM_DESC(bdl_pos_adj, "BDL position adjustment offset.");
module_param_array(probe_mask, int, NULL, 0444);
MODULE_PARM_DESC(probe_mask, "Bitmask to probe codecs (default = -1).");
module_param_array(probe_only, int, NULL, 0444);
MODULE_PARM_DESC(probe_only, "Only probing and no codec initialization.");
module_param_array(jackpoll_ms, int, NULL, 0444);
MODULE_PARM_DESC(jackpoll_ms, "Ms between polling for jack events (default = 0, using unsol events only)");
module_param(single_cmd, bool, 0444);
MODULE_PARM_DESC(single_cmd, "Use single command to communicate with codecs "
		 "(for debugging only).");
module_param(enable_msi, bint, 0444);
MODULE_PARM_DESC(enable_msi, "Enable Message Signaled Interrupt (MSI)");
#ifdef CONFIG_SND_HDA_PATCH_LOADER
module_param_array(patch, charp, NULL, 0444);
MODULE_PARM_DESC(patch, "Patch file for Intel HD audio interface.");
#endif
#ifdef CONFIG_SND_HDA_INPUT_BEEP
module_param_array(beep_mode, bool, NULL, 0444);
MODULE_PARM_DESC(beep_mode, "Select HDA Beep registration mode "
			    "(0=off, 1=on) (default=1).");
#endif

#ifdef CONFIG_PM
static int param_set_xint(const char *val, const struct kernel_param *kp);
static struct kernel_param_ops param_ops_xint = {
	.set = param_set_xint,
	.get = param_get_int,
};
#define param_check_xint param_check_int

static int power_save = CONFIG_SND_HDA_POWER_SAVE_DEFAULT;
static int *power_save_addr = &power_save;
module_param(power_save, xint, 0644);
MODULE_PARM_DESC(power_save, "Automatic power-saving timeout "
		 "(in second, 0 = disable).");

/* reset the HD-audio controller in power save mode.
 * this may give more power-saving, but will take longer time to
 * wake up.
 */
static bool power_save_controller = 1;
module_param(power_save_controller, bool, 0644);
MODULE_PARM_DESC(power_save_controller, "Reset controller in power save mode.");
#else
static int *power_save_addr;
#endif /* CONFIG_PM */

static int align_buffer_size = -1;
module_param(align_buffer_size, bint, 0644);
MODULE_PARM_DESC(align_buffer_size,
		"Force buffer and period sizes to be multiple of 128 bytes.");

#ifdef CONFIG_X86
static bool hda_snoop = true;
module_param_named(snoop, hda_snoop, bool, 0444);
MODULE_PARM_DESC(snoop, "Enable/disable snooping");
#else
#define hda_snoop		true
#endif


MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Intel, ICH6},"
			 "{Intel, ICH6M},"
			 "{Intel, ICH7},"
			 "{Intel, ESB2},"
			 "{Intel, ICH8},"
			 "{Intel, ICH9},"
			 "{Intel, ICH10},"
			 "{Intel, PCH},"
			 "{Intel, CPT},"
			 "{Intel, PPT},"
			 "{Intel, LPT},"
			 "{Intel, LPT_LP},"
			 "{Intel, WPT_LP},"
			 "{Intel, HPT},"
			 "{Intel, PBG},"
			 "{Intel, SCH},"
			 "{ATI, SB450},"
			 "{ATI, SB600},"
			 "{ATI, RS600},"
			 "{ATI, RS690},"
			 "{ATI, RS780},"
			 "{ATI, R600},"
			 "{ATI, RV630},"
			 "{ATI, RV610},"
			 "{ATI, RV670},"
			 "{ATI, RV635},"
			 "{ATI, RV620},"
			 "{ATI, RV770},"
			 "{VIA, VT8251},"
			 "{VIA, VT8237A},"
			 "{SiS, SIS966},"
			 "{ULI, M5461}}");
MODULE_DESCRIPTION("Intel HDA driver");

#if defined(CONFIG_PM) && defined(CONFIG_VGA_SWITCHEROO)
#if IS_ENABLED(CONFIG_SND_HDA_CODEC_HDMI)
#define SUPPORT_VGA_SWITCHEROO
#endif
#endif


/*
 */

/* driver types */
enum {
	AZX_DRIVER_ICH,
	AZX_DRIVER_PCH,
	AZX_DRIVER_SCH,
	AZX_DRIVER_HDMI,
	AZX_DRIVER_ATI,
	AZX_DRIVER_ATIHDMI,
	AZX_DRIVER_ATIHDMI_NS,
	AZX_DRIVER_VIA,
	AZX_DRIVER_SIS,
	AZX_DRIVER_ULI,
	AZX_DRIVER_NVIDIA,
	AZX_DRIVER_TERA,
	AZX_DRIVER_CTX,
	AZX_DRIVER_CTHDA,
	AZX_DRIVER_GENERIC,
	AZX_NUM_DRIVERS, /* keep this as last entry */
};

/* quirks for Intel PCH */
#define AZX_DCAPS_INTEL_PCH_NOPM \
	(AZX_DCAPS_SCH_SNOOP | AZX_DCAPS_BUFSIZE | \
	 AZX_DCAPS_COUNT_LPIB_DELAY)

#define AZX_DCAPS_INTEL_PCH \
	(AZX_DCAPS_INTEL_PCH_NOPM | AZX_DCAPS_PM_RUNTIME)

#define AZX_DCAPS_INTEL_HASWELL \
	(AZX_DCAPS_SCH_SNOOP | AZX_DCAPS_ALIGN_BUFSIZE | \
	 AZX_DCAPS_COUNT_LPIB_DELAY | AZX_DCAPS_PM_RUNTIME | \
	 AZX_DCAPS_I915_POWERWELL)

/* quirks for ATI SB / AMD Hudson */
#define AZX_DCAPS_PRESET_ATI_SB \
	(AZX_DCAPS_ATI_SNOOP | AZX_DCAPS_NO_TCSEL | \
	 AZX_DCAPS_SYNC_WRITE | AZX_DCAPS_POSFIX_LPIB)

/* quirks for ATI/AMD HDMI */
#define AZX_DCAPS_PRESET_ATI_HDMI \
	(AZX_DCAPS_NO_TCSEL | AZX_DCAPS_SYNC_WRITE | AZX_DCAPS_POSFIX_LPIB)

/* quirks for Nvidia */
#define AZX_DCAPS_PRESET_NVIDIA \
	(AZX_DCAPS_NVIDIA_SNOOP | AZX_DCAPS_RIRB_DELAY | AZX_DCAPS_NO_MSI |\
	 AZX_DCAPS_ALIGN_BUFSIZE | AZX_DCAPS_NO_64BIT |\
	 AZX_DCAPS_CORBRP_SELF_CLEAR)

#define AZX_DCAPS_PRESET_CTHDA \
	(AZX_DCAPS_NO_MSI | AZX_DCAPS_POSFIX_LPIB | AZX_DCAPS_4K_BDLE_BOUNDARY)

/*
 * VGA-switcher support
 */
#ifdef SUPPORT_VGA_SWITCHEROO
#define use_vga_switcheroo(chip)	((chip)->use_vga_switcheroo)
#else
#define use_vga_switcheroo(chip)	0
#endif

static char *driver_short_names[] = {
	[AZX_DRIVER_ICH] = "HDA Intel",
	[AZX_DRIVER_PCH] = "HDA Intel PCH",
	[AZX_DRIVER_SCH] = "HDA Intel MID",
	[AZX_DRIVER_HDMI] = "HDA Intel HDMI",
	[AZX_DRIVER_ATI] = "HDA ATI SB",
	[AZX_DRIVER_ATIHDMI] = "HDA ATI HDMI",
	[AZX_DRIVER_ATIHDMI_NS] = "HDA ATI HDMI",
	[AZX_DRIVER_VIA] = "HDA VIA VT82xx",
	[AZX_DRIVER_SIS] = "HDA SIS966",
	[AZX_DRIVER_ULI] = "HDA ULI M5461",
	[AZX_DRIVER_NVIDIA] = "HDA NVidia",
	[AZX_DRIVER_TERA] = "HDA Teradici", 
	[AZX_DRIVER_CTX] = "HDA Creative", 
	[AZX_DRIVER_CTHDA] = "HDA Creative",
	[AZX_DRIVER_GENERIC] = "HD-Audio Generic",
};

#ifdef CONFIG_X86
static void __mark_pages_wc(struct azx *chip, struct snd_dma_buffer *dmab, bool on)
{
	int pages;

	if (azx_snoop(chip))
		return;
	if (!dmab || !dmab->area || !dmab->bytes)
		return;

#ifdef CONFIG_SND_DMA_SGBUF
	if (dmab->dev.type == SNDRV_DMA_TYPE_DEV_SG) {
		struct snd_sg_buf *sgbuf = dmab->private_data;
		if (on)
			set_pages_array_wc(sgbuf->page_table, sgbuf->pages);
		else
			set_pages_array_wb(sgbuf->page_table, sgbuf->pages);
		return;
	}
#endif

	pages = (dmab->bytes + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (on)
		set_memory_wc((unsigned long)dmab->area, pages);
	else
		set_memory_wb((unsigned long)dmab->area, pages);
}

static inline void mark_pages_wc(struct azx *chip, struct snd_dma_buffer *buf,
				 bool on)
{
	__mark_pages_wc(chip, buf, on);
}
static inline void mark_runtime_wc(struct azx *chip, struct azx_dev *azx_dev,
				   struct snd_pcm_substream *substream, bool on)
{
	if (azx_dev->wc_marked != on) {
		__mark_pages_wc(chip, snd_pcm_get_dma_buf(substream), on);
		azx_dev->wc_marked = on;
	}
}
#else
/* NOP for other archs */
static inline void mark_pages_wc(struct azx *chip, struct snd_dma_buffer *buf,
				 bool on)
{
}
static inline void mark_runtime_wc(struct azx *chip, struct azx_dev *azx_dev,
				   struct snd_pcm_substream *substream, bool on)
{
}
#endif

static int azx_acquire_irq(struct azx *chip, int do_disconnect);

/*
 * initialize the PCI registers
 */
/* update bits in a PCI register byte */
static void update_pci_byte(struct pci_dev *pci, unsigned int reg,
			    unsigned char mask, unsigned char val)
{
	unsigned char data;

	pci_read_config_byte(pci, reg, &data);
	data &= ~mask;
	data |= (val & mask);
	pci_write_config_byte(pci, reg, data);
}

static void azx_init_pci(struct azx *chip)
{
	/* Clear bits 0-2 of PCI register TCSEL (at offset 0x44)
	 * TCSEL == Traffic Class Select Register, which sets PCI express QOS
	 * Ensuring these bits are 0 clears playback static on some HD Audio
	 * codecs.
	 * The PCI register TCSEL is defined in the Intel manuals.
	 */
	if (!(chip->driver_caps & AZX_DCAPS_NO_TCSEL)) {
		dev_dbg(chip->card->dev, "Clearing TCSEL\n");
		update_pci_byte(chip->pci, ICH6_PCIREG_TCSEL, 0x07, 0);
	}

	/* For ATI SB450/600/700/800/900 and AMD Hudson azalia HD audio,
	 * we need to enable snoop.
	 */
	if (chip->driver_caps & AZX_DCAPS_ATI_SNOOP) {
		dev_dbg(chip->card->dev, "Setting ATI snoop: %d\n",
			azx_snoop(chip));
		update_pci_byte(chip->pci,
				ATI_SB450_HDAUDIO_MISC_CNTR2_ADDR, 0x07,
				azx_snoop(chip) ? ATI_SB450_HDAUDIO_ENABLE_SNOOP : 0);
	}

	/* For NVIDIA HDA, enable snoop */
	if (chip->driver_caps & AZX_DCAPS_NVIDIA_SNOOP) {
		dev_dbg(chip->card->dev, "Setting Nvidia snoop: %d\n",
			azx_snoop(chip));
		update_pci_byte(chip->pci,
				NVIDIA_HDA_TRANSREG_ADDR,
				0x0f, NVIDIA_HDA_ENABLE_COHBITS);
		update_pci_byte(chip->pci,
				NVIDIA_HDA_ISTRM_COH,
				0x01, NVIDIA_HDA_ENABLE_COHBIT);
		update_pci_byte(chip->pci,
				NVIDIA_HDA_OSTRM_COH,
				0x01, NVIDIA_HDA_ENABLE_COHBIT);
	}

	/* Enable SCH/PCH snoop if needed */
	if (chip->driver_caps & AZX_DCAPS_SCH_SNOOP) {
		unsigned short snoop;
		pci_read_config_word(chip->pci, INTEL_SCH_HDA_DEVC, &snoop);
		if ((!azx_snoop(chip) && !(snoop & INTEL_SCH_HDA_DEVC_NOSNOOP)) ||
		    (azx_snoop(chip) && (snoop & INTEL_SCH_HDA_DEVC_NOSNOOP))) {
			snoop &= ~INTEL_SCH_HDA_DEVC_NOSNOOP;
			if (!azx_snoop(chip))
				snoop |= INTEL_SCH_HDA_DEVC_NOSNOOP;
			pci_write_config_word(chip->pci, INTEL_SCH_HDA_DEVC, snoop);
			pci_read_config_word(chip->pci,
				INTEL_SCH_HDA_DEVC, &snoop);
		}
		dev_dbg(chip->card->dev, "SCH snoop: %s\n",
			(snoop & INTEL_SCH_HDA_DEVC_NOSNOOP) ?
			"Disabled" : "Enabled");
        }
}

static int azx_position_ok(struct azx *chip, struct azx_dev *azx_dev);

/* called from IRQ */
static int azx_position_check(struct azx *chip, struct azx_dev *azx_dev)
{
	int ok;

	ok = azx_position_ok(chip, azx_dev);
	if (ok == 1) {
		azx_dev->irq_pending = 0;
		return ok;
	} else if (ok == 0 && chip->bus && chip->bus->workq) {
		/* bogus IRQ, process it later */
		azx_dev->irq_pending = 1;
		queue_work(chip->bus->workq, &chip->irq_pending_work);
	}
	return 0;
}

/*
 * Check whether the current DMA position is acceptable for updating
 * periods.  Returns non-zero if it's OK.
 *
 * Many HD-audio controllers appear pretty inaccurate about
 * the update-IRQ timing.  The IRQ is issued before actually the
 * data is processed.  So, we need to process it afterwords in a
 * workqueue.
 */
static int azx_position_ok(struct azx *chip, struct azx_dev *azx_dev)
{
	u32 wallclk;
	unsigned int pos;

	wallclk = azx_readl(chip, WALLCLK) - azx_dev->start_wallclk;
	if (wallclk < (azx_dev->period_wallclk * 2) / 3)
		return -1;	/* bogus (too early) interrupt */

	pos = azx_get_position(chip, azx_dev, true);

	if (WARN_ONCE(!azx_dev->period_bytes,
		      "hda-intel: zero azx_dev->period_bytes"))
		return -1; /* this shouldn't happen! */
	if (wallclk < (azx_dev->period_wallclk * 5) / 4 &&
	    pos % azx_dev->period_bytes > azx_dev->period_bytes / 2)
		/* NG - it's below the first next period boundary */
		return chip->bdl_pos_adj[chip->dev_index] ? 0 : -1;
	azx_dev->start_wallclk += wallclk;
	return 1; /* OK, it's fine */
}

/*
 * The work for pending PCM period updates.
 */
static void azx_irq_pending_work(struct work_struct *work)
{
	struct azx *chip = container_of(work, struct azx, irq_pending_work);
	int i, pending, ok;

	if (!chip->irq_pending_warned) {
		dev_info(chip->card->dev,
			 "IRQ timing workaround is activated for card #%d. Suggest a bigger bdl_pos_adj.\n",
			 chip->card->number);
		chip->irq_pending_warned = 1;
	}

	for (;;) {
		pending = 0;
		spin_lock_irq(&chip->reg_lock);
		for (i = 0; i < chip->num_streams; i++) {
			struct azx_dev *azx_dev = &chip->azx_dev[i];
			if (!azx_dev->irq_pending ||
			    !azx_dev->substream ||
			    !azx_dev->running)
				continue;
			ok = azx_position_ok(chip, azx_dev);
			if (ok > 0) {
				azx_dev->irq_pending = 0;
				spin_unlock(&chip->reg_lock);
				snd_pcm_period_elapsed(azx_dev->substream);
				spin_lock(&chip->reg_lock);
			} else if (ok < 0) {
				pending = 0;	/* too early */
			} else
				pending++;
		}
		spin_unlock_irq(&chip->reg_lock);
		if (!pending)
			return;
		msleep(1);
	}
}

/* clear irq_pending flags and assure no on-going workq */
static void azx_clear_irq_pending(struct azx *chip)
{
	int i;

	spin_lock_irq(&chip->reg_lock);
	for (i = 0; i < chip->num_streams; i++)
		chip->azx_dev[i].irq_pending = 0;
	spin_unlock_irq(&chip->reg_lock);
}

static int azx_acquire_irq(struct azx *chip, int do_disconnect)
{
	if (request_irq(chip->pci->irq, azx_interrupt,
			chip->msi ? 0 : IRQF_SHARED,
			KBUILD_MODNAME, chip)) {
		dev_err(chip->card->dev,
			"unable to grab IRQ %d, disabling device\n",
			chip->pci->irq);
		if (do_disconnect)
			snd_card_disconnect(chip->card);
		return -1;
	}
	chip->irq = chip->pci->irq;
	pci_intx(chip->pci, !chip->msi);
	return 0;
}

#ifdef CONFIG_PM
static DEFINE_MUTEX(card_list_lock);
static LIST_HEAD(card_list);

static void azx_add_card_list(struct azx *chip)
{
	mutex_lock(&card_list_lock);
	list_add(&chip->list, &card_list);
	mutex_unlock(&card_list_lock);
}

static void azx_del_card_list(struct azx *chip)
{
	mutex_lock(&card_list_lock);
	list_del_init(&chip->list);
	mutex_unlock(&card_list_lock);
}

/* trigger power-save check at writing parameter */
static int param_set_xint(const char *val, const struct kernel_param *kp)
{
	struct azx *chip;
	struct hda_codec *c;
	int prev = power_save;
	int ret = param_set_int(val, kp);

	if (ret || prev == power_save)
		return ret;

	mutex_lock(&card_list_lock);
	list_for_each_entry(chip, &card_list, list) {
		if (!chip->bus || chip->disabled)
			continue;
		list_for_each_entry(c, &chip->bus->codec_list, list)
			snd_hda_power_sync(c);
	}
	mutex_unlock(&card_list_lock);
	return 0;
}
#else
#define azx_add_card_list(chip) /* NOP */
#define azx_del_card_list(chip) /* NOP */
#endif /* CONFIG_PM */

#if defined(CONFIG_PM_SLEEP) || defined(SUPPORT_VGA_SWITCHEROO)
/*
 * power management
 */
static int azx_suspend(struct device *dev)
{
	struct pci_dev *pci = to_pci_dev(dev);
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;
	struct azx_pcm *p;

	if (chip->disabled)
		return 0;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	azx_clear_irq_pending(chip);
	list_for_each_entry(p, &chip->pcm_list, list)
		snd_pcm_suspend_all(p->pcm);
	if (chip->initialized)
		snd_hda_suspend(chip->bus);
	azx_stop_chip(chip);
	azx_enter_link_reset(chip);
	if (chip->irq >= 0) {
		free_irq(chip->irq, chip);
		chip->irq = -1;
	}
	if (chip->msi)
		pci_disable_msi(chip->pci);
	pci_disable_device(pci);
	pci_save_state(pci);
	pci_set_power_state(pci, PCI_D3hot);
	if (chip->driver_caps & AZX_DCAPS_I915_POWERWELL)
		hda_display_power(false);
	return 0;
}

static int azx_resume(struct device *dev)
{
	struct pci_dev *pci = to_pci_dev(dev);
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;

	if (chip->disabled)
		return 0;

	if (chip->driver_caps & AZX_DCAPS_I915_POWERWELL)
		hda_display_power(true);
	pci_set_power_state(pci, PCI_D0);
	pci_restore_state(pci);
	if (pci_enable_device(pci) < 0) {
		dev_err(chip->card->dev,
			"pci_enable_device failed, disabling device\n");
		snd_card_disconnect(card);
		return -EIO;
	}
	pci_set_master(pci);
	if (chip->msi)
		if (pci_enable_msi(pci) < 0)
			chip->msi = 0;
	if (azx_acquire_irq(chip, 1) < 0)
		return -EIO;
	azx_init_pci(chip);

	azx_init_chip(chip, true);

	snd_hda_resume(chip->bus);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif /* CONFIG_PM_SLEEP || SUPPORT_VGA_SWITCHEROO */

#ifdef CONFIG_PM_RUNTIME
static int azx_runtime_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;

	if (chip->disabled)
		return 0;

	if (!(chip->driver_caps & AZX_DCAPS_PM_RUNTIME))
		return 0;

	/* enable controller wake up event */
	azx_writew(chip, WAKEEN, azx_readw(chip, WAKEEN) |
		  STATESTS_INT_MASK);

	azx_stop_chip(chip);
	azx_enter_link_reset(chip);
	azx_clear_irq_pending(chip);
	if (chip->driver_caps & AZX_DCAPS_I915_POWERWELL)
		hda_display_power(false);
	return 0;
}

static int azx_runtime_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;
	struct hda_bus *bus;
	struct hda_codec *codec;
	int status;

	if (chip->disabled)
		return 0;

	if (!(chip->driver_caps & AZX_DCAPS_PM_RUNTIME))
		return 0;

	if (chip->driver_caps & AZX_DCAPS_I915_POWERWELL)
		hda_display_power(true);

	/* Read STATESTS before controller reset */
	status = azx_readw(chip, STATESTS);

	azx_init_pci(chip);
	azx_init_chip(chip, true);

	bus = chip->bus;
	if (status && bus) {
		list_for_each_entry(codec, &bus->codec_list, list)
			if (status & (1 << codec->addr))
				queue_delayed_work(codec->bus->workq,
						   &codec->jackpoll_work, codec->jackpoll_interval);
	}

	/* disable controller Wake Up event*/
	azx_writew(chip, WAKEEN, azx_readw(chip, WAKEEN) &
			~STATESTS_INT_MASK);

	return 0;
}

static int azx_runtime_idle(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;

	if (chip->disabled)
		return 0;

	if (!power_save_controller ||
	    !(chip->driver_caps & AZX_DCAPS_PM_RUNTIME))
		return -EBUSY;

	return 0;
}

#endif /* CONFIG_PM_RUNTIME */

#ifdef CONFIG_PM
static const struct dev_pm_ops azx_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(azx_suspend, azx_resume)
	SET_RUNTIME_PM_OPS(azx_runtime_suspend, azx_runtime_resume, azx_runtime_idle)
};

#define AZX_PM_OPS	&azx_pm
#else
#define AZX_PM_OPS	NULL
#endif /* CONFIG_PM */


/*
 * reboot notifier for hang-up problem at power-down
 */
static int azx_halt(struct notifier_block *nb, unsigned long event, void *buf)
{
	struct azx *chip = container_of(nb, struct azx, reboot_notifier);
	snd_hda_bus_reboot_notify(chip->bus);
	azx_stop_chip(chip);
	return NOTIFY_OK;
}

static void azx_notifier_register(struct azx *chip)
{
	chip->reboot_notifier.notifier_call = azx_halt;
	register_reboot_notifier(&chip->reboot_notifier);
}

static void azx_notifier_unregister(struct azx *chip)
{
	if (chip->reboot_notifier.notifier_call)
		unregister_reboot_notifier(&chip->reboot_notifier);
}

static int azx_probe_continue(struct azx *chip);

#ifdef SUPPORT_VGA_SWITCHEROO
static struct pci_dev *get_bound_vga(struct pci_dev *pci);

static void azx_vs_set_state(struct pci_dev *pci,
			     enum vga_switcheroo_state state)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct azx *chip = card->private_data;
	bool disabled;

	wait_for_completion(&chip->probe_wait);
	if (chip->init_failed)
		return;

	disabled = (state == VGA_SWITCHEROO_OFF);
	if (chip->disabled == disabled)
		return;

	if (!chip->bus) {
		chip->disabled = disabled;
		if (!disabled) {
			dev_info(chip->card->dev,
				 "Start delayed initialization\n");
			if (azx_probe_continue(chip) < 0) {
				dev_err(chip->card->dev, "initialization error\n");
				chip->init_failed = true;
			}
		}
	} else {
		dev_info(chip->card->dev, "%s via VGA-switcheroo\n",
			 disabled ? "Disabling" : "Enabling");
		if (disabled) {
			pm_runtime_put_sync_suspend(card->dev);
			azx_suspend(card->dev);
			/* when we get suspended by vga switcheroo we end up in D3cold,
			 * however we have no ACPI handle, so pci/acpi can't put us there,
			 * put ourselves there */
			pci->current_state = PCI_D3cold;
			chip->disabled = true;
			if (snd_hda_lock_devices(chip->bus))
				dev_warn(chip->card->dev,
					 "Cannot lock devices!\n");
		} else {
			snd_hda_unlock_devices(chip->bus);
			pm_runtime_get_noresume(card->dev);
			chip->disabled = false;
			azx_resume(card->dev);
		}
	}
}

static bool azx_vs_can_switch(struct pci_dev *pci)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct azx *chip = card->private_data;

	wait_for_completion(&chip->probe_wait);
	if (chip->init_failed)
		return false;
	if (chip->disabled || !chip->bus)
		return true;
	if (snd_hda_lock_devices(chip->bus))
		return false;
	snd_hda_unlock_devices(chip->bus);
	return true;
}

static void init_vga_switcheroo(struct azx *chip)
{
	struct pci_dev *p = get_bound_vga(chip->pci);
	if (p) {
		dev_info(chip->card->dev,
			 "Handle VGA-switcheroo audio client\n");
		chip->use_vga_switcheroo = 1;
		pci_dev_put(p);
	}
}

static const struct vga_switcheroo_client_ops azx_vs_ops = {
	.set_gpu_state = azx_vs_set_state,
	.can_switch = azx_vs_can_switch,
};

static int register_vga_switcheroo(struct azx *chip)
{
	int err;

	if (!chip->use_vga_switcheroo)
		return 0;
	/* FIXME: currently only handling DIS controller
	 * is there any machine with two switchable HDMI audio controllers?
	 */
	err = vga_switcheroo_register_audio_client(chip->pci, &azx_vs_ops,
						    VGA_SWITCHEROO_DIS,
						    chip->bus != NULL);
	if (err < 0)
		return err;
	chip->vga_switcheroo_registered = 1;

	/* register as an optimus hdmi audio power domain */
	vga_switcheroo_init_domain_pm_optimus_hdmi_audio(chip->card->dev,
							 &chip->hdmi_pm_domain);
	return 0;
}
#else
#define init_vga_switcheroo(chip)		/* NOP */
#define register_vga_switcheroo(chip)		0
#define check_hdmi_disabled(pci)	false
#endif /* SUPPORT_VGA_SWITCHER */

/*
 * destructor
 */
static int azx_free(struct azx *chip)
{
	struct pci_dev *pci = chip->pci;
	int i;

	if ((chip->driver_caps & AZX_DCAPS_PM_RUNTIME)
			&& chip->running)
		pm_runtime_get_noresume(&pci->dev);

	azx_del_card_list(chip);

	azx_notifier_unregister(chip);

	chip->init_failed = 1; /* to be sure */
	complete_all(&chip->probe_wait);

	if (use_vga_switcheroo(chip)) {
		if (chip->disabled && chip->bus)
			snd_hda_unlock_devices(chip->bus);
		if (chip->vga_switcheroo_registered)
			vga_switcheroo_unregister_client(chip->pci);
	}

	if (chip->initialized) {
		azx_clear_irq_pending(chip);
		for (i = 0; i < chip->num_streams; i++)
			azx_stream_stop(chip, &chip->azx_dev[i]);
		azx_stop_chip(chip);
	}

	if (chip->irq >= 0)
		free_irq(chip->irq, (void*)chip);
	if (chip->msi)
		pci_disable_msi(chip->pci);
	if (chip->remap_addr)
		iounmap(chip->remap_addr);

	azx_free_stream_pages(chip);
	if (chip->region_requested)
		pci_release_regions(chip->pci);
	pci_disable_device(chip->pci);
	kfree(chip->azx_dev);
#ifdef CONFIG_SND_HDA_PATCH_LOADER
	if (chip->fw)
		release_firmware(chip->fw);
#endif
	if (chip->driver_caps & AZX_DCAPS_I915_POWERWELL) {
		hda_display_power(false);
		hda_i915_exit();
	}
	kfree(chip);

	return 0;
}

static int azx_dev_free(struct snd_device *device)
{
	return azx_free(device->device_data);
}

#ifdef SUPPORT_VGA_SWITCHEROO
/*
 * Check of disabled HDMI controller by vga-switcheroo
 */
static struct pci_dev *get_bound_vga(struct pci_dev *pci)
{
	struct pci_dev *p;

	/* check only discrete GPU */
	switch (pci->vendor) {
	case PCI_VENDOR_ID_ATI:
	case PCI_VENDOR_ID_AMD:
	case PCI_VENDOR_ID_NVIDIA:
		if (pci->devfn == 1) {
			p = pci_get_domain_bus_and_slot(pci_domain_nr(pci->bus),
							pci->bus->number, 0);
			if (p) {
				if ((p->class >> 8) == PCI_CLASS_DISPLAY_VGA)
					return p;
				pci_dev_put(p);
			}
		}
		break;
	}
	return NULL;
}

static bool check_hdmi_disabled(struct pci_dev *pci)
{
	bool vga_inactive = false;
	struct pci_dev *p = get_bound_vga(pci);

	if (p) {
		if (vga_switcheroo_get_client_state(p) == VGA_SWITCHEROO_OFF)
			vga_inactive = true;
		pci_dev_put(p);
	}
	return vga_inactive;
}
#endif /* SUPPORT_VGA_SWITCHEROO */

/*
 * white/black-listing for position_fix
 */
static struct snd_pci_quirk position_fix_list[] = {
	SND_PCI_QUIRK(0x1028, 0x01cc, "Dell D820", POS_FIX_LPIB),
	SND_PCI_QUIRK(0x1028, 0x01de, "Dell Precision 390", POS_FIX_LPIB),
	SND_PCI_QUIRK(0x103c, 0x306d, "HP dv3", POS_FIX_LPIB),
	SND_PCI_QUIRK(0x1043, 0x813d, "ASUS P5AD2", POS_FIX_LPIB),
	SND_PCI_QUIRK(0x1043, 0x81b3, "ASUS", POS_FIX_LPIB),
	SND_PCI_QUIRK(0x1043, 0x81e7, "ASUS M2V", POS_FIX_LPIB),
	SND_PCI_QUIRK(0x104d, 0x9069, "Sony VPCS11V9E", POS_FIX_LPIB),
	SND_PCI_QUIRK(0x10de, 0xcb89, "Macbook Pro 7,1", POS_FIX_LPIB),
	SND_PCI_QUIRK(0x1297, 0x3166, "Shuttle", POS_FIX_LPIB),
	SND_PCI_QUIRK(0x1458, 0xa022, "ga-ma770-ud3", POS_FIX_LPIB),
	SND_PCI_QUIRK(0x1462, 0x1002, "MSI Wind U115", POS_FIX_LPIB),
	SND_PCI_QUIRK(0x1565, 0x8218, "Biostar Microtech", POS_FIX_LPIB),
	SND_PCI_QUIRK(0x1849, 0x0888, "775Dual-VSTA", POS_FIX_LPIB),
	SND_PCI_QUIRK(0x8086, 0x2503, "DG965OT AAD63733-203", POS_FIX_LPIB),
	{}
};

static int check_position_fix(struct azx *chip, int fix)
{
	const struct snd_pci_quirk *q;

	switch (fix) {
	case POS_FIX_AUTO:
	case POS_FIX_LPIB:
	case POS_FIX_POSBUF:
	case POS_FIX_VIACOMBO:
	case POS_FIX_COMBO:
		return fix;
	}

	q = snd_pci_quirk_lookup(chip->pci, position_fix_list);
	if (q) {
		dev_info(chip->card->dev,
			 "position_fix set to %d for device %04x:%04x\n",
			 q->value, q->subvendor, q->subdevice);
		return q->value;
	}

	/* Check VIA/ATI HD Audio Controller exist */
	if (chip->driver_caps & AZX_DCAPS_POSFIX_VIA) {
		dev_dbg(chip->card->dev, "Using VIACOMBO position fix\n");
		return POS_FIX_VIACOMBO;
	}
	if (chip->driver_caps & AZX_DCAPS_POSFIX_LPIB) {
		dev_dbg(chip->card->dev, "Using LPIB position fix\n");
		return POS_FIX_LPIB;
	}
	return POS_FIX_AUTO;
}

/*
 * black-lists for probe_mask
 */
static struct snd_pci_quirk probe_mask_list[] = {
	/* Thinkpad often breaks the controller communication when accessing
	 * to the non-working (or non-existing) modem codec slot.
	 */
	SND_PCI_QUIRK(0x1014, 0x05b7, "Thinkpad Z60", 0x01),
	SND_PCI_QUIRK(0x17aa, 0x2010, "Thinkpad X/T/R60", 0x01),
	SND_PCI_QUIRK(0x17aa, 0x20ac, "Thinkpad X/T/R61", 0x01),
	/* broken BIOS */
	SND_PCI_QUIRK(0x1028, 0x20ac, "Dell Studio Desktop", 0x01),
	/* including bogus ALC268 in slot#2 that conflicts with ALC888 */
	SND_PCI_QUIRK(0x17c0, 0x4085, "Medion MD96630", 0x01),
	/* forced codec slots */
	SND_PCI_QUIRK(0x1043, 0x1262, "ASUS W5Fm", 0x103),
	SND_PCI_QUIRK(0x1046, 0x1262, "ASUS W5F", 0x103),
	/* WinFast VP200 H (Teradici) user reported broken communication */
	SND_PCI_QUIRK(0x3a21, 0x040d, "WinFast VP200 H", 0x101),
	{}
};

#define AZX_FORCE_CODEC_MASK	0x100

static void check_probe_mask(struct azx *chip, int dev)
{
	const struct snd_pci_quirk *q;

	chip->codec_probe_mask = probe_mask[dev];
	if (chip->codec_probe_mask == -1) {
		q = snd_pci_quirk_lookup(chip->pci, probe_mask_list);
		if (q) {
			dev_info(chip->card->dev,
				 "probe_mask set to 0x%x for device %04x:%04x\n",
				 q->value, q->subvendor, q->subdevice);
			chip->codec_probe_mask = q->value;
		}
	}

	/* check forced option */
	if (chip->codec_probe_mask != -1 &&
	    (chip->codec_probe_mask & AZX_FORCE_CODEC_MASK)) {
		chip->codec_mask = chip->codec_probe_mask & 0xff;
		dev_info(chip->card->dev, "codec_mask forced to 0x%x\n",
			 chip->codec_mask);
	}
}

/*
 * white/black-list for enable_msi
 */
static struct snd_pci_quirk msi_black_list[] = {
	SND_PCI_QUIRK(0x103c, 0x2191, "HP", 0), /* AMD Hudson */
	SND_PCI_QUIRK(0x103c, 0x2192, "HP", 0), /* AMD Hudson */
	SND_PCI_QUIRK(0x103c, 0x21f7, "HP", 0), /* AMD Hudson */
	SND_PCI_QUIRK(0x103c, 0x21fa, "HP", 0), /* AMD Hudson */
	SND_PCI_QUIRK(0x1043, 0x81f2, "ASUS", 0), /* Athlon64 X2 + nvidia */
	SND_PCI_QUIRK(0x1043, 0x81f6, "ASUS", 0), /* nvidia */
	SND_PCI_QUIRK(0x1043, 0x822d, "ASUS", 0), /* Athlon64 X2 + nvidia MCP55 */
	SND_PCI_QUIRK(0x1179, 0xfb44, "Toshiba Satellite C870", 0), /* AMD Hudson */
	SND_PCI_QUIRK(0x1849, 0x0888, "ASRock", 0), /* Athlon64 X2 + nvidia */
	SND_PCI_QUIRK(0xa0a0, 0x0575, "Aopen MZ915-M", 0), /* ICH6 */
	{}
};

static void check_msi(struct azx *chip)
{
	const struct snd_pci_quirk *q;

	if (enable_msi >= 0) {
		chip->msi = !!enable_msi;
		return;
	}
	chip->msi = 1;	/* enable MSI as default */
	q = snd_pci_quirk_lookup(chip->pci, msi_black_list);
	if (q) {
		dev_info(chip->card->dev,
			 "msi for device %04x:%04x set to %d\n",
			 q->subvendor, q->subdevice, q->value);
		chip->msi = q->value;
		return;
	}

	/* NVidia chipsets seem to cause troubles with MSI */
	if (chip->driver_caps & AZX_DCAPS_NO_MSI) {
		dev_info(chip->card->dev, "Disabling MSI\n");
		chip->msi = 0;
	}
}

/* check the snoop mode availability */
static void azx_check_snoop_available(struct azx *chip)
{
	bool snoop = chip->snoop;

	switch (chip->driver_type) {
	case AZX_DRIVER_VIA:
		/* force to non-snoop mode for a new VIA controller
		 * when BIOS is set
		 */
		if (snoop) {
			u8 val;
			pci_read_config_byte(chip->pci, 0x42, &val);
			if (!(val & 0x80) && chip->pci->revision == 0x30)
				snoop = false;
		}
		break;
	case AZX_DRIVER_ATIHDMI_NS:
		/* new ATI HDMI requires non-snoop */
		snoop = false;
		break;
	case AZX_DRIVER_CTHDA:
		snoop = false;
		break;
	}

	if (snoop != chip->snoop) {
		dev_info(chip->card->dev, "Force to %s mode\n",
			 snoop ? "snoop" : "non-snoop");
		chip->snoop = snoop;
	}
}

static void azx_probe_work(struct work_struct *work)
{
	azx_probe_continue(container_of(work, struct azx, probe_work));
}

/*
 * constructor
 */
static int azx_create(struct snd_card *card, struct pci_dev *pci,
		      int dev, unsigned int driver_caps,
		      const struct hda_controller_ops *hda_ops,
		      struct azx **rchip)
{
	static struct snd_device_ops ops = {
		.dev_free = azx_dev_free,
	};
	struct azx *chip;
	int err;

	*rchip = NULL;

	err = pci_enable_device(pci);
	if (err < 0)
		return err;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(card->dev, "Cannot allocate chip\n");
		pci_disable_device(pci);
		return -ENOMEM;
	}

	spin_lock_init(&chip->reg_lock);
	mutex_init(&chip->open_mutex);
	chip->card = card;
	chip->pci = pci;
	chip->ops = hda_ops;
	chip->irq = -1;
	chip->driver_caps = driver_caps;
	chip->driver_type = driver_caps & 0xff;
	check_msi(chip);
	chip->dev_index = dev;
	chip->jackpoll_ms = jackpoll_ms;
	INIT_WORK(&chip->irq_pending_work, azx_irq_pending_work);
	INIT_LIST_HEAD(&chip->pcm_list);
	INIT_LIST_HEAD(&chip->list);
	init_vga_switcheroo(chip);
	init_completion(&chip->probe_wait);

	chip->position_fix[0] = chip->position_fix[1] =
		check_position_fix(chip, position_fix[dev]);
	/* combo mode uses LPIB for playback */
	if (chip->position_fix[0] == POS_FIX_COMBO) {
		chip->position_fix[0] = POS_FIX_LPIB;
		chip->position_fix[1] = POS_FIX_AUTO;
	}

	check_probe_mask(chip, dev);

	chip->single_cmd = single_cmd;
	chip->snoop = hda_snoop;
	azx_check_snoop_available(chip);

	if (bdl_pos_adj[dev] < 0) {
		switch (chip->driver_type) {
		case AZX_DRIVER_ICH:
		case AZX_DRIVER_PCH:
			bdl_pos_adj[dev] = 1;
			break;
		default:
			bdl_pos_adj[dev] = 32;
			break;
		}
	}
	chip->bdl_pos_adj = bdl_pos_adj;

	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
	if (err < 0) {
		dev_err(card->dev, "Error creating device [card]!\n");
		azx_free(chip);
		return err;
	}

	/* continue probing in work context as may trigger request module */
	INIT_WORK(&chip->probe_work, azx_probe_work);

	*rchip = chip;

	return 0;
}

static int azx_first_init(struct azx *chip)
{
	int dev = chip->dev_index;
	struct pci_dev *pci = chip->pci;
	struct snd_card *card = chip->card;
	int err;
	unsigned short gcap;

#if BITS_PER_LONG != 64
	/* Fix up base address on ULI M5461 */
	if (chip->driver_type == AZX_DRIVER_ULI) {
		u16 tmp3;
		pci_read_config_word(pci, 0x40, &tmp3);
		pci_write_config_word(pci, 0x40, tmp3 | 0x10);
		pci_write_config_dword(pci, PCI_BASE_ADDRESS_1, 0);
	}
#endif

	err = pci_request_regions(pci, "ICH HD audio");
	if (err < 0)
		return err;
	chip->region_requested = 1;

	chip->addr = pci_resource_start(pci, 0);
	chip->remap_addr = pci_ioremap_bar(pci, 0);
	if (chip->remap_addr == NULL) {
		dev_err(card->dev, "ioremap error\n");
		return -ENXIO;
	}

	if (chip->msi)
		if (pci_enable_msi(pci) < 0)
			chip->msi = 0;

	if (azx_acquire_irq(chip, 0) < 0)
		return -EBUSY;

	pci_set_master(pci);
	synchronize_irq(chip->irq);

	gcap = azx_readw(chip, GCAP);
	dev_dbg(card->dev, "chipset global capabilities = 0x%x\n", gcap);

	/* disable SB600 64bit support for safety */
	if (chip->pci->vendor == PCI_VENDOR_ID_ATI) {
		struct pci_dev *p_smbus;
		p_smbus = pci_get_device(PCI_VENDOR_ID_ATI,
					 PCI_DEVICE_ID_ATI_SBX00_SMBUS,
					 NULL);
		if (p_smbus) {
			if (p_smbus->revision < 0x30)
				gcap &= ~ICH6_GCAP_64OK;
			pci_dev_put(p_smbus);
		}
	}

	/* disable 64bit DMA address on some devices */
	if (chip->driver_caps & AZX_DCAPS_NO_64BIT) {
		dev_dbg(card->dev, "Disabling 64bit DMA\n");
		gcap &= ~ICH6_GCAP_64OK;
	}

	/* disable buffer size rounding to 128-byte multiples if supported */
	if (align_buffer_size >= 0)
		chip->align_buffer_size = !!align_buffer_size;
	else {
		if (chip->driver_caps & AZX_DCAPS_BUFSIZE)
			chip->align_buffer_size = 0;
		else if (chip->driver_caps & AZX_DCAPS_ALIGN_BUFSIZE)
			chip->align_buffer_size = 1;
		else
			chip->align_buffer_size = 1;
	}

	/* allow 64bit DMA address if supported by H/W */
	if ((gcap & ICH6_GCAP_64OK) && !pci_set_dma_mask(pci, DMA_BIT_MASK(64)))
		pci_set_consistent_dma_mask(pci, DMA_BIT_MASK(64));
	else {
		pci_set_dma_mask(pci, DMA_BIT_MASK(32));
		pci_set_consistent_dma_mask(pci, DMA_BIT_MASK(32));
	}

	/* read number of streams from GCAP register instead of using
	 * hardcoded value
	 */
	chip->capture_streams = (gcap >> 8) & 0x0f;
	chip->playback_streams = (gcap >> 12) & 0x0f;
	if (!chip->playback_streams && !chip->capture_streams) {
		/* gcap didn't give any info, switching to old method */

		switch (chip->driver_type) {
		case AZX_DRIVER_ULI:
			chip->playback_streams = ULI_NUM_PLAYBACK;
			chip->capture_streams = ULI_NUM_CAPTURE;
			break;
		case AZX_DRIVER_ATIHDMI:
		case AZX_DRIVER_ATIHDMI_NS:
			chip->playback_streams = ATIHDMI_NUM_PLAYBACK;
			chip->capture_streams = ATIHDMI_NUM_CAPTURE;
			break;
		case AZX_DRIVER_GENERIC:
		default:
			chip->playback_streams = ICH6_NUM_PLAYBACK;
			chip->capture_streams = ICH6_NUM_CAPTURE;
			break;
		}
	}
	chip->capture_index_offset = 0;
	chip->playback_index_offset = chip->capture_streams;
	chip->num_streams = chip->playback_streams + chip->capture_streams;
	chip->azx_dev = kcalloc(chip->num_streams, sizeof(*chip->azx_dev),
				GFP_KERNEL);
	if (!chip->azx_dev) {
		dev_err(card->dev, "cannot malloc azx_dev\n");
		return -ENOMEM;
	}

	err = azx_alloc_stream_pages(chip);
	if (err < 0)
		return err;

	/* initialize streams */
	azx_init_stream(chip);

	/* workaround for Broadwell HDMI: the first stream is broken,
	 * so mask it by keeping it as if opened
	 */
	if (pci->vendor == 0x8086 && pci->device == 0x160c)
		chip->azx_dev[0].opened = 1;

	/* initialize chip */
	azx_init_pci(chip);
	azx_init_chip(chip, (probe_only[dev] & 2) == 0);

	/* codec detection */
	if (!chip->codec_mask) {
		dev_err(card->dev, "no codecs found!\n");
		return -ENODEV;
	}

	strcpy(card->driver, "HDA-Intel");
	strlcpy(card->shortname, driver_short_names[chip->driver_type],
		sizeof(card->shortname));
	snprintf(card->longname, sizeof(card->longname),
		 "%s at 0x%lx irq %i",
		 card->shortname, chip->addr, chip->irq);

	return 0;
}

static void power_down_all_codecs(struct azx *chip)
{
#ifdef CONFIG_PM
	/* The codecs were powered up in snd_hda_codec_new().
	 * Now all initialization done, so turn them down if possible
	 */
	struct hda_codec *codec;
	list_for_each_entry(codec, &chip->bus->codec_list, list) {
		snd_hda_power_down(codec);
	}
#endif
}

#ifdef CONFIG_SND_HDA_PATCH_LOADER
/* callback from request_firmware_nowait() */
static void azx_firmware_cb(const struct firmware *fw, void *context)
{
	struct snd_card *card = context;
	struct azx *chip = card->private_data;
	struct pci_dev *pci = chip->pci;

	if (!fw) {
		dev_err(card->dev, "Cannot load firmware, aborting\n");
		goto error;
	}

	chip->fw = fw;
	if (!chip->disabled) {
		/* continue probing */
		if (azx_probe_continue(chip))
			goto error;
	}
	return; /* OK */

 error:
	snd_card_free(card);
	pci_set_drvdata(pci, NULL);
}
#endif

/*
 * HDA controller ops.
 */

/* PCI register access. */
static void pci_azx_writel(u32 value, u32 __iomem *addr)
{
	writel(value, addr);
}

static u32 pci_azx_readl(u32 __iomem *addr)
{
	return readl(addr);
}

static void pci_azx_writew(u16 value, u16 __iomem *addr)
{
	writew(value, addr);
}

static u16 pci_azx_readw(u16 __iomem *addr)
{
	return readw(addr);
}

static void pci_azx_writeb(u8 value, u8 __iomem *addr)
{
	writeb(value, addr);
}

static u8 pci_azx_readb(u8 __iomem *addr)
{
	return readb(addr);
}

static int disable_msi_reset_irq(struct azx *chip)
{
	int err;

	free_irq(chip->irq, chip);
	chip->irq = -1;
	pci_disable_msi(chip->pci);
	chip->msi = 0;
	err = azx_acquire_irq(chip, 1);
	if (err < 0)
		return err;

	return 0;
}

/* DMA page allocation helpers.  */
static int dma_alloc_pages(struct azx *chip,
			   int type,
			   size_t size,
			   struct snd_dma_buffer *buf)
{
	int err;

	err = snd_dma_alloc_pages(type,
				  chip->card->dev,
				  size, buf);
	if (err < 0)
		return err;
	mark_pages_wc(chip, buf, true);
	return 0;
}

static void dma_free_pages(struct azx *chip, struct snd_dma_buffer *buf)
{
	mark_pages_wc(chip, buf, false);
	snd_dma_free_pages(buf);
}

static int substream_alloc_pages(struct azx *chip,
				 struct snd_pcm_substream *substream,
				 size_t size)
{
	struct azx_dev *azx_dev = get_azx_dev(substream);
	int ret;

	mark_runtime_wc(chip, azx_dev, substream, false);
	azx_dev->bufsize = 0;
	azx_dev->period_bytes = 0;
	azx_dev->format_val = 0;
	ret = snd_pcm_lib_malloc_pages(substream, size);
	if (ret < 0)
		return ret;
	mark_runtime_wc(chip, azx_dev, substream, true);
	return 0;
}

static int substream_free_pages(struct azx *chip,
				struct snd_pcm_substream *substream)
{
	struct azx_dev *azx_dev = get_azx_dev(substream);
	mark_runtime_wc(chip, azx_dev, substream, false);
	return snd_pcm_lib_free_pages(substream);
}

static void pcm_mmap_prepare(struct snd_pcm_substream *substream,
			     struct vm_area_struct *area)
{
#ifdef CONFIG_X86
	struct azx_pcm *apcm = snd_pcm_substream_chip(substream);
	struct azx *chip = apcm->chip;
	if (!azx_snoop(chip))
		area->vm_page_prot = pgprot_writecombine(area->vm_page_prot);
#endif
}

static const struct hda_controller_ops pci_hda_ops = {
	.reg_writel = pci_azx_writel,
	.reg_readl = pci_azx_readl,
	.reg_writew = pci_azx_writew,
	.reg_readw = pci_azx_readw,
	.reg_writeb = pci_azx_writeb,
	.reg_readb = pci_azx_readb,
	.disable_msi_reset_irq = disable_msi_reset_irq,
	.dma_alloc_pages = dma_alloc_pages,
	.dma_free_pages = dma_free_pages,
	.substream_alloc_pages = substream_alloc_pages,
	.substream_free_pages = substream_free_pages,
	.pcm_mmap_prepare = pcm_mmap_prepare,
	.position_check = azx_position_check,
};

static int azx_probe(struct pci_dev *pci,
		     const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct azx *chip;
	bool schedule_probe;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	err = snd_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
			   0, &card);
	if (err < 0) {
		dev_err(&pci->dev, "Error creating card!\n");
		return err;
	}

	err = azx_create(card, pci, dev, pci_id->driver_data,
			 &pci_hda_ops, &chip);
	if (err < 0)
		goto out_free;
	card->private_data = chip;

	pci_set_drvdata(pci, card);

	err = register_vga_switcheroo(chip);
	if (err < 0) {
		dev_err(card->dev, "Error registering VGA-switcheroo client\n");
		goto out_free;
	}

	if (check_hdmi_disabled(pci)) {
		dev_info(card->dev, "VGA controller is disabled\n");
		dev_info(card->dev, "Delaying initialization\n");
		chip->disabled = true;
	}

	schedule_probe = !chip->disabled;

#ifdef CONFIG_SND_HDA_PATCH_LOADER
	if (patch[dev] && *patch[dev]) {
		dev_info(card->dev, "Applying patch firmware '%s'\n",
			 patch[dev]);
		err = request_firmware_nowait(THIS_MODULE, true, patch[dev],
					      &pci->dev, GFP_KERNEL, card,
					      azx_firmware_cb);
		if (err < 0)
			goto out_free;
		schedule_probe = false; /* continued in azx_firmware_cb() */
	}
#endif /* CONFIG_SND_HDA_PATCH_LOADER */

#ifndef CONFIG_SND_HDA_I915
	if (chip->driver_caps & AZX_DCAPS_I915_POWERWELL)
		dev_err(card->dev, "Haswell must build in CONFIG_SND_HDA_I915\n");
#endif

	if (schedule_probe)
		schedule_work(&chip->probe_work);

	dev++;
	if (chip->disabled)
		complete_all(&chip->probe_wait);
	return 0;

out_free:
	snd_card_free(card);
	return err;
}

/* number of codec slots for each chipset: 0 = default slots (i.e. 4) */
static unsigned int azx_max_codecs[AZX_NUM_DRIVERS] = {
	[AZX_DRIVER_NVIDIA] = 8,
	[AZX_DRIVER_TERA] = 1,
};

static int azx_probe_continue(struct azx *chip)
{
	struct pci_dev *pci = chip->pci;
	int dev = chip->dev_index;
	int err;

	/* Request power well for Haswell HDA controller and codec */
	if (chip->driver_caps & AZX_DCAPS_I915_POWERWELL) {
#ifdef CONFIG_SND_HDA_I915
		err = hda_i915_init();
		if (err < 0) {
			dev_err(chip->card->dev,
				"Error request power-well from i915\n");
			goto out_free;
		}
#endif
		hda_display_power(true);
	}

	err = azx_first_init(chip);
	if (err < 0)
		goto out_free;

#ifdef CONFIG_SND_HDA_INPUT_BEEP
	chip->beep_mode = beep_mode[dev];
#endif

	/* create codec instances */
	err = azx_codec_create(chip, model[dev],
			       azx_max_codecs[chip->driver_type],
			       power_save_addr);

	if (err < 0)
		goto out_free;
#ifdef CONFIG_SND_HDA_PATCH_LOADER
	if (chip->fw) {
		err = snd_hda_load_patch(chip->bus, chip->fw->size,
					 chip->fw->data);
		if (err < 0)
			goto out_free;
#ifndef CONFIG_PM
		release_firmware(chip->fw); /* no longer needed */
		chip->fw = NULL;
#endif
	}
#endif
	if ((probe_only[dev] & 1) == 0) {
		err = azx_codec_configure(chip);
		if (err < 0)
			goto out_free;
	}

	/* create PCM streams */
	err = snd_hda_build_pcms(chip->bus);
	if (err < 0)
		goto out_free;

	/* create mixer controls */
	err = azx_mixer_create(chip);
	if (err < 0)
		goto out_free;

	err = snd_card_register(chip->card);
	if (err < 0)
		goto out_free;

	chip->running = 1;
	power_down_all_codecs(chip);
	azx_notifier_register(chip);
	azx_add_card_list(chip);
	if ((chip->driver_caps & AZX_DCAPS_PM_RUNTIME) || chip->use_vga_switcheroo)
		pm_runtime_put_noidle(&pci->dev);

out_free:
	if (err < 0)
		chip->init_failed = 1;
	complete_all(&chip->probe_wait);
	return err;
}

static void azx_remove(struct pci_dev *pci)
{
	struct snd_card *card = pci_get_drvdata(pci);

	if (card)
		snd_card_free(card);
}

/* PCI IDs */
static DEFINE_PCI_DEVICE_TABLE(azx_ids) = {
	/* CPT */
	{ PCI_DEVICE(0x8086, 0x1c20),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_PCH_NOPM },
	/* PBG */
	{ PCI_DEVICE(0x8086, 0x1d20),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_PCH_NOPM },
	/* Panther Point */
	{ PCI_DEVICE(0x8086, 0x1e20),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_PCH },
	/* Lynx Point */
	{ PCI_DEVICE(0x8086, 0x8c20),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_PCH },
	/* 9 Series */
	{ PCI_DEVICE(0x8086, 0x8ca0),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_PCH },
	/* Wellsburg */
	{ PCI_DEVICE(0x8086, 0x8d20),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_PCH },
	{ PCI_DEVICE(0x8086, 0x8d21),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_PCH },
	/* Lynx Point-LP */
	{ PCI_DEVICE(0x8086, 0x9c20),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_PCH },
	/* Lynx Point-LP */
	{ PCI_DEVICE(0x8086, 0x9c21),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_PCH },
	/* Wildcat Point-LP */
	{ PCI_DEVICE(0x8086, 0x9ca0),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_PCH },
	/* Haswell */
	{ PCI_DEVICE(0x8086, 0x0a0c),
	  .driver_data = AZX_DRIVER_HDMI | AZX_DCAPS_INTEL_HASWELL },
	{ PCI_DEVICE(0x8086, 0x0c0c),
	  .driver_data = AZX_DRIVER_HDMI | AZX_DCAPS_INTEL_HASWELL },
	{ PCI_DEVICE(0x8086, 0x0d0c),
	  .driver_data = AZX_DRIVER_HDMI | AZX_DCAPS_INTEL_HASWELL },
	/* Broadwell */
	{ PCI_DEVICE(0x8086, 0x160c),
	  .driver_data = AZX_DRIVER_HDMI | AZX_DCAPS_INTEL_HASWELL },
	/* 5 Series/3400 */
	{ PCI_DEVICE(0x8086, 0x3b56),
	  .driver_data = AZX_DRIVER_SCH | AZX_DCAPS_INTEL_PCH_NOPM },
	/* Poulsbo */
	{ PCI_DEVICE(0x8086, 0x811b),
	  .driver_data = AZX_DRIVER_SCH | AZX_DCAPS_INTEL_PCH_NOPM },
	/* Oaktrail */
	{ PCI_DEVICE(0x8086, 0x080a),
	  .driver_data = AZX_DRIVER_SCH | AZX_DCAPS_INTEL_PCH_NOPM },
	/* BayTrail */
	{ PCI_DEVICE(0x8086, 0x0f04),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_PCH_NOPM },
	/* ICH */
	{ PCI_DEVICE(0x8086, 0x2668),
	  .driver_data = AZX_DRIVER_ICH | AZX_DCAPS_OLD_SSYNC |
	  AZX_DCAPS_BUFSIZE },  /* ICH6 */
	{ PCI_DEVICE(0x8086, 0x27d8),
	  .driver_data = AZX_DRIVER_ICH | AZX_DCAPS_OLD_SSYNC |
	  AZX_DCAPS_BUFSIZE },  /* ICH7 */
	{ PCI_DEVICE(0x8086, 0x269a),
	  .driver_data = AZX_DRIVER_ICH | AZX_DCAPS_OLD_SSYNC |
	  AZX_DCAPS_BUFSIZE },  /* ESB2 */
	{ PCI_DEVICE(0x8086, 0x284b),
	  .driver_data = AZX_DRIVER_ICH | AZX_DCAPS_OLD_SSYNC |
	  AZX_DCAPS_BUFSIZE },  /* ICH8 */
	{ PCI_DEVICE(0x8086, 0x293e),
	  .driver_data = AZX_DRIVER_ICH | AZX_DCAPS_OLD_SSYNC |
	  AZX_DCAPS_BUFSIZE },  /* ICH9 */
	{ PCI_DEVICE(0x8086, 0x293f),
	  .driver_data = AZX_DRIVER_ICH | AZX_DCAPS_OLD_SSYNC |
	  AZX_DCAPS_BUFSIZE },  /* ICH9 */
	{ PCI_DEVICE(0x8086, 0x3a3e),
	  .driver_data = AZX_DRIVER_ICH | AZX_DCAPS_OLD_SSYNC |
	  AZX_DCAPS_BUFSIZE },  /* ICH10 */
	{ PCI_DEVICE(0x8086, 0x3a6e),
	  .driver_data = AZX_DRIVER_ICH | AZX_DCAPS_OLD_SSYNC |
	  AZX_DCAPS_BUFSIZE },  /* ICH10 */
	/* Generic Intel */
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_ANY_ID),
	  .class = PCI_CLASS_MULTIMEDIA_HD_AUDIO << 8,
	  .class_mask = 0xffffff,
	  .driver_data = AZX_DRIVER_ICH | AZX_DCAPS_BUFSIZE },
	/* ATI SB 450/600/700/800/900 */
	{ PCI_DEVICE(0x1002, 0x437b),
	  .driver_data = AZX_DRIVER_ATI | AZX_DCAPS_PRESET_ATI_SB },
	{ PCI_DEVICE(0x1002, 0x4383),
	  .driver_data = AZX_DRIVER_ATI | AZX_DCAPS_PRESET_ATI_SB },
	/* AMD Hudson */
	{ PCI_DEVICE(0x1022, 0x780d),
	  .driver_data = AZX_DRIVER_GENERIC | AZX_DCAPS_PRESET_ATI_SB },
	/* ATI HDMI */
	{ PCI_DEVICE(0x1002, 0x793b),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0x7919),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0x960f),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0x970f),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaa00),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaa08),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaa10),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaa18),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaa20),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaa28),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaa30),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaa38),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaa40),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaa48),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaa50),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaa58),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaa60),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaa68),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaa80),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaa88),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaa90),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaa98),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0x9902),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaaa0),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaaa8),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0xaab0),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI },
	/* VIA VT8251/VT8237A */
	{ PCI_DEVICE(0x1106, 0x3288),
	  .driver_data = AZX_DRIVER_VIA | AZX_DCAPS_POSFIX_VIA },
	/* VIA GFX VT7122/VX900 */
	{ PCI_DEVICE(0x1106, 0x9170), .driver_data = AZX_DRIVER_GENERIC },
	/* VIA GFX VT6122/VX11 */
	{ PCI_DEVICE(0x1106, 0x9140), .driver_data = AZX_DRIVER_GENERIC },
	/* SIS966 */
	{ PCI_DEVICE(0x1039, 0x7502), .driver_data = AZX_DRIVER_SIS },
	/* ULI M5461 */
	{ PCI_DEVICE(0x10b9, 0x5461), .driver_data = AZX_DRIVER_ULI },
	/* NVIDIA MCP */
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID),
	  .class = PCI_CLASS_MULTIMEDIA_HD_AUDIO << 8,
	  .class_mask = 0xffffff,
	  .driver_data = AZX_DRIVER_NVIDIA | AZX_DCAPS_PRESET_NVIDIA },
	/* Teradici */
	{ PCI_DEVICE(0x6549, 0x1200),
	  .driver_data = AZX_DRIVER_TERA | AZX_DCAPS_NO_64BIT },
	{ PCI_DEVICE(0x6549, 0x2200),
	  .driver_data = AZX_DRIVER_TERA | AZX_DCAPS_NO_64BIT },
	/* Creative X-Fi (CA0110-IBG) */
	/* CTHDA chips */
	{ PCI_DEVICE(0x1102, 0x0010),
	  .driver_data = AZX_DRIVER_CTHDA | AZX_DCAPS_PRESET_CTHDA },
	{ PCI_DEVICE(0x1102, 0x0012),
	  .driver_data = AZX_DRIVER_CTHDA | AZX_DCAPS_PRESET_CTHDA },
#if !IS_ENABLED(CONFIG_SND_CTXFI)
	/* the following entry conflicts with snd-ctxfi driver,
	 * as ctxfi driver mutates from HD-audio to native mode with
	 * a special command sequence.
	 */
	{ PCI_DEVICE(PCI_VENDOR_ID_CREATIVE, PCI_ANY_ID),
	  .class = PCI_CLASS_MULTIMEDIA_HD_AUDIO << 8,
	  .class_mask = 0xffffff,
	  .driver_data = AZX_DRIVER_CTX | AZX_DCAPS_CTX_WORKAROUND |
	  AZX_DCAPS_RIRB_PRE_DELAY | AZX_DCAPS_POSFIX_LPIB },
#else
	/* this entry seems still valid -- i.e. without emu20kx chip */
	{ PCI_DEVICE(0x1102, 0x0009),
	  .driver_data = AZX_DRIVER_CTX | AZX_DCAPS_CTX_WORKAROUND |
	  AZX_DCAPS_RIRB_PRE_DELAY | AZX_DCAPS_POSFIX_LPIB },
#endif
	/* Vortex86MX */
	{ PCI_DEVICE(0x17f3, 0x3010), .driver_data = AZX_DRIVER_GENERIC },
	/* VMware HDAudio */
	{ PCI_DEVICE(0x15ad, 0x1977), .driver_data = AZX_DRIVER_GENERIC },
	/* AMD/ATI Generic, PCI class code and Vendor ID for HD Audio */
	{ PCI_DEVICE(PCI_VENDOR_ID_ATI, PCI_ANY_ID),
	  .class = PCI_CLASS_MULTIMEDIA_HD_AUDIO << 8,
	  .class_mask = 0xffffff,
	  .driver_data = AZX_DRIVER_GENERIC | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_ANY_ID),
	  .class = PCI_CLASS_MULTIMEDIA_HD_AUDIO << 8,
	  .class_mask = 0xffffff,
	  .driver_data = AZX_DRIVER_GENERIC | AZX_DCAPS_PRESET_ATI_HDMI },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, azx_ids);

/* pci_driver definition */
static struct pci_driver azx_driver = {
	.name = KBUILD_MODNAME,
	.id_table = azx_ids,
	.probe = azx_probe,
	.remove = azx_remove,
	.driver = {
		.pm = AZX_PM_OPS,
	},
};

module_pci_driver(azx_driver);
