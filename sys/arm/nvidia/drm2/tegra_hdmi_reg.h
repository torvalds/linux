/*-
 * Copyright 1992-2016 Michal Meloun
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _TEGRA_HDMI_REG_H_
#define	_TEGRA_HDMI_REG_H_

/*
 * !!! WARNING !!!
 * Tegra manual uses registers index (and not register addreses).
 * We follow the TRM notation and index is converted to offset in
 * WR4 / RD4 macros
 */
#define	HDMI_NV_PDISP_SOR_STATE0		0x001
#define	 SOR_STATE0_UPDATE				(1 << 0)

#define	HDMI_NV_PDISP_SOR_STATE1		0x002
#define	 SOR_STATE1_ATTACHED				(1 << 3)
#define	 SOR_STATE1_ASY_ORMODE_NORMAL			(1 << 2)
#define	 SOR_STATE1_ASY_HEAD_OPMODE(x)			(((x) & 0x3) << 0)
#define	  ASY_HEAD_OPMODE_SLEEP					0
#define	  ASY_HEAD_OPMODE_SNOOZE				1
#define	  ASY_HEAD_OPMODE_AWAKE					2

#define	HDMI_NV_PDISP_SOR_STATE2		0x003
#define	 SOR_STATE2_ASY_DEPOL_NEG			(1 << 14)
#define	 SOR_STATE2_ASY_VSYNCPOL_NEG			(1 << 13)
#define	 SOR_STATE2_ASY_HSYNCPOL_NEG			(1 << 12)
#define	 SOR_STATE2_ASY_PROTOCOL(x)			(((x) & 0xf) << 8)
#define	  ASY_PROTOCOL_SINGLE_TMDS_A				1
#define	  ASY_PROTOCOL_CUSTOM					15
#define	 SOR_STATE2_ASY_CRCMODE(x)			(((x) & 0x3) <<  6)
#define	  ASY_CRCMODE_ACTIVE					0
#define	  ASY_CRCMODE_COMPLETE					1
#define	  ASY_CRCMODE_NON_ACTIVE				2
#define	 SOR_STATE2_ASY_SUBOWNER(x)			(((x) & 0x3) <<  4)
#define	  ASY_SUBOWNER_NONE					0
#define	  ASY_SUBOWNER_SUBHEAD0					1
#define	  ASY_SUBOWNER_SUBHEAD1					2
#define	  SUBOWNER_BOTH						3
#define	 SOR_STATE2_ASY_OWNER(x)			(((x) & 0x3) <<  0)
#define	  ASY_OWNER_NONE					0
#define	  ASY_OWNER_HEAD0					1

#define	HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_CTRL	0x01e
#define	 AUDIO_INFOFRAME_CTRL_ENABLE			(1 << 0)
#define	HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_STATUS 0x01f
#define	HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_HEADER 0x020
#define	HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_SUBPACK0_LOW 0x021
#define	HDMI_NV_PDISP_HDMI_AUDIO_INFOFRAME_SUBPACK0_HIGH 0x022
#define	 INFOFRAME_HEADER_LEN(x)			(((x) & 0x0f) << 16)
#define	 INFOFRAME_HEADER_VERSION(x)			(((x) & 0xff) <<  8)
#define	 INFOFRAME_HEADER_TYPE(x)			(((x) & 0xff) <<  0)

#define	HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_CTRL	0x023
#define	 AVI_INFOFRAME_CTRL_ENABLE			(1 << 0)
#define	HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_STATUS	0x024
#define	HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_HEADER	0x025
#define	HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK0_LOW  0x026
#define	HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK0_HIGH 0x027
#define	HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK1_LOW  0x028
#define	HDMI_NV_PDISP_HDMI_AVI_INFOFRAME_SUBPACK1_HIGH 0x029

#define	HDMI_NV_PDISP_HDMI_GENERIC_CTRL		0x02a
#define	 GENERIC_CTRL_AUDIO				(1 << 16)
#define	 GENERIC_CTRL_HBLANK				(1 << 12)
#define	 GENERIC_CTRL_SINGLE				(1 <<  8)
#define	 GENERIC_CTRL_OTHER				(1 <<  4)
#define	 GENERIC_CTRL_ENABLE				(1 <<  0)
#define	HDMI_NV_PDISP_HDMI_GENERIC_STATUS	0x02b
#define	HDMI_NV_PDISP_HDMI_GENERIC_HEADER	0x02c
#define	HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK0_LOW	 0x02d
#define	HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK0_HIGH 0x02e
#define	HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK1_LOW	 0x02f
#define	HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK1_HIGH 0x030
#define	HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK2_LOW	 0x031
#define	HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK2_HIGH 0x032
#define	HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK3_LOW	 0x033
#define	HDMI_NV_PDISP_HDMI_GENERIC_SUBPACK3_HIGH 0x034

#define	HDMI_NV_PDISP_HDMI_ACR_CTRL		0x035
#define	HDMI_NV_PDISP_HDMI_ACR_0320_SUBPACK_LOW	 0x036
#define	HDMI_NV_PDISP_HDMI_ACR_0320_SUBPACK_HIGH 0x037
#define	HDMI_NV_PDISP_HDMI_ACR_0441_SUBPACK_LOW	 0x038
#define	HDMI_NV_PDISP_HDMI_ACR_0441_SUBPACK_HIGH 0x039
#define	HDMI_NV_PDISP_HDMI_ACR_0882_SUBPACK_LOW	 0x03a
#define	HDMI_NV_PDISP_HDMI_ACR_0882_SUBPACK_HIGH 0x03b
#define	HDMI_NV_PDISP_HDMI_ACR_1764_SUBPACK_LOW	 0x03c
#define	HDMI_NV_PDISP_HDMI_ACR_1764_SUBPACK_HIGH 0x03d
#define	HDMI_NV_PDISP_HDMI_ACR_0480_SUBPACK_LOW	 0x03e
#define	HDMI_NV_PDISP_HDMI_ACR_0480_SUBPACK_HIGH 0x03f
#define	HDMI_NV_PDISP_HDMI_ACR_0960_SUBPACK_LOW	 0x040
#define	HDMI_NV_PDISP_HDMI_ACR_0960_SUBPACK_HIGH 0x041
#define	HDMI_NV_PDISP_HDMI_ACR_1920_SUBPACK_LOW	 0x042
#define	HDMI_NV_PDISP_HDMI_ACR_1920_SUBPACK_HIGH 0x043
#define	 ACR_ENABLE					(1U << 31)
#define	 ACR_SUBPACK_CTS(x)				(((x) & 0xffffff) << 8)
#define	 ACR_SUBPACK_N(x)				(((x) & 0xffffff) << 0)

#define	HDMI_NV_PDISP_HDMI_CTRL			0x044
#define	 HDMI_CTRL_ENABLE				(1 << 30)
#define	 HDMI_CTRL_CA_SELECT				(1 << 28)
#define	 HDMI_CTRL_SS_SELECT				(1 << 27)
#define	 HDMI_CTRL_SF_SELECT				(1 << 26)
#define	 HDMI_CTRL_CC_SELECT				(1 << 25)
#define	 HDMI_CTRL_CT_SELECT				(1 << 24)
#define	 HDMI_CTRL_MAX_AC_PACKET(x)			(((x) & 0x1f) << 16)
#define	 HDMI_CTRL_SAMPLE_FLAT				(1 << 12)
#define	 HDMI_CTRL_AUDIO_LAYOUT_SELECT			(1 << 10)
#define	 HDMI_CTRL_AUDIO_LAYOUT				(1 <<  8)
#define	 HDMI_CTRL_REKEY(x)				(((x) & 0x7f) <<  0)

#define	HDMI_NV_PDISP_HDMI_VSYNC_WINDOW		0x046
#define	 VSYNC_WINDOW_ENABLE				(1U << 31)
#define	 VSYNC_WINDOW_START(x)				(((x) & 0x3ff) << 16)
#define	 VSYNC_WINDOW_END(x)				(((x) & 0x3ff) <<  0)

#define	HDMI_NV_PDISP_HDMI_SPARE		0x04f
#define	 SPARE_ACR_PRIORITY				(1U << 31)
#define	 SPARE_CTS_RESET_VAL(x)				(((x) & 0x7) << 16)
#define	 SPARE_SUPRESS_SP_B				(1 << 2)
#define	 SPARE_FORCE_SW_CTS				(1 << 1)
#define	 SPARE_HW_CTS					(1 << 0)

#define	HDMI_NV_PDISP_SOR_PWR			0x055
#define	 SOR_PWR_SETTING_NEW				(1U << 31)
#define	 SOR_PWR_SAFE_STATE_PU				(1 << 16)
#define	 SOR_PWR_NORMAL_START_ALT			(1 <<  1)
#define	 SOR_PWR_NORMAL_STATE_PU			(1 <<  0)

#define	HDMI_NV_PDISP_SOR_PLL0			0x057
#define	 SOR_PLL0_TX_REG_LOAD(x)			(((x) & 0xf) << 28)
#define	 SOR_PLL0_ICHPMP(x)				(((x) & 0xf) << 24)
#define	 SOR_PLL0_FILTER(x)				(((x) & 0xf) << 16)
#define	 SOR_PLL0_BG_V17_S(x)				(((x) & 0xf) << 12)
#define	 SOR_PLL0_VCOCAP(x)				(((x) & 0xf) <<  8)
#define	 SOR_PLL0_PULLDOWN				(1 << 5)
#define	 SOR_PLL0_RESISTORSEL				(1 << 4)
#define	 SOR_PLL0_PDPORT				(1 << 3)
#define	 SOR_PLL0_VCOPD					(1 << 2)
#define	 SOR_PLL0_PDBG					(1 << 1)
#define	 SOR_PLL0_PWR					(1 << 0)

#define	HDMI_NV_PDISP_SOR_PLL1			0x058
#define	 SOR_PLL1_S_D_PIN_PE				(1 << 30)
#define	 SOR_PLL1_HALF_FULL_PE				(1 << 29)
#define	 SOR_PLL1_PE_EN					(1 << 28)
#define	 SOR_PLL1_LOADADJ(x)				(((x) & 0xf) << 20)
#define	 SOR_PLL1_TMDS_TERMADJ(x)			(((x) & 0xf) <<  9)
#define	 SOR_PLL1_TMDS_TERM				(1 << 8)

#define	HDMI_NV_PDISP_SOR_CSTM			0x05a
#define	 SOR_CSTM_ROTAT(x)				(((x) & 0xf) << 28)
#define	 SOR_CSTM_ROTCLK(x)				(((x) & 0xf) << 24)
#define	 SOR_CSTM_PLLDIV				(1 << 21)
#define	 SOR_CSTM_BALANCED				(1 << 19)
#define	 SOR_CSTM_NEW_MODE				(1 << 18)
#define	 SOR_CSTM_DUP_SYNC				(1 << 17)
#define	 SOR_CSTM_LVDS_ENABLE				(1 << 16)
#define	 SOR_CSTM_LINKACTB				(1 << 15)
#define	 SOR_CSTM_LINKACTA				(1 << 14)
#define	 SOR_CSTM_MODE(x)				(((x) & 0x3) << 12)
#define	  CSTM_MODE_LVDS					0
#define	  CSTM_MODE_TMDS					1

#define	HDMI_NV_PDISP_SOR_SEQ_CTL		0x05f
#define	 SOR_SEQ_SWITCH					(1 << 30)
#define	 SOR_SEQ_STATUS					(1 << 28)
#define	 SOR_SEQ_PC(x)					(((x) & 0xf) << 16)
#define	 SOR_SEQ_PD_PC_ALT(x)				(((x) & 0xf) << 12)
#define	 SOR_SEQ_PD_PC(x)				(((x) & 0xf) <<  8)
#define	 SOR_SEQ_PU_PC_ALT(x)				(((x) & 0xf) <<  4)
#define	 SOR_SEQ_PU_PC(x)				(((x) & 0xf) <<  0)

#define	HDMI_NV_PDISP_SOR_SEQ_INST(x)		(0x060 + (x))
#define	 SOR_SEQ_INST_PLL_PULLDOWN			(1U << 31)
#define	 SOR_SEQ_INST_POWERDOWN_MACRO			(1 << 30)
#define	 SOR_SEQ_INST_ASSERT_PLL_RESETV			(1 << 29)
#define	 SOR_SEQ_INST_BLANK_V				(1 << 28)
#define	 SOR_SEQ_INST_BLANK_H				(1 << 27)
#define	 SOR_SEQ_INST_BLANK_DE				(1 << 26)
#define	 SOR_SEQ_INST_BLACK_DATA			(1 << 25)
#define	 SOR_SEQ_INST_TRISTATE_IOS			(1 << 24)
#define	 SOR_SEQ_INST_DRIVE_PWM_OUT_LO			(1 << 23)
#define	 SOR_SEQ_INST_PIN_B_HIGH			(1 << 22)
#define	 SOR_SEQ_INST_PIN_A_HIGH			(1 << 21)
#define	 SOR_SEQ_INST_HALT				(1 << 15)
#define	 SOR_SEQ_INST_WAIT_UNITS(x)			(((x) & 0x3) << 12)
#define	  WAIT_UNITS_US						0
#define	  WAIT_UNITS_MS						1
#define	  WAIT_UNITS_VSYNC					2
#define	 SOR_SEQ_INST_WAIT_TIME(x)			(((x) & 0x3ff) << 0)

#define	HDMI_NV_PDISP_SOR_LANE_DRIVE_CURRENT	0x07e


#define	HDMI_NV_PDISP_AUDIO_N			0x08c
#define	 AUDIO_N_LOOKUP					(1 << 28)
#define	 AUDIO_N_GENERATE_ALTERNATE			(1 << 24)
#define	 AUDIO_N_RESETF					(1 << 20)
#define	 AUDIO_N_VALUE(x)				(((x) & 0xfffff) << 0)

#define	HDMI_NV_PDISP_SOR_REFCLK		0x095
#define	 SOR_REFCLK_DIV_INT(x)				(((x) & 0xff) << 8)
#define	 SOR_REFCLK_DIV_FRAC(x)				(((x) & 0x03) << 6)

#define	HDMI_NV_PDISP_INPUT_CONTROL		0x097
#define	 ARM_VIDEO_RANGE_LIMITED			(1 << 1)
#define	 HDMI_SRC_DISPLAYB				(1 << 0)

#define	HDMI_NV_PDISP_PE_CURRENT		0x099
#define	HDMI_NV_PDISP_SOR_AUDIO_CNTRL0		0x0ac
#define	 SOR_AUDIO_CNTRL0_INJECT_NULLSMPL		(1 << 29)
#define	 SOR_AUDIO_CNTRL0_SOURCE_SELECT(x)		(((x) & 0x03) << 20)
#define	  SOURCE_SELECT_AUTO					0
#define	  SOURCE_SELECT_SPDIF					1
#define	  SOURCE_SELECT_HDAL					2
#define	 SOR_AUDIO_CNTRL0_AFIFO_FLUSH			(1 << 12)

#define	HDMI_NV_PDISP_SOR_AUDIO_SPARE0		0x0ae
#define	 SOR_AUDIO_SPARE0_HBR_ENABLE			(1 << 27)

#define	HDMI_NV_PDISP_SOR_AUDIO_NVAL_0320	0x0af
#define	HDMI_NV_PDISP_SOR_AUDIO_NVAL_0441	0x0b0
#define	HDMI_NV_PDISP_SOR_AUDIO_NVAL_0882	0x0b1
#define	HDMI_NV_PDISP_SOR_AUDIO_NVAL_1764	0x0b2
#define	HDMI_NV_PDISP_SOR_AUDIO_NVAL_0480	0x0b3
#define	HDMI_NV_PDISP_SOR_AUDIO_NVAL_0960	0x0b4
#define	HDMI_NV_PDISP_SOR_AUDIO_NVAL_1920	0x0b5
#define	HDMI_NV_PDISP_SOR_AUDIO_HDA_SCRATCH0	0x0b6
#define	HDMI_NV_PDISP_SOR_AUDIO_HDA_SCRATCH1	0x0b7
#define	HDMI_NV_PDISP_SOR_AUDIO_HDA_SCRATCH2	0x0b8
#define	HDMI_NV_PDISP_SOR_AUDIO_HDA_SCRATCH3	0x0b9
#define	HDMI_NV_PDISP_SOR_AUDIO_HDA_CODEC_SCRATCH0 0x0ba
#define	HDMI_NV_PDISP_SOR_AUDIO_HDA_CODEC_SCRATCH1 0x0bb
#define	HDMI_NV_PDISP_SOR_AUDIO_HDA_ELD_BUFWR	0x0bc
#define	HDMI_NV_PDISP_SOR_AUDIO_HDA_PRESENSE	0x0bd
#define	 SOR_AUDIO_HDA_PRESENSE_VALID			(1 << 1)
#define	 SOR_AUDIO_HDA_PRESENSE_PRESENT			(1 << 0)

#define	HDMI_NV_PDISP_SOR_AUDIO_AVAL_0320	0x0bf
#define	HDMI_NV_PDISP_SOR_AUDIO_AVAL_0441	0x0c0
#define	HDMI_NV_PDISP_SOR_AUDIO_AVAL_0882	0x0c1
#define	HDMI_NV_PDISP_SOR_AUDIO_AVAL_1764	0x0c2
#define	HDMI_NV_PDISP_SOR_AUDIO_AVAL_0480	0x0c3
#define	HDMI_NV_PDISP_SOR_AUDIO_AVAL_0960	0x0c4
#define	HDMI_NV_PDISP_SOR_AUDIO_AVAL_1920	0x0c5
#define	HDMI_NV_PDISP_SOR_AUDIO_AVAL_DEFAULT	0x0c6

#define	HDMI_NV_PDISP_INT_STATUS		0x0cc
#define	 INT_SCRATCH					(1 << 3)
#define	 INT_CP_REQUEST					(1 << 2)
#define	 INT_CODEC_SCRATCH1				(1 << 1)
#define	 INT_CODEC_SCRATCH0				(1 << 0)

#define	HDMI_NV_PDISP_INT_MASK			0x0cd
#define	HDMI_NV_PDISP_INT_ENABLE		0x0ce
#define	HDMI_NV_PDISP_SOR_IO_PEAK_CURRENT	0x0d1
#define	HDMI_NV_PDISP_SOR_PAD_CTLS0		0x0d2


#endif /* _TEGRA_HDMI_REG_H_ */
