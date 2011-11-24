// Copyright (c) 2004-2006 Atheros Communications Inc.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Portions of this code were developed with information supplied from the 
// SD Card Association Simplified Specifications. The following conditions and disclaimers may apply:
//
//  The following conditions apply to the release of the SD simplified specification (“Simplified
//  Specification”) by the SD Card Association. The Simplified Specification is a subset of the complete 
//  SD Specification which is owned by the SD Card Association. This Simplified Specification is provided 
//  on a non-confidential basis subject to the disclaimers below. Any implementation of the Simplified 
//  Specification may require a license from the SD Card Association or other third parties.
//  Disclaimers:
//  The information contained in the Simplified Specification is presented only as a standard 
//  specification for SD Cards and SD Host/Ancillary products and is provided "AS-IS" without any 
//  representations or warranties of any kind. No responsibility is assumed by the SD Card Association for 
//  any damages, any infringements of patents or other right of the SD Card Association or any third 
//  parties, which may result from its use. No license is granted by implication, estoppel or otherwise 
//  under any patent or other rights of the SD Card Association or any third party. Nothing herein shall 
//  be construed as an obligation by the SD Card Association to disclose or distribute any technical 
//  information, know-how or other confidential information to any third party.
//
//
// The initial developers of the original code are Seung Yi and Paul Lever
//
// sdio@atheros.com
//
//

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
@file: mmc_defs.h

@abstract: MMC definitions not already defined in _sdio_defs.h
 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#ifndef ___MMC_DEFS_H___
#define ___MMC_DEFS_H___

#define MMC_MAX_BUS_CLOCK    20000000 /* max clock speed in hz */
#define MMC_HS_MAX_BUS_CLOCK 52000000 /* MMC PLUS (high speed) max clock rate in hz */

/* R2 (CSD) macros */
#define GET_MMC_CSD_TRANS_SPEED(pR) (pR)[12]
#define GET_MMC_SPEC_VERSION(pR)    (((pR)[15] >> 2) & 0x0F)
#define MMC_SPEC_1_0_TO_1_2         0x00
#define MMC_SPEC_1_4                0x01
#define MMC_SPEC_2_0_TO_2_2         0x02
#define MMC_SPEC_3_1                0x03
#define MMC_SPEC_4_0_TO_4_1         0x04

#define MMC_CMD_SWITCH    6
#define MMC_CMD8    8

#define MMC_SWITCH_CMD_SET    0
#define MMC_SWITCH_SET_BITS   1
#define MMC_SWITCH_CLEAR_BITS 2
#define MMC_SWITCH_WRITE_BYTE 3
#define MMC_SWITCH_CMD_SET0   0
#define MMC_SWITCH_BUILD_ARG(cmdset,access,index,value) \
     (((cmdset) & 0x07) | (((access) & 0x03) << 24) | (((index) & 0xFF) << 16) | (((value) & 0xFF) << 8)) 

#define MMC_EXT_CSD_SIZE                     512

#define MMC_EXT_S_CMD_SET_OFFSET             504
#define MMC_EXT_MIN_PERF_W_8_52_OFFSET       210  
#define MMC_EXT_MIN_PERF_R_8_52_OFFSET       209
#define MMC_EXT_MIN_PERF_W_8_26_4_52_OFFSET  208
#define MMC_EXT_MIN_PERF_R_8_26_4_52_OFFSET  207
#define MMC_EXT_MIN_PERF_W_4_26_OFFSET       206
#define MMC_EXT_MIN_PERF_R_4_56_OFFSET       205  
#define MMC_EXT_PWR_CL_26_360_OFFSET         203
#define MMC_EXT_PWR_CL_52_360_OFFSET         202
#define MMC_EXT_PWR_CL_26_195_OFFSET         201
#define MMC_EXT_PWR_CL_52_195_OFFSET         200
#define MMC_EXT_GET_PWR_CLASS(reg)    ((reg) & 0xF)
#define MMC_EXT_MAX_PWR_CLASSES       16
#define MMC_EXT_CARD_TYPE_OFFSET             196
#define MMC_EXT_CARD_TYPE_HS_52  (1 << 1)
#define MMC_EXT_CARD_TYPE_HS_26  (1 << 0)
#define MMC_EXT_CSD_VER_OFFSET               194
#define MMC_EXT_VER_OFFSET                   192
#define MMC_EXT_VER_1_0          0
#define MMC_EXT_VER_1_1          1
#define MMC_EXT_CMD_SET_OFFSET               191
#define MMC_EXT_CMD_SET_REV_OFFSET           189
#define MMC_EXT_PWR_CLASS_OFFSET             187
#define MMC_EXT_HS_TIMING_OFFSET             185
#define MMC_EXT_HS_TIMING_ENABLE   0x01
#define MMC_EXT_BUS_WIDTH_OFFSET             183
#define MMC_EXT_BUS_WIDTH_1_BIT    0x00
#define MMC_EXT_BUS_WIDTH_4_BIT    0x01
#define MMC_EXT_BUS_WIDTH_8_BIT    0x02

#endif
