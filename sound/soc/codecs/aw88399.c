// SPDX-License-Identifier: GPL-2.0-only
//
// aw88399.c --  ALSA SoC AW88399 codec support
//
// Copyright (c) 2023 AWINIC Technology CO., LTD
//
// Author: Weidong Wang <wangweidong.a@awinic.com>
//

#include <linux/crc32.h>
#include <linux/i2c.h>
#include <linux/firmware.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include "aw88399.h"
#include "aw88395/aw88395_device.h"
#include "aw88395/aw88395_reg.h"

static const struct regmap_config aw88399_remap_config = {
	.val_bits = 16,
	.reg_bits = 8,
	.max_register = AW88399_REG_MAX,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_BIG,
};

static int aw_dev_dsp_write_16bit(struct aw_device *aw_dev,
		unsigned short dsp_addr, unsigned int dsp_data)
{
	int ret;

	ret = regmap_write(aw_dev->regmap, AW88399_DSPMADD_REG, dsp_addr);
	if (ret) {
		dev_err(aw_dev->dev, "%s write addr error, ret=%d", __func__, ret);
		return ret;
	}

	ret = regmap_write(aw_dev->regmap, AW88399_DSPMDAT_REG, (u16)dsp_data);
	if (ret) {
		dev_err(aw_dev->dev, "%s write data error, ret=%d", __func__, ret);
		return ret;
	}

	return 0;
}

static int aw_dev_dsp_read_16bit(struct aw_device *aw_dev,
		unsigned short dsp_addr, unsigned int *dsp_data)
{
	unsigned int temp_data;
	int ret;

	ret = regmap_write(aw_dev->regmap, AW88399_DSPMADD_REG, dsp_addr);
	if (ret) {
		dev_err(aw_dev->dev, "%s write error, ret=%d", __func__, ret);
		return ret;
	}

	ret = regmap_read(aw_dev->regmap, AW88399_DSPMDAT_REG, &temp_data);
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

	ret = regmap_write(aw_dev->regmap, AW88399_DSPMADD_REG, dsp_addr);
	if (ret) {
		dev_err(aw_dev->dev, "%s write error, ret=%d", __func__, ret);
		return ret;
	}

	ret = regmap_read(aw_dev->regmap, AW88399_DSPMDAT_REG, &temp_data);
	if (ret) {
		dev_err(aw_dev->dev, "%s read error, ret=%d", __func__, ret);
		return ret;
	}
	*dsp_data = temp_data;

	ret = regmap_read(aw_dev->regmap, AW88399_DSPMDAT_REG, &temp_data);
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
	case AW88399_DSP_16_DATA:
		ret = aw_dev_dsp_read_16bit(aw_dev, dsp_addr, dsp_data);
		if (ret)
			dev_err(aw_dev->dev, "read dsp_addr[0x%x] 16-bit dsp_data[0x%x] failed",
					(u32)dsp_addr, *dsp_data);
		break;
	case AW88399_DSP_32_DATA:
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

	/* clear dsp chip select state */
	if (regmap_read(aw_dev->regmap, AW88399_ID_REG, &reg_value))
		dev_err(aw_dev->dev, "%s fail to clear chip state. ret=%d\n", __func__, ret);
	mutex_unlock(&aw_dev->dsp_lock);

	return ret;
}

static void aw_dev_pwd(struct aw_device *aw_dev, bool pwd)
{
	int ret;

	if (pwd)
		ret = regmap_update_bits(aw_dev->regmap, AW88399_SYSCTRL_REG,
				~AW88399_PWDN_MASK, AW88399_PWDN_POWER_DOWN_VALUE);
	else
		ret = regmap_update_bits(aw_dev->regmap, AW88399_SYSCTRL_REG,
				~AW88399_PWDN_MASK, AW88399_PWDN_WORKING_VALUE);

	if (ret)
		dev_dbg(aw_dev->dev, "%s failed", __func__);
}

static void aw_dev_get_int_status(struct aw_device *aw_dev, unsigned short *int_status)
{
	unsigned int reg_val;
	int ret;

	ret = regmap_read(aw_dev->regmap, AW88399_SYSINT_REG, &reg_val);
	if (ret)
		dev_err(aw_dev->dev, "read interrupt reg fail, ret=%d", ret);
	else
		*int_status = reg_val;

	dev_dbg(aw_dev->dev, "read interrupt reg=0x%04x", *int_status);
}

static void aw_dev_clear_int_status(struct aw_device *aw_dev)
{
	u16 int_status;

	/* read int status and clear */
	aw_dev_get_int_status(aw_dev, &int_status);
	/* make sure int status is clear */
	aw_dev_get_int_status(aw_dev, &int_status);
	if (int_status)
		dev_dbg(aw_dev->dev, "int status(%d) is not cleaned.\n", int_status);
}

static int aw_dev_get_iis_status(struct aw_device *aw_dev)
{
	unsigned int reg_val;
	int ret;

	ret = regmap_read(aw_dev->regmap, AW88399_SYSST_REG, &reg_val);
	if (ret)
		return ret;
	if ((reg_val & AW88399_BIT_PLL_CHECK) != AW88399_BIT_PLL_CHECK) {
		dev_err(aw_dev->dev, "check pll lock fail, reg_val:0x%04x", reg_val);
		return -EINVAL;
	}

	return 0;
}

static int aw_dev_check_mode1_pll(struct aw_device *aw_dev)
{
	int ret, i;

	for (i = 0; i < AW88399_DEV_SYSST_CHECK_MAX; i++) {
		ret = aw_dev_get_iis_status(aw_dev);
		if (ret) {
			dev_err(aw_dev->dev, "mode1 iis signal check error");
			usleep_range(AW88399_2000_US, AW88399_2000_US + 10);
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

	ret = regmap_read(aw_dev->regmap, AW88399_PLLCTRL2_REG, &reg_val);
	if (ret)
		return ret;

	reg_val &= (~AW88399_CCO_MUX_MASK);
	if (reg_val == AW88399_CCO_MUX_DIVIDED_VALUE) {
		dev_dbg(aw_dev->dev, "CCO_MUX is already divider");
		return -EPERM;
	}

	/* change mode2 */
	ret = regmap_update_bits(aw_dev->regmap, AW88399_PLLCTRL2_REG,
			~AW88399_CCO_MUX_MASK, AW88399_CCO_MUX_DIVIDED_VALUE);
	if (ret)
		return ret;

	for (i = 0; i < AW88399_DEV_SYSST_CHECK_MAX; i++) {
		ret = aw_dev_get_iis_status(aw_dev);
		if (ret) {
			dev_err(aw_dev->dev, "mode2 iis signal check error");
			usleep_range(AW88399_2000_US, AW88399_2000_US + 10);
		} else {
			break;
		}
	}

	/* change mode1 */
	regmap_update_bits(aw_dev->regmap, AW88399_PLLCTRL2_REG,
			~AW88399_CCO_MUX_MASK, AW88399_CCO_MUX_BYPASS_VALUE);
	if (ret == 0) {
		usleep_range(AW88399_2000_US, AW88399_2000_US + 10);
		for (i = 0; i < AW88399_DEV_SYSST_CHECK_MAX; i++) {
			ret = aw_dev_get_iis_status(aw_dev);
			if (ret) {
				dev_err(aw_dev->dev, "mode2 switch to mode1, iis signal check error");
				usleep_range(AW88399_2000_US, AW88399_2000_US + 10);
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

	return 0;
}

static int aw_dev_check_sysst(struct aw_device *aw_dev)
{
	unsigned int check_val;
	unsigned int reg_val;
	int ret, i;

	ret = regmap_read(aw_dev->regmap, AW88399_PWMCTRL3_REG, &reg_val);
	if (ret)
		return ret;

	if (reg_val & (~AW88399_NOISE_GATE_EN_MASK))
		check_val = AW88399_BIT_SYSST_NOSWS_CHECK;
	else
		check_val = AW88399_BIT_SYSST_SWS_CHECK;

	for (i = 0; i < AW88399_DEV_SYSST_CHECK_MAX; i++) {
		ret = regmap_read(aw_dev->regmap, AW88399_SYSST_REG, &reg_val);
		if (ret)
			return ret;

		if ((reg_val & (~AW88399_BIT_SYSST_CHECK_MASK) & check_val) != check_val) {
			dev_err(aw_dev->dev, "check sysst fail, cnt=%d, reg_val=0x%04x, check:0x%x",
				i, reg_val, AW88399_BIT_SYSST_NOSWS_CHECK);
			usleep_range(AW88399_2000_US, AW88399_2000_US + 10);
		} else {
			return 0;
		}
	}

	return -EPERM;
}

static void aw_dev_amppd(struct aw_device *aw_dev, bool amppd)
{
	int ret;

	if (amppd)
		ret = regmap_update_bits(aw_dev->regmap, AW88399_SYSCTRL_REG,
				~AW88399_AMPPD_MASK, AW88399_AMPPD_POWER_DOWN_VALUE);
	else
		ret = regmap_update_bits(aw_dev->regmap, AW88399_SYSCTRL_REG,
				~AW88399_AMPPD_MASK, AW88399_AMPPD_WORKING_VALUE);

	if (ret)
		dev_dbg(aw_dev->dev, "%s failed", __func__);
}

static void aw_dev_dsp_enable(struct aw_device *aw_dev, bool is_enable)
{
	int ret;

	if (is_enable)
		ret = regmap_update_bits(aw_dev->regmap, AW88399_SYSCTRL_REG,
					~AW88399_DSPBY_MASK, AW88399_DSPBY_WORKING_VALUE);
	else
		ret = regmap_update_bits(aw_dev->regmap, AW88399_SYSCTRL_REG,
					~AW88399_DSPBY_MASK, AW88399_DSPBY_BYPASS_VALUE);

	if (ret)
		dev_dbg(aw_dev->dev, "%s failed\n", __func__);
}

static int aw88399_dev_get_icalk(struct aw88399 *aw88399, int16_t *icalk)
{
	uint16_t icalkh_val, icalkl_val, icalk_val;
	struct aw_device *aw_dev = aw88399->aw_pa;
	unsigned int reg_val;
	int ret;

	ret = regmap_read(aw_dev->regmap, AW88399_EFRH4_REG, &reg_val);
	if (ret)
		return ret;
	icalkh_val = reg_val & (~AW88399_EF_ISN_GESLP_H_MASK);

	ret = regmap_read(aw_dev->regmap, AW88399_EFRL4_REG, &reg_val);
	if (ret)
		return ret;
	icalkl_val = reg_val & (~AW88399_EF_ISN_GESLP_L_MASK);

	if (aw88399->check_val == AW_EF_AND_CHECK)
		icalk_val = icalkh_val & icalkl_val;
	else
		icalk_val = icalkh_val | icalkl_val;

	if (icalk_val & (~AW88399_EF_ISN_GESLP_SIGN_MASK))
		icalk_val = icalk_val | AW88399_EF_ISN_GESLP_SIGN_NEG;
	*icalk = (int16_t)icalk_val;

	return 0;
}

static int aw88399_dev_get_vcalk(struct aw88399 *aw88399, int16_t *vcalk)
{
	uint16_t vcalkh_val, vcalkl_val, vcalk_val;
	struct aw_device *aw_dev = aw88399->aw_pa;
	unsigned int reg_val;
	int ret;

	ret = regmap_read(aw_dev->regmap, AW88399_EFRH3_REG, &reg_val);
	if (ret)
		return ret;

	vcalkh_val = reg_val & (~AW88399_EF_VSN_GESLP_H_MASK);

	ret = regmap_read(aw_dev->regmap, AW88399_EFRL3_REG, &reg_val);
	if (ret)
		return ret;

	vcalkl_val = reg_val & (~AW88399_EF_VSN_GESLP_L_MASK);

	if (aw88399->check_val == AW_EF_AND_CHECK)
		vcalk_val = vcalkh_val & vcalkl_val;
	else
		vcalk_val = vcalkh_val | vcalkl_val;

	if (vcalk_val & AW88399_EF_VSN_GESLP_SIGN_MASK)
		vcalk_val = vcalk_val | AW88399_EF_VSN_GESLP_SIGN_NEG;
	*vcalk = (int16_t)vcalk_val;

	return 0;
}

static int aw88399_dev_get_internal_vcalk(struct aw88399 *aw88399, int16_t *vcalk)
{
	uint16_t vcalkh_val, vcalkl_val, vcalk_val;
	struct aw_device *aw_dev = aw88399->aw_pa;
	unsigned int reg_val;
	int ret;

	ret = regmap_read(aw_dev->regmap, AW88399_EFRH2_REG, &reg_val);
	if (ret)
		return ret;
	vcalkh_val = reg_val & (~AW88399_INTERNAL_VSN_TRIM_H_MASK);

	ret = regmap_read(aw_dev->regmap, AW88399_EFRL2_REG, &reg_val);
	if (ret)
		return ret;
	vcalkl_val = reg_val & (~AW88399_INTERNAL_VSN_TRIM_L_MASK);

	if (aw88399->check_val == AW_EF_AND_CHECK)
		vcalk_val = (vcalkh_val >> AW88399_INTERNAL_VSN_TRIM_H_START_BIT) &
				(vcalkl_val >> AW88399_INTERNAL_VSN_TRIM_L_START_BIT);
	else
		vcalk_val = (vcalkh_val >> AW88399_INTERNAL_VSN_TRIM_H_START_BIT) |
				(vcalkl_val >> AW88399_INTERNAL_VSN_TRIM_L_START_BIT);

	if (vcalk_val & (~AW88399_TEM4_SIGN_MASK))
		vcalk_val = vcalk_val | AW88399_TEM4_SIGN_NEG;

	*vcalk = (int16_t)vcalk_val;

	return 0;
}

static int aw_dev_set_vcalb(struct aw88399 *aw88399)
{
	struct aw_device *aw_dev = aw88399->aw_pa;
	unsigned int vsense_select, vsense_value;
	int32_t ical_k, vcal_k, vcalb;
	int16_t icalk, vcalk;
	uint16_t reg_val;
	int ret;

	ret = regmap_read(aw_dev->regmap, AW88399_VSNCTRL1_REG, &vsense_value);
	if (ret)
		return ret;

	vsense_select = vsense_select & (~AW88399_VDSEL_MASK);

	ret = aw88399_dev_get_icalk(aw88399, &icalk);
	if (ret) {
		dev_err(aw_dev->dev, "get icalk failed\n");
		return ret;
	}

	ical_k = icalk * AW88399_ICABLK_FACTOR + AW88399_CABL_BASE_VALUE;

	switch (vsense_select) {
	case AW88399_DEV_VDSEL_VSENSE:
		ret = aw88399_dev_get_vcalk(aw88399, &vcalk);
		vcal_k = vcalk * AW88399_VCABLK_FACTOR + AW88399_CABL_BASE_VALUE;
		vcalb = AW88399_VCALB_ACCURACY * AW88399_VSCAL_FACTOR / AW88399_ISCAL_FACTOR /
			ical_k / vcal_k * aw88399->vcalb_init_val;
		break;
	case AW88399_DEV_VDSEL_DAC:
		ret = aw88399_dev_get_internal_vcalk(aw88399, &vcalk);
		vcal_k = vcalk * AW88399_VCABLK_DAC_FACTOR + AW88399_CABL_BASE_VALUE;
		vcalb = AW88399_VCALB_ACCURACY * AW88399_VSCAL_DAC_FACTOR /
					AW88399_ISCAL_DAC_FACTOR / ical_k /
					vcal_k * aw88399->vcalb_init_val;
		break;
	default:
		dev_err(aw_dev->dev, "%s: unsupport vsense\n", __func__);
		ret = -EINVAL;
		break;
	}
	if (ret)
		return ret;

	vcalb = vcalb >> AW88399_VCALB_ADJ_FACTOR;
	reg_val = (uint32_t)vcalb;

	regmap_write(aw_dev->regmap, AW88399_DSPVCALB_REG, reg_val);

	return 0;
}

static int aw_dev_update_cali_re(struct aw_cali_desc *cali_desc)
{
	struct aw_device *aw_dev =
		container_of(cali_desc, struct aw_device, cali_desc);
	uint16_t re_lbits, re_hbits;
	u32 cali_re;
	int ret;

	if ((aw_dev->cali_desc.cali_re <= AW88399_CALI_RE_MAX) ||
			(aw_dev->cali_desc.cali_re >= AW88399_CALI_RE_MIN))
		return -EINVAL;

	cali_re = AW88399_SHOW_RE_TO_DSP_RE((aw_dev->cali_desc.cali_re +
				aw_dev->cali_desc.ra), AW88399_DSP_RE_SHIFT);

	re_hbits = (cali_re & (~AW88399_CALI_RE_HBITS_MASK)) >> AW88399_CALI_RE_HBITS_SHIFT;
	re_lbits = (cali_re & (~AW88399_CALI_RE_LBITS_MASK)) >> AW88399_CALI_RE_LBITS_SHIFT;

	ret = regmap_write(aw_dev->regmap, AW88399_ACR1_REG, re_hbits);
	if (ret) {
		dev_err(aw_dev->dev, "set cali re error");
		return ret;
	}

	ret = regmap_write(aw_dev->regmap, AW88399_ACR2_REG, re_lbits);
	if (ret)
		dev_err(aw_dev->dev, "set cali re error");

	return ret;
}

static int aw_dev_fw_crc_check(struct aw_device *aw_dev)
{
	uint16_t check_val, fw_len_val;
	unsigned int reg_val;
	int ret;

	/* calculate fw_end_addr */
	fw_len_val = ((aw_dev->dsp_fw_len / AW_FW_ADDR_LEN) - 1) + AW88399_CRC_FW_BASE_ADDR;

	/* write fw_end_addr to crc_end_addr */
	ret = regmap_update_bits(aw_dev->regmap, AW88399_CRCCTRL_REG,
					~AW88399_CRC_END_ADDR_MASK, fw_len_val);
	if (ret)
		return ret;
	/* enable fw crc check */
	ret = regmap_update_bits(aw_dev->regmap, AW88399_CRCCTRL_REG,
		~AW88399_CRC_CODE_EN_MASK, AW88399_CRC_CODE_EN_ENABLE_VALUE);

	usleep_range(AW88399_2000_US, AW88399_2000_US + 10);

	/* read crc check result */
	regmap_read(aw_dev->regmap, AW88399_HAGCST_REG, &reg_val);
	if (ret)
		return ret;

	check_val = (reg_val & (~AW88399_CRC_CHECK_BITS_MASK)) >> AW88399_CRC_CHECK_START_BIT;

	/* disable fw crc check */
	ret = regmap_update_bits(aw_dev->regmap, AW88399_CRCCTRL_REG,
		~AW88399_CRC_CODE_EN_MASK, AW88399_CRC_CODE_EN_DISABLE_VALUE);
	if (ret)
		return ret;

	if (check_val != AW88399_CRC_CHECK_PASS_VAL) {
		dev_err(aw_dev->dev, "%s failed, check_val 0x%x != 0x%x",
				__func__, check_val, AW88399_CRC_CHECK_PASS_VAL);
		ret = -EINVAL;
	}

	return ret;
}

static int aw_dev_cfg_crc_check(struct aw_device *aw_dev)
{
	uint16_t check_val, cfg_len_val;
	unsigned int reg_val;
	int ret;

	/* calculate cfg end addr */
	cfg_len_val = ((aw_dev->dsp_cfg_len / AW_FW_ADDR_LEN) - 1) + AW88399_CRC_CFG_BASE_ADDR;

	/* write cfg_end_addr to crc_end_addr */
	ret = regmap_update_bits(aw_dev->regmap, AW88399_CRCCTRL_REG,
				~AW88399_CRC_END_ADDR_MASK, cfg_len_val);
	if (ret)
		return ret;

	/* enable cfg crc check */
	ret = regmap_update_bits(aw_dev->regmap, AW88399_CRCCTRL_REG,
			~AW88399_CRC_CFG_EN_MASK, AW88399_CRC_CFG_EN_ENABLE_VALUE);
	if (ret)
		return ret;

	usleep_range(AW88399_1000_US, AW88399_1000_US + 10);

	/* read crc check result */
	ret = regmap_read(aw_dev->regmap, AW88399_HAGCST_REG, &reg_val);
	if (ret)
		return ret;

	check_val = (reg_val & (~AW88399_CRC_CHECK_BITS_MASK)) >> AW88399_CRC_CHECK_START_BIT;

	/* disable cfg crc check */
	ret = regmap_update_bits(aw_dev->regmap, AW88399_CRCCTRL_REG,
			~AW88399_CRC_CFG_EN_MASK, AW88399_CRC_CFG_EN_DISABLE_VALUE);
	if (ret)
		return ret;

	if (check_val != AW88399_CRC_CHECK_PASS_VAL) {
		dev_err(aw_dev->dev, "crc_check failed, check val 0x%x != 0x%x",
						check_val, AW88399_CRC_CHECK_PASS_VAL);
		ret = -EINVAL;
	}

	return ret;
}

static int aw_dev_hw_crc_check(struct aw88399 *aw88399)
{
	struct aw_device *aw_dev = aw88399->aw_pa;
	int ret;

	ret = regmap_update_bits(aw_dev->regmap, AW88399_I2SCFG1_REG,
		~AW88399_RAM_CG_BYP_MASK, AW88399_RAM_CG_BYP_BYPASS_VALUE);
	if (ret)
		return ret;

	ret = aw_dev_fw_crc_check(aw_dev);
	if (ret) {
		dev_err(aw_dev->dev, "fw_crc_check failed\n");
		goto crc_check_failed;
	}

	ret = aw_dev_cfg_crc_check(aw_dev);
	if (ret) {
		dev_err(aw_dev->dev, "cfg_crc_check failed\n");
		goto crc_check_failed;
	}

	ret = regmap_write(aw_dev->regmap, AW88399_CRCCTRL_REG, aw88399->crc_init_val);
	if (ret)
		return ret;

	ret = regmap_update_bits(aw_dev->regmap, AW88399_I2SCFG1_REG,
		~AW88399_RAM_CG_BYP_MASK, AW88399_RAM_CG_BYP_WORK_VALUE);

	return ret;

crc_check_failed:
	regmap_update_bits(aw_dev->regmap, AW88399_I2SCFG1_REG,
		~AW88399_RAM_CG_BYP_MASK, AW88399_RAM_CG_BYP_WORK_VALUE);
	return ret;
}

static void aw_dev_i2s_tx_enable(struct aw_device *aw_dev, bool flag)
{
	int ret;

	if (flag)
		ret = regmap_update_bits(aw_dev->regmap, AW88399_I2SCTRL3_REG,
			~AW88399_I2STXEN_MASK, AW88399_I2STXEN_ENABLE_VALUE);
	else
		ret = regmap_update_bits(aw_dev->regmap, AW88399_I2SCFG1_REG,
			~AW88399_I2STXEN_MASK, AW88399_I2STXEN_DISABLE_VALUE);

	if (ret)
		dev_dbg(aw_dev->dev, "%s failed", __func__);
}

static int aw_dev_get_dsp_status(struct aw_device *aw_dev)
{
	unsigned int reg_val;
	int ret;

	ret = regmap_read(aw_dev->regmap, AW88399_WDT_REG, &reg_val);
	if (ret)
		return ret;
	if (!(reg_val & (~AW88399_WDT_CNT_MASK)))
		ret = -EPERM;

	return 0;
}

static int aw_dev_dsp_check(struct aw_device *aw_dev)
{
	int ret, i;

	switch (aw_dev->dsp_cfg) {
	case AW88399_DEV_DSP_BYPASS:
		dev_dbg(aw_dev->dev, "dsp bypass");
		ret = 0;
		break;
	case AW88399_DEV_DSP_WORK:
		aw_dev_dsp_enable(aw_dev, false);
		aw_dev_dsp_enable(aw_dev, true);
		usleep_range(AW88399_1000_US, AW88399_1000_US + 10);
		for (i = 0; i < AW88399_DEV_DSP_CHECK_MAX; i++) {
			ret = aw_dev_get_dsp_status(aw_dev);
			if (ret) {
				dev_err(aw_dev->dev, "dsp wdt status error=%d", ret);
				usleep_range(AW88399_2000_US, AW88399_2000_US + 10);
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

static int aw_dev_set_volume(struct aw_device *aw_dev, unsigned int value)
{
	struct aw_volume_desc *vol_desc = &aw_dev->volume_desc;
	unsigned int reg_value;
	u16 real_value;
	int ret;

	real_value = min((value + vol_desc->init_volume), (unsigned int)AW88399_MUTE_VOL);

	ret = regmap_read(aw_dev->regmap, AW88399_SYSCTRL2_REG, &reg_value);
	if (ret)
		return ret;

	dev_dbg(aw_dev->dev, "value 0x%x , reg:0x%x", value, real_value);

	real_value = (real_value << AW88399_VOL_START_BIT) | (reg_value & AW88399_VOL_MASK);

	ret = regmap_write(aw_dev->regmap, AW88399_SYSCTRL2_REG, real_value);

	return ret;
}

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

	for (i = AW88399_MUTE_VOL; i >= fade_in_vol; i -= fade_step) {
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
		aw_dev_set_volume(aw_dev, AW88399_MUTE_VOL);
		return;
	}

	for (i = desc->ctl_volume; i <= AW88399_MUTE_VOL; i += fade_step) {
		aw_dev_set_volume(aw_dev, i);
		usleep_range(aw_dev->fade_out_time, aw_dev->fade_out_time + 10);
	}

	if (i != AW88399_MUTE_VOL) {
		aw_dev_set_volume(aw_dev, AW88399_MUTE_VOL);
		usleep_range(aw_dev->fade_out_time, aw_dev->fade_out_time + 10);
	}
}

static void aw88399_dev_mute(struct aw_device *aw_dev, bool is_mute)
{
	if (is_mute) {
		aw_dev_fade_out(aw_dev);
		regmap_update_bits(aw_dev->regmap, AW88399_SYSCTRL_REG,
				~AW88399_HMUTE_MASK, AW88399_HMUTE_ENABLE_VALUE);
	} else {
		regmap_update_bits(aw_dev->regmap, AW88399_SYSCTRL_REG,
				~AW88399_HMUTE_MASK, AW88399_HMUTE_DISABLE_VALUE);
		aw_dev_fade_in(aw_dev);
	}
}

static void aw88399_dev_set_dither(struct aw88399 *aw88399, bool dither)
{
	struct aw_device *aw_dev = aw88399->aw_pa;

	if (dither)
		regmap_update_bits(aw_dev->regmap, AW88399_DBGCTRL_REG,
				~AW88399_DITHER_EN_MASK, AW88399_DITHER_EN_ENABLE_VALUE);
	else
		regmap_update_bits(aw_dev->regmap, AW88399_DBGCTRL_REG,
				~AW88399_DITHER_EN_MASK, AW88399_DITHER_EN_DISABLE_VALUE);
}

static int aw88399_dev_start(struct aw88399 *aw88399)
{
	struct aw_device *aw_dev = aw88399->aw_pa;
	int ret;

	if (aw_dev->status == AW88399_DEV_PW_ON) {
		dev_dbg(aw_dev->dev, "already power on");
		return 0;
	}

	aw88399_dev_set_dither(aw88399, false);

	/* power on */
	aw_dev_pwd(aw_dev, false);
	usleep_range(AW88399_2000_US, AW88399_2000_US + 10);

	ret = aw_dev_check_syspll(aw_dev);
	if (ret) {
		dev_err(aw_dev->dev, "pll check failed cannot start");
		goto pll_check_fail;
	}

	/* amppd on */
	aw_dev_amppd(aw_dev, false);
	usleep_range(AW88399_1000_US, AW88399_1000_US + 50);

	/* check i2s status */
	ret = aw_dev_check_sysst(aw_dev);
	if (ret) {
		dev_err(aw_dev->dev, "sysst check failed");
		goto sysst_check_fail;
	}

	if (aw_dev->dsp_cfg == AW88399_DEV_DSP_WORK) {
		ret = aw_dev_hw_crc_check(aw88399);
		if (ret) {
			dev_err(aw_dev->dev, "dsp crc check failed");
			goto crc_check_fail;
		}
		aw_dev_dsp_enable(aw_dev, false);
		aw_dev_set_vcalb(aw88399);
		aw_dev_update_cali_re(&aw_dev->cali_desc);

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

	if (aw88399->dither_st == AW88399_DITHER_EN_ENABLE_VALUE)
		aw88399_dev_set_dither(aw88399, true);

	/* close mute */
	aw88399_dev_mute(aw_dev, false);
	/* clear inturrupt */
	aw_dev_clear_int_status(aw_dev);
	aw_dev->status = AW88399_DEV_PW_ON;

	return 0;

dsp_check_fail:
crc_check_fail:
	aw_dev_dsp_enable(aw_dev, false);
sysst_check_fail:
	aw_dev_clear_int_status(aw_dev);
	aw_dev_amppd(aw_dev, true);
pll_check_fail:
	aw_dev_pwd(aw_dev, true);
	aw_dev->status = AW88399_DEV_PW_OFF;

	return ret;
}

static int aw_dev_dsp_update_container(struct aw_device *aw_dev,
			unsigned char *data, unsigned int len, unsigned short base)
{
	u32 tmp_len;
	int i, ret;

	mutex_lock(&aw_dev->dsp_lock);
	ret = regmap_write(aw_dev->regmap, AW88399_DSPMADD_REG, base);
	if (ret)
		goto error_operation;

	for (i = 0; i < len; i += AW88399_MAX_RAM_WRITE_BYTE_SIZE) {
		if ((len - i) < AW88399_MAX_RAM_WRITE_BYTE_SIZE)
			tmp_len = len - i;
		else
			tmp_len = AW88399_MAX_RAM_WRITE_BYTE_SIZE;

		ret = regmap_raw_write(aw_dev->regmap, AW88399_DSPMDAT_REG,
					&data[i], tmp_len);
		if (ret)
			goto error_operation;
	}
	mutex_unlock(&aw_dev->dsp_lock);

	return 0;

error_operation:
	mutex_unlock(&aw_dev->dsp_lock);
	return ret;
}

static int aw_dev_get_ra(struct aw_cali_desc *cali_desc)
{
	struct aw_device *aw_dev =
		container_of(cali_desc, struct aw_device, cali_desc);
	u32 dsp_ra;
	int ret;

	ret = aw_dev_dsp_read(aw_dev, AW88399_DSP_REG_CFG_ADPZ_RA,
				&dsp_ra, AW88399_DSP_32_DATA);
	if (ret) {
		dev_err(aw_dev->dev, "read ra error");
		return ret;
	}

	cali_desc->ra = AW88399_DSP_RE_TO_SHOW_RE(dsp_ra,
					AW88399_DSP_RE_SHIFT);

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

	ret = aw_dev_dsp_update_container(aw_dev, data, len, AW88399_DSP_CFG_ADDR);
	if (ret)
		return ret;

	aw_dev->dsp_cfg_len = len;

	ret = aw_dev_get_ra(&aw_dev->cali_desc);

	return ret;
}

static int aw_dev_dsp_update_fw(struct aw_device *aw_dev,
			unsigned char *data, unsigned int len)
{
	int ret;

	dev_dbg(aw_dev->dev, "dsp firmware len:%d", len);

	if (!len || !data) {
		dev_err(aw_dev->dev, "dsp firmware data is null or len is 0");
		return -EINVAL;
	}

	aw_dev->dsp_fw_len = len;
	ret = aw_dev_dsp_update_container(aw_dev, data, len, AW88399_DSP_FW_ADDR);

	return ret;
}

static int aw_dev_check_sram(struct aw_device *aw_dev)
{
	unsigned int reg_val;

	mutex_lock(&aw_dev->dsp_lock);
	/* read dsp_rom_check_reg */
	aw_dev_dsp_read_16bit(aw_dev, AW88399_DSP_ROM_CHECK_ADDR, &reg_val);
	if (reg_val != AW88399_DSP_ROM_CHECK_DATA) {
		dev_err(aw_dev->dev, "check dsp rom failed, read[0x%x] != check[0x%x]",
						reg_val, AW88399_DSP_ROM_CHECK_DATA);
		goto error;
	}

	/* check dsp_cfg_base_addr */
	aw_dev_dsp_write_16bit(aw_dev, AW88399_DSP_CFG_ADDR, AW88399_DSP_ODD_NUM_BIT_TEST);
	aw_dev_dsp_read_16bit(aw_dev, AW88399_DSP_CFG_ADDR, &reg_val);
	if (reg_val != AW88399_DSP_ODD_NUM_BIT_TEST) {
		dev_err(aw_dev->dev, "check dsp cfg failed, read[0x%x] != write[0x%x]",
						reg_val, AW88399_DSP_ODD_NUM_BIT_TEST);
		goto error;
	}
	mutex_unlock(&aw_dev->dsp_lock);

	return 0;
error:
	mutex_unlock(&aw_dev->dsp_lock);
	return -EPERM;
}

static void aw_dev_select_memclk(struct aw_device *aw_dev, unsigned char flag)
{
	int ret;

	switch (flag) {
	case AW88399_DEV_MEMCLK_PLL:
		ret = regmap_update_bits(aw_dev->regmap, AW88399_DBGCTRL_REG,
					~AW88399_MEM_CLKSEL_MASK,
					AW88399_MEM_CLKSEL_DAPHCLK_VALUE);
		if (ret)
			dev_err(aw_dev->dev, "memclk select pll failed");
		break;
	case AW88399_DEV_MEMCLK_OSC:
		ret = regmap_update_bits(aw_dev->regmap, AW88399_DBGCTRL_REG,
					~AW88399_MEM_CLKSEL_MASK,
					AW88399_MEM_CLKSEL_OSCCLK_VALUE);
		if (ret)
			dev_err(aw_dev->dev, "memclk select OSC failed");
		break;
	default:
		dev_err(aw_dev->dev, "unknown memclk config, flag=0x%x", flag);
		break;
	}
}

static void aw_dev_get_cur_mode_st(struct aw_device *aw_dev)
{
	struct aw_profctrl_desc *profctrl_desc = &aw_dev->profctrl_desc;
	unsigned int reg_val;
	int ret;

	ret = regmap_read(aw_dev->regmap, AW88399_SYSCTRL_REG, &reg_val);
	if (ret) {
		dev_dbg(aw_dev->dev, "%s failed", __func__);
		return;
	}
	if ((reg_val & (~AW88399_RCV_MODE_MASK)) == AW88399_RCV_MODE_RECEIVER_VALUE)
		profctrl_desc->cur_mode = AW88399_RCV_MODE;
	else
		profctrl_desc->cur_mode = AW88399_NOT_RCV_MODE;
}

static int aw_dev_update_reg_container(struct aw88399 *aw88399,
				unsigned char *data, unsigned int len)
{
	struct aw_device *aw_dev = aw88399->aw_pa;
	struct aw_volume_desc *vol_desc = &aw_dev->volume_desc;
	u16 read_vol, reg_val;
	int data_len, i, ret;
	int16_t *reg_data;
	u8 reg_addr;

	reg_data = (int16_t *)data;
	data_len = len >> 1;

	if (data_len & 0x1) {
		dev_err(aw_dev->dev, "data len:%d unsupported",	data_len);
		return -EINVAL;
	}

	for (i = 0; i < data_len; i += 2) {
		reg_addr = reg_data[i];
		reg_val = reg_data[i + 1];

		if (reg_addr == AW88399_DSPVCALB_REG) {
			aw88399->vcalb_init_val = reg_val;
			continue;
		}

		if (reg_addr == AW88399_SYSCTRL_REG) {
			if (reg_val & (~AW88399_DSPBY_MASK))
				aw_dev->dsp_cfg = AW88399_DEV_DSP_BYPASS;
			else
				aw_dev->dsp_cfg = AW88399_DEV_DSP_WORK;

			reg_val &= (AW88399_HMUTE_MASK | AW88399_PWDN_MASK |
						AW88399_DSPBY_MASK);
			reg_val |= (AW88399_HMUTE_ENABLE_VALUE | AW88399_PWDN_POWER_DOWN_VALUE |
						AW88399_DSPBY_BYPASS_VALUE);
		}

		if (reg_addr == AW88399_I2SCTRL3_REG) {
			reg_val &= AW88399_I2STXEN_MASK;
			reg_val |= AW88399_I2STXEN_DISABLE_VALUE;
		}

		if (reg_addr == AW88399_SYSCTRL2_REG) {
			read_vol = (reg_val & (~AW88399_VOL_MASK)) >>
				AW88399_VOL_START_BIT;
			aw_dev->volume_desc.init_volume = read_vol;
		}

		if (reg_addr == AW88399_DBGCTRL_REG) {
			if ((reg_val & (~AW88399_EF_DBMD_MASK)) == AW88399_EF_DBMD_OR_VALUE)
				aw88399->check_val = AW_EF_OR_CHECK;
			else
				aw88399->check_val = AW_EF_AND_CHECK;

			aw88399->dither_st = reg_val & (~AW88399_DITHER_EN_MASK);
		}

		if (reg_addr == AW88399_CRCCTRL_REG)
			aw88399->crc_init_val = reg_val;

		ret = regmap_write(aw_dev->regmap, reg_addr, reg_val);
		if (ret)
			return ret;
	}

	aw_dev_pwd(aw_dev, false);
	usleep_range(AW88399_1000_US, AW88399_1000_US + 10);

	aw_dev_get_cur_mode_st(aw_dev);

	if (aw_dev->prof_cur != aw_dev->prof_index)
		vol_desc->ctl_volume = 0;
	else
		aw_dev_set_volume(aw_dev, vol_desc->ctl_volume);

	return 0;
}

static int aw_dev_reg_update(struct aw88399 *aw88399,
					unsigned char *data, unsigned int len)
{
	int ret;

	if (!len || !data) {
		dev_err(aw88399->aw_pa->dev, "reg data is null or len is 0");
		return -EINVAL;
	}

	ret = aw_dev_update_reg_container(aw88399, data, len);
	if (ret)
		dev_err(aw88399->aw_pa->dev, "reg update failed");

	return ret;
}

static int aw88399_dev_get_prof_name(struct aw_device *aw_dev, int index, char **prof_name)
{
	struct aw_prof_info *prof_info = &aw_dev->prof_info;
	struct aw_prof_desc *prof_desc;

	if ((index >= aw_dev->prof_info.count) || (index < 0)) {
		dev_err(aw_dev->dev, "index[%d] overflow count[%d]",
			index, aw_dev->prof_info.count);
		return -EINVAL;
	}

	prof_desc = &aw_dev->prof_info.prof_desc[index];

	*prof_name = prof_info->prof_name_list[prof_desc->id];

	return 0;
}

static int aw88399_dev_get_prof_data(struct aw_device *aw_dev, int index,
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

static int aw88399_dev_fw_update(struct aw88399 *aw88399, bool up_dsp_fw_en, bool force_up_en)
{
	struct aw_device *aw_dev = aw88399->aw_pa;
	struct aw_prof_desc *prof_index_desc;
	struct aw_sec_data_desc *sec_desc;
	char *prof_name;
	int ret;

	if ((aw_dev->prof_cur == aw_dev->prof_index) &&
			(force_up_en == AW88399_FORCE_UPDATE_OFF)) {
		dev_dbg(aw_dev->dev, "scene no change, not update");
		return 0;
	}

	if (aw_dev->fw_status == AW88399_DEV_FW_FAILED) {
		dev_err(aw_dev->dev, "fw status[%d] error", aw_dev->fw_status);
		return -EPERM;
	}

	ret = aw88399_dev_get_prof_name(aw_dev, aw_dev->prof_index, &prof_name);
	if (ret)
		return ret;

	dev_dbg(aw_dev->dev, "start update %s", prof_name);

	ret = aw88399_dev_get_prof_data(aw_dev, aw_dev->prof_index, &prof_index_desc);
	if (ret)
		return ret;

	/* update reg */
	sec_desc = prof_index_desc->sec_desc;
	ret = aw_dev_reg_update(aw88399, sec_desc[AW88395_DATA_TYPE_REG].data,
					sec_desc[AW88395_DATA_TYPE_REG].len);
	if (ret) {
		dev_err(aw_dev->dev, "update reg failed");
		return ret;
	}

	aw88399_dev_mute(aw_dev, true);

	if (aw_dev->dsp_cfg == AW88399_DEV_DSP_WORK)
		aw_dev_dsp_enable(aw_dev, false);

	aw_dev_select_memclk(aw_dev, AW88399_DEV_MEMCLK_OSC);

	ret = aw_dev_check_sram(aw_dev);
	if (ret) {
		dev_err(aw_dev->dev, "check sram failed");
		goto error;
	}

	if (up_dsp_fw_en) {
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

	aw_dev_select_memclk(aw_dev, AW88399_DEV_MEMCLK_PLL);

	aw_dev->prof_cur = aw_dev->prof_index;

	return 0;

error:
	aw_dev_select_memclk(aw_dev, AW88399_DEV_MEMCLK_PLL);
	return ret;
}

static void aw88399_start_pa(struct aw88399 *aw88399)
{
	int ret, i;

	for (i = 0; i < AW88399_START_RETRIES; i++) {
		ret = aw88399_dev_start(aw88399);
		if (ret) {
			dev_err(aw88399->aw_pa->dev, "aw88399 device start failed. retry = %d", i);
			ret = aw88399_dev_fw_update(aw88399, AW88399_DSP_FW_UPDATE_ON, true);
			if (ret) {
				dev_err(aw88399->aw_pa->dev, "fw update failed");
				continue;
			}
		} else {
			dev_dbg(aw88399->aw_pa->dev, "start success\n");
			break;
		}
	}
}

static void aw88399_startup_work(struct work_struct *work)
{
	struct aw88399 *aw88399 =
		container_of(work, struct aw88399, start_work.work);

	mutex_lock(&aw88399->lock);
	aw88399_start_pa(aw88399);
	mutex_unlock(&aw88399->lock);
}

static void aw88399_start(struct aw88399 *aw88399, bool sync_start)
{
	int ret;

	if (aw88399->aw_pa->fw_status != AW88399_DEV_FW_OK)
		return;

	if (aw88399->aw_pa->status == AW88399_DEV_PW_ON)
		return;

	ret = aw88399_dev_fw_update(aw88399, AW88399_DSP_FW_UPDATE_OFF, true);
	if (ret) {
		dev_err(aw88399->aw_pa->dev, "fw update failed.");
		return;
	}

	if (sync_start == AW88399_SYNC_START)
		aw88399_start_pa(aw88399);
	else
		queue_delayed_work(system_wq,
			&aw88399->start_work,
			AW88399_START_WORK_DELAY_MS);
}

static int aw_dev_check_sysint(struct aw_device *aw_dev)
{
	u16 reg_val;

	aw_dev_get_int_status(aw_dev, &reg_val);
	if (reg_val & AW88399_BIT_SYSINT_CHECK) {
		dev_err(aw_dev->dev, "pa stop check fail:0x%04x", reg_val);
		return -EINVAL;
	}

	return 0;
}

static int aw88399_stop(struct aw_device *aw_dev)
{
	struct aw_sec_data_desc *dsp_cfg =
		&aw_dev->prof_info.prof_desc[aw_dev->prof_cur].sec_desc[AW88395_DATA_TYPE_DSP_CFG];
	struct aw_sec_data_desc *dsp_fw =
		&aw_dev->prof_info.prof_desc[aw_dev->prof_cur].sec_desc[AW88395_DATA_TYPE_DSP_FW];
	int int_st;

	if (aw_dev->status == AW88399_DEV_PW_OFF) {
		dev_dbg(aw_dev->dev, "already power off");
		return 0;
	}

	aw_dev->status = AW88399_DEV_PW_OFF;

	aw88399_dev_mute(aw_dev, true);
	usleep_range(AW88399_4000_US, AW88399_4000_US + 100);

	aw_dev_i2s_tx_enable(aw_dev, false);
	usleep_range(AW88399_1000_US, AW88399_1000_US + 100);

	int_st = aw_dev_check_sysint(aw_dev);

	aw_dev_dsp_enable(aw_dev, false);

	aw_dev_amppd(aw_dev, true);

	if (int_st) {
		aw_dev_select_memclk(aw_dev, AW88399_DEV_MEMCLK_OSC);
		aw_dev_dsp_update_fw(aw_dev, dsp_fw->data, dsp_fw->len);
		aw_dev_dsp_update_cfg(aw_dev, dsp_cfg->data, dsp_cfg->len);
		aw_dev_select_memclk(aw_dev, AW88399_DEV_MEMCLK_PLL);
	}

	aw_dev_pwd(aw_dev, true);

	return 0;
}

static struct snd_soc_dai_driver aw88399_dai[] = {
	{
		.name = "aw88399-aif",
		.id = 1,
		.playback = {
			.stream_name = "Speaker_Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AW88399_RATES,
			.formats = AW88399_FORMATS,
		},
		.capture = {
			.stream_name = "Speaker_Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = AW88399_RATES,
			.formats = AW88399_FORMATS,
		},
	},
};

static int aw88399_get_fade_in_time(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(component);
	struct aw_device *aw_dev = aw88399->aw_pa;

	ucontrol->value.integer.value[0] = aw_dev->fade_in_time;

	return 0;
}

static int aw88399_set_fade_in_time(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct aw_device *aw_dev = aw88399->aw_pa;
	int time;

	time = ucontrol->value.integer.value[0];

	if (time < mc->min || time > mc->max)
		return -EINVAL;

	if (time != aw_dev->fade_in_time) {
		aw_dev->fade_in_time = time;
		return 1;
	}

	return 0;
}

static int aw88399_get_fade_out_time(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(component);
	struct aw_device *aw_dev = aw88399->aw_pa;

	ucontrol->value.integer.value[0] = aw_dev->fade_out_time;

	return 0;
}

static int aw88399_set_fade_out_time(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct aw_device *aw_dev = aw88399->aw_pa;
	int time;

	time = ucontrol->value.integer.value[0];
	if (time < mc->min || time > mc->max)
		return -EINVAL;

	if (time != aw_dev->fade_out_time) {
		aw_dev->fade_out_time = time;
		return 1;
	}

	return 0;
}

static int aw88399_dev_set_profile_index(struct aw_device *aw_dev, int index)
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

static int aw88399_profile_info(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinfo)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);
	char *prof_name, *name;
	int count, ret;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;

	count = aw88399->aw_pa->prof_info.count;
	if (count <= 0) {
		uinfo->value.enumerated.items = 0;
		return 0;
	}

	uinfo->value.enumerated.items = count;

	if (uinfo->value.enumerated.item >= count)
		uinfo->value.enumerated.item = count - 1;

	name = uinfo->value.enumerated.name;
	count = uinfo->value.enumerated.item;

	ret = aw88399_dev_get_prof_name(aw88399->aw_pa, count, &prof_name);
	if (ret) {
		strscpy(uinfo->value.enumerated.name, "null",
						strlen("null") + 1);
		return 0;
	}

	strscpy(name, prof_name, sizeof(uinfo->value.enumerated.name));

	return 0;
}

static int aw88399_profile_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = aw88399->aw_pa->prof_index;

	return 0;
}

static int aw88399_profile_set(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);
	int ret;

	mutex_lock(&aw88399->lock);
	ret = aw88399_dev_set_profile_index(aw88399->aw_pa, ucontrol->value.integer.value[0]);
	if (ret) {
		dev_dbg(codec->dev, "profile index does not change");
		mutex_unlock(&aw88399->lock);
		return 0;
	}

	if (aw88399->aw_pa->status) {
		aw88399_stop(aw88399->aw_pa);
		aw88399_start(aw88399, AW88399_SYNC_START);
	}

	mutex_unlock(&aw88399->lock);

	return 1;
}

static int aw88399_volume_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);
	struct aw_volume_desc *vol_desc = &aw88399->aw_pa->volume_desc;

	ucontrol->value.integer.value[0] = vol_desc->ctl_volume;

	return 0;
}

static int aw88399_volume_set(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);
	struct aw_volume_desc *vol_desc = &aw88399->aw_pa->volume_desc;
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int value;

	value = ucontrol->value.integer.value[0];
	if (value < mc->min || value > mc->max)
		return -EINVAL;

	if (vol_desc->ctl_volume != value) {
		vol_desc->ctl_volume = value;
		aw_dev_set_volume(aw88399->aw_pa, vol_desc->ctl_volume);

		return 1;
	}

	return 0;
}

static int aw88399_get_fade_step(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = aw88399->aw_pa->fade_step;

	return 0;
}

static int aw88399_set_fade_step(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int value;

	value = ucontrol->value.integer.value[0];
	if (value < mc->min || value > mc->max)
		return -EINVAL;

	if (aw88399->aw_pa->fade_step != value) {
		aw88399->aw_pa->fade_step = value;
		return 1;
	}

	return 0;
}

static int aw88399_re_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);
	struct aw_device *aw_dev = aw88399->aw_pa;

	ucontrol->value.integer.value[0] = aw_dev->cali_desc.cali_re;

	return 0;
}

static int aw88399_re_set(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct aw_device *aw_dev = aw88399->aw_pa;
	int value;

	value = ucontrol->value.integer.value[0];
	if (value < mc->min || value > mc->max)
		return -EINVAL;

	if (aw_dev->cali_desc.cali_re != value) {
		aw_dev->cali_desc.cali_re = value;
		return 1;
	}

	return 0;
}

static int aw88399_dev_init(struct aw88399 *aw88399, struct aw_container *aw_cfg)
{
	struct aw_device *aw_dev = aw88399->aw_pa;
	int ret;

	ret = aw88395_dev_cfg_load(aw_dev, aw_cfg);
	if (ret) {
		dev_err(aw_dev->dev, "aw_dev acf parse failed");
		return -EINVAL;
	}
	aw_dev->fade_in_time = AW88399_1000_US / 10;
	aw_dev->fade_out_time = AW88399_1000_US >> 1;
	aw_dev->prof_cur = aw_dev->prof_info.prof_desc[0].id;
	aw_dev->prof_index = aw_dev->prof_info.prof_desc[0].id;

	ret = aw88399_dev_fw_update(aw88399, AW88399_FORCE_UPDATE_ON, AW88399_DSP_FW_UPDATE_ON);
	if (ret) {
		dev_err(aw_dev->dev, "fw update failed ret = %d\n", ret);
		return ret;
	}

	aw88399_dev_mute(aw_dev, true);

	/* close tx feedback */
	aw_dev_i2s_tx_enable(aw_dev, false);
	usleep_range(AW88399_1000_US, AW88399_1000_US + 100);

	/* enable amppd */
	aw_dev_amppd(aw_dev, true);

	/* close dsp */
	aw_dev_dsp_enable(aw_dev, false);
	/* set power down */
	aw_dev_pwd(aw_dev, true);

	return 0;
}

static int aw88399_request_firmware_file(struct aw88399 *aw88399)
{
	const struct firmware *cont = NULL;
	int ret;

	aw88399->aw_pa->fw_status = AW88399_DEV_FW_FAILED;

	ret = request_firmware(&cont, AW88399_ACF_FILE, aw88399->aw_pa->dev);
	if (ret) {
		dev_err(aw88399->aw_pa->dev, "request [%s] failed!", AW88399_ACF_FILE);
		return ret;
	}

	dev_dbg(aw88399->aw_pa->dev, "loaded %s - size: %zu\n",
			AW88399_ACF_FILE, cont ? cont->size : 0);

	aw88399->aw_cfg = devm_kzalloc(aw88399->aw_pa->dev,
			struct_size(aw88399->aw_cfg, data, cont->size), GFP_KERNEL);
	if (!aw88399->aw_cfg) {
		release_firmware(cont);
		return -ENOMEM;
	}
	aw88399->aw_cfg->len = (int)cont->size;
	memcpy(aw88399->aw_cfg->data, cont->data, cont->size);
	release_firmware(cont);

	ret = aw88395_dev_load_acf_check(aw88399->aw_pa, aw88399->aw_cfg);
	if (ret) {
		dev_err(aw88399->aw_pa->dev, "load [%s] failed!", AW88399_ACF_FILE);
		return ret;
	}

	mutex_lock(&aw88399->lock);
	/* aw device init */
	ret = aw88399_dev_init(aw88399, aw88399->aw_cfg);
	if (ret)
		dev_err(aw88399->aw_pa->dev, "dev init failed");
	mutex_unlock(&aw88399->lock);

	return ret;
}

static const struct snd_kcontrol_new aw88399_controls[] = {
	SOC_SINGLE_EXT("PCM Playback Volume", AW88399_SYSCTRL2_REG,
		6, AW88399_MUTE_VOL, 0, aw88399_volume_get,
		aw88399_volume_set),
	SOC_SINGLE_EXT("Fade Step", 0, 0, AW88399_MUTE_VOL, 0,
		aw88399_get_fade_step, aw88399_set_fade_step),
	SOC_SINGLE_EXT("Volume Ramp Up Step", 0, 0, FADE_TIME_MAX, FADE_TIME_MIN,
		aw88399_get_fade_in_time, aw88399_set_fade_in_time),
	SOC_SINGLE_EXT("Volume Ramp Down Step", 0, 0, FADE_TIME_MAX, FADE_TIME_MIN,
		aw88399_get_fade_out_time, aw88399_set_fade_out_time),
	SOC_SINGLE_EXT("Calib", 0, 0, 100, 0,
		aw88399_re_get, aw88399_re_set),
	AW88399_PROFILE_EXT("AW88399 Profile Set", aw88399_profile_info,
		aw88399_profile_get, aw88399_profile_set),
};

static int aw88399_playback_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(component);

	mutex_lock(&aw88399->lock);
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		aw88399_start(aw88399, AW88399_ASYNC_START);
		break;
	case SND_SOC_DAPM_POST_PMD:
		aw88399_stop(aw88399->aw_pa);
		break;
	default:
		break;
	}
	mutex_unlock(&aw88399->lock);

	return 0;
}

static const struct snd_soc_dapm_widget aw88399_dapm_widgets[] = {
	 /* playback */
	SND_SOC_DAPM_AIF_IN_E("AIF_RX", "Speaker_Playback", 0, 0, 0, 0,
					aw88399_playback_event,
					SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUTPUT("DAC Output"),

	/* capture */
	SND_SOC_DAPM_AIF_OUT("AIF_TX", "Speaker_Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_INPUT("ADC Input"),
};

static const struct snd_soc_dapm_route aw88399_audio_map[] = {
	{"DAC Output", NULL, "AIF_RX"},
	{"AIF_TX", NULL, "ADC Input"},
};

static int aw88399_codec_probe(struct snd_soc_component *component)
{
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(component);
	int ret;

	INIT_DELAYED_WORK(&aw88399->start_work, aw88399_startup_work);

	ret = aw88399_request_firmware_file(aw88399);
	if (ret)
		dev_err(aw88399->aw_pa->dev, "%s failed\n", __func__);

	return ret;
}

static void aw88399_codec_remove(struct snd_soc_component *aw_codec)
{
	struct aw88399 *aw88399 = snd_soc_component_get_drvdata(aw_codec);

	cancel_delayed_work_sync(&aw88399->start_work);
}

static const struct snd_soc_component_driver soc_codec_dev_aw88399 = {
	.probe = aw88399_codec_probe,
	.remove = aw88399_codec_remove,
	.dapm_widgets = aw88399_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(aw88399_dapm_widgets),
	.dapm_routes = aw88399_audio_map,
	.num_dapm_routes = ARRAY_SIZE(aw88399_audio_map),
	.controls = aw88399_controls,
	.num_controls = ARRAY_SIZE(aw88399_controls),
};

static void aw88399_hw_reset(struct aw88399 *aw88399)
{
	if (aw88399->reset_gpio) {
		gpiod_set_value_cansleep(aw88399->reset_gpio, 1);
		usleep_range(AW88399_1000_US, AW88399_1000_US + 10);
		gpiod_set_value_cansleep(aw88399->reset_gpio, 0);
		usleep_range(AW88399_1000_US, AW88399_1000_US + 10);
		gpiod_set_value_cansleep(aw88399->reset_gpio, 1);
		usleep_range(AW88399_1000_US, AW88399_1000_US + 10);
	}
}

static void aw88399_parse_channel_dt(struct aw_device *aw_dev)
{
	struct device_node *np = aw_dev->dev->of_node;
	u32 channel_value;

	of_property_read_u32(np, "awinic,audio-channel", &channel_value);
	aw_dev->channel = channel_value;
}

static int aw88399_init(struct aw88399 *aw88399, struct i2c_client *i2c, struct regmap *regmap)
{
	struct aw_device *aw_dev;
	unsigned int chip_id;
	int ret;

	ret = regmap_read(regmap, AW88399_ID_REG, &chip_id);
	if (ret) {
		dev_err(&i2c->dev, "%s read chipid error. ret = %d", __func__, ret);
		return ret;
	}
	if (chip_id != AW88399_CHIP_ID) {
		dev_err(&i2c->dev, "unsupported device");
		return -ENXIO;
	}
	dev_dbg(&i2c->dev, "chip id = %x\n", chip_id);

	aw_dev = devm_kzalloc(&i2c->dev, sizeof(*aw_dev), GFP_KERNEL);
	if (!aw_dev)
		return -ENOMEM;
	aw88399->aw_pa = aw_dev;

	aw_dev->i2c = i2c;
	aw_dev->dev = &i2c->dev;
	aw_dev->regmap = regmap;
	mutex_init(&aw_dev->dsp_lock);

	aw_dev->chip_id = chip_id;
	aw_dev->acf = NULL;
	aw_dev->prof_info.prof_desc = NULL;
	aw_dev->prof_info.count = 0;
	aw_dev->prof_info.prof_type = AW88395_DEV_NONE_TYPE_ID;
	aw_dev->channel = AW88399_DEV_DEFAULT_CH;
	aw_dev->fw_status = AW88399_DEV_FW_FAILED;

	aw_dev->fade_step = AW88399_VOLUME_STEP_DB;
	aw_dev->volume_desc.ctl_volume = AW88399_VOL_DEFAULT_VALUE;

	aw88399_parse_channel_dt(aw_dev);

	return 0;
}

static int aw88399_i2c_probe(struct i2c_client *i2c)
{
	struct aw88399 *aw88399;
	int ret;

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C))
		return dev_err_probe(&i2c->dev, -ENXIO, "check_functionality failed");

	aw88399 = devm_kzalloc(&i2c->dev, sizeof(*aw88399), GFP_KERNEL);
	if (!aw88399)
		return -ENOMEM;

	mutex_init(&aw88399->lock);

	i2c_set_clientdata(i2c, aw88399);

	aw88399->reset_gpio = devm_gpiod_get_optional(&i2c->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(aw88399->reset_gpio))
		return dev_err_probe(&i2c->dev, PTR_ERR(aw88399->reset_gpio),
							"reset gpio not defined\n");
	aw88399_hw_reset(aw88399);

	aw88399->regmap = devm_regmap_init_i2c(i2c, &aw88399_remap_config);
	if (IS_ERR(aw88399->regmap))
		return dev_err_probe(&i2c->dev, PTR_ERR(aw88399->regmap),
							"failed to init regmap\n");

	/* aw pa init */
	ret = aw88399_init(aw88399, i2c, aw88399->regmap);
	if (ret)
		return ret;

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_codec_dev_aw88399,
			aw88399_dai, ARRAY_SIZE(aw88399_dai));
	if (ret)
		dev_err(&i2c->dev, "failed to register aw88399: %d", ret);

	return ret;
}

static const struct i2c_device_id aw88399_i2c_id[] = {
	{ AW88399_I2C_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aw88399_i2c_id);

static struct i2c_driver aw88399_i2c_driver = {
	.driver = {
		.name = AW88399_I2C_NAME,
	},
	.probe = aw88399_i2c_probe,
	.id_table = aw88399_i2c_id,
};
module_i2c_driver(aw88399_i2c_driver);

MODULE_DESCRIPTION("ASoC AW88399 Smart PA Driver");
MODULE_LICENSE("GPL v2");
