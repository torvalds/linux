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
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include "hda_generic.h"
#include "hda_component.h"
#include "cs35l41_hda.h"
#include "hda_cs_dsp_ctl.h"

#define CS35L41_FIRMWARE_ROOT "cirrus/"
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
	{ CS35L41_AMP_DIG_VOL_CTRL,	0x00008000 }, // AMP_HPF_PCM_EN = 1, AMP_VOL_PCM  0.0 dB
	{ CS35L41_AMP_GAIN_CTRL,	0x00000084 }, // AMP_GAIN_PCM 4.5 dB
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
	{ CS35L41_DAC_PCM1_SRC,		0x00000032 }, // DACPCM1_SRC = ERR_VOL
	{ CS35L41_ASP_TX1_SRC,		0x00000018 }, // ASPTX1 SRC = VMON
	{ CS35L41_ASP_TX2_SRC,		0x00000019 }, // ASPTX2 SRC = IMON
	{ CS35L41_ASP_TX3_SRC,		0x00000028 }, // ASPTX3 SRC = VPMON
	{ CS35L41_ASP_TX4_SRC,		0x00000029 }, // ASPTX4 SRC = VBSTMON
	{ CS35L41_DSP1_RX1_SRC,		0x00000008 }, // DSP1RX1 SRC = ASPRX1
	{ CS35L41_DSP1_RX2_SRC,		0x00000008 }, // DSP1RX2 SRC = ASPRX1
	{ CS35L41_DSP1_RX3_SRC,         0x00000018 }, // DSP1RX3 SRC = VMON
	{ CS35L41_DSP1_RX4_SRC,         0x00000019 }, // DSP1RX4 SRC = IMON
	{ CS35L41_DSP1_RX5_SRC,         0x00000029 }, // DSP1RX5 SRC = VBSTMON
	{ CS35L41_AMP_DIG_VOL_CTRL,	0x00008000 }, // AMP_HPF_PCM_EN = 1, AMP_VOL_PCM  0.0 dB
	{ CS35L41_AMP_GAIN_CTRL,	0x00000233 }, // AMP_GAIN_PCM = 17.5dB AMP_GAIN_PDM = 19.5dB
};

static const struct reg_sequence cs35l41_hda_mute[] = {
	{ CS35L41_AMP_GAIN_CTRL,	0x00000000 }, // AMP_GAIN_PCM 0.5 dB
	{ CS35L41_AMP_DIG_VOL_CTRL,	0x0000A678 }, // AMP_HPF_PCM_EN = 1, AMP_VOL_PCM Mute
};

static void cs35l41_add_controls(struct cs35l41_hda *cs35l41)
{
	struct hda_cs_dsp_ctl_info info;

	info.device_name = cs35l41->amp_name;
	info.fw_type = cs35l41->firmware_type;
	info.card = cs35l41->codec->card;

	hda_cs_dsp_add_controls(&cs35l41->cs_dsp, &info);
}

static const struct cs_dsp_client_ops client_ops = {
	.control_remove = hda_cs_dsp_control_remove,
};

static int cs35l41_request_firmware_file(struct cs35l41_hda *cs35l41,
					 const struct firmware **firmware, char **filename,
					 const char *dir, const char *ssid, const char *amp_name,
					 int spkid, const char *filetype)
{
	const char * const dsp_name = cs35l41->cs_dsp.name;
	char *s, c;
	int ret = 0;

	if (spkid > -1 && ssid && amp_name)
		*filename = kasprintf(GFP_KERNEL, "%s%s-%s-%s-%s-spkid%d-%s.%s", dir, CS35L41_PART,
				      dsp_name, hda_cs_dsp_fw_ids[cs35l41->firmware_type],
				      ssid, spkid, amp_name, filetype);
	else if (spkid > -1 && ssid)
		*filename = kasprintf(GFP_KERNEL, "%s%s-%s-%s-%s-spkid%d.%s", dir, CS35L41_PART,
				      dsp_name, hda_cs_dsp_fw_ids[cs35l41->firmware_type],
				      ssid, spkid, filetype);
	else if (ssid && amp_name)
		*filename = kasprintf(GFP_KERNEL, "%s%s-%s-%s-%s-%s.%s", dir, CS35L41_PART,
				      dsp_name, hda_cs_dsp_fw_ids[cs35l41->firmware_type],
				      ssid, amp_name, filetype);
	else if (ssid)
		*filename = kasprintf(GFP_KERNEL, "%s%s-%s-%s-%s.%s", dir, CS35L41_PART,
				      dsp_name, hda_cs_dsp_fw_ids[cs35l41->firmware_type],
				      ssid, filetype);
	else
		*filename = kasprintf(GFP_KERNEL, "%s%s-%s-%s.%s", dir, CS35L41_PART,
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
					    CS35L41_FIRMWARE_ROOT,
					    cs35l41->acpi_subsystem_id, cs35l41->amp_name,
					    cs35l41->speaker_id, "wmfw");
	if (!ret) {
		/* try cirrus/part-dspN-fwtype-sub<-spkidN><-ampname>.bin */
		return cs35l41_request_firmware_file(cs35l41, coeff_firmware, coeff_filename,
						     CS35L41_FIRMWARE_ROOT,
						     cs35l41->acpi_subsystem_id, cs35l41->amp_name,
						     cs35l41->speaker_id, "bin");
	}

	/* try cirrus/part-dspN-fwtype-sub<-ampname>.wmfw */
	ret = cs35l41_request_firmware_file(cs35l41, wmfw_firmware, wmfw_filename,
					    CS35L41_FIRMWARE_ROOT, cs35l41->acpi_subsystem_id,
					    cs35l41->amp_name, -1, "wmfw");
	if (!ret) {
		/* try cirrus/part-dspN-fwtype-sub<-spkidN><-ampname>.bin */
		return cs35l41_request_firmware_file(cs35l41, coeff_firmware, coeff_filename,
						     CS35L41_FIRMWARE_ROOT,
						     cs35l41->acpi_subsystem_id, cs35l41->amp_name,
						     cs35l41->speaker_id, "bin");
	}

	/* try cirrus/part-dspN-fwtype-sub<-spkidN>.wmfw */
	ret = cs35l41_request_firmware_file(cs35l41, wmfw_firmware, wmfw_filename,
					    CS35L41_FIRMWARE_ROOT, cs35l41->acpi_subsystem_id,
					    NULL, cs35l41->speaker_id, "wmfw");
	if (!ret) {
		/* try cirrus/part-dspN-fwtype-sub<-spkidN><-ampname>.bin */
		ret = cs35l41_request_firmware_file(cs35l41, coeff_firmware, coeff_filename,
						    CS35L41_FIRMWARE_ROOT,
						    cs35l41->acpi_subsystem_id,
						    cs35l41->amp_name, cs35l41->speaker_id, "bin");
		if (ret)
			/* try cirrus/part-dspN-fwtype-sub<-spkidN>.bin */
			return cs35l41_request_firmware_file(cs35l41, coeff_firmware,
							     coeff_filename, CS35L41_FIRMWARE_ROOT,
							     cs35l41->acpi_subsystem_id, NULL,
							     cs35l41->speaker_id, "bin");
	}

	/* try cirrus/part-dspN-fwtype-sub.wmfw */
	ret = cs35l41_request_firmware_file(cs35l41, wmfw_firmware, wmfw_filename,
					    CS35L41_FIRMWARE_ROOT, cs35l41->acpi_subsystem_id,
					    NULL, -1, "wmfw");
	if (!ret) {
		/* try cirrus/part-dspN-fwtype-sub<-spkidN><-ampname>.bin */
		ret = cs35l41_request_firmware_file(cs35l41, coeff_firmware, coeff_filename,
						    CS35L41_FIRMWARE_ROOT,
						    cs35l41->acpi_subsystem_id, cs35l41->amp_name,
						    cs35l41->speaker_id, "bin");
		if (ret)
			/* try cirrus/part-dspN-fwtype-sub<-spkidN>.bin */
			return cs35l41_request_firmware_file(cs35l41, coeff_firmware,
							     coeff_filename, CS35L41_FIRMWARE_ROOT,
							     cs35l41->acpi_subsystem_id, NULL,
							     cs35l41->speaker_id, "bin");
	}

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
					    CS35L41_FIRMWARE_ROOT, cs35l41->acpi_subsystem_id,
					    cs35l41->amp_name, -1, "wmfw");
	if (!ret) {
		/* try cirrus/part-dspN-fwtype-sub<-ampname>.bin */
		ret = cs35l41_request_firmware_file(cs35l41, coeff_firmware, coeff_filename,
						    CS35L41_FIRMWARE_ROOT,
						    cs35l41->acpi_subsystem_id, cs35l41->amp_name,
						    -1, "bin");
		goto out;
	}

	/* try cirrus/part-dspN-fwtype-sub.wmfw */
	ret = cs35l41_request_firmware_file(cs35l41, wmfw_firmware, wmfw_filename,
					    CS35L41_FIRMWARE_ROOT, cs35l41->acpi_subsystem_id,
					    NULL, -1, "wmfw");
	if (!ret) {
		/* try cirrus/part-dspN-fwtype-sub<-ampname>.bin */
		ret = cs35l41_request_firmware_file(cs35l41, coeff_firmware, coeff_filename,
						    CS35L41_FIRMWARE_ROOT,
						    cs35l41->acpi_subsystem_id,
						    cs35l41->amp_name, -1, "bin");
		if (ret)
			/* try cirrus/part-dspN-fwtype-sub.bin */
			ret = cs35l41_request_firmware_file(cs35l41, coeff_firmware, coeff_filename,
							    CS35L41_FIRMWARE_ROOT,
							    cs35l41->acpi_subsystem_id, NULL, -1,
							    "bin");
	}

out:
	if (!ret)
		return 0;

	/* Handle fallback */
	dev_warn(cs35l41->dev, "Falling back to default firmware.\n");

	release_firmware(*wmfw_firmware);
	kfree(*wmfw_filename);

	/* fallback try cirrus/part-dspN-fwtype.wmfw */
	ret = cs35l41_request_firmware_file(cs35l41, wmfw_firmware, wmfw_filename,
					    CS35L41_FIRMWARE_ROOT, NULL, NULL, -1, "wmfw");
	if (!ret)
		/* fallback try cirrus/part-dspN-fwtype.bin */
		ret = cs35l41_request_firmware_file(cs35l41, coeff_firmware, coeff_filename,
						    CS35L41_FIRMWARE_ROOT, NULL, NULL, -1, "bin");

	if (ret) {
		release_firmware(*wmfw_firmware);
		kfree(*wmfw_filename);
		dev_warn(cs35l41->dev, "Unable to find firmware and tuning\n");
	}
	return ret;
}

#if IS_ENABLED(CONFIG_EFI)
static int cs35l41_apply_calibration(struct cs35l41_hda *cs35l41, unsigned int ambient,
				     unsigned int r0, unsigned int status, unsigned int checksum)
{
	int ret;

	ret = hda_cs_dsp_write_ctl(&cs35l41->cs_dsp, CAL_AMBIENT_DSP_CTL_NAME, CAL_DSP_CTL_TYPE,
				   CAL_DSP_CTL_ALG, &ambient, 4);
	if (ret) {
		dev_err(cs35l41->dev, "Cannot Write Control: %s - %d\n", CAL_AMBIENT_DSP_CTL_NAME,
			ret);
		return ret;
	}
	ret = hda_cs_dsp_write_ctl(&cs35l41->cs_dsp, CAL_R_DSP_CTL_NAME, CAL_DSP_CTL_TYPE,
				   CAL_DSP_CTL_ALG, &r0, 4);
	if (ret) {
		dev_err(cs35l41->dev, "Cannot Write Control: %s - %d\n", CAL_R_DSP_CTL_NAME, ret);
		return ret;
	}
	ret = hda_cs_dsp_write_ctl(&cs35l41->cs_dsp, CAL_STATUS_DSP_CTL_NAME, CAL_DSP_CTL_TYPE,
				   CAL_DSP_CTL_ALG, &status, 4);
	if (ret) {
		dev_err(cs35l41->dev, "Cannot Write Control: %s - %d\n", CAL_STATUS_DSP_CTL_NAME,
			ret);
		return ret;
	}
	ret = hda_cs_dsp_write_ctl(&cs35l41->cs_dsp, CAL_CHECKSUM_DSP_CTL_NAME, CAL_DSP_CTL_TYPE,
				   CAL_DSP_CTL_ALG, &checksum, 4);
	if (ret) {
		dev_err(cs35l41->dev, "Cannot Write Control: %s - %d\n", CAL_CHECKSUM_DSP_CTL_NAME,
			ret);
		return ret;
	}

	return 0;
}

static int cs35l41_save_calibration(struct cs35l41_hda *cs35l41)
{
	static efi_guid_t efi_guid = EFI_GUID(0x02f9af02, 0x7734, 0x4233, 0xb4, 0x3d, 0x93, 0xfe,
					      0x5a, 0xa3, 0x5d, 0xb3);
	static efi_char16_t efi_name[] = L"CirrusSmartAmpCalibrationData";
	const struct cs35l41_amp_efi_data *efi_data;
	const struct cs35l41_amp_cal_data *cl;
	unsigned long data_size = 0;
	efi_status_t status;
	int ret = 0;
	u8 *data = NULL;
	u32 attr;

	/* Get real size of UEFI variable */
	status = efi.get_variable(efi_name, &efi_guid, &attr, &data_size, data);
	if (status == EFI_BUFFER_TOO_SMALL) {
		ret = -ENODEV;
		/* Allocate data buffer of data_size bytes */
		data = vmalloc(data_size);
		if (!data)
			return -ENOMEM;
		/* Get variable contents into buffer */
		status = efi.get_variable(efi_name, &efi_guid, &attr, &data_size, data);
		if (status == EFI_SUCCESS) {
			efi_data = (struct cs35l41_amp_efi_data *)data;
			dev_dbg(cs35l41->dev, "Calibration: Size=%d, Amp Count=%d\n",
				efi_data->size, efi_data->count);
			if (efi_data->count > cs35l41->index) {
				cl = &efi_data->data[cs35l41->index];
				dev_dbg(cs35l41->dev,
					"Calibration: Ambient=%02x, Status=%02x, R0=%d\n",
					cl->calAmbient, cl->calStatus, cl->calR);

				/* Calibration can only be applied whilst the DSP is not running */
				ret = cs35l41_apply_calibration(cs35l41,
								cpu_to_be32(cl->calAmbient),
								cpu_to_be32(cl->calR),
								cpu_to_be32(cl->calStatus),
								cpu_to_be32(cl->calR + 1));
			}
		}
		vfree(data);
	}
	return ret;
}
#else
static int cs35l41_save_calibration(struct cs35l41_hda *cs35l41)
{
	dev_warn(cs35l41->dev, "Calibration not supported without EFI support.\n");
	return 0;
}
#endif

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

	ret = cs35l41_request_firmware_files(cs35l41, &wmfw_firmware, &wmfw_filename,
					     &coeff_firmware, &coeff_filename);
	if (ret < 0)
		return ret;

	dev_dbg(cs35l41->dev, "Loading WMFW Firmware: %s\n", wmfw_filename);
	if (coeff_filename)
		dev_dbg(cs35l41->dev, "Loading Coefficient File: %s\n", coeff_filename);
	else
		dev_warn(cs35l41->dev, "No Coefficient File available.\n");

	ret = cs_dsp_power_up(dsp, wmfw_firmware, wmfw_filename, coeff_firmware, coeff_filename,
			      hda_cs_dsp_fw_ids[cs35l41->firmware_type]);
	if (ret)
		goto err_release;

	cs35l41_add_controls(cs35l41);

	ret = cs35l41_save_calibration(cs35l41);

err_release:
	release_firmware(wmfw_firmware);
	release_firmware(coeff_firmware);
	kfree(wmfw_filename);
	kfree(coeff_filename);

	return ret;
}

static void cs35l41_shutdown_dsp(struct cs35l41_hda *cs35l41)
{
	struct cs_dsp *dsp = &cs35l41->cs_dsp;

	cs_dsp_stop(dsp);
	cs_dsp_power_down(dsp);
	cs35l41->firmware_running = false;
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

static void cs35l41_hda_playback_hook(struct device *dev, int action)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	struct regmap *reg = cs35l41->regmap;
	int ret = 0;

	switch (action) {
	case HDA_GEN_PCM_ACT_OPEN:
		pm_runtime_get_sync(dev);
		mutex_lock(&cs35l41->fw_mutex);
		cs35l41->playback_started = true;
		if (cs35l41->firmware_running) {
			regmap_multi_reg_write(reg, cs35l41_hda_config_dsp,
					       ARRAY_SIZE(cs35l41_hda_config_dsp));
			regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL2,
					   CS35L41_VMON_EN_MASK | CS35L41_IMON_EN_MASK,
					   1 << CS35L41_VMON_EN_SHIFT | 1 << CS35L41_IMON_EN_SHIFT);
			cs35l41_set_cspl_mbox_cmd(cs35l41->dev, cs35l41->regmap,
						  CSPL_MBOX_CMD_RESUME);
		} else {
			regmap_multi_reg_write(reg, cs35l41_hda_config,
					       ARRAY_SIZE(cs35l41_hda_config));
		}
		ret = regmap_update_bits(reg, CS35L41_PWR_CTRL2,
					 CS35L41_AMP_EN_MASK, 1 << CS35L41_AMP_EN_SHIFT);
		if (cs35l41->hw_cfg.bst_type == CS35L41_EXT_BOOST)
			regmap_write(reg, CS35L41_GPIO1_CTRL1, 0x00008001);
		mutex_unlock(&cs35l41->fw_mutex);
		break;
	case HDA_GEN_PCM_ACT_PREPARE:
		mutex_lock(&cs35l41->fw_mutex);
		ret = cs35l41_global_enable(reg, cs35l41->hw_cfg.bst_type, 1, NULL);
		mutex_unlock(&cs35l41->fw_mutex);
		break;
	case HDA_GEN_PCM_ACT_CLEANUP:
		mutex_lock(&cs35l41->fw_mutex);
		regmap_multi_reg_write(reg, cs35l41_hda_mute, ARRAY_SIZE(cs35l41_hda_mute));
		ret = cs35l41_global_enable(reg, cs35l41->hw_cfg.bst_type, 0, NULL);
		mutex_unlock(&cs35l41->fw_mutex);
		break;
	case HDA_GEN_PCM_ACT_CLOSE:
		mutex_lock(&cs35l41->fw_mutex);
		ret = regmap_update_bits(reg, CS35L41_PWR_CTRL2,
					 CS35L41_AMP_EN_MASK, 0 << CS35L41_AMP_EN_SHIFT);
		if (cs35l41->hw_cfg.bst_type == CS35L41_EXT_BOOST)
			regmap_write(reg, CS35L41_GPIO1_CTRL1, 0x00000001);
		if (cs35l41->firmware_running) {
			cs35l41_set_cspl_mbox_cmd(cs35l41->dev, cs35l41->regmap,
						  CSPL_MBOX_CMD_PAUSE);
			regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL2,
					   CS35L41_VMON_EN_MASK | CS35L41_IMON_EN_MASK,
					   0 << CS35L41_VMON_EN_SHIFT | 0 << CS35L41_IMON_EN_SHIFT);
		}
		cs35l41_irq_release(cs35l41);
		cs35l41->playback_started = false;
		mutex_unlock(&cs35l41->fw_mutex);

		pm_runtime_mark_last_busy(dev);
		pm_runtime_put_autosuspend(dev);
		break;
	default:
		dev_warn(cs35l41->dev, "Playback action not supported: %d\n", action);
		break;
	}

	if (ret)
		dev_err(cs35l41->dev, "Regmap access fail: %d\n", ret);
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

static void cs35l41_ready_for_reset(struct cs35l41_hda *cs35l41)
{
	mutex_lock(&cs35l41->fw_mutex);
	if (cs35l41->firmware_running) {

		regcache_cache_only(cs35l41->regmap, false);

		cs35l41_exit_hibernate(cs35l41->dev, cs35l41->regmap);
		cs35l41_shutdown_dsp(cs35l41);
		cs35l41_safe_reset(cs35l41->regmap, cs35l41->hw_cfg.bst_type);

		regcache_cache_only(cs35l41->regmap, true);
		regcache_mark_dirty(cs35l41->regmap);
	}
	mutex_unlock(&cs35l41->fw_mutex);
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

	ret = pm_runtime_force_suspend(dev);
	if (ret)
		return ret;

	/* Shutdown DSP before system suspend */
	cs35l41_ready_for_reset(cs35l41);

	/*
	 * Reset GPIO may be shared, so cannot reset here.
	 * However beyond this point, amps may be powered down.
	 */
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
		usleep_range(2000, 2100);
		gpiod_set_value_cansleep(cs35l41->reset_gpio, 1);
	}

	usleep_range(2000, 2100);

	ret = pm_runtime_force_resume(dev);

	mutex_lock(&cs35l41->fw_mutex);
	if (!ret && cs35l41->request_fw_load && !cs35l41->fw_request_ongoing) {
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

	if (cs35l41->playback_started) {
		regmap_multi_reg_write(cs35l41->regmap, cs35l41_hda_mute,
				       ARRAY_SIZE(cs35l41_hda_mute));
		cs35l41_global_enable(cs35l41->regmap, cs35l41->hw_cfg.bst_type, 0, NULL);
		regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL2,
				   CS35L41_AMP_EN_MASK, 0 << CS35L41_AMP_EN_SHIFT);
		if (cs35l41->hw_cfg.bst_type == CS35L41_EXT_BOOST)
			regmap_write(cs35l41->regmap, CS35L41_GPIO1_CTRL1, 0x00000001);
		regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL2,
				   CS35L41_VMON_EN_MASK | CS35L41_IMON_EN_MASK,
				   0 << CS35L41_VMON_EN_SHIFT | 0 << CS35L41_IMON_EN_SHIFT);
		cs35l41->playback_started = false;
	}

	if (cs35l41->firmware_running) {
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
	int ret = 0;

	dev_dbg(cs35l41->dev, "Runtime Resume\n");

	if (cs35l41->hw_cfg.bst_type == CS35L41_EXT_BOOST_NO_VSPK_SWITCH) {
		dev_dbg(cs35l41->dev, "Runtime Resume not supported\n");
		return 0;
	}

	mutex_lock(&cs35l41->fw_mutex);

	regcache_cache_only(cs35l41->regmap, false);

	if (cs35l41->firmware_running)	{
		ret = cs35l41_exit_hibernate(cs35l41->dev, cs35l41->regmap);
		if (ret) {
			dev_warn(cs35l41->dev, "Unable to exit Hibernate.");
			goto err;
		}
	}

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

err:
	mutex_unlock(&cs35l41->fw_mutex);

	return ret;
}

static int cs35l41_smart_amp(struct cs35l41_hda *cs35l41)
{
	int halo_sts;
	int ret;

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
		dev_err(cs35l41->dev, "Timeout waiting for HALO Core to start. State: %d\n",
			 halo_sts);
		goto clean_dsp;
	}

	cs35l41_set_cspl_mbox_cmd(cs35l41->dev, cs35l41->regmap, CSPL_MBOX_CMD_PAUSE);
	cs35l41->firmware_running = true;

	return 0;

clean_dsp:
	cs35l41_shutdown_dsp(cs35l41);
	return ret;
}

static void cs35l41_load_firmware(struct cs35l41_hda *cs35l41, bool load)
{
	if (cs35l41->firmware_running && !load) {
		dev_dbg(cs35l41->dev, "Unloading Firmware\n");
		cs35l41_shutdown_dsp(cs35l41);
	} else if (!cs35l41->firmware_running && load) {
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
	unsigned int ret = 0;

	mutex_lock(&cs35l41->fw_mutex);

	if (cs35l41->request_fw_load == ucontrol->value.integer.value[0])
		goto err;

	if (cs35l41->fw_request_ongoing) {
		dev_dbg(cs35l41->dev, "Existing request not complete\n");
		ret = -EBUSY;
		goto err;
	}

	/* Check if playback is ongoing when initial request is made */
	if (cs35l41->playback_started) {
		dev_err(cs35l41->dev, "Cannot Load/Unload firmware during Playback\n");
		ret = -EBUSY;
		goto err;
	}

	cs35l41->fw_request_ongoing = true;
	cs35l41->request_fw_load = ucontrol->value.integer.value[0];
	schedule_work(&cs35l41->fw_load_work);

err:
	mutex_unlock(&cs35l41->fw_mutex);

	return ret;
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
		cs35l41->firmware_type = ucontrol->value.enumerated.item[0];
		return 0;
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
	int ret;

	scnprintf(fw_type_ctl_name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN, "%s DSP1 Firmware Type",
		  cs35l41->amp_name);
	scnprintf(fw_load_ctl_name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN, "%s DSP1 Firmware Load",
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

	return 0;
}

static int cs35l41_hda_bind(struct device *dev, struct device *master, void *master_data)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	struct hda_component *comps = master_data;
	int ret = 0;

	if (!comps || cs35l41->index < 0 || cs35l41->index >= HDA_MAX_COMPONENTS)
		return -EINVAL;

	comps = &comps[cs35l41->index];
	if (comps->dev)
		return -EBUSY;

	pm_runtime_get_sync(dev);

	mutex_lock(&cs35l41->fw_mutex);

	comps->dev = dev;
	if (!cs35l41->acpi_subsystem_id)
		cs35l41->acpi_subsystem_id = kasprintf(GFP_KERNEL, "%.8x",
						       comps->codec->core.subsystem_id);
	cs35l41->codec = comps->codec;
	strscpy(comps->name, dev_name(dev), sizeof(comps->name));

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

	comps->playback_hook = cs35l41_hda_playback_hook;

	mutex_unlock(&cs35l41->fw_mutex);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

static void cs35l41_hda_unbind(struct device *dev, struct device *master, void *master_data)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	struct hda_component *comps = master_data;

	if (comps[cs35l41->index].dev == dev)
		memset(&comps[cs35l41->index], 0, sizeof(*comps));
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

static int cs35l41_hda_apply_properties(struct cs35l41_hda *cs35l41)
{
	struct cs35l41_hw_cfg *hw_cfg = &cs35l41->hw_cfg;
	bool using_irq = false;
	int irq, irq_pol;
	int ret;
	int i;

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

	if (cs35l41->irq && using_irq) {
		ret = devm_regmap_add_irq_chip(cs35l41->dev, cs35l41->regmap, cs35l41->irq,
					       IRQF_ONESHOT | IRQF_SHARED | irq_pol,
					       0, &cs35l41_regmap_irq_chip, &cs35l41->irq_data);
		if (ret)
			return ret;

		for (i = 0; i < ARRAY_SIZE(cs35l41_irqs); i++) {
			irq = regmap_irq_get_virq(cs35l41->irq_data, cs35l41_irqs[i].irq);
			if (irq < 0)
				return irq;

			ret = devm_request_threaded_irq(cs35l41->dev, irq, NULL,
							cs35l41_irqs[i].handler,
							IRQF_ONESHOT | IRQF_SHARED | irq_pol,
							cs35l41_irqs[i].name, cs35l41);
			if (ret)
				return ret;
		}
	}

	return cs35l41_hda_channel_map(cs35l41->dev, 0, NULL, 1, &hw_cfg->spk_pos);
}

static int cs35l41_get_speaker_id(struct device *dev, int amp_index,
				  int num_amps, int fixed_gpio_id)
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

/*
 * Device CLSA010(0/1) doesn't have _DSD so a gpiod_get by the label reset won't work.
 * And devices created by serial-multi-instantiate don't have their device struct
 * pointing to the correct fwnode, so acpi_dev must be used here.
 * And devm functions expect that the device requesting the resource has the correct
 * fwnode.
 */
static int cs35l41_no_acpi_dsd(struct cs35l41_hda *cs35l41, struct device *physdev, int id,
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

	if (strncmp(hid, "CLSA0100", 8) == 0) {
		hw_cfg->bst_type = CS35L41_EXT_BOOST_NO_VSPK_SWITCH;
	} else if (strncmp(hid, "CLSA0101", 8) == 0) {
		hw_cfg->bst_type = CS35L41_EXT_BOOST;
		hw_cfg->gpio1.func = CS35l41_VSPK_SWITCH;
		hw_cfg->gpio1.valid = true;
	} else {
		/*
		 * Note: CLSA010(0/1) are special cases which use a slightly different design.
		 * All other HIDs e.g. CSC3551 require valid ACPI _DSD properties to be supported.
		 */
		dev_err(cs35l41->dev, "Error: ACPI _DSD Properties are missing for HID %s.\n", hid);
		hw_cfg->valid = false;
		hw_cfg->gpio1.valid = false;
		hw_cfg->gpio2.valid = false;
		return -EINVAL;
	}

	return 0;
}

static int cs35l41_hda_read_acpi(struct cs35l41_hda *cs35l41, const char *hid, int id)
{
	struct cs35l41_hw_cfg *hw_cfg = &cs35l41->hw_cfg;
	u32 values[HDA_MAX_COMPONENTS];
	struct acpi_device *adev;
	struct device *physdev;
	const char *sub;
	char *property;
	size_t nval;
	int i, ret;

	adev = acpi_dev_get_first_match_dev(hid, NULL, -1);
	if (!adev) {
		dev_err(cs35l41->dev, "Failed to find an ACPI device for %s\n", hid);
		return -ENODEV;
	}

	physdev = get_device(acpi_get_first_physical_node(adev));
	acpi_dev_put(adev);

	sub = acpi_get_subsystem_id(ACPI_HANDLE(physdev));
	if (IS_ERR(sub))
		sub = NULL;
	cs35l41->acpi_subsystem_id = sub;

	property = "cirrus,dev-index";
	ret = device_property_count_u32(physdev, property);
	if (ret <= 0) {
		ret = cs35l41_no_acpi_dsd(cs35l41, physdev, id, hid);
		goto err_put_physdev;
	}
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
	cs35l41->reset_gpio = fwnode_gpiod_get_index(acpi_fwnode_handle(adev), "reset", cs35l41->index,
						     GPIOD_OUT_LOW, "cs35l41-reset");

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
	put_device(physdev);

	return 0;

err:
	dev_err(cs35l41->dev, "Failed property %s: %d\n", property, ret);
err_put_physdev:
	put_device(physdev);

	return ret;
}

int cs35l41_hda_probe(struct device *dev, const char *device_name, int id, int irq,
		      struct regmap *regmap)
{
	unsigned int int_sts, regid, reg_revid, mtl_revid, chipid, int_status;
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
		usleep_range(2000, 2100);
		gpiod_set_value_cansleep(cs35l41->reset_gpio, 1);
	}

	usleep_range(2000, 2100);

	ret = regmap_read_poll_timeout(cs35l41->regmap, CS35L41_IRQ1_STATUS4, int_status,
				       int_status & CS35L41_OTP_BOOT_DONE, 1000, 100000);
	if (ret) {
		dev_err(cs35l41->dev, "Failed waiting for OTP_BOOT_DONE: %d\n", ret);
		goto err;
	}

	ret = regmap_read(cs35l41->regmap, CS35L41_IRQ1_STATUS3, &int_sts);
	if (ret || (int_sts & CS35L41_OTP_BOOT_ERR)) {
		dev_err(cs35l41->dev, "OTP Boot status %x error: %d\n",
			int_sts & CS35L41_OTP_BOOT_ERR, ret);
		ret = -EIO;
		goto err;
	}

	ret = regmap_read(cs35l41->regmap, CS35L41_DEVID, &regid);
	if (ret) {
		dev_err(cs35l41->dev, "Get Device ID failed: %d\n", ret);
		goto err;
	}

	ret = regmap_read(cs35l41->regmap, CS35L41_REVID, &reg_revid);
	if (ret) {
		dev_err(cs35l41->dev, "Get Revision ID failed: %d\n", ret);
		goto err;
	}

	mtl_revid = reg_revid & CS35L41_MTLREVID_MASK;

	chipid = (mtl_revid % 2) ? CS35L41R_CHIP_ID : CS35L41_CHIP_ID;
	if (regid != chipid) {
		dev_err(cs35l41->dev, "CS35L41 Device ID (%X). Expected ID %X\n", regid, chipid);
		ret = -ENODEV;
		goto err;
	}

	ret = cs35l41_test_key_unlock(cs35l41->dev, cs35l41->regmap);
	if (ret)
		goto err;

	ret = cs35l41_register_errata_patch(cs35l41->dev, cs35l41->regmap, reg_revid);
	if (ret)
		goto err;

	ret = cs35l41_otp_unpack(cs35l41->dev, cs35l41->regmap);
	if (ret) {
		dev_err(cs35l41->dev, "OTP Unpack failed: %d\n", ret);
		goto err;
	}

	ret = cs35l41_test_key_lock(cs35l41->dev, cs35l41->regmap);
	if (ret)
		goto err;

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
		dev_err(cs35l41->dev, "Register component failed: %d\n", ret);
		pm_runtime_disable(cs35l41->dev);
		goto err;
	}

	dev_info(cs35l41->dev, "Cirrus Logic CS35L41 (%x), Revision: %02X\n", regid, reg_revid);

	return 0;

err_pm:
	pm_runtime_disable(cs35l41->dev);
	pm_runtime_put_noidle(cs35l41->dev);

err:
	if (cs35l41_safe_reset(cs35l41->regmap, cs35l41->hw_cfg.bst_type))
		gpiod_set_value_cansleep(cs35l41->reset_gpio, 0);
	gpiod_put(cs35l41->reset_gpio);
	kfree(cs35l41->acpi_subsystem_id);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs35l41_hda_probe, SND_HDA_SCODEC_CS35L41);

void cs35l41_hda_remove(struct device *dev)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);

	pm_runtime_get_sync(cs35l41->dev);
	pm_runtime_disable(cs35l41->dev);

	if (cs35l41->halo_initialized)
		cs35l41_remove_dsp(cs35l41);

	component_del(cs35l41->dev, &cs35l41_hda_comp_ops);

	pm_runtime_put_noidle(cs35l41->dev);

	if (cs35l41_safe_reset(cs35l41->regmap, cs35l41->hw_cfg.bst_type))
		gpiod_set_value_cansleep(cs35l41->reset_gpio, 0);
	gpiod_put(cs35l41->reset_gpio);
	kfree(cs35l41->acpi_subsystem_id);
}
EXPORT_SYMBOL_NS_GPL(cs35l41_hda_remove, SND_HDA_SCODEC_CS35L41);

const struct dev_pm_ops cs35l41_hda_pm_ops = {
	RUNTIME_PM_OPS(cs35l41_runtime_suspend, cs35l41_runtime_resume,
		       cs35l41_runtime_idle)
	SYSTEM_SLEEP_PM_OPS(cs35l41_system_suspend, cs35l41_system_resume)
};
EXPORT_SYMBOL_NS_GPL(cs35l41_hda_pm_ops, SND_HDA_SCODEC_CS35L41);

MODULE_DESCRIPTION("CS35L41 HDA Driver");
MODULE_IMPORT_NS(SND_HDA_CS_DSP_CONTROLS);
MODULE_AUTHOR("Lucas Tanure, Cirrus Logic Inc, <tanureal@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(FW_CS_DSP);
