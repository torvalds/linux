// SPDX-License-Identifier: GPL-2.0-or-later
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
 *  CONTACTS:
 *
 *  Matt Jared		matt.jared@intel.com
 *  Andy Kopp		andy.kopp@intel.com
 *  Dan Kogan		dan.d.kogan@intel.com
 *
 *  CHANGES:
 *
 *  2004.12.01	Major rewrite by tiwai, merged the work of pshou
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
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/clocksource.h>
#include <linux/time.h>
#include <linux/completion.h>
#include <linux/acpi.h>
#include <linux/pgtable.h>

#ifdef CONFIG_X86
/* for snoop control */
#include <asm/set_memory.h>
#include <asm/cpufeature.h>
#endif
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/hdaudio.h>
#include <sound/hda_i915.h>
#include <sound/intel-dsp-config.h>
#include <linux/vgaarb.h>
#include <linux/vga_switcheroo.h>
#include <linux/firmware.h>
#include <sound/hda_codec.h>
#include "hda_controller.h"
#include "hda_intel.h"

#define CREATE_TRACE_POINTS
#include "hda_intel_trace.h"

/* position fix mode */
enum {
	POS_FIX_AUTO,
	POS_FIX_LPIB,
	POS_FIX_POSBUF,
	POS_FIX_VIACOMBO,
	POS_FIX_COMBO,
	POS_FIX_SKL,
	POS_FIX_FIFO,
};

/* Defines for ATI HD Audio support in SB450 south bridge */
#define ATI_SB450_HDAUDIO_MISC_CNTR2_ADDR   0x42
#define ATI_SB450_HDAUDIO_ENABLE_SNOOP      0x02

/* Defines for Nvidia HDA support */
#define NVIDIA_HDA_TRANSREG_ADDR      0x4e
#define NVIDIA_HDA_ENABLE_COHBITS     0x0f
#define NVIDIA_HDA_ISTRM_COH          0x4d
#define NVIDIA_HDA_OSTRM_COH          0x4c
#define NVIDIA_HDA_ENABLE_COHBIT      0x01

/* Defines for Intel SCH HDA snoop control */
#define INTEL_HDA_CGCTL	 0x48
#define INTEL_HDA_CGCTL_MISCBDCGE        (0x1 << 6)
#define INTEL_SCH_HDA_DEVC      0x78
#define INTEL_SCH_HDA_DEVC_NOSNOOP       (0x1<<11)

/* Define VIA HD Audio Device ID*/
#define VIA_HDAC_DEVICE_ID		0x3288

/* max number of SDs */
/* ICH, ATI and VIA have 4 playback and 4 capture */
#define ICH6_NUM_CAPTURE	4
#define ICH6_NUM_PLAYBACK	4

/* ULI has 6 playback and 5 capture */
#define ULI_NUM_CAPTURE		5
#define ULI_NUM_PLAYBACK	6

/* ATI HDMI may have up to 8 playbacks and 0 capture */
#define ATIHDMI_NUM_CAPTURE	0
#define ATIHDMI_NUM_PLAYBACK	8

/* TERA has 4 playback and 3 capture */
#define TERA_NUM_CAPTURE	3
#define TERA_NUM_PLAYBACK	4


static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;
static char *model[SNDRV_CARDS];
static int position_fix[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] = -1};
static int bdl_pos_adj[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] = -1};
static int probe_mask[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] = -1};
static int probe_only[SNDRV_CARDS];
static int jackpoll_ms[SNDRV_CARDS];
static int single_cmd = -1;
static int enable_msi = -1;
#ifdef CONFIG_SND_HDA_PATCH_LOADER
static char *patch[SNDRV_CARDS];
#endif
#ifdef CONFIG_SND_HDA_INPUT_BEEP
static bool beep_mode[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] =
					CONFIG_SND_HDA_INPUT_BEEP_MODE};
#endif
static bool dmic_detect = 1;

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
		 "(-1 = system default, 0 = auto, 1 = LPIB, 2 = POSBUF, 3 = VIACOMBO, 4 = COMBO, 5 = SKL+, 6 = FIFO).");
module_param_array(bdl_pos_adj, int, NULL, 0644);
MODULE_PARM_DESC(bdl_pos_adj, "BDL position adjustment offset.");
module_param_array(probe_mask, int, NULL, 0444);
MODULE_PARM_DESC(probe_mask, "Bitmask to probe codecs (default = -1).");
module_param_array(probe_only, int, NULL, 0444);
MODULE_PARM_DESC(probe_only, "Only probing and no codec initialization.");
module_param_array(jackpoll_ms, int, NULL, 0444);
MODULE_PARM_DESC(jackpoll_ms, "Ms between polling for jack events (default = 0, using unsol events only)");
module_param(single_cmd, bint, 0444);
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
module_param(dmic_detect, bool, 0444);
MODULE_PARM_DESC(dmic_detect, "Allow DSP driver selection (bypass this driver) "
			     "(0=off, 1=on) (default=1); "
		 "deprecated, use snd-intel-dspcfg.dsp_driver option instead");

#ifdef CONFIG_PM
static int param_set_xint(const char *val, const struct kernel_param *kp);
static const struct kernel_param_ops param_ops_xint = {
	.set = param_set_xint,
	.get = param_get_int,
};
#define param_check_xint param_check_int

static int power_save = CONFIG_SND_HDA_POWER_SAVE_DEFAULT;
module_param(power_save, xint, 0644);
MODULE_PARM_DESC(power_save, "Automatic power-saving timeout "
		 "(in second, 0 = disable).");

static bool pm_blacklist = true;
module_param(pm_blacklist, bool, 0644);
MODULE_PARM_DESC(pm_blacklist, "Enable power-management denylist");

/* reset the HD-audio controller in power save mode.
 * this may give more power-saving, but will take longer time to
 * wake up.
 */
static bool power_save_controller = 1;
module_param(power_save_controller, bool, 0644);
MODULE_PARM_DESC(power_save_controller, "Reset controller in power save mode.");
#else
#define power_save	0
#endif /* CONFIG_PM */

static int align_buffer_size = -1;
module_param(align_buffer_size, bint, 0644);
MODULE_PARM_DESC(align_buffer_size,
		"Force buffer and period sizes to be multiple of 128 bytes.");

#ifdef CONFIG_X86
static int hda_snoop = -1;
module_param_named(snoop, hda_snoop, bint, 0444);
MODULE_PARM_DESC(snoop, "Enable/disable snooping");
#else
#define hda_snoop		true
#endif


MODULE_LICENSE("GPL");
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
	AZX_DRIVER_SKL,
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
	AZX_DRIVER_CMEDIA,
	AZX_DRIVER_ZHAOXIN,
	AZX_DRIVER_GENERIC,
	AZX_NUM_DRIVERS, /* keep this as last entry */
};

#define azx_get_snoop_type(chip) \
	(((chip)->driver_caps & AZX_DCAPS_SNOOP_MASK) >> 10)
#define AZX_DCAPS_SNOOP_TYPE(type) ((AZX_SNOOP_TYPE_ ## type) << 10)

/* quirks for old Intel chipsets */
#define AZX_DCAPS_INTEL_ICH \
	(AZX_DCAPS_OLD_SSYNC | AZX_DCAPS_NO_ALIGN_BUFSIZE)

/* quirks for Intel PCH */
#define AZX_DCAPS_INTEL_PCH_BASE \
	(AZX_DCAPS_NO_ALIGN_BUFSIZE | AZX_DCAPS_COUNT_LPIB_DELAY |\
	 AZX_DCAPS_SNOOP_TYPE(SCH))

/* PCH up to IVB; no runtime PM; bind with i915 gfx */
#define AZX_DCAPS_INTEL_PCH_NOPM \
	(AZX_DCAPS_INTEL_PCH_BASE | AZX_DCAPS_I915_COMPONENT)

/* PCH for HSW/BDW; with runtime PM */
/* no i915 binding for this as HSW/BDW has another controller for HDMI */
#define AZX_DCAPS_INTEL_PCH \
	(AZX_DCAPS_INTEL_PCH_BASE | AZX_DCAPS_PM_RUNTIME)

/* HSW HDMI */
#define AZX_DCAPS_INTEL_HASWELL \
	(/*AZX_DCAPS_ALIGN_BUFSIZE |*/ AZX_DCAPS_COUNT_LPIB_DELAY |\
	 AZX_DCAPS_PM_RUNTIME | AZX_DCAPS_I915_COMPONENT |\
	 AZX_DCAPS_SNOOP_TYPE(SCH))

/* Broadwell HDMI can't use position buffer reliably, force to use LPIB */
#define AZX_DCAPS_INTEL_BROADWELL \
	(/*AZX_DCAPS_ALIGN_BUFSIZE |*/ AZX_DCAPS_POSFIX_LPIB |\
	 AZX_DCAPS_PM_RUNTIME | AZX_DCAPS_I915_COMPONENT |\
	 AZX_DCAPS_SNOOP_TYPE(SCH))

#define AZX_DCAPS_INTEL_BAYTRAIL \
	(AZX_DCAPS_INTEL_PCH_BASE | AZX_DCAPS_I915_COMPONENT)

#define AZX_DCAPS_INTEL_BRASWELL \
	(AZX_DCAPS_INTEL_PCH_BASE | AZX_DCAPS_PM_RUNTIME |\
	 AZX_DCAPS_I915_COMPONENT)

#define AZX_DCAPS_INTEL_SKYLAKE \
	(AZX_DCAPS_INTEL_PCH_BASE | AZX_DCAPS_PM_RUNTIME |\
	 AZX_DCAPS_SEPARATE_STREAM_TAG | AZX_DCAPS_I915_COMPONENT)

#define AZX_DCAPS_INTEL_BROXTON		AZX_DCAPS_INTEL_SKYLAKE

/* quirks for ATI SB / AMD Hudson */
#define AZX_DCAPS_PRESET_ATI_SB \
	(AZX_DCAPS_NO_TCSEL | AZX_DCAPS_POSFIX_LPIB |\
	 AZX_DCAPS_SNOOP_TYPE(ATI))

/* quirks for ATI/AMD HDMI */
#define AZX_DCAPS_PRESET_ATI_HDMI \
	(AZX_DCAPS_NO_TCSEL | AZX_DCAPS_POSFIX_LPIB|\
	 AZX_DCAPS_NO_MSI64)

/* quirks for ATI HDMI with snoop off */
#define AZX_DCAPS_PRESET_ATI_HDMI_NS \
	(AZX_DCAPS_PRESET_ATI_HDMI | AZX_DCAPS_SNOOP_OFF)

/* quirks for AMD SB */
#define AZX_DCAPS_PRESET_AMD_SB \
	(AZX_DCAPS_NO_TCSEL | AZX_DCAPS_AMD_WORKAROUND |\
	 AZX_DCAPS_SNOOP_TYPE(ATI) | AZX_DCAPS_PM_RUNTIME |\
	 AZX_DCAPS_RETRY_PROBE)

/* quirks for Nvidia */
#define AZX_DCAPS_PRESET_NVIDIA \
	(AZX_DCAPS_NO_MSI | AZX_DCAPS_CORBRP_SELF_CLEAR |\
	 AZX_DCAPS_SNOOP_TYPE(NVIDIA))

#define AZX_DCAPS_PRESET_CTHDA \
	(AZX_DCAPS_NO_MSI | AZX_DCAPS_POSFIX_LPIB |\
	 AZX_DCAPS_NO_64BIT |\
	 AZX_DCAPS_4K_BDLE_BOUNDARY | AZX_DCAPS_SNOOP_OFF)

/*
 * vga_switcheroo support
 */
#ifdef SUPPORT_VGA_SWITCHEROO
#define use_vga_switcheroo(chip)	((chip)->use_vga_switcheroo)
#define needs_eld_notify_link(chip)	((chip)->bus.keep_power)
#else
#define use_vga_switcheroo(chip)	0
#define needs_eld_notify_link(chip)	false
#endif

#define CONTROLLER_IN_GPU(pci) (((pci)->device == 0x0a0c) || \
					((pci)->device == 0x0c0c) || \
					((pci)->device == 0x0d0c) || \
					((pci)->device == 0x160c) || \
					((pci)->device == 0x490d) || \
					((pci)->device == 0x4f90) || \
					((pci)->device == 0x4f91) || \
					((pci)->device == 0x4f92))

#define IS_BXT(pci) ((pci)->vendor == 0x8086 && (pci)->device == 0x5a98)

static const char * const driver_short_names[] = {
	[AZX_DRIVER_ICH] = "HDA Intel",
	[AZX_DRIVER_PCH] = "HDA Intel PCH",
	[AZX_DRIVER_SCH] = "HDA Intel MID",
	[AZX_DRIVER_SKL] = "HDA Intel PCH", /* kept old name for compatibility */
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
	[AZX_DRIVER_CMEDIA] = "HDA C-Media",
	[AZX_DRIVER_ZHAOXIN] = "HDA Zhaoxin",
	[AZX_DRIVER_GENERIC] = "HD-Audio Generic",
};

static int azx_acquire_irq(struct azx *chip, int do_disconnect);
static void set_default_power_save(struct azx *chip);

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
	int snoop_type = azx_get_snoop_type(chip);

	/* Clear bits 0-2 of PCI register TCSEL (at offset 0x44)
	 * TCSEL == Traffic Class Select Register, which sets PCI express QOS
	 * Ensuring these bits are 0 clears playback static on some HD Audio
	 * codecs.
	 * The PCI register TCSEL is defined in the Intel manuals.
	 */
	if (!(chip->driver_caps & AZX_DCAPS_NO_TCSEL)) {
		dev_dbg(chip->card->dev, "Clearing TCSEL\n");
		update_pci_byte(chip->pci, AZX_PCIREG_TCSEL, 0x07, 0);
	}

	/* For ATI SB450/600/700/800/900 and AMD Hudson azalia HD audio,
	 * we need to enable snoop.
	 */
	if (snoop_type == AZX_SNOOP_TYPE_ATI) {
		dev_dbg(chip->card->dev, "Setting ATI snoop: %d\n",
			azx_snoop(chip));
		update_pci_byte(chip->pci,
				ATI_SB450_HDAUDIO_MISC_CNTR2_ADDR, 0x07,
				azx_snoop(chip) ? ATI_SB450_HDAUDIO_ENABLE_SNOOP : 0);
	}

	/* For NVIDIA HDA, enable snoop */
	if (snoop_type == AZX_SNOOP_TYPE_NVIDIA) {
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
	if (snoop_type == AZX_SNOOP_TYPE_SCH) {
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

/*
 * In BXT-P A0, HD-Audio DMA requests is later than expected,
 * and makes an audio stream sensitive to system latencies when
 * 24/32 bits are playing.
 * Adjusting threshold of DMA fifo to force the DMA request
 * sooner to improve latency tolerance at the expense of power.
 */
static void bxt_reduce_dma_latency(struct azx *chip)
{
	u32 val;

	val = azx_readl(chip, VS_EM4L);
	val &= (0x3 << 20);
	azx_writel(chip, VS_EM4L, val);
}

/*
 * ML_LCAP bits:
 *  bit 0: 6 MHz Supported
 *  bit 1: 12 MHz Supported
 *  bit 2: 24 MHz Supported
 *  bit 3: 48 MHz Supported
 *  bit 4: 96 MHz Supported
 *  bit 5: 192 MHz Supported
 */
static int intel_get_lctl_scf(struct azx *chip)
{
	struct hdac_bus *bus = azx_bus(chip);
	static const int preferred_bits[] = { 2, 3, 1, 4, 5 };
	u32 val, t;
	int i;

	val = readl(bus->mlcap + AZX_ML_BASE + AZX_REG_ML_LCAP);

	for (i = 0; i < ARRAY_SIZE(preferred_bits); i++) {
		t = preferred_bits[i];
		if (val & (1 << t))
			return t;
	}

	dev_warn(chip->card->dev, "set audio clock frequency to 6MHz");
	return 0;
}

static int intel_ml_lctl_set_power(struct azx *chip, int state)
{
	struct hdac_bus *bus = azx_bus(chip);
	u32 val;
	int timeout;

	/*
	 * the codecs are sharing the first link setting by default
	 * If other links are enabled for stream, they need similar fix
	 */
	val = readl(bus->mlcap + AZX_ML_BASE + AZX_REG_ML_LCTL);
	val &= ~AZX_MLCTL_SPA;
	val |= state << AZX_MLCTL_SPA_SHIFT;
	writel(val, bus->mlcap + AZX_ML_BASE + AZX_REG_ML_LCTL);
	/* wait for CPA */
	timeout = 50;
	while (timeout) {
		if (((readl(bus->mlcap + AZX_ML_BASE + AZX_REG_ML_LCTL)) &
		    AZX_MLCTL_CPA) == (state << AZX_MLCTL_CPA_SHIFT))
			return 0;
		timeout--;
		udelay(10);
	}

	return -1;
}

static void intel_init_lctl(struct azx *chip)
{
	struct hdac_bus *bus = azx_bus(chip);
	u32 val;
	int ret;

	/* 0. check lctl register value is correct or not */
	val = readl(bus->mlcap + AZX_ML_BASE + AZX_REG_ML_LCTL);
	/* if SCF is already set, let's use it */
	if ((val & ML_LCTL_SCF_MASK) != 0)
		return;

	/*
	 * Before operating on SPA, CPA must match SPA.
	 * Any deviation may result in undefined behavior.
	 */
	if (((val & AZX_MLCTL_SPA) >> AZX_MLCTL_SPA_SHIFT) !=
		((val & AZX_MLCTL_CPA) >> AZX_MLCTL_CPA_SHIFT))
		return;

	/* 1. turn link down: set SPA to 0 and wait CPA to 0 */
	ret = intel_ml_lctl_set_power(chip, 0);
	udelay(100);
	if (ret)
		goto set_spa;

	/* 2. update SCF to select a properly audio clock*/
	val &= ~ML_LCTL_SCF_MASK;
	val |= intel_get_lctl_scf(chip);
	writel(val, bus->mlcap + AZX_ML_BASE + AZX_REG_ML_LCTL);

set_spa:
	/* 4. turn link up: set SPA to 1 and wait CPA to 1 */
	intel_ml_lctl_set_power(chip, 1);
	udelay(100);
}

static void hda_intel_init_chip(struct azx *chip, bool full_reset)
{
	struct hdac_bus *bus = azx_bus(chip);
	struct pci_dev *pci = chip->pci;
	u32 val;

	snd_hdac_set_codec_wakeup(bus, true);
	if (chip->driver_type == AZX_DRIVER_SKL) {
		pci_read_config_dword(pci, INTEL_HDA_CGCTL, &val);
		val = val & ~INTEL_HDA_CGCTL_MISCBDCGE;
		pci_write_config_dword(pci, INTEL_HDA_CGCTL, val);
	}
	azx_init_chip(chip, full_reset);
	if (chip->driver_type == AZX_DRIVER_SKL) {
		pci_read_config_dword(pci, INTEL_HDA_CGCTL, &val);
		val = val | INTEL_HDA_CGCTL_MISCBDCGE;
		pci_write_config_dword(pci, INTEL_HDA_CGCTL, val);
	}

	snd_hdac_set_codec_wakeup(bus, false);

	/* reduce dma latency to avoid noise */
	if (IS_BXT(pci))
		bxt_reduce_dma_latency(chip);

	if (bus->mlcap != NULL)
		intel_init_lctl(chip);
}

/* calculate runtime delay from LPIB */
static int azx_get_delay_from_lpib(struct azx *chip, struct azx_dev *azx_dev,
				   unsigned int pos)
{
	struct snd_pcm_substream *substream = azx_dev->core.substream;
	int stream = substream->stream;
	unsigned int lpib_pos = azx_get_pos_lpib(chip, azx_dev);
	int delay;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		delay = pos - lpib_pos;
	else
		delay = lpib_pos - pos;
	if (delay < 0) {
		if (delay >= azx_dev->core.delay_negative_threshold)
			delay = 0;
		else
			delay += azx_dev->core.bufsize;
	}

	if (delay >= azx_dev->core.period_bytes) {
		dev_info(chip->card->dev,
			 "Unstable LPIB (%d >= %d); disabling LPIB delay counting\n",
			 delay, azx_dev->core.period_bytes);
		delay = 0;
		chip->driver_caps &= ~AZX_DCAPS_COUNT_LPIB_DELAY;
		chip->get_delay[stream] = NULL;
	}

	return bytes_to_frames(substream->runtime, delay);
}

static int azx_position_ok(struct azx *chip, struct azx_dev *azx_dev);

/* called from IRQ */
static int azx_position_check(struct azx *chip, struct azx_dev *azx_dev)
{
	struct hda_intel *hda = container_of(chip, struct hda_intel, chip);
	int ok;

	ok = azx_position_ok(chip, azx_dev);
	if (ok == 1) {
		azx_dev->irq_pending = 0;
		return ok;
	} else if (ok == 0) {
		/* bogus IRQ, process it later */
		azx_dev->irq_pending = 1;
		schedule_work(&hda->irq_pending_work);
	}
	return 0;
}

#define display_power(chip, enable) \
	snd_hdac_display_power(azx_bus(chip), HDA_CODEC_IDX_CONTROLLER, enable)

/*
 * Check whether the current DMA position is acceptable for updating
 * periods.  Returns non-zero if it's OK.
 *
 * Many HD-audio controllers appear pretty inaccurate about
 * the update-IRQ timing.  The IRQ is issued before actually the
 * data is processed.  So, we need to process it afterwords in a
 * workqueue.
 *
 * Returns 1 if OK to proceed, 0 for delay handling, -1 for skipping update
 */
static int azx_position_ok(struct azx *chip, struct azx_dev *azx_dev)
{
	struct snd_pcm_substream *substream = azx_dev->core.substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int stream = substream->stream;
	u32 wallclk;
	unsigned int pos;
	snd_pcm_uframes_t hwptr, target;

	wallclk = azx_readl(chip, WALLCLK) - azx_dev->core.start_wallclk;
	if (wallclk < (azx_dev->core.period_wallclk * 2) / 3)
		return -1;	/* bogus (too early) interrupt */

	if (chip->get_position[stream])
		pos = chip->get_position[stream](chip, azx_dev);
	else { /* use the position buffer as default */
		pos = azx_get_pos_posbuf(chip, azx_dev);
		if (!pos || pos == (u32)-1) {
			dev_info(chip->card->dev,
				 "Invalid position buffer, using LPIB read method instead.\n");
			chip->get_position[stream] = azx_get_pos_lpib;
			if (chip->get_position[0] == azx_get_pos_lpib &&
			    chip->get_position[1] == azx_get_pos_lpib)
				azx_bus(chip)->use_posbuf = false;
			pos = azx_get_pos_lpib(chip, azx_dev);
			chip->get_delay[stream] = NULL;
		} else {
			chip->get_position[stream] = azx_get_pos_posbuf;
			if (chip->driver_caps & AZX_DCAPS_COUNT_LPIB_DELAY)
				chip->get_delay[stream] = azx_get_delay_from_lpib;
		}
	}

	if (pos >= azx_dev->core.bufsize)
		pos = 0;

	if (WARN_ONCE(!azx_dev->core.period_bytes,
		      "hda-intel: zero azx_dev->period_bytes"))
		return -1; /* this shouldn't happen! */
	if (wallclk < (azx_dev->core.period_wallclk * 5) / 4 &&
	    pos % azx_dev->core.period_bytes > azx_dev->core.period_bytes / 2)
		/* NG - it's below the first next period boundary */
		return chip->bdl_pos_adj ? 0 : -1;
	azx_dev->core.start_wallclk += wallclk;

	if (azx_dev->core.no_period_wakeup)
		return 1; /* OK, no need to check period boundary */

	if (runtime->hw_ptr_base != runtime->hw_ptr_interrupt)
		return 1; /* OK, already in hwptr updating process */

	/* check whether the period gets really elapsed */
	pos = bytes_to_frames(runtime, pos);
	hwptr = runtime->hw_ptr_base + pos;
	if (hwptr < runtime->status->hw_ptr)
		hwptr += runtime->buffer_size;
	target = runtime->hw_ptr_interrupt + runtime->period_size;
	if (hwptr < target) {
		/* too early wakeup, process it later */
		return chip->bdl_pos_adj ? 0 : -1;
	}

	return 1; /* OK, it's fine */
}

/*
 * The work for pending PCM period updates.
 */
static void azx_irq_pending_work(struct work_struct *work)
{
	struct hda_intel *hda = container_of(work, struct hda_intel, irq_pending_work);
	struct azx *chip = &hda->chip;
	struct hdac_bus *bus = azx_bus(chip);
	struct hdac_stream *s;
	int pending, ok;

	if (!hda->irq_pending_warned) {
		dev_info(chip->card->dev,
			 "IRQ timing workaround is activated for card #%d. Suggest a bigger bdl_pos_adj.\n",
			 chip->card->number);
		hda->irq_pending_warned = 1;
	}

	for (;;) {
		pending = 0;
		spin_lock_irq(&bus->reg_lock);
		list_for_each_entry(s, &bus->stream_list, list) {
			struct azx_dev *azx_dev = stream_to_azx_dev(s);
			if (!azx_dev->irq_pending ||
			    !s->substream ||
			    !s->running)
				continue;
			ok = azx_position_ok(chip, azx_dev);
			if (ok > 0) {
				azx_dev->irq_pending = 0;
				spin_unlock(&bus->reg_lock);
				snd_pcm_period_elapsed(s->substream);
				spin_lock(&bus->reg_lock);
			} else if (ok < 0) {
				pending = 0;	/* too early */
			} else
				pending++;
		}
		spin_unlock_irq(&bus->reg_lock);
		if (!pending)
			return;
		msleep(1);
	}
}

/* clear irq_pending flags and assure no on-going workq */
static void azx_clear_irq_pending(struct azx *chip)
{
	struct hdac_bus *bus = azx_bus(chip);
	struct hdac_stream *s;

	spin_lock_irq(&bus->reg_lock);
	list_for_each_entry(s, &bus->stream_list, list) {
		struct azx_dev *azx_dev = stream_to_azx_dev(s);
		azx_dev->irq_pending = 0;
	}
	spin_unlock_irq(&bus->reg_lock);
}

static int azx_acquire_irq(struct azx *chip, int do_disconnect)
{
	struct hdac_bus *bus = azx_bus(chip);

	if (request_irq(chip->pci->irq, azx_interrupt,
			chip->msi ? 0 : IRQF_SHARED,
			chip->card->irq_descr, chip)) {
		dev_err(chip->card->dev,
			"unable to grab IRQ %d, disabling device\n",
			chip->pci->irq);
		if (do_disconnect)
			snd_card_disconnect(chip->card);
		return -1;
	}
	bus->irq = chip->pci->irq;
	chip->card->sync_irq = bus->irq;
	pci_intx(chip->pci, !chip->msi);
	return 0;
}

/* get the current DMA position with correction on VIA chips */
static unsigned int azx_via_get_position(struct azx *chip,
					 struct azx_dev *azx_dev)
{
	unsigned int link_pos, mini_pos, bound_pos;
	unsigned int mod_link_pos, mod_dma_pos, mod_mini_pos;
	unsigned int fifo_size;

	link_pos = snd_hdac_stream_get_pos_lpib(azx_stream(azx_dev));
	if (azx_dev->core.substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		/* Playback, no problem using link position */
		return link_pos;
	}

	/* Capture */
	/* For new chipset,
	 * use mod to get the DMA position just like old chipset
	 */
	mod_dma_pos = le32_to_cpu(*azx_dev->core.posbuf);
	mod_dma_pos %= azx_dev->core.period_bytes;

	fifo_size = azx_stream(azx_dev)->fifo_size - 1;

	if (azx_dev->insufficient) {
		/* Link position never gather than FIFO size */
		if (link_pos <= fifo_size)
			return 0;

		azx_dev->insufficient = 0;
	}

	if (link_pos <= fifo_size)
		mini_pos = azx_dev->core.bufsize + link_pos - fifo_size;
	else
		mini_pos = link_pos - fifo_size;

	/* Find nearest previous boudary */
	mod_mini_pos = mini_pos % azx_dev->core.period_bytes;
	mod_link_pos = link_pos % azx_dev->core.period_bytes;
	if (mod_link_pos >= fifo_size)
		bound_pos = link_pos - mod_link_pos;
	else if (mod_dma_pos >= mod_mini_pos)
		bound_pos = mini_pos - mod_mini_pos;
	else {
		bound_pos = mini_pos - mod_mini_pos + azx_dev->core.period_bytes;
		if (bound_pos >= azx_dev->core.bufsize)
			bound_pos = 0;
	}

	/* Calculate real DMA position we want */
	return bound_pos + mod_dma_pos;
}

#define AMD_FIFO_SIZE	32

/* get the current DMA position with FIFO size correction */
static unsigned int azx_get_pos_fifo(struct azx *chip, struct azx_dev *azx_dev)
{
	struct snd_pcm_substream *substream = azx_dev->core.substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int pos, delay;

	pos = snd_hdac_stream_get_pos_lpib(azx_stream(azx_dev));
	if (!runtime)
		return pos;

	runtime->delay = AMD_FIFO_SIZE;
	delay = frames_to_bytes(runtime, AMD_FIFO_SIZE);
	if (azx_dev->insufficient) {
		if (pos < delay) {
			delay = pos;
			runtime->delay = bytes_to_frames(runtime, pos);
		} else {
			azx_dev->insufficient = 0;
		}
	}

	/* correct the DMA position for capture stream */
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		if (pos < delay)
			pos += azx_dev->core.bufsize;
		pos -= delay;
	}

	return pos;
}

static int azx_get_delay_from_fifo(struct azx *chip, struct azx_dev *azx_dev,
				   unsigned int pos)
{
	struct snd_pcm_substream *substream = azx_dev->core.substream;

	/* just read back the calculated value in the above */
	return substream->runtime->delay;
}

static void __azx_shutdown_chip(struct azx *chip, bool skip_link_reset)
{
	azx_stop_chip(chip);
	if (!skip_link_reset)
		azx_enter_link_reset(chip);
	azx_clear_irq_pending(chip);
	display_power(chip, false);
}

#ifdef CONFIG_PM
static DEFINE_MUTEX(card_list_lock);
static LIST_HEAD(card_list);

static void azx_shutdown_chip(struct azx *chip)
{
	__azx_shutdown_chip(chip, false);
}

static void azx_add_card_list(struct azx *chip)
{
	struct hda_intel *hda = container_of(chip, struct hda_intel, chip);
	mutex_lock(&card_list_lock);
	list_add(&hda->list, &card_list);
	mutex_unlock(&card_list_lock);
}

static void azx_del_card_list(struct azx *chip)
{
	struct hda_intel *hda = container_of(chip, struct hda_intel, chip);
	mutex_lock(&card_list_lock);
	list_del_init(&hda->list);
	mutex_unlock(&card_list_lock);
}

/* trigger power-save check at writing parameter */
static int param_set_xint(const char *val, const struct kernel_param *kp)
{
	struct hda_intel *hda;
	struct azx *chip;
	int prev = power_save;
	int ret = param_set_int(val, kp);

	if (ret || prev == power_save)
		return ret;

	mutex_lock(&card_list_lock);
	list_for_each_entry(hda, &card_list, list) {
		chip = &hda->chip;
		if (!hda->probe_continued || chip->disabled)
			continue;
		snd_hda_set_power_save(&chip->bus, power_save * 1000);
	}
	mutex_unlock(&card_list_lock);
	return 0;
}

/*
 * power management
 */
static bool azx_is_pm_ready(struct snd_card *card)
{
	struct azx *chip;
	struct hda_intel *hda;

	if (!card)
		return false;
	chip = card->private_data;
	hda = container_of(chip, struct hda_intel, chip);
	if (chip->disabled || hda->init_failed || !chip->running)
		return false;
	return true;
}

static void __azx_runtime_resume(struct azx *chip)
{
	struct hda_intel *hda = container_of(chip, struct hda_intel, chip);
	struct hdac_bus *bus = azx_bus(chip);
	struct hda_codec *codec;
	int status;

	display_power(chip, true);
	if (hda->need_i915_power)
		snd_hdac_i915_set_bclk(bus);

	/* Read STATESTS before controller reset */
	status = azx_readw(chip, STATESTS);

	azx_init_pci(chip);
	hda_intel_init_chip(chip, true);

	/* Avoid codec resume if runtime resume is for system suspend */
	if (!chip->pm_prepared) {
		list_for_each_codec(codec, &chip->bus) {
			if (codec->relaxed_resume)
				continue;

			if (codec->forced_resume || (status & (1 << codec->addr)))
				pm_request_resume(hda_codec_dev(codec));
		}
	}

	/* power down again for link-controlled chips */
	if (!hda->need_i915_power)
		display_power(chip, false);
}

#ifdef CONFIG_PM_SLEEP
static int azx_prepare(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip;

	if (!azx_is_pm_ready(card))
		return 0;

	chip = card->private_data;
	chip->pm_prepared = 1;
	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);

	flush_work(&azx_bus(chip)->unsol_work);

	/* HDA controller always requires different WAKEEN for runtime suspend
	 * and system suspend, so don't use direct-complete here.
	 */
	return 0;
}

static void azx_complete(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip;

	if (!azx_is_pm_ready(card))
		return;

	chip = card->private_data;
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	chip->pm_prepared = 0;
}

static int azx_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip;
	struct hdac_bus *bus;

	if (!azx_is_pm_ready(card))
		return 0;

	chip = card->private_data;
	bus = azx_bus(chip);
	azx_shutdown_chip(chip);
	if (bus->irq >= 0) {
		free_irq(bus->irq, chip);
		bus->irq = -1;
		chip->card->sync_irq = -1;
	}

	if (chip->msi)
		pci_disable_msi(chip->pci);

	trace_azx_suspend(chip);
	return 0;
}

static int azx_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip;

	if (!azx_is_pm_ready(card))
		return 0;

	chip = card->private_data;
	if (chip->msi)
		if (pci_enable_msi(chip->pci) < 0)
			chip->msi = 0;
	if (azx_acquire_irq(chip, 1) < 0)
		return -EIO;

	__azx_runtime_resume(chip);

	trace_azx_resume(chip);
	return 0;
}

/* put codec down to D3 at hibernation for Intel SKL+;
 * otherwise BIOS may still access the codec and screw up the driver
 */
static int azx_freeze_noirq(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;
	struct pci_dev *pci = to_pci_dev(dev);

	if (!azx_is_pm_ready(card))
		return 0;
	if (chip->driver_type == AZX_DRIVER_SKL)
		pci_set_power_state(pci, PCI_D3hot);

	return 0;
}

static int azx_thaw_noirq(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip = card->private_data;
	struct pci_dev *pci = to_pci_dev(dev);

	if (!azx_is_pm_ready(card))
		return 0;
	if (chip->driver_type == AZX_DRIVER_SKL)
		pci_set_power_state(pci, PCI_D0);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static int azx_runtime_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip;

	if (!azx_is_pm_ready(card))
		return 0;
	chip = card->private_data;

	/* enable controller wake up event */
	azx_writew(chip, WAKEEN, azx_readw(chip, WAKEEN) | STATESTS_INT_MASK);

	azx_shutdown_chip(chip);
	trace_azx_runtime_suspend(chip);
	return 0;
}

static int azx_runtime_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip;

	if (!azx_is_pm_ready(card))
		return 0;
	chip = card->private_data;
	__azx_runtime_resume(chip);

	/* disable controller Wake Up event*/
	azx_writew(chip, WAKEEN, azx_readw(chip, WAKEEN) & ~STATESTS_INT_MASK);

	trace_azx_runtime_resume(chip);
	return 0;
}

static int azx_runtime_idle(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct azx *chip;
	struct hda_intel *hda;

	if (!card)
		return 0;

	chip = card->private_data;
	hda = container_of(chip, struct hda_intel, chip);
	if (chip->disabled || hda->init_failed)
		return 0;

	if (!power_save_controller || !azx_has_pm_runtime(chip) ||
	    azx_bus(chip)->codec_powered || !chip->running)
		return -EBUSY;

	/* ELD notification gets broken when HD-audio bus is off */
	if (needs_eld_notify_link(chip))
		return -EBUSY;

	return 0;
}

static const struct dev_pm_ops azx_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(azx_suspend, azx_resume)
#ifdef CONFIG_PM_SLEEP
	.prepare = azx_prepare,
	.complete = azx_complete,
	.freeze_noirq = azx_freeze_noirq,
	.thaw_noirq = azx_thaw_noirq,
#endif
	SET_RUNTIME_PM_OPS(azx_runtime_suspend, azx_runtime_resume, azx_runtime_idle)
};

#define AZX_PM_OPS	&azx_pm
#else
#define azx_add_card_list(chip) /* NOP */
#define azx_del_card_list(chip) /* NOP */
#define AZX_PM_OPS	NULL
#endif /* CONFIG_PM */


static int azx_probe_continue(struct azx *chip);

#ifdef SUPPORT_VGA_SWITCHEROO
static struct pci_dev *get_bound_vga(struct pci_dev *pci);

static void azx_vs_set_state(struct pci_dev *pci,
			     enum vga_switcheroo_state state)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct azx *chip = card->private_data;
	struct hda_intel *hda = container_of(chip, struct hda_intel, chip);
	struct hda_codec *codec;
	bool disabled;

	wait_for_completion(&hda->probe_wait);
	if (hda->init_failed)
		return;

	disabled = (state == VGA_SWITCHEROO_OFF);
	if (chip->disabled == disabled)
		return;

	if (!hda->probe_continued) {
		chip->disabled = disabled;
		if (!disabled) {
			dev_info(chip->card->dev,
				 "Start delayed initialization\n");
			if (azx_probe_continue(chip) < 0)
				dev_err(chip->card->dev, "initialization error\n");
		}
	} else {
		dev_info(chip->card->dev, "%s via vga_switcheroo\n",
			 disabled ? "Disabling" : "Enabling");
		if (disabled) {
			list_for_each_codec(codec, &chip->bus) {
				pm_runtime_suspend(hda_codec_dev(codec));
				pm_runtime_disable(hda_codec_dev(codec));
			}
			pm_runtime_suspend(card->dev);
			pm_runtime_disable(card->dev);
			/* when we get suspended by vga_switcheroo we end up in D3cold,
			 * however we have no ACPI handle, so pci/acpi can't put us there,
			 * put ourselves there */
			pci->current_state = PCI_D3cold;
			chip->disabled = true;
			if (snd_hda_lock_devices(&chip->bus))
				dev_warn(chip->card->dev,
					 "Cannot lock devices!\n");
		} else {
			snd_hda_unlock_devices(&chip->bus);
			chip->disabled = false;
			pm_runtime_enable(card->dev);
			list_for_each_codec(codec, &chip->bus) {
				pm_runtime_enable(hda_codec_dev(codec));
				pm_runtime_resume(hda_codec_dev(codec));
			}
		}
	}
}

static bool azx_vs_can_switch(struct pci_dev *pci)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct azx *chip = card->private_data;
	struct hda_intel *hda = container_of(chip, struct hda_intel, chip);

	wait_for_completion(&hda->probe_wait);
	if (hda->init_failed)
		return false;
	if (chip->disabled || !hda->probe_continued)
		return true;
	if (snd_hda_lock_devices(&chip->bus))
		return false;
	snd_hda_unlock_devices(&chip->bus);
	return true;
}

/*
 * The discrete GPU cannot power down unless the HDA controller runtime
 * suspends, so activate runtime PM on codecs even if power_save == 0.
 */
static void setup_vga_switcheroo_runtime_pm(struct azx *chip)
{
	struct hda_intel *hda = container_of(chip, struct hda_intel, chip);
	struct hda_codec *codec;

	if (hda->use_vga_switcheroo && !needs_eld_notify_link(chip)) {
		list_for_each_codec(codec, &chip->bus)
			codec->auto_runtime_pm = 1;
		/* reset the power save setup */
		if (chip->running)
			set_default_power_save(chip);
	}
}

static void azx_vs_gpu_bound(struct pci_dev *pci,
			     enum vga_switcheroo_client_id client_id)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct azx *chip = card->private_data;

	if (client_id == VGA_SWITCHEROO_DIS)
		chip->bus.keep_power = 0;
	setup_vga_switcheroo_runtime_pm(chip);
}

static void init_vga_switcheroo(struct azx *chip)
{
	struct hda_intel *hda = container_of(chip, struct hda_intel, chip);
	struct pci_dev *p = get_bound_vga(chip->pci);
	struct pci_dev *parent;
	if (p) {
		dev_info(chip->card->dev,
			 "Handle vga_switcheroo audio client\n");
		hda->use_vga_switcheroo = 1;

		/* cleared in either gpu_bound op or codec probe, or when its
		 * upstream port has _PR3 (i.e. dGPU).
		 */
		parent = pci_upstream_bridge(p);
		chip->bus.keep_power = parent ? !pci_pr3_present(parent) : 1;
		chip->driver_caps |= AZX_DCAPS_PM_RUNTIME;
		pci_dev_put(p);
	}
}

static const struct vga_switcheroo_client_ops azx_vs_ops = {
	.set_gpu_state = azx_vs_set_state,
	.can_switch = azx_vs_can_switch,
	.gpu_bound = azx_vs_gpu_bound,
};

static int register_vga_switcheroo(struct azx *chip)
{
	struct hda_intel *hda = container_of(chip, struct hda_intel, chip);
	struct pci_dev *p;
	int err;

	if (!hda->use_vga_switcheroo)
		return 0;

	p = get_bound_vga(chip->pci);
	err = vga_switcheroo_register_audio_client(chip->pci, &azx_vs_ops, p);
	pci_dev_put(p);

	if (err < 0)
		return err;
	hda->vga_switcheroo_registered = 1;

	return 0;
}
#else
#define init_vga_switcheroo(chip)		/* NOP */
#define register_vga_switcheroo(chip)		0
#define check_hdmi_disabled(pci)	false
#define setup_vga_switcheroo_runtime_pm(chip)	/* NOP */
#endif /* SUPPORT_VGA_SWITCHER */

/*
 * destructor
 */
static void azx_free(struct azx *chip)
{
	struct pci_dev *pci = chip->pci;
	struct hda_intel *hda = container_of(chip, struct hda_intel, chip);
	struct hdac_bus *bus = azx_bus(chip);

	if (hda->freed)
		return;

	if (azx_has_pm_runtime(chip) && chip->running) {
		pm_runtime_get_noresume(&pci->dev);
		pm_runtime_forbid(&pci->dev);
		pm_runtime_dont_use_autosuspend(&pci->dev);
	}

	chip->running = 0;

	azx_del_card_list(chip);

	hda->init_failed = 1; /* to be sure */
	complete_all(&hda->probe_wait);

	if (use_vga_switcheroo(hda)) {
		if (chip->disabled && hda->probe_continued)
			snd_hda_unlock_devices(&chip->bus);
		if (hda->vga_switcheroo_registered)
			vga_switcheroo_unregister_client(chip->pci);
	}

	if (bus->chip_init) {
		azx_clear_irq_pending(chip);
		azx_stop_all_streams(chip);
		azx_stop_chip(chip);
	}

	if (bus->irq >= 0)
		free_irq(bus->irq, (void*)chip);

	azx_free_stream_pages(chip);
	azx_free_streams(chip);
	snd_hdac_bus_exit(bus);

#ifdef CONFIG_SND_HDA_PATCH_LOADER
	release_firmware(chip->fw);
#endif
	display_power(chip, false);

	if (chip->driver_caps & AZX_DCAPS_I915_COMPONENT)
		snd_hdac_i915_exit(bus);

	hda->freed = 1;
}

static int azx_dev_disconnect(struct snd_device *device)
{
	struct azx *chip = device->device_data;
	struct hdac_bus *bus = azx_bus(chip);

	chip->bus.shutdown = 1;
	cancel_work_sync(&bus->unsol_work);

	return 0;
}

static int azx_dev_free(struct snd_device *device)
{
	azx_free(device->device_data);
	return 0;
}

#ifdef SUPPORT_VGA_SWITCHEROO
#ifdef CONFIG_ACPI
/* ATPX is in the integrated GPU's namespace */
static bool atpx_present(void)
{
	struct pci_dev *pdev = NULL;
	acpi_handle dhandle, atpx_handle;
	acpi_status status;

	while ((pdev = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, pdev)) != NULL) {
		dhandle = ACPI_HANDLE(&pdev->dev);
		if (dhandle) {
			status = acpi_get_handle(dhandle, "ATPX", &atpx_handle);
			if (ACPI_SUCCESS(status)) {
				pci_dev_put(pdev);
				return true;
			}
		}
	}
	while ((pdev = pci_get_class(PCI_CLASS_DISPLAY_OTHER << 8, pdev)) != NULL) {
		dhandle = ACPI_HANDLE(&pdev->dev);
		if (dhandle) {
			status = acpi_get_handle(dhandle, "ATPX", &atpx_handle);
			if (ACPI_SUCCESS(status)) {
				pci_dev_put(pdev);
				return true;
			}
		}
	}
	return false;
}
#else
static bool atpx_present(void)
{
	return false;
}
#endif

/*
 * Check of disabled HDMI controller by vga_switcheroo
 */
static struct pci_dev *get_bound_vga(struct pci_dev *pci)
{
	struct pci_dev *p;

	/* check only discrete GPU */
	switch (pci->vendor) {
	case PCI_VENDOR_ID_ATI:
	case PCI_VENDOR_ID_AMD:
		if (pci->devfn == 1) {
			p = pci_get_domain_bus_and_slot(pci_domain_nr(pci->bus),
							pci->bus->number, 0);
			if (p) {
				/* ATPX is in the integrated GPU's ACPI namespace
				 * rather than the dGPU's namespace. However,
				 * the dGPU is the one who is involved in
				 * vgaswitcheroo.
				 */
				if (((p->class >> 16) == PCI_BASE_CLASS_DISPLAY) &&
				    atpx_present())
					return p;
				pci_dev_put(p);
			}
		}
		break;
	case PCI_VENDOR_ID_NVIDIA:
		if (pci->devfn == 1) {
			p = pci_get_domain_bus_and_slot(pci_domain_nr(pci->bus),
							pci->bus->number, 0);
			if (p) {
				if ((p->class >> 16) == PCI_BASE_CLASS_DISPLAY)
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
 * allow/deny-listing for position_fix
 */
static const struct snd_pci_quirk position_fix_list[] = {
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
	case POS_FIX_SKL:
	case POS_FIX_FIFO:
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
	if (chip->driver_type == AZX_DRIVER_VIA) {
		dev_dbg(chip->card->dev, "Using VIACOMBO position fix\n");
		return POS_FIX_VIACOMBO;
	}
	if (chip->driver_caps & AZX_DCAPS_AMD_WORKAROUND) {
		dev_dbg(chip->card->dev, "Using FIFO position fix\n");
		return POS_FIX_FIFO;
	}
	if (chip->driver_caps & AZX_DCAPS_POSFIX_LPIB) {
		dev_dbg(chip->card->dev, "Using LPIB position fix\n");
		return POS_FIX_LPIB;
	}
	if (chip->driver_type == AZX_DRIVER_SKL) {
		dev_dbg(chip->card->dev, "Using SKL position fix\n");
		return POS_FIX_SKL;
	}
	return POS_FIX_AUTO;
}

static void assign_position_fix(struct azx *chip, int fix)
{
	static const azx_get_pos_callback_t callbacks[] = {
		[POS_FIX_AUTO] = NULL,
		[POS_FIX_LPIB] = azx_get_pos_lpib,
		[POS_FIX_POSBUF] = azx_get_pos_posbuf,
		[POS_FIX_VIACOMBO] = azx_via_get_position,
		[POS_FIX_COMBO] = azx_get_pos_lpib,
		[POS_FIX_SKL] = azx_get_pos_posbuf,
		[POS_FIX_FIFO] = azx_get_pos_fifo,
	};

	chip->get_position[0] = chip->get_position[1] = callbacks[fix];

	/* combo mode uses LPIB only for playback */
	if (fix == POS_FIX_COMBO)
		chip->get_position[1] = NULL;

	if ((fix == POS_FIX_POSBUF || fix == POS_FIX_SKL) &&
	    (chip->driver_caps & AZX_DCAPS_COUNT_LPIB_DELAY)) {
		chip->get_delay[0] = chip->get_delay[1] =
			azx_get_delay_from_lpib;
	}

	if (fix == POS_FIX_FIFO)
		chip->get_delay[0] = chip->get_delay[1] =
			azx_get_delay_from_fifo;
}

/*
 * deny-lists for probe_mask
 */
static const struct snd_pci_quirk probe_mask_list[] = {
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
		azx_bus(chip)->codec_mask = chip->codec_probe_mask & 0xff;
		dev_info(chip->card->dev, "codec_mask forced to 0x%x\n",
			 (int)azx_bus(chip)->codec_mask);
	}
}

/*
 * allow/deny-list for enable_msi
 */
static const struct snd_pci_quirk msi_deny_list[] = {
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
	q = snd_pci_quirk_lookup(chip->pci, msi_deny_list);
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
	int snoop = hda_snoop;

	if (snoop >= 0) {
		dev_info(chip->card->dev, "Force to %s mode by module option\n",
			 snoop ? "snoop" : "non-snoop");
		chip->snoop = snoop;
		chip->uc_buffer = !snoop;
		return;
	}

	snoop = true;
	if (azx_get_snoop_type(chip) == AZX_SNOOP_TYPE_NONE &&
	    chip->driver_type == AZX_DRIVER_VIA) {
		/* force to non-snoop mode for a new VIA controller
		 * when BIOS is set
		 */
		u8 val;
		pci_read_config_byte(chip->pci, 0x42, &val);
		if (!(val & 0x80) && (chip->pci->revision == 0x30 ||
				      chip->pci->revision == 0x20))
			snoop = false;
	}

	if (chip->driver_caps & AZX_DCAPS_SNOOP_OFF)
		snoop = false;

	chip->snoop = snoop;
	if (!snoop) {
		dev_info(chip->card->dev, "Force to non-snoop mode\n");
		/* C-Media requires non-cached pages only for CORB/RIRB */
		if (chip->driver_type != AZX_DRIVER_CMEDIA)
			chip->uc_buffer = true;
	}
}

static void azx_probe_work(struct work_struct *work)
{
	struct hda_intel *hda = container_of(work, struct hda_intel, probe_work.work);
	azx_probe_continue(&hda->chip);
}

static int default_bdl_pos_adj(struct azx *chip)
{
	/* some exceptions: Atoms seem problematic with value 1 */
	if (chip->pci->vendor == PCI_VENDOR_ID_INTEL) {
		switch (chip->pci->device) {
		case 0x0f04: /* Baytrail */
		case 0x2284: /* Braswell */
			return 32;
		}
	}

	switch (chip->driver_type) {
	case AZX_DRIVER_ICH:
	case AZX_DRIVER_PCH:
		return 1;
	default:
		return 32;
	}
}

/*
 * constructor
 */
static const struct hda_controller_ops pci_hda_ops;

static int azx_create(struct snd_card *card, struct pci_dev *pci,
		      int dev, unsigned int driver_caps,
		      struct azx **rchip)
{
	static const struct snd_device_ops ops = {
		.dev_disconnect = azx_dev_disconnect,
		.dev_free = azx_dev_free,
	};
	struct hda_intel *hda;
	struct azx *chip;
	int err;

	*rchip = NULL;

	err = pcim_enable_device(pci);
	if (err < 0)
		return err;

	hda = devm_kzalloc(&pci->dev, sizeof(*hda), GFP_KERNEL);
	if (!hda)
		return -ENOMEM;

	chip = &hda->chip;
	mutex_init(&chip->open_mutex);
	chip->card = card;
	chip->pci = pci;
	chip->ops = &pci_hda_ops;
	chip->driver_caps = driver_caps;
	chip->driver_type = driver_caps & 0xff;
	check_msi(chip);
	chip->dev_index = dev;
	if (jackpoll_ms[dev] >= 50 && jackpoll_ms[dev] <= 60000)
		chip->jackpoll_interval = msecs_to_jiffies(jackpoll_ms[dev]);
	INIT_LIST_HEAD(&chip->pcm_list);
	INIT_WORK(&hda->irq_pending_work, azx_irq_pending_work);
	INIT_LIST_HEAD(&hda->list);
	init_vga_switcheroo(chip);
	init_completion(&hda->probe_wait);

	assign_position_fix(chip, check_position_fix(chip, position_fix[dev]));

	check_probe_mask(chip, dev);

	if (single_cmd < 0) /* allow fallback to single_cmd at errors */
		chip->fallback_to_single_cmd = 1;
	else /* explicitly set to single_cmd or not */
		chip->single_cmd = single_cmd;

	azx_check_snoop_available(chip);

	if (bdl_pos_adj[dev] < 0)
		chip->bdl_pos_adj = default_bdl_pos_adj(chip);
	else
		chip->bdl_pos_adj = bdl_pos_adj[dev];

	err = azx_bus_init(chip, model[dev]);
	if (err < 0)
		return err;

	/* use the non-cached pages in non-snoop mode */
	if (!azx_snoop(chip))
		azx_bus(chip)->dma_type = SNDRV_DMA_TYPE_DEV_WC;

	if (chip->driver_type == AZX_DRIVER_NVIDIA) {
		dev_dbg(chip->card->dev, "Enable delay in RIRB handling\n");
		chip->bus.core.needs_damn_long_delay = 1;
	}

	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
	if (err < 0) {
		dev_err(card->dev, "Error creating device [card]!\n");
		azx_free(chip);
		return err;
	}

	/* continue probing in work context as may trigger request module */
	INIT_DELAYED_WORK(&hda->probe_work, azx_probe_work);

	*rchip = chip;

	return 0;
}

static int azx_first_init(struct azx *chip)
{
	int dev = chip->dev_index;
	struct pci_dev *pci = chip->pci;
	struct snd_card *card = chip->card;
	struct hdac_bus *bus = azx_bus(chip);
	int err;
	unsigned short gcap;
	unsigned int dma_bits = 64;

#if BITS_PER_LONG != 64
	/* Fix up base address on ULI M5461 */
	if (chip->driver_type == AZX_DRIVER_ULI) {
		u16 tmp3;
		pci_read_config_word(pci, 0x40, &tmp3);
		pci_write_config_word(pci, 0x40, tmp3 | 0x10);
		pci_write_config_dword(pci, PCI_BASE_ADDRESS_1, 0);
	}
#endif

	err = pcim_iomap_regions(pci, 1 << 0, "ICH HD audio");
	if (err < 0)
		return err;

	bus->addr = pci_resource_start(pci, 0);
	bus->remap_addr = pcim_iomap_table(pci)[0];

	if (chip->driver_type == AZX_DRIVER_SKL)
		snd_hdac_bus_parse_capabilities(bus);

	/*
	 * Some Intel CPUs has always running timer (ART) feature and
	 * controller may have Global time sync reporting capability, so
	 * check both of these before declaring synchronized time reporting
	 * capability SNDRV_PCM_INFO_HAS_LINK_SYNCHRONIZED_ATIME
	 */
	chip->gts_present = false;

#ifdef CONFIG_X86
	if (bus->ppcap && boot_cpu_has(X86_FEATURE_ART))
		chip->gts_present = true;
#endif

	if (chip->msi) {
		if (chip->driver_caps & AZX_DCAPS_NO_MSI64) {
			dev_dbg(card->dev, "Disabling 64bit MSI\n");
			pci->no_64bit_msi = true;
		}
		if (pci_enable_msi(pci) < 0)
			chip->msi = 0;
	}

	pci_set_master(pci);

	gcap = azx_readw(chip, GCAP);
	dev_dbg(card->dev, "chipset global capabilities = 0x%x\n", gcap);

	/* AMD devices support 40 or 48bit DMA, take the safe one */
	if (chip->pci->vendor == PCI_VENDOR_ID_AMD)
		dma_bits = 40;

	/* disable SB600 64bit support for safety */
	if (chip->pci->vendor == PCI_VENDOR_ID_ATI) {
		struct pci_dev *p_smbus;
		dma_bits = 40;
		p_smbus = pci_get_device(PCI_VENDOR_ID_ATI,
					 PCI_DEVICE_ID_ATI_SBX00_SMBUS,
					 NULL);
		if (p_smbus) {
			if (p_smbus->revision < 0x30)
				gcap &= ~AZX_GCAP_64OK;
			pci_dev_put(p_smbus);
		}
	}

	/* NVidia hardware normally only supports up to 40 bits of DMA */
	if (chip->pci->vendor == PCI_VENDOR_ID_NVIDIA)
		dma_bits = 40;

	/* disable 64bit DMA address on some devices */
	if (chip->driver_caps & AZX_DCAPS_NO_64BIT) {
		dev_dbg(card->dev, "Disabling 64bit DMA\n");
		gcap &= ~AZX_GCAP_64OK;
	}

	/* disable buffer size rounding to 128-byte multiples if supported */
	if (align_buffer_size >= 0)
		chip->align_buffer_size = !!align_buffer_size;
	else {
		if (chip->driver_caps & AZX_DCAPS_NO_ALIGN_BUFSIZE)
			chip->align_buffer_size = 0;
		else
			chip->align_buffer_size = 1;
	}

	/* allow 64bit DMA address if supported by H/W */
	if (!(gcap & AZX_GCAP_64OK))
		dma_bits = 32;
	if (dma_set_mask_and_coherent(&pci->dev, DMA_BIT_MASK(dma_bits)))
		dma_set_mask_and_coherent(&pci->dev, DMA_BIT_MASK(32));

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

	/* sanity check for the SDxCTL.STRM field overflow */
	if (chip->num_streams > 15 &&
	    (chip->driver_caps & AZX_DCAPS_SEPARATE_STREAM_TAG) == 0) {
		dev_warn(chip->card->dev, "number of I/O streams is %d, "
			 "forcing separate stream tags", chip->num_streams);
		chip->driver_caps |= AZX_DCAPS_SEPARATE_STREAM_TAG;
	}

	/* initialize streams */
	err = azx_init_streams(chip);
	if (err < 0)
		return err;

	err = azx_alloc_stream_pages(chip);
	if (err < 0)
		return err;

	/* initialize chip */
	azx_init_pci(chip);

	snd_hdac_i915_set_bclk(bus);

	hda_intel_init_chip(chip, (probe_only[dev] & 2) == 0);

	/* codec detection */
	if (!azx_bus(chip)->codec_mask) {
		dev_err(card->dev, "no codecs found!\n");
		/* keep running the rest for the runtime PM */
	}

	if (azx_acquire_irq(chip, 0) < 0)
		return -EBUSY;

	strcpy(card->driver, "HDA-Intel");
	strscpy(card->shortname, driver_short_names[chip->driver_type],
		sizeof(card->shortname));
	snprintf(card->longname, sizeof(card->longname),
		 "%s at 0x%lx irq %i",
		 card->shortname, bus->addr, bus->irq);

	return 0;
}

#ifdef CONFIG_SND_HDA_PATCH_LOADER
/* callback from request_firmware_nowait() */
static void azx_firmware_cb(const struct firmware *fw, void *context)
{
	struct snd_card *card = context;
	struct azx *chip = card->private_data;

	if (fw)
		chip->fw = fw;
	else
		dev_err(card->dev, "Cannot load firmware, continue without patching\n");
	if (!chip->disabled) {
		/* continue probing */
		azx_probe_continue(chip);
	}
}
#endif

static int disable_msi_reset_irq(struct azx *chip)
{
	struct hdac_bus *bus = azx_bus(chip);
	int err;

	free_irq(bus->irq, chip);
	bus->irq = -1;
	chip->card->sync_irq = -1;
	pci_disable_msi(chip->pci);
	chip->msi = 0;
	err = azx_acquire_irq(chip, 1);
	if (err < 0)
		return err;

	return 0;
}

/* Denylist for skipping the whole probe:
 * some HD-audio PCI entries are exposed without any codecs, and such devices
 * should be ignored from the beginning.
 */
static const struct pci_device_id driver_denylist[] = {
	{ PCI_DEVICE_SUB(0x1022, 0x1487, 0x1043, 0x874f) }, /* ASUS ROG Zenith II / Strix */
	{ PCI_DEVICE_SUB(0x1022, 0x1487, 0x1462, 0xcb59) }, /* MSI TRX40 Creator */
	{ PCI_DEVICE_SUB(0x1022, 0x1487, 0x1462, 0xcb60) }, /* MSI TRX40 */
	{}
};

static const struct hda_controller_ops pci_hda_ops = {
	.disable_msi_reset_irq = disable_msi_reset_irq,
	.position_check = azx_position_check,
};

static DECLARE_BITMAP(probed_devs, SNDRV_CARDS);

static int azx_probe(struct pci_dev *pci,
		     const struct pci_device_id *pci_id)
{
	struct snd_card *card;
	struct hda_intel *hda;
	struct azx *chip;
	bool schedule_probe;
	int dev;
	int err;

	if (pci_match_id(driver_denylist, pci)) {
		dev_info(&pci->dev, "Skipping the device on the denylist\n");
		return -ENODEV;
	}

	dev = find_first_zero_bit(probed_devs, SNDRV_CARDS);
	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		set_bit(dev, probed_devs);
		return -ENOENT;
	}

	/*
	 * stop probe if another Intel's DSP driver should be activated
	 */
	if (dmic_detect) {
		err = snd_intel_dsp_driver_probe(pci);
		if (err != SND_INTEL_DSP_DRIVER_ANY && err != SND_INTEL_DSP_DRIVER_LEGACY) {
			dev_dbg(&pci->dev, "HDAudio driver not selected, aborting probe\n");
			return -ENODEV;
		}
	} else {
		dev_warn(&pci->dev, "dmic_detect option is deprecated, pass snd-intel-dspcfg.dsp_driver=1 option instead\n");
	}

	err = snd_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
			   0, &card);
	if (err < 0) {
		dev_err(&pci->dev, "Error creating card!\n");
		return err;
	}

	err = azx_create(card, pci, dev, pci_id->driver_data, &chip);
	if (err < 0)
		goto out_free;
	card->private_data = chip;
	hda = container_of(chip, struct hda_intel, chip);

	pci_set_drvdata(pci, card);

	err = register_vga_switcheroo(chip);
	if (err < 0) {
		dev_err(card->dev, "Error registering vga_switcheroo client\n");
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
	if (CONTROLLER_IN_GPU(pci))
		dev_err(card->dev, "Haswell/Broadwell HDMI/DP must build in CONFIG_SND_HDA_I915\n");
#endif

	if (schedule_probe)
		schedule_delayed_work(&hda->probe_work, 0);

	set_bit(dev, probed_devs);
	if (chip->disabled)
		complete_all(&hda->probe_wait);
	return 0;

out_free:
	snd_card_free(card);
	return err;
}

#ifdef CONFIG_PM
/* On some boards setting power_save to a non 0 value leads to clicking /
 * popping sounds when ever we enter/leave powersaving mode. Ideally we would
 * figure out how to avoid these sounds, but that is not always feasible.
 * So we keep a list of devices where we disable powersaving as its known
 * to causes problems on these devices.
 */
static const struct snd_pci_quirk power_save_denylist[] = {
	/* https://bugzilla.redhat.com/show_bug.cgi?id=1525104 */
	SND_PCI_QUIRK(0x1849, 0xc892, "Asrock B85M-ITX", 0),
	/* https://bugzilla.redhat.com/show_bug.cgi?id=1525104 */
	SND_PCI_QUIRK(0x1849, 0x0397, "Asrock N68C-S UCC", 0),
	/* https://bugzilla.redhat.com/show_bug.cgi?id=1525104 */
	SND_PCI_QUIRK(0x1849, 0x7662, "Asrock H81M-HDS", 0),
	/* https://bugzilla.redhat.com/show_bug.cgi?id=1525104 */
	SND_PCI_QUIRK(0x1043, 0x8733, "Asus Prime X370-Pro", 0),
	/* https://bugzilla.redhat.com/show_bug.cgi?id=1525104 */
	SND_PCI_QUIRK(0x1028, 0x0497, "Dell Precision T3600", 0),
	/* https://bugzilla.redhat.com/show_bug.cgi?id=1525104 */
	/* Note the P55A-UD3 and Z87-D3HP share the subsys id for the HDA dev */
	SND_PCI_QUIRK(0x1458, 0xa002, "Gigabyte P55A-UD3 / Z87-D3HP", 0),
	/* https://bugzilla.redhat.com/show_bug.cgi?id=1525104 */
	SND_PCI_QUIRK(0x8086, 0x2040, "Intel DZ77BH-55K", 0),
	/* https://bugzilla.kernel.org/show_bug.cgi?id=199607 */
	SND_PCI_QUIRK(0x8086, 0x2057, "Intel NUC5i7RYB", 0),
	/* https://bugs.launchpad.net/bugs/1821663 */
	SND_PCI_QUIRK(0x8086, 0x2064, "Intel SDP 8086:2064", 0),
	/* https://bugzilla.redhat.com/show_bug.cgi?id=1520902 */
	SND_PCI_QUIRK(0x8086, 0x2068, "Intel NUC7i3BNB", 0),
	/* https://bugzilla.kernel.org/show_bug.cgi?id=198611 */
	SND_PCI_QUIRK(0x17aa, 0x2227, "Lenovo X1 Carbon 3rd Gen", 0),
	/* https://bugzilla.redhat.com/show_bug.cgi?id=1689623 */
	SND_PCI_QUIRK(0x17aa, 0x367b, "Lenovo IdeaCentre B550", 0),
	/* https://bugzilla.redhat.com/show_bug.cgi?id=1572975 */
	SND_PCI_QUIRK(0x17aa, 0x36a7, "Lenovo C50 All in one", 0),
	/* https://bugs.launchpad.net/bugs/1821663 */
	SND_PCI_QUIRK(0x1631, 0xe017, "Packard Bell NEC IMEDIA 5204", 0),
	{}
};
#endif /* CONFIG_PM */

static void set_default_power_save(struct azx *chip)
{
	int val = power_save;

#ifdef CONFIG_PM
	if (pm_blacklist) {
		const struct snd_pci_quirk *q;

		q = snd_pci_quirk_lookup(chip->pci, power_save_denylist);
		if (q && val) {
			dev_info(chip->card->dev, "device %04x:%04x is on the power_save denylist, forcing power_save to 0\n",
				 q->subvendor, q->subdevice);
			val = 0;
		}
	}
#endif /* CONFIG_PM */
	snd_hda_set_power_save(&chip->bus, val * 1000);
}

/* number of codec slots for each chipset: 0 = default slots (i.e. 4) */
static const unsigned int azx_max_codecs[AZX_NUM_DRIVERS] = {
	[AZX_DRIVER_NVIDIA] = 8,
	[AZX_DRIVER_TERA] = 1,
};

static int azx_probe_continue(struct azx *chip)
{
	struct hda_intel *hda = container_of(chip, struct hda_intel, chip);
	struct hdac_bus *bus = azx_bus(chip);
	struct pci_dev *pci = chip->pci;
	int dev = chip->dev_index;
	int err;

	if (chip->disabled || hda->init_failed)
		return -EIO;
	if (hda->probe_retry)
		goto probe_retry;

	to_hda_bus(bus)->bus_probing = 1;
	hda->probe_continued = 1;

	/* bind with i915 if needed */
	if (chip->driver_caps & AZX_DCAPS_I915_COMPONENT) {
		err = snd_hdac_i915_init(bus);
		if (err < 0) {
			/* if the controller is bound only with HDMI/DP
			 * (for HSW and BDW), we need to abort the probe;
			 * for other chips, still continue probing as other
			 * codecs can be on the same link.
			 */
			if (CONTROLLER_IN_GPU(pci)) {
				dev_err(chip->card->dev,
					"HSW/BDW HD-audio HDMI/DP requires binding with gfx driver\n");
				goto out_free;
			} else {
				/* don't bother any longer */
				chip->driver_caps &= ~AZX_DCAPS_I915_COMPONENT;
			}
		}

		/* HSW/BDW controllers need this power */
		if (CONTROLLER_IN_GPU(pci))
			hda->need_i915_power = true;
	}

	/* Request display power well for the HDA controller or codec. For
	 * Haswell/Broadwell, both the display HDA controller and codec need
	 * this power. For other platforms, like Baytrail/Braswell, only the
	 * display codec needs the power and it can be released after probe.
	 */
	display_power(chip, true);

	err = azx_first_init(chip);
	if (err < 0)
		goto out_free;

#ifdef CONFIG_SND_HDA_INPUT_BEEP
	chip->beep_mode = beep_mode[dev];
#endif

	/* create codec instances */
	if (bus->codec_mask) {
		err = azx_probe_codecs(chip, azx_max_codecs[chip->driver_type]);
		if (err < 0)
			goto out_free;
	}

#ifdef CONFIG_SND_HDA_PATCH_LOADER
	if (chip->fw) {
		err = snd_hda_load_patch(&chip->bus, chip->fw->size,
					 chip->fw->data);
		if (err < 0)
			goto out_free;
#ifndef CONFIG_PM
		release_firmware(chip->fw); /* no longer needed */
		chip->fw = NULL;
#endif
	}
#endif

 probe_retry:
	if (bus->codec_mask && !(probe_only[dev] & 1)) {
		err = azx_codec_configure(chip);
		if (err) {
			if ((chip->driver_caps & AZX_DCAPS_RETRY_PROBE) &&
			    ++hda->probe_retry < 60) {
				schedule_delayed_work(&hda->probe_work,
						      msecs_to_jiffies(1000));
				return 0; /* keep things up */
			}
			dev_err(chip->card->dev, "Cannot probe codecs, giving up\n");
			goto out_free;
		}
	}

	err = snd_card_register(chip->card);
	if (err < 0)
		goto out_free;

	setup_vga_switcheroo_runtime_pm(chip);

	chip->running = 1;
	azx_add_card_list(chip);

	set_default_power_save(chip);

	if (azx_has_pm_runtime(chip)) {
		pm_runtime_use_autosuspend(&pci->dev);
		pm_runtime_allow(&pci->dev);
		pm_runtime_put_autosuspend(&pci->dev);
	}

out_free:
	if (err < 0) {
		pci_set_drvdata(pci, NULL);
		snd_card_free(chip->card);
		return err;
	}

	if (!hda->need_i915_power)
		display_power(chip, false);
	complete_all(&hda->probe_wait);
	to_hda_bus(bus)->bus_probing = 0;
	hda->probe_retry = 0;
	return 0;
}

static void azx_remove(struct pci_dev *pci)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct azx *chip;
	struct hda_intel *hda;

	if (card) {
		/* cancel the pending probing work */
		chip = card->private_data;
		hda = container_of(chip, struct hda_intel, chip);
		/* FIXME: below is an ugly workaround.
		 * Both device_release_driver() and driver_probe_device()
		 * take *both* the device's and its parent's lock before
		 * calling the remove() and probe() callbacks.  The codec
		 * probe takes the locks of both the codec itself and its
		 * parent, i.e. the PCI controller dev.  Meanwhile, when
		 * the PCI controller is unbound, it takes its lock, too
		 * ==> ouch, a deadlock!
		 * As a workaround, we unlock temporarily here the controller
		 * device during cancel_work_sync() call.
		 */
		device_unlock(&pci->dev);
		cancel_delayed_work_sync(&hda->probe_work);
		device_lock(&pci->dev);

		clear_bit(chip->dev_index, probed_devs);
		pci_set_drvdata(pci, NULL);
		snd_card_free(card);
	}
}

static void azx_shutdown(struct pci_dev *pci)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct azx *chip;

	if (!card)
		return;
	chip = card->private_data;
	if (chip && chip->running)
		__azx_shutdown_chip(chip, true);
}

/* PCI IDs */
static const struct pci_device_id azx_ids[] = {
	/* CPT */
	{ PCI_DEVICE(0x8086, 0x1c20),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_PCH_NOPM },
	/* PBG */
	{ PCI_DEVICE(0x8086, 0x1d20),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_PCH_NOPM },
	/* Panther Point */
	{ PCI_DEVICE(0x8086, 0x1e20),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_PCH_NOPM },
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
	/* Lewisburg */
	{ PCI_DEVICE(0x8086, 0xa1f0),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_SKYLAKE },
	{ PCI_DEVICE(0x8086, 0xa270),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_SKYLAKE },
	/* Lynx Point-LP */
	{ PCI_DEVICE(0x8086, 0x9c20),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_PCH },
	/* Lynx Point-LP */
	{ PCI_DEVICE(0x8086, 0x9c21),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_PCH },
	/* Wildcat Point-LP */
	{ PCI_DEVICE(0x8086, 0x9ca0),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_PCH },
	/* Sunrise Point */
	{ PCI_DEVICE(0x8086, 0xa170),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE },
	/* Sunrise Point-LP */
	{ PCI_DEVICE(0x8086, 0x9d70),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE },
	/* Kabylake */
	{ PCI_DEVICE(0x8086, 0xa171),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE },
	/* Kabylake-LP */
	{ PCI_DEVICE(0x8086, 0x9d71),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE },
	/* Kabylake-H */
	{ PCI_DEVICE(0x8086, 0xa2f0),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE },
	/* Coffelake */
	{ PCI_DEVICE(0x8086, 0xa348),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* Cannonlake */
	{ PCI_DEVICE(0x8086, 0x9dc8),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* CometLake-LP */
	{ PCI_DEVICE(0x8086, 0x02C8),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* CometLake-H */
	{ PCI_DEVICE(0x8086, 0x06C8),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	{ PCI_DEVICE(0x8086, 0xf1c8),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* CometLake-S */
	{ PCI_DEVICE(0x8086, 0xa3f0),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* CometLake-R */
	{ PCI_DEVICE(0x8086, 0xf0c8),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* Icelake */
	{ PCI_DEVICE(0x8086, 0x34c8),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* Icelake-H */
	{ PCI_DEVICE(0x8086, 0x3dc8),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* Jasperlake */
	{ PCI_DEVICE(0x8086, 0x38c8),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	{ PCI_DEVICE(0x8086, 0x4dc8),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* Tigerlake */
	{ PCI_DEVICE(0x8086, 0xa0c8),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* Tigerlake-H */
	{ PCI_DEVICE(0x8086, 0x43c8),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* DG1 */
	{ PCI_DEVICE(0x8086, 0x490d),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* DG2 */
	{ PCI_DEVICE(0x8086, 0x4f90),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	{ PCI_DEVICE(0x8086, 0x4f91),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	{ PCI_DEVICE(0x8086, 0x4f92),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* Alderlake-S */
	{ PCI_DEVICE(0x8086, 0x7ad0),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* Alderlake-P */
	{ PCI_DEVICE(0x8086, 0x51c8),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	{ PCI_DEVICE(0x8086, 0x51cd),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* Alderlake-M */
	{ PCI_DEVICE(0x8086, 0x51cc),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* Alderlake-N */
	{ PCI_DEVICE(0x8086, 0x54c8),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* Elkhart Lake */
	{ PCI_DEVICE(0x8086, 0x4b55),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	{ PCI_DEVICE(0x8086, 0x4b58),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* Raptor Lake */
	{ PCI_DEVICE(0x8086, 0x7a50),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	{ PCI_DEVICE(0x8086, 0x51ca),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	{ PCI_DEVICE(0x8086, 0x51cb),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	{ PCI_DEVICE(0x8086, 0x51ce),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	{ PCI_DEVICE(0x8086, 0x51cf),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_SKYLAKE},
	/* Broxton-P(Apollolake) */
	{ PCI_DEVICE(0x8086, 0x5a98),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_BROXTON },
	/* Broxton-T */
	{ PCI_DEVICE(0x8086, 0x1a98),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_BROXTON },
	/* Gemini-Lake */
	{ PCI_DEVICE(0x8086, 0x3198),
	  .driver_data = AZX_DRIVER_SKL | AZX_DCAPS_INTEL_BROXTON },
	/* Haswell */
	{ PCI_DEVICE(0x8086, 0x0a0c),
	  .driver_data = AZX_DRIVER_HDMI | AZX_DCAPS_INTEL_HASWELL },
	{ PCI_DEVICE(0x8086, 0x0c0c),
	  .driver_data = AZX_DRIVER_HDMI | AZX_DCAPS_INTEL_HASWELL },
	{ PCI_DEVICE(0x8086, 0x0d0c),
	  .driver_data = AZX_DRIVER_HDMI | AZX_DCAPS_INTEL_HASWELL },
	/* Broadwell */
	{ PCI_DEVICE(0x8086, 0x160c),
	  .driver_data = AZX_DRIVER_HDMI | AZX_DCAPS_INTEL_BROADWELL },
	/* 5 Series/3400 */
	{ PCI_DEVICE(0x8086, 0x3b56),
	  .driver_data = AZX_DRIVER_SCH | AZX_DCAPS_INTEL_PCH_NOPM },
	/* Poulsbo */
	{ PCI_DEVICE(0x8086, 0x811b),
	  .driver_data = AZX_DRIVER_SCH | AZX_DCAPS_INTEL_PCH_BASE },
	/* Oaktrail */
	{ PCI_DEVICE(0x8086, 0x080a),
	  .driver_data = AZX_DRIVER_SCH | AZX_DCAPS_INTEL_PCH_BASE },
	/* BayTrail */
	{ PCI_DEVICE(0x8086, 0x0f04),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_BAYTRAIL },
	/* Braswell */
	{ PCI_DEVICE(0x8086, 0x2284),
	  .driver_data = AZX_DRIVER_PCH | AZX_DCAPS_INTEL_BRASWELL },
	/* ICH6 */
	{ PCI_DEVICE(0x8086, 0x2668),
	  .driver_data = AZX_DRIVER_ICH | AZX_DCAPS_INTEL_ICH },
	/* ICH7 */
	{ PCI_DEVICE(0x8086, 0x27d8),
	  .driver_data = AZX_DRIVER_ICH | AZX_DCAPS_INTEL_ICH },
	/* ESB2 */
	{ PCI_DEVICE(0x8086, 0x269a),
	  .driver_data = AZX_DRIVER_ICH | AZX_DCAPS_INTEL_ICH },
	/* ICH8 */
	{ PCI_DEVICE(0x8086, 0x284b),
	  .driver_data = AZX_DRIVER_ICH | AZX_DCAPS_INTEL_ICH },
	/* ICH9 */
	{ PCI_DEVICE(0x8086, 0x293e),
	  .driver_data = AZX_DRIVER_ICH | AZX_DCAPS_INTEL_ICH },
	/* ICH9 */
	{ PCI_DEVICE(0x8086, 0x293f),
	  .driver_data = AZX_DRIVER_ICH | AZX_DCAPS_INTEL_ICH },
	/* ICH10 */
	{ PCI_DEVICE(0x8086, 0x3a3e),
	  .driver_data = AZX_DRIVER_ICH | AZX_DCAPS_INTEL_ICH },
	/* ICH10 */
	{ PCI_DEVICE(0x8086, 0x3a6e),
	  .driver_data = AZX_DRIVER_ICH | AZX_DCAPS_INTEL_ICH },
	/* Generic Intel */
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_ANY_ID),
	  .class = PCI_CLASS_MULTIMEDIA_HD_AUDIO << 8,
	  .class_mask = 0xffffff,
	  .driver_data = AZX_DRIVER_ICH | AZX_DCAPS_NO_ALIGN_BUFSIZE },
	/* ATI SB 450/600/700/800/900 */
	{ PCI_DEVICE(0x1002, 0x437b),
	  .driver_data = AZX_DRIVER_ATI | AZX_DCAPS_PRESET_ATI_SB },
	{ PCI_DEVICE(0x1002, 0x4383),
	  .driver_data = AZX_DRIVER_ATI | AZX_DCAPS_PRESET_ATI_SB },
	/* AMD Hudson */
	{ PCI_DEVICE(0x1022, 0x780d),
	  .driver_data = AZX_DRIVER_GENERIC | AZX_DCAPS_PRESET_ATI_SB },
	/* AMD, X370 & co */
	{ PCI_DEVICE(0x1022, 0x1457),
	  .driver_data = AZX_DRIVER_GENERIC | AZX_DCAPS_PRESET_AMD_SB },
	/* AMD, X570 & co */
	{ PCI_DEVICE(0x1022, 0x1487),
	  .driver_data = AZX_DRIVER_GENERIC | AZX_DCAPS_PRESET_AMD_SB },
	/* AMD Stoney */
	{ PCI_DEVICE(0x1022, 0x157a),
	  .driver_data = AZX_DRIVER_GENERIC | AZX_DCAPS_PRESET_ATI_SB |
			 AZX_DCAPS_PM_RUNTIME },
	/* AMD Raven */
	{ PCI_DEVICE(0x1022, 0x15e3),
	  .driver_data = AZX_DRIVER_GENERIC | AZX_DCAPS_PRESET_AMD_SB },
	/* ATI HDMI */
	{ PCI_DEVICE(0x1002, 0x0002),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS |
	  AZX_DCAPS_PM_RUNTIME },
	{ PCI_DEVICE(0x1002, 0x1308),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS },
	{ PCI_DEVICE(0x1002, 0x157a),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS },
	{ PCI_DEVICE(0x1002, 0x15b3),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS },
	{ PCI_DEVICE(0x1002, 0x793b),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0x7919),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0x960f),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0x970f),
	  .driver_data = AZX_DRIVER_ATIHDMI | AZX_DCAPS_PRESET_ATI_HDMI },
	{ PCI_DEVICE(0x1002, 0x9840),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS },
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
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS },
	{ PCI_DEVICE(0x1002, 0xaaa0),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS },
	{ PCI_DEVICE(0x1002, 0xaaa8),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS },
	{ PCI_DEVICE(0x1002, 0xaab0),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS },
	{ PCI_DEVICE(0x1002, 0xaac0),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS |
	  AZX_DCAPS_PM_RUNTIME },
	{ PCI_DEVICE(0x1002, 0xaac8),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS |
	  AZX_DCAPS_PM_RUNTIME },
	{ PCI_DEVICE(0x1002, 0xaad8),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS |
	  AZX_DCAPS_PM_RUNTIME },
	{ PCI_DEVICE(0x1002, 0xaae0),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS |
	  AZX_DCAPS_PM_RUNTIME },
	{ PCI_DEVICE(0x1002, 0xaae8),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS |
	  AZX_DCAPS_PM_RUNTIME },
	{ PCI_DEVICE(0x1002, 0xaaf0),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS |
	  AZX_DCAPS_PM_RUNTIME },
	{ PCI_DEVICE(0x1002, 0xaaf8),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS |
	  AZX_DCAPS_PM_RUNTIME },
	{ PCI_DEVICE(0x1002, 0xab00),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS |
	  AZX_DCAPS_PM_RUNTIME },
	{ PCI_DEVICE(0x1002, 0xab08),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS |
	  AZX_DCAPS_PM_RUNTIME },
	{ PCI_DEVICE(0x1002, 0xab10),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS |
	  AZX_DCAPS_PM_RUNTIME },
	{ PCI_DEVICE(0x1002, 0xab18),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS |
	  AZX_DCAPS_PM_RUNTIME },
	{ PCI_DEVICE(0x1002, 0xab20),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS |
	  AZX_DCAPS_PM_RUNTIME },
	{ PCI_DEVICE(0x1002, 0xab28),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS |
	  AZX_DCAPS_PM_RUNTIME },
	{ PCI_DEVICE(0x1002, 0xab38),
	  .driver_data = AZX_DRIVER_ATIHDMI_NS | AZX_DCAPS_PRESET_ATI_HDMI_NS |
	  AZX_DCAPS_PM_RUNTIME },
	/* VIA VT8251/VT8237A */
	{ PCI_DEVICE(0x1106, 0x3288), .driver_data = AZX_DRIVER_VIA },
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
	  AZX_DCAPS_NO_64BIT | AZX_DCAPS_POSFIX_LPIB },
#else
	/* this entry seems still valid -- i.e. without emu20kx chip */
	{ PCI_DEVICE(0x1102, 0x0009),
	  .driver_data = AZX_DRIVER_CTX | AZX_DCAPS_CTX_WORKAROUND |
	  AZX_DCAPS_NO_64BIT | AZX_DCAPS_POSFIX_LPIB },
#endif
	/* CM8888 */
	{ PCI_DEVICE(0x13f6, 0x5011),
	  .driver_data = AZX_DRIVER_CMEDIA |
	  AZX_DCAPS_NO_MSI | AZX_DCAPS_POSFIX_LPIB | AZX_DCAPS_SNOOP_OFF },
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
	/* Zhaoxin */
	{ PCI_DEVICE(0x1d17, 0x3288), .driver_data = AZX_DRIVER_ZHAOXIN },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, azx_ids);

/* pci_driver definition */
static struct pci_driver azx_driver = {
	.name = KBUILD_MODNAME,
	.id_table = azx_ids,
	.probe = azx_probe,
	.remove = azx_remove,
	.shutdown = azx_shutdown,
	.driver = {
		.pm = AZX_PM_OPS,
	},
};

module_pci_driver(azx_driver);
