/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2013-15, Intel Corporation. All rights reserved.
 */

#ifndef __LINUX_SND_SOC_ACPI_H
#define __LINUX_SND_SOC_ACPI_H

#include <linux/stddef.h>
#include <linux/acpi.h>
#include <linux/mod_devicetable.h>

struct snd_soc_acpi_package_context {
	char *name;           /* package name */
	int length;           /* number of elements */
	struct acpi_buffer *format;
	struct acpi_buffer *state;
	bool data_valid;
};

/* codec name is used in DAIs is i2c-<HID>:00 with HID being 8 chars */
#define SND_ACPI_I2C_ID_LEN (4 + ACPI_ID_LEN + 3 + 1)

#if IS_ENABLED(CONFIG_ACPI)
/* acpi match */
struct snd_soc_acpi_mach *
snd_soc_acpi_find_machine(struct snd_soc_acpi_mach *machines);

bool snd_soc_acpi_find_package_from_hid(const u8 hid[ACPI_ID_LEN],
				    struct snd_soc_acpi_package_context *ctx);

/* check all codecs */
struct snd_soc_acpi_mach *snd_soc_acpi_codec_list(void *arg);

#else
/* acpi match */
static inline struct snd_soc_acpi_mach *
snd_soc_acpi_find_machine(struct snd_soc_acpi_mach *machines)
{
	return NULL;
}

static inline bool
snd_soc_acpi_find_package_from_hid(const u8 hid[ACPI_ID_LEN],
				   struct snd_soc_acpi_package_context *ctx)
{
	return false;
}

/* check all codecs */
static inline struct snd_soc_acpi_mach *snd_soc_acpi_codec_list(void *arg)
{
	return NULL;
}
#endif

/**
 * snd_soc_acpi_mach_params: interface for machine driver configuration
 *
 * @acpi_ipc_irq_index: used for BYT-CR detection
 * @platform: string used for HDAudio codec support
 * @codec_mask: used for HDAudio support
 * @common_hdmi_codec_drv: use commom HDAudio HDMI codec driver
 * @link_mask: links enabled on the board
 * @links: array of link _ADR descriptors, null terminated
 * @num_dai_drivers: number of elements in @dai_drivers
 * @dai_drivers: pointer to dai_drivers, used e.g. in nocodec mode
 */
struct snd_soc_acpi_mach_params {
	u32 acpi_ipc_irq_index;
	const char *platform;
	u32 codec_mask;
	u32 dmic_num;
	bool common_hdmi_codec_drv;
	u32 link_mask;
	const struct snd_soc_acpi_link_adr *links;
	u32 num_dai_drivers;
	struct snd_soc_dai_driver *dai_drivers;
};

/**
 * snd_soc_acpi_endpoint - endpoint descriptor
 * @num: endpoint number (mandatory, unique per device)
 * @aggregated: 0 (independent) or 1 (logically grouped)
 * @group_position: zero-based order (only when @aggregated is 1)
 * @group_id: platform-unique group identifier (only when @aggregrated is 1)
 */
struct snd_soc_acpi_endpoint {
	u8 num;
	u8 aggregated;
	u8 group_position;
	u8 group_id;
};

/**
 * snd_soc_acpi_adr_device - descriptor for _ADR-enumerated device
 * @adr: 64 bit ACPI _ADR value
 * @num_endpoints: number of endpoints for this device
 * @endpoints: array of endpoints
 * @name_prefix: string used for codec controls
 */
struct snd_soc_acpi_adr_device {
	const u64 adr;
	const u8 num_endpoints;
	const struct snd_soc_acpi_endpoint *endpoints;
	const char *name_prefix;
};

/**
 * snd_soc_acpi_link_adr - ACPI-based list of _ADR enumerated devices
 * @mask: one bit set indicates the link this list applies to
 * @num_adr: ARRAY_SIZE of devices
 * @adr_d: array of devices
 *
 * The number of devices per link can be more than 1, e.g. in SoundWire
 * multi-drop configurations.
 */

struct snd_soc_acpi_link_adr {
	const u32 mask;
	const u32 num_adr;
	const struct snd_soc_acpi_adr_device *adr_d;
};

/**
 * snd_soc_acpi_mach: ACPI-based machine descriptor. Most of the fields are
 * related to the hardware, except for the firmware and topology file names.
 * A platform supported by legacy and Sound Open Firmware (SOF) would expose
 * all firmware/topology related fields.
 *
 * @id: ACPI ID (usually the codec's) used to find a matching machine driver.
 * @comp_ids: list of compatible audio codecs using the same machine driver,
 * firmware and topology
 * @link_mask: describes required board layout, e.g. for SoundWire.
 * @links: array of link _ADR descriptors, null terminated.
 * @drv_name: machine driver name
 * @fw_filename: firmware file name. Used when SOF is not enabled.
 * @board: board name
 * @machine_quirk: pointer to quirk, usually based on DMI information when
 * ACPI ID alone is not sufficient, wrong or misleading
 * @quirk_data: data used to uniquely identify a machine, usually a list of
 * audio codecs whose presence if checked with ACPI
 * @pdata: intended for platform data or machine specific-ops. This structure
 *  is not constant since this field may be updated at run-time
 * @sof_fw_filename: Sound Open Firmware file name, if enabled
 * @sof_tplg_filename: Sound Open Firmware topology file name, if enabled
 */
/* Descriptor for SST ASoC machine driver */
struct snd_soc_acpi_mach {
	u8 id[ACPI_ID_LEN];
	const struct snd_soc_acpi_codecs *comp_ids;
	const u32 link_mask;
	const struct snd_soc_acpi_link_adr *links;
	const char *drv_name;
	const char *fw_filename;
	const char *board;
	struct snd_soc_acpi_mach * (*machine_quirk)(void *arg);
	const void *quirk_data;
	void *pdata;
	struct snd_soc_acpi_mach_params mach_params;
	const char *sof_fw_filename;
	const char *sof_tplg_filename;
};

#define SND_SOC_ACPI_MAX_CODECS 3

/**
 * struct snd_soc_acpi_codecs: Structure to hold secondary codec information
 * apart from the matched one, this data will be passed to the quirk function
 * to match with the ACPI detected devices
 *
 * @num_codecs: number of secondary codecs used in the platform
 * @codecs: holds the codec IDs
 *
 */
struct snd_soc_acpi_codecs {
	int num_codecs;
	u8 codecs[SND_SOC_ACPI_MAX_CODECS][ACPI_ID_LEN];
};

static inline bool snd_soc_acpi_sof_parent(struct device *dev)
{
	return dev->parent && dev->parent->driver && dev->parent->driver->name &&
		!strncmp(dev->parent->driver->name, "sof-audio-acpi", strlen("sof-audio-acpi"));
}

#endif
