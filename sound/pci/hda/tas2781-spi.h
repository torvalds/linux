/* SPDX-License-Identifier: GPL-2.0 */
//
// ALSA SoC Texas Instruments TAS2781 Audio Smart Amplifier
//
// Copyright (C) 2024 Texas Instruments Incorporated
// https://www.ti.com
//
// The TAS2781 driver implements a flexible and configurable
// algo coefficient setting for TAS2781 chips.
//
// Author: Baojun Xu <baojun.xu@ti.com>
//

#ifndef __TAS2781_SPI_H__
#define __TAS2781_SPI_H__

#define TASDEVICE_RATES			\
	(SNDRV_PCM_RATE_44100 |	SNDRV_PCM_RATE_48000 | \
	SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_88200)

#define TASDEVICE_FORMATS		\
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | \
	SNDRV_PCM_FMTBIT_S32_LE)

#define TASDEVICE_MAX_BOOK_NUM		256
#define TASDEVICE_MAX_PAGE		256

#define TASDEVICE_MAX_SIZE	(TASDEVICE_MAX_BOOK_NUM * TASDEVICE_MAX_PAGE)

/* PAGE Control Register (available in page0 of each book) */
#define TASDEVICE_PAGE_SELECT		0x00
#define TASDEVICE_BOOKCTL_PAGE		0x00
#define TASDEVICE_BOOKCTL_REG		GENMASK(7, 1)
#define TASDEVICE_BOOK_ID(reg)		(((reg) & GENMASK(24, 16)) >> 16)
#define TASDEVICE_PAGE_ID(reg)		(((reg) & GENMASK(15, 8)) >> 8)
#define TASDEVICE_REG_ID(reg)		(((reg) & GENMASK(7, 1)) >> 1)
#define TASDEVICE_PAGE_REG(reg)		((reg) & GENMASK(15, 1))
#define TASDEVICE_REG(book, page, reg)	\
	(((book) << 16) | ((page) << 8) | ((reg) << 1))

/* Software Reset */
#define TAS2781_REG_SWRESET		TASDEVICE_REG(0x0, 0x0, 0x01)
#define TAS2781_REG_SWRESET_RESET	BIT(0)

/* System Reset Check Register */
#define TAS2781_REG_CLK_CONFIG		TASDEVICE_REG(0x0, 0x0, 0x5c)
#define TAS2781_REG_CLK_CONFIG_RESET	(0x19)
#define TAS2781_PRE_POST_RESET_CFG	3

/* Block Checksum */
#define TASDEVICE_CHECKSUM		TASDEVICE_REG(0x0, 0x0, 0x7e)

/* Volume control */
#define TAS2781_DVC_LVL			TASDEVICE_REG(0x0, 0x0, 0x1a)
#define TAS2781_AMP_LEVEL		TASDEVICE_REG(0x0, 0x0, 0x03)
#define TAS2781_AMP_LEVEL_MASK		GENMASK(5, 1)

#define TASDEVICE_CMD_SING_W		0x1
#define TASDEVICE_CMD_BURST		0x2
#define TASDEVICE_CMD_DELAY		0x3
#define TASDEVICE_CMD_FIELD_W		0x4

#define TAS2781_SPI_MAX_FREQ		(4 * HZ_PER_MHZ)

#define TASDEVICE_CRC8_POLYNOMIAL		0x4d
#define TASDEVICE_SPEAKER_CALIBRATION_SIZE	20

/* Flag of calibration registers address. */
#define TASDEVICE_CALIBRATION_REG_ADDRESS	BIT(7)

#define TASDEVICE_CALIBRATION_DATA_NAME	L"CALI_DATA"
#define TASDEVICE_CALIBRATION_DATA_SIZE	256

enum calib_data {
	R0_VAL = 0,
	INV_R0,
	R0LOW,
	POWER,
	TLIM,
	CALIB_MAX
};

struct tasdevice_priv {
	struct tasdevice_fw *cali_data_fmw;
	struct tasdevice_rca rcabin;
	struct tasdevice_fw *fmw;
	struct gpio_desc *reset;
	struct mutex codec_lock;
	struct regmap *regmap;
	struct device *dev;

	unsigned char crc8_lkp_tbl[CRC8_TABLE_SIZE];
	unsigned char coef_binaryname[64];
	unsigned char rca_binaryname[64];
	unsigned char dev_name[32];

	bool force_fwload_status;
	bool playback_started;
	bool is_loading;
	bool is_loaderr;
	unsigned int cali_reg_array[CALIB_MAX];
	unsigned int cali_data[CALIB_MAX];
	unsigned int err_code;
	void *codec;
	int cur_book;
	int cur_prog;
	int cur_conf;
	int fw_state;
	int index;
	int irq;

	int (*fw_parse_variable_header)(struct tasdevice_priv *tas_priv,
					const struct firmware *fmw,
					int offset);
	int (*fw_parse_program_data)(struct tasdevice_priv *tas_priv,
				     struct tasdevice_fw *tas_fmw,
				     const struct firmware *fmw, int offset);
	int (*fw_parse_configuration_data)(struct tasdevice_priv *tas_priv,
					   struct tasdevice_fw *tas_fmw,
					   const struct firmware *fmw,
					   int offset);
	int (*tasdevice_load_block)(struct tasdevice_priv *tas_priv,
				    struct tasdev_blk *block);

	int (*save_calibration)(struct tasdevice_priv *tas_priv);
	void (*apply_calibration)(struct tasdevice_priv *tas_priv);
};

int tasdevice_spi_dev_read(struct tasdevice_priv *tas_priv,
			   unsigned int reg, unsigned int *value);
int tasdevice_spi_dev_write(struct tasdevice_priv *tas_priv,
			    unsigned int reg, unsigned int value);
int tasdevice_spi_dev_bulk_write(struct tasdevice_priv *tas_priv,
				 unsigned int reg, unsigned char *p_data,
				 unsigned int n_length);
int tasdevice_spi_dev_bulk_read(struct tasdevice_priv *tas_priv,
				unsigned int reg, unsigned char *p_data,
				unsigned int n_length);
int tasdevice_spi_dev_update_bits(struct tasdevice_priv *tasdevice,
				  unsigned int reg, unsigned int mask,
				  unsigned int value);

void tasdevice_spi_select_cfg_blk(void *context, int conf_no,
				  unsigned char block_type);
void tasdevice_spi_config_info_remove(void *context);
int tasdevice_spi_dsp_parser(void *context);
int tasdevice_spi_rca_parser(void *context, const struct firmware *fmw);
void tasdevice_spi_dsp_remove(void *context);
void tasdevice_spi_calbin_remove(void *context);
int tasdevice_spi_select_tuningprm_cfg(void *context, int prm, int cfg_no,
				       int rca_conf_no);
int tasdevice_spi_prmg_load(void *context, int prm_no);
int tasdevice_spi_prmg_calibdata_load(void *context, int prm_no);
void tasdevice_spi_tuning_switch(void *context, int state);
int tas2781_spi_load_calibration(void *context, char *file_name,
				 unsigned short i);
#endif /* __TAS2781_SPI_H__ */
