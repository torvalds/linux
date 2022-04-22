// SPDX-License-Identifier: GPL-2.0+

#include <linux/module.h>
#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <linux/syscalls.h>
#include <sound/control.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>

#include "aw883xx.h"
#include "aw_bin_parse.h"
#include "aw_pid_2049_reg.h"
#include "aw_log.h"

#define AW_FW_CHECK_PART		(10)

static int aw883xx_dev_i2c_writes(struct aw_device *aw_dev,
		uint8_t reg_addr, uint8_t *buf, uint16_t len)
{
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev->private_data;

	return aw883xx_i2c_writes(aw883xx, reg_addr, buf, len);
}

static int aw883xx_dev_i2c_write(struct aw_device *aw_dev,
		uint8_t reg_addr, uint16_t reg_data)
{
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev->private_data;

	return aw883xx_i2c_write(aw883xx, reg_addr, reg_data);
}

static int aw883xx_dev_i2c_read(struct aw_device *aw_dev,
			uint8_t reg_addr, uint16_t *reg_data)
{
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev->private_data;

	return aw883xx_i2c_read(aw883xx, reg_addr, reg_data);
}


static int aw883xx_dev_reg_read(struct aw_device *aw_dev,
			uint8_t reg_addr, uint16_t *reg_data)
{
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev->private_data;

	return aw883xx_reg_read(aw883xx, reg_addr, reg_data);
}

static int aw883xx_dev_reg_write(struct aw_device *aw_dev,
			uint8_t reg_addr, uint16_t reg_data)
{
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev->private_data;

	return aw883xx_reg_write(aw883xx, reg_addr, reg_data);
}

static int aw883xx_dev_reg_write_bits(struct aw_device *aw_dev,
			uint8_t reg_addr, uint16_t mask, uint16_t reg_data)
{
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev->private_data;

	return aw883xx_reg_write_bits(aw883xx, reg_addr, mask, reg_data);
}

static int aw883xx_dev_dsp_write(struct aw_device *aw_dev,
			uint16_t dsp_addr, uint32_t dsp_data, uint8_t data_type)
{
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev->private_data;

	return aw883xx_dsp_write(aw883xx, dsp_addr, dsp_data, data_type);
}

static int aw883xx_dev_dsp_read(struct aw_device *aw_dev,
			uint16_t dsp_addr, uint32_t *dsp_data, uint8_t data_type)
{
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev->private_data;

	return aw883xx_dsp_read(aw883xx, dsp_addr, dsp_data, data_type);
}


/******************************************************
 *
 * aw883xx i2c write/read
 *
 ******************************************************/
/*[9 : 6]: -6DB ; [5 : 0]: -0.125DB  real_value = value * 8 : 0.125db --> 1*/
static unsigned int aw_pid_2049_reg_val_to_db(unsigned int value)
{
	return (((value >> AW_PID_2049_VOL_6DB_START) * AW_PID_2049_VOLUME_STEP_DB) +
			((value & 0x3f) % AW_PID_2049_VOLUME_STEP_DB));
}

/*[9 : 6]: -6DB ; [5 : 0]: -0.125DB reg_value = value / step << 6 + value % step ; step = 6 * 8*/
static uint16_t aw883xx_db_val_to_reg(uint16_t value)
{
	return (((value / AW_PID_2049_VOLUME_STEP_DB) << AW_PID_2049_VOL_6DB_START) +
			(value % AW_PID_2049_VOLUME_STEP_DB));
}

static int aw883xx_set_volume(struct aw883xx *aw883xx, uint16_t value)
{
	uint16_t reg_value = 0;
	uint16_t real_value = aw883xx_db_val_to_reg(value);

	/* cal real value */
	aw883xx_reg_read(aw883xx, AW_PID_2049_SYSCTRL2_REG, &reg_value);

	aw_dev_dbg(aw883xx->dev, "value 0x%x , reg:0x%x", value, real_value);

	/*[15 : 6] volume*/
	real_value = (real_value << AW_PID_2049_VOL_START_BIT) | (reg_value & AW_PID_2049_VOL_MASK);

	/* write value */
	aw883xx_reg_write(aw883xx, AW_PID_2049_SYSCTRL2_REG, real_value);

	return 0;
}

static int aw883xx_get_volume(struct aw883xx *aw883xx, uint16_t *value)
{
	uint16_t reg_value = 0;
	uint16_t real_value = 0;

	/* read value */
	aw883xx_reg_read(aw883xx, AW_PID_2049_SYSCTRL2_REG, &reg_value);

	/*[15 : 6] volume*/
	real_value = reg_value >> AW_PID_2049_VOL_START_BIT;

	real_value = aw_pid_2049_reg_val_to_db(real_value);

	*value = real_value;

	return 0;
}

static int aw_pid_2049_set_volume(struct aw_device *aw_dev, uint16_t value)
{
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev->private_data;
	return aw883xx_set_volume(aw883xx, value);
}

static int aw_pid_2049_get_volume(struct aw_device *aw_dev, uint16_t *value)
{
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev->private_data;
	return aw883xx_get_volume(aw883xx, value);
}

static void aw_pid_2049_i2s_tx_enable(struct aw_device *aw_dev, bool flag)
{
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev->private_data;

	aw_dev_dbg(aw883xx->dev, "enter");

	if (flag) {
		aw883xx_reg_write_bits(aw883xx, AW_PID_2049_I2SCFG1_REG,
				AW_PID_2049_I2STXEN_MASK,
				AW_PID_2049_I2STXEN_ENABLE_VALUE);
	} else {
		aw883xx_reg_write_bits(aw883xx, AW_PID_2049_I2SCFG1_REG,
				AW_PID_2049_I2STXEN_MASK,
				AW_PID_2049_I2STXEN_DISABLE_VALUE);
	}
}

static void aw_pid_2049_set_cfg_f0_fs(struct aw_device *aw_dev, uint32_t *f0_fs)
{
	uint16_t rate_data = 0;
	uint32_t fs = 0;
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev->private_data;

	aw_dev_dbg(aw883xx->dev, "enter");
	aw883xx_reg_read(aw883xx, AW_PID_2049_I2SCTRL_REG, &rate_data);

	switch (rate_data & (~AW_PID_2049_I2SSR_MASK)) {
	case AW_PID_2049_I2SSR_8_KHZ_VALUE:
		fs = 8000;
		break;
	case AW_PID_2049_I2SSR_16_KHZ_VALUE:
		fs = 16000;
		break;
	case AW_PID_2049_I2SSR_32_KHZ_VALUE:
		fs = 32000;
		break;
	case AW_PID_2049_I2SSR_44_KHZ_VALUE:
		fs = 44000;
		break;
	case AW_PID_2049_I2SSR_48_KHZ_VALUE:
		fs = 48000;
		break;
	case AW_PID_2049_I2SSR_96_KHZ_VALUE:
		fs = 96000;
		break;
	case AW_PID_2049_I2SSR_192KHZ_VALUE:
		fs = 192000;
		break;
	default:
		fs = 48000;
		aw_dev_err(aw883xx->dev,
			"rate can not support, use default 48k");
		break;
	}

	aw_dev_dbg(aw883xx->dev, "get i2s fs:%d", fs);
	*f0_fs = fs / 8;

	aw883xx_dsp_write(aw883xx,
		AW_PID_2049_DSP_REG_CFGF0_FS, *f0_fs, AW_DSP_32_DATA);
}

static bool aw_pid_2049_check_rd_access(int reg)
{
	if (reg >= AW_PID_2049_REG_MAX)
		return false;

	if (aw_pid_2049_reg_access[reg] & REG_RD_ACCESS)
		return true;
	else
		return false;
}

static bool aw_pid_2049_check_wr_access(int reg)
{
	if (reg >= AW_PID_2049_REG_MAX)
		return false;

	if (aw_pid_2049_reg_access[reg] & REG_WR_ACCESS)
		return true;
	else
		return false;
}

static int aw_pid_2049_get_reg_num(void)
{
	return AW_PID_2049_REG_MAX;
}

static int aw_pid_2049_get_hw_mon_st(struct aw_device *aw_dev,
					bool *is_enable, uint8_t *temp_flag)
{
	int ret = 0;
	uint32_t vbat_en = 0;
	uint32_t temp_en = 0;
	uint32_t temp_switch = 0;
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev->private_data;

	ret = aw883xx_dsp_read(aw883xx,
		AW_PID_2049_DSP_REG_CFG_MBMEC_GLBCFG, &vbat_en, AW_DSP_16_DATA);
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "read hardware monitor status failed");
		return ret;
	}

	ret = aw883xx_dsp_read(aw883xx,
		AW_PID_2049_DSP_REG_TEMP_SWITCH, &temp_en, AW_DSP_16_DATA);
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "read hardware temp switch failed");
		return ret;
	}

	temp_switch = temp_en;
	vbat_en &= (~AW_PID_2049_DSP_MONITOR_MASK);
	temp_en &= (~AW_PID_2049_DSP_TEMP_PEAK_MASK);
	temp_switch &= (~AW_PID_2049_DSP_TEMP_SEL_FLAG);

	if (vbat_en || temp_en)
		*is_enable = true;
	else
		*is_enable = false;

	if (temp_switch)
		*temp_flag = AW_EXTERNAL_TEMP;
	else
		*temp_flag = AW_INTERNAL_TEMP;

	return 0;
}

static int aw_pid_2049_cali_get_iv_st(struct aw_device *aw_dev)
{
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev->private_data;
	int ret;
	uint16_t reg_data = 0;
	int i;

	aw_dev_info(aw_dev->dev, "enter");

	for (i = 0; i < AW_GET_IV_CNT_MAX; i++) {
		ret = aw883xx_reg_read(aw883xx, AW_PID_2049_ASR1_REG, &reg_data);
		if (ret < 0) {
			aw_dev_err(aw883xx->dev,
				"read 0x%x failed", AW_PID_2049_ASR1_REG);
			return ret;
		}

		reg_data &= (~AW_PID_2049_ReAbs_MASK);
		if (!reg_data)
			return 0;
		msleep(30);
	}

	aw_dev_err(aw883xx->dev, "IV data abnormal, please check");

	return -EINVAL;
}

static int aw_pid_2049_dsp_fw_check(struct aw_device *aw_dev)
{
	struct aw_prof_desc *set_prof_desc = NULL;
	struct aw_sec_data_desc *dsp_fw_desc = NULL;
	uint16_t base_addr = AW_PID_2049_DSP_FW_ADDR;
	uint16_t addr = base_addr;
	int ret, i;
	uint32_t dsp_val;
	uint16_t bin_val;

	aw_dev_info(aw_dev->dev, "enter");

	ret = aw_dev_get_prof_data(aw_dev, aw_dev->cur_prof, &set_prof_desc);
	if (ret < 0)
		return ret;

	/*update reg*/
	dsp_fw_desc = &set_prof_desc->sec_desc[AW_DATA_TYPE_DSP_FW];

	for (i = 0; i < AW_FW_CHECK_PART; i++) {
		ret = aw883xx_dev_dsp_read(aw_dev, addr, &dsp_val, AW_DSP_16_DATA);
		if (ret  < 0) {
			aw_dev_err(aw_dev->dev, "dsp read failed");
			return ret;
		}

		bin_val = AW_GET_16_DATA(dsp_fw_desc->data[2 * (addr - base_addr)],
					dsp_fw_desc->data[2 * (addr - base_addr) + 1]);

		if (dsp_val != bin_val) {
			aw_dev_err(aw_dev->dev, "check failed, addr[0x%x], read[0x%x] != bindata[0x%x]",
					addr, dsp_val, bin_val);
			return -EINVAL;
		}

		addr += (dsp_fw_desc->len / 2) / AW_FW_CHECK_PART;
		if ((addr - base_addr) > dsp_fw_desc->len) {
			aw_dev_err(aw_dev->dev, "check failed, addr[0x%x] too large", addr);
			return -EINVAL;
		}
	}

	aw_dev_info(aw_dev->dev, "dsp fw check success");

	return 0;
}

static int aw883xx_dev_init(struct aw883xx *aw883xx)
{
	struct aw_device *aw_pa = NULL;

	aw_pa = devm_kzalloc(aw883xx->dev, sizeof(struct aw_device), GFP_KERNEL);
	if (aw_pa == NULL) {
		aw_dev_err(aw883xx->dev, "dev kalloc failed");
		return -ENOMEM;
	}

	/*call aw device init func*/
	aw_pa->acf = NULL;
	aw_pa->prof_info.prof_desc = NULL;
	aw_pa->prof_info.count = 0;
	aw_pa->prof_info.prof_type = AW_DEV_NONE_TYPE_ID;
	aw_pa->channel = 0;
	aw_pa->i2c_lock = &aw883xx->i2c_lock;
	aw_pa->i2c = aw883xx->i2c;
	aw_pa->fw_status = AW_DEV_FW_FAILED;
	aw_pa->fade_step = AW_PID_2049_VOLUME_STEP_DB;
	aw_pa->re_range.re_min_default = AW_RE_MIN;
	aw_pa->re_range.re_max_default = AW_RE_MAX;
	aw_pa->monitor_desc.hw_monitor_delay = AW_HW_MONITOR_DELAY;

	aw_pa->chip_id = aw883xx->chip_id;
	aw_pa->private_data = (void *)aw883xx;
	aw_pa->dev = aw883xx->dev;
	aw_pa->ops.aw_get_version = aw883xx_get_version;
	aw_pa->ops.aw_i2c_writes = aw883xx_dev_i2c_writes;
	aw_pa->ops.aw_i2c_write = aw883xx_dev_i2c_write;
	aw_pa->ops.aw_reg_write = aw883xx_dev_reg_write;
	aw_pa->ops.aw_reg_write_bits = aw883xx_dev_reg_write_bits;
	aw_pa->ops.aw_i2c_read = aw883xx_dev_i2c_read;
	aw_pa->ops.aw_reg_read = aw883xx_dev_reg_read;
	aw_pa->ops.aw_dsp_read = aw883xx_dev_dsp_read;
	aw_pa->ops.aw_dsp_write = aw883xx_dev_dsp_write;
	aw_pa->ops.aw_get_dev_num = aw883xx_get_dev_num;

	aw_pa->ops.aw_get_volume = aw_pid_2049_get_volume;
	aw_pa->ops.aw_set_volume = aw_pid_2049_set_volume;
	aw_pa->ops.aw_reg_val_to_db = aw_pid_2049_reg_val_to_db;
	aw_pa->ops.aw_check_rd_access = aw_pid_2049_check_rd_access;
	aw_pa->ops.aw_check_wr_access = aw_pid_2049_check_wr_access;
	aw_pa->ops.aw_get_reg_num = aw_pid_2049_get_reg_num;

	aw_pa->ops.aw_i2s_tx_enable = aw_pid_2049_i2s_tx_enable;
	aw_pa->ops.aw_get_hw_mon_st = aw_pid_2049_get_hw_mon_st;

	aw_pa->ops.aw_cali_svc_get_iv_st = aw_pid_2049_cali_get_iv_st;
	aw_pa->ops.aw_set_cfg_f0_fs = aw_pid_2049_set_cfg_f0_fs;
	aw_pa->ops.aw_dsp_fw_check = aw_pid_2049_dsp_fw_check;

	aw_pa->int_desc.mask_reg = AW_PID_2049_SYSINTM_REG;
	aw_pa->int_desc.mask_default = AW_PID_2049_SYSINTM_DEFAULT;
	aw_pa->int_desc.int_mask = AW_PID_2049_SYSINTM_DEFAULT;
	aw_pa->int_desc.st_reg = AW_PID_2049_SYSINT_REG;
	aw_pa->int_desc.intst_mask = AW_PID_2049_BIT_SYSINT_CHECK;

	aw_pa->pwd_desc.reg = AW_PID_2049_SYSCTRL_REG;
	aw_pa->pwd_desc.mask = AW_PID_2049_PWDN_MASK;
	aw_pa->pwd_desc.enable = AW_PID_2049_PWDN_POWER_DOWN_VALUE;
	aw_pa->pwd_desc.disable = AW_PID_2049_PWDN_WORKING_VALUE;

	aw_pa->mute_desc.reg = AW_PID_2049_SYSCTRL_REG;
	aw_pa->mute_desc.mask = AW_PID_2049_HMUTE_MASK;
	aw_pa->mute_desc.enable = AW_PID_2049_HMUTE_ENABLE_VALUE;
	aw_pa->mute_desc.disable = AW_PID_2049_HMUTE_DISABLE_VALUE;

	aw_pa->vcalb_desc.vcalb_dsp_reg = AW_PID_2049_DSP_REG_VCALB;
	aw_pa->vcalb_desc.data_type = AW_DSP_16_DATA;
	aw_pa->vcalb_desc.vcal_factor = AW_PID_2049_VCAL_FACTOR;
	aw_pa->vcalb_desc.cabl_base_value = AW_PID_2049_CABL_BASE_VALUE;
	aw_pa->vcalb_desc.vscal_factor = AW_PID_2049_VSCAL_FACTOR;
	aw_pa->vcalb_desc.iscal_factor = AW_PID_2049_ISCAL_FACTOR;

	aw_pa->vcalb_desc.vcalb_adj_shift = AW_PID_2049_VCALB_ADJ_FACTOR;

	aw_pa->vcalb_desc.icalk_value_factor = AW_PID_2049_ICABLK_FACTOR;
	aw_pa->vcalb_desc.icalk_reg = AW_PID_2049_EFRM2_REG;
	aw_pa->vcalb_desc.icalk_reg_mask = AW_PID_2049_EF_ISN_GESLP_MASK;
	aw_pa->vcalb_desc.icalk_sign_mask = AW_PID_2049_EF_ISN_GESLP_SIGN_MASK;
	aw_pa->vcalb_desc.icalk_neg_mask = AW_PID_2049_EF_ISN_GESLP_SIGN_NEG;

	aw_pa->vcalb_desc.vcalk_reg = AW_PID_2049_EFRH_REG;
	aw_pa->vcalb_desc.vcalk_reg_mask = AW_PID_2049_EF_VSN_GESLP_MASK;
	aw_pa->vcalb_desc.vcalk_sign_mask = AW_PID_2049_EF_VSN_GESLP_SIGN_MASK;
	aw_pa->vcalb_desc.vcalk_neg_mask = AW_PID_2049_EF_VSN_GESLP_SIGN_NEG;
	aw_pa->vcalb_desc.vcalk_value_factor = AW_PID_2049_VCABLK_FACTOR;
	aw_pa->vcalb_desc.vcalk_shift = AW_PID_2049_EF_VSENSE_GAIN_SHIFT;

	aw_pa->vcalb_desc.vcalb_vsense_reg = AW_PID_2049_I2SCFG3_REG;
	aw_pa->vcalb_desc.vcalk_vdsel_mask = AW_PID_2049_VDSEL_MASK;
	aw_pa->vcalb_desc.vcalk_value_factor_vsense_in = AW_PID_2049_VCABLK_FACTOR_DAC;
	aw_pa->vcalb_desc.vscal_factor_vsense_in = AW_PID_2049_VSCAL_FACTOR_DAC;
	aw_pa->vcalb_desc.vcalk_dac_shift = AW_PID_2049_EF_DAC_GESLP_SHIFT;
	aw_pa->vcalb_desc.vcalk_dac_mask = AW_PID_2049_EF_DAC_GESLP_SIGN_MASK;
	aw_pa->vcalb_desc.vcalk_dac_neg_mask = AW_PID_2049_EF_DAC_GESLP_SIGN_NEG;

	aw_pa->sysst_desc.reg = AW_PID_2049_SYSST_REG;
	aw_pa->sysst_desc.st_check = AW_PID_2049_BIT_SYSST_CHECK;
	aw_pa->sysst_desc.st_mask = AW_PID_2049_BIT_SYSST_CHECK_MASK;
	aw_pa->sysst_desc.pll_check = AW_PID_2049_BIT_PLL_CHECK;
	aw_pa->sysst_desc.dsp_check = AW_PID_2049_DSPS_NORMAL_VALUE;
	aw_pa->sysst_desc.dsp_mask = AW_PID_2049_DSPS_MASK;

	aw_pa->profctrl_desc.reg = AW_PID_2049_SYSCTRL_REG;
	aw_pa->profctrl_desc.mask = AW_PID_2049_RCV_MODE_MASK;
	aw_pa->profctrl_desc.rcv_mode_val = AW_PID_2049_RCV_MODE_RECEIVER_VALUE;

	aw_pa->volume_desc.reg = AW_PID_2049_SYSCTRL2_REG;
	aw_pa->volume_desc.mask = AW_PID_2049_VOL_MASK;
	aw_pa->volume_desc.shift = AW_PID_2049_VOL_START_BIT;
	aw_pa->volume_desc.mute_volume = AW_PID_2049_MUTE_VOL;

	aw_pa->dsp_en_desc.reg = AW_PID_2049_SYSCTRL_REG;
	aw_pa->dsp_en_desc.mask = AW_PID_2049_DSPBY_MASK;
	aw_pa->dsp_en_desc.enable = AW_PID_2049_DSPBY_WORKING_VALUE;
	aw_pa->dsp_en_desc.disable = AW_PID_2049_DSPBY_BYPASS_VALUE;

	aw_pa->memclk_desc.reg = AW_PID_2049_DBGCTRL_REG;
	aw_pa->memclk_desc.mask = AW_PID_2049_MEM_CLKSEL_MASK;
	aw_pa->memclk_desc.mcu_hclk = AW_PID_2049_MEM_CLKSEL_DAP_HCLK_VALUE;
	aw_pa->memclk_desc.osc_clk = AW_PID_2049_MEM_CLKSEL_OSC_CLK_VALUE;

	aw_pa->watch_dog_desc.reg = AW_PID_2049_WDT_REG;
	aw_pa->watch_dog_desc.mask = AW_PID_2049_WDT_CNT_MASK;

	aw_pa->dsp_mem_desc.dsp_madd_reg = AW_PID_2049_DSPMADD_REG;
	aw_pa->dsp_mem_desc.dsp_mdat_reg = AW_PID_2049_DSPMDAT_REG;
	aw_pa->dsp_mem_desc.dsp_cfg_base_addr = AW_PID_2049_DSP_CFG_ADDR;
	aw_pa->dsp_mem_desc.dsp_fw_base_addr = AW_PID_2049_DSP_FW_ADDR;

	aw_pa->voltage_desc.reg = AW_PID_2049_VBAT_REG;
	aw_pa->voltage_desc.vbat_range = AW_PID_2049_VBAT_RANGE;
	aw_pa->voltage_desc.int_bit = AW_PID_2049_INT_10BIT;

	aw_pa->temp_desc.reg = AW_PID_2049_TEMP_REG;
	aw_pa->temp_desc.sign_mask = AW_PID_2049_TEMP_SIGN_MASK;
	aw_pa->temp_desc.neg_mask = AW_PID_2049_TEMP_NEG_MASK;

	aw_pa->vmax_desc.dsp_reg = AW_PID_2049_DSP_REG_VMAX;
	aw_pa->vmax_desc.data_type = AW_DSP_16_DATA;

	aw_pa->ipeak_desc.reg = AW_PID_2049_SYSCTRL2_REG;
	aw_pa->ipeak_desc.mask = AW_PID_2049_BST_IPEAK_MASK;

	aw_pa->soft_rst.reg = AW_PID_2049_ID_REG;
	aw_pa->soft_rst.reg_value = AW_PID_2049_SOFT_RESET_VALUE;

	aw_pa->dsp_vol_desc.reg = AW_PID_2049_DSPCFG_REG;
	aw_pa->dsp_vol_desc.mask = AW_PID_2049_DSP_VOL_MASK;
	aw_pa->dsp_vol_desc.mute_st = AW_PID_2049_DSP_VOL_MUTE;
	aw_pa->dsp_vol_desc.noise_st = AW_PID_2049_DSP_VOL_NOISE_ST;

	aw_pa->amppd_desc.reg = AW_PID_2049_SYSCTRL_REG;
	aw_pa->amppd_desc.mask = AW_PID_2049_AMPPD_MASK;
	aw_pa->amppd_desc.enable = AW_PID_2049_AMPPD_POWER_DOWN_VALUE;
	aw_pa->amppd_desc.disable = AW_PID_2049_AMPPD_WORKING_VALUE;

	aw_pa->spkr_temp_desc.reg = AW_PID_2049_ASR2_REG;

	/*32-bit data types need bypass dsp*/
	aw_pa->ra_desc.dsp_reg = AW_PID_2049_DSP_REG_CFG_ADPZ_RA;
	aw_pa->ra_desc.data_type = AW_DSP_32_DATA;

	/*32-bit data types need bypass dsp*/
	aw_pa->cali_cfg_desc.actampth_reg = AW_PID_2049_DSP_REG_CFG_MBMEC_ACTAMPTH;
	aw_pa->cali_cfg_desc.actampth_data_type = AW_DSP_32_DATA;

	/*32-bit data types need bypass dsp*/
	aw_pa->cali_cfg_desc.noiseampth_reg = AW_PID_2049_DSP_REG_CFG_MBMEC_NOISEAMPTH;
	aw_pa->cali_cfg_desc.noiseampth_data_type = AW_DSP_32_DATA;

	aw_pa->cali_cfg_desc.ustepn_reg = AW_PID_2049_DSP_REG_CFG_ADPZ_USTEPN;
	aw_pa->cali_cfg_desc.ustepn_data_type = AW_DSP_16_DATA;

	aw_pa->cali_cfg_desc.alphan_reg = AW_PID_2049_DSP_REG_CFG_RE_ALPHA;
	aw_pa->cali_cfg_desc.alphan_data_type = AW_DSP_16_DATA;

	/*32-bit data types need bypass dsp*/
	aw_pa->adpz_re_desc.dsp_reg = AW_PID_2049_DSP_REG_CFG_ADPZ_RE;
	aw_pa->adpz_re_desc.data_type = AW_DSP_32_DATA;
	aw_pa->adpz_re_desc.shift = AW_PID_2049_DSP_RE_SHIFT;

	aw_pa->t0_desc.dsp_reg = AW_PID_2049_DSP_CFG_ADPZ_T0;
	aw_pa->t0_desc.data_type = AW_DSP_16_DATA;
	aw_pa->t0_desc.coilalpha_reg = AW_PID_2049_DSP_CFG_ADPZ_COILALPHA;
	aw_pa->t0_desc.coil_type = AW_DSP_16_DATA;

	aw_pa->ste_re_desc.shift = AW_PID_2049_DSP_REG_CALRE_SHIFT;
	aw_pa->ste_re_desc.dsp_reg = AW_PID_2049_DSP_REG_CALRE;
	aw_pa->ste_re_desc.data_type = AW_DSP_16_DATA;

	aw_pa->noise_desc.dsp_reg = AW_PID_2049_DSP_REG_CFG_MBMEC_GLBCFG;
	aw_pa->noise_desc.data_type = AW_DSP_16_DATA;
	aw_pa->noise_desc.mask = AW_PID_2049_DSP_REG_NOISE_MASK;

	aw_pa->f0_desc.dsp_reg = AW_PID_2049_DSP_REG_RESULT_F0;
	aw_pa->f0_desc.shift = AW_PID_2049_DSP_F0_SHIFT;
	aw_pa->f0_desc.data_type = AW_DSP_16_DATA;

	/*32-bit data types need bypass dsp*/
	aw_pa->cfgf0_fs_desc.dsp_reg = AW_PID_2049_DSP_REG_CFGF0_FS;
	aw_pa->cfgf0_fs_desc.data_type = AW_DSP_32_DATA;

	aw_pa->q_desc.dsp_reg = AW_PID_2049_DSP_REG_RESULT_Q;
	aw_pa->q_desc.shift = AW_PID_2049_DSP_Q_SHIFT;
	aw_pa->q_desc.data_type = AW_DSP_16_DATA;

	/*32-bit data types need bypass dsp*/
	aw_pa->dsp_crc_desc.dsp_reg = AW_PID_2049_DSP_REG_CRC_ADDR;
	aw_pa->dsp_crc_desc.data_type = AW_DSP_32_DATA;

	aw_pa->dsp_crc_desc.ctl_reg = AW_PID_2049_HAGCCFG7_REG;
	aw_pa->dsp_crc_desc.ctl_mask = AW_PID_2049_AGC_DSP_CTL_MASK;
	aw_pa->dsp_crc_desc.ctl_enable = AW_PID_2049_AGC_DSP_CTL_ENABLE_VALUE;
	aw_pa->dsp_crc_desc.ctl_disable = AW_PID_2049_AGC_DSP_CTL_DISABLE_VALUE;

	aw_pa->cco_mux_desc.reg = AW_PID_2049_PLLCTRL1_REG;
	aw_pa->cco_mux_desc.mask = AW_PID_2049_CCO_MUX_MASK;
	aw_pa->cco_mux_desc.divider = AW_PID_2049_CCO_MUX_DIVIDED_VALUE;
	aw_pa->cco_mux_desc.bypass = AW_PID_2049_CCO_MUX_BYPASS_VALUE;

	/*hw monitor temp reg*/
	aw_pa->hw_temp_desc.dsp_reg = AW_PID_2049_DSP_REG_TEMP_ADDR;
	aw_pa->hw_temp_desc.data_type = AW_DSP_16_DATA;

	aw_pa->chansel_desc.rxchan_reg = AW_PID_2049_I2SCTRL_REG;
	aw_pa->chansel_desc.rxchan_mask = AW_PID_2049_CHSEL_MASK;
	aw_pa->chansel_desc.txchan_reg = AW_PID_2049_I2SCFG1_REG;
	aw_pa->chansel_desc.txchan_mask = AW_PID_2049_I2SCHS_MASK;

	aw_pa->chansel_desc.rx_left = AW_PID_2049_CHSEL_LEFT_VALUE;
	aw_pa->chansel_desc.rx_right = AW_PID_2049_CHSEL_RIGHT_VALUE;
	aw_pa->chansel_desc.tx_left = AW_PID_2049_I2SCHS_LEFT_VALUE;
	aw_pa->chansel_desc.tx_right = AW_PID_2049_I2SCHS_RIGHT_VALUE;

	aw_pa->tx_en_desc.tx_en_mask = AW_PID_2049_I2STXEN_MASK;
	aw_pa->tx_en_desc.tx_disable = AW_PID_2049_I2STXEN_DISABLE_VALUE;

	aw_pa->cali_delay_desc.dsp_reg = AW_PID_2049_DSP_CALI_F0_DELAY;
	aw_pa->cali_delay_desc.data_type = AW_DSP_16_DATA;

	aw_pa->dsp_st_desc.dsp_reg_s1 = AW_PID_2049_DSP_ST_S1;
	aw_pa->dsp_st_desc.dsp_reg_e1 = AW_PID_2049_DSP_ST_E1;
	aw_pa->dsp_st_desc.dsp_reg_s2 = AW_PID_2049_DSP_ST_S2;
	aw_pa->dsp_st_desc.dsp_reg_e2 = AW_PID_2049_DSP_ST_E2;

	aw_device_probe(aw_pa);

	aw883xx->aw_pa = aw_pa;

	return 0;
}

int aw883xx_init(struct aw883xx *aw883xx)
{
	if (aw883xx->chip_id == AW883XX_PID_2049) {
		return aw883xx_dev_init(aw883xx);
	} else {
		aw_dev_err(aw883xx->dev, "unsupported device");
		return -EINVAL;
	}
}

