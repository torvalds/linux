/* SPDX-License-Identifier: GPL-2.0 */
/*
 * soc-intel-quirks.h - prototypes for quirk autodetection
 *
 * Copyright (c) 2019, Intel Corporation.
 *
 */

#ifndef _SND_SOC_INTEL_QUIRKS_H
#define _SND_SOC_INTEL_QUIRKS_H

#if IS_ENABLED(CONFIG_X86)

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/iosf_mbi.h>

#define ICPU(model)	{ X86_VENDOR_INTEL, 6, model, X86_FEATURE_ANY, }

#define SOC_INTEL_IS_CPU(soc, type)				\
static inline bool soc_intel_is_##soc(void)			\
{								\
	static const struct x86_cpu_id soc##_cpu_ids[] = {	\
		ICPU(type),					\
		{}						\
	};							\
	const struct x86_cpu_id *id;				\
								\
	id = x86_match_cpu(soc##_cpu_ids);			\
	if (id)							\
		return true;					\
	return false;						\
}

SOC_INTEL_IS_CPU(byt, INTEL_FAM6_ATOM_SILVERMONT);
SOC_INTEL_IS_CPU(cht, INTEL_FAM6_ATOM_AIRMONT);
SOC_INTEL_IS_CPU(apl, INTEL_FAM6_ATOM_GOLDMONT);
SOC_INTEL_IS_CPU(glk, INTEL_FAM6_ATOM_GOLDMONT_PLUS);

static inline bool soc_intel_is_byt_cr(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int status = 0;

	if (!soc_intel_is_byt())
		return false;

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

static inline bool soc_intel_is_byt(void)
{
	return false;
}

static inline bool soc_intel_is_cht(void)
{
	return false;
}

static inline bool soc_intel_is_apl(void)
{
	return false;
}

static inline bool soc_intel_is_glk(void)
{
	return false;
}

#endif

 #endif /* _SND_SOC_INTEL_QUIRKS_H */
