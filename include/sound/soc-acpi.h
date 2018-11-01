/* SPDX-License-Identifier: GPL-2.0
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
bool snd_soc_acpi_find_package_from_hid(const u8 hid[ACPI_ID_LEN],
				    struct snd_soc_acpi_package_context *ctx);
#else
static inline bool
snd_soc_acpi_find_package_from_hid(const u8 hid[ACPI_ID_LEN],
				   struct snd_soc_acpi_package_context *ctx)
{
	return false;
}
#endif

/* acpi match */
struct snd_soc_acpi_mach *
snd_soc_acpi_find_machine(struct snd_soc_acpi_mach *machines);

/**
 * snd_soc_acpi_mach_params: interface for machine driver configuration
 *
 * @acpi_ipc_irq_index: used for BYT-CR detection
 * @platform: string used for HDaudio codec support
 * @codec_mask: used for HDAudio support
 */
struct snd_soc_acpi_mach_params {
	u32 acpi_ipc_irq_index;
	const char *platform;
	u32 codec_mask;
};

/**
 * snd_soc_acpi_mach: ACPI-based machine descriptor. Most of the fields are
 * related to the hardware, except for the firmware and topology file names.
 * A platform supported by legacy and Sound Open Firmware (SOF) would expose
 * all firmware/topology related fields.
 *
 * @id: ACPI ID (usually the codec's) used to find a matching machine driver.
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
 * @asoc_plat_name: ASoC platform name, used for binding machine drivers
 * if non NULL
 * @new_mach_data: machine driver private data fixup
 */
/* Descriptor for SST ASoC machine driver */
struct snd_soc_acpi_mach {
	const u8 id[ACPI_ID_LEN];
	const char *drv_name;
	const char *fw_filename;
	const char *board;
	struct snd_soc_acpi_mach * (*machine_quirk)(void *arg);
	const void *quirk_data;
	void *pdata;
	struct snd_soc_acpi_mach_params mach_params;
	const char *sof_fw_filename;
	const char *sof_tplg_filename;
	const char *asoc_plat_name;
	struct platform_device * (*new_mach_data)(void *pdata);
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

/* check all codecs */
struct snd_soc_acpi_mach *snd_soc_acpi_codec_list(void *arg);

#endif
