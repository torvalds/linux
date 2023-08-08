// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Jaroslav Kysela <perex@perex.cz>

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_intel.h>
#include <sound/core.h>
#include <sound/intel-dsp-config.h>
#include <sound/intel-nhlt.h>
#include <sound/soc-acpi.h>

static int dsp_driver;

module_param(dsp_driver, int, 0444);
MODULE_PARM_DESC(dsp_driver, "Force the DSP driver for Intel DSP (0=auto, 1=legacy, 2=SST, 3=SOF)");

#define FLAG_SST			BIT(0)
#define FLAG_SOF			BIT(1)
#define FLAG_SST_ONLY_IF_DMIC		BIT(15)
#define FLAG_SOF_ONLY_IF_DMIC		BIT(16)
#define FLAG_SOF_ONLY_IF_SOUNDWIRE	BIT(17)

#define FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE (FLAG_SOF_ONLY_IF_DMIC | \
					    FLAG_SOF_ONLY_IF_SOUNDWIRE)

struct config_entry {
	u32 flags;
	u16 device;
	u8 acpi_hid[ACPI_ID_LEN];
	const struct dmi_system_id *dmi_table;
	const struct snd_soc_acpi_codecs *codec_hid;
};

static const struct snd_soc_acpi_codecs __maybe_unused essx_83x6 = {
	.num_codecs = 3,
	.codecs = { "ESSX8316", "ESSX8326", "ESSX8336"},
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
		.device = PCI_DEVICE_ID_INTEL_SST_TNG,
	},
#endif
/*
 * Apollolake (Broxton-P)
 * the legacy HDAudio driver is used except on Up Squared (SOF) and
 * Chromebooks (SST), as well as devices based on the ES8336 codec
 */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_APOLLOLAKE)
	{
		.flags = FLAG_SOF,
		.device = PCI_DEVICE_ID_INTEL_HDA_APL,
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
	{
		.flags = FLAG_SOF,
		.device = PCI_DEVICE_ID_INTEL_HDA_APL,
		.codec_hid =  &essx_83x6,
	},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_INTEL_APL)
	{
		.flags = FLAG_SST,
		.device = PCI_DEVICE_ID_INTEL_HDA_APL,
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
 * Skylake and Kabylake use legacy HDAudio driver except for Google
 * Chromebooks (SST)
 */

/* Sunrise Point-LP */
#if IS_ENABLED(CONFIG_SND_SOC_INTEL_SKL)
	{
		.flags = FLAG_SST,
		.device = PCI_DEVICE_ID_INTEL_HDA_SKL_LP,
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
		.flags = FLAG_SST | FLAG_SST_ONLY_IF_DMIC,
		.device = PCI_DEVICE_ID_INTEL_HDA_SKL_LP,
	},
#endif
/* Kabylake-LP */
#if IS_ENABLED(CONFIG_SND_SOC_INTEL_KBL)
	{
		.flags = FLAG_SST,
		.device = PCI_DEVICE_ID_INTEL_HDA_KBL_LP,
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
		.flags = FLAG_SST | FLAG_SST_ONLY_IF_DMIC,
		.device = PCI_DEVICE_ID_INTEL_HDA_KBL_LP,
	},
#endif

/*
 * Geminilake uses legacy HDAudio driver except for Google
 * Chromebooks and devices based on the ES8336 codec
 */
/* Geminilake */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_GEMINILAKE)
	{
		.flags = FLAG_SOF,
		.device = PCI_DEVICE_ID_INTEL_HDA_GML,
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
		.flags = FLAG_SOF,
		.device = PCI_DEVICE_ID_INTEL_HDA_GML,
		.codec_hid =  &essx_83x6,
	},
#endif

/*
 * CoffeeLake, CannonLake, CometLake, IceLake, TigerLake use legacy
 * HDAudio driver except for Google Chromebooks and when DMICs are
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
		.device = PCI_DEVICE_ID_INTEL_HDA_CNL_LP,
		.dmi_table = (const struct dmi_system_id []) {
			{
				.ident = "Google Chromebooks",
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "Google"),
				}
			},
			{
				.ident = "UP-WHL",
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "AAEON"),
				}
			},
			{}
		}
	},
	{
		.flags = FLAG_SOF,
		.device = PCI_DEVICE_ID_INTEL_HDA_CNL_LP,
		.codec_hid =  &essx_83x6,
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_CNL_LP,
	},
#endif

/* Coffelake */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_COFFEELAKE)
	{
		.flags = FLAG_SOF,
		.device = PCI_DEVICE_ID_INTEL_HDA_CNL_H,
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
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_CNL_H,
	},
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_COMETLAKE)
/* Cometlake-LP */
	{
		.flags = FLAG_SOF,
		.device = PCI_DEVICE_ID_INTEL_HDA_CML_LP,
		.dmi_table = (const struct dmi_system_id []) {
			{
				.ident = "Google Chromebooks",
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "Google"),
				}
			},
			{
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
					DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "09C6")
				},
			},
			{
				/* early version of SKU 09C6 */
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
					DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0983")
				},
			},
			{}
		}
	},
	{
		.flags = FLAG_SOF,
		.device = PCI_DEVICE_ID_INTEL_HDA_CML_LP,
		.codec_hid =  &essx_83x6,
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_CML_LP,
	},
/* Cometlake-H */
	{
		.flags = FLAG_SOF,
		.device = PCI_DEVICE_ID_INTEL_HDA_CML_H,
		.dmi_table = (const struct dmi_system_id []) {
			{
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
					DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "098F"),
				},
			},
			{
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
					DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0990"),
				},
			},
			{}
		}
	},
	{
		.flags = FLAG_SOF,
		.device = PCI_DEVICE_ID_INTEL_HDA_CML_H,
		.codec_hid =  &essx_83x6,
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_CML_H,
	},
#endif

/* Icelake */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_ICELAKE)
	{
		.flags = FLAG_SOF,
		.device = PCI_DEVICE_ID_INTEL_HDA_ICL_LP,
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
		.flags = FLAG_SOF,
		.device = PCI_DEVICE_ID_INTEL_HDA_ICL_LP,
		.codec_hid =  &essx_83x6,
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_ICL_LP,
	},
#endif

/* Jasper Lake */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_JASPERLAKE)
	{
		.flags = FLAG_SOF,
		.device = PCI_DEVICE_ID_INTEL_HDA_JSL_N,
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
		.flags = FLAG_SOF,
		.device = PCI_DEVICE_ID_INTEL_HDA_JSL_N,
		.codec_hid =  &essx_83x6,
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC,
		.device = PCI_DEVICE_ID_INTEL_HDA_JSL_N,
	},
#endif

/* Tigerlake */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_TIGERLAKE)
	{
		.flags = FLAG_SOF,
		.device = PCI_DEVICE_ID_INTEL_HDA_TGL_LP,
		.dmi_table = (const struct dmi_system_id []) {
			{
				.ident = "Google Chromebooks",
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "Google"),
				}
			},
			{
				.ident = "UPX-TGL",
				.matches = {
					DMI_MATCH(DMI_SYS_VENDOR, "AAEON"),
				}
			},
			{}
		}
	},
	{
		.flags = FLAG_SOF,
		.device = PCI_DEVICE_ID_INTEL_HDA_TGL_LP,
		.codec_hid =  &essx_83x6,
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_TGL_LP,
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_TGL_H,
	},
#endif

/* Elkhart Lake */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_ELKHARTLAKE)
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC,
		.device = PCI_DEVICE_ID_INTEL_HDA_EHL_0,
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC,
		.device = PCI_DEVICE_ID_INTEL_HDA_EHL_3,
	},
#endif

/* Alder Lake / Raptor Lake */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_ALDERLAKE)
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_ADL_S,
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_RPL_S,
	},
	{
		.flags = FLAG_SOF,
		.device = PCI_DEVICE_ID_INTEL_HDA_ADL_P,
		.codec_hid =  &essx_83x6,
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_ADL_P,
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_ADL_PX,
	},
	{
		.flags = FLAG_SOF,
		.device = PCI_DEVICE_ID_INTEL_HDA_ADL_PS,
		.codec_hid =  &essx_83x6,
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_ADL_PS,
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_ADL_M,
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_ADL_N,
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_RPL_P_0,
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_RPL_P_1,
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_RPL_M,
	},
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_RPL_PX,
	},
#endif

/* Meteor Lake */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_METEORLAKE)
	/* Meteorlake-P */
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_MTL,
	},
#endif

/* Lunar Lake */
#if IS_ENABLED(CONFIG_SND_SOC_SOF_LUNARLAKE)
	/* Lunarlake-P */
	{
		.flags = FLAG_SOF | FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE,
		.device = PCI_DEVICE_ID_INTEL_HDA_LNL_P,
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
		if (table->codec_hid) {
			int i;

			for (i = 0; i < table->codec_hid->num_codecs; i++)
				if (acpi_dev_present(table->codec_hid->codecs[i], NULL, -1))
					break;
			if (i == table->codec_hid->num_codecs)
				continue;
		}
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
		if (intel_nhlt_has_endpoint_type(nhlt, NHLT_LINK_DMIC))
			ret = 1;
		intel_nhlt_free(nhlt);
	}
	return ret;
}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_INTEL_SOUNDWIRE)
static int snd_intel_dsp_check_soundwire(struct pci_dev *pci)
{
	struct sdw_intel_acpi_info info;
	acpi_handle handle;
	int ret;

	handle = ACPI_HANDLE(&pci->dev);

	ret = sdw_intel_acpi_scan(handle, &info);
	if (ret < 0)
		return ret;

	return info.link_mask;
}
#else
static int snd_intel_dsp_check_soundwire(struct pci_dev *pci)
{
	return 0;
}
#endif

int snd_intel_dsp_driver_probe(struct pci_dev *pci)
{
	const struct config_entry *cfg;

	/* Intel vendor only */
	if (pci->vendor != PCI_VENDOR_ID_INTEL)
		return SND_INTEL_DSP_DRIVER_ANY;

	/*
	 * Legacy devices don't have a PCI-based DSP and use HDaudio
	 * for HDMI/DP support, ignore kernel parameter
	 */
	switch (pci->device) {
	case PCI_DEVICE_ID_INTEL_HDA_BDW:
	case PCI_DEVICE_ID_INTEL_HDA_HSW_0:
	case PCI_DEVICE_ID_INTEL_HDA_HSW_2:
	case PCI_DEVICE_ID_INTEL_HDA_HSW_3:
	case PCI_DEVICE_ID_INTEL_HDA_BYT:
	case PCI_DEVICE_ID_INTEL_HDA_BSW:
		return SND_INTEL_DSP_DRIVER_ANY;
	}

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
		dev_err(&pci->dev, "Unknown PCI class/subclass/prog-if information (0x%06x) found, selecting HDAudio legacy driver\n", pci->class);
		return SND_INTEL_DSP_DRIVER_LEGACY;
	}

	dev_info(&pci->dev, "DSP detected with PCI class/subclass/prog-if info 0x%06x\n", pci->class);

	/* find the configuration for the specific device */
	cfg = snd_intel_dsp_find_config(pci, config_table, ARRAY_SIZE(config_table));
	if (!cfg)
		return SND_INTEL_DSP_DRIVER_ANY;

	if (cfg->flags & FLAG_SOF) {
		if (cfg->flags & FLAG_SOF_ONLY_IF_SOUNDWIRE &&
		    snd_intel_dsp_check_soundwire(pci) > 0) {
			dev_info(&pci->dev, "SoundWire enabled on CannonLake+ platform, using SOF driver\n");
			return SND_INTEL_DSP_DRIVER_SOF;
		}
		if (cfg->flags & FLAG_SOF_ONLY_IF_DMIC &&
		    snd_intel_dsp_check_dmic(pci)) {
			dev_info(&pci->dev, "Digital mics found on Skylake+ platform, using SOF driver\n");
			return SND_INTEL_DSP_DRIVER_SOF;
		}
		if (!(cfg->flags & FLAG_SOF_ONLY_IF_DMIC_OR_SOUNDWIRE))
			return SND_INTEL_DSP_DRIVER_SOF;
	}


	if (cfg->flags & FLAG_SST) {
		if (cfg->flags & FLAG_SST_ONLY_IF_DMIC) {
			if (snd_intel_dsp_check_dmic(pci)) {
				dev_info(&pci->dev, "Digital mics found on Skylake+ platform, using SST driver\n");
				return SND_INTEL_DSP_DRIVER_SST;
			}
		} else {
			return SND_INTEL_DSP_DRIVER_SST;
		}
	}

	return SND_INTEL_DSP_DRIVER_LEGACY;
}
EXPORT_SYMBOL_GPL(snd_intel_dsp_driver_probe);

/* Should we default to SOF or SST for BYT/CHT ? */
#if IS_ENABLED(CONFIG_SND_INTEL_BYT_PREFER_SOF) || \
    !IS_ENABLED(CONFIG_SND_SST_ATOM_HIFI2_PLATFORM_ACPI)
#define FLAG_SST_OR_SOF_BYT	FLAG_SOF
#else
#define FLAG_SST_OR_SOF_BYT	FLAG_SST
#endif

/*
 * configuration table
 * - the order of similar ACPI ID entries is important!
 * - the first successful match will win
 */
static const struct config_entry acpi_config_table[] = {
#if IS_ENABLED(CONFIG_SND_SST_ATOM_HIFI2_PLATFORM_ACPI) || \
    IS_ENABLED(CONFIG_SND_SOC_SOF_BAYTRAIL)
/* BayTrail */
	{
		.flags = FLAG_SST_OR_SOF_BYT,
		.acpi_hid = "80860F28",
	},
/* CherryTrail */
	{
		.flags = FLAG_SST_OR_SOF_BYT,
		.acpi_hid = "808622A8",
	},
#endif
/* Broadwell */
#if IS_ENABLED(CONFIG_SND_SOC_INTEL_CATPT)
	{
		.flags = FLAG_SST,
		.acpi_hid = "INT3438"
	},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SOF_BROADWELL)
	{
		.flags = FLAG_SOF,
		.acpi_hid = "INT3438"
	},
#endif
/* Haswell - not supported by SOF but added for consistency */
#if IS_ENABLED(CONFIG_SND_SOC_INTEL_CATPT)
	{
		.flags = FLAG_SST,
		.acpi_hid = "INT33C8"
	},
#endif
};

static const struct config_entry *snd_intel_acpi_dsp_find_config(const u8 acpi_hid[ACPI_ID_LEN],
								 const struct config_entry *table,
								 u32 len)
{
	for (; len > 0; len--, table++) {
		if (memcmp(table->acpi_hid, acpi_hid, ACPI_ID_LEN))
			continue;
		if (table->dmi_table && !dmi_check_system(table->dmi_table))
			continue;
		return table;
	}
	return NULL;
}

int snd_intel_acpi_dsp_driver_probe(struct device *dev, const u8 acpi_hid[ACPI_ID_LEN])
{
	const struct config_entry *cfg;

	if (dsp_driver > SND_INTEL_DSP_DRIVER_LEGACY && dsp_driver <= SND_INTEL_DSP_DRIVER_LAST)
		return dsp_driver;

	if (dsp_driver == SND_INTEL_DSP_DRIVER_LEGACY) {
		dev_warn(dev, "dsp_driver parameter %d not supported, using automatic detection\n",
			 SND_INTEL_DSP_DRIVER_LEGACY);
	}

	/* find the configuration for the specific device */
	cfg = snd_intel_acpi_dsp_find_config(acpi_hid,  acpi_config_table,
					     ARRAY_SIZE(acpi_config_table));
	if (!cfg)
		return SND_INTEL_DSP_DRIVER_ANY;

	if (cfg->flags & FLAG_SST)
		return SND_INTEL_DSP_DRIVER_SST;

	if (cfg->flags & FLAG_SOF)
		return SND_INTEL_DSP_DRIVER_SOF;

	return SND_INTEL_DSP_DRIVER_SST;
}
EXPORT_SYMBOL_GPL(snd_intel_acpi_dsp_driver_probe);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel DSP config driver");
MODULE_IMPORT_NS(SND_INTEL_SOUNDWIRE_ACPI);
