/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <dt-bindings/thermal/thermal.h>

#ifndef _DT_BINDINGS_QTI_THERMAL_H
#define _DT_BINDINGS_QTI_THERMAL_H

#define THERMAL_MAX_LIMIT	(THERMAL_NO_LIMIT - 1)
#define AGGREGATE_COEFF_VALUE	0
#define AGGREGATE_MAX_VALUE	1
#define AGGREGATE_MIN_VALUE	2

#define QMI_PA			0
#define QMI_PA_1		1
#define QMI_PA_2		2
#define QMI_QFE_PA_0		3
#define QMI_QFE_WTR_0		4
#define QMI_MODEM_TSENS		5
#define QMI_QFE_MMW_0		6
#define QMI_QFE_MMW_1		7
#define QMI_QFE_MMW_2		8
#define QMI_QFE_MMW_3		9
#define QMI_XO_THERM		10
#define QMI_QFE_PA_MDM		11
#define QMI_QFE_PA_WTR		12
#define QMI_QFE_MMW_STREAMER_0	13
#define QMI_QFE_MMW_0_MOD	14
#define QMI_QFE_MMW_1_MOD	15
#define QMI_QFE_MMW_2_MOD	16
#define QMI_QFE_MMW_3_MOD	17
#define QMI_QFE_RET_PA_0	18
#define QMI_QFE_WTR_PA_0	19
#define QMI_QFE_WTR_PA_1	20
#define QMI_QFE_WTR_PA_2	21
#define QMI_QFE_WTR_PA_3	22
#define QMI_SYS_THERM_1		23
#define QMI_SYS_THERM_2		24
#define QMI_MODEM_TSENS_1	25
#define QMI_MMW_PA1		26
#define QMI_MMW_PA2		27
#define QMI_MMW_PA3		28
#define QMI_SDR_MMW		29
#define QMI_QTM_THERM		30
#define QMI_BCL_WARN		31
#define QMI_SDR0_PA0		32
#define QMI_SDR0_PA1		33
#define QMI_SDR0_PA2		34
#define QMI_SDR0_PA3		35
#define QMI_SDR0_PA4		36
#define QMI_SDR0_PA5		37
#define QMI_SDR0		38
#define QMI_SDR1_PA0		39
#define QMI_SDR1_PA1		40
#define QMI_SDR1_PA2		41
#define QMI_SDR1_PA3		42
#define QMI_SDR1_PA4		43
#define QMI_SDR1_PA5		44
#define QMI_SDR1		45
#define QMI_MMW0		46
#define QMI_MMW1		47
#define QMI_MMW2		48
#define QMI_MMW3		49
#define QMI_MMW_IFIC0		50
#define QMI_SUB1_MODEM_CFG	51
#define QMI_SUB1_LTE_CC		52
#define QMI_SUB1_MCG_FR1_CC	53
#define QMI_SUB1_MCG_FR2_CC	54
#define QMI_SUB1_SCG_FR1_CC	55
#define QMI_SUB1_SCG_FR2_CC	56
#define QMI_SUB2_MODEM_CFG	57
#define QMI_SUB2_LTE_CC		58
#define QMI_SUB2_MCG_FR1_CC	59
#define QMI_SUB2_MCG_FR2_CC	60
#define QMI_SUB2_SCG_FR1_CC	61
#define QMI_SUB2_SCG_FR2_CC	62
#define QMI_NSP_ISENSE_TRIM	63
#define QMI_EPM0		64
#define QMI_EPM1		65
#define QMI_EPM2		66
#define QMI_EPM3		67
#define QMI_EPM4		68
#define QMI_EPM5		69
#define QMI_EPM6		70
#define QMI_EPM7		71
#define QMI_SDR0_PA		72
#define QMI_SDR1_PA		73
#define QMI_SUB0_SDR0_PA	74
#define QMI_SUB1_SDR0_PA	75
#define QMI_SYS_THERM_3		76
#define QMI_SYS_THERM_4		77
#define QMI_SYS_THERM_5		78
#define QMI_SYS_THERM_6		79
#define QMI_BEAMER_N_THERM		80
#define QMI_BEAMER_E_THERM		81
#define QMI_BEAMER_W_THERM		82

#define QMI_MODEM_INST_ID	0x0
#define QMI_ADSP_INST_ID	0x1
#define QMI_CDSP_INST_ID	0x43
#define QMI_SLPI_INST_ID	0x53
#define QMI_MODEM_NR_INST_ID	0x64

#endif
