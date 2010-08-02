/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef __SOUND_HDA_CODEC_H
#define __SOUND_HDA_CODEC_H

#include <sound/info.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/hwdep.h>

#if defined(CONFIG_PM) || defined(CONFIG_SND_HDA_POWER_SAVE)
#define SND_HDA_NEEDS_RESUME	/* resume control code is required */
#endif

/*
 * nodes
 */
#define	AC_NODE_ROOT		0x00

/*
 * function group types
 */
enum {
	AC_GRP_AUDIO_FUNCTION = 0x01,
	AC_GRP_MODEM_FUNCTION = 0x02,
};
	
/*
 * widget types
 */
enum {
	AC_WID_AUD_OUT,		/* Audio Out */
	AC_WID_AUD_IN,		/* Audio In */
	AC_WID_AUD_MIX,		/* Audio Mixer */
	AC_WID_AUD_SEL,		/* Audio Selector */
	AC_WID_PIN,		/* Pin Complex */
	AC_WID_POWER,		/* Power */
	AC_WID_VOL_KNB,		/* Volume Knob */
	AC_WID_BEEP,		/* Beep Generator */
	AC_WID_VENDOR = 0x0f	/* Vendor specific */
};

/*
 * GET verbs
 */
#define AC_VERB_GET_STREAM_FORMAT		0x0a00
#define AC_VERB_GET_AMP_GAIN_MUTE		0x0b00
#define AC_VERB_GET_PROC_COEF			0x0c00
#define AC_VERB_GET_COEF_INDEX			0x0d00
#define AC_VERB_PARAMETERS			0x0f00
#define AC_VERB_GET_CONNECT_SEL			0x0f01
#define AC_VERB_GET_CONNECT_LIST		0x0f02
#define AC_VERB_GET_PROC_STATE			0x0f03
#define AC_VERB_GET_SDI_SELECT			0x0f04
#define AC_VERB_GET_POWER_STATE			0x0f05
#define AC_VERB_GET_CONV			0x0f06
#define AC_VERB_GET_PIN_WIDGET_CONTROL		0x0f07
#define AC_VERB_GET_UNSOLICITED_RESPONSE	0x0f08
#define AC_VERB_GET_PIN_SENSE			0x0f09
#define AC_VERB_GET_BEEP_CONTROL		0x0f0a
#define AC_VERB_GET_EAPD_BTLENABLE		0x0f0c
#define AC_VERB_GET_DIGI_CONVERT_1		0x0f0d
#define AC_VERB_GET_DIGI_CONVERT_2		0x0f0e /* unused */
#define AC_VERB_GET_VOLUME_KNOB_CONTROL		0x0f0f
/* f10-f1a: GPIO */
#define AC_VERB_GET_GPIO_DATA			0x0f15
#define AC_VERB_GET_GPIO_MASK			0x0f16
#define AC_VERB_GET_GPIO_DIRECTION		0x0f17
#define AC_VERB_GET_GPIO_WAKE_MASK		0x0f18
#define AC_VERB_GET_GPIO_UNSOLICITED_RSP_MASK	0x0f19
#define AC_VERB_GET_GPIO_STICKY_MASK		0x0f1a
#define AC_VERB_GET_CONFIG_DEFAULT		0x0f1c
/* f20: AFG/MFG */
#define AC_VERB_GET_SUBSYSTEM_ID		0x0f20
#define AC_VERB_GET_CVT_CHAN_COUNT		0x0f2d
#define AC_VERB_GET_HDMI_DIP_SIZE		0x0f2e
#define AC_VERB_GET_HDMI_ELDD			0x0f2f
#define AC_VERB_GET_HDMI_DIP_INDEX		0x0f30
#define AC_VERB_GET_HDMI_DIP_DATA		0x0f31
#define AC_VERB_GET_HDMI_DIP_XMIT		0x0f32
#define AC_VERB_GET_HDMI_CP_CTRL		0x0f33
#define AC_VERB_GET_HDMI_CHAN_SLOT		0x0f34

/*
 * SET verbs
 */
#define AC_VERB_SET_STREAM_FORMAT		0x200
#define AC_VERB_SET_AMP_GAIN_MUTE		0x300
#define AC_VERB_SET_PROC_COEF			0x400
#define AC_VERB_SET_COEF_INDEX			0x500
#define AC_VERB_SET_CONNECT_SEL			0x701
#define AC_VERB_SET_PROC_STATE			0x703
#define AC_VERB_SET_SDI_SELECT			0x704
#define AC_VERB_SET_POWER_STATE			0x705
#define AC_VERB_SET_CHANNEL_STREAMID		0x706
#define AC_VERB_SET_PIN_WIDGET_CONTROL		0x707
#define AC_VERB_SET_UNSOLICITED_ENABLE		0x708
#define AC_VERB_SET_PIN_SENSE			0x709
#define AC_VERB_SET_BEEP_CONTROL		0x70a
#define AC_VERB_SET_EAPD_BTLENABLE		0x70c
#define AC_VERB_SET_DIGI_CONVERT_1		0x70d
#define AC_VERB_SET_DIGI_CONVERT_2		0x70e
#define AC_VERB_SET_VOLUME_KNOB_CONTROL		0x70f
#define AC_VERB_SET_GPIO_DATA			0x715
#define AC_VERB_SET_GPIO_MASK			0x716
#define AC_VERB_SET_GPIO_DIRECTION		0x717
#define AC_VERB_SET_GPIO_WAKE_MASK		0x718
#define AC_VERB_SET_GPIO_UNSOLICITED_RSP_MASK	0x719
#define AC_VERB_SET_GPIO_STICKY_MASK		0x71a
#define AC_VERB_SET_CONFIG_DEFAULT_BYTES_0	0x71c
#define AC_VERB_SET_CONFIG_DEFAULT_BYTES_1	0x71d
#define AC_VERB_SET_CONFIG_DEFAULT_BYTES_2	0x71e
#define AC_VERB_SET_CONFIG_DEFAULT_BYTES_3	0x71f
#define AC_VERB_SET_EAPD				0x788
#define AC_VERB_SET_CODEC_RESET			0x7ff
#define AC_VERB_SET_CVT_CHAN_COUNT		0x72d
#define AC_VERB_SET_HDMI_DIP_INDEX		0x730
#define AC_VERB_SET_HDMI_DIP_DATA		0x731
#define AC_VERB_SET_HDMI_DIP_XMIT		0x732
#define AC_VERB_SET_HDMI_CP_CTRL		0x733
#define AC_VERB_SET_HDMI_CHAN_SLOT		0x734

/*
 * Parameter IDs
 */
#define AC_PAR_VENDOR_ID		0x00
#define AC_PAR_SUBSYSTEM_ID		0x01
#define AC_PAR_REV_ID			0x02
#define AC_PAR_NODE_COUNT		0x04
#define AC_PAR_FUNCTION_TYPE		0x05
#define AC_PAR_AUDIO_FG_CAP		0x08
#define AC_PAR_AUDIO_WIDGET_CAP		0x09
#define AC_PAR_PCM			0x0a
#define AC_PAR_STREAM			0x0b
#define AC_PAR_PIN_CAP			0x0c
#define AC_PAR_AMP_IN_CAP		0x0d
#define AC_PAR_CONNLIST_LEN		0x0e
#define AC_PAR_POWER_STATE		0x0f
#define AC_PAR_PROC_CAP			0x10
#define AC_PAR_GPIO_CAP			0x11
#define AC_PAR_AMP_OUT_CAP		0x12
#define AC_PAR_VOL_KNB_CAP		0x13
#define AC_PAR_HDMI_LPCM_CAP		0x20

/*
 * AC_VERB_PARAMETERS results (32bit)
 */

/* Function Group Type */
#define AC_FGT_TYPE			(0xff<<0)
#define AC_FGT_TYPE_SHIFT		0
#define AC_FGT_UNSOL_CAP		(1<<8)

/* Audio Function Group Capabilities */
#define AC_AFG_OUT_DELAY		(0xf<<0)
#define AC_AFG_IN_DELAY			(0xf<<8)
#define AC_AFG_BEEP_GEN			(1<<16)

/* Audio Widget Capabilities */
#define AC_WCAP_STEREO			(1<<0)	/* stereo I/O */
#define AC_WCAP_IN_AMP			(1<<1)	/* AMP-in present */
#define AC_WCAP_OUT_AMP			(1<<2)	/* AMP-out present */
#define AC_WCAP_AMP_OVRD		(1<<3)	/* AMP-parameter override */
#define AC_WCAP_FORMAT_OVRD		(1<<4)	/* format override */
#define AC_WCAP_STRIPE			(1<<5)	/* stripe */
#define AC_WCAP_PROC_WID		(1<<6)	/* Proc Widget */
#define AC_WCAP_UNSOL_CAP		(1<<7)	/* Unsol capable */
#define AC_WCAP_CONN_LIST		(1<<8)	/* connection list */
#define AC_WCAP_DIGITAL			(1<<9)	/* digital I/O */
#define AC_WCAP_POWER			(1<<10)	/* power control */
#define AC_WCAP_LR_SWAP			(1<<11)	/* L/R swap */
#define AC_WCAP_CP_CAPS			(1<<12) /* content protection */
#define AC_WCAP_CHAN_CNT_EXT		(7<<13)	/* channel count ext */
#define AC_WCAP_DELAY			(0xf<<16)
#define AC_WCAP_DELAY_SHIFT		16
#define AC_WCAP_TYPE			(0xf<<20)
#define AC_WCAP_TYPE_SHIFT		20

/* supported PCM rates and bits */
#define AC_SUPPCM_RATES			(0xfff << 0)
#define AC_SUPPCM_BITS_8		(1<<16)
#define AC_SUPPCM_BITS_16		(1<<17)
#define AC_SUPPCM_BITS_20		(1<<18)
#define AC_SUPPCM_BITS_24		(1<<19)
#define AC_SUPPCM_BITS_32		(1<<20)

/* supported PCM stream format */
#define AC_SUPFMT_PCM			(1<<0)
#define AC_SUPFMT_FLOAT32		(1<<1)
#define AC_SUPFMT_AC3			(1<<2)

/* GP I/O count */
#define AC_GPIO_IO_COUNT		(0xff<<0)
#define AC_GPIO_O_COUNT			(0xff<<8)
#define AC_GPIO_O_COUNT_SHIFT		8
#define AC_GPIO_I_COUNT			(0xff<<16)
#define AC_GPIO_I_COUNT_SHIFT		16
#define AC_GPIO_UNSOLICITED		(1<<30)
#define AC_GPIO_WAKE			(1<<31)

/* Converter stream, channel */
#define AC_CONV_CHANNEL			(0xf<<0)
#define AC_CONV_STREAM			(0xf<<4)
#define AC_CONV_STREAM_SHIFT		4

/* Input converter SDI select */
#define AC_SDI_SELECT			(0xf<<0)

/* Unsolicited response control */
#define AC_UNSOL_TAG			(0x3f<<0)
#define AC_UNSOL_ENABLED		(1<<7)
#define AC_USRSP_EN			AC_UNSOL_ENABLED

/* Unsolicited responses */
#define AC_UNSOL_RES_TAG		(0x3f<<26)
#define AC_UNSOL_RES_TAG_SHIFT		26
#define AC_UNSOL_RES_SUBTAG		(0x1f<<21)
#define AC_UNSOL_RES_SUBTAG_SHIFT	21
#define AC_UNSOL_RES_ELDV		(1<<1)	/* ELD Data valid (for HDMI) */
#define AC_UNSOL_RES_PD			(1<<0)	/* pinsense detect */
#define AC_UNSOL_RES_CP_STATE		(1<<1)	/* content protection */
#define AC_UNSOL_RES_CP_READY		(1<<0)	/* content protection */

/* Pin widget capabilies */
#define AC_PINCAP_IMP_SENSE		(1<<0)	/* impedance sense capable */
#define AC_PINCAP_TRIG_REQ		(1<<1)	/* trigger required */
#define AC_PINCAP_PRES_DETECT		(1<<2)	/* presence detect capable */
#define AC_PINCAP_HP_DRV		(1<<3)	/* headphone drive capable */
#define AC_PINCAP_OUT			(1<<4)	/* output capable */
#define AC_PINCAP_IN			(1<<5)	/* input capable */
#define AC_PINCAP_BALANCE		(1<<6)	/* balanced I/O capable */
/* Note: This LR_SWAP pincap is defined in the Realtek ALC883 specification,
 *       but is marked reserved in the Intel HDA specification.
 */
#define AC_PINCAP_LR_SWAP		(1<<7)	/* L/R swap */
/* Note: The same bit as LR_SWAP is newly defined as HDMI capability
 *       in HD-audio specification
 */
#define AC_PINCAP_HDMI			(1<<7)	/* HDMI pin */
#define AC_PINCAP_DP			(1<<24)	/* DisplayPort pin, can
						 * coexist with AC_PINCAP_HDMI
						 */
#define AC_PINCAP_VREF			(0x37<<8)
#define AC_PINCAP_VREF_SHIFT		8
#define AC_PINCAP_EAPD			(1<<16)	/* EAPD capable */
#define AC_PINCAP_HBR			(1<<27)	/* High Bit Rate */
/* Vref status (used in pin cap) */
#define AC_PINCAP_VREF_HIZ		(1<<0)	/* Hi-Z */
#define AC_PINCAP_VREF_50		(1<<1)	/* 50% */
#define AC_PINCAP_VREF_GRD		(1<<2)	/* ground */
#define AC_PINCAP_VREF_80		(1<<4)	/* 80% */
#define AC_PINCAP_VREF_100		(1<<5)	/* 100% */

/* Amplifier capabilities */
#define AC_AMPCAP_OFFSET		(0x7f<<0)  /* 0dB offset */
#define AC_AMPCAP_OFFSET_SHIFT		0
#define AC_AMPCAP_NUM_STEPS		(0x7f<<8)  /* number of steps */
#define AC_AMPCAP_NUM_STEPS_SHIFT	8
#define AC_AMPCAP_STEP_SIZE		(0x7f<<16) /* step size 0-32dB
						    * in 0.25dB
						    */
#define AC_AMPCAP_STEP_SIZE_SHIFT	16
#define AC_AMPCAP_MUTE			(1<<31)    /* mute capable */
#define AC_AMPCAP_MUTE_SHIFT		31

/* Connection list */
#define AC_CLIST_LENGTH			(0x7f<<0)
#define AC_CLIST_LONG			(1<<7)

/* Supported power status */
#define AC_PWRST_D0SUP			(1<<0)
#define AC_PWRST_D1SUP			(1<<1)
#define AC_PWRST_D2SUP			(1<<2)
#define AC_PWRST_D3SUP			(1<<3)
#define AC_PWRST_D3COLDSUP		(1<<4)
#define AC_PWRST_S3D3COLDSUP		(1<<29)
#define AC_PWRST_CLKSTOP		(1<<30)
#define AC_PWRST_EPSS			(1U<<31)

/* Power state values */
#define AC_PWRST_SETTING		(0xf<<0)
#define AC_PWRST_ACTUAL			(0xf<<4)
#define AC_PWRST_ACTUAL_SHIFT		4
#define AC_PWRST_D0			0x00
#define AC_PWRST_D1			0x01
#define AC_PWRST_D2			0x02
#define AC_PWRST_D3			0x03

/* Processing capabilies */
#define AC_PCAP_BENIGN			(1<<0)
#define AC_PCAP_NUM_COEF		(0xff<<8)
#define AC_PCAP_NUM_COEF_SHIFT		8

/* Volume knobs capabilities */
#define AC_KNBCAP_NUM_STEPS		(0x7f<<0)
#define AC_KNBCAP_DELTA			(1<<7)

/* HDMI LPCM capabilities */
#define AC_LPCMCAP_48K_CP_CHNS		(0x0f<<0) /* max channels w/ CP-on */	
#define AC_LPCMCAP_48K_NO_CHNS		(0x0f<<4) /* max channels w/o CP-on */
#define AC_LPCMCAP_48K_20BIT		(1<<8)	/* 20b bitrate supported */
#define AC_LPCMCAP_48K_24BIT		(1<<9)	/* 24b bitrate supported */
#define AC_LPCMCAP_96K_CP_CHNS		(0x0f<<10) /* max channels w/ CP-on */	
#define AC_LPCMCAP_96K_NO_CHNS		(0x0f<<14) /* max channels w/o CP-on */
#define AC_LPCMCAP_96K_20BIT		(1<<18)	/* 20b bitrate supported */
#define AC_LPCMCAP_96K_24BIT		(1<<19)	/* 24b bitrate supported */
#define AC_LPCMCAP_192K_CP_CHNS		(0x0f<<20) /* max channels w/ CP-on */	
#define AC_LPCMCAP_192K_NO_CHNS		(0x0f<<24) /* max channels w/o CP-on */
#define AC_LPCMCAP_192K_20BIT		(1<<28)	/* 20b bitrate supported */
#define AC_LPCMCAP_192K_24BIT		(1<<29)	/* 24b bitrate supported */
#define AC_LPCMCAP_44K			(1<<30)	/* 44.1kHz support */
#define AC_LPCMCAP_44K_MS		(1<<31)	/* 44.1kHz-multiplies support */

/*
 * Control Parameters
 */

/* Amp gain/mute */
#define AC_AMP_MUTE			(1<<7)
#define AC_AMP_GAIN			(0x7f)
#define AC_AMP_GET_INDEX		(0xf<<0)

#define AC_AMP_GET_LEFT			(1<<13)
#define AC_AMP_GET_RIGHT		(0<<13)
#define AC_AMP_GET_OUTPUT		(1<<15)
#define AC_AMP_GET_INPUT		(0<<15)

#define AC_AMP_SET_INDEX		(0xf<<8)
#define AC_AMP_SET_INDEX_SHIFT		8
#define AC_AMP_SET_RIGHT		(1<<12)
#define AC_AMP_SET_LEFT			(1<<13)
#define AC_AMP_SET_INPUT		(1<<14)
#define AC_AMP_SET_OUTPUT		(1<<15)

/* DIGITAL1 bits */
#define AC_DIG1_ENABLE			(1<<0)
#define AC_DIG1_V			(1<<1)
#define AC_DIG1_VCFG			(1<<2)
#define AC_DIG1_EMPHASIS		(1<<3)
#define AC_DIG1_COPYRIGHT		(1<<4)
#define AC_DIG1_NONAUDIO		(1<<5)
#define AC_DIG1_PROFESSIONAL		(1<<6)
#define AC_DIG1_LEVEL			(1<<7)

/* DIGITAL2 bits */
#define AC_DIG2_CC			(0x7f<<0)

/* Pin widget control - 8bit */
#define AC_PINCTL_VREFEN		(0x7<<0)
#define AC_PINCTL_VREF_HIZ		0	/* Hi-Z */
#define AC_PINCTL_VREF_50		1	/* 50% */
#define AC_PINCTL_VREF_GRD		2	/* ground */
#define AC_PINCTL_VREF_80		4	/* 80% */
#define AC_PINCTL_VREF_100		5	/* 100% */
#define AC_PINCTL_IN_EN			(1<<5)
#define AC_PINCTL_OUT_EN		(1<<6)
#define AC_PINCTL_HP_EN			(1<<7)

/* Pin sense - 32bit */
#define AC_PINSENSE_IMPEDANCE_MASK	(0x7fffffff)
#define AC_PINSENSE_PRESENCE		(1<<31)
#define AC_PINSENSE_ELDV		(1<<30)	/* ELD valid (HDMI) */

/* EAPD/BTL enable - 32bit */
#define AC_EAPDBTL_BALANCED		(1<<0)
#define AC_EAPDBTL_EAPD			(1<<1)
#define AC_EAPDBTL_LR_SWAP		(1<<2)

/* HDMI ELD data */
#define AC_ELDD_ELD_VALID		(1<<31)
#define AC_ELDD_ELD_DATA		0xff

/* HDMI DIP size */
#define AC_DIPSIZE_ELD_BUF		(1<<3) /* ELD buf size of packet size */
#define AC_DIPSIZE_PACK_IDX		(0x07<<0) /* packet index */

/* HDMI DIP index */
#define AC_DIPIDX_PACK_IDX		(0x07<<5) /* packet idnex */
#define AC_DIPIDX_BYTE_IDX		(0x1f<<0) /* byte index */

/* HDMI DIP xmit (transmit) control */
#define AC_DIPXMIT_MASK			(0x3<<6)
#define AC_DIPXMIT_DISABLE		(0x0<<6) /* disable xmit */
#define AC_DIPXMIT_ONCE			(0x2<<6) /* xmit once then disable */
#define AC_DIPXMIT_BEST			(0x3<<6) /* best effort */

/* HDMI content protection (CP) control */
#define AC_CPCTRL_CES			(1<<9) /* current encryption state */
#define AC_CPCTRL_READY			(1<<8) /* ready bit */
#define AC_CPCTRL_SUBTAG		(0x1f<<3) /* subtag for unsol-resp */
#define AC_CPCTRL_STATE			(3<<0) /* current CP request state */

/* Converter channel <-> HDMI slot mapping */
#define AC_CVTMAP_HDMI_SLOT		(0xf<<0) /* HDMI slot number */
#define AC_CVTMAP_CHAN			(0xf<<4) /* converter channel number */

/* configuration default - 32bit */
#define AC_DEFCFG_SEQUENCE		(0xf<<0)
#define AC_DEFCFG_DEF_ASSOC		(0xf<<4)
#define AC_DEFCFG_ASSOC_SHIFT		4
#define AC_DEFCFG_MISC			(0xf<<8)
#define AC_DEFCFG_MISC_SHIFT		8
#define AC_DEFCFG_MISC_NO_PRESENCE	(1<<0)
#define AC_DEFCFG_COLOR			(0xf<<12)
#define AC_DEFCFG_COLOR_SHIFT		12
#define AC_DEFCFG_CONN_TYPE		(0xf<<16)
#define AC_DEFCFG_CONN_TYPE_SHIFT	16
#define AC_DEFCFG_DEVICE		(0xf<<20)
#define AC_DEFCFG_DEVICE_SHIFT		20
#define AC_DEFCFG_LOCATION		(0x3f<<24)
#define AC_DEFCFG_LOCATION_SHIFT	24
#define AC_DEFCFG_PORT_CONN		(0x3<<30)
#define AC_DEFCFG_PORT_CONN_SHIFT	30

/* device device types (0x0-0xf) */
enum {
	AC_JACK_LINE_OUT,
	AC_JACK_SPEAKER,
	AC_JACK_HP_OUT,
	AC_JACK_CD,
	AC_JACK_SPDIF_OUT,
	AC_JACK_DIG_OTHER_OUT,
	AC_JACK_MODEM_LINE_SIDE,
	AC_JACK_MODEM_HAND_SIDE,
	AC_JACK_LINE_IN,
	AC_JACK_AUX,
	AC_JACK_MIC_IN,
	AC_JACK_TELEPHONY,
	AC_JACK_SPDIF_IN,
	AC_JACK_DIG_OTHER_IN,
	AC_JACK_OTHER = 0xf,
};

/* jack connection types (0x0-0xf) */
enum {
	AC_JACK_CONN_UNKNOWN,
	AC_JACK_CONN_1_8,
	AC_JACK_CONN_1_4,
	AC_JACK_CONN_ATAPI,
	AC_JACK_CONN_RCA,
	AC_JACK_CONN_OPTICAL,
	AC_JACK_CONN_OTHER_DIGITAL,
	AC_JACK_CONN_OTHER_ANALOG,
	AC_JACK_CONN_DIN,
	AC_JACK_CONN_XLR,
	AC_JACK_CONN_RJ11,
	AC_JACK_CONN_COMB,
	AC_JACK_CONN_OTHER = 0xf,
};

/* jack colors (0x0-0xf) */
enum {
	AC_JACK_COLOR_UNKNOWN,
	AC_JACK_COLOR_BLACK,
	AC_JACK_COLOR_GREY,
	AC_JACK_COLOR_BLUE,
	AC_JACK_COLOR_GREEN,
	AC_JACK_COLOR_RED,
	AC_JACK_COLOR_ORANGE,
	AC_JACK_COLOR_YELLOW,
	AC_JACK_COLOR_PURPLE,
	AC_JACK_COLOR_PINK,
	AC_JACK_COLOR_WHITE = 0xe,
	AC_JACK_COLOR_OTHER,
};

/* Jack location (0x0-0x3f) */
/* common case */
enum {
	AC_JACK_LOC_NONE,
	AC_JACK_LOC_REAR,
	AC_JACK_LOC_FRONT,
	AC_JACK_LOC_LEFT,
	AC_JACK_LOC_RIGHT,
	AC_JACK_LOC_TOP,
	AC_JACK_LOC_BOTTOM,
};
/* bits 4-5 */
enum {
	AC_JACK_LOC_EXTERNAL = 0x00,
	AC_JACK_LOC_INTERNAL = 0x10,
	AC_JACK_LOC_SEPARATE = 0x20,
	AC_JACK_LOC_OTHER    = 0x30,
};
enum {
	/* external on primary chasis */
	AC_JACK_LOC_REAR_PANEL = 0x07,
	AC_JACK_LOC_DRIVE_BAY,
	/* internal */
	AC_JACK_LOC_RISER = 0x17,
	AC_JACK_LOC_HDMI,
	AC_JACK_LOC_ATAPI,
	/* others */
	AC_JACK_LOC_MOBILE_IN = 0x37,
	AC_JACK_LOC_MOBILE_OUT,
};

/* Port connectivity (0-3) */
enum {
	AC_JACK_PORT_COMPLEX,
	AC_JACK_PORT_NONE,
	AC_JACK_PORT_FIXED,
	AC_JACK_PORT_BOTH,
};

/* max. connections to a widget */
#define HDA_MAX_CONNECTIONS	32

/* max. codec address */
#define HDA_MAX_CODEC_ADDRESS	0x0f

/* max number of PCM devics per card */
#define HDA_MAX_PCMS		10

/*
 * generic arrays
 */
struct snd_array {
	unsigned int used;
	unsigned int alloced;
	unsigned int elem_size;
	unsigned int alloc_align;
	void *list;
};

void *snd_array_new(struct snd_array *array);
void snd_array_free(struct snd_array *array);
static inline void snd_array_init(struct snd_array *array, unsigned int size,
				  unsigned int align)
{
	array->elem_size = size;
	array->alloc_align = align;
}

static inline void *snd_array_elem(struct snd_array *array, unsigned int idx)
{
	return array->list + idx * array->elem_size;
}

static inline unsigned int snd_array_index(struct snd_array *array, void *ptr)
{
	return (unsigned long)(ptr - array->list) / array->elem_size;
}

/*
 * Structures
 */

struct hda_bus;
struct hda_beep;
struct hda_codec;
struct hda_pcm;
struct hda_pcm_stream;
struct hda_bus_unsolicited;

/* NID type */
typedef u16 hda_nid_t;

/* bus operators */
struct hda_bus_ops {
	/* send a single command */
	int (*command)(struct hda_bus *bus, unsigned int cmd);
	/* get a response from the last command */
	unsigned int (*get_response)(struct hda_bus *bus, unsigned int addr);
	/* free the private data */
	void (*private_free)(struct hda_bus *);
	/* attach a PCM stream */
	int (*attach_pcm)(struct hda_bus *bus, struct hda_codec *codec,
			  struct hda_pcm *pcm);
	/* reset bus for retry verb */
	void (*bus_reset)(struct hda_bus *bus);
#ifdef CONFIG_SND_HDA_POWER_SAVE
	/* notify power-up/down from codec to controller */
	void (*pm_notify)(struct hda_bus *bus);
#endif
};

/* template to pass to the bus constructor */
struct hda_bus_template {
	void *private_data;
	struct pci_dev *pci;
	const char *modelname;
	int *power_save;
	struct hda_bus_ops ops;
};

/*
 * codec bus
 *
 * each controller needs to creata a hda_bus to assign the accessor.
 * A hda_bus contains several codecs in the list codec_list.
 */
struct hda_bus {
	struct snd_card *card;

	/* copied from template */
	void *private_data;
	struct pci_dev *pci;
	const char *modelname;
	int *power_save;
	struct hda_bus_ops ops;

	/* codec linked list */
	struct list_head codec_list;
	/* link caddr -> codec */
	struct hda_codec *caddr_tbl[HDA_MAX_CODEC_ADDRESS + 1];

	struct mutex cmd_mutex;

	/* unsolicited event queue */
	struct hda_bus_unsolicited *unsol;
	char workq_name[16];
	struct workqueue_struct *workq;	/* common workqueue for codecs */

	/* assigned PCMs */
	DECLARE_BITMAP(pcm_dev_bits, SNDRV_PCM_DEVICES);

	/* misc op flags */
	unsigned int needs_damn_long_delay :1;
	unsigned int allow_bus_reset:1;	/* allow bus reset at fatal error */
	unsigned int sync_write:1;	/* sync after verb write */
	/* status for codec/controller */
	unsigned int shutdown :1;	/* being unloaded */
	unsigned int rirb_error:1;	/* error in codec communication */
	unsigned int response_reset:1;	/* controller was reset */
	unsigned int in_reset:1;	/* during reset operation */
	unsigned int power_keep_link_on:1; /* don't power off HDA link */
};

/*
 * codec preset
 *
 * Known codecs have the patch to build and set up the controls/PCMs
 * better than the generic parser.
 */
struct hda_codec_preset {
	unsigned int id;
	unsigned int mask;
	unsigned int subs;
	unsigned int subs_mask;
	unsigned int rev;
	hda_nid_t afg, mfg;
	const char *name;
	int (*patch)(struct hda_codec *codec);
};
	
struct hda_codec_preset_list {
	const struct hda_codec_preset *preset;
	struct module *owner;
	struct list_head list;
};

/* initial hook */
int snd_hda_add_codec_preset(struct hda_codec_preset_list *preset);
int snd_hda_delete_codec_preset(struct hda_codec_preset_list *preset);

/* ops set by the preset patch */
struct hda_codec_ops {
	int (*build_controls)(struct hda_codec *codec);
	int (*build_pcms)(struct hda_codec *codec);
	int (*init)(struct hda_codec *codec);
	void (*free)(struct hda_codec *codec);
	void (*unsol_event)(struct hda_codec *codec, unsigned int res);
#ifdef SND_HDA_NEEDS_RESUME
	int (*suspend)(struct hda_codec *codec, pm_message_t state);
	int (*resume)(struct hda_codec *codec);
#endif
#ifdef CONFIG_SND_HDA_POWER_SAVE
	int (*check_power_status)(struct hda_codec *codec, hda_nid_t nid);
#endif
	void (*reboot_notify)(struct hda_codec *codec);
};

/* record for amp information cache */
struct hda_cache_head {
	u32 key;		/* hash key */
	u16 val;		/* assigned value */
	u16 next;		/* next link; -1 = terminal */
};

struct hda_amp_info {
	struct hda_cache_head head;
	u32 amp_caps;		/* amp capabilities */
	u16 vol[2];		/* current volume & mute */
};

struct hda_cache_rec {
	u16 hash[64];			/* hash table for index */
	struct snd_array buf;		/* record entries */
};

/* PCM callbacks */
struct hda_pcm_ops {
	int (*open)(struct hda_pcm_stream *info, struct hda_codec *codec,
		    struct snd_pcm_substream *substream);
	int (*close)(struct hda_pcm_stream *info, struct hda_codec *codec,
		     struct snd_pcm_substream *substream);
	int (*prepare)(struct hda_pcm_stream *info, struct hda_codec *codec,
		       unsigned int stream_tag, unsigned int format,
		       struct snd_pcm_substream *substream);
	int (*cleanup)(struct hda_pcm_stream *info, struct hda_codec *codec,
		       struct snd_pcm_substream *substream);
};

/* PCM information for each substream */
struct hda_pcm_stream {
	unsigned int substreams;	/* number of substreams, 0 = not exist*/
	unsigned int channels_min;	/* min. number of channels */
	unsigned int channels_max;	/* max. number of channels */
	hda_nid_t nid;	/* default NID to query rates/formats/bps, or set up */
	u32 rates;	/* supported rates */
	u64 formats;	/* supported formats (SNDRV_PCM_FMTBIT_) */
	unsigned int maxbps;	/* supported max. bit per sample */
	struct hda_pcm_ops ops;
};

/* PCM types */
enum {
	HDA_PCM_TYPE_AUDIO,
	HDA_PCM_TYPE_SPDIF,
	HDA_PCM_TYPE_HDMI,
	HDA_PCM_TYPE_MODEM,
	HDA_PCM_NTYPES
};

/* for PCM creation */
struct hda_pcm {
	char *name;
	struct hda_pcm_stream stream[2];
	unsigned int pcm_type;	/* HDA_PCM_TYPE_XXX */
	int device;		/* device number to assign */
	struct snd_pcm *pcm;	/* assigned PCM instance */
};

/* codec information */
struct hda_codec {
	struct hda_bus *bus;
	unsigned int addr;	/* codec addr*/
	struct list_head list;	/* list point */

	hda_nid_t afg;	/* AFG node id */
	hda_nid_t mfg;	/* MFG node id */

	/* ids */
	u32 function_id;
	u32 vendor_id;
	u32 subsystem_id;
	u32 revision_id;

	/* detected preset */
	const struct hda_codec_preset *preset;
	struct module *owner;
	const char *vendor_name;	/* codec vendor name */
	const char *chip_name;		/* codec chip name */
	const char *modelname;	/* model name for preset */

	/* set by patch */
	struct hda_codec_ops patch_ops;

	/* PCM to create, set by patch_ops.build_pcms callback */
	unsigned int num_pcms;
	struct hda_pcm *pcm_info;

	/* codec specific info */
	void *spec;

	/* beep device */
	struct hda_beep *beep;
	unsigned int beep_mode;

	/* widget capabilities cache */
	unsigned int num_nodes;
	hda_nid_t start_nid;
	u32 *wcaps;

	struct snd_array mixers;	/* list of assigned mixer elements */
	struct snd_array nids;		/* list of mapped mixer elements */

	struct hda_cache_rec amp_cache;	/* cache for amp access */
	struct hda_cache_rec cmd_cache;	/* cache for other commands */

	struct mutex spdif_mutex;
	struct mutex control_mutex;
	unsigned int spdif_status;	/* IEC958 status bits */
	unsigned short spdif_ctls;	/* SPDIF control bits */
	unsigned int spdif_in_enable;	/* SPDIF input enable? */
	hda_nid_t *slave_dig_outs; /* optional digital out slave widgets */
	struct snd_array init_pins;	/* initial (BIOS) pin configurations */
	struct snd_array driver_pins;	/* pin configs set by codec parser */

#ifdef CONFIG_SND_HDA_HWDEP
	struct snd_hwdep *hwdep;	/* assigned hwdep device */
	struct snd_array init_verbs;	/* additional init verbs */
	struct snd_array hints;		/* additional hints */
	struct snd_array user_pins;	/* default pin configs to override */
#endif

	/* misc flags */
	unsigned int spdif_status_reset :1; /* needs to toggle SPDIF for each
					     * status change
					     * (e.g. Realtek codecs)
					     */
	unsigned int pin_amp_workaround:1; /* pin out-amp takes index
					    * (e.g. Conexant codecs)
					    */
	unsigned int pins_shutup:1;	/* pins are shut up */
	unsigned int no_trigger_sense:1; /* don't trigger at pin-sensing */
#ifdef CONFIG_SND_HDA_POWER_SAVE
	unsigned int power_on :1;	/* current (global) power-state */
	unsigned int power_transition :1; /* power-state in transition */
	int power_count;	/* current (global) power refcount */
	struct delayed_work power_work; /* delayed task for powerdown */
	unsigned long power_on_acct;
	unsigned long power_off_acct;
	unsigned long power_jiffies;
#endif

	/* codec-specific additional proc output */
	void (*proc_widget_hook)(struct snd_info_buffer *buffer,
				 struct hda_codec *codec, hda_nid_t nid);
};

/* direction */
enum {
	HDA_INPUT, HDA_OUTPUT
};


/*
 * constructors
 */
int snd_hda_bus_new(struct snd_card *card, const struct hda_bus_template *temp,
		    struct hda_bus **busp);
int snd_hda_codec_new(struct hda_bus *bus, unsigned int codec_addr,
		      struct hda_codec **codecp);
int snd_hda_codec_configure(struct hda_codec *codec);

/*
 * low level functions
 */
unsigned int snd_hda_codec_read(struct hda_codec *codec, hda_nid_t nid,
				int direct,
				unsigned int verb, unsigned int parm);
int snd_hda_codec_write(struct hda_codec *codec, hda_nid_t nid, int direct,
			unsigned int verb, unsigned int parm);
#define snd_hda_param_read(codec, nid, param) \
	snd_hda_codec_read(codec, nid, 0, AC_VERB_PARAMETERS, param)
int snd_hda_get_sub_nodes(struct hda_codec *codec, hda_nid_t nid,
			  hda_nid_t *start_id);
int snd_hda_get_connections(struct hda_codec *codec, hda_nid_t nid,
			    hda_nid_t *conn_list, int max_conns);

struct hda_verb {
	hda_nid_t nid;
	u32 verb;
	u32 param;
};

void snd_hda_sequence_write(struct hda_codec *codec,
			    const struct hda_verb *seq);

/* unsolicited event */
int snd_hda_queue_unsol_event(struct hda_bus *bus, u32 res, u32 res_ex);

/* cached write */
#ifdef SND_HDA_NEEDS_RESUME
int snd_hda_codec_write_cache(struct hda_codec *codec, hda_nid_t nid,
			      int direct, unsigned int verb, unsigned int parm);
void snd_hda_sequence_write_cache(struct hda_codec *codec,
				  const struct hda_verb *seq);
int snd_hda_codec_update_cache(struct hda_codec *codec, hda_nid_t nid,
			      int direct, unsigned int verb, unsigned int parm);
void snd_hda_codec_resume_cache(struct hda_codec *codec);
#else
#define snd_hda_codec_write_cache	snd_hda_codec_write
#define snd_hda_codec_update_cache	snd_hda_codec_write
#define snd_hda_sequence_write_cache	snd_hda_sequence_write
#endif

/* the struct for codec->pin_configs */
struct hda_pincfg {
	hda_nid_t nid;
	unsigned char ctrl;	/* current pin control value */
	unsigned char pad;	/* reserved */
	unsigned int cfg;	/* default configuration */
};

unsigned int snd_hda_codec_get_pincfg(struct hda_codec *codec, hda_nid_t nid);
int snd_hda_codec_set_pincfg(struct hda_codec *codec, hda_nid_t nid,
			     unsigned int cfg);
int snd_hda_add_pincfg(struct hda_codec *codec, struct snd_array *list,
		       hda_nid_t nid, unsigned int cfg); /* for hwdep */
void snd_hda_shutup_pins(struct hda_codec *codec);

/*
 * Mixer
 */
int snd_hda_build_controls(struct hda_bus *bus);
int snd_hda_codec_build_controls(struct hda_codec *codec);

/*
 * PCM
 */
int snd_hda_build_pcms(struct hda_bus *bus);
int snd_hda_codec_build_pcms(struct hda_codec *codec);
void snd_hda_codec_setup_stream(struct hda_codec *codec, hda_nid_t nid,
				u32 stream_tag,
				int channel_id, int format);
void snd_hda_codec_cleanup_stream(struct hda_codec *codec, hda_nid_t nid);
unsigned int snd_hda_calc_stream_format(unsigned int rate,
					unsigned int channels,
					unsigned int format,
					unsigned int maxbps);
int snd_hda_is_supported_format(struct hda_codec *codec, hda_nid_t nid,
				unsigned int format);

/*
 * Misc
 */
void snd_hda_get_codec_name(struct hda_codec *codec, char *name, int namelen);
void snd_hda_bus_reboot_notify(struct hda_bus *bus);

/*
 * power management
 */
#ifdef CONFIG_PM
int snd_hda_suspend(struct hda_bus *bus);
int snd_hda_resume(struct hda_bus *bus);
#endif

/*
 * get widget information
 */
const char *snd_hda_get_jack_connectivity(u32 cfg);
const char *snd_hda_get_jack_type(u32 cfg);
const char *snd_hda_get_jack_location(u32 cfg);

/*
 * power saving
 */
#ifdef CONFIG_SND_HDA_POWER_SAVE
void snd_hda_power_up(struct hda_codec *codec);
void snd_hda_power_down(struct hda_codec *codec);
#define snd_hda_codec_needs_resume(codec) codec->power_count
void snd_hda_update_power_acct(struct hda_codec *codec);
#else
static inline void snd_hda_power_up(struct hda_codec *codec) {}
static inline void snd_hda_power_down(struct hda_codec *codec) {}
#define snd_hda_codec_needs_resume(codec) 1
#endif

#ifdef CONFIG_SND_HDA_PATCH_LOADER
/*
 * patch firmware
 */
int snd_hda_load_patch(struct hda_bus *bus, const char *patch);
#endif

/*
 * Codec modularization
 */

/* Export symbols only for communication with codec drivers;
 * When built in kernel, all HD-audio drivers are supposed to be statically
 * linked to the kernel.  Thus, the symbols don't have to (or shouldn't) be
 * exported unless it's built as a module.
 */
#ifdef MODULE
#define EXPORT_SYMBOL_HDA(sym) EXPORT_SYMBOL_GPL(sym)
#else
#define EXPORT_SYMBOL_HDA(sym)
#endif

#endif /* __SOUND_HDA_CODEC_H */
