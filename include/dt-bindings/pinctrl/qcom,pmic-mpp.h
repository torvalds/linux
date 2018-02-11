/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides constants for the Qualcomm PMIC's
 * Multi-Purpose Pin binding.
 */

#ifndef _DT_BINDINGS_PINCTRL_QCOM_PMIC_MPP_H
#define _DT_BINDINGS_PINCTRL_QCOM_PMIC_MPP_H

/* power-source */

/* Digital Input/Output: level [PM8058] */
#define PM8058_MPP_VPH			0
#define PM8058_MPP_S3			1
#define PM8058_MPP_L2			2
#define PM8058_MPP_L3			3

/* Digital Input/Output: level [PM8901] */
#define PM8901_MPP_MSMIO		0
#define PM8901_MPP_DIG			1
#define PM8901_MPP_L5			2
#define PM8901_MPP_S4			3
#define PM8901_MPP_VPH			4

/* Digital Input/Output: level [PM8921] */
#define PM8921_MPP_S4			1
#define PM8921_MPP_L15			3
#define PM8921_MPP_L17			4
#define PM8921_MPP_VPH			7

/* Digital Input/Output: level [PM8821] */
#define PM8821_MPP_1P8			0
#define PM8821_MPP_VPH			7

/* Digital Input/Output: level [PM8018] */
#define PM8018_MPP_L4			0
#define PM8018_MPP_L14			1
#define PM8018_MPP_S3			2
#define PM8018_MPP_L6			3
#define PM8018_MPP_L2			4
#define PM8018_MPP_L5			5
#define PM8018_MPP_VPH			7

/* Digital Input/Output: level [PM8038] */
#define PM8038_MPP_L20			0
#define PM8038_MPP_L11			1
#define PM8038_MPP_L5			2
#define PM8038_MPP_L15			3
#define PM8038_MPP_L17			4
#define PM8038_MPP_VPH			7

#define PM8841_MPP_VPH			0
#define PM8841_MPP_S3			2

#define PM8916_MPP_VPH			0
#define PM8916_MPP_L2			2
#define PM8916_MPP_L5			3

#define PM8941_MPP_VPH			0
#define PM8941_MPP_L1			1
#define PM8941_MPP_S3			2
#define PM8941_MPP_L6			3

#define PMA8084_MPP_VPH			0
#define PMA8084_MPP_L1			1
#define PMA8084_MPP_S4			2
#define PMA8084_MPP_L6			3

#define PM8994_MPP_VPH			0
/* Only supported for MPP_05-MPP_08 */
#define PM8994_MPP_L19			1
#define PM8994_MPP_S4			2
#define PM8994_MPP_L12			3

/*
 * Analog Input - Set the source for analog input.
 * To be used with "qcom,amux-route" property
 */
#define PMIC_MPP_AMUX_ROUTE_CH5		0
#define PMIC_MPP_AMUX_ROUTE_CH6		1
#define PMIC_MPP_AMUX_ROUTE_CH7		2
#define PMIC_MPP_AMUX_ROUTE_CH8		3
#define PMIC_MPP_AMUX_ROUTE_ABUS1	4
#define PMIC_MPP_AMUX_ROUTE_ABUS2	5
#define PMIC_MPP_AMUX_ROUTE_ABUS3	6
#define PMIC_MPP_AMUX_ROUTE_ABUS4	7

/* Analog Output: level */
#define PMIC_MPP_AOUT_LVL_1V25		0
#define PMIC_MPP_AOUT_LVL_1V25_2	1
#define PMIC_MPP_AOUT_LVL_0V625		2
#define PMIC_MPP_AOUT_LVL_0V3125	3
#define PMIC_MPP_AOUT_LVL_MPP		4
#define PMIC_MPP_AOUT_LVL_ABUS1		5
#define PMIC_MPP_AOUT_LVL_ABUS2		6
#define PMIC_MPP_AOUT_LVL_ABUS3		7

/* To be used with "function" */
#define PMIC_MPP_FUNC_NORMAL		"normal"
#define PMIC_MPP_FUNC_PAIRED		"paired"
#define PMIC_MPP_FUNC_DTEST1		"dtest1"
#define PMIC_MPP_FUNC_DTEST2		"dtest2"
#define PMIC_MPP_FUNC_DTEST3		"dtest3"
#define PMIC_MPP_FUNC_DTEST4		"dtest4"

#endif
