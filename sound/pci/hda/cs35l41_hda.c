// SPDX-License-Identifier: GPL-2.0
//
// CS35l41 ALSA HDA audio driver
//
// Copyright 2021 Cirrus Logic, Inc.
//
// Author: Lucas Tanure <tanureal@opensource.cirrus.com>

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <sound/hda_codec.h>
#include <sound/soc.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/vmalloc.h>
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include "hda_generic.h"
#include "hda_component.h"
#include "cs35l41_hda.h"
#include "hda_cs_dsp_ctl.h"
#include "cs35l41_hda_property.h"

#define CS35L41_PART "cs35l41"

#define HALO_STATE_DSP_CTL_NAME		"HALO_STATE"
#define HALO_STATE_DSP_CTL_TYPE		5
#define HALO_STATE_DSP_CTL_ALG		262308
#define CAL_R_DSP_CTL_NAME		"CAL_R"
#define CAL_STATUS_DSP_CTL_NAME		"CAL_STATUS"
#define CAL_CHECKSUM_DSP_CTL_NAME	"CAL_CHECKSUM"
#define CAL_AMBIENT_DSP_CTL_NAME	"CAL_AMBIENT"
#define CAL_DSP_CTL_TYPE		5
#define CAL_DSP_CTL_ALG			205
#define CS35L41_UUID			"50d90cdc-3de4-4f18-b528-c7fe3b71f40d"
#define CS35L41_DSM_GET_MUTE		5
#define CS35L41_NOTIFY_EVENT		0x91
#define CS35L41_TUNING_SIG		0x109A4A35

enum cs35l41_tuning_param_types {
	TUNING_PARAM_GAIN,
};

struct cs35l41_tuning_param_hdr {
	__le32 tuning_index;
	__le32 type;
	__le32 size;
} __packed;

struct cs35l41_tuning_param {
	struct cs35l41_tuning_param_hdr hdr;
	union {
		__le32 gain;
	};
} __packed;

struct cs35l41_tuning_params {
	__le32 signature;
	__le32 version;
	__le32 size;
	__le32 num_entries;
	u8 data[];
} __packed;

/* Firmware calibration controls */
static const struct cirrus_amp_cal_controls cs35l41_calibration_controls = {
	.alg_id =	CAL_DSP_CTL_ALG,
	.mem_region =	CAL_DSP_CTL_TYPE,
	.ambient =	CAL_AMBIENT_DSP_CTL_NAME,
	.calr =		CAL_R_DSP_CTL_NAME,
	.status =	CAL_STATUS_DSP_CTL_NAME,
	.checksum =	CAL_CHECKSUM_DSP_CTL_NAME,
};

static bool firmware_autostart = 1;
module_param(firmware_autostart, bool, 0444);
MODULE_PARM_DESC(firmware_autostart, "Allow automatic firmware download on boot"
			     "(0=Disable, 1=Enable) (default=1); ");

static const struct reg_sequence cs35l41_hda_config[] = {
	{ CS35L41_PLL_CLK_CTRL,		0x00000430 }, // 3072000Hz, BCLK Input, PLL_REFCLK_EN = 1
	{ CS35L41_DSP_CLK_CTRL,		0x00000003 }, // DSP CLK EN
	{ CS35L41_GLOBAL_CLK_CTRL,	0x00000003 }, // GLOBAL_FS = 48 kHz
	{ CS35L41_SP_ENABLES,		0x00010000 }, // ASP_RX1_EN = 1
	{ CS35L41_SP_RATE_CTRL,		0x00000021 }, // ASP_BCLK_FREQ = 3.072 MHz
	{ CS35L41_SP_FORMAT,		0x20200200 }, // 32 bits RX/TX slots, I2S, clk consumer
	{ CS35L41_SP_HIZ_CTRL,		0x00000002 }, // Hi-Z unused
	{ CS35L41_SP_TX_WL,		0x00000018 }, // 24 cycles/slot
	{ CS35L41_SP_RX_WL,		0x00000018 }, // 24 cycles/slot
	{ CS35L41_DAC_PCM1_SRC,		0x00000008 }, // DACPCM1_SRC = ASPRX1
	{ CS35L41_ASP_TX1_SRC,		0x00000018 }, // ASPTX1 SRC = VMON
	{ CS35L41_ASP_TX2_SRC,		0x00000019 }, // ASPTX2 SRC = IMON
	{ CS35L41_ASP_TX3_SRC,		0x00000032 }, // ASPTX3 SRC = ERRVOL
	{ CS35L41_ASP_TX4_SRC,		0x00000033 }, // ASPTX4 SRC = CLASSH_TGT
	{ CS35L41_DSP1_RX1_SRC,		0x00000008 }, // DSP1RX1 SRC = ASPRX1
	{ CS35L41_DSP1_RX2_SRC,		0x00000009 }, // DSP1RX2 SRC = ASPRX2
	{ CS35L41_DSP1_RX3_SRC,         0x00000018 }, // DSP1RX3 SRC = VMON
	{ CS35L41_DSP1_RX4_SRC,         0x00000019 }, // DSP1RX4 SRC = IMON
	{ CS35L41_DSP1_RX5_SRC,         0x00000020 }, // DSP1RX5 SRC = ERRVOL
};

static const struct reg_sequence cs35l41_hda_config_dsp[] = {
	{ CS35L41_PLL_CLK_CTRL,		0x00000430 }, // 3072000Hz, BCLK Input, PLL_REFCLK_EN = 1
	{ CS35L41_DSP_CLK_CTRL,		0x00000003 }, // DSP CLK EN
	{ CS35L41_GLOBAL_CLK_CTRL,	0x00000003 }, // GLOBAL_FS = 48 kHz
	{ CS35L41_SP_ENABLES,		0x00010001 }, // ASP_RX1_EN = 1, ASP_TX1_EN = 1
	{ CS35L41_SP_RATE_CTRL,		0x00000021 }, // ASP_BCLK_FREQ = 3.072 MHz
	{ CS35L41_SP_FORMAT,		0x20200200 }, // 32 bits RX/TX slots, I2S, clk consumer
	{ CS35L41_SP_HIZ_CTRL,		0x00000003 }, // Hi-Z unused/disabled
	{ CS35L41_SP_TX_WL,		0x00000018 }, // 24 cycles/slot
	{ CS35L41_SP_RX_WL,		0x00000018 }, // 24 cycles/slot
	{ CS35L41_DAC_PCM1_SRC,		0x00000032 }, // DACPCM1_SRC = DSP1TX1
	{ CS35L41_ASP_TX1_SRC,		0x00000018 }, // ASPTX1 SRC = VMON
	{ CS35L41_ASP_TX2_SRC,		0x00000019 }, // ASPTX2 SRC = IMON
	{ CS35L41_ASP_TX3_SRC,		0x00000028 }, // ASPTX3 SRC = VPMON
	{ CS35L41_ASP_TX4_SRC,		0x00000029 }, // ASPTX4 SRC = VBSTMON
	{ CS35L41_DSP1_RX1_SRC,		0x00000008 }, // DSP1RX1 SRC = ASPRX1
	{ CS35L41_DSP1_RX2_SRC,		0x00000008 }, // DSP1RX2 SRC = ASPRX1
	{ CS35L41_DSP1_RX3_SRC,         0x00000018 }, // DSP1RX3 SRC = VMON
	{ CS35L41_DSP1_RX4_SRC,         0x00000019 }, // DSP1RX4 SRC = IMON
	{ CS35L41_DSP1_RX6_SRC,         0x00000029 }, // DSP1RX6 SRC = VBSTMON
};

static const struct reg_sequence cs35l41_hda_unmute[] = {
	{ CS35L41_AMP_DIG_VOL_CTRL,	0x00008000 }, // AMP_HPF_PCM_EN = 1, AMP_VOL_PCM  0.0 dB
	{ CS35L41_AMP_GAIN_CTRL,	0x00000084 }, // AMP_GAIN_PCM 4.5 dB
};

static const struct reg_sequence cs35l41_hda_mute[] = {
	{ CS35L41_AMP_GAIN_CTRL,	0x00000000 }, // AMP_GAIN_PCM 0.5 dB
	{ CS35L41_AMP_DIG_VOL_CTRL,	0x0000A678 }, // AMP_HPF_PCM_EN = 1, AMP_VOL_PCM Mute
};

static const struct cs_dsp_client_ops client_ops = {
	/* cs_dsp requires the client to provide this even if it is empty */
};

static int cs35l41_request_tuning_param_file(struct cs35l41_hda *cs35l41, char *tuning_filename,
					     const struct firmware **firmware, char **filename,
					     const char *ssid)
{
	int ret = 0;

	/* Filename is the same as the tuning file with "cfg" suffix */
	*filename = kasprintf(GFP_KERNEL, "%scfg", tuning_filename);
	if (*filename == NULL)
		return -ENOMEM;

	ret = firmware_request_nowarn(firmware, *filename, cs35l41->dev);
	if (ret != 0) {
		dev_dbg(cs35l41->dev, "Failed to request '%s'\n", *filename);
		kfree(*filename);
		*filename = NULL;
	}

	return ret;
}

static int cs35l41_request_firmware_file(struct cs35l41_hda *cs35l41,
					 const struct firmware **firmware, char **filename,
					 const char *ssid, const char *amp_name,
					 int spkid, const char *filetype)
{
	const char * const dsp_name = cs35l41->cs_dsp.name;
	char *s, c;
	int ret = 0;

	if (spkid > -1 && ssid && amp_name)
		*filename = kasprintf(GFP_KERNEL, "cirrus/%s-%s-%s-%s-spkid%d-%s.%s", CS35L41_PART,
				      dsp_name, hda_cs_dsp_fw_ids[cs35l41->firmware_type],
				      ssid, spkid, amp_name, filetype);
	else if (spkid > -1 && ssid)
		*filename = kasprintf(GFP_KERNEL, "cirrus/%s-%s-%s-%s-spkid%d.%s", CS35L41_PART,
				      dsp_name, hda_cs_dsp_fw_ids[cs35l41->firmware_type],
				      ssid, spkid, filetype);
	else if (ssid && amp_name)
		*filename = kasprintf(GFP_KERNEL, "cirrus/%s-%s-%s-%s-%s.%s", CS35L41_PART,
				      dsp_name, hda_cs_dsp_fw_ids[cs35l41->firmware_type],
				      ssid, amp_name, filetype);
	else if (ssid)
		*filename = kasprintf(GFP_KERNEL, "cirrus/%s-%s-%s-%s.%s", CS35L41_PART,
				      dsp_name, hda_cs_dsp_fw_ids[cs35l41->firmware_type],
				      ssid, filetype);
	else
		*filename = kasprintf(GFP_KERNEL, "cirrus/%s-%s-%s.%s", CS35L41_PART,
				      dsp_name, hda_cs_dsp_fw_ids[cs35l41->firmware_type],
				      filetype);

	if (*filename == NULL)
		return -ENOMEM;

	/*
	 * Make sure that filename is lower-case and any non alpha-numeric
	 * characters except full stop and '/' are replaced with hyphens.
	 */
	s = *filename;
	while (*s) {
		c = *s;
		if (isalnum(c))
			*s = tolower(c);
		else if (c != '.' && c != '/')
			*s = '-';
		s++;
	}

	ret = firmware_request_nowarn(firmware, *filename, cs35l41->dev);
	if (ret != 0) {
		dev_dbg(cs35l41->dev, "Failed to request '%s'\n", *filename);
		kfree(*filename);
		*filename = NULL;
	}

	return ret;
}

static int cs35l41_request_firmware_files_spkid(struct cs35l41_hda *cs35l41,
						const struct firmware **wmfw_firmware,
						char **wmfw_filename,
						const struct firmware **coeff_firmware,
						char **coeff_filename)
{
	int ret;

	/* try cirrus/part-dspN-fwtype-sub<-spkidN><-ampname>.wmfw */
	ret = cs35l41_request_firmware_file(cs35l41, wmfw_firmware, wmfw_filename,
					    cs35l41->acpi_subsystem_id, cs35l41->amp_name,
					    cs35l41->speaker_id, "wmfw");
	if (!ret) {
		/* try cirrus/part-dspN-fwtype-sub<-spkidN><-ampname>.bin */
		ret = cs35l41_request_firmware_file(cs35l41, coeff_firmware, coeff_filename,
						    cs35l41->acpi_subsystem_id, cs35l41->amp_name,
						    cs35l41->speaker_id, "bin");
		if (ret)
			goto coeff_err;

		return 0;
	}

	/* try cirrus/part-dspN-fwtype-sub<-ampname>.wmfw */
	ret = cs35l41_request_firmware_file(cs35l41, wmfw_firmware, wmfw_filename,
					    cs35l41->acpi_subsystem_id,
					    cs35l41->amp_name, -1, "wmfw");
	if (!ret) {
		/* try cirrus/part-dspN-fwtype-sub<-spkidN><-ampname>.bin */
		ret = cs35l41_request_firmware_file(cs35l41, coeff_firmware, coeff_filename,
						    cs35l41->acpi_subsystem_id, cs35l41->amp_name,
						    cs35l41->speaker_id, "bin");
		if (ret)
			goto coeff_err;

		return 0;
	}

	/* try cirrus/part-dspN-fwtype-sub<-spkidN>.wmfw */
	ret = cs35l41_request_firmware_file(cs35l41, wmfw_firmware, wmfw_filename,
					    cs35l41->acpi_subsystem_id,
					    NULL, cs35l41->speaker_id, "wmfw");
	if (!ret) {
		/* try cirrus/part-dspN-fwtype-sub<-spkidN><-ampname>.bin */
		ret = cs35l41_request_firmware_file(cs35l41, coeff_firmware, coeff_filename,
						    cs35l41->acpi_subsystem_id,
						    cs35l41->amp_name, cs35l41->speaker_id, "bin");
		if (ret)
			/* try cirrus/part-dspN-fwtype-sub<-spkidN>.bin */
			ret = cs35l41_request_firmware_file(cs35l41, coeff_firmware,
							    coeff_filename,
							    cs35l41->acpi_subsystem_id, NULL,
							    cs35l41->speaker_id, "bin");
		if (ret)
			goto coeff_err;

		return 0;
	}

	/* try cirrus/part-dspN-fwtype-sub.wmfw */
	ret = cs35l41_request_firmware_file(cs35l41, wmfw_firmware, wmfw_filename,
					    cs35l41->acpi_subsystem_id,
					    NULL, -1, "wmfw");
	if (!ret) {
		/* try cirrus/part-dspN-fwtype-sub<-spkidN><-ampname>.bin */
		ret = cs35l41_request_firmware_file(cs35l41, coeff_firmware, coeff_filename,
						    cs35l41->acpi_subsystem_id, cs35l41->amp_name,
						    cs35l41->speaker_id, "bin");
		if (ret)
			/* try cirrus/part-dspN-fwtype-sub<-spkidN>.bin */
			ret = cs35l41_request_firmware_file(cs35l41, coeff_firmware,
							    coeff_filename,
							    cs35l41->acpi_subsystem_id, NULL,
							    cs35l41->speaker_id, "bin");
		if (ret)
			goto coeff_err;
	}

	return ret;
coeff_err:
	release_firmware(*wmfw_firmware);
	kfree(*wmfw_filename);
	return ret;
}

static int cs35l41_fallback_firmware_file(struct cs35l41_hda *cs35l41,
					  const struct firmware **wmfw_firmware,
					  char **wmfw_filename,
					  const struct firmware **coeff_firmware,
					  char **coeff_filename)
{
	int ret;

	/* Handle fallback */
	dev_warn(cs35l41->dev, "Falling back to default firmware.\n");

	/* fallback try cirrus/part-dspN-fwtype.wmfw */
	ret = cs35l41_request_firmware_file(cs35l41, wmfw_firmware, wmfw_filename,
					    NULL, NULL, -1, "wmfw");
	if (ret)
		goto err;

	/* fallback try cirrus/part-dspN-fwtype.bin */
	ret = cs35l41_request_firmware_file(cs35l41, coeff_firmware, coeff_filename,
					    NULL, NULL, -1, "bin");
	if (ret) {
		release_firmware(*wmfw_firmware);
		kfree(*wmfw_filename);
		goto err;
	}
	return 0;

err:
	dev_warn(cs35l41->dev, "Unable to find firmware and tuning\n");
	return ret;
}

static int cs35l41_request_firmware_files(struct cs35l41_hda *cs35l41,
					  const struct firmware **wmfw_firmware,
					  char **wmfw_filename,
					  const struct firmware **coeff_firmware,
					  char **coeff_filename)
{
	int ret;

	if (cs35l41->speaker_id > -1) {
		ret = cs35l41_request_firmware_files_spkid(cs35l41, wmfw_firmware, wmfw_filename,
							   coeff_firmware, coeff_filename);
		goto out;
	}

	/* try cirrus/part-dspN-fwtype-sub<-ampname>.wmfw */
	ret = cs35l41_request_firmware_file(cs35l41, wmfw_firmware, wmfw_filename,
					    cs35l41->acpi_subsystem_id,
					    cs35l41->amp_name, -1, "wmfw");
	if (!ret) {
		/* try cirrus/part-dspN-fwtype-sub<-ampname>.bin */
		ret = cs35l41_request_firmware_file(cs35l41, coeff_firmware, coeff_filename,
						    cs35l41->acpi_subsystem_id, cs35l41->amp_name,
						    -1, "bin");
		if (ret)
			goto coeff_err;

		goto out;
	}

	/* try cirrus/part-dspN-fwtype-sub.wmfw */
	ret = cs35l41_request_firmware_file(cs35l41, wmfw_firmware, wmfw_filename,
					    cs35l41->acpi_subsystem_id,
					    NULL, -1, "wmfw");
	if (!ret) {
		/* try cirrus/part-dspN-fwtype-sub<-ampname>.bin */
		ret = cs35l41_request_firmware_file(cs35l41, coeff_firmware, coeff_filename,
						    cs35l41->acpi_subsystem_id,
						    cs35l41->amp_name, -1, "bin");
		if (ret)
			/* try cirrus/part-dspN-fwtype-sub.bin */
			ret = cs35l41_request_firmware_file(cs35l41, coeff_firmware, coeff_filename,
							    cs35l41->acpi_subsystem_id, NULL, -1,
							    "bin");
		if (ret)
			goto coeff_err;
	}

out:
	if (ret)
		/* if all attempts at finding firmware fail, try fallback */
		goto fallback;

	return 0;

coeff_err:
	release_firmware(*wmfw_firmware);
	kfree(*wmfw_filename);
fallback:
	return cs35l41_fallback_firmware_file(cs35l41, wmfw_firmware, wmfw_filename,
					      coeff_firmware, coeff_filename);
}


static void cs35l41_hda_apply_calibration(struct cs35l41_hda *cs35l41)
{
	int ret;

	if (!cs35l41->cal_data_valid)
		return;

	ret = cs_amp_write_cal_coeffs(&cs35l41->cs_dsp, &cs35l41_calibration_controls,
				      &cs35l41->cal_data);
	if (ret < 0)
		dev_warn(cs35l41->dev, "Failed to apply calibration: %d\n", ret);
	else
		dev_info(cs35l41->dev, "Calibration applied: R0=%d\n", cs35l41->cal_data.calR);
}

static int cs35l41_read_silicon_uid(struct cs35l41_hda *cs35l41, u64 *uid)
{
	u32 tmp;
	int ret;

	ret = regmap_read(cs35l41->regmap, CS35L41_DIE_STS2, &tmp);
	if (ret) {
		dev_err(cs35l41->dev, "Cannot obtain CS35L41_DIE_STS2: %d\n", ret);
		return ret;
	}

	*uid = tmp;
	*uid <<= 32;

	ret = regmap_read(cs35l41->regmap, CS35L41_DIE_STS1, &tmp);
	if (ret) {
		dev_err(cs35l41->dev, "Cannot obtain CS35L41_DIE_STS1: %d\n", ret);
		return ret;
	}

	*uid |= tmp;

	dev_dbg(cs35l41->dev, "UniqueID = %#llx\n", *uid);

	return 0;
}

static int cs35l41_get_calibration(struct cs35l41_hda *cs35l41)
{
	u64 silicon_uid;
	int ret;

	ret = cs35l41_read_silicon_uid(cs35l41, &silicon_uid);
	if (ret < 0)
		return ret;

	ret = cs_amp_get_efi_calibration_data(cs35l41->dev, silicon_uid,
					      cs35l41->index,
					      &cs35l41->cal_data);

	/* Only return an error status if probe should be aborted */
	if ((ret == -ENOENT) || (ret == -EOVERFLOW))
		return 0;

	if (ret < 0)
		return ret;

	cs35l41->cal_data_valid = true;

	return 0;
}


static void cs35l41_set_default_tuning_params(struct cs35l41_hda *cs35l41)
{
	cs35l41->tuning_gain = DEFAULT_AMP_GAIN_PCM;
}

static int cs35l41_read_tuning_params(struct cs35l41_hda *cs35l41, const struct firmware *firmware)
{
	struct cs35l41_tuning_params *params;
	unsigned int offset = 0;
	unsigned int end;
	int i;

	params = (void *)&firmware->data[0];

	if (le32_to_cpu(params->size) != firmware->size) {
		dev_err(cs35l41->dev, "Wrong Size for Tuning Param file. Expected %d got %zu\n",
			le32_to_cpu(params->size), firmware->size);
		return -EINVAL;
	}

	if (le32_to_cpu(params->version) != 1) {
		dev_err(cs35l41->dev, "Unsupported Tuning Param Version: %d\n",
			le32_to_cpu(params->version));
		return -EINVAL;
	}

	if (le32_to_cpu(params->signature) != CS35L41_TUNING_SIG) {
		dev_err(cs35l41->dev,
			"Mismatched Signature for Tuning Param file. Expected %#x got %#x\n",
			CS35L41_TUNING_SIG, le32_to_cpu(params->signature));
		return -EINVAL;
	}

	end = firmware->size - sizeof(struct cs35l41_tuning_params);

	for (i = 0; i < le32_to_cpu(params->num_entries); i++) {
		struct cs35l41_tuning_param *param;

		if ((offset >= end) || ((offset + sizeof(struct cs35l41_tuning_param_hdr)) >= end))
			return -EFAULT;

		param = (void *)&params->data[offset];
		offset += le32_to_cpu(param->hdr.size);

		if (offset > end)
			return -EFAULT;

		switch (le32_to_cpu(param->hdr.type)) {
		case TUNING_PARAM_GAIN:
			cs35l41->tuning_gain = le32_to_cpu(param->gain);
			dev_dbg(cs35l41->dev, "Applying Gain: %d\n", cs35l41->tuning_gain);
			break;
		default:
			break;
		}
	}

	return 0;
}

static int cs35l41_load_tuning_params(struct cs35l41_hda *cs35l41, char *tuning_filename)
{
	const struct firmware *tuning_param_file = NULL;
	char *tuning_param_filename = NULL;
	int ret;

	ret = cs35l41_request_tuning_param_file(cs35l41, tuning_filename, &tuning_param_file,
						&tuning_param_filename, cs35l41->acpi_subsystem_id);
	if (ret) {
		dev_dbg(cs35l41->dev, "Missing Tuning Param for file: %s: %d\n", tuning_filename,
			ret);
		return 0;
	}

	ret = cs35l41_read_tuning_params(cs35l41, tuning_param_file);
	if (ret) {
		dev_err(cs35l41->dev, "Error reading Tuning Params from file: %s: %d\n",
			tuning_param_filename, ret);
		/* Reset to default Tuning Parameters */
		cs35l41_set_default_tuning_params(cs35l41);
	}

	release_firmware(tuning_param_file);
	kfree(tuning_param_filename);

	return ret;
}

static int cs35l41_init_dsp(struct cs35l41_hda *cs35l41)
{
	const struct firmware *coeff_firmware = NULL;
	const struct firmware *wmfw_firmware = NULL;
	struct cs_dsp *dsp = &cs35l41->cs_dsp;
	char *coeff_filename = NULL;
	char *wmfw_filename = NULL;
	int ret;

	if (!cs35l41->halo_initialized) {
		cs35l41_configure_cs_dsp(cs35l41->dev, cs35l41->regmap, dsp);
		dsp->client_ops = &client_ops;

		ret = cs_dsp_halo_init(&cs35l41->cs_dsp);
		if (ret)
			return ret;
		cs35l41->halo_initialized = true;
	}

	cs35l41_set_default_tuning_params(cs35l41);

	ret = cs35l41_request_firmware_files(cs35l41, &wmfw_firmware, &wmfw_filename,
					     &coeff_firmware, &coeff_filename);
	if (ret < 0)
		return ret;

	dev_dbg(cs35l41->dev, "Loading WMFW Firmware: %s\n", wmfw_filename);
	if (coeff_filename) {
		dev_dbg(cs35l41->dev, "Loading Coefficient File: %s\n", coeff_filename);
		ret = cs35l41_load_tuning_params(cs35l41, coeff_filename);
		if (ret)
			dev_warn(cs35l41->dev, "Unable to load Tuning Parameters: %d\n", ret);
	} else {
		dev_warn(cs35l41->dev, "No Coefficient File available.\n");
	}

	ret = cs_dsp_power_up(dsp, wmfw_firmware, wmfw_filename, coeff_firmware, coeff_filename,
			      hda_cs_dsp_fw_ids[cs35l41->firmware_type]);
	if (ret)
		goto err;

	cs35l41_hda_apply_calibration(cs35l41);

err:
	if (ret)
		cs35l41_set_default_tuning_params(cs35l41);
	release_firmware(wmfw_firmware);
	release_firmware(coeff_firmware);
	kfree(wmfw_filename);
	kfree(coeff_filename);

	return ret;
}

static void cs35l41_shutdown_dsp(struct cs35l41_hda *cs35l41)
{
	struct cs_dsp *dsp = &cs35l41->cs_dsp;

	cs35l41_set_default_tuning_params(cs35l41);
	cs_dsp_stop(dsp);
	cs_dsp_power_down(dsp);
	dev_dbg(cs35l41->dev, "Unloaded Firmware\n");
}

static void cs35l41_remove_dsp(struct cs35l41_hda *cs35l41)
{
	struct cs_dsp *dsp = &cs35l41->cs_dsp;

	cancel_work_sync(&cs35l41->fw_load_work);

	mutex_lock(&cs35l41->fw_mutex);
	cs35l41_shutdown_dsp(cs35l41);
	cs_dsp_remove(dsp);
	cs35l41->halo_initialized = false;
	mutex_unlock(&cs35l41->fw_mutex);
}

/* Protection release cycle to get the speaker out of Safe-Mode */
static void cs35l41_error_release(struct device *dev, struct regmap *regmap, unsigned int mask)
{
	regmap_write(regmap, CS35L41_PROTECT_REL_ERR_IGN, 0);
	regmap_set_bits(regmap, CS35L41_PROTECT_REL_ERR_IGN, mask);
	regmap_clear_bits(regmap, CS35L41_PROTECT_REL_ERR_IGN, mask);
}

/* Clear all errors to release safe mode. Global Enable must be cleared first. */
static void cs35l41_irq_release(struct cs35l41_hda *cs35l41)
{
	cs35l41_error_release(cs35l41->dev, cs35l41->regmap, cs35l41->irq_errors);
	cs35l41->irq_errors = 0;
}

static void cs35l41_hda_play_start(struct device *dev)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	struct regmap *reg = cs35l41->regmap;

	dev_dbg(dev, "Play (Start)\n");

	if (cs35l41->playback_started) {
		dev_dbg(dev, "Playback already started.");
		return;
	}

	cs35l41->playback_started = true;

	if (cs35l41->cs_dsp.running) {
		regmap_multi_reg_write(reg, cs35l41_hda_config_dsp,
				       ARRAY_SIZE(cs35l41_hda_config_dsp));
		if (cs35l41->hw_cfg.bst_type == CS35L41_INT_BOOST)
			regmap_write(reg, CS35L41_DSP1_RX5_SRC, CS35L41_INPUT_SRC_VPMON);
		else
			regmap_write(reg, CS35L41_DSP1_RX5_SRC, CS35L41_INPUT_SRC_VBSTMON);
		regmap_update_bits(reg, CS35L41_PWR_CTRL2,
				   CS35L41_VMON_EN_MASK | CS35L41_IMON_EN_MASK,
				   1 << CS35L41_VMON_EN_SHIFT | 1 << CS35L41_IMON_EN_SHIFT);
		cs35l41_set_cspl_mbox_cmd(cs35l41->dev, reg, CSPL_MBOX_CMD_RESUME);
	} else {
		regmap_multi_reg_write(reg, cs35l41_hda_config, ARRAY_SIZE(cs35l41_hda_config));
	}
	regmap_update_bits(reg, CS35L41_PWR_CTRL2, CS35L41_AMP_EN_MASK, 1 << CS35L41_AMP_EN_SHIFT);
	if (cs35l41->hw_cfg.bst_type == CS35L41_EXT_BOOST)
		regmap_write(reg, CS35L41_GPIO1_CTRL1, 0x00008001);

}

static void cs35l41_mute(struct device *dev, bool mute)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	struct regmap *reg = cs35l41->regmap;
	unsigned int amp_gain;

	dev_dbg(dev, "Mute(%d:%d) Playback Started: %d\n", mute, cs35l41->mute_override,
		cs35l41->playback_started);

	if (cs35l41->playback_started) {
		if (mute || cs35l41->mute_override) {
			dev_dbg(dev, "Muting\n");
			regmap_multi_reg_write(reg, cs35l41_hda_mute, ARRAY_SIZE(cs35l41_hda_mute));
		} else {
			dev_dbg(dev, "Unmuting\n");
			if (cs35l41->cs_dsp.running) {
				dev_dbg(dev, "Using Tuned Gain: %d\n", cs35l41->tuning_gain);
				amp_gain = (cs35l41->tuning_gain << CS35L41_AMP_GAIN_PCM_SHIFT) |
					(DEFAULT_AMP_GAIN_PDM << CS35L41_AMP_GAIN_PDM_SHIFT);

				/* AMP_HPF_PCM_EN = 1, AMP_VOL_PCM  0.0 dB */
				regmap_write(reg, CS35L41_AMP_DIG_VOL_CTRL, 0x00008000);
				regmap_write(reg, CS35L41_AMP_GAIN_CTRL, amp_gain);
			} else {
				regmap_multi_reg_write(reg, cs35l41_hda_unmute,
						ARRAY_SIZE(cs35l41_hda_unmute));
			}
		}
	}
}

static void cs35l41_hda_play_done(struct device *dev)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	struct regmap *reg = cs35l41->regmap;

	dev_dbg(dev, "Play (Complete)\n");

	cs35l41_global_enable(dev, reg, cs35l41->hw_cfg.bst_type, 1,
			      &cs35l41->cs_dsp);
	cs35l41_mute(dev, false);
}

static void cs35l41_hda_pause_start(struct device *dev)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	struct regmap *reg = cs35l41->regmap;

	dev_dbg(dev, "Pause (Start)\n");

	cs35l41_mute(dev, true);
	cs35l41_global_enable(dev, reg, cs35l41->hw_cfg.bst_type, 0,
			      &cs35l41->cs_dsp);
}

static void cs35l41_hda_pause_done(struct device *dev)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	struct regmap *reg = cs35l41->regmap;

	dev_dbg(dev, "Pause (Complete)\n");

	regmap_update_bits(reg, CS35L41_PWR_CTRL2, CS35L41_AMP_EN_MASK, 0 << CS35L41_AMP_EN_SHIFT);
	if (cs35l41->hw_cfg.bst_type == CS35L41_EXT_BOOST)
		regmap_write(reg, CS35L41_GPIO1_CTRL1, 0x00000001);
	if (cs35l41->cs_dsp.running) {
		cs35l41_set_cspl_mbox_cmd(dev, reg, CSPL_MBOX_CMD_PAUSE);
		regmap_update_bits(reg, CS35L41_PWR_CTRL2,
				   CS35L41_VMON_EN_MASK | CS35L41_IMON_EN_MASK,
				   0 << CS35L41_VMON_EN_SHIFT | 0 << CS35L41_IMON_EN_SHIFT);
	}
	cs35l41_irq_release(cs35l41);
	cs35l41->playback_started = false;
}

static void cs35l41_hda_pre_playback_hook(struct device *dev, int action)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);

	switch (action) {
	case HDA_GEN_PCM_ACT_CLEANUP:
		mutex_lock(&cs35l41->fw_mutex);
		cs35l41_hda_pause_start(dev);
		mutex_unlock(&cs35l41->fw_mutex);
		break;
	default:
		break;
	}
}
static void cs35l41_hda_playback_hook(struct device *dev, int action)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);

	switch (action) {
	case HDA_GEN_PCM_ACT_OPEN:
		/*
		 * All amps must be resumed before we can start playing back.
		 * This ensures, for external boost, that all amps are in AMP_SAFE mode.
		 * Do this in HDA_GEN_PCM_ACT_OPEN, since this is run prior to any of the
		 * other actions.
		 */
		pm_runtime_get_sync(dev);
		break;
	case HDA_GEN_PCM_ACT_PREPARE:
		mutex_lock(&cs35l41->fw_mutex);
		cs35l41_hda_play_start(dev);
		mutex_unlock(&cs35l41->fw_mutex);
		break;
	case HDA_GEN_PCM_ACT_CLEANUP:
		mutex_lock(&cs35l41->fw_mutex);
		cs35l41_hda_pause_done(dev);
		mutex_unlock(&cs35l41->fw_mutex);
		break;
	case HDA_GEN_PCM_ACT_CLOSE:
		mutex_lock(&cs35l41->fw_mutex);
		if (!cs35l41->cs_dsp.running && cs35l41->request_fw_load &&
		    !cs35l41->fw_request_ongoing) {
			dev_info(dev, "Requesting Firmware Load after HDA_GEN_PCM_ACT_CLOSE\n");
			cs35l41->fw_request_ongoing = true;
			schedule_work(&cs35l41->fw_load_work);
		}
		mutex_unlock(&cs35l41->fw_mutex);

		/*
		 * Playback must be finished for all amps before we start runtime suspend.
		 * This ensures no amps are playing back when we start putting them to sleep.
		 */
		pm_runtime_mark_last_busy(dev);
		pm_runtime_put_autosuspend(dev);
		break;
	default:
		break;
	}
}

static void cs35l41_hda_post_playback_hook(struct device *dev, int action)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);

	switch (action) {
	case HDA_GEN_PCM_ACT_PREPARE:
		mutex_lock(&cs35l41->fw_mutex);
		cs35l41_hda_play_done(dev);
		mutex_unlock(&cs35l41->fw_mutex);
		break;
	default:
		break;
	}
}

static int cs35l41_hda_channel_map(struct device *dev, unsigned int tx_num, unsigned int *tx_slot,
				    unsigned int rx_num, unsigned int *rx_slot)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	static const char * const channel_name[] = { "L", "R" };

	if (!cs35l41->amp_name) {
		if (*rx_slot >= ARRAY_SIZE(channel_name))
			return -EINVAL;

		cs35l41->amp_name = devm_kasprintf(cs35l41->dev, GFP_KERNEL, "%s%d",
						   channel_name[*rx_slot], cs35l41->channel_index);
		if (!cs35l41->amp_name)
			return -ENOMEM;
	}

	return cs35l41_set_channels(cs35l41->dev, cs35l41->regmap, tx_num, tx_slot, rx_num,
				    rx_slot);
}

static int cs35l41_verify_id(struct cs35l41_hda *cs35l41, unsigned int *regid, unsigned int *reg_revid)
{
	unsigned int mtl_revid, chipid;
	int ret;

	ret = regmap_read(cs35l41->regmap, CS35L41_DEVID, regid);
	if (ret) {
		dev_err_probe(cs35l41->dev, ret, "Get Device ID failed\n");
		return ret;
	}

	ret = regmap_read(cs35l41->regmap, CS35L41_REVID, reg_revid);
	if (ret) {
		dev_err_probe(cs35l41->dev, ret, "Get Revision ID failed\n");
		return ret;
	}

	mtl_revid = *reg_revid & CS35L41_MTLREVID_MASK;

	chipid = (mtl_revid % 2) ? CS35L41R_CHIP_ID : CS35L41_CHIP_ID;
	if (*regid != chipid) {
		dev_err(cs35l41->dev, "CS35L41 Device ID (%X). Expected ID %X\n", *regid, chipid);
		return -ENODEV;
	}

	return 0;
}

static int cs35l41_ready_for_reset(struct cs35l41_hda *cs35l41)
{
	mutex_lock(&cs35l41->fw_mutex);
	if (cs35l41->cs_dsp.running) {
		cs35l41->cs_dsp.running = false;
		cs35l41->cs_dsp.booted = false;
	}
	regcache_mark_dirty(cs35l41->regmap);
	mutex_unlock(&cs35l41->fw_mutex);

	return 0;
}

static int cs35l41_system_suspend_prep(struct device *dev)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);

	dev_dbg(cs35l41->dev, "System Suspend Prepare\n");

	if (cs35l41->hw_cfg.bst_type == CS35L41_EXT_BOOST_NO_VSPK_SWITCH) {
		dev_err_once(cs35l41->dev, "System Suspend not supported\n");
		return 0; /* don't block the whole system suspend */
	}

	mutex_lock(&cs35l41->fw_mutex);
	if (cs35l41->playback_started)
		cs35l41_hda_pause_start(dev);
	mutex_unlock(&cs35l41->fw_mutex);

	return 0;
}

static int cs35l41_system_suspend(struct device *dev)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	int ret;

	dev_dbg(cs35l41->dev, "System Suspend\n");

	if (cs35l41->hw_cfg.bst_type == CS35L41_EXT_BOOST_NO_VSPK_SWITCH) {
		dev_err_once(cs35l41->dev, "System Suspend not supported\n");
		return 0; /* don't block the whole system suspend */
	}

	mutex_lock(&cs35l41->fw_mutex);
	if (cs35l41->playback_started)
		cs35l41_hda_pause_done(dev);
	mutex_unlock(&cs35l41->fw_mutex);

	ret = pm_runtime_force_suspend(dev);
	if (ret) {
		dev_err(dev, "System Suspend Failed, unable to runtime suspend: %d\n", ret);
		return ret;
	}

	/* Shutdown DSP before system suspend */
	ret = cs35l41_ready_for_reset(cs35l41);
	if (ret)
		dev_err(dev, "System Suspend Failed, not ready for Reset: %d\n", ret);

	if (cs35l41->reset_gpio) {
		dev_info(cs35l41->dev, "Asserting Reset\n");
		gpiod_set_value_cansleep(cs35l41->reset_gpio, 0);
		usleep_range(2000, 2100);
	}

	dev_dbg(cs35l41->dev, "System Suspended\n");

	return ret;
}

static int cs35l41_wait_boot_done(struct cs35l41_hda *cs35l41)
{
	unsigned int int_status;
	int ret;

	ret = regmap_read_poll_timeout(cs35l41->regmap, CS35L41_IRQ1_STATUS4, int_status,
				       int_status & CS35L41_OTP_BOOT_DONE, 1000, 100000);
	if (ret) {
		dev_err(cs35l41->dev, "Failed waiting for OTP_BOOT_DONE\n");
		return ret;
	}

	ret = regmap_read(cs35l41->regmap, CS35L41_IRQ1_STATUS3, &int_status);
	if (ret || (int_status & CS35L41_OTP_BOOT_ERR)) {
		dev_err(cs35l41->dev, "OTP Boot status %x error\n",
			int_status & CS35L41_OTP_BOOT_ERR);
		if (!ret)
			ret = -EIO;
		return ret;
	}

	return 0;
}

static int cs35l41_system_resume(struct device *dev)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	int ret;

	dev_dbg(cs35l41->dev, "System Resume\n");

	if (cs35l41->hw_cfg.bst_type == CS35L41_EXT_BOOST_NO_VSPK_SWITCH) {
		dev_err_once(cs35l41->dev, "System Resume not supported\n");
		return 0; /* don't block the whole system resume */
	}

	if (cs35l41->reset_gpio) {
		gpiod_set_value_cansleep(cs35l41->reset_gpio, 0);
		usleep_range(2000, 2100);
		gpiod_set_value_cansleep(cs35l41->reset_gpio, 1);
	}

	usleep_range(2000, 2100);

	regcache_cache_only(cs35l41->regmap, false);

	regmap_write(cs35l41->regmap, CS35L41_SFT_RESET, CS35L41_SOFTWARE_RESET);
	usleep_range(2000, 2100);

	ret = cs35l41_wait_boot_done(cs35l41);
	if (ret)
		return ret;

	regcache_cache_only(cs35l41->regmap, true);

	ret = pm_runtime_force_resume(dev);
	if (ret) {
		dev_err(dev, "System Resume Failed: Unable to runtime resume: %d\n", ret);
		return ret;
	}

	mutex_lock(&cs35l41->fw_mutex);

	if (cs35l41->request_fw_load && !cs35l41->fw_request_ongoing) {
		cs35l41->fw_request_ongoing = true;
		schedule_work(&cs35l41->fw_load_work);
	}
	mutex_unlock(&cs35l41->fw_mutex);

	return ret;
}

static int cs35l41_runtime_idle(struct device *dev)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);

	if (cs35l41->hw_cfg.bst_type == CS35L41_EXT_BOOST_NO_VSPK_SWITCH)
		return -EBUSY; /* suspend not supported yet on this model */
	return 0;
}

static int cs35l41_runtime_suspend(struct device *dev)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	int ret = 0;

	dev_dbg(cs35l41->dev, "Runtime Suspend\n");

	if (cs35l41->hw_cfg.bst_type == CS35L41_EXT_BOOST_NO_VSPK_SWITCH) {
		dev_dbg(cs35l41->dev, "Runtime Suspend not supported\n");
		return 0;
	}

	mutex_lock(&cs35l41->fw_mutex);

	if (cs35l41->cs_dsp.running) {
		ret = cs35l41_enter_hibernate(cs35l41->dev, cs35l41->regmap,
					      cs35l41->hw_cfg.bst_type);
		if (ret)
			goto err;
	} else {
		cs35l41_safe_reset(cs35l41->regmap, cs35l41->hw_cfg.bst_type);
	}

	regcache_cache_only(cs35l41->regmap, true);
	regcache_mark_dirty(cs35l41->regmap);

err:
	mutex_unlock(&cs35l41->fw_mutex);

	return ret;
}

static int cs35l41_runtime_resume(struct device *dev)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	unsigned int regid, reg_revid;
	int ret = 0;

	dev_dbg(cs35l41->dev, "Runtime Resume\n");

	if (cs35l41->hw_cfg.bst_type == CS35L41_EXT_BOOST_NO_VSPK_SWITCH) {
		dev_dbg(cs35l41->dev, "Runtime Resume not supported\n");
		return 0;
	}

	mutex_lock(&cs35l41->fw_mutex);

	regcache_cache_only(cs35l41->regmap, false);

	if (cs35l41->cs_dsp.running)	{
		ret = cs35l41_exit_hibernate(cs35l41->dev, cs35l41->regmap);
		if (ret) {
			dev_warn(cs35l41->dev, "Unable to exit Hibernate.");
			goto err;
		}
	}

	ret = cs35l41_verify_id(cs35l41, &regid, &reg_revid);
	if (ret)
		goto err;

	/* Test key needs to be unlocked to allow the OTP settings to re-apply */
	cs35l41_test_key_unlock(cs35l41->dev, cs35l41->regmap);
	ret = regcache_sync(cs35l41->regmap);
	cs35l41_test_key_lock(cs35l41->dev, cs35l41->regmap);
	if (ret) {
		dev_err(cs35l41->dev, "Failed to restore register cache: %d\n", ret);
		goto err;
	}

	if (cs35l41->hw_cfg.bst_type == CS35L41_EXT_BOOST)
		cs35l41_init_boost(cs35l41->dev, cs35l41->regmap, &cs35l41->hw_cfg);

	dev_dbg(cs35l41->dev, "CS35L41 Resumed (%x), Revision: %02X\n", regid, reg_revid);

err:
	mutex_unlock(&cs35l41->fw_mutex);

	return ret;
}

static int cs35l41_smart_amp(struct cs35l41_hda *cs35l41)
{
	unsigned int fw_status;
	__be32 halo_sts;
	int ret;

	if (cs35l41->bypass_fw) {
		dev_warn(cs35l41->dev, "Bypassing Firmware.\n");
		return 0;
	}

	ret = cs35l41_init_dsp(cs35l41);
	if (ret) {
		dev_warn(cs35l41->dev, "Cannot Initialize Firmware. Error: %d\n", ret);
		goto clean_dsp;
	}

	ret = cs35l41_write_fs_errata(cs35l41->dev, cs35l41->regmap);
	if (ret) {
		dev_err(cs35l41->dev, "Cannot Write FS Errata: %d\n", ret);
		goto clean_dsp;
	}

	ret = cs_dsp_run(&cs35l41->cs_dsp);
	if (ret) {
		dev_err(cs35l41->dev, "Fail to start dsp: %d\n", ret);
		goto clean_dsp;
	}

	ret = read_poll_timeout(hda_cs_dsp_read_ctl, ret,
				be32_to_cpu(halo_sts) == HALO_STATE_CODE_RUN,
				1000, 15000, false, &cs35l41->cs_dsp, HALO_STATE_DSP_CTL_NAME,
				HALO_STATE_DSP_CTL_TYPE, HALO_STATE_DSP_CTL_ALG,
				&halo_sts, sizeof(halo_sts));

	if (ret) {
		dev_err(cs35l41->dev, "Timeout waiting for HALO Core to start. State: %u\n",
			 halo_sts);
		goto clean_dsp;
	}

	ret = regmap_read(cs35l41->regmap, CS35L41_DSP_MBOX_2, &fw_status);
	if (ret < 0) {
		dev_err(cs35l41->dev,
			"Failed to read firmware status: %d\n", ret);
		goto clean_dsp;
	}

	switch (fw_status) {
	case CSPL_MBOX_STS_RUNNING:
	case CSPL_MBOX_STS_PAUSED:
		break;
	default:
		dev_err(cs35l41->dev, "Firmware status is invalid: %u\n",
			fw_status);
		ret = -EINVAL;
		goto clean_dsp;
	}

	ret = cs35l41_set_cspl_mbox_cmd(cs35l41->dev, cs35l41->regmap, CSPL_MBOX_CMD_PAUSE);
	if (ret) {
		dev_err(cs35l41->dev, "Error waiting for DSP to pause: %u\n", ret);
		goto clean_dsp;
	}

	dev_info(cs35l41->dev, "Firmware Loaded - Type: %s, Gain: %d\n",
		 hda_cs_dsp_fw_ids[cs35l41->firmware_type], cs35l41->tuning_gain);

	return 0;

clean_dsp:
	cs35l41_shutdown_dsp(cs35l41);
	return ret;
}

static void cs35l41_load_firmware(struct cs35l41_hda *cs35l41, bool load)
{
	if (cs35l41->cs_dsp.running && !load) {
		dev_dbg(cs35l41->dev, "Unloading Firmware\n");
		cs35l41_shutdown_dsp(cs35l41);
	} else if (!cs35l41->cs_dsp.running && load) {
		dev_dbg(cs35l41->dev, "Loading Firmware\n");
		cs35l41_smart_amp(cs35l41);
	} else {
		dev_dbg(cs35l41->dev, "Unable to Load firmware.\n");
	}
}

static int cs35l41_fw_load_ctl_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct cs35l41_hda *cs35l41 = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = cs35l41->request_fw_load;
	return 0;
}

static int cs35l41_mute_override_ctl_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct cs35l41_hda *cs35l41 = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = cs35l41->mute_override;
	return 0;
}

static void cs35l41_fw_load_work(struct work_struct *work)
{
	struct cs35l41_hda *cs35l41 = container_of(work, struct cs35l41_hda, fw_load_work);

	pm_runtime_get_sync(cs35l41->dev);

	mutex_lock(&cs35l41->fw_mutex);

	/* Recheck if playback is ongoing, mutex will block playback during firmware loading */
	if (cs35l41->playback_started)
		dev_err(cs35l41->dev, "Cannot Load/Unload firmware during Playback. Retrying...\n");
	else
		cs35l41_load_firmware(cs35l41, cs35l41->request_fw_load);

	cs35l41->fw_request_ongoing = false;
	mutex_unlock(&cs35l41->fw_mutex);

	pm_runtime_mark_last_busy(cs35l41->dev);
	pm_runtime_put_autosuspend(cs35l41->dev);
}

static int cs35l41_fw_load_ctl_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct cs35l41_hda *cs35l41 = snd_kcontrol_chip(kcontrol);

	if (cs35l41->request_fw_load == ucontrol->value.integer.value[0])
		return 0;

	if (cs35l41->fw_request_ongoing) {
		dev_dbg(cs35l41->dev, "Existing request not complete\n");
		return -EBUSY;
	}

	/* Check if playback is ongoing when initial request is made */
	if (cs35l41->playback_started) {
		dev_err(cs35l41->dev, "Cannot Load/Unload firmware during Playback\n");
		return -EBUSY;
	}

	cs35l41->fw_request_ongoing = true;
	cs35l41->request_fw_load = ucontrol->value.integer.value[0];
	schedule_work(&cs35l41->fw_load_work);

	return 1;
}

static int cs35l41_fw_type_ctl_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct cs35l41_hda *cs35l41 = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = cs35l41->firmware_type;

	return 0;
}

static int cs35l41_fw_type_ctl_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct cs35l41_hda *cs35l41 = snd_kcontrol_chip(kcontrol);

	if (ucontrol->value.enumerated.item[0] < HDA_CS_DSP_NUM_FW) {
		if (cs35l41->firmware_type != ucontrol->value.enumerated.item[0]) {
			cs35l41->firmware_type = ucontrol->value.enumerated.item[0];
			return 1;
		} else {
			return 0;
		}
	}

	return -EINVAL;
}

static int cs35l41_fw_type_ctl_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	return snd_ctl_enum_info(uinfo, 1, ARRAY_SIZE(hda_cs_dsp_fw_ids), hda_cs_dsp_fw_ids);
}

static int cs35l41_create_controls(struct cs35l41_hda *cs35l41)
{
	char fw_type_ctl_name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	char fw_load_ctl_name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	char mute_override_ctl_name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	struct snd_kcontrol_new fw_type_ctl = {
		.name = fw_type_ctl_name,
		.iface = SNDRV_CTL_ELEM_IFACE_CARD,
		.info = cs35l41_fw_type_ctl_info,
		.get = cs35l41_fw_type_ctl_get,
		.put = cs35l41_fw_type_ctl_put,
	};
	struct snd_kcontrol_new fw_load_ctl = {
		.name = fw_load_ctl_name,
		.iface = SNDRV_CTL_ELEM_IFACE_CARD,
		.info = snd_ctl_boolean_mono_info,
		.get = cs35l41_fw_load_ctl_get,
		.put = cs35l41_fw_load_ctl_put,
	};
	struct snd_kcontrol_new mute_override_ctl = {
		.name = mute_override_ctl_name,
		.iface = SNDRV_CTL_ELEM_IFACE_CARD,
		.info = snd_ctl_boolean_mono_info,
		.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.get = cs35l41_mute_override_ctl_get,
	};
	int ret;

	scnprintf(fw_type_ctl_name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN, "%s DSP1 Firmware Type",
		  cs35l41->amp_name);
	scnprintf(fw_load_ctl_name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN, "%s DSP1 Firmware Load",
		  cs35l41->amp_name);
	scnprintf(mute_override_ctl_name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN, "%s Forced Mute Status",
		  cs35l41->amp_name);

	ret = snd_ctl_add(cs35l41->codec->card, snd_ctl_new1(&fw_type_ctl, cs35l41));
	if (ret) {
		dev_err(cs35l41->dev, "Failed to add KControl %s = %d\n", fw_type_ctl.name, ret);
		return ret;
	}

	dev_dbg(cs35l41->dev, "Added Control %s\n", fw_type_ctl.name);

	ret = snd_ctl_add(cs35l41->codec->card, snd_ctl_new1(&fw_load_ctl, cs35l41));
	if (ret) {
		dev_err(cs35l41->dev, "Failed to add KControl %s = %d\n", fw_load_ctl.name, ret);
		return ret;
	}

	dev_dbg(cs35l41->dev, "Added Control %s\n", fw_load_ctl.name);

	ret = snd_ctl_add(cs35l41->codec->card, snd_ctl_new1(&mute_override_ctl, cs35l41));
	if (ret) {
		dev_err(cs35l41->dev, "Failed to add KControl %s = %d\n", mute_override_ctl.name,
			ret);
		return ret;
	}

	dev_dbg(cs35l41->dev, "Added Control %s\n", mute_override_ctl.name);

	return 0;
}

static bool cs35l41_dsm_supported(acpi_handle handle, unsigned int commands)
{
	guid_t guid;

	guid_parse(CS35L41_UUID, &guid);

	return acpi_check_dsm(handle, &guid, 0, BIT(commands));
}

static int cs35l41_get_acpi_mute_state(struct cs35l41_hda *cs35l41, acpi_handle handle)
{
	guid_t guid;
	union acpi_object *ret;
	int mute = -ENODEV;

	guid_parse(CS35L41_UUID, &guid);

	if (cs35l41_dsm_supported(handle, CS35L41_DSM_GET_MUTE)) {
		ret = acpi_evaluate_dsm(handle, &guid, 0, CS35L41_DSM_GET_MUTE, NULL);
		mute = *ret->buffer.pointer;
		dev_dbg(cs35l41->dev, "CS35L41_DSM_GET_MUTE: %d\n", mute);
	}

	dev_dbg(cs35l41->dev, "%s: %d\n", __func__, mute);

	return mute;
}

static void cs35l41_acpi_device_notify(acpi_handle handle, u32 event, struct device *dev)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	int mute;

	if (event != CS35L41_NOTIFY_EVENT)
		return;

	mute = cs35l41_get_acpi_mute_state(cs35l41, handle);
	if (mute < 0) {
		dev_warn(cs35l41->dev, "Unable to retrieve mute state: %d\n", mute);
		return;
	}

	dev_dbg(cs35l41->dev, "Requesting mute value: %d\n", mute);
	cs35l41->mute_override = (mute > 0);
	cs35l41_mute(cs35l41->dev, cs35l41->mute_override);
}

static int cs35l41_hda_bind(struct device *dev, struct device *master, void *master_data)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	struct hda_component_parent *parent = master_data;
	struct hda_component *comp;
	unsigned int sleep_flags;
	int ret = 0;

	comp = hda_component_from_index(parent, cs35l41->index);
	if (!comp)
		return -EINVAL;

	if (comp->dev)
		return -EBUSY;

	pm_runtime_get_sync(dev);

	mutex_lock(&cs35l41->fw_mutex);

	comp->dev = dev;
	cs35l41->codec = parent->codec;
	if (!cs35l41->acpi_subsystem_id)
		cs35l41->acpi_subsystem_id = kasprintf(GFP_KERNEL, "%.8x",
						       cs35l41->codec->core.subsystem_id);

	strscpy(comp->name, dev_name(dev), sizeof(comp->name));

	cs35l41->firmware_type = HDA_CS_DSP_FW_SPK_PROT;

	if (firmware_autostart) {
		dev_dbg(cs35l41->dev, "Firmware Autostart.\n");
		cs35l41->request_fw_load = true;
		if (cs35l41_smart_amp(cs35l41) < 0)
			dev_warn(cs35l41->dev, "Cannot Run Firmware, reverting to dsp bypass...\n");
	} else {
		dev_dbg(cs35l41->dev, "Firmware Autostart is disabled.\n");
	}

	ret = cs35l41_create_controls(cs35l41);

	comp->playback_hook = cs35l41_hda_playback_hook;
	comp->pre_playback_hook = cs35l41_hda_pre_playback_hook;
	comp->post_playback_hook = cs35l41_hda_post_playback_hook;
	comp->acpi_notify = cs35l41_acpi_device_notify;
	comp->adev = cs35l41->dacpi;

	comp->acpi_notifications_supported = cs35l41_dsm_supported(acpi_device_handle(comp->adev),
		CS35L41_DSM_GET_MUTE);

	cs35l41->mute_override = cs35l41_get_acpi_mute_state(cs35l41,
						acpi_device_handle(cs35l41->dacpi)) > 0;

	mutex_unlock(&cs35l41->fw_mutex);

	sleep_flags = lock_system_sleep();
	if (!device_link_add(&cs35l41->codec->core.dev, cs35l41->dev, DL_FLAG_STATELESS))
		dev_warn(dev, "Unable to create device link\n");
	unlock_system_sleep(sleep_flags);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	dev_info(cs35l41->dev,
		 "CS35L41 Bound - SSID: %s, BST: %d, VSPK: %d, CH: %c, FW EN: %d, SPKID: %d\n",
		 cs35l41->acpi_subsystem_id, cs35l41->hw_cfg.bst_type,
		 cs35l41->hw_cfg.gpio1.func == CS35l41_VSPK_SWITCH,
		 cs35l41->hw_cfg.spk_pos ? 'R' : 'L',
		 cs35l41->cs_dsp.running, cs35l41->speaker_id);

	return ret;
}

static void cs35l41_hda_unbind(struct device *dev, struct device *master, void *master_data)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	struct hda_component_parent *parent = master_data;
	struct hda_component *comp;
	unsigned int sleep_flags;

	comp = hda_component_from_index(parent, cs35l41->index);
	if (!comp)
		return;

	if (comp->dev == dev) {
		sleep_flags = lock_system_sleep();
		device_link_remove(&cs35l41->codec->core.dev, cs35l41->dev);
		unlock_system_sleep(sleep_flags);
		memset(comp, 0, sizeof(*comp));
	}
}

static const struct component_ops cs35l41_hda_comp_ops = {
	.bind = cs35l41_hda_bind,
	.unbind = cs35l41_hda_unbind,
};

static irqreturn_t cs35l41_bst_short_err(int irq, void *data)
{
	struct cs35l41_hda *cs35l41 = data;

	dev_crit_ratelimited(cs35l41->dev, "LBST Error\n");
	set_bit(CS35L41_BST_SHORT_ERR_RLS_SHIFT, &cs35l41->irq_errors);

	return IRQ_HANDLED;
}

static irqreturn_t cs35l41_bst_dcm_uvp_err(int irq, void *data)
{
	struct cs35l41_hda *cs35l41 = data;

	dev_crit_ratelimited(cs35l41->dev, "DCM VBST Under Voltage Error\n");
	set_bit(CS35L41_BST_UVP_ERR_RLS_SHIFT, &cs35l41->irq_errors);

	return IRQ_HANDLED;
}

static irqreturn_t cs35l41_bst_ovp_err(int irq, void *data)
{
	struct cs35l41_hda *cs35l41 = data;

	dev_crit_ratelimited(cs35l41->dev, "VBST Over Voltage error\n");
	set_bit(CS35L41_BST_OVP_ERR_RLS_SHIFT, &cs35l41->irq_errors);

	return IRQ_HANDLED;
}

static irqreturn_t cs35l41_temp_err(int irq, void *data)
{
	struct cs35l41_hda *cs35l41 = data;

	dev_crit_ratelimited(cs35l41->dev, "Over temperature error\n");
	set_bit(CS35L41_TEMP_ERR_RLS_SHIFT, &cs35l41->irq_errors);

	return IRQ_HANDLED;
}

static irqreturn_t cs35l41_temp_warn(int irq, void *data)
{
	struct cs35l41_hda *cs35l41 = data;

	dev_crit_ratelimited(cs35l41->dev, "Over temperature warning\n");
	set_bit(CS35L41_TEMP_WARN_ERR_RLS_SHIFT, &cs35l41->irq_errors);

	return IRQ_HANDLED;
}

static irqreturn_t cs35l41_amp_short(int irq, void *data)
{
	struct cs35l41_hda *cs35l41 = data;

	dev_crit_ratelimited(cs35l41->dev, "Amp short error\n");
	set_bit(CS35L41_AMP_SHORT_ERR_RLS_SHIFT, &cs35l41->irq_errors);

	return IRQ_HANDLED;
}

static const struct cs35l41_irq cs35l41_irqs[] = {
	CS35L41_IRQ(BST_OVP_ERR, "Boost Overvoltage Error", cs35l41_bst_ovp_err),
	CS35L41_IRQ(BST_DCM_UVP_ERR, "Boost Undervoltage Error", cs35l41_bst_dcm_uvp_err),
	CS35L41_IRQ(BST_SHORT_ERR, "Boost Inductor Short Error", cs35l41_bst_short_err),
	CS35L41_IRQ(TEMP_WARN, "Temperature Warning", cs35l41_temp_warn),
	CS35L41_IRQ(TEMP_ERR, "Temperature Error", cs35l41_temp_err),
	CS35L41_IRQ(AMP_SHORT_ERR, "Amp Short", cs35l41_amp_short),
};

static const struct regmap_irq cs35l41_reg_irqs[] = {
	CS35L41_REG_IRQ(IRQ1_STATUS1, BST_OVP_ERR),
	CS35L41_REG_IRQ(IRQ1_STATUS1, BST_DCM_UVP_ERR),
	CS35L41_REG_IRQ(IRQ1_STATUS1, BST_SHORT_ERR),
	CS35L41_REG_IRQ(IRQ1_STATUS1, TEMP_WARN),
	CS35L41_REG_IRQ(IRQ1_STATUS1, TEMP_ERR),
	CS35L41_REG_IRQ(IRQ1_STATUS1, AMP_SHORT_ERR),
};

static struct regmap_irq_chip cs35l41_regmap_irq_chip = {
	.name = "cs35l41 IRQ1 Controller",
	.status_base = CS35L41_IRQ1_STATUS1,
	.mask_base = CS35L41_IRQ1_MASK1,
	.ack_base = CS35L41_IRQ1_STATUS1,
	.num_regs = 4,
	.irqs = cs35l41_reg_irqs,
	.num_irqs = ARRAY_SIZE(cs35l41_reg_irqs),
	.runtime_pm = true,
};

static void cs35l41_configure_interrupt(struct cs35l41_hda *cs35l41, int irq_pol)
{
	int irq;
	int ret;
	int i;

	if (!cs35l41->irq) {
		dev_warn(cs35l41->dev, "No Interrupt Found");
		goto err;
	}

	ret = devm_regmap_add_irq_chip(cs35l41->dev, cs35l41->regmap, cs35l41->irq,
					IRQF_ONESHOT | IRQF_SHARED | irq_pol,
					0, &cs35l41_regmap_irq_chip, &cs35l41->irq_data);
	if (ret) {
		dev_dbg(cs35l41->dev, "Unable to add IRQ Chip: %d.", ret);
		goto err;
	}

	for (i = 0; i < ARRAY_SIZE(cs35l41_irqs); i++) {
		irq = regmap_irq_get_virq(cs35l41->irq_data, cs35l41_irqs[i].irq);
		if (irq < 0) {
			ret = irq;
			dev_dbg(cs35l41->dev, "Unable to map IRQ %s: %d.", cs35l41_irqs[i].name,
				ret);
			goto err;
		}

		ret = devm_request_threaded_irq(cs35l41->dev, irq, NULL,
						cs35l41_irqs[i].handler,
						IRQF_ONESHOT | IRQF_SHARED | irq_pol,
						cs35l41_irqs[i].name, cs35l41);
		if (ret) {
			dev_dbg(cs35l41->dev, "Unable to allocate IRQ %s:: %d.",
				cs35l41_irqs[i].name, ret);
			goto err;
		}
	}
	return;
err:
	dev_warn(cs35l41->dev,
		 "IRQ Config Failed. Amp errors may not be recoverable without reboot.");
}

static int cs35l41_hda_apply_properties(struct cs35l41_hda *cs35l41)
{
	struct cs35l41_hw_cfg *hw_cfg = &cs35l41->hw_cfg;
	bool using_irq = false;
	int irq_pol;
	int ret;

	if (!cs35l41->hw_cfg.valid)
		return -EINVAL;

	ret = cs35l41_init_boost(cs35l41->dev, cs35l41->regmap, hw_cfg);
	if (ret)
		return ret;

	if (hw_cfg->gpio1.valid) {
		switch (hw_cfg->gpio1.func) {
		case CS35L41_NOT_USED:
			break;
		case CS35l41_VSPK_SWITCH:
			hw_cfg->gpio1.func = CS35L41_GPIO1_GPIO;
			hw_cfg->gpio1.out_en = true;
			break;
		case CS35l41_SYNC:
			hw_cfg->gpio1.func = CS35L41_GPIO1_MDSYNC;
			break;
		default:
			dev_err(cs35l41->dev, "Invalid function %d for GPIO1\n",
				hw_cfg->gpio1.func);
			return -EINVAL;
		}
	}

	if (hw_cfg->gpio2.valid) {
		switch (hw_cfg->gpio2.func) {
		case CS35L41_NOT_USED:
			break;
		case CS35L41_INTERRUPT:
			using_irq = true;
			hw_cfg->gpio2.func = CS35L41_GPIO2_INT_OPEN_DRAIN;
			break;
		default:
			dev_err(cs35l41->dev, "Invalid GPIO2 function %d\n", hw_cfg->gpio2.func);
			return -EINVAL;
		}
	}

	irq_pol = cs35l41_gpio_config(cs35l41->regmap, hw_cfg);

	if (using_irq)
		cs35l41_configure_interrupt(cs35l41, irq_pol);

	return cs35l41_hda_channel_map(cs35l41->dev, 0, NULL, 1, &hw_cfg->spk_pos);
}

int cs35l41_get_speaker_id(struct device *dev, int amp_index, int num_amps, int fixed_gpio_id)
{
	struct gpio_desc *speaker_id_desc;
	int speaker_id = -ENODEV;

	if (fixed_gpio_id >= 0) {
		dev_dbg(dev, "Found Fixed Speaker ID GPIO (index = %d)\n", fixed_gpio_id);
		speaker_id_desc = gpiod_get_index(dev, NULL, fixed_gpio_id, GPIOD_IN);
		if (IS_ERR(speaker_id_desc)) {
			speaker_id = PTR_ERR(speaker_id_desc);
			return speaker_id;
		}
		speaker_id = gpiod_get_value_cansleep(speaker_id_desc);
		gpiod_put(speaker_id_desc);
		dev_dbg(dev, "Speaker ID = %d\n", speaker_id);
	} else {
		int base_index;
		int gpios_per_amp;
		int count;
		int tmp;
		int i;

		count = gpiod_count(dev, "spk-id");
		if (count > 0) {
			speaker_id = 0;
			gpios_per_amp = count / num_amps;
			base_index = gpios_per_amp * amp_index;

			if (count % num_amps)
				return -EINVAL;

			dev_dbg(dev, "Found %d Speaker ID GPIOs per Amp\n", gpios_per_amp);

			for (i = 0; i < gpios_per_amp; i++) {
				speaker_id_desc = gpiod_get_index(dev, "spk-id", i + base_index,
								  GPIOD_IN);
				if (IS_ERR(speaker_id_desc)) {
					speaker_id = PTR_ERR(speaker_id_desc);
					break;
				}
				tmp = gpiod_get_value_cansleep(speaker_id_desc);
				gpiod_put(speaker_id_desc);
				if (tmp < 0) {
					speaker_id = tmp;
					break;
				}
				speaker_id |= tmp << i;
			}
			dev_dbg(dev, "Speaker ID = %d\n", speaker_id);
		}
	}
	return speaker_id;
}

int cs35l41_hda_parse_acpi(struct cs35l41_hda *cs35l41, struct device *physdev, int id)
{
	struct cs35l41_hw_cfg *hw_cfg = &cs35l41->hw_cfg;
	u32 values[HDA_MAX_COMPONENTS];
	char *property;
	size_t nval;
	int i, ret;

	property = "cirrus,dev-index";
	ret = device_property_count_u32(physdev, property);
	if (ret <= 0)
		goto err;

	if (ret > ARRAY_SIZE(values)) {
		ret = -EINVAL;
		goto err;
	}
	nval = ret;

	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret)
		goto err;

	cs35l41->index = -1;
	for (i = 0; i < nval; i++) {
		if (values[i] == id) {
			cs35l41->index = i;
			break;
		}
	}
	if (cs35l41->index == -1) {
		dev_err(cs35l41->dev, "No index found in %s\n", property);
		ret = -ENODEV;
		goto err;
	}

	/* To use the same release code for all laptop variants we can't use devm_ version of
	 * gpiod_get here, as CLSA010* don't have a fully functional bios with an _DSD node
	 */
	cs35l41->reset_gpio = fwnode_gpiod_get_index(acpi_fwnode_handle(cs35l41->dacpi), "reset",
						     cs35l41->index, GPIOD_OUT_LOW,
						     "cs35l41-reset");

	property = "cirrus,speaker-position";
	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret)
		goto err;
	hw_cfg->spk_pos = values[cs35l41->index];

	cs35l41->channel_index = 0;
	for (i = 0; i < cs35l41->index; i++)
		if (values[i] == hw_cfg->spk_pos)
			cs35l41->channel_index++;

	property = "cirrus,gpio1-func";
	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret)
		goto err;
	hw_cfg->gpio1.func = values[cs35l41->index];
	hw_cfg->gpio1.valid = true;

	property = "cirrus,gpio2-func";
	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret)
		goto err;
	hw_cfg->gpio2.func = values[cs35l41->index];
	hw_cfg->gpio2.valid = true;

	property = "cirrus,boost-peak-milliamp";
	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret == 0)
		hw_cfg->bst_ipk = values[cs35l41->index];
	else
		hw_cfg->bst_ipk = -1;

	property = "cirrus,boost-ind-nanohenry";
	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret == 0)
		hw_cfg->bst_ind = values[cs35l41->index];
	else
		hw_cfg->bst_ind = -1;

	property = "cirrus,boost-cap-microfarad";
	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret == 0)
		hw_cfg->bst_cap = values[cs35l41->index];
	else
		hw_cfg->bst_cap = -1;

	cs35l41->speaker_id = cs35l41_get_speaker_id(physdev, cs35l41->index, nval, -1);

	if (hw_cfg->bst_ind > 0 || hw_cfg->bst_cap > 0 || hw_cfg->bst_ipk > 0)
		hw_cfg->bst_type = CS35L41_INT_BOOST;
	else
		hw_cfg->bst_type = CS35L41_EXT_BOOST;

	hw_cfg->valid = true;

	return 0;
err:
	dev_err(cs35l41->dev, "Failed property %s: %d\n", property, ret);
	hw_cfg->valid = false;
	hw_cfg->gpio1.valid = false;
	hw_cfg->gpio2.valid = false;
	acpi_dev_put(cs35l41->dacpi);

	return ret;
}

static int cs35l41_hda_read_acpi(struct cs35l41_hda *cs35l41, const char *hid, int id)
{
	struct acpi_device *adev;
	struct device *physdev;
	struct spi_device *spi;
	const char *sub;
	int ret;

	adev = acpi_dev_get_first_match_dev(hid, NULL, -1);
	if (!adev) {
		dev_err(cs35l41->dev, "Failed to find an ACPI device for %s\n", hid);
		return -ENODEV;
	}

	cs35l41->dacpi = adev;
	physdev = get_device(acpi_get_first_physical_node(adev));

	sub = acpi_get_subsystem_id(ACPI_HANDLE(physdev));
	if (IS_ERR(sub))
		sub = NULL;
	cs35l41->acpi_subsystem_id = sub;

	ret = cs35l41_add_dsd_properties(cs35l41, physdev, id, hid);
	if (!ret) {
		dev_info(cs35l41->dev, "Using extra _DSD properties, bypassing _DSD in ACPI\n");
		goto out;
	}

	ret = cs35l41_hda_parse_acpi(cs35l41, physdev, id);
	if (ret) {
		put_device(physdev);
		return ret;
	}
out:
	put_device(physdev);

	cs35l41->bypass_fw = false;
	if (cs35l41->control_bus == SPI) {
		spi = to_spi_device(cs35l41->dev);
		if (spi->max_speed_hz < CS35L41_MAX_ACCEPTABLE_SPI_SPEED_HZ) {
			dev_warn(cs35l41->dev,
				 "SPI speed is too slow to support firmware download: %d Hz.\n",
				 spi->max_speed_hz);
			cs35l41->bypass_fw = true;
		}
	}

	return 0;
}

int cs35l41_hda_probe(struct device *dev, const char *device_name, int id, int irq,
		      struct regmap *regmap, enum control_bus control_bus)
{
	unsigned int regid, reg_revid;
	struct cs35l41_hda *cs35l41;
	int ret;

	BUILD_BUG_ON(ARRAY_SIZE(cs35l41_irqs) != ARRAY_SIZE(cs35l41_reg_irqs));
	BUILD_BUG_ON(ARRAY_SIZE(cs35l41_irqs) != CS35L41_NUM_IRQ);

	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	cs35l41 = devm_kzalloc(dev, sizeof(*cs35l41), GFP_KERNEL);
	if (!cs35l41)
		return -ENOMEM;

	cs35l41->dev = dev;
	cs35l41->irq = irq;
	cs35l41->regmap = regmap;
	cs35l41->control_bus = control_bus;
	dev_set_drvdata(dev, cs35l41);

	ret = cs35l41_hda_read_acpi(cs35l41, device_name, id);
	if (ret)
		return dev_err_probe(cs35l41->dev, ret, "Platform not supported\n");

	if (IS_ERR(cs35l41->reset_gpio)) {
		ret = PTR_ERR(cs35l41->reset_gpio);
		cs35l41->reset_gpio = NULL;
		if (ret == -EBUSY) {
			dev_info(cs35l41->dev, "Reset line busy, assuming shared reset\n");
		} else {
			dev_err_probe(cs35l41->dev, ret, "Failed to get reset GPIO\n");
			goto err;
		}
	}
	if (cs35l41->reset_gpio) {
		gpiod_set_value_cansleep(cs35l41->reset_gpio, 0);
		usleep_range(2000, 2100);
		gpiod_set_value_cansleep(cs35l41->reset_gpio, 1);
	}

	usleep_range(2000, 2100);
	regmap_write(cs35l41->regmap, CS35L41_SFT_RESET, CS35L41_SOFTWARE_RESET);
	usleep_range(2000, 2100);

	ret = cs35l41_wait_boot_done(cs35l41);
	if (ret)
		goto err;

	ret = cs35l41_verify_id(cs35l41, &regid, &reg_revid);
	if (ret)
		goto err;

	ret = cs35l41_test_key_unlock(cs35l41->dev, cs35l41->regmap);
	if (ret)
		goto err;

	ret = cs35l41_register_errata_patch(cs35l41->dev, cs35l41->regmap, reg_revid);
	if (ret)
		goto err;

	ret = cs35l41_otp_unpack(cs35l41->dev, cs35l41->regmap);
	if (ret) {
		dev_err_probe(cs35l41->dev, ret, "OTP Unpack failed\n");
		goto err;
	}

	ret = cs35l41_test_key_lock(cs35l41->dev, cs35l41->regmap);
	if (ret)
		goto err;

	ret = cs35l41_get_calibration(cs35l41);
	if (ret && ret != -ENOENT)
		goto err;

	cs35l41_mute(cs35l41->dev, true);

	INIT_WORK(&cs35l41->fw_load_work, cs35l41_fw_load_work);
	mutex_init(&cs35l41->fw_mutex);

	pm_runtime_set_autosuspend_delay(cs35l41->dev, 3000);
	pm_runtime_use_autosuspend(cs35l41->dev);
	pm_runtime_mark_last_busy(cs35l41->dev);
	pm_runtime_set_active(cs35l41->dev);
	pm_runtime_get_noresume(cs35l41->dev);
	pm_runtime_enable(cs35l41->dev);

	ret = cs35l41_hda_apply_properties(cs35l41);
	if (ret)
		goto err_pm;

	pm_runtime_put_autosuspend(cs35l41->dev);

	ret = component_add(cs35l41->dev, &cs35l41_hda_comp_ops);
	if (ret) {
		dev_err_probe(cs35l41->dev, ret, "Register component failed\n");
		goto err_pm;
	}

	dev_info(cs35l41->dev, "Cirrus Logic CS35L41 (%x), Revision: %02X\n", regid, reg_revid);

	return 0;

err_pm:
	pm_runtime_dont_use_autosuspend(cs35l41->dev);
	pm_runtime_disable(cs35l41->dev);
	pm_runtime_put_noidle(cs35l41->dev);

err:
	if (cs35l41_safe_reset(cs35l41->regmap, cs35l41->hw_cfg.bst_type))
		gpiod_set_value_cansleep(cs35l41->reset_gpio, 0);
	gpiod_put(cs35l41->reset_gpio);
	gpiod_put(cs35l41->cs_gpio);
	acpi_dev_put(cs35l41->dacpi);
	kfree(cs35l41->acpi_subsystem_id);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs35l41_hda_probe, SND_HDA_SCODEC_CS35L41);

void cs35l41_hda_remove(struct device *dev)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);

	component_del(cs35l41->dev, &cs35l41_hda_comp_ops);

	pm_runtime_get_sync(cs35l41->dev);
	pm_runtime_dont_use_autosuspend(cs35l41->dev);
	pm_runtime_disable(cs35l41->dev);

	if (cs35l41->halo_initialized)
		cs35l41_remove_dsp(cs35l41);

	acpi_dev_put(cs35l41->dacpi);

	pm_runtime_put_noidle(cs35l41->dev);

	if (cs35l41_safe_reset(cs35l41->regmap, cs35l41->hw_cfg.bst_type))
		gpiod_set_value_cansleep(cs35l41->reset_gpio, 0);
	gpiod_put(cs35l41->reset_gpio);
	gpiod_put(cs35l41->cs_gpio);
	kfree(cs35l41->acpi_subsystem_id);
}
EXPORT_SYMBOL_NS_GPL(cs35l41_hda_remove, SND_HDA_SCODEC_CS35L41);

const struct dev_pm_ops cs35l41_hda_pm_ops = {
	RUNTIME_PM_OPS(cs35l41_runtime_suspend, cs35l41_runtime_resume,
		       cs35l41_runtime_idle)
	.prepare = cs35l41_system_suspend_prep,
	SYSTEM_SLEEP_PM_OPS(cs35l41_system_suspend, cs35l41_system_resume)
};
EXPORT_SYMBOL_NS_GPL(cs35l41_hda_pm_ops, SND_HDA_SCODEC_CS35L41);

MODULE_DESCRIPTION("CS35L41 HDA Driver");
MODULE_IMPORT_NS(SND_HDA_CS_DSP_CONTROLS);
MODULE_IMPORT_NS(SND_SOC_CS_AMP_LIB);
MODULE_AUTHOR("Lucas Tanure, Cirrus Logic Inc, <tanureal@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(FW_CS_DSP);
