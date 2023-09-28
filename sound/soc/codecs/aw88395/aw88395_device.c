// SPDX-License-Identifier: GPL-2.0-only
//
// aw88395_device.c --  AW88395 function for ALSA Audio Driver
//
// Copyright (c) 2022-2023 AWINIC Technology CO., LTD
//
// Author: Bruce zhao <zhaolei@awinic.com>
// Author: Ben Yi <yijiangtao@awinic.com>
//

#include <linux/crc32.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include "aw88395_device.h"
#include "aw88395_reg.h"

static int aw_dev_dsp_write_16bit(struct aw_device *aw_dev,
		unsigned short dsp_addr, unsigned int dsp_data)
{
	int ret;

	ret = regmap_write(aw_dev->regmap, AW88395_DSPMADD_REG, dsp_addr);
	if (ret) {
		dev_err(aw_dev->dev, "%s write addr error, ret=%d", __func__, ret);
		return ret;
	}

	ret = regmap_write(aw_dev->regmap, AW88395_DSPMDAT_REG, (u16)dsp_data);
	if (ret) {
		dev_err(aw_dev->dev, "%s write data error, ret=%d", __func__, ret);
		return ret;
	}

	return 0;
}

static int aw_dev_dsp_write_32bit(struct aw_device *aw_dev,
		unsigned short dsp_addr, unsigned int dsp_data)
{
	u16 temp_data;
	int ret;

	ret = regmap_write(aw_dev->regmap, AW88395_DSPMADD_REG, dsp_addr);
	if (ret) {
		dev_err(aw_dev->dev, "%s write addr error, ret=%d", __func__, ret);
		return ret;
	}

	temp_data = dsp_data & AW88395_DSP_16_DATA_MASK;
	ret = regmap_write(aw_dev->regmap, AW88395_DSPMDAT_REG, (u16)temp_data);
	if (ret) {
		dev_err(aw_dev->dev, "%s write datal error, ret=%d", __func__, ret);
		return ret;
	}

	temp_data = dsp_data >> 16;
	ret = regmap_write(aw_dev->regmap, AW88395_DSPMDAT_REG, (u16)temp_data);
	if (ret) {
		dev_err(aw_dev->dev, "%s write datah error, ret=%d", __func__, ret);
		return ret;
	}

	return 0;
}

static int aw_dev_dsp_write(struct aw_device *aw_dev,
		unsigned short dsp_addr, unsigned int dsp_data, unsigned char data_type)
{
	u32 reg_value;
	int ret;

	mutex_lock(&aw_dev->dsp_lock);
	switch (data_type) {
	case AW88395_DSP_16_DATA:
		ret = aw_dev_dsp_write_16bit(aw_dev, dsp_addr, dsp_data);
		if (ret)
			dev_err(aw_dev->dev, "write dsp_addr[0x%x] 16-bit dsp_data[0x%x] failed",
					(u32)dsp_addr, dsp_data);
		break;
	case AW88395_DSP_32_DATA:
		ret = aw_dev_dsp_write_32bit(aw_dev, dsp_addr, dsp_data);
		if (ret)
			dev_err(aw_dev->dev, "write dsp_addr[0x%x] 32-bit dsp_data[0x%x] failed",
					(u32)dsp_addr, dsp_data);
		break;
	default:
		dev_err(aw_dev->dev, "data type[%d] unsupported", data_type);
		ret = -EINVAL;
		break;
	}

	/* clear dsp chip select state*/
	if (regmap_read(aw_dev->regmap, AW88395_ID_REG, &reg_value))
		dev_err(aw_dev->dev, "%s fail to clear chip state. Err=%d\n", __func__, ret);
	mutex_unlock(&aw_dev->dsp_lock);

	return ret;
}

static int aw_dev_dsp_read_16bit(struct aw_device *aw_dev,
		unsigned short dsp_addr, unsigned int *dsp_data)
{
	unsigned int temp_data;
	int ret;

	ret = regmap_write(aw_dev->regmap, AW88395_DSPMADD_REG, dsp_addr);
	if (ret) {
		dev_err(aw_dev->dev, "%s write error, ret=%d", __func__, ret);
		return ret;
	}

	ret = regmap_read(aw_dev->regmap, AW88395_DSPMDAT_REG, &temp_data);
	if (ret) {
		dev_err(aw_dev->dev, "%s read error, ret=%d", __func__, ret);
		return ret;
	}
	*dsp_data = temp_data;

	return 0;
}

static int aw_dev_dsp_read_32bit(struct aw_device *aw_dev,
		unsigned short dsp_addr, unsigned int *dsp_data)
{
	unsigned int temp_data;
	int ret;

	ret = regmap_write(aw_dev->regmap, AW88395_DSPMADD_REG, dsp_addr);
	if (ret) {
		dev_err(aw_dev->dev, "%s write error, ret=%d", __func__, ret);
		return ret;
	}

	ret = regmap_read(aw_dev->regmap, AW88395_DSPMDAT_REG, &temp_data);
	if (ret) {
		dev_err(aw_dev->dev, "%s read error, ret=%d", __func__, ret);
		return ret;
	}
	*dsp_data = temp_data;

	ret = regmap_read(aw_dev->regmap, AW88395_DSPMDAT_REG, &temp_data);
	if (ret) {
		dev_err(aw_dev->dev, "%s read error, ret=%d", __func__, ret);
		return ret;
	}
	*dsp_data |= (temp_data << 16);

	return 0;
}

static int aw_dev_dsp_read(struct aw_device *aw_dev,
		unsigned short dsp_addr, unsigned int *dsp_data, unsigned char data_type)
{
	u32 reg_value;
	int ret;

	mutex_lock(&aw_dev->dsp_lock);
	switch (data_type) {
	case AW88395_DSP_16_DATA:
		ret = aw_dev_dsp_read_16bit(aw_dev, dsp_addr, dsp_data);
		if (ret)
			dev_err(aw_dev->dev, "read dsp_addr[0x%x] 16-bit dsp_data[0x%x] failed",
					(u32)dsp_addr, *dsp_data);
		break;
	case AW88395_DSP_32_DATA:
		ret = aw_dev_dsp_read_32bit(aw_dev, dsp_addr, dsp_data);
		if (ret)
			dev_err(aw_dev->dev, "read dsp_addr[0x%x] 32r-bit dsp_data[0x%x] failed",
					(u32)dsp_addr, *dsp_data);
		break;
	default:
		dev_err(aw_dev->dev, "data type[%d] unsupported", data_type);
		ret = -EINVAL;
		break;
	}

	/* clear dsp chip select state*/
	if (regmap_read(aw_dev->regmap, AW88395_ID_REG, &reg_value))
		dev_err(aw_dev->dev, "%s fail to clear chip state. Err=%d\n", __func__, ret);
	mutex_unlock(&aw_dev->dsp_lock);

	return ret;
}


static int aw_dev_read_chipid(struct aw_device *aw_dev, u16 *chip_id)
{
	int reg_val;
	int ret;

	ret = regmap_read(aw_dev->regmap, AW88395_CHIP_ID_REG, &reg_val);
	if (ret) {
		dev_err(aw_dev->dev, "%s read chipid error. ret = %d", __func__, ret);
		return ret;
	}

	dev_info(aw_dev->dev, "chip id = %x\n", reg_val);
	*chip_id = reg_val;

	return 0;
}

static unsigned int reg_val_to_db(unsigned int value)
{
	return (((value >> AW88395_VOL_6DB_START) * AW88395_VOLUME_STEP_DB) +
			((value & 0x3f) % AW88395_VOLUME_STEP_DB));
}

static unsigned short db_to_reg_val(unsigned short value)
{
	return (((value / AW88395_VOLUME_STEP_DB) << AW88395_VOL_6DB_START) +
			(value % AW88395_VOLUME_STEP_DB));
}

static int aw_dev_dsp_fw_check(struct aw_device *aw_dev)
{
	struct aw_sec_data_desc *dsp_fw_desc;
	struct aw_prof_desc *set_prof_desc;
	u16 base_addr = AW88395_DSP_FW_ADDR;
	u16 addr = base_addr;
	u32 dsp_val;
	u16 bin_val;
	int ret, i;

	ret = aw88395_dev_get_prof_data(aw_dev, aw_dev->prof_cur, &set_prof_desc);
	if (ret)
		return ret;

	/* update reg */
	dsp_fw_desc = &set_prof_desc->sec_desc[AW88395_DATA_TYPE_DSP_FW];

	for (i = 0; i < AW88395_FW_CHECK_PART; i++) {
		ret = aw_dev_dsp_read(aw_dev, addr, &dsp_val, AW88395_DSP_16_DATA);
		if (ret) {
			dev_err(aw_dev->dev, "dsp read failed");
			return ret;
		}

		bin_val = be16_to_cpup((void *)&dsp_fw_desc->data[2 * (addr - base_addr)]);

		if (dsp_val != bin_val) {
			dev_err(aw_dev->dev, "fw check failed, addr[0x%x], read[0x%x] != bindata[0x%x]",
					addr, dsp_val, bin_val);
			return -EINVAL;
		}

		addr += (dsp_fw_desc->len / 2) / AW88395_FW_CHECK_PART;
		if ((addr - base_addr) > dsp_fw_desc->len) {
			dev_err(aw_dev->dev, "fw check failed, addr[0x%x] too large", addr);
			return -EINVAL;
		}
	}

	return 0;
}

static int aw_dev_set_volume(struct aw_device *aw_dev, unsigned int value)
{
	struct aw_volume_desc *vol_desc = &aw_dev->volume_desc;
	unsigned int reg_value;
	u16 real_value, volume;
	int ret;

	volume = min((value + vol_desc->init_volume), (unsigned int)AW88395_MUTE_VOL);
	real_value = db_to_reg_val(volume);

	/* cal real value */
	ret = regmap_read(aw_dev->regmap, AW88395_SYSCTRL2_REG, &reg_value);
	if (ret)
		return ret;

	dev_dbg(aw_dev->dev, "value 0x%x , reg:0x%x", value, real_value);

	/* [15 : 6] volume */
	real_value = (real_value << AW88395_VOL_START_BIT) | (reg_value & AW88395_VOL_MASK);

	/* write value */
	ret = regmap_write(aw_dev->regmap, AW88395_SYSCTRL2_REG, real_value);

	return ret;
}

void aw88395_dev_set_volume(struct aw_device *aw_dev, unsigned short set_vol)
{
	int ret;

	ret = aw_dev_set_volume(aw_dev, set_vol);
	if (ret)
		dev_dbg(aw_dev->dev, "set volume failed");
}
EXPORT_SYMBOL_GPL(aw88395_dev_set_volume);

static void aw_dev_fade_in(struct aw_device *aw_dev)
{
	struct aw_volume_desc *desc = &aw_dev->volume_desc;
	u16 fade_in_vol = desc->ctl_volume;
	int fade_step = aw_dev->fade_step;
	int i;

	if (fade_step == 0 || aw_dev->fade_in_time == 0) {
		aw_dev_set_volume(aw_dev, fade_in_vol);
		return;
	}

	for (i = AW88395_MUTE_VOL; i >= fade_in_vol; i -= fade_step) {
		aw_dev_set_volume(aw_dev, i);
		usleep_range(aw_dev->fade_in_time, aw_dev->fade_in_time + 10);
	}

	if (i != fade_in_vol)
		aw_dev_set_volume(aw_dev, fade_in_vol);
}

static void aw_dev_fade_out(struct aw_device *aw_dev)
{
	struct aw_volume_desc *desc = &aw_dev->volume_desc;
	int fade_step = aw_dev->fade_step;
	int i;

	if (fade_step == 0 || aw_dev->fade_out_time == 0) {
		aw_dev_set_volume(aw_dev, AW88395_MUTE_VOL);
		return;
	}

	for (i = desc->ctl_volume; i <= AW88395_MUTE_VOL; i += fade_step) {
		aw_dev_set_volume(aw_dev, i);
		usleep_range(aw_dev->fade_out_time, aw_dev->fade_out_time + 10);
	}

	if (i != AW88395_MUTE_VOL) {
		aw_dev_set_volume(aw_dev, AW88395_MUTE_VOL);
		usleep_range(aw_dev->fade_out_time, aw_dev->fade_out_time + 10);
	}
}

static int aw_dev_modify_dsp_cfg(struct aw_device *aw_dev,
			unsigned int addr, unsigned int dsp_data, unsigned char data_type)
{
	struct aw_sec_data_desc *crc_dsp_cfg = &aw_dev->crc_dsp_cfg;
	unsigned int addr_offset;
	__le16 data1;
	__le32 data2;

	dev_dbg(aw_dev->dev, "addr:0x%x, dsp_data:0x%x", addr, dsp_data);

	addr_offset = (addr - AW88395_DSP_CFG_ADDR) * 2;
	if (addr_offset > crc_dsp_cfg->len) {
		dev_err(aw_dev->dev, "addr_offset[%d] > crc_dsp_cfg->len[%d]",
				addr_offset, crc_dsp_cfg->len);
		return -EINVAL;
	}
	switch (data_type) {
	case AW88395_DSP_16_DATA:
		data1 = cpu_to_le16((u16)dsp_data);
		memcpy(crc_dsp_cfg->data + addr_offset, (u8 *)&data1, 2);
		break;
	case AW88395_DSP_32_DATA:
		data2 = cpu_to_le32(dsp_data);
		memcpy(crc_dsp_cfg->data + addr_offset, (u8 *)&data2, 4);
		break;
	default:
		dev_err(aw_dev->dev, "data type[%d] unsupported", data_type);
		return -EINVAL;
	}

	return 0;
}

static int aw_dev_dsp_set_cali_re(struct aw_device *aw_dev)
{
	u32 cali_re;
	int ret;

	cali_re = AW88395_SHOW_RE_TO_DSP_RE((aw_dev->cali_desc.cali_re +
		aw_dev->cali_desc.ra), AW88395_DSP_RE_SHIFT);

	/* set cali re to device */
	ret = aw_dev_dsp_write(aw_dev,
			AW88395_DSP_REG_CFG_ADPZ_RE, cali_re, AW88395_DSP_32_DATA);
	if (ret) {
		dev_err(aw_dev->dev, "set cali re error");
		return ret;
	}

	ret = aw_dev_modify_dsp_cfg(aw_dev, AW88395_DSP_REG_CFG_ADPZ_RE,
				cali_re, AW88395_DSP_32_DATA);
	if (ret)
		dev_err(aw_dev->dev, "modify dsp cfg failed");

	return ret;
}

static void aw_dev_i2s_tx_enable(struct aw_device *aw_dev, bool flag)
{
	int ret;

	if (flag) {
		ret = regmap_update_bits(aw_dev->regmap, AW88395_I2SCFG1_REG,
			~AW88395_I2STXEN_MASK, AW88395_I2STXEN_ENABLE_VALUE);
	} else {
		ret = regmap_update_bits(aw_dev->regmap, AW88395_I2SCFG1_REG,
			~AW88395_I2STXEN_MASK, AW88395_I2STXEN_DISABLE_VALUE);
	}

	if (ret)
		dev_dbg(aw_dev->dev, "%s failed", __func__);
}

static int aw_dev_dsp_set_crc32(struct aw_device *aw_dev)
{
	struct aw_sec_data_desc *crc_dsp_cfg = &aw_dev->crc_dsp_cfg;
	u32 crc_value, crc_data_len;

	/* get crc data len */
	crc_data_len = (AW88395_DSP_REG_CRC_ADDR - AW88395_DSP_CFG_ADDR) * 2;
	if (crc_data_len > crc_dsp_cfg->len) {
		dev_err(aw_dev->dev, "crc data len :%d > cfg_data len:%d",
			crc_data_len, crc_dsp_cfg->len);
		return -EINVAL;
	}

	if (crc_data_len & 0x11) {
		dev_err(aw_dev->dev, "The crc data len :%d unsupport", crc_data_len);
		return -EINVAL;
	}

	crc_value = __crc32c_le(0xFFFFFFFF, crc_dsp_cfg->data, crc_data_len) ^ 0xFFFFFFFF;

	return aw_dev_dsp_write(aw_dev, AW88395_DSP_REG_CRC_ADDR, crc_value,
						AW88395_DSP_32_DATA);
}

static void aw_dev_dsp_check_crc_enable(struct aw_device *aw_dev, bool flag)
{
	int ret;

	if (flag) {
		ret = regmap_update_bits(aw_dev->regmap, AW88395_HAGCCFG7_REG,
			~AW88395_AGC_DSP_CTL_MASK, AW88395_AGC_DSP_CTL_ENABLE_VALUE);
	} else {
		ret = regmap_update_bits(aw_dev->regmap, AW88395_HAGCCFG7_REG,
			~AW88395_AGC_DSP_CTL_MASK, AW88395_AGC_DSP_CTL_DISABLE_VALUE);
	}
	if (ret)
		dev_dbg(aw_dev->dev, "%s failed", __func__);
}

static int aw_dev_dsp_check_st(struct aw_device *aw_dev)
{
	unsigned int reg_val;
	int ret;
	int i;

	for (i = 0; i < AW88395_DSP_ST_CHECK_MAX; i++) {
		ret = regmap_read(aw_dev->regmap, AW88395_SYSST_REG, &reg_val);
		if (ret) {
			dev_err(aw_dev->dev, "read reg0x%x failed", AW88395_SYSST_REG);
			continue;
		}

		if ((reg_val & (~AW88395_DSPS_MASK)) != AW88395_DSPS_NORMAL_VALUE) {
			dev_err(aw_dev->dev, "check dsp st fail,reg_val:0x%04x", reg_val);
			ret = -EPERM;
			continue;
		} else {
			dev_dbg(aw_dev->dev, "dsp st check ok, reg_val:0x%04x", reg_val);
			return 0;
		}
	}

	return ret;
}

static void aw_dev_dsp_enable(struct aw_device *aw_dev, bool is_enable)
{
	int ret;

	if (is_enable) {
		ret = regmap_update_bits(aw_dev->regmap, AW88395_SYSCTRL_REG,
					~AW88395_DSPBY_MASK, AW88395_DSPBY_WORKING_VALUE);
		if (ret)
			dev_dbg(aw_dev->dev, "enable dsp failed");
	} else {
		ret = regmap_update_bits(aw_dev->regmap, AW88395_SYSCTRL_REG,
					~AW88395_DSPBY_MASK, AW88395_DSPBY_BYPASS_VALUE);
		if (ret)
			dev_dbg(aw_dev->dev, "disable dsp failed");
	}
}

static int aw_dev_dsp_check_crc32(struct aw_device *aw_dev)
{
	int ret;

	if (aw_dev->dsp_cfg == AW88395_DEV_DSP_BYPASS) {
		dev_info(aw_dev->dev, "dsp bypass");
		return 0;
	}

	ret = aw_dev_dsp_set_crc32(aw_dev);
	if (ret) {
		dev_err(aw_dev->dev, "set dsp crc32 failed");
		return ret;
	}

	aw_dev_dsp_check_crc_enable(aw_dev, true);

	/* dsp enable */
	aw_dev_dsp_enable(aw_dev, true);
	usleep_range(AW88395_5000_US, AW88395_5000_US + 100);

	ret = aw_dev_dsp_check_st(aw_dev);
	if (ret) {
		dev_err(aw_dev->dev, "check crc32 fail");
	} else {
		aw_dev_dsp_check_crc_enable(aw_dev, false);
		aw_dev->dsp_crc_st = AW88395_DSP_CRC_OK;
	}

	return ret;
}

static void aw_dev_pwd(struct aw_device *aw_dev, bool pwd)
{
	int ret;

	if (pwd) {
		ret = regmap_update_bits(aw_dev->regmap, AW88395_SYSCTRL_REG,
				~AW88395_PWDN_MASK,	AW88395_PWDN_POWER_DOWN_VALUE);
	} else {
		ret = regmap_update_bits(aw_dev->regmap, AW88395_SYSCTRL_REG,
				~AW88395_PWDN_MASK,	AW88395_PWDN_WORKING_VALUE);
	}
	if (ret)
		dev_dbg(aw_dev->dev, "%s failed", __func__);
}

static void aw_dev_amppd(struct aw_device *aw_dev, bool amppd)
{
	int ret;

	if (amppd) {
		ret = regmap_update_bits(aw_dev->regmap, AW88395_SYSCTRL_REG,
				~AW88395_AMPPD_MASK, AW88395_AMPPD_POWER_DOWN_VALUE);
	} else {
		ret = regmap_update_bits(aw_dev->regmap, AW88395_SYSCTRL_REG,
				~AW88395_AMPPD_MASK, AW88395_AMPPD_WORKING_VALUE);
	}
	if (ret)
		dev_dbg(aw_dev->dev, "%s failed", __func__);
}

void aw88395_dev_mute(struct aw_device *aw_dev, bool is_mute)
{
	int ret;

	if (is_mute) {
		aw_dev_fade_out(aw_dev);
		ret = regmap_update_bits(aw_dev->regmap, AW88395_SYSCTRL_REG,
				~AW88395_HMUTE_MASK, AW88395_HMUTE_ENABLE_VALUE);
	} else {
		ret = regmap_update_bits(aw_dev->regmap, AW88395_SYSCTRL_REG,
				~AW88395_HMUTE_MASK, AW88395_HMUTE_DISABLE_VALUE);
		aw_dev_fade_in(aw_dev);
	}

	if (ret)
		dev_dbg(aw_dev->dev, "%s failed", __func__);
}
EXPORT_SYMBOL_GPL(aw88395_dev_mute);

static int aw_dev_get_icalk(struct aw_device *aw_dev, int16_t *icalk)
{
	unsigned int reg_val;
	u16 reg_icalk;
	int ret;

	ret = regmap_read(aw_dev->regmap, AW88395_EFRM2_REG, &reg_val);
	if (ret)
		return ret;

	reg_icalk = reg_val & (~AW88395_EF_ISN_GESLP_MASK);

	if (reg_icalk & (~AW88395_EF_ISN_GESLP_SIGN_MASK))
		reg_icalk = reg_icalk | AW88395_EF_ISN_GESLP_SIGN_NEG;

	*icalk = (int16_t)reg_icalk;

	return ret;
}

static int aw_dev_get_vcalk(struct aw_device *aw_dev, int16_t *vcalk)
{
	unsigned int reg_val;
	u16 reg_vcalk;
	int ret;

	ret = regmap_read(aw_dev->regmap, AW88395_EFRH_REG, &reg_val);
	if (ret)
		return ret;

	reg_val = reg_val >> AW88395_EF_VSENSE_GAIN_SHIFT;

	reg_vcalk = (u16)reg_val & (~AW88395_EF_VSN_GESLP_MASK);

	if (reg_vcalk & (~AW88395_EF_VSN_GESLP_SIGN_MASK))
		reg_vcalk = reg_vcalk | AW88395_EF_VSN_GESLP_SIGN_NEG;

	*vcalk = (int16_t)reg_vcalk;

	return ret;
}

static int aw_dev_get_vcalk_dac(struct aw_device *aw_dev, int16_t *vcalk)
{
	unsigned int reg_val;
	u16 reg_vcalk;
	int ret;

	ret = regmap_read(aw_dev->regmap, AW88395_EFRM2_REG, &reg_val);
	if (ret)
		return ret;

	reg_vcalk = reg_val >> AW88395_EF_DAC_GESLP_SHIFT;

	if (reg_vcalk & AW88395_EF_DAC_GESLP_SIGN_MASK)
		reg_vcalk = reg_vcalk | AW88395_EF_DAC_GESLP_SIGN_NEG;

	*vcalk = (int16_t)reg_vcalk;

	return ret;
}

static int aw_dev_vsense_select(struct aw_device *aw_dev, int *vsense_select)
{
	unsigned int vsense_reg_val;
	int ret;

	ret = regmap_read(aw_dev->regmap, AW88395_I2SCFG3_REG, &vsense_reg_val);
	if (ret) {
		dev_err(aw_dev->dev, "read vsense_reg_val failed");
		return ret;
	}
	dev_dbg(aw_dev->dev, "vsense_reg = 0x%x", vsense_reg_val);

	if (vsense_reg_val & (~AW88395_VDSEL_MASK)) {
		*vsense_select = AW88395_DEV_VDSEL_VSENSE;
		dev_dbg(aw_dev->dev, "vsense outside");
	} else {
		*vsense_select = AW88395_DEV_VDSEL_DAC;
		dev_dbg(aw_dev->dev, "vsense inside");
	}

	return 0;
}

static int aw_dev_set_vcalb(struct aw_device *aw_dev)
{
	int16_t icalk_val, vcalk_val;
	int icalk, vsense_select;
	u32 vcalb_adj, reg_val;
	int vcalb, vcalk;
	int ret;

	ret = aw_dev_dsp_read(aw_dev, AW88395_DSP_REG_VCALB, &vcalb_adj, AW88395_DSP_16_DATA);
	if (ret) {
		dev_err(aw_dev->dev, "read vcalb_adj failed");
		return ret;
	}

	ret = aw_dev_vsense_select(aw_dev, &vsense_select);
	if (ret)
		return ret;
	dev_dbg(aw_dev->dev, "vsense_select = %d", vsense_select);

	ret = aw_dev_get_icalk(aw_dev, &icalk_val);
	if (ret)
		return ret;
	icalk = AW88395_CABL_BASE_VALUE + AW88395_ICABLK_FACTOR * icalk_val;

	switch (vsense_select) {
	case AW88395_DEV_VDSEL_VSENSE:
		ret = aw_dev_get_vcalk(aw_dev, &vcalk_val);
		if (ret)
			return ret;
		vcalk = AW88395_CABL_BASE_VALUE + AW88395_VCABLK_FACTOR * vcalk_val;
		vcalb = AW88395_VCAL_FACTOR * AW88395_VSCAL_FACTOR /
			AW88395_ISCAL_FACTOR * icalk / vcalk * vcalb_adj;

		dev_dbg(aw_dev->dev, "vcalk_factor=%d, vscal_factor=%d, icalk=%d, vcalk=%d",
				AW88395_VCABLK_FACTOR, AW88395_VSCAL_FACTOR, icalk, vcalk);
		break;
	case AW88395_DEV_VDSEL_DAC:
		ret = aw_dev_get_vcalk_dac(aw_dev, &vcalk_val);
		if (ret)
			return ret;
		vcalk = AW88395_CABL_BASE_VALUE + AW88395_VCABLK_FACTOR_DAC * vcalk_val;
		vcalb = AW88395_VCAL_FACTOR * AW88395_VSCAL_FACTOR_DAC /
			AW88395_ISCAL_FACTOR * icalk / vcalk * vcalb_adj;

		dev_dbg(aw_dev->dev, "vcalk_dac_factor=%d, vscal_dac_factor=%d, icalk=%d, vcalk=%d",
				AW88395_VCABLK_FACTOR_DAC,
				AW88395_VSCAL_FACTOR_DAC, icalk, vcalk);
		break;
	default:
		dev_err(aw_dev->dev, "unsupport vsense status");
		return -EINVAL;
	}

	if ((vcalk == 0) || (AW88395_ISCAL_FACTOR == 0)) {
		dev_err(aw_dev->dev, "vcalk:%d or desc->iscal_factor:%d unsupported",
			vcalk, AW88395_ISCAL_FACTOR);
		return -EINVAL;
	}

	vcalb = vcalb >> AW88395_VCALB_ADJ_FACTOR;
	reg_val = (u32)vcalb;

	dev_dbg(aw_dev->dev, "vcalb=%d, reg_val=0x%x, vcalb_adj =0x%x",
				vcalb, reg_val, vcalb_adj);

	ret = aw_dev_dsp_write(aw_dev, AW88395_DSP_REG_VCALB, reg_val, AW88395_DSP_16_DATA);
	if (ret) {
		dev_err(aw_dev->dev, "write vcalb failed");
		return ret;
	}

	ret = aw_dev_modify_dsp_cfg(aw_dev, AW88395_DSP_REG_VCALB,
					(u32)reg_val, AW88395_DSP_16_DATA);
	if (ret)
		dev_err(aw_dev->dev, "modify dsp cfg failed");

	return ret;
}

static int aw_dev_get_cali_f0_delay(struct aw_device *aw_dev)
{
	struct aw_cali_delay_desc *desc = &aw_dev->cali_delay_desc;
	u32 cali_delay;
	int ret;

	ret = aw_dev_dsp_read(aw_dev,
			AW88395_DSP_CALI_F0_DELAY, &cali_delay, AW88395_DSP_16_DATA);
	if (ret)
		dev_err(aw_dev->dev, "read cali delay failed, ret=%d", ret);
	else
		desc->delay = AW88395_CALI_DELAY_CACL(cali_delay);

	dev_dbg(aw_dev->dev, "read cali delay: %d ms", desc->delay);

	return ret;
}

static void aw_dev_get_int_status(struct aw_device *aw_dev, unsigned short *int_status)
{
	unsigned int reg_val;
	int ret;

	ret = regmap_read(aw_dev->regmap, AW88395_SYSINT_REG, &reg_val);
	if (ret)
		dev_err(aw_dev->dev, "read interrupt reg fail, ret=%d", ret);
	else
		*int_status = reg_val;

	dev_dbg(aw_dev->dev, "read interrupt reg = 0x%04x", *int_status);
}

static void aw_dev_clear_int_status(struct aw_device *aw_dev)
{
	u16 int_status;

	/* read int status and clear */
	aw_dev_get_int_status(aw_dev, &int_status);
	/* make sure int status is clear */
	aw_dev_get_int_status(aw_dev, &int_status);
	if (int_status)
		dev_info(aw_dev->dev, "int status(%d) is not cleaned.\n", int_status);
}

static int aw_dev_get_iis_status(struct aw_device *aw_dev)
{
	unsigned int reg_val;
	int ret;

	ret = regmap_read(aw_dev->regmap, AW88395_SYSST_REG, &reg_val);
	if (ret)
		return -EIO;
	if ((reg_val & AW88395_BIT_PLL_CHECK) != AW88395_BIT_PLL_CHECK) {
		dev_err(aw_dev->dev, "check pll lock fail,reg_val:0x%04x", reg_val);
		return -EINVAL;
	}

	return 0;
}

static int aw_dev_check_mode1_pll(struct aw_device *aw_dev)
{
	int ret, i;

	for (i = 0; i < AW88395_DEV_SYSST_CHECK_MAX; i++) {
		ret = aw_dev_get_iis_status(aw_dev);
		if (ret < 0) {
			dev_err(aw_dev->dev, "mode1 iis signal check error");
			usleep_range(AW88395_2000_US, AW88395_2000_US + 10);
		} else {
			return 0;
		}
	}

	return -EPERM;
}

static int aw_dev_check_mode2_pll(struct aw_device *aw_dev)
{
	unsigned int reg_val;
	int ret, i;

	ret = regmap_read(aw_dev->regmap, AW88395_PLLCTRL1_REG, &reg_val);
	if (ret)
		return ret;

	reg_val &= (~AW88395_CCO_MUX_MASK);
	if (reg_val == AW88395_CCO_MUX_DIVIDED_VALUE) {
		dev_dbg(aw_dev->dev, "CCO_MUX is already divider");
		return -EPERM;
	}

	/* change mode2 */
	ret = regmap_update_bits(aw_dev->regmap, AW88395_PLLCTRL1_REG,
			~AW88395_CCO_MUX_MASK, AW88395_CCO_MUX_DIVIDED_VALUE);
	if (ret)
		return ret;

	for (i = 0; i < AW88395_DEV_SYSST_CHECK_MAX; i++) {
		ret = aw_dev_get_iis_status(aw_dev);
		if (ret) {
			dev_err(aw_dev->dev, "mode2 iis signal check error");
			usleep_range(AW88395_2000_US, AW88395_2000_US + 10);
		} else {
			break;
		}
	}

	/* change mode1 */
	ret = regmap_update_bits(aw_dev->regmap, AW88395_PLLCTRL1_REG,
			~AW88395_CCO_MUX_MASK, AW88395_CCO_MUX_BYPASS_VALUE);
	if (ret == 0) {
		usleep_range(AW88395_2000_US, AW88395_2000_US + 10);
		for (i = 0; i < AW88395_DEV_SYSST_CHECK_MAX; i++) {
			ret = aw_dev_check_mode1_pll(aw_dev);
			if (ret < 0) {
				dev_err(aw_dev->dev, "mode2 switch to mode1, iis signal check error");
				usleep_range(AW88395_2000_US, AW88395_2000_US + 10);
			} else {
				break;
			}
		}
	}

	return ret;
}

static int aw_dev_check_syspll(struct aw_device *aw_dev)
{
	int ret;

	ret = aw_dev_check_mode1_pll(aw_dev);
	if (ret) {
		dev_dbg(aw_dev->dev, "mode1 check iis failed try switch to mode2 check");
		ret = aw_dev_check_mode2_pll(aw_dev);
		if (ret) {
			dev_err(aw_dev->dev, "mode2 check iis failed");
			return ret;
		}
	}

	return ret;
}

static int aw_dev_check_sysst(struct aw_device *aw_dev)
{
	unsigned int check_val;
	unsigned int reg_val;
	int ret, i;

	for (i = 0; i < AW88395_DEV_SYSST_CHECK_MAX; i++) {
		ret = regmap_read(aw_dev->regmap, AW88395_SYSST_REG, &reg_val);
		if (ret)
			return ret;

		check_val = reg_val & (~AW88395_BIT_SYSST_CHECK_MASK)
							& AW88395_BIT_SYSST_CHECK;
		if (check_val != AW88395_BIT_SYSST_CHECK) {
			dev_err(aw_dev->dev, "check sysst fail, cnt=%d, reg_val=0x%04x, check:0x%x",
				i, reg_val, AW88395_BIT_SYSST_CHECK);
			usleep_range(AW88395_2000_US, AW88395_2000_US + 10);
		} else {
			return 0;
		}
	}

	return -EPERM;
}

static int aw_dev_check_sysint(struct aw_device *aw_dev)
{
	u16 reg_val;

	aw_dev_get_int_status(aw_dev, &reg_val);

	if (reg_val & AW88395_BIT_SYSINT_CHECK) {
		dev_err(aw_dev->dev, "pa stop check fail:0x%04x", reg_val);
		return -EINVAL;
	}

	return 0;
}

static void aw_dev_get_cur_mode_st(struct aw_device *aw_dev)
{
	struct aw_profctrl_desc *profctrl_desc = &aw_dev->profctrl_desc;
	unsigned int reg_val;
	int ret;

	ret = regmap_read(aw_dev->regmap, AW88395_SYSCTRL_REG, &reg_val);
	if (ret) {
		dev_dbg(aw_dev->dev, "%s failed", __func__);
		return;
	}
	if ((reg_val & (~AW88395_RCV_MODE_MASK)) == AW88395_RCV_MODE_RECEIVER_VALUE)
		profctrl_desc->cur_mode = AW88395_RCV_MODE;
	else
		profctrl_desc->cur_mode = AW88395_NOT_RCV_MODE;
}

static void aw_dev_get_dsp_config(struct aw_device *aw_dev, unsigned char *dsp_cfg)
{
	unsigned int reg_val = 0;
	int ret;

	ret = regmap_read(aw_dev->regmap, AW88395_SYSCTRL_REG, &reg_val);
	if (ret) {
		dev_dbg(aw_dev->dev, "%s failed", __func__);
		return;
	}
	if (reg_val & (~AW88395_DSPBY_MASK))
		*dsp_cfg = AW88395_DEV_DSP_BYPASS;
	else
		*dsp_cfg = AW88395_DEV_DSP_WORK;
}

static void aw_dev_select_memclk(struct aw_device *aw_dev, unsigned char flag)
{
	int ret;

	switch (flag) {
	case AW88395_DEV_MEMCLK_PLL:
		ret = regmap_update_bits(aw_dev->regmap, AW88395_DBGCTRL_REG,
					~AW88395_MEM_CLKSEL_MASK,
					AW88395_MEM_CLKSEL_DAP_HCLK_VALUE);
		if (ret)
			dev_err(aw_dev->dev, "memclk select pll failed");
		break;
	case AW88395_DEV_MEMCLK_OSC:
		ret = regmap_update_bits(aw_dev->regmap, AW88395_DBGCTRL_REG,
					~AW88395_MEM_CLKSEL_MASK,
					AW88395_MEM_CLKSEL_OSC_CLK_VALUE);
		if (ret)
			dev_err(aw_dev->dev, "memclk select OSC failed");
		break;
	default:
		dev_err(aw_dev->dev, "unknown memclk config, flag=0x%x", flag);
		break;
	}
}

static int aw_dev_get_dsp_status(struct aw_device *aw_dev)
{
	unsigned int reg_val;
	int ret;

	ret = regmap_read(aw_dev->regmap, AW88395_WDT_REG, &reg_val);
	if (ret)
		return ret;
	if (!(reg_val & (~AW88395_WDT_CNT_MASK)))
		ret = -EPERM;

	return ret;
}

static int aw_dev_get_vmax(struct aw_device *aw_dev, unsigned int *vmax)
{
	return aw_dev_dsp_read(aw_dev, AW88395_DSP_REG_VMAX, vmax, AW88395_DSP_16_DATA);
}

static int aw_dev_update_reg_container(struct aw_device *aw_dev,
				unsigned char *data, unsigned int len)
{
	struct aw_volume_desc *vol_desc = &aw_dev->volume_desc;
	unsigned int read_val;
	int16_t *reg_data;
	int data_len;
	u16 read_vol;
	u16 reg_val;
	u8 reg_addr;
	int i, ret;

	reg_data = (int16_t *)data;
	data_len = len >> 1;

	if (data_len & 0x1) {
		dev_err(aw_dev->dev, "data len:%d unsupported",	data_len);
		return -EINVAL;
	}

	for (i = 0; i < data_len; i += 2) {
		reg_addr = reg_data[i];
		reg_val = reg_data[i + 1];

		if (reg_addr == AW88395_SYSCTRL_REG) {
			ret = regmap_read(aw_dev->regmap, reg_addr, &read_val);
			if (ret)
				break;
			read_val &= (~AW88395_HMUTE_MASK);
			reg_val &= AW88395_HMUTE_MASK;
			reg_val |= read_val;
		}
		if (reg_addr == AW88395_HAGCCFG7_REG)
			reg_val &= AW88395_AGC_DSP_CTL_MASK;

		if (reg_addr == AW88395_I2SCFG1_REG) {
			/* close tx */
			reg_val &= AW88395_I2STXEN_MASK;
			reg_val |= AW88395_I2STXEN_DISABLE_VALUE;
		}

		if (reg_addr == AW88395_SYSCTRL2_REG) {
			read_vol = (reg_val & (~AW88395_VOL_MASK)) >>
				AW88395_VOL_START_BIT;
			aw_dev->volume_desc.init_volume =
				reg_val_to_db(read_vol);
		}
		ret = regmap_write(aw_dev->regmap, reg_addr, reg_val);
		if (ret)
			break;

	}

	aw_dev_get_cur_mode_st(aw_dev);

	if (aw_dev->prof_cur != aw_dev->prof_index) {
		/* clear control volume when PA change profile */
		vol_desc->ctl_volume = 0;
	} else {
		/* keep control volume when PA start with sync mode */
		aw_dev_set_volume(aw_dev, vol_desc->ctl_volume);
	}

	aw_dev_get_dsp_config(aw_dev, &aw_dev->dsp_cfg);

	return ret;
}

static int aw_dev_reg_update(struct aw_device *aw_dev,
					unsigned char *data, unsigned int len)
{
	int ret;

	if (!len || !data) {
		dev_err(aw_dev->dev, "reg data is null or len is 0");
		return -EINVAL;
	}

	ret = aw_dev_update_reg_container(aw_dev, data, len);
	if (ret) {
		dev_err(aw_dev->dev, "reg update failed");
		return ret;
	}

	return 0;
}

static int aw_dev_get_ra(struct aw_cali_desc *cali_desc)
{
	struct aw_device *aw_dev =
		container_of(cali_desc, struct aw_device, cali_desc);
	u32 dsp_ra;
	int ret;

	ret = aw_dev_dsp_read(aw_dev, AW88395_DSP_REG_CFG_ADPZ_RA,
				&dsp_ra, AW88395_DSP_32_DATA);
	if (ret) {
		dev_err(aw_dev->dev, "read ra error");
		return ret;
	}

	cali_desc->ra = AW88395_DSP_RE_TO_SHOW_RE(dsp_ra,
					AW88395_DSP_RE_SHIFT);

	return ret;
}

static int aw_dev_dsp_update_container(struct aw_device *aw_dev,
			unsigned char *data, unsigned int len, unsigned short base)
{
	int i, ret;

#ifdef AW88395_DSP_I2C_WRITES
	u32 tmp_len;

	mutex_lock(&aw_dev->dsp_lock);
	ret = regmap_write(aw_dev->regmap, AW88395_DSPMADD_REG, base);
	if (ret)
		goto error_operation;

	for (i = 0; i < len; i += AW88395_MAX_RAM_WRITE_BYTE_SIZE) {
		if ((len - i) < AW88395_MAX_RAM_WRITE_BYTE_SIZE)
			tmp_len = len - i;
		else
			tmp_len = AW88395_MAX_RAM_WRITE_BYTE_SIZE;

		ret = regmap_raw_write(aw_dev->regmap, AW88395_DSPMDAT_REG,
					&data[i], tmp_len);
		if (ret)
			goto error_operation;
	}
	mutex_unlock(&aw_dev->dsp_lock);
#else
	__be16 reg_val;

	mutex_lock(&aw_dev->dsp_lock);
	/* i2c write */
	ret = regmap_write(aw_dev->regmap, AW88395_DSPMADD_REG, base);
	if (ret)
		goto error_operation;
	for (i = 0; i < len; i += 2) {
		reg_val = cpu_to_be16p((u16 *)(data + i));
		ret = regmap_write(aw_dev->regmap, AW88395_DSPMDAT_REG,
					(u16)reg_val);
		if (ret)
			goto error_operation;
	}
	mutex_unlock(&aw_dev->dsp_lock);
#endif

	return 0;

error_operation:
	mutex_unlock(&aw_dev->dsp_lock);
	return ret;
}

static int aw_dev_dsp_update_fw(struct aw_device *aw_dev,
			unsigned char *data, unsigned int len)
{

	dev_dbg(aw_dev->dev, "dsp firmware len:%d", len);

	if (!len || !data) {
		dev_err(aw_dev->dev, "dsp firmware data is null or len is 0");
		return -EINVAL;
	}
	aw_dev_dsp_update_container(aw_dev, data, len, AW88395_DSP_FW_ADDR);
	aw_dev->dsp_fw_len = len;

	return 0;
}

static int aw_dev_copy_to_crc_dsp_cfg(struct aw_device *aw_dev,
			unsigned char *data, unsigned int size)
{
	struct aw_sec_data_desc *crc_dsp_cfg = &aw_dev->crc_dsp_cfg;

	if (!crc_dsp_cfg->data) {
		crc_dsp_cfg->data = devm_kzalloc(aw_dev->dev, size, GFP_KERNEL);
		if (!crc_dsp_cfg->data)
			return -ENOMEM;
		crc_dsp_cfg->len = size;
	} else if (crc_dsp_cfg->len < size) {
		devm_kfree(aw_dev->dev, crc_dsp_cfg->data);
		crc_dsp_cfg->data = devm_kzalloc(aw_dev->dev, size, GFP_KERNEL);
		if (!crc_dsp_cfg->data)
			return -ENOMEM;
		crc_dsp_cfg->len = size;
	}
	memcpy(crc_dsp_cfg->data, data, size);
	swab16_array((u16 *)crc_dsp_cfg->data, size >> 1);

	return 0;
}

static int aw_dev_dsp_update_cfg(struct aw_device *aw_dev,
			unsigned char *data, unsigned int len)
{
	int ret;

	dev_dbg(aw_dev->dev, "dsp config len:%d", len);

	if (!len || !data) {
		dev_err(aw_dev->dev, "dsp config data is null or len is 0");
		return -EINVAL;
	}

	aw_dev_dsp_update_container(aw_dev, data, len, AW88395_DSP_CFG_ADDR);
	aw_dev->dsp_cfg_len = len;

	ret = aw_dev_copy_to_crc_dsp_cfg(aw_dev, data, len);
	if (ret)
		return ret;

	ret = aw_dev_set_vcalb(aw_dev);
	if (ret)
		return ret;
	ret = aw_dev_get_ra(&aw_dev->cali_desc);
	if (ret)
		return ret;
	ret = aw_dev_get_cali_f0_delay(aw_dev);
	if (ret)
		return ret;

	ret = aw_dev_get_vmax(aw_dev, &aw_dev->vmax_desc.init_vmax);
	if (ret) {
		dev_err(aw_dev->dev, "get vmax failed");
		return ret;
	}
	dev_dbg(aw_dev->dev, "get init vmax:0x%x", aw_dev->vmax_desc.init_vmax);
	aw_dev->dsp_crc_st = AW88395_DSP_CRC_NA;

	return 0;
}

static int aw_dev_check_sram(struct aw_device *aw_dev)
{
	unsigned int reg_val;

	mutex_lock(&aw_dev->dsp_lock);
	/* check the odd bits of reg 0x40 */
	regmap_write(aw_dev->regmap, AW88395_DSPMADD_REG, AW88395_DSP_ODD_NUM_BIT_TEST);
	regmap_read(aw_dev->regmap, AW88395_DSPMADD_REG, &reg_val);
	if (reg_val != AW88395_DSP_ODD_NUM_BIT_TEST) {
		dev_err(aw_dev->dev, "check reg 0x40 odd bit failed, read[0x%x] != write[0x%x]",
				reg_val, AW88395_DSP_ODD_NUM_BIT_TEST);
		goto error;
	}

	/* check the even bits of reg 0x40 */
	regmap_write(aw_dev->regmap, AW88395_DSPMADD_REG, AW88395_DSP_EVEN_NUM_BIT_TEST);
	regmap_read(aw_dev->regmap, AW88395_DSPMADD_REG, &reg_val);
	if (reg_val != AW88395_DSP_EVEN_NUM_BIT_TEST) {
		dev_err(aw_dev->dev, "check reg 0x40 even bit failed, read[0x%x] != write[0x%x]",
				reg_val, AW88395_DSP_EVEN_NUM_BIT_TEST);
		goto error;
	}

	/* check dsp_fw_base_addr */
	aw_dev_dsp_write_16bit(aw_dev, AW88395_DSP_FW_ADDR,	AW88395_DSP_EVEN_NUM_BIT_TEST);
	aw_dev_dsp_read_16bit(aw_dev, AW88395_DSP_FW_ADDR, &reg_val);
	if (reg_val != AW88395_DSP_EVEN_NUM_BIT_TEST) {
		dev_err(aw_dev->dev, "check dsp fw addr failed, read[0x%x] != write[0x%x]",
						reg_val, AW88395_DSP_EVEN_NUM_BIT_TEST);
		goto error;
	}

	/* check dsp_cfg_base_addr */
	aw_dev_dsp_write_16bit(aw_dev, AW88395_DSP_CFG_ADDR, AW88395_DSP_ODD_NUM_BIT_TEST);
	aw_dev_dsp_read_16bit(aw_dev, AW88395_DSP_CFG_ADDR, &reg_val);
	if (reg_val != AW88395_DSP_ODD_NUM_BIT_TEST) {
		dev_err(aw_dev->dev, "check dsp cfg failed, read[0x%x] != write[0x%x]",
						reg_val, AW88395_DSP_ODD_NUM_BIT_TEST);
		goto error;
	}
	mutex_unlock(&aw_dev->dsp_lock);

	return 0;

error:
	mutex_unlock(&aw_dev->dsp_lock);
	return -EPERM;
}

int aw88395_dev_fw_update(struct aw_device *aw_dev, bool up_dsp_fw_en, bool force_up_en)
{
	struct aw_prof_desc *prof_index_desc;
	struct aw_sec_data_desc *sec_desc;
	char *prof_name;
	int ret;

	if ((aw_dev->prof_cur == aw_dev->prof_index) &&
			(force_up_en == AW88395_FORCE_UPDATE_OFF)) {
		dev_dbg(aw_dev->dev, "scene no change, not update");
		return 0;
	}

	if (aw_dev->fw_status == AW88395_DEV_FW_FAILED) {
		dev_err(aw_dev->dev, "fw status[%d] error", aw_dev->fw_status);
		return -EPERM;
	}

	prof_name = aw88395_dev_get_prof_name(aw_dev, aw_dev->prof_index);

	dev_dbg(aw_dev->dev, "start update %s", prof_name);

	ret = aw88395_dev_get_prof_data(aw_dev, aw_dev->prof_index, &prof_index_desc);
	if (ret)
		return ret;

	/* update reg */
	sec_desc = prof_index_desc->sec_desc;
	ret = aw_dev_reg_update(aw_dev, sec_desc[AW88395_DATA_TYPE_REG].data,
					sec_desc[AW88395_DATA_TYPE_REG].len);
	if (ret) {
		dev_err(aw_dev->dev, "update reg failed");
		return ret;
	}

	aw88395_dev_mute(aw_dev, true);

	if (aw_dev->dsp_cfg == AW88395_DEV_DSP_WORK)
		aw_dev_dsp_enable(aw_dev, false);

	aw_dev_select_memclk(aw_dev, AW88395_DEV_MEMCLK_OSC);

	if (up_dsp_fw_en) {
		ret = aw_dev_check_sram(aw_dev);
		if (ret) {
			dev_err(aw_dev->dev, "check sram failed");
			goto error;
		}

		/* update dsp firmware */
		dev_dbg(aw_dev->dev, "fw_ver: [%x]", prof_index_desc->fw_ver);
		ret = aw_dev_dsp_update_fw(aw_dev, sec_desc[AW88395_DATA_TYPE_DSP_FW].data,
					sec_desc[AW88395_DATA_TYPE_DSP_FW].len);
		if (ret) {
			dev_err(aw_dev->dev, "update dsp fw failed");
			goto error;
		}
	}

	/* update dsp config */
	ret = aw_dev_dsp_update_cfg(aw_dev, sec_desc[AW88395_DATA_TYPE_DSP_CFG].data,
					sec_desc[AW88395_DATA_TYPE_DSP_CFG].len);
	if (ret) {
		dev_err(aw_dev->dev, "update dsp cfg failed");
		goto error;
	}

	aw_dev_select_memclk(aw_dev, AW88395_DEV_MEMCLK_PLL);

	aw_dev->prof_cur = aw_dev->prof_index;

	return 0;

error:
	aw_dev_select_memclk(aw_dev, AW88395_DEV_MEMCLK_PLL);
	return ret;
}
EXPORT_SYMBOL_GPL(aw88395_dev_fw_update);

static int aw_dev_dsp_check(struct aw_device *aw_dev)
{
	int ret, i;

	switch (aw_dev->dsp_cfg) {
	case AW88395_DEV_DSP_BYPASS:
		dev_dbg(aw_dev->dev, "dsp bypass");
		ret = 0;
		break;
	case AW88395_DEV_DSP_WORK:
		aw_dev_dsp_enable(aw_dev, false);
		aw_dev_dsp_enable(aw_dev, true);
		usleep_range(AW88395_1000_US, AW88395_1000_US + 10);
		for (i = 0; i < AW88395_DEV_DSP_CHECK_MAX; i++) {
			ret = aw_dev_get_dsp_status(aw_dev);
			if (ret) {
				dev_err(aw_dev->dev, "dsp wdt status error=%d", ret);
				usleep_range(AW88395_2000_US, AW88395_2000_US + 10);
			}
		}
		break;
	default:
		dev_err(aw_dev->dev, "unknown dsp cfg=%d", aw_dev->dsp_cfg);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static void aw_dev_update_cali_re(struct aw_cali_desc *cali_desc)
{
	struct aw_device *aw_dev =
		container_of(cali_desc, struct aw_device, cali_desc);
	int ret;

	if ((aw_dev->cali_desc.cali_re < AW88395_CALI_RE_MAX) &&
		(aw_dev->cali_desc.cali_re > AW88395_CALI_RE_MIN)) {

		ret = aw_dev_dsp_set_cali_re(aw_dev);
		if (ret)
			dev_err(aw_dev->dev, "set cali re failed");
	}
}

int aw88395_dev_start(struct aw_device *aw_dev)
{
	int ret;

	if (aw_dev->status == AW88395_DEV_PW_ON) {
		dev_info(aw_dev->dev, "already power on");
		return 0;
	}
	/* power on */
	aw_dev_pwd(aw_dev, false);
	usleep_range(AW88395_2000_US, AW88395_2000_US + 10);

	ret = aw_dev_check_syspll(aw_dev);
	if (ret) {
		dev_err(aw_dev->dev, "pll check failed cannot start");
		goto pll_check_fail;
	}

	/* amppd on */
	aw_dev_amppd(aw_dev, false);
	usleep_range(AW88395_1000_US, AW88395_1000_US + 50);

	/* check i2s status */
	ret = aw_dev_check_sysst(aw_dev);
	if (ret) {
		dev_err(aw_dev->dev, "sysst check failed");
		goto sysst_check_fail;
	}

	if (aw_dev->dsp_cfg == AW88395_DEV_DSP_WORK) {
		/* dsp bypass */
		aw_dev_dsp_enable(aw_dev, false);
		ret = aw_dev_dsp_fw_check(aw_dev);
		if (ret)
			goto dev_dsp_fw_check_fail;

		aw_dev_update_cali_re(&aw_dev->cali_desc);

		if (aw_dev->dsp_crc_st != AW88395_DSP_CRC_OK) {
			ret = aw_dev_dsp_check_crc32(aw_dev);
			if (ret) {
				dev_err(aw_dev->dev, "dsp crc check failed");
				goto crc_check_fail;
			}
		}

		ret = aw_dev_dsp_check(aw_dev);
		if (ret) {
			dev_err(aw_dev->dev, "dsp status check failed");
			goto dsp_check_fail;
		}
	} else {
		dev_dbg(aw_dev->dev, "start pa with dsp bypass");
	}

	/* enable tx feedback */
	aw_dev_i2s_tx_enable(aw_dev, true);

	/* close mute */
	aw88395_dev_mute(aw_dev, false);
	/* clear inturrupt */
	aw_dev_clear_int_status(aw_dev);
	aw_dev->status = AW88395_DEV_PW_ON;

	return 0;

dsp_check_fail:
crc_check_fail:
	aw_dev_dsp_enable(aw_dev, false);
dev_dsp_fw_check_fail:
sysst_check_fail:
	aw_dev_clear_int_status(aw_dev);
	aw_dev_amppd(aw_dev, true);
pll_check_fail:
	aw_dev_pwd(aw_dev, true);
	aw_dev->status = AW88395_DEV_PW_OFF;

	return ret;
}
EXPORT_SYMBOL_GPL(aw88395_dev_start);

int aw88395_dev_stop(struct aw_device *aw_dev)
{
	struct aw_sec_data_desc *dsp_cfg =
		&aw_dev->prof_info.prof_desc[aw_dev->prof_cur].sec_desc[AW88395_DATA_TYPE_DSP_CFG];
	struct aw_sec_data_desc *dsp_fw =
		&aw_dev->prof_info.prof_desc[aw_dev->prof_cur].sec_desc[AW88395_DATA_TYPE_DSP_FW];
	int int_st = 0;
	int ret;

	if (aw_dev->status == AW88395_DEV_PW_OFF) {
		dev_info(aw_dev->dev, "already power off");
		return 0;
	}

	aw_dev->status = AW88395_DEV_PW_OFF;

	/* set mute */
	aw88395_dev_mute(aw_dev, true);
	usleep_range(AW88395_4000_US, AW88395_4000_US + 100);

	/* close tx feedback */
	aw_dev_i2s_tx_enable(aw_dev, false);
	usleep_range(AW88395_1000_US, AW88395_1000_US + 100);

	/* check sysint state */
	int_st = aw_dev_check_sysint(aw_dev);

	/* close dsp */
	aw_dev_dsp_enable(aw_dev, false);

	/* enable amppd */
	aw_dev_amppd(aw_dev, true);

	if (int_st < 0) {
		/* system status anomaly */
		aw_dev_select_memclk(aw_dev, AW88395_DEV_MEMCLK_OSC);
		ret = aw_dev_dsp_update_fw(aw_dev, dsp_fw->data, dsp_fw->len);
		if (ret)
			dev_err(aw_dev->dev, "update dsp fw failed");
		ret = aw_dev_dsp_update_cfg(aw_dev, dsp_cfg->data, dsp_cfg->len);
		if (ret)
			dev_err(aw_dev->dev, "update dsp cfg failed");
		aw_dev_select_memclk(aw_dev, AW88395_DEV_MEMCLK_PLL);
	}

	/* set power down */
	aw_dev_pwd(aw_dev, true);

	return 0;
}
EXPORT_SYMBOL_GPL(aw88395_dev_stop);

int aw88395_dev_init(struct aw_device *aw_dev, struct aw_container *aw_cfg)
{
	int ret;

	if ((!aw_dev) || (!aw_cfg)) {
		pr_err("aw_dev is NULL or aw_cfg is NULL");
		return -ENOMEM;
	}
	ret = aw88395_dev_cfg_load(aw_dev, aw_cfg);
	if (ret) {
		dev_err(aw_dev->dev, "aw_dev acf parse failed");
		return -EINVAL;
	}
	aw_dev->fade_in_time = AW88395_1000_US / 10;
	aw_dev->fade_out_time = AW88395_1000_US >> 1;
	aw_dev->prof_cur = aw_dev->prof_info.prof_desc[0].id;
	aw_dev->prof_index = aw_dev->prof_info.prof_desc[0].id;

	ret = aw88395_dev_fw_update(aw_dev, AW88395_FORCE_UPDATE_ON,	AW88395_DSP_FW_UPDATE_ON);
	if (ret) {
		dev_err(aw_dev->dev, "fw update failed ret = %d\n", ret);
		return ret;
	}

	/* set mute */
	aw88395_dev_mute(aw_dev, true);
	usleep_range(AW88395_4000_US, AW88395_4000_US + 100);

	/* close tx feedback */
	aw_dev_i2s_tx_enable(aw_dev, false);
	usleep_range(AW88395_1000_US, AW88395_1000_US + 100);

	/* close dsp */
	aw_dev_dsp_enable(aw_dev, false);
	/* enable amppd */
	aw_dev_amppd(aw_dev, true);
	/* set power down */
	aw_dev_pwd(aw_dev, true);

	return 0;
}
EXPORT_SYMBOL_GPL(aw88395_dev_init);

static void aw88395_parse_channel_dt(struct aw_device *aw_dev)
{
	struct device_node *np = aw_dev->dev->of_node;
	u32 channel_value;
	int ret;

	ret = of_property_read_u32(np, "awinic,audio-channel", &channel_value);
	if (ret) {
		dev_dbg(aw_dev->dev,
			"read audio-channel failed,use default 0");
		aw_dev->channel = AW88395_DEV_DEFAULT_CH;
		return;
	}

	dev_dbg(aw_dev->dev, "read audio-channel value is: %d",
			channel_value);
	aw_dev->channel = channel_value;
}

static int aw_dev_init(struct aw_device *aw_dev)
{
	aw_dev->chip_id = AW88395_CHIP_ID;
	/* call aw device init func */
	aw_dev->acf = NULL;
	aw_dev->prof_info.prof_desc = NULL;
	aw_dev->prof_info.count = 0;
	aw_dev->prof_info.prof_type = AW88395_DEV_NONE_TYPE_ID;
	aw_dev->channel = 0;
	aw_dev->fw_status = AW88395_DEV_FW_FAILED;

	aw_dev->fade_step = AW88395_VOLUME_STEP_DB;
	aw_dev->volume_desc.ctl_volume = AW88395_VOL_DEFAULT_VALUE;
	aw88395_parse_channel_dt(aw_dev);

	return 0;
}

int aw88395_dev_get_profile_count(struct aw_device *aw_dev)
{
	return aw_dev->prof_info.count;
}
EXPORT_SYMBOL_GPL(aw88395_dev_get_profile_count);

int aw88395_dev_get_profile_index(struct aw_device *aw_dev)
{
	return aw_dev->prof_index;
}
EXPORT_SYMBOL_GPL(aw88395_dev_get_profile_index);

int aw88395_dev_set_profile_index(struct aw_device *aw_dev, int index)
{
	/* check the index whether is valid */
	if ((index >= aw_dev->prof_info.count) || (index < 0))
		return -EINVAL;
	/* check the index whether change */
	if (aw_dev->prof_index == index)
		return -EINVAL;

	aw_dev->prof_index = index;
	dev_dbg(aw_dev->dev, "set prof[%s]",
		aw_dev->prof_info.prof_name_list[aw_dev->prof_info.prof_desc[index].id]);

	return 0;
}
EXPORT_SYMBOL_GPL(aw88395_dev_set_profile_index);

char *aw88395_dev_get_prof_name(struct aw_device *aw_dev, int index)
{
	struct aw_prof_info *prof_info = &aw_dev->prof_info;
	struct aw_prof_desc *prof_desc;

	if ((index >= aw_dev->prof_info.count) || (index < 0)) {
		dev_err(aw_dev->dev, "index[%d] overflow count[%d]",
			index, aw_dev->prof_info.count);
		return NULL;
	}

	prof_desc = &aw_dev->prof_info.prof_desc[index];

	return prof_info->prof_name_list[prof_desc->id];
}
EXPORT_SYMBOL_GPL(aw88395_dev_get_prof_name);

int aw88395_dev_get_prof_data(struct aw_device *aw_dev, int index,
			struct aw_prof_desc **prof_desc)
{
	if ((index >= aw_dev->prof_info.count) || (index < 0)) {
		dev_err(aw_dev->dev, "%s: index[%d] overflow count[%d]\n",
				__func__, index, aw_dev->prof_info.count);
		return -EINVAL;
	}

	*prof_desc = &aw_dev->prof_info.prof_desc[index];

	return 0;
}
EXPORT_SYMBOL_GPL(aw88395_dev_get_prof_data);

int aw88395_init(struct aw_device **aw_dev, struct i2c_client *i2c, struct regmap *regmap)
{
	u16 chip_id;
	int ret;

	if (*aw_dev) {
		dev_info(&i2c->dev, "it should be initialized here.\n");
	} else {
		*aw_dev = devm_kzalloc(&i2c->dev, sizeof(struct aw_device), GFP_KERNEL);
		if (!(*aw_dev))
			return -ENOMEM;
	}

	(*aw_dev)->i2c = i2c;
	(*aw_dev)->dev = &i2c->dev;
	(*aw_dev)->regmap = regmap;
	mutex_init(&(*aw_dev)->dsp_lock);

	/* read chip id */
	ret = aw_dev_read_chipid((*aw_dev), &chip_id);
	if (ret) {
		dev_err(&i2c->dev, "dev_read_chipid failed ret=%d", ret);
		return ret;
	}

	switch (chip_id) {
	case AW88395_CHIP_ID:
		ret = aw_dev_init((*aw_dev));
		break;
	default:
		ret = -EINVAL;
		dev_err((*aw_dev)->dev, "unsupported device");
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(aw88395_init);

MODULE_DESCRIPTION("AW88395 device lib");
MODULE_LICENSE("GPL v2");
