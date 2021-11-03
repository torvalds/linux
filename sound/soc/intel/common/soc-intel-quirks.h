/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * soc-intel-quirks.h - prototypes for quirk autodetection
 *
 * Copyright (c) 2019, Intel Corporation.
 *
 */

#ifndef _SND_SOC_INTEL_QUIRKS_H
#define _SND_SOC_INTEL_QUIRKS_H

#include <linux/platform_data/x86/soc.h>

#if IS_ENABLED(CONFIG_X86)

#include <linux/dmi.h>
#include <asm/iosf_mbi.h>

static inline bool soc_intel_is_byt_cr(struct platform_device *pdev)
{
	/*
	 * List of systems which:
	 * 1. Use a non CR version of the Bay Trail SoC
	 * 2. Contain at least 6 interrupt resources so that the
	 *    platform_get_resource(pdev, IORESOURCE_IRQ, 5) check below
	 *    succeeds
	 * 3. Despite 1. and 2. still have their IPC IRQ at index 0 rather then 5
	 *
	 * This needs to be here so that it can be shared between the SST and
	 * SOF drivers. We rely on the compiler to optimize this out in files
	 * where soc_intel_is_byt_cr is not used.
	 */
	static const struct dmi_system_id force_bytcr_table[] = {
		{	/* Lenovo Yoga Tablet 2 series */
			.matches = {
				DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
				DMI_MATCH(DMI_PRODUCT_FAMILY, "YOGATablet2"),
			},
		},
		{}
	};
	struct device *dev = &pdev->dev;
	int status = 0;

	if (!soc_intel_is_byt())
		return false;

	if (dmi_check_system(force_bytcr_table))
		return true;

	if (iosf_mbi_available()) {
		u32 bios_status;

		status = iosf_mbi_read(BT_MBI_UNIT_PMC, /* 0x04 PUNIT */
				       MBI_REG_READ, /* 0x10 */
				       0x006, /* BIOS_CONFIG */
				       &bios_status);

		if (status) {
			dev_err(dev, "could not read PUNIT BIOS_CONFIG\n");
		} else {
			/* bits 26:27 mirror PMIC options */
			bios_status = (bios_status >> 26) & 3;

			if (bios_status == 1 || bios_status == 3) {
				dev_info(dev, "Detected Baytrail-CR platform\n");
				return true;
			}

			dev_info(dev, "BYT-CR not detected\n");
		}
	} else {
		dev_info(dev, "IOSF_MBI not available, no BYT-CR detection\n");
	}

	if (!platform_get_resource(pdev, IORESOURCE_IRQ, 5)) {
		/*
		 * Some devices detected as BYT-T have only a single IRQ listed,
		 * causing platform_get_irq with index 5 to return -ENXIO.
		 * The correct IRQ in this case is at index 0, as on BYT-CR.
		 */
		dev_info(dev, "Falling back to Baytrail-CR platform\n");
		return true;
	}

	return false;
}

#else

static inline bool soc_intel_is_byt_cr(struct platform_device *pdev)
{
	return false;
}

#endif

#endif /* _SND_SOC_INTEL_QUIRKS_H */
