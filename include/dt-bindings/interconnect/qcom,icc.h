/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_ICC_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_ICC_H

#define QCOM_ICC_BUCKET_0		0
#define QCOM_ICC_BUCKET_1		1
#define QCOM_ICC_BUCKET_2		2
#define QCOM_ICC_BUCKET_3		3
#define QCOM_ICC_BUCKET_4		4
#define QCOM_ICC_NUM_BUCKETS		5

/*
 * The AMC bucket denotes constraints that are applied to hardware when
 * icc_set_bw() completes, whereas the WAKE and SLEEP constraints are applied
 * when the execution environment transitions between active and low power mode.
 */
#define QCOM_ICC_BUCKET_AMC		QCOM_ICC_BUCKET_0
#define QCOM_ICC_BUCKET_WAKE		QCOM_ICC_BUCKET_1
#define QCOM_ICC_BUCKET_SLEEP		QCOM_ICC_BUCKET_2

#define QCOM_ICC_TAG_AMC		(1 << QCOM_ICC_BUCKET_AMC)
#define QCOM_ICC_TAG_WAKE		(1 << QCOM_ICC_BUCKET_WAKE)
#define QCOM_ICC_TAG_SLEEP		(1 << QCOM_ICC_BUCKET_SLEEP)
#define QCOM_ICC_TAG_ACTIVE_ONLY	(QCOM_ICC_TAG_AMC | QCOM_ICC_TAG_WAKE)
#define QCOM_ICC_TAG_ALWAYS		(QCOM_ICC_TAG_AMC | QCOM_ICC_TAG_WAKE |\
					 QCOM_ICC_TAG_SLEEP)

#define QCOM_ICC_TAG_PWR_ST_0		(1 << QCOM_ICC_BUCKET_0)
#define QCOM_ICC_TAG_PWR_ST_1		(1 << QCOM_ICC_BUCKET_1)
#define QCOM_ICC_TAG_PWR_ST_2		(1 << QCOM_ICC_BUCKET_2)
#define QCOM_ICC_TAG_PWR_ST_3		(1 << QCOM_ICC_BUCKET_3)
#define QCOM_ICC_TAG_PWR_ST_4		(1 << QCOM_ICC_BUCKET_4)

/*
 * PERF_MODE indicates that each node in the requested path should use
 * performance-optimized settings if supported by the node.
 */
#define QCOM_ICC_TAG_PERF_MODE		(1 << 8)

#endif
