/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, The Linux Foundation. All rights reserved. */

#ifndef _DT_BINDINGS_POWER_QCOM_RPMPD_H
#define _DT_BINDINGS_POWER_QCOM_RPMPD_H

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

/* SDM845 Power Domain performance levels */
#define RPMH_REGULATOR_LEVEL_RETENTION	16
#define RPMH_REGULATOR_LEVEL_MIN_SVS	48
#define RPMH_REGULATOR_LEVEL_LOW_SVS	64
#define RPMH_REGULATOR_LEVEL_SVS	128
#define RPMH_REGULATOR_LEVEL_SVS_L1	192
#define RPMH_REGULATOR_LEVEL_NOM	256
#define RPMH_REGULATOR_LEVEL_NOM_L1	320
#define RPMH_REGULATOR_LEVEL_NOM_L2	336
#define RPMH_REGULATOR_LEVEL_TURBO	384
#define RPMH_REGULATOR_LEVEL_TURBO_L1	416

/* MSM8976 Power Domain Indexes */
#define MSM8976_VDDCX		0
#define MSM8976_VDDCX_AO	1
#define MSM8976_VDDCX_VFL	2
#define MSM8976_VDDMX		3
#define MSM8976_VDDMX_AO	4
#define MSM8976_VDDMX_VFL	5

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
