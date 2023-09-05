/* SPDX-License-Identifier: GPL-2.0 */
//
// ALSA SoC Texas Instruments TAS2781 Audio Smart Amplifier
//
// Copyright (C) 2022 - 2023 Texas Instruments Incorporated
// https://www.ti.com
//
// The TAS2781 driver implements a flexible and configurable
// algo coefficient setting for one, two, or even multiple
// TAS2781 chips.
//
// Author: Shenghao Ding <shenghao-ding@ti.com>
// Author: Kevin Lu <kevin-lu@ti.com>
//

#ifndef __TAS2781_H__
#define __TAS2781_H__

#include "tas2781-dsp.h"

/* version number */
#define TAS2781_DRV_VER			1
#define SMARTAMP_MODULE_NAME		"tas2781"
#define TAS2781_GLOBAL_ADDR	0x40
#define TASDEVICE_RATES			(SNDRV_PCM_RATE_44100 |\
	SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |\
	SNDRV_PCM_RATE_88200)

#define TASDEVICE_FORMATS		(SNDRV_PCM_FMTBIT_S16_LE | \
	SNDRV_PCM_FMTBIT_S24_LE | \
	SNDRV_PCM_FMTBIT_S32_LE)

/*PAGE Control Register (available in page0 of each book) */
#define TASDEVICE_PAGE_SELECT		0x00
#define TASDEVICE_BOOKCTL_PAGE		0x00
#define TASDEVICE_BOOKCTL_REG		127
#define TASDEVICE_BOOK_ID(reg)		(reg / (256 * 128))
#define TASDEVICE_PAGE_ID(reg)		((reg % (256 * 128)) / 128)
#define TASDEVICE_PAGE_REG(reg)		((reg % (256 * 128)) % 128)
#define TASDEVICE_PGRG(reg)		(reg % (256 * 128))
#define TASDEVICE_REG(book, page, reg)	(((book * 256 * 128) + \
					(page * 128)) + reg)

/*Software Reset */
#define TAS2781_REG_SWRESET		TASDEVICE_REG(0x0, 0X0, 0x01)
#define TAS2781_REG_SWRESET_RESET	BIT(0)

/*I2C Checksum */
#define TASDEVICE_I2CChecksum		TASDEVICE_REG(0x0, 0x0, 0x7E)

/* Volume control */
#define TAS2781_DVC_LVL			TASDEVICE_REG(0x0, 0x0, 0x1A)
#define TAS2781_AMP_LEVEL		TASDEVICE_REG(0x0, 0x0, 0x03)
#define TAS2781_AMP_LEVEL_MASK		GENMASK(5, 1)

#define TASDEVICE_CMD_SING_W		0x1
#define TASDEVICE_CMD_BURST		0x2
#define TASDEVICE_CMD_DELAY		0x3
#define TASDEVICE_CMD_FIELD_W		0x4

enum audio_device {
	TAS2781	= 0,
};

enum device_catlog_id {
	LENOVO = 0,
	OTHERS
};

struct tasdevice {
	struct tasdevice_fw *cali_data_fmw;
	unsigned int dev_addr;
	unsigned int err_code;
	unsigned char cur_book;
	short cur_prog;
	short cur_conf;
	bool is_loading;
	bool is_loaderr;
};

struct tasdevice_irqinfo {
	int irq_gpio;
	int irq;
};

struct calidata {
	unsigned char *data;
	unsigned long total_sz;
};

struct tasdevice_priv {
	struct tasdevice tasdevice[TASDEVICE_MAX_CHANNELS];
	struct tasdevice_irqinfo irq_info;
	struct tasdevice_rca rcabin;
	struct calidata cali_data;
	struct tasdevice_fw *fmw;
	struct gpio_desc *reset;
	struct mutex codec_lock;
	struct regmap *regmap;
	struct device *dev;
	struct tm tm;

	enum device_catlog_id catlog_id;
	const char *acpi_subsystem_id;
	unsigned char cal_binaryname[TASDEVICE_MAX_CHANNELS][64];
	unsigned char crc8_lkp_tbl[CRC8_TABLE_SIZE];
	unsigned char coef_binaryname[64];
	unsigned char rca_binaryname[64];
	unsigned char dev_name[32];
	unsigned char ndev;
	unsigned int magic_num;
	unsigned int chip_id;
	unsigned int sysclk;

	int cur_prog;
	int cur_conf;
	int fw_state;
	int index;
	void *client;
	void *codec;
	bool force_fwload_status;
	bool playback_started;
	bool isacpi;
	int (*fw_parse_variable_header)(struct tasdevice_priv *tas_priv,
		const struct firmware *fmw, int offset);
	int (*fw_parse_program_data)(struct tasdevice_priv *tas_priv,
		struct tasdevice_fw *tas_fmw,
		const struct firmware *fmw, int offset);
	int (*fw_parse_configuration_data)(struct tasdevice_priv *tas_priv,
		struct tasdevice_fw *tas_fmw,
		const struct firmware *fmw, int offset);
	int (*tasdevice_load_block)(struct tasdevice_priv *tas_priv,
		struct tasdev_blk *block);
};

void tas2781_reset(struct tasdevice_priv *tas_dev);
int tascodec_init(struct tasdevice_priv *tas_priv, void *codec,
	void (*cont)(const struct firmware *fw, void *context));
struct tasdevice_priv *tasdevice_kzalloc(struct i2c_client *i2c);
int tasdevice_init(struct tasdevice_priv *tas_priv);
void tasdevice_remove(struct tasdevice_priv *tas_priv);
int tasdevice_dev_read(struct tasdevice_priv *tas_priv,
	unsigned short chn, unsigned int reg, unsigned int *value);
int tasdevice_dev_write(struct tasdevice_priv *tas_priv,
	unsigned short chn, unsigned int reg, unsigned int value);
int tasdevice_dev_bulk_write(
	struct tasdevice_priv *tas_priv, unsigned short chn,
	unsigned int reg, unsigned char *p_data, unsigned int n_length);
int tasdevice_dev_bulk_read(struct tasdevice_priv *tas_priv,
	unsigned short chn, unsigned int reg, unsigned char *p_data,
	unsigned int n_length);
int tasdevice_dev_update_bits(
	struct tasdevice_priv *tasdevice, unsigned short chn,
	unsigned int reg, unsigned int mask, unsigned int value);
int tasdevice_amp_putvol(struct tasdevice_priv *tas_priv,
	struct snd_ctl_elem_value *ucontrol, struct soc_mixer_control *mc);
int tasdevice_amp_getvol(struct tasdevice_priv *tas_priv,
	struct snd_ctl_elem_value *ucontrol, struct soc_mixer_control *mc);
int tasdevice_digital_putvol(struct tasdevice_priv *tas_priv,
	struct snd_ctl_elem_value *ucontrol, struct soc_mixer_control *mc);
int tasdevice_digital_getvol(struct tasdevice_priv *tas_priv,
	struct snd_ctl_elem_value *ucontrol, struct soc_mixer_control *mc);

#endif /* __TAS2781_H__ */
