/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __QCOM_SMD_RPM_H__
#define __QCOM_SMD_RPM_H__

struct qcom_smd_rpm;

#define QCOM_SMD_RPM_ACTIVE_STATE        0
#define QCOM_SMD_RPM_SLEEP_STATE         1

/*
 * Constants used for addressing resources in the RPM.
 */
#define QCOM_SMD_RPM_BBYB	0x62796262
#define QCOM_SMD_RPM_BOBB	0x62626f62
#define QCOM_SMD_RPM_BOOST	0x61747362
#define QCOM_SMD_RPM_BUS_CLK	0x316b6c63
#define QCOM_SMD_RPM_BUS_MASTER	0x73616d62
#define QCOM_SMD_RPM_BUS_SLAVE	0x766c7362
#define QCOM_SMD_RPM_CLK_BUF_A	0x616B6C63
#define QCOM_SMD_RPM_LDOA	0x616f646c
#define QCOM_SMD_RPM_LDOB	0x626F646C
#define QCOM_SMD_RPM_MEM_CLK	0x326b6c63
#define QCOM_SMD_RPM_MISC_CLK	0x306b6c63
#define QCOM_SMD_RPM_NCPA	0x6170636E
#define QCOM_SMD_RPM_NCPB	0x6270636E
#define QCOM_SMD_RPM_OCMEM_PWR	0x706d636f
#define QCOM_SMD_RPM_QPIC_CLK	0x63697071
#define QCOM_SMD_RPM_SMPA	0x61706d73
#define QCOM_SMD_RPM_SMPB	0x62706d73
#define QCOM_SMD_RPM_SPDM	0x63707362
#define QCOM_SMD_RPM_VSA	0x00617376
#define QCOM_SMD_RPM_MMAXI_CLK	0x69786d6d
#define QCOM_SMD_RPM_IPA_CLK	0x617069
#define QCOM_SMD_RPM_CE_CLK	0x6563
#define QCOM_SMD_RPM_AGGR_CLK	0x72676761

int qcom_rpm_smd_write(struct qcom_smd_rpm *rpm,
		       int state,
		       u32 resource_type, u32 resource_id,
		       void *buf, size_t count);

#endif
