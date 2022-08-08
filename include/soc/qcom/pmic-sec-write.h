/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_ARCH_QCOM_PMIC_SEC_WRITE_H__
#define __SOC_ARCH_QCOM_PMIC_SEC_WRITE_H__

int qcom_pmic_sec_write(struct regmap *regmap, u16 addr, u8 *val, int len);
int qcom_pmic_sec_masked_write(struct regmap *regmap, u16 addr, u8 mask, u8 val);

#endif
