/*
 * This header provides constants for the Qualcomm PMIC's
 * Multi-Purpose Pin binding.
 */

#ifndef _DT_BINDINGS_PINCTRL_QCOM_PMIC_MPP_H
#define _DT_BINDINGS_PINCTRL_QCOM_PMIC_MPP_H

/* power-source */
#define PM8841_MPP_VPH			0
#define PM8841_MPP_S3			2

#define PM8941_MPP_VPH			0
#define PM8941_MPP_L1			1
#define PM8941_MPP_S3			2
#define PM8941_MPP_L6			3

#define PMA8084_MPP_VPH			0
#define PMA8084_MPP_L1			1
#define PMA8084_MPP_S4			2
#define PMA8084_MPP_L6			3

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

/* To be used with "function" */
#define PMIC_MPP_FUNC_NORMAL		"normal"
#define PMIC_MPP_FUNC_PAIRED		"paired"
#define PMIC_MPP_FUNC_DTEST1		"dtest1"
#define PMIC_MPP_FUNC_DTEST2		"dtest2"
#define PMIC_MPP_FUNC_DTEST3		"dtest3"
#define PMIC_MPP_FUNC_DTEST4		"dtest4"

#endif
