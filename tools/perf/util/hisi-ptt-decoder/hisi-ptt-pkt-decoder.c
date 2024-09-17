// SPDX-License-Identifier: GPL-2.0
/*
 * HiSilicon PCIe Trace and Tuning (PTT) support
 * Copyright (c) 2022 HiSilicon Technologies Co., Ltd.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <endian.h>
#include <byteswap.h>
#include <linux/bitops.h>
#include <stdarg.h>

#include "../color.h"
#include "hisi-ptt-pkt-decoder.h"

/*
 * For 8DW format, the bit[31:11] of DW0 is always 0x1fffff, which can be
 * used to distinguish the data format.
 * 8DW format is like:
 *   bits [                 31:11                 ][       10:0       ]
 *        |---------------------------------------|-------------------|
 *    DW0 [                0x1fffff               ][ Reserved (0x7ff) ]
 *    DW1 [                       Prefix                              ]
 *    DW2 [                     Header DW0                            ]
 *    DW3 [                     Header DW1                            ]
 *    DW4 [                     Header DW2                            ]
 *    DW5 [                     Header DW3                            ]
 *    DW6 [                   Reserved (0x0)                          ]
 *    DW7 [                        Time                               ]
 *
 * 4DW format is like:
 *   bits [31:30] [ 29:25 ][24][23][22][21][    20:11   ][    10:0    ]
 *        |-----|---------|---|---|---|---|-------------|-------------|
 *    DW0 [ Fmt ][  Type  ][T9][T8][TH][SO][   Length   ][    Time    ]
 *    DW1 [                     Header DW1                            ]
 *    DW2 [                     Header DW2                            ]
 *    DW3 [                     Header DW3                            ]
 */

enum hisi_ptt_8dw_pkt_field_type {
	HISI_PTT_8DW_CHK_AND_RSV0,
	HISI_PTT_8DW_PREFIX,
	HISI_PTT_8DW_HEAD0,
	HISI_PTT_8DW_HEAD1,
	HISI_PTT_8DW_HEAD2,
	HISI_PTT_8DW_HEAD3,
	HISI_PTT_8DW_RSV1,
	HISI_PTT_8DW_TIME,
	HISI_PTT_8DW_TYPE_MAX
};

enum hisi_ptt_4dw_pkt_field_type {
	HISI_PTT_4DW_HEAD1,
	HISI_PTT_4DW_HEAD2,
	HISI_PTT_4DW_HEAD3,
	HISI_PTT_4DW_TYPE_MAX
};

static const char * const hisi_ptt_8dw_pkt_field_name[] = {
	[HISI_PTT_8DW_PREFIX]	= "Prefix",
	[HISI_PTT_8DW_HEAD0]	= "Header DW0",
	[HISI_PTT_8DW_HEAD1]	= "Header DW1",
	[HISI_PTT_8DW_HEAD2]	= "Header DW2",
	[HISI_PTT_8DW_HEAD3]	= "Header DW3",
	[HISI_PTT_8DW_TIME]	= "Time"
};

static const char * const hisi_ptt_4dw_pkt_field_name[] = {
	[HISI_PTT_4DW_HEAD1]	= "Header DW1",
	[HISI_PTT_4DW_HEAD2]	= "Header DW2",
	[HISI_PTT_4DW_HEAD3]	= "Header DW3",
};

union hisi_ptt_4dw {
	struct {
		uint32_t format : 2;
		uint32_t type : 5;
		uint32_t t9 : 1;
		uint32_t t8 : 1;
		uint32_t th : 1;
		uint32_t so : 1;
		uint32_t len : 10;
		uint32_t time : 11;
	};
	uint32_t value;
};

static void hisi_ptt_print_pkt(const unsigned char *buf, int pos, const char *desc)
{
	const char *color = PERF_COLOR_BLUE;
	int i;

	printf(".");
	color_fprintf(stdout, color, "  %08x: ", pos);
	for (i = 0; i < HISI_PTT_FIELD_LENTH; i++)
		color_fprintf(stdout, color, "%02x ", buf[pos + i]);
	for (i = 0; i < HISI_PTT_MAX_SPACE_LEN; i++)
		color_fprintf(stdout, color, "   ");
	color_fprintf(stdout, color, "  %s\n", desc);
}

static int hisi_ptt_8dw_kpt_desc(const unsigned char *buf, int pos)
{
	int i;

	for (i = 0; i < HISI_PTT_8DW_TYPE_MAX; i++) {
		/* Do not show 8DW check field and reserved fields */
		if (i == HISI_PTT_8DW_CHK_AND_RSV0 || i == HISI_PTT_8DW_RSV1) {
			pos += HISI_PTT_FIELD_LENTH;
			continue;
		}

		hisi_ptt_print_pkt(buf, pos, hisi_ptt_8dw_pkt_field_name[i]);
		pos += HISI_PTT_FIELD_LENTH;
	}

	return hisi_ptt_pkt_size[HISI_PTT_8DW_PKT];
}

static void hisi_ptt_4dw_print_dw0(const unsigned char *buf, int pos)
{
	const char *color = PERF_COLOR_BLUE;
	union hisi_ptt_4dw dw0;
	int i;

	dw0.value = *(uint32_t *)(buf + pos);
	printf(".");
	color_fprintf(stdout, color, "  %08x: ", pos);
	for (i = 0; i < HISI_PTT_FIELD_LENTH; i++)
		color_fprintf(stdout, color, "%02x ", buf[pos + i]);
	for (i = 0; i < HISI_PTT_MAX_SPACE_LEN; i++)
		color_fprintf(stdout, color, "   ");

	color_fprintf(stdout, color,
		      "  %s %x %s %x %s %x %s %x %s %x %s %x %s %x %s %x\n",
		      "Format", dw0.format, "Type", dw0.type, "T9", dw0.t9,
		      "T8", dw0.t8, "TH", dw0.th, "SO", dw0.so, "Length",
		      dw0.len, "Time", dw0.time);
}

static int hisi_ptt_4dw_kpt_desc(const unsigned char *buf, int pos)
{
	int i;

	hisi_ptt_4dw_print_dw0(buf, pos);
	pos += HISI_PTT_FIELD_LENTH;

	for (i = 0; i < HISI_PTT_4DW_TYPE_MAX; i++) {
		hisi_ptt_print_pkt(buf, pos, hisi_ptt_4dw_pkt_field_name[i]);
		pos += HISI_PTT_FIELD_LENTH;
	}

	return hisi_ptt_pkt_size[HISI_PTT_4DW_PKT];
}

int hisi_ptt_pkt_desc(const unsigned char *buf, int pos, enum hisi_ptt_pkt_type type)
{
	if (type == HISI_PTT_8DW_PKT)
		return hisi_ptt_8dw_kpt_desc(buf, pos);

	return hisi_ptt_4dw_kpt_desc(buf, pos);
}
