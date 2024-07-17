/* SPDX-License-Identifier: GPL-2.0 */
//
// ALSA SoC Texas Instruments PCM6240 Family Audio ADC/DAC/Router
//
// Copyright (C) 2022 - 2024 Texas Instruments Incorporated
// https://www.ti.com
//
// The PCM6240 driver implements a flexible and configurable
// algo coefficient setting for one, two, or even multiple
// PCM6240 Family Audio chips.
//
// Author: Shenghao Ding <shenghao-ding@ti.com>
//

#ifndef __PCM6240_H__
#define __PCM6240_H__

enum pcm_device {
	ADC3120,
	ADC5120,
	ADC6120,
	DIX4192,
	PCM1690,
	PCM3120,
	PCM3140,
	PCM5120,
	PCM5140,
	PCM6120,
	PCM6140,
	PCM6240,
	PCM6260,
	PCM9211,
	PCMD3140,
	PCMD3180,
	PCMD512X,
	TAA5212,
	TAA5412,
	TAD5212,
	TAD5412,
	MAX_DEVICE,
};

#define PCMDEV_GENERIC_VOL_CTRL			0x0
#define PCMDEV_PCM1690_VOL_CTRL			0x1
#define PCMDEV_PCM1690_FINE_VOL_CTRL		0x2

/* Maximum number of I2C addresses */
#define PCMDEVICE_MAX_I2C_DEVICES		4
/* Maximum number defined in REGBIN protocol */
#define PCMDEVICE_MAX_REGBIN_DEVICES		8
#define PCMDEVICE_CONFIG_SUM			64
#define PCMDEVICE_BIN_FILENAME_LEN		64

#define PCMDEVICE_RATES	(SNDRV_PCM_RATE_44100 | \
	SNDRV_PCM_RATE_48000)
#define PCMDEVICE_MAX_CHANNELS			8
#define PCMDEVICE_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
	SNDRV_PCM_FMTBIT_S20_3LE | \
	SNDRV_PCM_FMTBIT_S24_3LE | \
	SNDRV_PCM_FMTBIT_S24_LE | \
	SNDRV_PCM_FMTBIT_S32_LE)

/* PAGE Control Register (available in page0 of each book) */
#define PCMDEVICE_PAGE_SELECT			0x00
#define PCMDEVICE_REG(page, reg)		((page * 128) + reg)
#define PCMDEVICE_REG_SWRESET			PCMDEVICE_REG(0X0, 0x01)
#define PCMDEVICE_REG_SWRESET_RESET		BIT(0)

#define ADC5120_REG_CH1_ANALOG_GAIN		PCMDEVICE_REG(0X0, 0x3d)
#define ADC5120_REG_CH1_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x3e)
#define ADC5120_REG_CH2_ANALOG_GAIN		PCMDEVICE_REG(0X0, 0x42)
#define ADC5120_REG_CH2_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x43)

#define PCM1690_REG_MODE_CTRL			PCMDEVICE_REG(0X0, 0x46)
#define PCM1690_REG_MODE_CTRL_DAMS_MSK		BIT(7)
#define PCM1690_REG_MODE_CTRL_DAMS_FINE_STEP	0x0
#define PCM1690_REG_MODE_CTRL_DAMS_WIDE_RANGE	0x80

#define PCM1690_REG_CH1_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x48)
#define PCM1690_REG_CH2_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x49)
#define PCM1690_REG_CH3_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x4a)
#define PCM1690_REG_CH4_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x4b)
#define PCM1690_REG_CH5_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x4c)
#define PCM1690_REG_CH6_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x4d)
#define PCM1690_REG_CH7_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x4e)
#define PCM1690_REG_CH8_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x4f)

#define PCM6240_REG_CH1_ANALOG_GAIN		PCMDEVICE_REG(0X0, 0x3d)
#define PCM6240_REG_CH1_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x3e)
#define PCM6240_REG_CH2_ANALOG_GAIN		PCMDEVICE_REG(0X0, 0x42)
#define PCM6240_REG_CH2_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x43)
#define PCM6240_REG_CH3_ANALOG_GAIN		PCMDEVICE_REG(0X0, 0x47)
#define PCM6240_REG_CH3_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x48)
#define PCM6240_REG_CH4_ANALOG_GAIN		PCMDEVICE_REG(0X0, 0x4c)
#define PCM6240_REG_CH4_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x4d)

#define PCM6260_REG_CH1_ANALOG_GAIN		PCMDEVICE_REG(0X0, 0x3d)
#define PCM6260_REG_CH1_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x3e)
#define PCM6260_REG_CH2_ANALOG_GAIN		PCMDEVICE_REG(0X0, 0x42)
#define PCM6260_REG_CH2_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x43)
#define PCM6260_REG_CH3_ANALOG_GAIN		PCMDEVICE_REG(0X0, 0x47)
#define PCM6260_REG_CH3_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x48)
#define PCM6260_REG_CH4_ANALOG_GAIN		PCMDEVICE_REG(0X0, 0x4c)
#define PCM6260_REG_CH4_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x4d)
#define PCM6260_REG_CH5_ANALOG_GAIN		PCMDEVICE_REG(0X0, 0x51)
#define PCM6260_REG_CH5_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x52)
#define PCM6260_REG_CH6_ANALOG_GAIN		PCMDEVICE_REG(0X0, 0x56)
#define PCM6260_REG_CH6_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x57)

#define PCM9211_REG_SW_CTRL			PCMDEVICE_REG(0X0, 0x40)
#define PCM9211_REG_SW_CTRL_MRST_MSK		BIT(7)
#define PCM9211_REG_SW_CTRL_MRST		0x0

#define PCM9211_REG_CH1_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x46)
#define PCM9211_REG_CH2_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x47)

#define PCMD3140_REG_CH1_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x3E)
#define PCMD3140_REG_CH2_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x43)
#define PCMD3140_REG_CH3_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x48)
#define PCMD3140_REG_CH4_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x4D)

#define PCMD3140_REG_CH1_FINE_GAIN		PCMDEVICE_REG(0X0, 0x3F)
#define PCMD3140_REG_CH2_FINE_GAIN		PCMDEVICE_REG(0X0, 0x44)
#define PCMD3140_REG_CH3_FINE_GAIN		PCMDEVICE_REG(0X0, 0x49)
#define PCMD3140_REG_CH4_FINE_GAIN		PCMDEVICE_REG(0X0, 0x4E)

#define PCMD3180_REG_CH1_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x3E)
#define PCMD3180_REG_CH2_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x43)
#define PCMD3180_REG_CH3_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x48)
#define PCMD3180_REG_CH4_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x4D)
#define PCMD3180_REG_CH5_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x52)
#define PCMD3180_REG_CH6_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x57)
#define PCMD3180_REG_CH7_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x5C)
#define PCMD3180_REG_CH8_DIGITAL_GAIN		PCMDEVICE_REG(0X0, 0x61)

#define PCMD3180_REG_CH1_FINE_GAIN		PCMDEVICE_REG(0X0, 0x3F)
#define PCMD3180_REG_CH2_FINE_GAIN		PCMDEVICE_REG(0X0, 0x44)
#define PCMD3180_REG_CH3_FINE_GAIN		PCMDEVICE_REG(0X0, 0x49)
#define PCMD3180_REG_CH4_FINE_GAIN		PCMDEVICE_REG(0X0, 0x4E)
#define PCMD3180_REG_CH5_FINE_GAIN		PCMDEVICE_REG(0X0, 0x53)
#define PCMD3180_REG_CH6_FINE_GAIN		PCMDEVICE_REG(0X0, 0x58)
#define PCMD3180_REG_CH7_FINE_GAIN		PCMDEVICE_REG(0X0, 0x5D)
#define PCMD3180_REG_CH8_FINE_GAIN		PCMDEVICE_REG(0X0, 0x62)

#define TAA5412_REG_CH1_DIGITAL_VOLUME		PCMDEVICE_REG(0X0, 0x52)
#define TAA5412_REG_CH2_DIGITAL_VOLUME		PCMDEVICE_REG(0X0, 0x57)
#define TAA5412_REG_CH3_DIGITAL_VOLUME		PCMDEVICE_REG(0X0, 0x5B)
#define TAA5412_REG_CH4_DIGITAL_VOLUME		PCMDEVICE_REG(0X0, 0x5F)

#define TAA5412_REG_CH1_FINE_GAIN		PCMDEVICE_REG(0X0, 0x53)
#define TAA5412_REG_CH2_FINE_GAIN		PCMDEVICE_REG(0X0, 0x58)
#define TAA5412_REG_CH3_FINE_GAIN		PCMDEVICE_REG(0X0, 0x5C)
#define TAA5412_REG_CH4_FINE_GAIN		PCMDEVICE_REG(0X0, 0x60)

#define PCMDEVICE_CMD_SING_W		0x1
#define PCMDEVICE_CMD_BURST		0x2
#define PCMDEVICE_CMD_DELAY		0x3
#define PCMDEVICE_CMD_FIELD_W		0x4

enum pcmdevice_bin_blk_type {
	PCMDEVICE_BIN_BLK_COEFF = 1,
	PCMDEVICE_BIN_BLK_POST_POWER_UP,
	PCMDEVICE_BIN_BLK_PRE_SHUTDOWN,
	PCMDEVICE_BIN_BLK_PRE_POWER_UP,
	PCMDEVICE_BIN_BLK_POST_SHUTDOWN
};

enum pcmdevice_fw_state {
	PCMDEVICE_FW_LOAD_OK = 0,
	PCMDEVICE_FW_LOAD_FAILED
};

struct pcmdevice_regbin_hdr {
	unsigned int img_sz;
	unsigned int checksum;
	unsigned int binary_version_num;
	unsigned int drv_fw_version;
	unsigned int timestamp;
	unsigned char plat_type;
	unsigned char dev_family;
	unsigned char reserve;
	unsigned char ndev;
	unsigned char devs[PCMDEVICE_MAX_REGBIN_DEVICES];
	unsigned int nconfig;
	unsigned int config_size[PCMDEVICE_CONFIG_SUM];
};

struct pcmdevice_block_data {
	unsigned char dev_idx;
	unsigned char block_type;
	unsigned short yram_checksum;
	unsigned int block_size;
	unsigned int n_subblks;
	unsigned char *regdata;
};

struct pcmdevice_config_info {
	char cfg_name[64];
	unsigned int nblocks;
	unsigned int real_nblocks;
	unsigned char active_dev;
	struct pcmdevice_block_data **blk_data;
};

struct pcmdevice_regbin {
	struct pcmdevice_regbin_hdr fw_hdr;
	int ncfgs;
	struct pcmdevice_config_info **cfg_info;
};

struct pcmdevice_irqinfo {
	int gpio;
	int nmb;
};

struct pcmdevice_priv {
	struct snd_soc_component *component;
	struct i2c_client *client;
	struct device *dev;
	struct mutex codec_lock;
	struct gpio_desc *hw_rst;
	struct regmap *regmap;
	struct pcmdevice_regbin regbin;
	struct pcmdevice_irqinfo irq_info;
	unsigned int addr[PCMDEVICE_MAX_I2C_DEVICES];
	unsigned int chip_id;
	int cur_conf;
	int fw_state;
	int ndev;
	unsigned char bin_name[PCMDEVICE_BIN_FILENAME_LEN];
	/* used for kcontrol name */
	unsigned char upper_dev_name[I2C_NAME_SIZE];
	unsigned char dev_name[I2C_NAME_SIZE];
};

/* mixer control */
struct pcmdevice_mixer_control {
	int max;
	int reg;
	unsigned int dev_no;
	unsigned int shift;
	unsigned int invert;
};
struct pcmdev_ctrl_info {
	const unsigned int *gain;
	const struct pcmdevice_mixer_control *pcmdev_ctrl;
	unsigned int ctrl_array_size;
	snd_kcontrol_get_t *get;
	snd_kcontrol_put_t *put;
	int pcmdev_ctrl_name_id;
};
#endif /* __PCM6240_H__ */
