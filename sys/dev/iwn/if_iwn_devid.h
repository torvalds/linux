/*-
 * Copyright (c) 2013 Cedric GROSS <cg@cgross.info>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */

#ifndef	__IF_IWN_DEVID_H__
#define	__IF_IWN_DEVID_H__

#define	IWN_HW_REV_TYPE_SHIFT	4
#define	IWN_HW_REV_TYPE_MASK	0x1f

/* Device revision */
#define	IWN_HW_REV_TYPE_4965	0
#define	IWN_HW_REV_TYPE_5300	2
#define	IWN_HW_REV_TYPE_5350	3
#define	IWN_HW_REV_TYPE_5150	4
#define	IWN_HW_REV_TYPE_5100	5
#define	IWN_HW_REV_TYPE_1000	6
#define	IWN_HW_REV_TYPE_6000	7
#define	IWN_HW_REV_TYPE_6050	8
#define	IWN_HW_REV_TYPE_6005	11
#define	IWN_HW_REV_TYPE_2030	12
#define	IWN_HW_REV_TYPE_2000	16
#define	IWN_HW_REV_TYPE_105	17
#define	IWN_HW_REV_TYPE_135	18

 /* ==========================================================================
 * DEVICE ID BLOCK
 * ==========================================================================
*/

/*
 * --------------------------------------------------------------------------
 * Device ID for 2x00 series
 * --------------------------------------------------------------------------
 */
#define	IWN_DID_2x00_1		0x0890
#define	IWN_DID_2x00_2		0x0891
/* SubDevice ID */
#define	IWN_SDID_2x00_1		0x4022
#define	IWN_SDID_2x00_2		0x4222
#define	IWN_SDID_2x00_3		0x4422
#define	IWN_SDID_2x00_4		0x4822

/*
 * --------------------------------------------------------------------------
 * Device ID for 2x30 series
 * --------------------------------------------------------------------------
 */
#define	IWN_DID_2x30_1		0x0887
#define	IWN_DID_2x30_2		0x0888
/* SubDevice ID */
#define	IWN_SDID_2x30_1		0x4062
#define	IWN_SDID_2x30_2		0x4262
#define	IWN_SDID_2x30_3		0x4462
#define	IWN_SDID_2x30_4		0x4066
#define	IWN_SDID_2x30_5		0x4266
#define	IWN_SDID_2x30_6		0x4466
/*
 * --------------------------------------------------------------------------
 * Device ID for 1000 series
 * --------------------------------------------------------------------------
 */
#define	IWN_DID_1000_1		0x0083
#define	IWN_DID_1000_2		0x0084
/* SubDevice ID */
#define	IWN_SDID_1000_1		0x1205
#define	IWN_SDID_1000_2		0x1305
#define	IWN_SDID_1000_3		0x1225
#define	IWN_SDID_1000_4		0x1325
#define	IWN_SDID_1000_5		0x1215
#define	IWN_SDID_1000_6		0x1315
#define	IWN_SDID_1000_7		0x1206
#define	IWN_SDID_1000_8		0x1306
#define	IWN_SDID_1000_9		0x1226
#define	IWN_SDID_1000_10	0x1326
#define	IWN_SDID_1000_11	0x1216
#define	IWN_SDID_1000_12	0x1316

/*
 * --------------------------------------------------------------------------
 * Device ID for 6x00 series
 * --------------------------------------------------------------------------
 */
#define	IWN_DID_6x00_1		0x422B
#define	IWN_DID_6x00_2		0x422C
#define	IWN_DID_6x00_3		0x4238
#define	IWN_DID_6x00_4		0x4239
/* SubDevice ID */
#define	IWN_SDID_6x00_1		0x1101
#define	IWN_SDID_6x00_2		0x1121
#define	IWN_SDID_6x00_3		0x1301
#define	IWN_SDID_6x00_4		0x1306
#define	IWN_SDID_6x00_5		0x1307
#define	IWN_SDID_6x00_6		0x1321
#define	IWN_SDID_6x00_7		0x1326
#define	IWN_SDID_6x00_8		0x1111
#define	IWN_SDID_6x00_9		0x1311
#define	IWN_SDID_6x00_10	0x1316
/*
 * --------------------------------------------------------------------------
 * Device ID for 6x05 series
 * --------------------------------------------------------------------------
 */
#define	IWN_DID_6x05_1		0x0082
#define	IWN_DID_6x05_2		0x0085
/* SubDevice ID */
#define	IWN_SDID_6x05_1		0x1301
#define	IWN_SDID_6x05_2		0x1306
#define	IWN_SDID_6x05_3		0x1307
#define	IWN_SDID_6x05_4		0x1321
#define	IWN_SDID_6x05_5		0x1326
#define	IWN_SDID_6x05_6		0x1311
#define	IWN_SDID_6x05_7		0x1316
#define	IWN_SDID_6x05_8		0xC020
#define	IWN_SDID_6x05_9		0xC220
#define	IWN_SDID_6x05_10	0x4820
#define	IWN_SDID_6x05_11	0x1304
#define	IWN_SDID_6x05_12	0x1305
/*
 * --------------------------------------------------------------------------
 * Device ID for 6050 WiFi/WiMax Series
 * --------------------------------------------------------------------------
 */
#define	IWN_DID_6050_1		0x0087
#define	IWN_DID_6050_2		0x0089
/* SubDevice ID */
#define	IWN_SDID_6050_1		0x1301
#define	IWN_SDID_6050_2		0x1306
#define	IWN_SDID_6050_3		0x1321
#define	IWN_SDID_6050_4		0x1326
#define	IWN_SDID_6050_5		0x1311
#define	IWN_SDID_6050_6		0x1316
/*
 * --------------------------------------------------------------------------
 * Device ID for 6150 Series
 * --------------------------------------------------------------------------
 */
#define	IWN_DID_6150_1		0x0885
#define	IWN_DID_6150_2		0x0886
/* SubDevice ID */
#define	IWN_SDID_6150_1		0x1305
#define	IWN_SDID_6150_2		0x1307
#define	IWN_SDID_6150_3		0x1325
#define	IWN_SDID_6150_4		0x1327
#define	IWN_SDID_6150_5		0x1315
#define	IWN_SDID_6150_6		0x1317
/*
 * --------------------------------------------------------------------------
 * Device ID for 6035 Series
 * --------------------------------------------------------------------------
 */
#define	IWN_DID_6035_1		0x088E
#define	IWN_DID_6035_2		0x088F
/* SubDevice ID */
#define	IWN_SDID_6035_1		0x4060
#define	IWN_SDID_6035_2		0x4260
#define	IWN_SDID_6035_3		0x4460
#define	IWN_SDID_6035_4		0x4860
#define	IWN_SDID_6035_5		0x5260
/*
 * --------------------------------------------------------------------------
 * Device ID for 1030 and 6030 Series
 * --------------------------------------------------------------------------
 */
#define	IWN_DID_x030_1		0x008A
#define	IWN_DID_x030_2		0x008B
#define	IWN_DID_x030_3		0x0090
#define	IWN_DID_x030_4		0x0091
/* SubDevice ID */
#define	IWN_SDID_x030_1		0x5305
#define	IWN_SDID_x030_2		0x5307
#define	IWN_SDID_x030_3		0x5325
#define	IWN_SDID_x030_4		0x5327
#define	IWN_SDID_x030_5		0x5315
#define	IWN_SDID_x030_6		0x5317
#define	IWN_SDID_x030_7		0x5211
#define	IWN_SDID_x030_8		0x5215
#define	IWN_SDID_x030_9		0x5216
#define	IWN_SDID_x030_10	0x5201
#define	IWN_SDID_x030_11	0x5205
#define	IWN_SDID_x030_12	0x5206
#define	IWN_SDID_x030_13	0x5207
#define	IWN_SDID_x030_14	0x5221
#define	IWN_SDID_x030_15	0x5225
#define	IWN_SDID_x030_16	0x5226
/*
 * --------------------------------------------------------------------------
 * Device ID for 130 Series
 * --------------------------------------------------------------------------
 */
#define	IWN_DID_130_1		0x0896
#define	IWN_DID_130_2		0x0897
/* SubDevice ID */
#define	IWN_SDID_130_1		0x5005
#define	IWN_SDID_130_2		0x5007
#define	IWN_SDID_130_3		0x5015
#define	IWN_SDID_130_4		0x5017
#define	IWN_SDID_130_5		0x5025
#define	IWN_SDID_130_6		0x5027

/*
 * --------------------------------------------------------------------------
 * Device ID for 100 Series
 * --------------------------------------------------------------------------
 */
#define	IWN_DID_100_1		0x08AE
#define	IWN_DID_100_2		0x08AF
/* SubDevice ID */
#define	IWN_SDID_100_1		0x1005
#define	IWN_SDID_100_2		0x1007
#define	IWN_SDID_100_3		0x1015
#define	IWN_SDID_100_4		0x1017
#define	IWN_SDID_100_5		0x1025
#define	IWN_SDID_100_6		0x1027

/*
 * --------------------------------------------------------------------------
 * Device ID for 105 Series
 * --------------------------------------------------------------------------
 */
#define	IWN_DID_105_1		0x0894
#define	IWN_DID_105_2		0x0895
/* SubDevice ID */
#define	IWN_SDID_105_1		0x0022
#define	IWN_SDID_105_2		0x0222
#define	IWN_SDID_105_3		0x0422
#define	IWN_SDID_105_4		0x0822

/*
 * --------------------------------------------------------------------------
 * Device ID for 135 Series
 * --------------------------------------------------------------------------
 */
#define	IWN_DID_135_1		0x0892
#define	IWN_DID_135_2		0x0893
/* SubDevice ID */
#define	IWN_SDID_135_1		0x0062
#define	IWN_SDID_135_2		0x0262
#define	IWN_SDID_135_3		0x0462

/*
 * --------------------------------------------------------------------------
 * Device ID for 5x00 Series
 * --------------------------------------------------------------------------
 */
#define	IWN_DID_5x00_1		0x4232
#define	IWN_DID_5x00_2		0x4237
#define	IWN_DID_5x00_3		0x4235
#define	IWN_DID_5x00_4		0x4236
/* SubDevice ID */
#define	IWN_SDID_5x00_1		0x1201
#define	IWN_SDID_5x00_2		0x1301
#define	IWN_SDID_5x00_3		0x1204
#define	IWN_SDID_5x00_4		0x1304
#define	IWN_SDID_5x00_5		0x1205
#define	IWN_SDID_5x00_6		0x1305
#define	IWN_SDID_5x00_7		0x1206
#define	IWN_SDID_5x00_8		0x1306
#define	IWN_SDID_5x00_9		0x1221
#define	IWN_SDID_5x00_10		0x1321
#define	IWN_SDID_5x00_11		0x1224
#define	IWN_SDID_5x00_12		0x1324
#define	IWN_SDID_5x00_13		0x1225
#define	IWN_SDID_5x00_14		0x1325
#define	IWN_SDID_5x00_15		0x1226
#define	IWN_SDID_5x00_16		0x1326
#define	IWN_SDID_5x00_17		0x1211
#define	IWN_SDID_5x00_18		0x1311
#define	IWN_SDID_5x00_19		0x1214
#define	IWN_SDID_5x00_20		0x1314
#define	IWN_SDID_5x00_21		0x1215
#define	IWN_SDID_5x00_22		0x1315
#define	IWN_SDID_5x00_23		0x1216
#define	IWN_SDID_5x00_24		0x1316
#define	IWN_SDID_5x00_25		0x1021
#define	IWN_SDID_5x00_26		0x1121
#define	IWN_SDID_5x00_27		0x1024
#define	IWN_SDID_5x00_28		0x1124
#define	IWN_SDID_5x00_29		0x1001
#define	IWN_SDID_5x00_30		0x1101
#define	IWN_SDID_5x00_31		0x1004
#define	IWN_SDID_5x00_32		0x1104
#define	IWN_SDID_5x00_33		0x1011
#define	IWN_SDID_5x00_34		0x1111
#define	IWN_SDID_5x00_35		0x1014
#define	IWN_SDID_5x00_36		0x1114
/*
 * --------------------------------------------------------------------------
 * Device ID for 5x50 Series
 * --------------------------------------------------------------------------
 */
#define	IWN_DID_5x50_1		0x423A
#define	IWN_DID_5x50_2		0x423B
#define	IWN_DID_5x50_3		0x423C
#define	IWN_DID_5x50_4		0x423D
/* SubDevice ID */
#define	IWN_SDID_5x50_1		0x1001
#define	IWN_SDID_5x50_2		0x1021
#define	IWN_SDID_5x50_3		0x1011
#define	IWN_SDID_5x50_4		0x1201
#define	IWN_SDID_5x50_5		0x1301
#define	IWN_SDID_5x50_6		0x1206
#define	IWN_SDID_5x50_7		0x1306
#define	IWN_SDID_5x50_8		0x1221
#define	IWN_SDID_5x50_9		0x1321
#define	IWN_SDID_5x50_10		0x1211
#define	IWN_SDID_5x50_11		0x1311
#define	IWN_SDID_5x50_12		0x1216
#define	IWN_SDID_5x50_13		0x1316
/*
 * --------------------------------------------------------------------------
 * Device ID for 4965 Series
 * --------------------------------------------------------------------------
 */
#define	IWN_DID_4965_1		0x4229
#define	IWN_DID_4965_2		0x422d
#define	IWN_DID_4965_3		0x4230
#define	IWN_DID_4965_4		0x4233

#endif	/* ! __IF_IWN_DEVID_H__ */
