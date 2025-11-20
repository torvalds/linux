/* SPDX-License-Identifier: GPL-2.0 */
//
// ALSA SoC Texas Instruments TAS2563/TAS2781 Audio Smart Amplifier
//
// Copyright (C) 2022 - 2025 Texas Instruments Incorporated
// https://www.ti.com
//
// The TAS2563/TAS2781 driver implements a flexible and configurable
// algo coefficient setting for one, two, or even multiple
// TAS2563/TAS2781 chips.
//
// Author: Shenghao Ding <shenghao-ding@ti.com>
// Author: Kevin Lu <kevin-lu@ti.com>
// Author: Baojun Xu <baojun.xu@ti.com>
//

#ifndef __TAS2781_H__
#define __TAS2781_H__

#ifdef CONFIG_SND_SOC_TAS2781_ACOUST_I2C
#include <linux/debugfs.h>
#endif

#include "tas2781-dsp.h"

/* version number */
#define TAS2781_DRV_VER			1
#define SMARTAMP_MODULE_NAME		"tas2781"
#define TAS2781_GLOBAL_ADDR	0x40
#define TAS2563_GLOBAL_ADDR	0x48
#define TASDEVICE_RATES			(SNDRV_PCM_RATE_44100 |\
	SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |\
	SNDRV_PCM_RATE_88200)

#define TASDEVICE_FORMATS		(SNDRV_PCM_FMTBIT_S16_LE | \
	SNDRV_PCM_FMTBIT_S24_LE | \
	SNDRV_PCM_FMTBIT_S32_LE)

#define TASDEVICE_CRC8_POLYNOMIAL		0x4d

/* PAGE Control Register (available in page0 of each book) */
#define TASDEVICE_PAGE_SELECT		0x00
#define TASDEVICE_BOOKCTL_PAGE		0x00
#define TASDEVICE_BOOKCTL_REG		127
#define TASDEVICE_BOOK_ID(reg)		(reg / (256 * 128))
#define TASDEVICE_PAGE_ID(reg)		((reg % (256 * 128)) / 128)
#define TASDEVICE_PAGE_REG(reg)		((reg % (256 * 128)) % 128)
#define TASDEVICE_PGRG(reg)		(reg % (256 * 128))
#define TASDEVICE_REG(book, page, reg)	(((book * 256 * 128) + \
					(page * 128)) + reg)

/* Software Reset, compatble with new device (TAS5825). */
#define TASDEVICE_REG_SWRESET		TASDEVICE_REG(0x0, 0x0, 0x01)
#define TASDEVICE_REG_SWRESET_RESET	BIT(0)

#define TAS5825_REG_SWRESET_RESET	(BIT(0) | BIT(4))

/* Checksum */
#define TASDEVICE_CHECKSUM_REG		TASDEVICE_REG(0x0, 0x0, 0x7e)

/* XM_340 */
#define	TASDEVICE_XM_A1_REG	TASDEVICE_REG(0x64, 0x63, 0x3c)
/* XM_341 */
#define	TASDEVICE_XM_A2_REG	TASDEVICE_REG(0x64, 0x63, 0x38)

/* Volume control */
#define TAS2563_DVC_LVL			TASDEVICE_REG(0x00, 0x02, 0x0c)
#define TAS2781_DVC_LVL			TASDEVICE_REG(0x0, 0x0, 0x1a)
#define TAS2781_AMP_LEVEL		TASDEVICE_REG(0x0, 0x0, 0x03)
#define TAS2781_AMP_LEVEL_MASK		GENMASK(5, 1)

#define TAS2563_IDLE		TASDEVICE_REG(0x00, 0x00, 0x3e)
#define TAS2563_PRM_R0_REG		TASDEVICE_REG(0x00, 0x0f, 0x34)

#define TAS2563_RUNTIME_RE_REG_TF	TASDEVICE_REG(0x64, 0x02, 0x70)
#define TAS2563_RUNTIME_RE_REG		TASDEVICE_REG(0x64, 0x02, 0x48)

#define TAS2563_PRM_ENFF_REG		TASDEVICE_REG(0x00, 0x0d, 0x54)
#define TAS2563_PRM_DISTCK_REG		TASDEVICE_REG(0x00, 0x0d, 0x58)
#define TAS2563_PRM_TE_SCTHR_REG	TASDEVICE_REG(0x00, 0x0f, 0x60)
#define TAS2563_PRM_PLT_FLAG_REG	TASDEVICE_REG(0x00, 0x0d, 0x74)
#define TAS2563_PRM_SINEGAIN_REG	TASDEVICE_REG(0x00, 0x0d, 0x7c)
/* prm_Int_B0 */
#define TAS2563_TE_TA1_REG		TASDEVICE_REG(0x00, 0x10, 0x0c)
/* prm_Int_A1 */
#define TAS2563_TE_TA1_AT_REG		TASDEVICE_REG(0x00, 0x10, 0x10)
/* prm_TE_Beta */
#define TAS2563_TE_TA2_REG		TASDEVICE_REG(0x00, 0x0f, 0x64)
/* prm_TE_Beta1 */
#define TAS2563_TE_AT_REG		TASDEVICE_REG(0x00, 0x0f, 0x68)
/* prm_TE_1_Beta1 */
#define TAS2563_TE_DT_REG		TASDEVICE_REG(0x00, 0x0f, 0x70)

#define TAS2781_PRM_INT_MASK_REG	TASDEVICE_REG(0x00, 0x00, 0x3b)
#define TAS2781_PRM_CLK_CFG_REG		TASDEVICE_REG(0x00, 0x00, 0x5c)
#define TAS2781_PRM_RSVD_REG		TASDEVICE_REG(0x00, 0x01, 0x19)
#define TAS2781_PRM_TEST_57_REG		TASDEVICE_REG(0x00, 0xfd, 0x39)
#define TAS2781_PRM_TEST_62_REG		TASDEVICE_REG(0x00, 0xfd, 0x3e)
#define TAS2781_PRM_PVDD_UVLO_REG	TASDEVICE_REG(0x00, 0x00, 0x71)
#define TAS2781_PRM_CHNL_0_REG		TASDEVICE_REG(0x00, 0x00, 0x03)
#define TAS2781_PRM_NG_CFG0_REG		TASDEVICE_REG(0x00, 0x00, 0x35)
#define TAS2781_PRM_IDLE_CH_DET_REG	TASDEVICE_REG(0x00, 0x00, 0x66)
#define TAS2781_PRM_PLT_FLAG_REG	TASDEVICE_REG(0x00, 0x14, 0x38)
#define TAS2781_PRM_SINEGAIN_REG	TASDEVICE_REG(0x00, 0x14, 0x40)
#define TAS2781_PRM_SINEGAIN2_REG	TASDEVICE_REG(0x00, 0x14, 0x44)

#define TAS2781_TEST_UNLOCK_REG		TASDEVICE_REG(0x00, 0xfd, 0x0d)
#define TAS2781_TEST_PAGE_UNLOCK	0x0d

#define TAS2781_RUNTIME_LATCH_RE_REG	TASDEVICE_REG(0x00, 0x00, 0x49)
#define TAS2781_RUNTIME_RE_REG_TF	TASDEVICE_REG(0x64, 0x62, 0x48)
#define TAS2781_RUNTIME_RE_REG		TASDEVICE_REG(0x64, 0x63, 0x44)

enum audio_device {
	TAS2020,
	TAS2118,
	TAS2120,
	TAS2320,
	TAS2563,
	TAS2570,
	TAS2572,
	TAS2781,
	TAS5802,
	TAS5815,
	TAS5825,
	TAS5827,
	TAS5828,
	TAS_OTHERS,
};

enum dspbin_type {
	TASDEV_BASIC,
	TASDEV_ALPHA,
	TASDEV_BETA,
};

struct bulk_reg_val {
	int reg;
	unsigned char val[4];
	unsigned char val_len;
	bool is_locked;
};

struct tasdevice {
	struct bulk_reg_val *cali_data_backup;
	struct bulk_reg_val alp_cali_bckp;
	struct tasdevice_fw *cali_data_fmw;
	unsigned int dev_addr;
	unsigned int err_code;
	unsigned char cur_book;
	short cur_prog;
	short cur_conf;
	bool is_loading;
	bool is_loaderr;
};

struct cali_reg {
	unsigned int r0_reg;
	unsigned int r0_low_reg;
	unsigned int invr0_reg;
	unsigned int pow_reg;
	unsigned int tlimit_reg;
};

struct calidata {
	unsigned char *data;
	unsigned long total_sz;
	struct cali_reg cali_reg_array;
	unsigned int cali_dat_sz_per_dev;
};

/*
 * To enable CONFIG_SND_SOC_TAS2781_ACOUST_I2C will create a bridge to the
 * acoustic tuning tool which can tune the chips' acoustic effect. Due to the
 * whole directly exposing the registers, there exist some potential risks. So
 * this define is invisible in Kconfig, anyone who wants to use acoustic tool
 * have to edit the source manually.
 */
#ifdef CONFIG_SND_SOC_TAS2781_ACOUST_I2C
#define TASDEV_DATA_PAYLOAD_SIZE	128
struct acoustic_data {
	unsigned char len;
	unsigned char id;
	unsigned char addr;
	unsigned char book;
	unsigned char page;
	unsigned char reg;
	unsigned char data[TASDEV_DATA_PAYLOAD_SIZE];
};
#endif

struct tasdevice_priv {
	struct tasdevice tasdevice[TASDEVICE_MAX_CHANNELS];
	struct tasdevice_rca rcabin;
	struct calidata cali_data;
#ifdef CONFIG_SND_SOC_TAS2781_ACOUST_I2C
	struct acoustic_data acou_data;
#endif
	struct tasdevice_fw *fmw;
	struct gpio_desc *speaker_id;
	struct gpio_desc *reset;
	struct mutex codec_lock;
	struct regmap *regmap;
	struct device *dev;

	unsigned char cal_binaryname[TASDEVICE_MAX_CHANNELS][64];
	unsigned char crc8_lkp_tbl[CRC8_TABLE_SIZE];
	unsigned char coef_binaryname[64];
	unsigned char rca_binaryname[64];
	unsigned char dev_name[32];
	const unsigned char (*dvc_tlv_table)[4];
	const char *name_prefix;
	unsigned char ndev;
	unsigned int dspbin_typ;
	unsigned int magic_num;
	unsigned int chip_id;
	unsigned int sysclk;

	int irq;
	int cur_prog;
	int cur_conf;
	int fw_state;
	int index;
	void *client;
	void *codec;
	bool force_fwload_status;
	bool playback_started;
	bool isacpi;
	bool isspi;
	bool is_user_space_calidata;
	unsigned int global_addr;

	int (*fw_parse_variable_header)(struct tasdevice_priv *tas_priv,
		const struct firmware *fmw, int offset);
	int (*fw_parse_program_data)(struct tasdevice_priv *tas_priv,
		struct tasdevice_fw *tas_fmw,
		const struct firmware *fmw, int offset);
	int (*fw_parse_configuration_data)(struct tasdevice_priv *tas_priv,
		struct tasdevice_fw *tas_fmw,
		const struct firmware *fmw, int offset);
	int (*fw_parse_fct_param_address)(struct tasdevice_priv *tas_priv,
		struct tasdevice_fw *tas_fmw,
		const struct firmware *fmw, int offset);
	int (*tasdevice_load_block)(struct tasdevice_priv *tas_priv,
		struct tasdev_blk *block);

	int (*change_chn_book)(struct tasdevice_priv *tas_priv,
		unsigned short chn, int book);
	int (*update_bits)(struct tasdevice_priv *tas_priv,
		unsigned short chn, unsigned int reg, unsigned int mask,
		unsigned int value);
	int (*dev_read)(struct tasdevice_priv *tas_priv,
		unsigned short chn, unsigned int reg, unsigned int *value);
	int (*dev_bulk_read)(struct tasdevice_priv *tas_priv,
		unsigned short chn, unsigned int reg, unsigned char *p_data,
		unsigned int n_length);
};

int tasdevice_dev_read(struct tasdevice_priv *tas_priv,
	unsigned short chn, unsigned int reg, unsigned int *value);
int tasdevice_dev_bulk_read(struct tasdevice_priv *tas_priv,
	unsigned short chn, unsigned int reg, unsigned char *p_data,
	unsigned int n_length);
int tasdevice_dev_write(struct tasdevice_priv *tas_priv,
	unsigned short chn, unsigned int reg, unsigned int value);
int tasdevice_dev_bulk_write(
	struct tasdevice_priv *tas_priv, unsigned short chn,
	unsigned int reg, unsigned char *p_data, unsigned int n_length);
void tasdevice_remove(struct tasdevice_priv *tas_priv);
#endif /* __TAS2781_H__ */
