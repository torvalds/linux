/*
 * MMC definitions for OMAP2
 *
 * Copyright (C) 2006 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * struct omap_hsmmc_dev_attr.flags possibilities
 *
 * OMAP_HSMMC_SUPPORTS_DUAL_VOLT: Some HSMMC controller instances can
 *    operate with either 1.8Vdc or 3.0Vdc card voltages; this flag
 *    should be set if this is the case.  See for example Section 22.5.3
 *    "MMC/SD/SDIO1 Bus Voltage Selection" of the OMAP34xx Multimedia
 *    Device Silicon Revision 3.1.x Revision ZR (July 2011) (SWPU223R).
 *
 * OMAP_HSMMC_BROKEN_MULTIBLOCK_READ: Multiple-block read transfers
 *    don't work correctly on some MMC controller instances on some
 *    OMAP3 SoCs; this flag should be set if this is the case.  See
 *    for example Advisory 2.1.1.128 "MMC: Multiple Block Read
 *    Operation Issue" in _OMAP3530/3525/3515/3503 Silicon Errata_
 *    Revision F (October 2010) (SPRZ278F).
 */
#define OMAP_HSMMC_SUPPORTS_DUAL_VOLT		BIT(0)
#define OMAP_HSMMC_BROKEN_MULTIBLOCK_READ	BIT(1)
#define OMAP_HSMMC_SWAKEUP_MISSING		BIT(2)

struct omap_hsmmc_dev_attr {
	u8 flags;
};

struct mmc_card;

struct omap_hsmmc_platform_data {
	/* back-link to device */
	struct device *dev;

	/* set if your board has components or wiring that limits the
	 * maximum frequency on the MMC bus */
	unsigned int max_freq;

	/* Integrating attributes from the omap_hwmod layer */
	u8 controller_flags;

	/* Register offset deviation */
	u16 reg_offset;

	/*
	 * 4/8 wires and any additional host capabilities
	 * need to OR'd all capabilities (ref. linux/mmc/host.h)
	 */
	u32 caps;	/* Used for the MMC driver on 2430 and later */
	u32 pm_caps;	/* PM capabilities of the mmc */

	/* use the internal clock */
	unsigned internal_clock:1;

	/* nonremovable e.g. eMMC */
	unsigned nonremovable:1;

	/* eMMC does not handle power off when not in sleep state */
	unsigned no_regulator_off_init:1;

	/* we can put the features above into this variable */
#define HSMMC_HAS_PBIAS		(1 << 0)
#define HSMMC_HAS_UPDATED_RESET	(1 << 1)
#define HSMMC_HAS_HSPE_SUPPORT	(1 << 2)
	unsigned features;

	int gpio_cd;			/* gpio (card detect) */
	int gpio_cod;			/* gpio (cover detect) */
	int gpio_wp;			/* gpio (write protect) */

	int (*set_power)(struct device *dev, int power_on, int vdd);
	void (*remux)(struct device *dev, int power_on);
	/* Call back before enabling / disabling regulators */
	void (*before_set_reg)(struct device *dev, int power_on, int vdd);
	/* Call back after enabling / disabling regulators */
	void (*after_set_reg)(struct device *dev, int power_on, int vdd);
	/* if we have special card, init it using this callback */
	void (*init_card)(struct mmc_card *card);

	const char *name;
	u32 ocr_mask;
};
