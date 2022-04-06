/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_QCOM_SPMI_VADC_GEN3_SMB139X_H
#define _DT_BINDINGS_QCOM_SPMI_VADC_GEN3_SMB139X_H

#ifndef SMB1394_1_SID
#define SMB1394_1_SID		0x09
#endif

#ifndef SMB1394_2_SID
#define SMB1394_2_SID		0x0b
#endif

#define SMB1394_1_ADC5_GEN3_SMB_TEMP		(SMB1394_1_SID << 8 | 0x06)
#define SMB1394_1_ADC5_GEN3_IIN_SMB		(SMB1394_1_SID << 8 | 0x19)
#define SMB1394_1_ADC5_GEN3_ICHG_SMB		(SMB1394_1_SID << 8 | 0x1b)

#define SMB1394_2_ADC5_GEN3_SMB_TEMP		(SMB1394_2_SID << 8 | 0x06)
#define SMB1394_2_ADC5_GEN3_IIN_SMB		(SMB1394_2_SID << 8 | 0x19)
#define SMB1394_2_ADC5_GEN3_ICHG_SMB		(SMB1394_2_SID << 8 | 0x1b)

#endif
