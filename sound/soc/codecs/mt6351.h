/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt6351.h  --  mt6351 ALSA SoC audio codec driver
 *
 * Copyright (c) 2018 MediaTek Inc.
 * Author: KaiChieh Chuang <kaichieh.chuang@mediatek.com>
 */

#ifndef __MT6351_H__
#define __MT6351_H__

#define MT6351_AFE_UL_DL_CON0               (0x2000 + 0x0000)
#define MT6351_AFE_DL_SRC2_CON0_H           (0x2000 + 0x0002)
#define MT6351_AFE_DL_SRC2_CON0_L           (0x2000 + 0x0004)
#define MT6351_AFE_DL_SDM_CON0              (0x2000 + 0x0006)
#define MT6351_AFE_DL_SDM_CON1              (0x2000 + 0x0008)
#define MT6351_AFE_UL_SRC_CON0_H            (0x2000 + 0x000a)
#define MT6351_AFE_UL_SRC_CON0_L            (0x2000 + 0x000c)
#define MT6351_AFE_UL_SRC_CON1_H            (0x2000 + 0x000e)
#define MT6351_AFE_UL_SRC_CON1_L            (0x2000 + 0x0010)
#define MT6351_AFE_TOP_CON0                 (0x2000 + 0x0012)
#define MT6351_AUDIO_TOP_CON0               (0x2000 + 0x0014)
#define MT6351_AFE_DL_SRC_MON0              (0x2000 + 0x0016)
#define MT6351_AFE_DL_SDM_TEST0             (0x2000 + 0x0018)
#define MT6351_AFE_MON_DEBUG0               (0x2000 + 0x001a)
#define MT6351_AFUNC_AUD_CON0               (0x2000 + 0x001c)
#define MT6351_AFUNC_AUD_CON1               (0x2000 + 0x001e)
#define MT6351_AFUNC_AUD_CON2               (0x2000 + 0x0020)
#define MT6351_AFUNC_AUD_CON3               (0x2000 + 0x0022)
#define MT6351_AFUNC_AUD_CON4               (0x2000 + 0x0024)
#define MT6351_AFUNC_AUD_MON0               (0x2000 + 0x0026)
#define MT6351_AFUNC_AUD_MON1               (0x2000 + 0x0028)
#define MT6351_AFE_UP8X_FIFO_CFG0           (0x2000 + 0x002c)
#define MT6351_AFE_UP8X_FIFO_LOG_MON0       (0x2000 + 0x002e)
#define MT6351_AFE_UP8X_FIFO_LOG_MON1       (0x2000 + 0x0030)
#define MT6351_AFE_DL_DC_COMP_CFG0          (0x2000 + 0x0032)
#define MT6351_AFE_DL_DC_COMP_CFG1          (0x2000 + 0x0034)
#define MT6351_AFE_DL_DC_COMP_CFG2          (0x2000 + 0x0036)
#define MT6351_AFE_PMIC_NEWIF_CFG0          (0x2000 + 0x0038)
#define MT6351_AFE_PMIC_NEWIF_CFG1          (0x2000 + 0x003a)
#define MT6351_AFE_PMIC_NEWIF_CFG2          (0x2000 + 0x003c)
#define MT6351_AFE_PMIC_NEWIF_CFG3          (0x2000 + 0x003e)
#define MT6351_AFE_SGEN_CFG0                (0x2000 + 0x0040)
#define MT6351_AFE_SGEN_CFG1                (0x2000 + 0x0042)
#define MT6351_AFE_ADDA2_UP8X_FIFO_LOG_MON0 (0x2000 + 0x004c)
#define MT6351_AFE_ADDA2_UP8X_FIFO_LOG_MON1 (0x2000 + 0x004e)
#define MT6351_AFE_ADDA2_PMIC_NEWIF_CFG0    (0x2000 + 0x0050)
#define MT6351_AFE_ADDA2_PMIC_NEWIF_CFG1    (0x2000 + 0x0052)
#define MT6351_AFE_ADDA2_PMIC_NEWIF_CFG2    (0x2000 + 0x0054)
#define MT6351_AFE_DCCLK_CFG0               (0x2000 + 0x0090)
#define MT6351_AFE_DCCLK_CFG1               (0x2000 + 0x0092)
#define MT6351_AFE_HPANC_CFG0               (0x2000 + 0x0094)
#define MT6351_AFE_NCP_CFG0                 (0x2000 + 0x0096)
#define MT6351_AFE_NCP_CFG1                 (0x2000 + 0x0098)

#define MT6351_TOP_CKPDN_CON0      0x023A
#define MT6351_TOP_CKPDN_CON0_SET  0x023C
#define MT6351_TOP_CKPDN_CON0_CLR  0x023E

#define MT6351_TOP_CLKSQ           0x029A
#define MT6351_TOP_CLKSQ_SET       0x029C
#define MT6351_TOP_CLKSQ_CLR       0x029E

#define MT6351_ZCD_CON0            0x0800
#define MT6351_ZCD_CON1            0x0802
#define MT6351_ZCD_CON2            0x0804
#define MT6351_ZCD_CON3            0x0806
#define MT6351_ZCD_CON4            0x0808
#define MT6351_ZCD_CON5            0x080A

#define MT6351_LDO_VA18_CON0       0x0A00
#define MT6351_LDO_VA18_CON1       0x0A02
#define MT6351_LDO_VUSB33_CON0     0x0A16
#define MT6351_LDO_VUSB33_CON1     0x0A18

#define MT6351_AUDDEC_ANA_CON0     0x0CF2
#define MT6351_AUDDEC_ANA_CON1     0x0CF4
#define MT6351_AUDDEC_ANA_CON2     0x0CF6
#define MT6351_AUDDEC_ANA_CON3     0x0CF8
#define MT6351_AUDDEC_ANA_CON4     0x0CFA
#define MT6351_AUDDEC_ANA_CON5     0x0CFC
#define MT6351_AUDDEC_ANA_CON6     0x0CFE
#define MT6351_AUDDEC_ANA_CON7     0x0D00
#define MT6351_AUDDEC_ANA_CON8     0x0D02
#define MT6351_AUDDEC_ANA_CON9     0x0D04
#define MT6351_AUDDEC_ANA_CON10    0x0D06

#define MT6351_AUDENC_ANA_CON0     0x0D08
#define MT6351_AUDENC_ANA_CON1     0x0D0A
#define MT6351_AUDENC_ANA_CON2     0x0D0C
#define MT6351_AUDENC_ANA_CON3     0x0D0E
#define MT6351_AUDENC_ANA_CON4     0x0D10
#define MT6351_AUDENC_ANA_CON5     0x0D12
#define MT6351_AUDENC_ANA_CON6     0x0D14
#define MT6351_AUDENC_ANA_CON7     0x0D16
#define MT6351_AUDENC_ANA_CON8     0x0D18
#define MT6351_AUDENC_ANA_CON9     0x0D1A
#define MT6351_AUDENC_ANA_CON10    0x0D1C
#define MT6351_AUDENC_ANA_CON11    0x0D1E
#define MT6351_AUDENC_ANA_CON12    0x0D20
#define MT6351_AUDENC_ANA_CON13    0x0D22
#define MT6351_AUDENC_ANA_CON14    0x0D24
#define MT6351_AUDENC_ANA_CON15    0x0D26
#define MT6351_AUDENC_ANA_CON16    0x0D28
#endif
