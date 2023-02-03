/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Linaro Ltd
 * Author: Krzysztof Kozlowski <krzk@kernel.org> based on previous work of Kumar Gala.
 */
#ifndef _DT_BINDINGS_ARM_QCOM_IDS_H
#define _DT_BINDINGS_ARM_QCOM_IDS_H

/*
 * The MSM chipset and hardware revision used by Qualcomm bootloaders, DTS for
 * older chipsets (qcom,msm-id) and in socinfo driver:
 */
#define QCOM_ID_MSM8960			87
#define QCOM_ID_APQ8064			109
#define QCOM_ID_MSM8660A		122
#define QCOM_ID_MSM8260A		123
#define QCOM_ID_APQ8060A		124
#define QCOM_ID_MSM8974			126
#define QCOM_ID_MPQ8064			130
#define QCOM_ID_MSM8960AB		138
#define QCOM_ID_APQ8060AB		139
#define QCOM_ID_MSM8260AB		140
#define QCOM_ID_MSM8660AB		141
#define QCOM_ID_MSM8626			145
#define QCOM_ID_MSM8610			147
#define QCOM_ID_APQ8064AB		153
#define QCOM_ID_MSM8226			158
#define QCOM_ID_MSM8526			159
#define QCOM_ID_MSM8110			161
#define QCOM_ID_MSM8210			162
#define QCOM_ID_MSM8810			163
#define QCOM_ID_MSM8212			164
#define QCOM_ID_MSM8612			165
#define QCOM_ID_MSM8112			166
#define QCOM_ID_MSM8225Q		168
#define QCOM_ID_MSM8625Q		169
#define QCOM_ID_MSM8125Q		170
#define QCOM_ID_APQ8064AA		172
#define QCOM_ID_APQ8084			178
#define QCOM_ID_APQ8074			184
#define QCOM_ID_MSM8274			185
#define QCOM_ID_MSM8674			186
#define QCOM_ID_MSM8974PRO_AC		194
#define QCOM_ID_MSM8126			198
#define QCOM_ID_APQ8026			199
#define QCOM_ID_MSM8926			200
#define QCOM_ID_MSM8326			205
#define QCOM_ID_MSM8916			206
#define QCOM_ID_MSM8994			207
#define QCOM_ID_APQ8074PRO_AA		208
#define QCOM_ID_APQ8074PRO_AB		209
#define QCOM_ID_APQ8074PRO_AC		210
#define QCOM_ID_MSM8274PRO_AA		211
#define QCOM_ID_MSM8274PRO_AB		212
#define QCOM_ID_MSM8274PRO_AC		213
#define QCOM_ID_MSM8674PRO_AA		214
#define QCOM_ID_MSM8674PRO_AB		215
#define QCOM_ID_MSM8674PRO_AC		216
#define QCOM_ID_MSM8974PRO_AA		217
#define QCOM_ID_MSM8974PRO_AB		218
#define QCOM_ID_APQ8028			219
#define QCOM_ID_MSM8128			220
#define QCOM_ID_MSM8228			221
#define QCOM_ID_MSM8528			222
#define QCOM_ID_MSM8628			223
#define QCOM_ID_MSM8928			224
#define QCOM_ID_MSM8510			225
#define QCOM_ID_MSM8512			226
#define QCOM_ID_MSM8936			233
#define QCOM_ID_MSM8939			239
#define QCOM_ID_APQ8036			240
#define QCOM_ID_APQ8039			241
#define QCOM_ID_MSM8996			246
#define QCOM_ID_APQ8016			247
#define QCOM_ID_MSM8216			248
#define QCOM_ID_MSM8116			249
#define QCOM_ID_MSM8616			250
#define QCOM_ID_MSM8992			251
#define QCOM_ID_APQ8094			253
#define QCOM_ID_MSM8956			266
#define QCOM_ID_MSM8976			278
#define QCOM_ID_MDM9607			290
#define QCOM_ID_APQ8096			291
#define QCOM_ID_MSM8998			292
#define QCOM_ID_MSM8953			293
#define QCOM_ID_MDM8207			296
#define QCOM_ID_MDM9207			297
#define QCOM_ID_MDM9307			298
#define QCOM_ID_MDM9628			299
#define QCOM_ID_APQ8053			304
#define QCOM_ID_MSM8996SG		305
#define QCOM_ID_MSM8996AU		310
#define QCOM_ID_APQ8096AU		311
#define QCOM_ID_APQ8096SG		312
#define QCOM_ID_SDM660			317
#define QCOM_ID_SDM630			318
#define QCOM_ID_APQ8098			319
#define QCOM_ID_SDM845			321
#define QCOM_ID_MDM9206			322
#define QCOM_ID_IPQ8074			323
#define QCOM_ID_SDA660			324
#define QCOM_ID_SDM658			325
#define QCOM_ID_SDA658			326
#define QCOM_ID_SDA630			327
#define QCOM_ID_SDM450			338
#define QCOM_ID_SM8150			339
#define QCOM_ID_SDA845			341
#define QCOM_ID_IPQ8072			342
#define QCOM_ID_IPQ8076			343
#define QCOM_ID_IPQ8078			344
#define QCOM_ID_SDM636			345
#define QCOM_ID_SDA636			346
#define QCOM_ID_SDM632			349
#define QCOM_ID_SDA632			350
#define QCOM_ID_SDA450			351
#define QCOM_ID_SM8250			356
#define QCOM_ID_SA8155			362
#define QCOM_ID_IPQ8070			375
#define QCOM_ID_IPQ8071			376
#define QCOM_ID_IPQ8072A		389
#define QCOM_ID_IPQ8074A		390
#define QCOM_ID_IPQ8076A		391
#define QCOM_ID_IPQ8078A		392
#define QCOM_ID_SM6125			394
#define QCOM_ID_IPQ8070A		395
#define QCOM_ID_IPQ8071A		396
#define QCOM_ID_IPQ6018			402
#define QCOM_ID_IPQ6028			403
#define QCOM_ID_SM4250			417
#define QCOM_ID_IPQ6000			421
#define QCOM_ID_IPQ6010			422
#define QCOM_ID_SC7180			425
#define QCOM_ID_SM6350			434
#define QCOM_ID_SM8350			439
#define QCOM_ID_SM6115			444
#define QCOM_ID_SC8280XP		449
#define QCOM_ID_IPQ6005			453
#define QCOM_ID_QRB5165			455
#define QCOM_ID_SM8450			457
#define QCOM_ID_SM7225			459
#define QCOM_ID_SA8295P			460
#define QCOM_ID_SA8540P			461
#define QCOM_ID_QCM4290			469
#define QCOM_ID_QCS4290			470
#define QCOM_ID_SM8450_2		480
#define QCOM_ID_SM8450_3		482
#define QCOM_ID_SC7280			487
#define QCOM_ID_SC7180P			495
#define QCOM_ID_SM6375			507
#define QCOM_ID_SM8550			519
#define QCOM_ID_QRU1000			539
#define QCOM_ID_QDU1000			545
#define QCOM_ID_QDU1010			587
#define QCOM_ID_QRU1032			588
#define QCOM_ID_QRU1052			589
#define QCOM_ID_QRU1062			590

/*
 * The board type and revision information, used by Qualcomm bootloaders and
 * DTS for older chipsets (qcom,board-id):
 */
#define QCOM_BOARD_ID(a, major, minor) \
	(((major & 0xff) << 16) | ((minor & 0xff) << 8) | QCOM_BOARD_ID_##a)

#define QCOM_BOARD_ID_MTP			8
#define QCOM_BOARD_ID_DRAGONBOARD		10
#define QCOM_BOARD_ID_SBC			24

#endif /* _DT_BINDINGS_ARM_QCOM_IDS_H */
