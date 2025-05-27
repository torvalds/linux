/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2013-15, Intel Corporation
 */

#ifndef __LINUX_SND_SOC_ACPI_H
#define __LINUX_SND_SOC_ACPI_H

#include <linux/stddef.h>
#include <linux/acpi.h>
#include <linux/mod_devicetable.h>
#include <linux/soundwire/sdw.h>
#include <sound/soc.h>

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
 * @dmic_num: number of SoC- or chipset-attached PDM digital microphones
 * @link_mask: SoundWire links enabled on the board
 * @links: array of SoundWire link _ADR descriptors, null terminated
 * @i2s_link_mask: I2S/TDM links enabled on the board
 * @num_dai_drivers: number of elements in @dai_drivers
 * @dai_drivers: pointer to dai_drivers, used e.g. in nocodec mode
 * @subsystem_vendor: optional PCI SSID vendor value
 * @subsystem_device: optional PCI SSID device value
 * @subsystem_rev: optional PCI SSID revision value
 * @subsystem_id_set: true if a value has been written to
 *		      subsystem_vendor and subsystem_device.
 * @bt_link_mask: BT offload link enabled on the board
 */
struct snd_soc_acpi_mach_params {
	u32 acpi_ipc_irq_index;
	const char *platform;
	u32 codec_mask;
	u32 dmic_num;
	u32 link_mask;
	const struct snd_soc_acpi_link_adr *links;
	u32 i2s_link_mask;
	u32 num_dai_drivers;
	struct snd_soc_dai_driver *dai_drivers;
	unsigned short subsystem_vendor;
	unsigned short subsystem_device;
	unsigned short subsystem_rev;
	bool subsystem_id_set;
	u32 bt_link_mask;
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

/*
 * when set the topology uses the -ssp<N> suffix, where N is determined based on
 * BIOS or DMI information
 */
#define SND_SOC_ACPI_TPLG_INTEL_SSP_NUMBER BIT(0)

/*
 * when more than one SSP is reported in the link mask, use the most significant.
 * This choice was found to be valid on platforms with ES8336 codecs.
 */
#define SND_SOC_ACPI_TPLG_INTEL_SSP_MSB BIT(1)

/*
 * when set the topology uses the -dmic<N>ch suffix, where N is determined based on
 * BIOS or DMI information
 */
#define SND_SOC_ACPI_TPLG_INTEL_DMIC_NUMBER BIT(2)

/*
 * when set the speaker amplifier name suffix (i.e. "-max98360a") will be
 * appended to topology file name
 */
#define SND_SOC_ACPI_TPLG_INTEL_AMP_NAME BIT(3)

/*
 * when set the headphone codec name suffix (i.e. "-rt5682") will be appended to
 * topology file name
 */
#define SND_SOC_ACPI_TPLG_INTEL_CODEC_NAME BIT(4)

/**
 * snd_soc_acpi_mach: ACPI-based machine descriptor. Most of the fields are
 * related to the hardware, except for the firmware and topology file names.
 * A platform supported by legacy and Sound Open Firmware (SOF) would expose
 * all firmware/topology related fields.
 *
 * @id: ACPI ID (usually the codec's) used to find a matching machine driver.
 * @uid: ACPI Unique ID, can be used to disambiguate matches.
 * @comp_ids: list of compatible audio codecs using the same machine driver,
 * firmware and topology
 * @link_mask: describes required board layout, e.g. for SoundWire.
 * @links: array of link _ADR descriptors, null terminated.
 * @drv_name: machine driver name
 * @fw_filename: firmware file name. Used when SOF is not enabled.
 * @tplg_filename: topology file name. Used when SOF is not enabled.
 * @board: board name
 * @machine_quirk: pointer to quirk, usually based on DMI information when
 * ACPI ID alone is not sufficient, wrong or misleading
 * @quirk_data: data used to uniquely identify a machine, usually a list of
 * audio codecs whose presence if checked with ACPI
 * @machine_check: pointer to quirk function. The functionality is similar to
 * the use of @machine_quirk, except that the return value is a boolean: the intent
 * is to skip a machine if the additional hardware/firmware verification invalidates
 * the initial selection in the snd_soc_acpi_mach table.
 * @pdata: intended for platform data or machine specific-ops. This structure
 *  is not constant since this field may be updated at run-time
 * @sof_tplg_filename: Sound Open Firmware topology file name, if enabled
 * @tplg_quirk_mask: quirks to select different topology files dynamically
 * @get_function_tplg_files: This is an optional callback, if specified then instead of
 *	the single sof_tplg_filename the callback will return the list of function topology
 *	files to be loaded.
 *	Return value: The number of the files or negative ERRNO. 0 means that the single topology
 *		      file should be used, no function topology split can be used on the machine.
 *	@card: the pointer of the card
 *	@mach: the pointer of the machine driver
 *	@prefix: the prefix of the topology file name. Typically, it is the path.
 *	@tplg_files: the pointer of the array of the topology file names.
 */
/* Descriptor for SST ASoC machine driver */
struct snd_soc_acpi_mach {
	u8 id[ACPI_ID_LEN];
	const char *uid;
	const struct snd_soc_acpi_codecs *comp_ids;
	const u32 link_mask;
	const struct snd_soc_acpi_link_adr *links;
	const char *drv_name;
	const char *fw_filename;
	const char *tplg_filename;
	const char *board;
	struct snd_soc_acpi_mach * (*machine_quirk)(void *arg);
	const void *quirk_data;
	bool (*machine_check)(void *arg);
	void *pdata;
	struct snd_soc_acpi_mach_params mach_params;
	const char *sof_tplg_filename;
	const u32 tplg_quirk_mask;
	int (*get_function_tplg_files)(struct snd_soc_card *card,
				       const struct snd_soc_acpi_mach *mach,
				       const char *prefix, const char ***tplg_files);
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

bool snd_soc_acpi_sdw_link_slaves_found(struct device *dev,
					const struct snd_soc_acpi_link_adr *link,
					struct sdw_peripherals *peripherals);

#endif
