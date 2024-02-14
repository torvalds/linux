/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DT_BINDINGS_QCOM_APR_H
#define __DT_BINDINGS_QCOM_APR_H

/* Domain IDs */
#define APR_DOMAIN_SIM		0x1
#define APR_DOMAIN_PC		0x2
#define APR_DOMAIN_MODEM	0x3
#define APR_DOMAIN_ADSP		0x4
#define APR_DOMAIN_APPS		0x5
#define APR_DOMAIN_MAX		0x6

/* ADSP service IDs */
#define APR_SVC_ADSP_CORE	0x3
#define APR_SVC_AFE		0x4
#define APR_SVC_VSM		0x5
#define APR_SVC_VPM		0x6
#define APR_SVC_ASM		0x7
#define APR_SVC_ADM		0x8
#define APR_SVC_ADSP_MVM	0x09
#define APR_SVC_ADSP_CVS	0x0A
#define APR_SVC_ADSP_CVP	0x0B
#define APR_SVC_USM		0x0C
#define APR_SVC_LSM		0x0D
#define APR_SVC_VIDC		0x16
#define APR_SVC_MAX		0x17

#endif /* __DT_BINDINGS_QCOM_APR_H */
