/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2022 Linaro. All rights reserved.
 * Author: Casey Connolly <casey.connolly@linaro.org>
 */

#ifndef __QCOM_SPMI_PMIC_H__
#define __QCOM_SPMI_PMIC_H__

#include <linux/device.h>

#define COMMON_SUBTYPE		0x00
#define PM8941_SUBTYPE		0x01
#define PM8841_SUBTYPE		0x02
#define PM8019_SUBTYPE		0x03
#define PM8226_SUBTYPE		0x04
#define PM8110_SUBTYPE		0x05
#define PMA8084_SUBTYPE		0x06
#define PMI8962_SUBTYPE		0x07
#define PMD9635_SUBTYPE		0x08
#define PM8994_SUBTYPE		0x09
#define PMI8994_SUBTYPE		0x0a
#define PM8916_SUBTYPE		0x0b
#define PM8004_SUBTYPE		0x0c
#define PM8909_SUBTYPE		0x0d
#define PM8028_SUBTYPE		0x0e
#define PM8901_SUBTYPE		0x0f
#define PM8950_SUBTYPE		0x10
#define PMI8950_SUBTYPE		0x11
#define PMK8001_SUBTYPE		0x12
#define PMI8996_SUBTYPE		0x13
#define PM8998_SUBTYPE		0x14
#define PMI8998_SUBTYPE		0x15
#define PM8005_SUBTYPE		0x18
#define PM8937_SUBTYPE		0x19
#define PM660L_SUBTYPE		0x1a
#define PM660_SUBTYPE		0x1b
#define PM8150_SUBTYPE		0x1e
#define PM8150L_SUBTYPE		0x1f
#define PM8150B_SUBTYPE		0x20
#define PMK8002_SUBTYPE		0x21
#define PM8009_SUBTYPE		0x24
#define PMI632_SUBTYPE		0x25
#define PM8150C_SUBTYPE		0x26
#define PM6150_SUBTYPE		0x28
#define SMB2351_SUBTYPE		0x29
#define PM8008_SUBTYPE		0x2c
#define PM6125_SUBTYPE		0x2d
#define PM7250B_SUBTYPE		0x2e
#define PMK8350_SUBTYPE		0x2f
#define PMR735B_SUBTYPE		0x34
#define PM6350_SUBTYPE		0x36
#define PM4125_SUBTYPE		0x37
#define PMM8650AU_SUBTYPE       0x4e
#define PMM8650AU_PSAIL_SUBTYPE 0x4f

#define PMI8998_FAB_ID_SMIC	0x11
#define PMI8998_FAB_ID_GF	0x30

#define PM660_FAB_ID_GF		0x0
#define PM660_FAB_ID_TSMC	0x2
#define PM660_FAB_ID_MX		0x3

struct qcom_spmi_pmic {
	unsigned int type;
	unsigned int subtype;
	unsigned int major;
	unsigned int minor;
	unsigned int rev2;
	unsigned int fab_id;
	const char *name;
};

const struct qcom_spmi_pmic *qcom_pmic_get(struct device *dev);

#endif /* __QCOM_SPMI_PMIC_H__ */
