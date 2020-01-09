// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Jaroslav Kysela <perex@perex.cz>

#include <linux/bits.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <sound/core.h>
#include <sound/intel-dsp-config.h>
#include <sound/intel-nhlt.h>

static int dsp_driver;

module_param(dsp_driver, int, 0444);
MODULE_PARM_DESC(dsp_driver, "Force the DSP driver for Intel DSP (0=auto, 1=legacy, 2=SST, 3=SOF)");

#define FLAG_SST		BIT(0)
#define FLAG_SOF		BIT(1)
#define FLAG_SOF_ONLY_IF_DMIC	BIT(16)

struct config_entry {
	u32 flags;
	u16 device;
	const struct dmi_system_id *dmi_table;
};

/*
 * configuration table
 * - the order of similar PCI ID entries is important!
 * - the first successful match will win
 */
static const struct config_entry config_table[] = {
/* Merrifield */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_MERRIFIELD)
	{
		.flags = FLAG_SOF,
		.device = 0x119a,
	},
#endif
/* Broxton-T */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_APOLLOLAKE)
	{
		.flags = FLAG_SOF,
		.device = 0x1a98,
	},
#endif
/*
 * Apollolake (Broxton-P)
 * the legacy HDaudio driver is used except on Up Squared (SOF) and
 * Chromebooks (SST)
 */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_APOLLOLAKE)
	{
		.flags = FLAG_SOF,
		.device = 0x5a98,
		.dmi_table = (const struct dmi_system_id []) {
			{
				.ident = "Up Squared",
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "AAEON"),
					DMI_MATCH(DMI_BOARD_NAME, "UP-APL01"),
				}
			},
			{}
		}
	},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_INTEL_APL)
	{
		.flags = FLAG_SST,
		.device = 0x5a98,
		.dmi_table = (const struct dmi_system_id []) {
			{
				.ident = "Google Chromebooks",
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "Google"),
				}
			},
			{}
		}
	},
#endif
/*
 * Skylake and Kabylake use legacy HDaudio driver except for Google
 * Chromebooks (SST)
 */

/* Sunrise Point-LP */
#if IS_ENABLED(CONFIG_SND_SOC_INTEL_SKL)
	{
		.flags = FLAG_SST,
		.device = 0x9d70,
		.dmi_table = (const struct dmi_system_id []) {
			{
				.ident = "Google Chromebooks",
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "Google"),
				}
			},
			{}
		}
	},
#endif
/* Kabylake-LP */
#if IS_ENABLED(CONFIG_SND_SOC_INTEL_KBL)
	{
		.flags = FLAG_SST,
		.device = 0x9d71,
		.dmi_table = (const struct dmi_system_id []) {
			{
				.ident = "Google Chromebooks",
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "Google"),
				}
			},
			{}
		}
	},
#endif

/*
 * Geminilake uses legacy HDaudio driver except for Google
 * Chromebooks
 */
/* Geminilake */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_GEMINILAKE)
	{
		.flags = FLAG_SOF,
		.device = 0x3198,
		.dmi_table = (const struct dmi_system_id []) {
			{
				.ident = "Google Chromebooks",
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "Google"),
				}
			},
			{}
		}
	},
#endif

/*
 * CoffeeLake, CannonLake, CometLake, IceLake, TigerLake use legacy
 * HDaudio driver except for Google Chromebooks and when DMICs are
 * present. Two cases are required since Coreboot does not expose NHLT
 * tables.
 *
 * When the Chromebook quirk is not present, it's based on information
 * that no such device exists. When the quirk is present, it could be
 * either based on product information or a placeholder.
 */

/* Cannonlake */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_CANNONLAKE)
	{
		.flags = FLAG_SOF,
		.device = 0x9dc8,
		.dmi_table = (const struct dmi_system_id []) {
			{
				.ident = "Google Chromebooks",
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "Google"),
				}
			},
			{}
		}
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC,
		.device = 0x9dc8,
	},
#endif

/* Coffelake */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_COFFEELAKE)
	{
		.flags = FLAG_SOF,
		.device = 0xa348,
		.dmi_table = (const struct dmi_system_id []) {
			{
				.ident = "Google Chromebooks",
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "Google"),
				}
			},
			{}
		}
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC,
		.device = 0xa348,
	},
#endif

/* Cometlake-LP */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_COMETLAKE_LP)
	{
		.flags = FLAG_SOF,
		.device = 0x02c8,
		.dmi_table = (const struct dmi_system_id []) {
			{
				.ident = "Google Chromebooks",
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "Google"),
				}
			},
			{
				.ident = "Dell laptop",
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
				}
			},
			{}
		}
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC,
		.device = 0x02c8,
	},
#endif
/* Cometlake-H */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_COMETLAKE_H)
	{
		.flags = FLAG_SOF,
		.device = 0x06c8,
		.dmi_table = (const struct dmi_system_id []) {
			{
				.ident = "Dell laptop",
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "Dell"),
				}
			},
			{}
		}
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC,
		.device = 0x06c8,
	},
#endif

/* Icelake */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_ICELAKE)
	{
		.flags = FLAG_SOF,
		.device = 0x34c8,
		.dmi_table = (const struct dmi_system_id []) {
			{
				.ident = "Google Chromebooks",
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "Google"),
				}
			},
			{}
		}
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC,
		.device = 0x34c8,
	},
#endif

/* Tigerlake */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_TIGERLAKE)
	{
		.flags = FLAG_SOF,
		.device = 0xa0c8,
		.dmi_table = (const struct dmi_system_id []) {
			{
				.ident = "Google Chromebooks",
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "Google"),
				}
			},
			{}
		}
	},

	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC,
		.device = 0xa0c8,
	},
#endif

/* Elkhart Lake */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_ELKHARTLAKE)
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC,
		.device = 0x4b55,
	},
#endif

};

static const struct config_entry *snd_intel_dsp_find_config
		(struct pci_dev *pci, const struct config_entry *table, u32 len)
{
	u16 device;

	device = pci->device;
	for (; len > 0; len--, table++) {
		if (table->device != device)
			continue;
		if (table->dmi_table && !dmi_check_system(table->dmi_table))
			continue;
		return table;
	}
	return NULL;
}

static int snd_intel_dsp_check_dmic(struct pci_dev *pci)
{
	struct nhlt_acpi_table *nhlt;
	int ret = 0;

	nhlt = intel_nhlt_init(&pci->dev);
	if (nhlt) {
		if (intel_nhlt_get_dmic_geo(&pci->dev, nhlt))
			ret = 1;
		intel_nhlt_free(nhlt);
	}
	return ret;
}

int snd_intel_dsp_driver_probe(struct pci_dev *pci)
{
	const struct config_entry *cfg;

	/* Intel vendor only */
	if (pci->vendor != 0x8086)
		return SND_INTEL_DSP_DRIVER_ANY;

	if (dsp_driver > 0 && dsp_driver <= SND_INTEL_DSP_DRIVER_LAST)
		return dsp_driver;

	/*
	 * detect DSP by checking class/subclass/prog-id information
	 * class=04 subclass 03 prog-if 00: no DSP, use legacy driver
	 * class=04 subclass 01 prog-if 00: DSP is present
	 *  (and may be required e.g. for DMIC or SSP support)
	 * class=04 subclass 03 prog-if 80: use DSP or legacy mode
	 */
	if (pci->class == 0x040300)
		return SND_INTEL_DSP_DRIVER_LEGACY;
	if (pci->class != 0x040100 && pci->class != 0x040380) {
		dev_err(&pci->dev, "Unknown PCI class/subclass/prog-if information (0x%06x) found, selecting HDA legacy driver\n", pci->class);
		return SND_INTEL_DSP_DRIVER_LEGACY;
	}

	dev_info(&pci->dev, "DSP detected with PCI class/subclass/prog-if info 0x%06x\n", pci->class);

	/* find the configuration for the specific device */
	cfg = snd_intel_dsp_find_config(pci, config_table, ARRAY_SIZE(config_table));
	if (!cfg)
		return SND_INTEL_DSP_DRIVER_ANY;

	if (cfg->flags & FLAG_SOF) {
		if (cfg->flags & FLAG_SOF_ONLY_IF_DMIC) {
			if (snd_intel_dsp_check_dmic(pci)) {
				dev_info(&pci->dev, "Digital mics found on Skylake+ platform, using SOF driver\n");
				return SND_INTEL_DSP_DRIVER_SOF;
			}
		} else {
			return SND_INTEL_DSP_DRIVER_SOF;
		}
	}

	if (cfg->flags & FLAG_SST)
		return SND_INTEL_DSP_DRIVER_SST;

	return SND_INTEL_DSP_DRIVER_LEGACY;
}
EXPORT_SYMBOL_GPL(snd_intel_dsp_driver_probe);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel DSP config driver");
