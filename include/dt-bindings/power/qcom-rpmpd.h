/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, The Linux Foundation. All rights reserved. */

#ifndef _DT_BINDINGS_POWER_QCOM_RPMPD_H
#define _DT_BINDINGS_POWER_QCOM_RPMPD_H

/* SDM670 Power Domain Indexes */
#define SDM670_MX	0
#define SDM670_MX_AO	1
#define SDM670_CX	2
#define SDM670_CX_AO	3
#define SDM670_LMX	4
#define SDM670_LCX	5
#define SDM670_GFX	6
#define SDM670_MSS	7

/* SDM845 Power Domain Indexes */
#define SDM845_EBI	0
#define SDM845_MX	1
#define SDM845_MX_AO	2
#define SDM845_CX	3
#define SDM845_CX_AO	4
#define SDM845_LMX	5
#define SDM845_LCX	6
#define SDM845_GFX	7
#define SDM845_MSS	8

/* SDX55 Power Domain Indexes */
#define SDX55_MSS	0
#define SDX55_MX	1
#define SDX55_CX	2

/* SDX65 Power Domain Indexes */
#define SDX65_MSS	0
#define SDX65_MX	1
#define SDX65_MX_AO	2
#define SDX65_CX	3
#define SDX65_CX_AO	4
#define SDX65_MXC	5

/* SM6350 Power Domain Indexes */
#define SM6350_CX	0
#define SM6350_GFX	1
#define SM6350_LCX	2
#define SM6350_LMX	3
#define SM6350_MSS	4
#define SM6350_MX	5

/* SM6350 Power Domain Indexes */
#define SM6375_VDDCX		0
#define SM6375_VDDCX_AO	1
#define SM6375_VDDCX_VFL	2
#define SM6375_VDDMX		3
#define SM6375_VDDMX_AO	4
#define SM6375_VDDMX_VFL	5
#define SM6375_VDDGX		6
#define SM6375_VDDGX_AO	7
#define SM6375_VDD_LPI_CX	8
#define SM6375_VDD_LPI_MX	9

/* SM8150 Power Domain Indexes */
#define SM8150_MSS	0
#define SM8150_EBI	1
#define SM8150_LMX	2
#define SM8150_LCX	3
#define SM8150_GFX	4
#define SM8150_MX	5
#define SM8150_MX_AO	6
#define SM8150_CX	7
#define SM8150_CX_AO	8
#define SM8150_MMCX	9
#define SM8150_MMCX_AO	10

/* SM8250 Power Domain Indexes */
#define SM8250_CX	0
#define SM8250_CX_AO	1
#define SM8250_EBI	2
#define SM8250_GFX	3
#define SM8250_LCX	4
#define SM8250_LMX	5
#define SM8250_MMCX	6
#define SM8250_MMCX_AO	7
#define SM8250_MX	8
#define SM8250_MX_AO	9

/* SM8350 Power Domain Indexes */
#define SM8350_CX	0
#define SM8350_CX_AO	1
#define SM8350_EBI	2
#define SM8350_GFX	3
#define SM8350_LCX	4
#define SM8350_LMX	5
#define SM8350_MMCX	6
#define SM8350_MMCX_AO	7
#define SM8350_MX	8
#define SM8350_MX_AO	9
#define SM8350_MXC	10
#define SM8350_MXC_AO	11
#define SM8350_MSS	12

/* SM8450 Power Domain Indexes */
#define SM8450_CX	0
#define SM8450_CX_AO	1
#define SM8450_EBI	2
#define SM8450_GFX	3
#define SM8450_LCX	4
#define SM8450_LMX	5
#define SM8450_MMCX	6
#define SM8450_MMCX_AO	7
#define SM8450_MX	8
#define SM8450_MX_AO	9
#define SM8450_MXC	10
#define SM8450_MXC_AO	11
#define SM8450_MSS	12

/* SM8550 Power Domain Indexes */
#define SM8550_CX	0
#define SM8550_CX_AO	1
#define SM8550_EBI	2
#define SM8550_GFX	3
#define SM8550_LCX	4
#define SM8550_LMX	5
#define SM8550_MMCX	6
#define SM8550_MMCX_AO	7
#define SM8550_MX	8
#define SM8550_MX_AO	9
#define SM8550_MXC	10
#define SM8550_MXC_AO	11
#define SM8550_MSS	12
#define SM8550_NSP	13

/* QDU1000/QRU1000 Power Domain Indexes */
#define QDU1000_EBI	0
#define QDU1000_MSS	1
#define QDU1000_CX	2
#define QDU1000_MX	3

/* SC7180 Power Domain Indexes */
#define SC7180_CX	0
#define SC7180_CX_AO	1
#define SC7180_GFX	2
#define SC7180_MX	3
#define SC7180_MX_AO	4
#define SC7180_LMX	5
#define SC7180_LCX	6
#define SC7180_MSS	7

/* SC7280 Power Domain Indexes */
#define SC7280_CX	0
#define SC7280_CX_AO	1
#define SC7280_EBI	2
#define SC7280_GFX	3
#define SC7280_MX	4
#define SC7280_MX_AO	5
#define SC7280_LMX	6
#define SC7280_LCX	7
#define SC7280_MSS	8

/* SC8180X Power Domain Indexes */
#define SC8180X_CX	0
#define SC8180X_CX_AO	1
#define SC8180X_EBI	2
#define SC8180X_GFX	3
#define SC8180X_LCX	4
#define SC8180X_LMX	5
#define SC8180X_MMCX	6
#define SC8180X_MMCX_AO	7
#define SC8180X_MSS	8
#define SC8180X_MX	9
#define SC8180X_MX_AO	10

/* SC8280XP Power Domain Indexes */
#define SC8280XP_CX		0
#define SC8280XP_CX_AO		1
#define SC8280XP_DDR		2
#define SC8280XP_EBI		3
#define SC8280XP_GFX		4
#define SC8280XP_LCX		5
#define SC8280XP_LMX		6
#define SC8280XP_MMCX		7
#define SC8280XP_MMCX_AO	8
#define SC8280XP_MSS		9
#define SC8280XP_MX		10
#define SC8280XP_MXC		12
#define SC8280XP_MX_AO		11
#define SC8280XP_NSP		13
#define SC8280XP_QPHY		14
#define SC8280XP_XO		15

/* SDM845 Power Domain performance levels */
#define RPMH_REGULATOR_LEVEL_RETENTION	16
#define RPMH_REGULATOR_LEVEL_MIN_SVS	48
#define RPMH_REGULATOR_LEVEL_LOW_SVS_D1	56
#define RPMH_REGULATOR_LEVEL_LOW_SVS	64
#define RPMH_REGULATOR_LEVEL_SVS	128
#define RPMH_REGULATOR_LEVEL_SVS_L0	144
#define RPMH_REGULATOR_LEVEL_SVS_L1	192
#define RPMH_REGULATOR_LEVEL_SVS_L2	224
#define RPMH_REGULATOR_LEVEL_NOM	256
#define RPMH_REGULATOR_LEVEL_NOM_L1	320
#define RPMH_REGULATOR_LEVEL_NOM_L2	336
#define RPMH_REGULATOR_LEVEL_TURBO	384
#define RPMH_REGULATOR_LEVEL_TURBO_L1	416

/* MDM9607 Power Domains */
#define MDM9607_VDDCX		0
#define MDM9607_VDDCX_AO	1
#define MDM9607_VDDCX_VFL	2
#define MDM9607_VDDMX		3
#define MDM9607_VDDMX_AO	4
#define MDM9607_VDDMX_VFL	5

/* MSM8226 Power Domain Indexes */
#define MSM8226_VDDCX		0
#define MSM8226_VDDCX_AO	1
#define MSM8226_VDDCX_VFC	2

/* MSM8939 Power Domains */
#define MSM8939_VDDMDCX		0
#define MSM8939_VDDMDCX_AO	1
#define MSM8939_VDDMDCX_VFC	2
#define MSM8939_VDDCX		3
#define MSM8939_VDDCX_AO	4
#define MSM8939_VDDCX_VFC	5
#define MSM8939_VDDMX		6
#define MSM8939_VDDMX_AO	7

/* MSM8916 Power Domain Indexes */
#define MSM8916_VDDCX		0
#define MSM8916_VDDCX_AO	1
#define MSM8916_VDDCX_VFC	2
#define MSM8916_VDDMX		3
#define MSM8916_VDDMX_AO	4

/* MSM8909 Power Domain Indexes */
#define MSM8909_VDDCX		MSM8916_VDDCX
#define MSM8909_VDDCX_AO	MSM8916_VDDCX_AO
#define MSM8909_VDDCX_VFC	MSM8916_VDDCX_VFC
#define MSM8909_VDDMX		MSM8916_VDDMX
#define MSM8909_VDDMX_AO	MSM8916_VDDMX_AO

/* MSM8953 Power Domain Indexes */
#define MSM8953_VDDMD		0
#define MSM8953_VDDMD_AO	1
#define MSM8953_VDDCX		2
#define MSM8953_VDDCX_AO	3
#define MSM8953_VDDCX_VFL	4
#define MSM8953_VDDMX		5
#define MSM8953_VDDMX_AO	6

/* MSM8976 Power Domain Indexes */
#define MSM8976_VDDCX		0
#define MSM8976_VDDCX_AO	1
#define MSM8976_VDDCX_VFL	2
#define MSM8976_VDDMX		3
#define MSM8976_VDDMX_AO	4
#define MSM8976_VDDMX_VFL	5

/* MSM8994 Power Domain Indexes */
#define MSM8994_VDDCX		0
#define MSM8994_VDDCX_AO	1
#define MSM8994_VDDCX_VFC	2
#define MSM8994_VDDMX		3
#define MSM8994_VDDMX_AO	4
#define MSM8994_VDDGFX		5
#define MSM8994_VDDGFX_VFC	6

/* MSM8996 Power Domain Indexes */
#define MSM8996_VDDCX		0
#define MSM8996_VDDCX_AO	1
#define MSM8996_VDDCX_VFC	2
#define MSM8996_VDDMX		3
#define MSM8996_VDDMX_AO	4
#define MSM8996_VDDSSCX		5
#define MSM8996_VDDSSCX_VFC	6

/* MSM8998 Power Domain Indexes */
#define MSM8998_VDDCX		0
#define MSM8998_VDDCX_AO	1
#define MSM8998_VDDCX_VFL	2
#define MSM8998_VDDMX		3
#define MSM8998_VDDMX_AO	4
#define MSM8998_VDDMX_VFL	5
#define MSM8998_SSCCX		6
#define MSM8998_SSCCX_VFL	7
#define MSM8998_SSCMX		8
#define MSM8998_SSCMX_VFL	9

/* QCS404 Power Domains */
#define QCS404_VDDMX		0
#define QCS404_VDDMX_AO		1
#define QCS404_VDDMX_VFL	2
#define QCS404_LPICX		3
#define QCS404_LPICX_VFL	4
#define QCS404_LPIMX		5
#define QCS404_LPIMX_VFL	6

/* SDM660 Power Domains */
#define SDM660_VDDCX		0
#define SDM660_VDDCX_AO		1
#define SDM660_VDDCX_VFL	2
#define SDM660_VDDMX		3
#define SDM660_VDDMX_AO		4
#define SDM660_VDDMX_VFL	5
#define SDM660_SSCCX		6
#define SDM660_SSCCX_VFL	7
#define SDM660_SSCMX		8
#define SDM660_SSCMX_VFL	9

/* SM4250 Power Domains */
#define SM4250_VDDCX		0
#define SM4250_VDDCX_AO		1
#define SM4250_VDDCX_VFL	2
#define SM4250_VDDMX		3
#define SM4250_VDDMX_AO		4
#define SM4250_VDDMX_VFL	5
#define SM4250_VDD_LPI_CX	6
#define SM4250_VDD_LPI_MX	7

/* SM6115 Power Domains */
#define SM6115_VDDCX		0
#define SM6115_VDDCX_AO		1
#define SM6115_VDDCX_VFL	2
#define SM6115_VDDMX		3
#define SM6115_VDDMX_AO		4
#define SM6115_VDDMX_VFL	5
#define SM6115_VDD_LPI_CX	6
#define SM6115_VDD_LPI_MX	7

/* SM6125 Power Domains */
#define SM6125_VDDCX		0
#define SM6125_VDDCX_AO		1
#define SM6125_VDDCX_VFL	2
#define SM6125_VDDMX		3
#define SM6125_VDDMX_AO		4
#define SM6125_VDDMX_VFL	5

/* QCM2290 Power Domains */
#define QCM2290_VDDCX		0
#define QCM2290_VDDCX_AO	1
#define QCM2290_VDDCX_VFL	2
#define QCM2290_VDDMX		3
#define QCM2290_VDDMX_AO	4
#define QCM2290_VDDMX_VFL	5
#define QCM2290_VDD_LPI_CX	6
#define QCM2290_VDD_LPI_MX	7

/* RPM SMD Power Domain performance levels */
#define RPM_SMD_LEVEL_RETENTION       16
#define RPM_SMD_LEVEL_RETENTION_PLUS  32
#define RPM_SMD_LEVEL_MIN_SVS         48
#define RPM_SMD_LEVEL_LOW_SVS         64
#define RPM_SMD_LEVEL_SVS             128
#define RPM_SMD_LEVEL_SVS_PLUS        192
#define RPM_SMD_LEVEL_NOM             256
#define RPM_SMD_LEVEL_NOM_PLUS        320
#define RPM_SMD_LEVEL_TURBO           384
#define RPM_SMD_LEVEL_TURBO_NO_CPR    416
#define RPM_SMD_LEVEL_TURBO_HIGH      448
#define RPM_SMD_LEVEL_BINNING         512

#endif
