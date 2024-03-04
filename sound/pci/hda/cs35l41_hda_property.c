// SPDX-License-Identifier: GPL-2.0
//
// CS35L41 ALSA HDA Property driver
//
// Copyright 2023 Cirrus Logic, Inc.
//
// Author: Stefan Binding <sbinding@opensource.cirrus.com>

#include <linux/acpi.h>
#include <linux/gpio/consumer.h>
#include <linux/string.h>
#include "cs35l41_hda_property.h"
#include <linux/spi/spi.h>

#define MAX_AMPS 4

struct cs35l41_config {
	const char *ssid;
	int num_amps;
	enum {
		INTERNAL,
		EXTERNAL
	} boost_type;
	u8 channel[MAX_AMPS];
	int reset_gpio_index; /* -1 if no reset gpio */
	int spkid_gpio_index; /* -1 if no spkid gpio */
	int cs_gpio_index; /* -1 if no cs gpio, or cs-gpios already exists, max num amps == 2 */
	int boost_ind_nanohenry; /* Required if boost_type == Internal */
	int boost_peak_milliamp; /* Required if boost_type == Internal */
	int boost_cap_microfarad; /* Required if boost_type == Internal */
};

static const struct cs35l41_config cs35l41_config_table[] = {
	{ "10280B27", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, 2, 0, 1000, 4500, 24 },
	{ "10280B28", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, 2, 0, 1000, 4500, 24 },
	{ "10280BEB", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, -1, 0, 0, 0, 0 },
	{ "10280C4D", 4, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, CS35L41_LEFT, CS35L41_RIGHT }, 0, 1, -1, 1000, 4500, 24 },
/*
 * Device 103C89C6 does have _DSD, however it is setup to use the wrong boost type.
 * We can override the _DSD to correct the boost type here.
 * Since this laptop has valid ACPI, we do not need to handle cs-gpios, since that already exists
 * in the ACPI. The Reset GPIO is also valid, so we can use the Reset defined in _DSD.
 */
	{ "103C89C6", 2, INTERNAL, { CS35L41_RIGHT, CS35L41_LEFT, 0, 0 }, -1, -1, -1, 1000, 4500, 24 },
	{ "103C8A28", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8A29", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8A2A", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8A2B", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8A2C", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8A2D", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8A2E", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8A30", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8A31", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8A6E", 4, EXTERNAL, { CS35L41_LEFT, CS35L41_LEFT, CS35L41_RIGHT, CS35L41_RIGHT }, 0, -1, -1, 0, 0, 0 },
	{ "103C8BB3", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8BB4", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8BDD", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8BDE", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8BDF", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8BE0", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8BE1", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8BE2", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8BE3", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8BE5", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8BE6", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8BE7", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8BE8", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8BE9", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8B3A", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8C15", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4000, 24 },
	{ "103C8C16", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4000, 24 },
	{ "103C8C17", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4000, 24 },
	{ "103C8C4F", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8C50", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8C51", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8CDD", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4100, 24 },
	{ "103C8CDE", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 3900, 24 },
	{ "104312AF", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, 2, 0, 1000, 4500, 24 },
	{ "10431433", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4500, 24 },
	{ "10431463", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4500, 24 },
	{ "10431473", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, -1, 0, 1000, 4500, 24 },
	{ "10431483", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, -1, 0, 1000, 4500, 24 },
	{ "10431493", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, 2, 0, 1000, 4500, 24 },
	{ "104314D3", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, 2, 0, 1000, 4500, 24 },
	{ "104314E3", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4500, 24 },
	{ "10431503", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4500, 24 },
	{ "10431533", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4500, 24 },
	{ "10431573", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, 2, 0, 1000, 4500, 24 },
	{ "10431663", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, -1, 0, 1000, 4500, 24 },
	{ "10431683", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 0, 0, 0 },
	{ "104316A3", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, 2, 0, 0, 0, 0 },
	{ "104316D3", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, 2, 0, 0, 0, 0 },
	{ "104316F3", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, 2, 0, 0, 0, 0 },
	{ "104317F3", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4500, 24 },
	{ "10431863", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, 2, 0, 1000, 4500, 24 },
	{ "104318D3", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 0, 0, 0 },
	{ "10431A83", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4500, 24 },
	{ "10431C9F", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, 2, 0, 1000, 4500, 24 },
	{ "10431CAF", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, 2, 0, 1000, 4500, 24 },
	{ "10431CCF", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, 2, 0, 1000, 4500, 24 },
	{ "10431CDF", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, 2, 0, 1000, 4500, 24 },
	{ "10431CEF", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, 2, 0, 1000, 4500, 24 },
	{ "10431D1F", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4500, 24 },
	{ "10431DA2", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, 2, 0, 0, 0, 0 },
	{ "10431E02", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, 2, 0, 0, 0, 0 },
	{ "10431E12", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 0, 0, 0 },
	{ "10431EE2", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, -1, -1, 0, 0, 0 },
	{ "10431F12", 2, INTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 1000, 4500, 24 },
	{ "10431F1F", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, -1, 0, 0, 0, 0 },
	{ "10431F62", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 1, 2, 0, 0, 0, 0 },
	{ "17AA386F", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, -1, -1, 0, 0, 0 },
	{ "17AA38A9", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 2, -1, 0, 0, 0 },
	{ "17AA38AB", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 2, -1, 0, 0, 0 },
	{ "17AA38B4", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 0, 0, 0 },
	{ "17AA38B5", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 0, 0, 0 },
	{ "17AA38B6", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 0, 0, 0 },
	{ "17AA38B7", 2, EXTERNAL, { CS35L41_LEFT, CS35L41_RIGHT, 0, 0 }, 0, 1, -1, 0, 0, 0 },
	{}
};

static int cs35l41_add_gpios(struct cs35l41_hda *cs35l41, struct device *physdev, int reset_gpio,
			     int spkid_gpio, int cs_gpio_index, int num_amps)
{
	struct acpi_gpio_mapping *gpio_mapping = NULL;
	struct acpi_gpio_params *reset_gpio_params = NULL;
	struct acpi_gpio_params *spkid_gpio_params = NULL;
	struct acpi_gpio_params *cs_gpio_params = NULL;
	unsigned int num_entries = 0;
	unsigned int reset_index, spkid_index, csgpio_index;
	int i;

	/*
	 * GPIO Mapping only needs to be done once, since it would be available for subsequent amps
	 */
	if (cs35l41->dacpi->driver_gpios)
		return 0;

	if (reset_gpio >= 0) {
		reset_index = num_entries;
		num_entries++;
	}

	if (spkid_gpio >= 0) {
		spkid_index = num_entries;
		num_entries++;
	}

	if ((cs_gpio_index >= 0)  && (num_amps == 2)) {
		csgpio_index = num_entries;
		num_entries++;
	}

	if (!num_entries)
		return 0;

	/* must include termination entry */
	num_entries++;

	gpio_mapping = devm_kcalloc(physdev, num_entries, sizeof(struct acpi_gpio_mapping),
				    GFP_KERNEL);

	if (!gpio_mapping)
		goto err;

	if (reset_gpio >= 0) {
		gpio_mapping[reset_index].name = "reset-gpios";
		reset_gpio_params = devm_kcalloc(physdev, num_amps, sizeof(struct acpi_gpio_params),
						 GFP_KERNEL);
		if (!reset_gpio_params)
			goto err;

		for (i = 0; i < num_amps; i++)
			reset_gpio_params[i].crs_entry_index = reset_gpio;

		gpio_mapping[reset_index].data = reset_gpio_params;
		gpio_mapping[reset_index].size = num_amps;
	}

	if (spkid_gpio >= 0) {
		gpio_mapping[spkid_index].name = "spk-id-gpios";
		spkid_gpio_params = devm_kcalloc(physdev, num_amps, sizeof(struct acpi_gpio_params),
						 GFP_KERNEL);
		if (!spkid_gpio_params)
			goto err;

		for (i = 0; i < num_amps; i++)
			spkid_gpio_params[i].crs_entry_index = spkid_gpio;

		gpio_mapping[spkid_index].data = spkid_gpio_params;
		gpio_mapping[spkid_index].size = num_amps;
	}

	if ((cs_gpio_index >= 0) && (num_amps == 2)) {
		gpio_mapping[csgpio_index].name = "cs-gpios";
		/* only one GPIO CS is supported without using _DSD, obtained using index 0 */
		cs_gpio_params = devm_kzalloc(physdev, sizeof(struct acpi_gpio_params), GFP_KERNEL);
		if (!cs_gpio_params)
			goto err;

		cs_gpio_params->crs_entry_index = cs_gpio_index;

		gpio_mapping[csgpio_index].data = cs_gpio_params;
		gpio_mapping[csgpio_index].size = 1;
	}

	return devm_acpi_dev_add_driver_gpios(physdev, gpio_mapping);
err:
	devm_kfree(physdev, gpio_mapping);
	devm_kfree(physdev, reset_gpio_params);
	devm_kfree(physdev, spkid_gpio_params);
	devm_kfree(physdev, cs_gpio_params);
	return -ENOMEM;
}

static int generic_dsd_config(struct cs35l41_hda *cs35l41, struct device *physdev, int id,
			      const char *hid)
{
	struct cs35l41_hw_cfg *hw_cfg = &cs35l41->hw_cfg;
	const struct cs35l41_config *cfg;
	struct gpio_desc *cs_gpiod;
	struct spi_device *spi;
	bool dsd_found;
	int ret;
	int i;

	for (cfg = cs35l41_config_table; cfg->ssid; cfg++) {
		if (!strcasecmp(cfg->ssid, cs35l41->acpi_subsystem_id))
			break;
	}

	if (!cfg->ssid)
		return -ENOENT;

	if (!cs35l41->dacpi || cs35l41->dacpi != ACPI_COMPANION(physdev)) {
		dev_err(cs35l41->dev, "ACPI Device does not match, cannot override _DSD.\n");
		return -ENODEV;
	}

	dev_info(cs35l41->dev, "Adding DSD properties for %s\n", cs35l41->acpi_subsystem_id);

	dsd_found = acpi_dev_has_props(cs35l41->dacpi);

	if (!dsd_found) {
		ret = cs35l41_add_gpios(cs35l41, physdev, cfg->reset_gpio_index,
						 cfg->spkid_gpio_index, cfg->cs_gpio_index,
						 cfg->num_amps);
		if (ret) {
			dev_err(cs35l41->dev, "Error adding GPIO mapping: %d\n", ret);
			return ret;
		}
	} else if (cfg->reset_gpio_index >= 0 || cfg->spkid_gpio_index >= 0) {
		dev_warn(cs35l41->dev, "Cannot add Reset/Speaker ID/SPI CS GPIO Mapping, "
			 "_DSD already exists.\n");
	}

	if (cs35l41->control_bus == SPI) {
		cs35l41->index = id;

		/*
		 * Manually set the Chip Select for the second amp <cs_gpio_index> in the node.
		 * This is only supported for systems with 2 amps, since we cannot expand the
		 * default number of chip selects without using cs-gpios
		 * The CS GPIO must be set high prior to communicating with the first amp (which
		 * uses a native chip select), to ensure the second amp does not clash with the
		 * first.
		 */
		if (IS_ENABLED(CONFIG_SPI) && cfg->cs_gpio_index >= 0) {
			spi = to_spi_device(cs35l41->dev);

			if (cfg->num_amps != 2) {
				dev_warn(cs35l41->dev,
					 "Cannot update SPI CS, Number of Amps (%d) != 2\n",
					 cfg->num_amps);
			} else if (dsd_found) {
				dev_warn(cs35l41->dev,
					"Cannot update SPI CS, _DSD already exists.\n");
			} else {
				/*
				 * This is obtained using driver_gpios, since only one GPIO for CS
				 * exists, this can be obtained using index 0.
				 */
				cs_gpiod = gpiod_get_index(physdev, "cs", 0, GPIOD_OUT_LOW);
				if (IS_ERR(cs_gpiod)) {
					dev_err(cs35l41->dev,
						"Unable to get Chip Select GPIO descriptor\n");
					return PTR_ERR(cs_gpiod);
				}
				if (id == 1) {
					spi_set_csgpiod(spi, 0, cs_gpiod);
					cs35l41->cs_gpio = cs_gpiod;
				} else {
					gpiod_set_value_cansleep(cs_gpiod, true);
					gpiod_put(cs_gpiod);
				}
				spi_setup(spi);
			}
		}
	} else {
		if (cfg->num_amps > 2)
			/*
			 * i2c addresses for 3/4 amps are used in order: 0x40, 0x41, 0x42, 0x43,
			 * subtracting 0x40 would give zero-based index
			 */
			cs35l41->index = id - 0x40;
		else
			/* i2c addr 0x40 for first amp (always), 0x41/0x42 for 2nd amp */
			cs35l41->index = id == 0x40 ? 0 : 1;
	}

	cs35l41->reset_gpio = fwnode_gpiod_get_index(acpi_fwnode_handle(cs35l41->dacpi), "reset",
						     cs35l41->index, GPIOD_OUT_LOW,
						     "cs35l41-reset");
	cs35l41->speaker_id = cs35l41_get_speaker_id(physdev, cs35l41->index, cfg->num_amps, -1);

	hw_cfg->spk_pos = cfg->channel[cs35l41->index];

	cs35l41->channel_index = 0;
	for (i = 0; i < cs35l41->index; i++)
		if (cfg->channel[i] == hw_cfg->spk_pos)
			cs35l41->channel_index++;

	if (cfg->boost_type == INTERNAL) {
		hw_cfg->bst_type = CS35L41_INT_BOOST;
		hw_cfg->bst_ind = cfg->boost_ind_nanohenry;
		hw_cfg->bst_ipk = cfg->boost_peak_milliamp;
		hw_cfg->bst_cap = cfg->boost_cap_microfarad;
		hw_cfg->gpio1.func = CS35L41_NOT_USED;
		hw_cfg->gpio1.valid = true;
	} else {
		hw_cfg->bst_type = CS35L41_EXT_BOOST;
		hw_cfg->bst_ind = -1;
		hw_cfg->bst_ipk = -1;
		hw_cfg->bst_cap = -1;
		hw_cfg->gpio1.func = CS35l41_VSPK_SWITCH;
		hw_cfg->gpio1.valid = true;
	}

	hw_cfg->gpio2.func = CS35L41_INTERRUPT;
	hw_cfg->gpio2.valid = true;
	hw_cfg->valid = true;

	return 0;
}

/*
 * Systems 103C8C66, 103C8C67, 103C8C68, 103C8C6A use a dual speaker id system - each speaker has
 * its own speaker id.
 */
static int hp_i2c_int_2amp_dual_spkid(struct cs35l41_hda *cs35l41, struct device *physdev, int id,
				      const char *hid)
{
	struct cs35l41_hw_cfg *hw_cfg = &cs35l41->hw_cfg;

	/* If _DSD exists for this laptop, we cannot support it through here */
	if (acpi_dev_has_props(cs35l41->dacpi))
		return -ENOENT;

	/* check I2C address to assign the index */
	cs35l41->index = id == 0x40 ? 0 : 1;
	cs35l41->channel_index = 0;
	cs35l41->reset_gpio = gpiod_get_index(physdev, NULL, 0, GPIOD_OUT_HIGH);
	if (cs35l41->index == 0)
		cs35l41->speaker_id = cs35l41_get_speaker_id(physdev, 0, 0, 1);
	else
		cs35l41->speaker_id = cs35l41_get_speaker_id(physdev, 0, 0, 2);
	hw_cfg->spk_pos = cs35l41->index;
	hw_cfg->gpio2.func = CS35L41_INTERRUPT;
	hw_cfg->gpio2.valid = true;
	hw_cfg->valid = true;

	hw_cfg->bst_type = CS35L41_INT_BOOST;
	hw_cfg->bst_ind = 1000;
	hw_cfg->bst_ipk = 4100;
	hw_cfg->bst_cap = 24;
	hw_cfg->gpio1.func = CS35L41_NOT_USED;
	hw_cfg->gpio1.valid = true;

	return 0;
}

/*
 * Device CLSA010(0/1) doesn't have _DSD so a gpiod_get by the label reset won't work.
 * And devices created by serial-multi-instantiate don't have their device struct
 * pointing to the correct fwnode, so acpi_dev must be used here.
 * And devm functions expect that the device requesting the resource has the correct
 * fwnode.
 */
static int lenovo_legion_no_acpi(struct cs35l41_hda *cs35l41, struct device *physdev, int id,
				 const char *hid)
{
	struct cs35l41_hw_cfg *hw_cfg = &cs35l41->hw_cfg;

	/* check I2C address to assign the index */
	cs35l41->index = id == 0x40 ? 0 : 1;
	cs35l41->channel_index = 0;
	cs35l41->reset_gpio = gpiod_get_index(physdev, NULL, 0, GPIOD_OUT_HIGH);
	cs35l41->speaker_id = cs35l41_get_speaker_id(physdev, 0, 0, 2);
	hw_cfg->spk_pos = cs35l41->index;
	hw_cfg->gpio2.func = CS35L41_INTERRUPT;
	hw_cfg->gpio2.valid = true;
	hw_cfg->valid = true;

	if (strcmp(hid, "CLSA0100") == 0) {
		hw_cfg->bst_type = CS35L41_EXT_BOOST_NO_VSPK_SWITCH;
	} else if (strcmp(hid, "CLSA0101") == 0) {
		hw_cfg->bst_type = CS35L41_EXT_BOOST;
		hw_cfg->gpio1.func = CS35l41_VSPK_SWITCH;
		hw_cfg->gpio1.valid = true;
	}

	return 0;
}

struct cs35l41_prop_model {
	const char *hid;
	const char *ssid;
	int (*add_prop)(struct cs35l41_hda *cs35l41, struct device *physdev, int id,
			const char *hid);
};

static const struct cs35l41_prop_model cs35l41_prop_model_table[] = {
	{ "CLSA0100", NULL, lenovo_legion_no_acpi },
	{ "CLSA0101", NULL, lenovo_legion_no_acpi },
	{ "CSC3551", "10280B27", generic_dsd_config },
	{ "CSC3551", "10280B28", generic_dsd_config },
	{ "CSC3551", "10280BEB", generic_dsd_config },
	{ "CSC3551", "10280C4D", generic_dsd_config },
	{ "CSC3551", "103C89C6", generic_dsd_config },
	{ "CSC3551", "103C8A28", generic_dsd_config },
	{ "CSC3551", "103C8A29", generic_dsd_config },
	{ "CSC3551", "103C8A2A", generic_dsd_config },
	{ "CSC3551", "103C8A2B", generic_dsd_config },
	{ "CSC3551", "103C8A2C", generic_dsd_config },
	{ "CSC3551", "103C8A2D", generic_dsd_config },
	{ "CSC3551", "103C8A2E", generic_dsd_config },
	{ "CSC3551", "103C8A30", generic_dsd_config },
	{ "CSC3551", "103C8A31", generic_dsd_config },
	{ "CSC3551", "103C8A6E", generic_dsd_config },
	{ "CSC3551", "103C8BB3", generic_dsd_config },
	{ "CSC3551", "103C8BB4", generic_dsd_config },
	{ "CSC3551", "103C8BDD", generic_dsd_config },
	{ "CSC3551", "103C8BDE", generic_dsd_config },
	{ "CSC3551", "103C8BDF", generic_dsd_config },
	{ "CSC3551", "103C8BE0", generic_dsd_config },
	{ "CSC3551", "103C8BE1", generic_dsd_config },
	{ "CSC3551", "103C8BE2", generic_dsd_config },
	{ "CSC3551", "103C8BE3", generic_dsd_config },
	{ "CSC3551", "103C8BE5", generic_dsd_config },
	{ "CSC3551", "103C8BE6", generic_dsd_config },
	{ "CSC3551", "103C8BE7", generic_dsd_config },
	{ "CSC3551", "103C8BE8", generic_dsd_config },
	{ "CSC3551", "103C8BE9", generic_dsd_config },
	{ "CSC3551", "103C8B3A", generic_dsd_config },
	{ "CSC3551", "103C8C15", generic_dsd_config },
	{ "CSC3551", "103C8C16", generic_dsd_config },
	{ "CSC3551", "103C8C17", generic_dsd_config },
	{ "CSC3551", "103C8C4F", generic_dsd_config },
	{ "CSC3551", "103C8C50", generic_dsd_config },
	{ "CSC3551", "103C8C51", generic_dsd_config },
	{ "CSC3551", "103C8C66", hp_i2c_int_2amp_dual_spkid },
	{ "CSC3551", "103C8C67", hp_i2c_int_2amp_dual_spkid },
	{ "CSC3551", "103C8C68", hp_i2c_int_2amp_dual_spkid },
	{ "CSC3551", "103C8C6A", hp_i2c_int_2amp_dual_spkid },
	{ "CSC3551", "103C8CDD", generic_dsd_config },
	{ "CSC3551", "103C8CDE", generic_dsd_config },
	{ "CSC3551", "104312AF", generic_dsd_config },
	{ "CSC3551", "10431433", generic_dsd_config },
	{ "CSC3551", "10431463", generic_dsd_config },
	{ "CSC3551", "10431473", generic_dsd_config },
	{ "CSC3551", "10431483", generic_dsd_config },
	{ "CSC3551", "10431493", generic_dsd_config },
	{ "CSC3551", "104314D3", generic_dsd_config },
	{ "CSC3551", "104314E3", generic_dsd_config },
	{ "CSC3551", "10431503", generic_dsd_config },
	{ "CSC3551", "10431533", generic_dsd_config },
	{ "CSC3551", "10431573", generic_dsd_config },
	{ "CSC3551", "10431663", generic_dsd_config },
	{ "CSC3551", "10431683", generic_dsd_config },
	{ "CSC3551", "104316A3", generic_dsd_config },
	{ "CSC3551", "104316D3", generic_dsd_config },
	{ "CSC3551", "104316F3", generic_dsd_config },
	{ "CSC3551", "104317F3", generic_dsd_config },
	{ "CSC3551", "10431863", generic_dsd_config },
	{ "CSC3551", "104318D3", generic_dsd_config },
	{ "CSC3551", "10431A83", generic_dsd_config },
	{ "CSC3551", "10431C9F", generic_dsd_config },
	{ "CSC3551", "10431CAF", generic_dsd_config },
	{ "CSC3551", "10431CCF", generic_dsd_config },
	{ "CSC3551", "10431CDF", generic_dsd_config },
	{ "CSC3551", "10431CEF", generic_dsd_config },
	{ "CSC3551", "10431D1F", generic_dsd_config },
	{ "CSC3551", "10431DA2", generic_dsd_config },
	{ "CSC3551", "10431E02", generic_dsd_config },
	{ "CSC3551", "10431E12", generic_dsd_config },
	{ "CSC3551", "10431EE2", generic_dsd_config },
	{ "CSC3551", "10431F12", generic_dsd_config },
	{ "CSC3551", "10431F1F", generic_dsd_config },
	{ "CSC3551", "10431F62", generic_dsd_config },
	{ "CSC3551", "17AA386F", generic_dsd_config },
	{ "CSC3551", "17AA38A9", generic_dsd_config },
	{ "CSC3551", "17AA38AB", generic_dsd_config },
	{ "CSC3551", "17AA38B4", generic_dsd_config },
	{ "CSC3551", "17AA38B5", generic_dsd_config },
	{ "CSC3551", "17AA38B6", generic_dsd_config },
	{ "CSC3551", "17AA38B7", generic_dsd_config },
	{}
};

int cs35l41_add_dsd_properties(struct cs35l41_hda *cs35l41, struct device *physdev, int id,
			       const char *hid)
{
	const struct cs35l41_prop_model *model;

	for (model = cs35l41_prop_model_table; model->hid; model++) {
		if (!strcmp(model->hid, hid) &&
		    (!model->ssid ||
		     (cs35l41->acpi_subsystem_id &&
		      !strcasecmp(model->ssid, cs35l41->acpi_subsystem_id))))
			return model->add_prop(cs35l41, physdev, id, hid);
	}

	return -ENOENT;
}
