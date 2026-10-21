/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HiSilicon PCIe Trace and Tuning (PTT) support
 * Copyright (c) 2022 HiSilicon Technologies Co., Ltd.
 */

#ifndef INCLUDE__HISI_PTT_PKT_DECODER_H__
#define INCLUDE__HISI_PTT_PKT_DECODER_H__

#include <stddef.h>
#include <stdint.h>
#include <linux/bits.h>
#include <linux/bitfield.h>

#define HISI_PTT_8DW_CHECK_MASK		GENMASK(31, 11)
#define HISI_PTT_IS_8DW_PKT		GENMASK(31, 11)
#define HISI_PTT_MAX_SPACE_LEN		10
#define HISI_PTT_FIELD_LENGTH		4

/* Header DW0 fields for 4DW format */
#define HISI_PTT_HEAD0_4DW_TIME		GENMASK_U32(10, 0)
#define HISI_PTT_HEAD0_4DW_LEN		GENMASK_U32(20, 11)
#define HISI_PTT_HEAD0_4DW_SO		BIT_U32(21)
#define HISI_PTT_HEAD0_4DW_TH		BIT_U32(22)
#define HISI_PTT_HEAD0_4DW_T8		BIT_U32(23)
#define HISI_PTT_HEAD0_4DW_T9		BIT_U32(24)
#define HISI_PTT_HEAD0_4DW_TYPE		GENMASK_U32(29, 25)
#define HISI_PTT_HEAD0_4DW_FORMAT	GENMASK_U32(31, 30)

enum hisi_ptt_pkt_type {
	HISI_PTT_4DW_PKT,
	HISI_PTT_8DW_PKT,
	HISI_PTT_PKT_MAX
};

static int hisi_ptt_pkt_size[] = {
	[HISI_PTT_4DW_PKT]	= 16,
	[HISI_PTT_8DW_PKT]	= 32,
};

int hisi_ptt_pkt_desc(const unsigned char *buf, int pos, enum hisi_ptt_pkt_type type);

#endif
