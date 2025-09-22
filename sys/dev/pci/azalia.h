/*	$OpenBSD: azalia.h,v 1.69 2019/10/14 02:04:35 jcs Exp $	*/
/*	$NetBSD: azalia.h,v 1.6 2006/01/16 14:15:26 kent Exp $	*/

/*-
 * Copyright (c) 2005 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by TAMURA Kent
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/audioio.h>

/* ----------------------------------------------------------------
 * High Definition Audio constant values
 * ---------------------------------------------------------------- */

/* High Definition Audio registers */
#define HDA_GCAP	0x000	/* 2 */
#define		HDA_GCAP_OSS(x)	((x & 0xf000) >> 12)
#define		HDA_GCAP_ISS(x)	((x & 0x0f00) >> 8)
#define		HDA_GCAP_BSS(x)	((x & 0x00f8) >> 3)
#define		HDA_GCAP_NSDO_MASK	0x0006
#define		HDA_GCAP_NSDO_1		0x0000
#define		HDA_GCAP_NSDO_2		0x0002
#define		HDA_GCAP_NSDO_4		0x0004
#define		HDA_GCAP_NSDO_RESERVED	0x0006
#define		HDA_GCAP_64OK	0x0001
#define HDA_VMIN	0x002	/* 1 */
#define HDA_VMAJ	0x003	/* 1 */
#define HDA_OUTPAY	0x004	/* 2 */
#define HDA_INPAY	0x006	/* 2 */
#define HDA_GCTL	0x008	/* 4 */
#define		HDA_GCTL_UNSOL	0x00000100
#define		HDA_GCTL_FCNTRL	0x00000002
#define		HDA_GCTL_CRST	0x00000001
#define HDA_WAKEEN	0x00c	/* 2 */
#define		HDA_WAKEEN_SDIWEN	0x7fff
#define HDA_STATESTS	0x00e	/* 2 */
#define		HDA_STATESTS_SDIWAKE	0x7fff
#define HDA_GSTS	0x010	/* 2 */
#define		HDA_GSTS_FSTS		0x0002
#define HDA_OUTSTRMPAY	0x018	/* 2 */
#define HDA_INSTRMPAY	0x01a	/* 2 */
#define HDA_INTCTL	0x020	/* 4 */
#define		HDA_INTCTL_GIE	0x80000000
#define		HDA_INTCTL_CIE	0x40000000
#define		HDA_INTCTL_SIE	0x3fffffff
#define HDA_INTSTS	0x024	/* 4 */
#define		HDA_INTSTS_GIS	0x80000000
#define		HDA_INTSTS_CIS	0x40000000
#define		HDA_INTSTS_SIS	0x3fffffff
#define HDA_WALCLK	0x030	/* 4 */
#define HDA_SSYNC	0x034	/* 4 */
#define		HDA_SSYNC_SSYNC	0x3fffffff
#define HDA_CORBLBASE	0x040	/* 4 */
#define HDA_CORBUBASE	0x044	/* 4 */
#define HDA_CORBWP	0x048	/* 2 */
#define		HDA_CORBWP_CORBWP	0x00ff
#define HDA_CORBRP	0x04a	/* 2 */
#define		HDA_CORBRP_CORBRPRST	0x8000
#define		HDA_CORBRP_CORBRP	0x00ff
#define HDA_CORBCTL	0x04c	/* 1 */
#define		HDA_CORBCTL_CORBRUN	0x02
#define		HDA_CORBCTL_CMEIE	0x01
#define HDA_CORBSTS	0x04d	/* 1 */
#define		HDA_CORBSTS_CMEI	0x01
#define HDA_CORBSIZE	0x04e	/* 1 */
#define		HDA_CORBSIZE_CORBSZCAP_MASK	0xf0
#define		HDA_CORBSIZE_CORBSZCAP_2	0x10
#define		HDA_CORBSIZE_CORBSZCAP_16	0x20
#define		HDA_CORBSIZE_CORBSZCAP_256	0x40
#define		HDA_CORBSIZE_CORBSIZE_MASK	0x03
#define		HDA_CORBSIZE_CORBSIZE_2		0x00
#define		HDA_CORBSIZE_CORBSIZE_16	0x01
#define		HDA_CORBSIZE_CORBSIZE_256	0x02
#define HDA_RIRBLBASE	0x050	/* 4 */
#define HDA_RIRBUBASE	0x054	/* 4 */
#define HDA_RIRBWP	0x058	/* 2 */
#define		HDA_RIRBWP_RIRBWPRST	0x8000
#define		HDA_RIRBWP_RIRBWP	0x00ff
#define HDA_RINTCNT	0x05a	/* 2 */
#define		HDA_RINTCNT_RINTCNT	0x00ff
#define HDA_RIRBCTL	0x05c	/* 1 */
#define		HDA_RIRBCTL_RIRBOIC	0x04
#define		HDA_RIRBCTL_RIRBDMAEN	0x02
#define		HDA_RIRBCTL_RINTCTL	0x01
#define HDA_RIRBSTS	0x05d	/* 1 */
#define		HDA_RIRBSTS_RIRBOIS	0x04
#define		HDA_RIRBSTS_RINTFL	0x01
#define HDA_RIRBSIZE	0x05e	/* 1 */
#define		HDA_RIRBSIZE_RIRBSZCAP_MASK	0xf0
#define		HDA_RIRBSIZE_RIRBSZCAP_2	0x10
#define		HDA_RIRBSIZE_RIRBSZCAP_16	0x20
#define		HDA_RIRBSIZE_RIRBSZCAP_256	0x40
#define		HDA_RIRBSIZE_RIRBSIZE_MASK	0x03
#define		HDA_RIRBSIZE_RIRBSIZE_2		0x00
#define		HDA_RIRBSIZE_RIRBSIZE_16	0x01
#define		HDA_RIRBSIZE_RIRBSIZE_256	0x02
#define HDA_IC		0x060	/* 4 */
#define HDA_IR		0x064	/* 4 */
#define HDA_IRS		0x068	/* 2 */
#define		HDA_IRS_IRRADD		0x00f0
#define		HDA_IRS_IRRUNSOL	0x0008
#define		HDA_IRS_IRV		0x0002
#define		HDA_IRS_ICB		0x0001
#define HDA_DPLBASE	0x070	/* 4 */
#define		HDA_DPLBASE_DPLBASE	0xffffff80
#define		HDA_DPLBASE_ENABLE	0x00000001
#define HDA_DPUBASE	0x074

#define HDA_SD_BASE	0x080
#define		HDA_SD_CTL	0x00 /* 2 */
#define			HDA_SD_CTL_DEIE	0x0010
#define			HDA_SD_CTL_FEIE	0x0008
#define			HDA_SD_CTL_IOCE	0x0004
#define			HDA_SD_CTL_RUN	0x0002
#define			HDA_SD_CTL_SRST	0x0001
#define		HDA_SD_CTL2	0x02 /* 1 */
#define			HDA_SD_CTL2_STRM	0xf0
#define			HDA_SD_CTL2_STRM_SHIFT	4
#define			HDA_SD_CTL2_DIR		0x08
#define			HDA_SD_CTL2_TP		0x04
#define			HDA_SD_CTL2_STRIPE	0x03
#define		HDA_SD_STS	0x03 /* 1 */
#define			HDA_SD_STS_FIFORDY	0x20
#define			HDA_SD_STS_DESE		0x10
#define			HDA_SD_STS_FIFOE	0x08
#define			HDA_SD_STS_BCIS		0x04
#define		HDA_SD_LPIB	0x04 /* 4 */
#define		HDA_SD_CBL	0x08 /* 4 */
#define		HDA_SD_LVI	0x0c /* 2 */
#define			HDA_SD_LVI_LVI	0x00ff
#define		HDA_SD_FIFOW	0x0e /* 2 */
#define		HDA_SD_FIFOS	0x10 /* 2 */
#define		HDA_SD_FMT	0x12 /* 2 */
#define			HDA_SD_FMT_BASE	0x4000
#define			HDA_SD_FMT_BASE_48	0x0000
#define			HDA_SD_FMT_BASE_44	0x4000
#define			HDA_SD_FMT_MULT	0x3800
#define			HDA_SD_FMT_MULT_X1	0x0000
#define			HDA_SD_FMT_MULT_X2	0x0800
#define			HDA_SD_FMT_MULT_X3	0x1000
#define			HDA_SD_FMT_MULT_X4	0x1800
#define			HDA_SD_FMT_DIV	0x0700
#define			HDA_SD_FMT_DIV_BY1	0x0000
#define			HDA_SD_FMT_DIV_BY2	0x0100
#define			HDA_SD_FMT_DIV_BY3	0x0200
#define			HDA_SD_FMT_DIV_BY4	0x0300
#define			HDA_SD_FMT_DIV_BY5	0x0400
#define			HDA_SD_FMT_DIV_BY6	0x0500
#define			HDA_SD_FMT_DIV_BY7	0x0600
#define			HDA_SD_FMT_DIV_BY8	0x0700
#define			HDA_SD_FMT_BITS	0x0070
#define			HDA_SD_FMT_BITS_8_16	0x0000
#define			HDA_SD_FMT_BITS_16_16	0x0010
#define			HDA_SD_FMT_BITS_20_32	0x0020
#define			HDA_SD_FMT_BITS_24_32	0x0030
#define			HDA_SD_FMT_BITS_32_32	0x0040
#define			HDA_SD_FMT_CHAN	0x000f
#define		HDA_SD_BDPL	0x18 /* 4 */
#define		HDA_SD_BDPU	0x1c /* 4 */
#define		HDA_SD_SIZE	0x20

/* CORB commands */
#define CORB_GET_PARAMETER		0xf00
#define		COP_VENDOR_ID			0x00
#define			COP_VID_VENDOR(x)	(x >> 16)
#define			COP_VID_DEVICE(x)	(x & 0xffff)
#define		COP_REVISION_ID			0x02
#define			COP_RID_MAJ(x)		((x >> 20) & 0x0f)
#define			COP_RID_MIN(x)		((x >> 16) & 0x0f)
#define			COP_RID_REVISION(x)	((x >> 8) & 0xff)
#define			COP_RID_STEPPING(x)	(x & 0xff)
#define		COP_SUBORDINATE_NODE_COUNT	0x04
#define			COP_START_NID(x)	((x & 0x00ff0000) >> 16)
#define			COP_NSUBNODES(x)	(x & 0x000000ff)
#define		COP_FUNCTION_GROUP_TYPE		0x05
#define			COP_FTYPE(x)		(x & 0x000000ff)
#define			COP_FTYPE_RESERVED	0x01
#define			COP_FTYPE_AUDIO		0x01
#define			COP_FTYPE_MODEM		0x02
#define		COP_AUDIO_FUNCTION_GROUP_CAPABILITY	0x08
#define		COP_AUDIO_WIDGET_CAP	0x09
#define			COP_AWCAP_TYPE(x)	((x >> 20) & 0xf)
#define			COP_AWTYPE_AUDIO_OUTPUT		0x0
#define			COP_AWTYPE_AUDIO_INPUT		0x1
#define			COP_AWTYPE_AUDIO_MIXER		0x2
#define			COP_AWTYPE_AUDIO_SELECTOR	0x3
#define			COP_AWTYPE_PIN_COMPLEX		0x4
#define			COP_AWTYPE_POWER		0x5
#define			COP_AWTYPE_VOLUME_KNOB		0x6
#define			COP_AWTYPE_BEEP_GENERATOR	0x7
#define			COP_AWTYPE_VENDOR_DEFINED	0xf
#define			COP_AWCAP_STEREO	0x001
#define			COP_AWCAP_INAMP		0x002
#define			COP_AWCAP_OUTAMP	0x004
#define			COP_AWCAP_AMPOV		0x008
#define			COP_AWCAP_FORMATOV	0x010
#define			COP_AWCAP_STRIPE	0x020
#define			COP_AWCAP_PROC		0x040
#define			COP_AWCAP_UNSOL		0x080
#define			COP_AWCAP_CONNLIST	0x100
#define			COP_AWCAP_DIGITAL	0x200
#define			COP_AWCAP_POWER		0x400
#define			COP_AWCAP_LRSWAP	0x800
#define			COP_AWCAP_DELAY(x)	((x >> 16) & 0xf)
#define		COP_PCM				0x0a
#define			COP_PCM_B32	0x00100000
#define			COP_PCM_B24	0x00080000
#define			COP_PCM_B20	0x00040000
#define			COP_PCM_B16	0x00020000
#define			COP_PCM_B8	0x00010000
#define			COP_PCM_R3840	0x00000800
#define			COP_PCM_R1920	0x00000400
#define			COP_PCM_R1764	0x00000200
#define			COP_PCM_R960	0x00000100
#define			COP_PCM_R882	0x00000080
#define			COP_PCM_R480	0x00000040
#define			COP_PCM_R441	0x00000020
#define			COP_PCM_R320	0x00000010
#define			COP_PCM_R220	0x00000008
#define			COP_PCM_R160	0x00000004
#define			COP_PCM_R110	0x00000002
#define			COP_PCM_R80	0x00000001
#define		COP_STREAM_FORMATS		0x0b
#define			COP_STREAM_FORMAT_PCM		0x00000001
#define			COP_STREAM_FORMAT_FLOAT32	0x00000002
#define			COP_STREAM_FORMAT_AC3		0x00000003
#define		COP_PINCAP		0x0c
#define			COP_PINCAP_IMPEDANCE	0x00000001
#define			COP_PINCAP_TRIGGER	0x00000002
#define			COP_PINCAP_PRESENCE	0x00000004
#define			COP_PINCAP_HEADPHONE	0x00000008
#define			COP_PINCAP_OUTPUT	0x00000010
#define			COP_PINCAP_INPUT	0x00000020
#define			COP_PINCAP_BALANCE	0x00000040
#define			COP_PINCAP_HDMI		0x00000080
#define			COP_PINCAP_VREF(x)	((x >> 8) & 0xff)
#define			COP_PINCAP_EAPD		0x00010000
#define		COP_INPUT_AMPCAP	0x0d
#define			COP_AMPCAP_OFFSET(x)	(x & 0x0000007f)
#define			COP_AMPCAP_NUMSTEPS(x)	((x >> 8) & 0x7f)
#define			COP_AMPCAP_STEPSIZE(x)	((x >> 16) & 0x7f)
#define			COP_AMPCAP_CTLOFF(x)	((x >> 24) & 0x7f)
#define			COP_AMPCAP_MUTE		0x80000000
#define		COP_CONNECTION_LIST_LENGTH	0x0e
#define			COP_CLL_LONG		0x00000080
#define			COP_CLL_LENGTH(x)	(x & 0x0000007f)
#define		COP_SUPPORTED_POWER_STATES	0x0f
#define		COP_PROCESSING_CAPABILITIES	0x10
#define		COP_GPIO_COUNT			0x11
#define			COP_GPIO_GPIOS(x)	(x & 0xff)
#define			COP_GPIO_GPOS(x)	((x >> 8) & 0xff)
#define			COP_GPIO_GPIS(x)	((x >> 16) & 0xff)
#define			COP_GPIO_UNSOL		0x40000000
#define			COP_GPIO_WAKE		0x80000000
#define		COP_OUTPUT_AMPCAP		0x12
#define		COP_VOLUME_KNOB_CAPABILITIES	0x13
#define			COP_VKCAP_DELTA		0x00000080
#define			COP_VKCAP_NUMSTEPS(x)	(x & 0x7f)
#define CORB_GET_CONNECTION_SELECT_CONTROL	0xf01
#define		CORB_CSC_INDEX(x)		(x & 0xff)
#define CORB_SET_CONNECTION_SELECT_CONTROL	0x701
#define CORB_GET_CONNECTION_LIST_ENTRY	0xf02
#define CORB_GET_PROCESSING_STATE	0xf03
#define CORB_SET_PROCESSING_STATE	0x703
#define CORB_GET_COEFFICIENT_INDEX	0xd00
#define CORB_SET_COEFFICIENT_INDEX	0x500
#define CORB_GET_PROCESSING_COEFFICIENT	0xc00
#define CORB_SET_PROCESSING_COEFFICIENT	0x400
#define CORB_GET_AMPLIFIER_GAIN_MUTE	0xb00
#define		CORB_GAGM_INPUT		0x0000
#define		CORB_GAGM_OUTPUT	0x8000
#define		CORB_GAGM_RIGHT		0x0000
#define		CORB_GAGM_LEFT		0x2000
#define		CORB_GAGM_MUTE		0x00000080
#define		CORB_GAGM_GAIN(x)	(x & 0x0000007f)
#define CORB_SET_AMPLIFIER_GAIN_MUTE	0x300
#define		CORB_AGM_GAIN_MASK	0x007f
#define		CORB_AGM_MUTE		0x0080
#define		CORB_AGM_INDEX_SHIFT	8
#define		CORB_AGM_RIGHT		0x1000
#define		CORB_AGM_LEFT		0x2000
#define		CORB_AGM_INPUT		0x4000
#define		CORB_AGM_OUTPUT		0x8000
#define CORB_GET_CONVERTER_FORMAT	0xa00
#define CORB_SET_CONVERTER_FORMAT	0x200
#define CORB_GET_DIGITAL_CONTROL	0xf0d
#define CORB_SET_DIGITAL_CONTROL_L	0x70d
#define CORB_SET_DIGITAL_CONTROL_H	0x70e
#define		CORB_DCC_DIGEN		0x01
#define		CORB_DCC_V		0x02
#define		CORB_DCC_VCFG		0x04
#define		CORB_DCC_PRE		0x08
#define		CORB_DCC_COPY		0x10
#define		CORB_DCC_NAUDIO		0x20
#define		CORB_DCC_PRO		0x40
#define		CORB_DCC_L		0x80
#define		CORB_DCC_CC(x)		((x >> 8) & 0x7f)
#define CORB_GET_POWER_STATE		0xf05
#define CORB_SET_POWER_STATE		0x705
#define		CORB_PS_D0		0x0
#define		CORB_PS_D1		0x1
#define		CORB_PS_D2		0x2
#define		CORB_PS_D3		0x3
#define CORB_GET_CONVERTER_STREAM_CHANNEL	0xf06
#define CORB_SET_CONVERTER_STREAM_CHANNEL	0x706
#define CORB_GET_INPUT_CONVERTER_SDI_SELECT	0xf04
#define CORB_SET_INPUT_CONVERTER_SDI_SELECT	0x704
#define CORB_GET_PIN_WIDGET_CONTROL	0xf07
#define CORB_SET_PIN_WIDGET_CONTROL	0x707
#define		CORB_PWC_HEADPHONE	0x80
#define		CORB_PWC_OUTPUT		0x40
#define		CORB_PWC_INPUT		0x20
#define		CORB_PWC_VREF_MASK	0x07
#define		CORB_PWC_VREF_HIZ	0x00
#define		CORB_PWC_VREF_50	0x01
#define		CORB_PWC_VREF_GND	0x02
#define		CORB_PWC_VREF_80	0x04
#define		CORB_PWC_VREF_100	0x05
#define CORB_GET_UNSOLICITED_RESPONSE	0xf08
#define CORB_SET_UNSOLICITED_RESPONSE	0x708
#define		CORB_UNSOL_ENABLE	0x80
#define		CORB_UNSOL_TAG(x)	(x & 0x3f)
#define CORB_GET_PIN_SENSE		0xf09
#define		CORB_PS_PRESENCE	0x80000000
#define		CORB_PS_IMPEDANCE(x)	(x & 0x7fffffff)
#define CORB_EXECUTE_PIN_SENSE		0x709
#define		CORB_PS_RIGHT		0x1
#define CORB_GET_EAPD_BTL_ENABLE	0xf0c
#define CORB_SET_EAPD_BTL_ENABLE	0x70c
#define		CORB_EAPD_BTL		0x01
#define		CORB_EAPD_EAPD		0x02
#define		CORB_EAPD_LRSWAP	0x04
#define CORB_GET_GPI_DATA		0xf10
#define CORB_SET_GPI_DATA		0x710
#define CORB_GET_GPI_WAKE_ENABLE_MASK	0xf11
#define CORB_SET_GPI_WAKE_ENABLE_MASK	0x711
#define CORB_GET_GPI_UNSOLICITED_ENABLE_MASK	0xf12
#define CORB_SET_GPI_UNSOLICITED_ENABLE_MASK	0x712
#define CORB_GET_GPI_STICKY_MASK	0xf13
#define CORB_SET_GPI_STICKY_MASK	0x713
#define CORB_GET_GPO_DATA		0xf14
#define CORB_SET_GPO_DATA		0x714
#define CORB_GET_GPIO_DATA		0xf15
#define CORB_SET_GPIO_DATA		0x715
#define CORB_GET_GPIO_ENABLE_MASK	0xf16
#define CORB_SET_GPIO_ENABLE_MASK	0x716
#define CORB_GET_GPIO_DIRECTION		0xf17
#define CORB_SET_GPIO_DIRECTION		0x717
#define CORB_GET_GPIO_WAKE_ENABLE_MASK	0xf18
#define CORB_SET_GPIO_WAKE_ENABLE_MASK	0x718
#define CORB_GET_GPIO_UNSOLICITED_ENABLE_MASK	0xf19
#define CORB_SET_GPIO_UNSOLICITED_ENABLE_MASK	0x719
#define CORB_GET_GPIO_STICKY_MASK	0xf1a
#define CORB_SET_GPIO_STICKY_MASK	0x71a
#define CORB_GET_GPIO_POLARITY		0xfe7
#define CORB_SET_GPIO_POLARITY		0x7e7
#define CORB_GET_BEEP_GENERATION	0xf0a
#define CORB_SET_BEEP_GENERATION	0x70a
#define CORB_GET_VOLUME_KNOB		0xf0f
#define CORB_SET_VOLUME_KNOB		0x70f
#define		CORB_VKNOB_DIRECT	0x80
#define		CORB_VKNOB_VOLUME(x)	(x & 0x7f)
#define CORB_GET_SUBSYSTEM_ID		0xf20
#define CORB_SET_SUBSYSTEM_ID_1		0x720
#define CORB_SET_SUBSYSTEM_ID_2		0x721
#define CORB_SET_SUBSYSTEM_ID_3		0x722
#define CORB_SET_SUBSYSTEM_ID_4		0x723
#define CORB_GET_CONFIGURATION_DEFAULT	0xf1c
#define CORB_SET_CONFIGURATION_DEFAULT_1	0x71c
#define CORB_SET_CONFIGURATION_DEFAULT_2	0x71d
#define CORB_SET_CONFIGURATION_DEFAULT_3	0x71e
#define CORB_SET_CONFIGURATION_DEFAULT_4	0x71f
#define		CORB_CD_SEQUENCE(x)	(x & 0x0000000f)
#define		CORB_CD_SEQUENCE_MAX	0x0f
#define		CORB_CD_ASSOCIATION(x)	((x >> 4) & 0xf)
#define		CORB_CD_ASSOCIATION_MAX	0x0f
#define		CORB_CD_MISC_MASK	0x00000f00
#define		CORB_CD_MISC(x)		((x >> 8) & 0xf)
#define			CORB_CD_PRESENCEOV	0x1
#define		CORB_CD_COLOR(x)	((x >> 12) & 0xf)
#define			CORB_CD_COLOR_UNKNOWN	0x0
#define			CORB_CD_BLACK	0x1
#define			CORB_CD_GRAY	0x2
#define			CORB_CD_BLUE	0x3
#define			CORB_CD_GREEN	0x4
#define			CORB_CD_RED	0x5
#define			CORB_CD_ORANGE	0x6
#define			CORB_CD_YELLOW	0x7
#define			CORB_CD_PURPLE	0x8
#define			CORB_CD_PINK	0x9
#define			CORB_CD_WHITE	0xe
#define			CORB_CD_COLOR_OTHER	0xf
#define		CORB_CD_CONNECTION_OFFSET	16
#define		CORB_CD_CONNECTION_BITS		0xf
#define		CORB_CD_CONNECTION_MASK	(CORB_CD_CONNECTION_BITS << CORB_CD_CONNECTION_OFFSET)
#define		CORB_CD_CONNECTION(x) ((x >> CORB_CD_CONNECTION_OFFSET) & CORB_CD_CONNECTION_BITS)
#define			CORB_CD_CONN_UNKNOWN	0x0
#define			CORB_CD_18		0x1
#define			CORB_CD_14		0x2
#define			CORB_CD_ATAPI		0x3
#define			CORB_CD_RCA		0x4
#define			CORB_CD_OPTICAL		0x5
#define			CORB_CD_OTHER_DIG	0x6
#define			CORB_CD_OTHER_ANALOG	0x7
#define			CORB_CD_DIN		0x8
#define			CORB_CD_XLF		0x9
#define			CORB_CD_RJ11		0xa
#define			CORB_CD_CONN_COMB	0xb
#define			CORB_CD_CONN_OTHER	0xf
#define		CORB_CD_DEVICE_OFFSET		20
#define		CORB_CD_DEVICE_BITS		0xf
#define		CORB_CD_DEVICE_MASK (CORB_CD_DEVICE_BITS << CORB_CD_DEVICE_OFFSET)
#define		CORB_CD_DEVICE(x) ((x >> CORB_CD_DEVICE_OFFSET) & CORB_CD_DEVICE_BITS)
#define			CORB_CD_LINEOUT		0x0
#define			CORB_CD_SPEAKER		0x1
#define			CORB_CD_HEADPHONE	0x2
#define			CORB_CD_CD		0x3
#define			CORB_CD_SPDIFOUT	0x4
#define			CORB_CD_DIGITALOUT	0x5
#define			CORB_CD_MODEMLINE	0x6
#define			CORB_CD_MODEMHANDSET	0x7
#define			CORB_CD_LINEIN		0x8
#define			CORB_CD_AUX		0x9
#define			CORB_CD_MICIN		0xa
#define			CORB_CD_TELEPHONY	0xb
#define			CORB_CD_SPDIFIN		0xc
#define			CORB_CD_DIGITALIN	0xd
#define			CORB_CD_BEEP		0xe
#define			CORB_CD_DEVICE_OTHER	0xf
#define		CORB_CD_LOCATION_MASK	0x3f000000
#define		CORB_CD_LOC_GEO(x)	((x >> 24) & 0xf)
#define			CORB_CD_LOC_GEO_NA	0x0
#define			CORB_CD_REAR		0x1
#define			CORB_CD_FRONT		0x2
#define			CORB_CD_LEFT		0x3
#define			CORB_CD_RIGHT		0x4
#define			CORB_CD_TOP		0x5
#define			CORB_CD_BOTTOM		0x6
#define			CORB_CD_LOC_SPEC0	0x7
#define			CORB_CD_LOC_SPEC1	0x8
#define			CORB_CD_LOC_SPEC2	0x9
#define		CORB_CD_LOC_CHASS(x)	((x >> 28) & 0x3)
#define			CORB_CD_EXTERNAL	0x0
#define			CORB_CD_INTERNAL	0x1
#define			CORB_CD_SEPARATE	0x2
#define			CORB_CD_LOC_OTHER	0x3
#define		CORB_CD_PORT_OFFSET		30
#define		CORB_CD_PORT_BITS		0x3
#define		CORB_CD_PORT_MASK (CORB_CD_PORT_BITS << CORB_CD_PORT_OFFSET)
#define		CORB_CD_PORT(x)	((x >> CORB_CD_PORT_OFFSET) & CORB_CD_PORT_BITS)
#define			CORB_CD_JACK		0x0
#define			CORB_CD_NONE		0x1
#define			CORB_CD_FIXED		0x2
#define			CORB_CD_BOTH		0x3
#define CORB_GET_STRIPE_CONTROL		0xf24
#define CORB_SET_STRIPE_CONTROL		0x720	/* XXX typo in the spec? */
#define CORB_EXECUTE_FUNCTION_RESET	0x7ff

#define CORB_NID_ROOT		0
#define HDA_MAX_CHANNELS	16
#define HDA_MAX_SENSE_PINS	16
#define HDA_MAX_CODECS		15

#define AZ_MAX_VOL_SLAVES	16
#define AZ_TAG_SPKR		0x01
#define AZ_TAG_PLAYVOL		0x02

#define AZ_CLASS_INPUT	0
#define AZ_CLASS_OUTPUT	1
#define AZ_CLASS_RECORD	2

#define AZ_QRK_NONE		0x00000000
#define AZ_QRK_GPIO_MASK	0x00000fff
#define AZ_QRK_GPIO_UNMUTE_0	0x00000001
#define AZ_QRK_GPIO_UNMUTE_1	0x00000002
#define AZ_QRK_GPIO_UNMUTE_2	0x00000004
#define AZ_QRK_GPIO_UNMUTE_3	0x00000008
#define AZ_QRK_GPIO_UNMUTE_4	0x00000010
#define AZ_QRK_GPIO_UNMUTE_5	0x00000020
#define AZ_QRK_GPIO_UNMUTE_6	0x00000040
#define AZ_QRK_GPIO_UNMUTE_7	0x00000080
#define AZ_QRK_GPIO_POL_0	0x00000100
#define AZ_QRK_WID_MASK		0x00fff000
#define AZ_QRK_WID_CDIN_1C	0x00001000
#define AZ_QRK_WID_BEEP_1D	0x00002000
#define AZ_QRK_WID_OVREF50	0x00004000
#define AZ_QRK_WID_AD1981_OAMP	0x00008000
#define AZ_QRK_WID_TPDOCK1	0x00010000
#define AZ_QRK_WID_TPDOCK2	0x00020000
#define AZ_QRK_WID_TPDOCK3	0x00040000
#define AZ_QRK_WID_CLOSE_PCBEEP 0x00080000
#define AZ_QRK_ROUTE_SPKR2_DAC	0x01000000
#define AZ_QRK_DOLBY_ATMOS	0x02000000

/* memory-mapped types */
typedef struct {
	uint32_t low;
	uint32_t high;
	uint32_t length;
	uint32_t flags;
#define	BDLIST_ENTRY_IOC	0x00000001
} __packed bdlist_entry_t;
#define HDA_BDL_MAX	256

typedef struct {
	uint32_t position;
	uint32_t reserved;
} __packed dmaposition_t;

typedef uint32_t corb_entry_t;
typedef struct {
	uint32_t resp;
	uint32_t resp_ex;
#define RIRB_UNSOL_TAG(resp)   ((resp) >> 26)
#define RIRB_RESP_UNSOL                (1 << 4)
#define RIRB_RESP_CODEC(ex)    ((ex) & 0xf)
} __packed rirb_entry_t;


/* #define AZALIA_DEBUG */
#ifdef AZALIA_DEBUG
# define DPRINTF(x)	do { printf x; } while (0/*CONSTCOND*/)
#else
# define DPRINTF(x)	do {} while (0/*CONSTCOND*/)
#endif
#define PTR_UPPER32(x)	((uint64_t)(x) >> 32)

typedef int nid_t;

typedef struct {
	nid_t nid;
	int enable;
	uint32_t widgetcap;
	int type;		/* = bit20-24 of widgetcap */
	nid_t parent;
	int mixer_class;
	int nconnections;
	nid_t *connections;
	int selected;
	uint32_t inamp_cap;
	uint32_t outamp_cap;
	char name[MAX_AUDIO_DEV_LEN];
	union {
		struct {	/* for AUDIO_INPUT/OUTPUT */
			uint32_t encodings;
			uint32_t bits_rates;
		} audio;
		struct {	/* for PIN */
			uint32_t cap;
			uint32_t config;
			int sequence;
			int association;
			int color;
			int device;
		} pin;
		struct {	/* for VOLUME_KNOB */
			uint32_t cap;
		} volume;
	} d;
} widget_t;
#define	WIDGET_CHANNELS(w)	((w)->widgetcap & COP_AWCAP_STEREO ? 2 : 1)

typedef struct {
	mixer_devinfo_t devinfo;
	nid_t nid;		/* target NID; 0 is invalid. */
	int target;		/* 0-15: inamp index, 0x100: outamp, ... */
#define IS_MI_TARGET_INAMP(x)	((x) <= 15)
#define MI_TARGET_INAMP(x)	(x)
#define MI_TARGET_OUTAMP	0x100
#define MI_TARGET_CONNLIST	0x101
#define MI_TARGET_PINDIR	0x102 /* for bidirectional pin */
#define MI_TARGET_PINBOOST	0x103 /* for headphone pin */
#define MI_TARGET_DAC		0x104
#define MI_TARGET_ADC		0x105
#define MI_TARGET_VOLUME	0x106
#define MI_TARGET_SPDIF		0x107
#define MI_TARGET_SPDIF_CC	0x108
#define MI_TARGET_EAPD		0x109
#define MI_TARGET_MUTESET	0x10a
#define MI_TARGET_PINSENSE	0x10b
#define MI_TARGET_SENSESET	0x10c
#define MI_TARGET_PLAYVOL	0x10d
#define MI_TARGET_RECVOL	0x10e
#define MI_TARGET_MIXERSET	0x10f
	union {
		int ord;
		int mask;
		mixer_level_t value;
	} saved;
} mixer_item_t;

#define VALID_WIDGET_NID(nid, codec)	(nid == (codec)->audiofunc || \
					 (nid >= (codec)->wstart &&   \
					  nid < (codec)->wend))

typedef struct {
	int nconv;
	nid_t conv[HDA_MAX_CHANNELS];
} convgroup_t;
typedef struct {
	int cur;
	int ngroups;
	convgroup_t groups[2];
} convgroupset_t;

typedef struct {
	int master;
	int vol_l;
	int vol_r;
	int mute;
	int hw_step;
	int hw_nsteps;
	nid_t slaves[AZ_MAX_VOL_SLAVES];
	int nslaves;
	int mask;
	int cur;
} volgroup_t;

struct io_pin {
	nid_t nid;		/* NID of pin */
	nid_t conv;		/* NID of default converter */
	int prio;		/* assoc/seq/dir "priority" */
};

typedef struct codec_t {
	struct azalia_t *az;
	uint32_t vid;		/* codec vendor/device ID */
	uint32_t subid;		/* PCI subvendor/device ID */
	const char *name;
	int address;
	int nfunctions;
	nid_t audiofunc;	/* NID of an audio function node */
	nid_t wstart;		/* start NID of audio widgets */
	nid_t wend;		/* the last NID of audio widgets + 1 */
	widget_t *w;		/* widgets in the audio function.
				 * w[0] to w[wstart-1] are unused. */
#define FOR_EACH_WIDGET(this, i)	for (i = (this)->wstart; i < (this)->wend; i++)

	int codec_type;
#define AZ_CODEC_TYPE_ANALOG	0
#define AZ_CODEC_TYPE_DIGITAL	1
#define AZ_CODEC_TYPE_HDMI	2

	int qrks;

	convgroupset_t dacs;
	convgroupset_t adcs;
	int running;

	int nmixers, maxmixers;
	mixer_item_t *mixers;

	struct audio_format *formats;
	int nformats;

	struct io_pin *ipins;
	int nipins;
	struct io_pin *ipins_d;
	int nipins_d;
	struct io_pin *opins;
	int nopins;
	struct io_pin *opins_d;
	int nopins_d;

	nid_t a_dacs[HDA_MAX_CHANNELS], a_dacs_d[HDA_MAX_CHANNELS];
	int na_dacs, na_dacs_d;
	nid_t a_adcs[HDA_MAX_CHANNELS], a_adcs_d[HDA_MAX_CHANNELS];
	int na_adcs, na_adcs_d;

	nid_t mic;		/* fixed (internal) mic */
	nid_t mic_adc;
	nid_t speaker;		/* fixed (internal) speaker */
	nid_t speaker2;		/* 2nd fixed (internal) speaker */
	nid_t spkr_dac;		/* default DAC for speaker and speaker2 */
	nid_t input_mixer;
	nid_t fhp;		/* front headphone jack */
	nid_t fhp_dac;
	int nout_jacks;		/* number of default output jacks */

	int spkr_muted;
	int spkr_muters;
	int spkr_mute_method;
#define	AZ_SPKR_MUTE_NONE	0
#define	AZ_SPKR_MUTE_SPKR_MUTE	1
#define	AZ_SPKR_MUTE_SPKR_DIR	2
#define	AZ_SPKR_MUTE_DAC_MUTE	3

	volgroup_t playvols;
	volgroup_t recvols;

	nid_t sense_pins[HDA_MAX_SENSE_PINS];
	int nsense_pins;
} codec_t;

int	azalia_codec_init_vtbl(codec_t *);
int	azalia_codec_construct_format(codec_t *, int, int);
int	azalia_widget_enabled(const codec_t *, nid_t);
int	azalia_codec_gpio_quirks(codec_t *);
int	azalia_codec_widget_quirks(codec_t *, nid_t);
int	azalia_codec_fnode(codec_t *, nid_t, int, int);

int	azalia_init_dacgroup(codec_t *);
int	azalia_mixer_init(codec_t *);
int	azalia_mixer_delete(codec_t *);
int	azalia_unsol_event(codec_t *, int);
int	azalia_comresp(const codec_t *, nid_t, uint32_t, uint32_t, uint32_t *);
int	azalia_mixer_get(const codec_t *, nid_t, int, mixer_ctrl_t *);
int	azalia_mixer_set(codec_t *, nid_t, int, const mixer_ctrl_t *);

int	azalia_codec_enable_unsol(codec_t *);

void	azalia_codec_init_dolby_atmos(codec_t *);
