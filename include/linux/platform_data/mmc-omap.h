/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * MMC definitions for OMAP2
 *
 * Copyright (C) 2006 Nokia Corporation
 */

#define OMAP_MMC_MAX_SLOTS	2

struct mmc_card;

struct omap_mmc_platform_data {
	/* back-link to device */
	struct device *dev;

	/* number of slots per controller */
	unsigned nr_slots:2;

	/* set if your board has components or wiring that limits the
	 * maximum frequency on the MMC bus */
	unsigned int max_freq;

	/* switch the bus to a new slot */
	int (*switch_slot)(struct device *dev, int slot);
	/* initialize board-specific MMC functionality, can be NULL if
	 * not supported */
	int (*init)(struct device *dev);
	void (*cleanup)(struct device *dev);
	void (*shutdown)(struct device *dev);

	/* Return context loss count due to PM states changing */
	int (*get_context_loss_count)(struct device *dev);

	/* Integrating attributes from the omap_hwmod layer */
	u8 controller_flags;

	/* Register offset deviation */
	u16 reg_offset;

	struct omap_mmc_slot_data {

		/*
		 * 4/8 wires and any additional host capabilities
		 * need to OR'd all capabilities (ref. linux/mmc/host.h)
		 */
		u8  wires;	/* Used for the MMC driver on omap1 and 2420 */
		u32 caps;	/* Used for the MMC driver on 2430 and later */
		u32 pm_caps;	/* PM capabilities of the mmc */

		/*
		 * nomux means "standard" muxing is wrong on this board, and
		 * that board-specific code handled it before common init logic.
		 */
		unsigned nomux:1;

		/* switch pin can be for card detect (default) or card cover */
		unsigned cover:1;

		/* use the internal clock */
		unsigned internal_clock:1;

		/* nonremovable e.g. eMMC */
		unsigned nonremovable:1;

		/* Try to sleep or power off when possible */
		unsigned power_saving:1;

		/* If using power_saving and the MMC power is not to go off */
		unsigned no_off:1;

		/* eMMC does not handle power off when not in sleep state */
		unsigned no_regulator_off_init:1;

		/* Regulator off remapped to sleep */
		unsigned vcc_aux_disable_is_sleep:1;

		/* we can put the features above into this variable */
#define MMC_OMAP7XX		(1 << 3)
#define MMC_OMAP15XX		(1 << 4)
#define MMC_OMAP16XX		(1 << 5)
		unsigned features;

		int switch_pin;			/* gpio (card detect) */
		int gpio_wp;			/* gpio (write protect) */

		int (*set_bus_mode)(struct device *dev, int slot, int bus_mode);
		int (*set_power)(struct device *dev, int slot,
				 int power_on, int vdd);
		int (*get_ro)(struct device *dev, int slot);
		void (*remux)(struct device *dev, int slot, int power_on);
		/* Call back before enabling / disabling regulators */
		void (*before_set_reg)(struct device *dev, int slot,
				       int power_on, int vdd);
		/* Call back after enabling / disabling regulators */
		void (*after_set_reg)(struct device *dev, int slot,
				      int power_on, int vdd);
		/* if we have special card, init it using this callback */
		void (*init_card)(struct mmc_card *card);

		/* return MMC cover switch state, can be NULL if not supported.
		 *
		 * possible return values:
		 *   0 - closed
		 *   1 - open
		 */
		int (*get_cover_state)(struct device *dev, int slot);

		const char *name;
		u32 ocr_mask;

		/* Card detection IRQs */
		int card_detect_irq;
		int (*card_detect)(struct device *dev, int slot);

		unsigned int ban_openended:1;

	} slots[OMAP_MMC_MAX_SLOTS];
};
