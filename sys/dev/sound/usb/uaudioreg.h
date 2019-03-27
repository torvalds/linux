/*	$NetBSD: uaudioreg.h,v 1.12 2004/11/05 19:08:29 kent Exp $	*/
/* $FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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

#ifndef _UAUDIOREG_H_
#define	_UAUDIOREG_H_

#define	UAUDIO_VERSION_10	0x0100
#define	UAUDIO_VERSION_20	0x0200
#define	UAUDIO_VERSION_30	0x0300

#define	UAUDIO_PROTOCOL_20	0x20

#define	UDESC_CS_UNDEFINED	0x20
#define	UDESC_CS_DEVICE		0x21
#define	UDESC_CS_CONFIG		0x22
#define	UDESC_CS_STRING		0x23
#define	UDESC_CS_INTERFACE	0x24
#define	UDESC_CS_ENDPOINT	0x25

#define	UDESCSUB_AC_HEADER	1
#define	UDESCSUB_AC_INPUT	2
#define	UDESCSUB_AC_OUTPUT	3
#define	UDESCSUB_AC_MIXER	4
#define	UDESCSUB_AC_SELECTOR	5
#define	UDESCSUB_AC_FEATURE	6
#define	UDESCSUB_AC_PROCESSING	7
#define	UDESCSUB_AC_EXTENSION	8
/* ==== USB audio v2.0 ==== */
#define	UDESCSUB_AC_EFFECT	7
#define	UDESCSUB_AC_PROCESSING_V2 8
#define	UDESCSUB_AC_EXTENSION_V2 9
#define	UDESCSUB_AC_CLOCK_SRC	10
#define	UDESCSUB_AC_CLOCK_SEL	11
#define	UDESCSUB_AC_CLOCK_MUL	12
#define	UDESCSUB_AC_SAMPLE_RT	13

/* These macros check if the endpoint descriptor has additional fields */
#define	UEP_MINSIZE	7
#define	UEP_HAS_REFRESH(ep)	((ep)->bLength >= 8)
#define	UEP_HAS_SYNCADDR(ep)	((ep)->bLength >= 9)

/* The first fields are identical to struct usb_endpoint_descriptor */
typedef struct {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bEndpointAddress;
	uByte	bmAttributes;
	uWord	wMaxPacketSize;
	uByte	bInterval;
	/*
	 * The following two entries are only used by the Audio Class.
	 * And according to the specs the Audio Class is the only one
	 * allowed to extend the endpoint descriptor.
	 * Who knows what goes on in the minds of the people in the USB
	 * standardization?  :-(
	 */
	uByte	bRefresh;
	uByte	bSynchAddress;
} __packed usb_endpoint_descriptor_audio_t;

struct usb_audio_control_descriptor {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uWord	bcdADC;
	uWord	wTotalLength;
	uByte	bInCollection;
	uByte	baInterfaceNr[1];
} __packed;

struct usb_audio_streaming_interface_descriptor {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bTerminalLink;
	uByte	bDelay;
	uWord	wFormatTag;
} __packed;

struct usb_audio_streaming_endpoint_descriptor {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bmAttributes;
#define	UA_SED_FREQ_CONTROL	0x01
#define	UA_SED_PITCH_CONTROL	0x02
#define	UA_SED_MAXPACKETSONLY	0x80
	uByte	bLockDelayUnits;
	uWord	wLockDelay;
} __packed;

struct usb_midi_streaming_endpoint_descriptor {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bNumEmbMIDIJack;
} __packed;

struct usb_audio_streaming_type1_descriptor {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bFormatType;
	uByte	bNrChannels;
	uByte	bSubFrameSize;
	uByte	bBitResolution;
	uByte	bSamFreqType;
#define	UA_SAMP_CONTNUOUS 0
	uByte	tSamFreq[0];
#define	UA_GETSAMP(p, n) ((uint32_t)((((p)->tSamFreq[((n)*3)+0]) | \
			  ((p)->tSamFreq[((n)*3)+1] << 8) | \
			  ((p)->tSamFreq[((n)*3)+2] << 16))))
#define	UA_SAMP_LO(p) UA_GETSAMP(p, 0)
#define	UA_SAMP_HI(p) UA_GETSAMP(p, 1)
} __packed;

struct usb_audio_cluster {
	uByte	bNrChannels;
	uWord	wChannelConfig;
#define	UA_CHANNEL_LEFT		0x0001
#define	UA_CHANNEL_RIGHT	0x0002
#define	UA_CHANNEL_CENTER	0x0004
#define	UA_CHANNEL_LFE		0x0008
#define	UA_CHANNEL_L_SURROUND	0x0010
#define	UA_CHANNEL_R_SURROUND	0x0020
#define	UA_CHANNEL_L_CENTER	0x0040
#define	UA_CHANNEL_R_CENTER	0x0080
#define	UA_CHANNEL_SURROUND	0x0100
#define	UA_CHANNEL_L_SIDE	0x0200
#define	UA_CHANNEL_R_SIDE	0x0400
#define	UA_CHANNEL_TOP		0x0800
	uByte	iChannelNames;
} __packed;

/* Shared by all units and terminals */
struct usb_audio_unit {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bUnitId;
};

/* UDESCSUB_AC_INPUT */
struct usb_audio_input_terminal {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bTerminalId;
	uWord	wTerminalType;
	uByte	bAssocTerminal;
	uByte	bNrChannels;
	uWord	wChannelConfig;
	uByte	iChannelNames;
/*	uByte		iTerminal; */
} __packed;

/* UDESCSUB_AC_OUTPUT */
struct usb_audio_output_terminal {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bTerminalId;
	uWord	wTerminalType;
	uByte	bAssocTerminal;
	uByte	bSourceId;
	uByte	iTerminal;
} __packed;

/* UDESCSUB_AC_MIXER */
struct usb_audio_mixer_unit_0 {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bUnitId;
	uByte	bNrInPins;
	uByte	baSourceId[0];		/* [bNrInPins] */
	/* struct usb_audio_mixer_unit_1 */
} __packed;
struct usb_audio_mixer_unit_1 {
	uByte	bNrChannels;
	uWord	wChannelConfig;
	uByte	iChannelNames;
	uByte	bmControls[0];		/* [see source code] */
	/* uByte		iMixer; */
} __packed;

/* UDESCSUB_AC_SELECTOR */
struct usb_audio_selector_unit {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bUnitId;
	uByte	bNrInPins;
	uByte	baSourceId[0];		/* [bNrInPins] */
	/* uByte	iSelector; */
} __packed;

/* UDESCSUB_AC_FEATURE */
struct usb_audio_feature_unit {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bUnitId;
	uByte	bSourceId;
	uByte	bControlSize;
	uByte	bmaControls[0];		/* [bControlSize * x] */
	/* uByte	iFeature; */
} __packed;

/* UDESCSUB_AC_PROCESSING */
struct usb_audio_processing_unit_0 {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bUnitId;
	uWord	wProcessType;
	uByte	bNrInPins;
	uByte	baSourceId[0];		/* [bNrInPins] */
	/* struct usb_audio_processing_unit_1 */
} __packed;
struct usb_audio_processing_unit_1 {
	uByte	bNrChannels;
	uWord	wChannelConfig;
	uByte	iChannelNames;
	uByte	bControlSize;
	uByte	bmControls[0];		/* [bControlSize] */
#define	UA_PROC_ENABLE_MASK 1
} __packed;

struct usb_audio_processing_unit_updown {
	uByte	iProcessing;
	uByte	bNrModes;
	uWord	waModes[0];		/* [bNrModes] */
} __packed;

/* UDESCSUB_AC_EXTENSION */
struct usb_audio_extension_unit_0 {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bUnitId;
	uWord	wExtensionCode;
	uByte	bNrInPins;
	uByte	baSourceId[0];		/* [bNrInPins] */
	/* struct usb_audio_extension_unit_1 */
} __packed;

struct usb_audio_extension_unit_1 {
	uByte	bNrChannels;
	uWord	wChannelConfig;
	uByte	iChannelNames;
	uByte	bControlSize;
	uByte	bmControls[0];		/* [bControlSize] */
#define	UA_EXT_ENABLE_MASK 1
#define	UA_EXT_ENABLE 1
	/* uByte		iExtension; */
} __packed;

/* USB terminal types */
#define	UAT_UNDEFINED		0x0100
#define	UAT_STREAM		0x0101
#define	UAT_VENDOR		0x01ff
/* input terminal types */
#define	UATI_UNDEFINED		0x0200
#define	UATI_MICROPHONE		0x0201
#define	UATI_DESKMICROPHONE	0x0202
#define	UATI_PERSONALMICROPHONE	0x0203
#define	UATI_OMNIMICROPHONE	0x0204
#define	UATI_MICROPHONEARRAY	0x0205
#define	UATI_PROCMICROPHONEARR	0x0206
/* output terminal types */
#define	UATO_UNDEFINED		0x0300
#define	UATO_SPEAKER		0x0301
#define	UATO_HEADPHONES		0x0302
#define	UATO_DISPLAYAUDIO	0x0303
#define	UATO_DESKTOPSPEAKER	0x0304
#define	UATO_ROOMSPEAKER	0x0305
#define	UATO_COMMSPEAKER	0x0306
#define	UATO_SUBWOOFER		0x0307
/* bidir terminal types */
#define	UATB_UNDEFINED		0x0400
#define	UATB_HANDSET		0x0401
#define	UATB_HEADSET		0x0402
#define	UATB_SPEAKERPHONE	0x0403
#define	UATB_SPEAKERPHONEESUP	0x0404
#define	UATB_SPEAKERPHONEECANC	0x0405
/* telephony terminal types */
#define	UATT_UNDEFINED		0x0500
#define	UATT_PHONELINE		0x0501
#define	UATT_TELEPHONE		0x0502
#define	UATT_DOWNLINEPHONE	0x0503
/* external terminal types */
#define	UATE_UNDEFINED		0x0600
#define	UATE_ANALOGCONN		0x0601
#define	UATE_DIGITALAUIFC	0x0602
#define	UATE_LINECONN		0x0603
#define	UATE_LEGACYCONN		0x0604
#define	UATE_SPDIF		0x0605
#define	UATE_1394DA		0x0606
#define	UATE_1394DV		0x0607
/* embedded function terminal types */
#define	UATF_UNDEFINED		0x0700
#define	UATF_CALIBNOISE		0x0701
#define	UATF_EQUNOISE		0x0702
#define	UATF_CDPLAYER		0x0703
#define	UATF_DAT		0x0704
#define	UATF_DCC		0x0705
#define	UATF_MINIDISK		0x0706
#define	UATF_ANALOGTAPE		0x0707
#define	UATF_PHONOGRAPH		0x0708
#define	UATF_VCRAUDIO		0x0709
#define	UATF_VIDEODISCAUDIO	0x070a
#define	UATF_DVDAUDIO		0x070b
#define	UATF_TVTUNERAUDIO	0x070c
#define	UATF_SATELLITE		0x070d
#define	UATF_CABLETUNER		0x070e
#define	UATF_DSS		0x070f
#define	UATF_RADIORECV		0x0710
#define	UATF_RADIOXMIT		0x0711
#define	UATF_MULTITRACK		0x0712
#define	UATF_SYNTHESIZER	0x0713


#define	SET_CUR 0x01
#define	GET_CUR 0x81
#define	SET_MIN 0x02
#define	GET_MIN 0x82
#define	SET_MAX 0x03
#define	GET_MAX 0x83
#define	SET_RES 0x04
#define	GET_RES 0x84
#define	SET_MEM 0x05
#define	GET_MEM 0x85
#define	GET_STAT 0xff

#define	MUTE_CONTROL	0x01
#define	VOLUME_CONTROL	0x02
#define	BASS_CONTROL	0x03
#define	MID_CONTROL	0x04
#define	TREBLE_CONTROL	0x05
#define	GRAPHIC_EQUALIZER_CONTROL	0x06
#define	AGC_CONTROL	0x07
#define	DELAY_CONTROL	0x08
#define	BASS_BOOST_CONTROL 0x09
#define	LOUDNESS_CONTROL 0x0a
/* ==== USB audio v2.0 ==== */
#define	INPUT_GAIN_CONTROL 0x0b
#define	INPUT_GAIN_PAD_CONTROL 0x0c
#define	PHASE_INVERTER_CONTROL 0x0d
#define	UNDERFLOW_CONTROL 0x0e
#define	OVERFLOW_CONTROL 0x0f
#define	LATENCY_CONTROL 0x10

#define	FU_MASK(u) (1 << ((u)-1))

#define	MASTER_CHAN	0

#define	MS_GENERAL	1
#define	AS_GENERAL	1
#define	FORMAT_TYPE	2
#define	FORMAT_SPECIFIC 3
/* ==== USB audio v2.0 ==== */
#define	FORMAT_ENCODER	3
#define	FORMAT_DECODER	4

#define	UA_FMT_PCM	1
#define	UA_FMT_PCM8	2
#define	UA_FMT_IEEE_FLOAT 3
#define	UA_FMT_ALAW	4
#define	UA_FMT_MULAW	5
#define	UA_FMT_MPEG	0x1001
#define	UA_FMT_AC3	0x1002

#define	SAMPLING_FREQ_CONTROL	0x01
#define	PITCH_CONTROL		0x02

#define	FORMAT_TYPE_UNDEFINED 0
#define	FORMAT_TYPE_I 1
#define	FORMAT_TYPE_II 2
#define	FORMAT_TYPE_III 3

#define	UA_PROC_MASK(n) (1<< ((n)-1))
#define	PROCESS_UNDEFINED		0
#define	XX_ENABLE_CONTROL			1
#define	UPDOWNMIX_PROCESS		1
#define	UD_ENABLE_CONTROL			1
#define	UD_MODE_SELECT_CONTROL			2
#define	DOLBY_PROLOGIC_PROCESS		2
#define	DP_ENABLE_CONTROL			1
#define	DP_MODE_SELECT_CONTROL			2
#define	P3D_STEREO_EXTENDER_PROCESS	3
#define	P3D_ENABLE_CONTROL			1
#define	P3D_SPACIOUSNESS_CONTROL		2
#define	REVERBATION_PROCESS		4
#define	RV_ENABLE_CONTROL			1
#define	RV_LEVEL_CONTROL			2
#define	RV_TIME_CONTROL			3
#define	RV_FEEDBACK_CONTROL			4
#define	CHORUS_PROCESS			5
#define	CH_ENABLE_CONTROL			1
#define	CH_LEVEL_CONTROL			2
#define	CH_RATE_CONTROL			3
#define	CH_DEPTH_CONTROL			4
#define	DYN_RANGE_COMP_PROCESS		6
#define	DR_ENABLE_CONTROL			1
#define	DR_COMPRESSION_RATE_CONTROL		2
#define	DR_MAXAMPL_CONTROL			3
#define	DR_THRESHOLD_CONTROL			4
#define	DR_ATTACK_TIME_CONTROL			5
#define	DR_RELEASE_TIME_CONTROL		6

/*------------------------------------------------------------------------*
 * USB audio v2.0 definitions
 *------------------------------------------------------------------------*/

struct usb_audio20_streaming_interface_descriptor {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bTerminalLink;
	uByte	bmControls;
	uByte	bFormatType;
	uDWord	bmFormats;
	uByte	bNrChannels;
	uDWord	bmChannelConfig;
	uByte	iChannelNames;
} __packed;

struct usb_audio20_encoder_descriptor {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bEncoderID;
	uByte	bEncoder;
	uDWord	bmControls;
	uByte	iParam1;
	uByte	iParam2;
	uByte	iParam3;
	uByte	iParam4;
	uByte	iParam5;
	uByte	iParam6;
	uByte	iParam7;
	uByte	iParam8;
	uByte	iEncoder;
} __packed;

struct usb_audio20_streaming_endpoint_descriptor {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bmAttributes;
#define	UA20_MPS_ONLY		0x80
	uByte	bmControls;
#define	UA20_PITCH_CONTROL_MASK	0x03
#define	UA20_DATA_OVERRUN_MASK	0x0C
#define	UA20_DATA_UNDERRUN_MASK	0x30
	uByte	bLockDelayUnits;
	uWord	wLockDelay;
} __packed;

struct usb_audio20_feedback_endpoint_descriptor {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bEndpointAddress;
	uByte	bmAttributes;
	uWord	wMaxPacketSize;
	uByte	bInterval;
} __packed;

#define	UA20_CS_CUR	0x01
#define	UA20_CS_RANGE	0x02
#define	UA20_CS_MEM	0x03

struct usb_audio20_cur1_parameter {
	uByte	bCur;
} __packed;

struct usb_audio20_ctl1_range_sub {
	uByte	bMIN;
	uByte	bMAX;
	uByte	bRES;
} __packed;

struct usb_audio20_ctl1_range {
	uWord	wNumSubRanges;
	struct usb_audio20_ctl1_range_sub sub[1];
} __packed;

struct usb_audio20_cur2_parameter {
	uWord	wCur;
} __packed;

struct usb_audio20_ctl2_range_sub {
	uWord	wMIN;
	uWord	wMAX;
	uWord	wRES;
} __packed;

struct usb_audio20_ctl2_range {
	uWord	wNumSubRanges;
	struct usb_audio20_ctl2_range_sub sub[1];
} __packed;

struct usb_audio20_cur4_parameter {
	uDWord	dCur;
} __packed;

struct usb_audio20_ctl4_range_sub {
	uDWord	dMIN;
	uDWord	dMAX;
	uDWord	dRES;
} __packed;

struct usb_audio20_ctl4_range {
	uWord	wNumSubRanges;
	struct usb_audio20_ctl4_range_sub sub[1];
} __packed;

struct usb_audio20_cc_cluster_descriptor {
	uByte	bNrChannels;
	uDWord	bmChannelConfig;
	uByte	iChannelNames;
} __packed;

struct usb_audio20_streaming_type1_descriptor {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bFormatType;
	uByte	bSubslotSize;
	uByte	bBitResolution;
} __packed;

#define	UA20_EERROR_NONE	0
#define	UA20_EERROR_MEMORY	1
#define	UA20_EERROR_BANDWIDTH	2
#define	UA20_EERROR_CPU		3
#define	UA20_EERROR_FORMATFR_ER	4
#define	UA20_EERROR_FORMATFR_SM	5
#define	UA20_EERROR_FORMATFR_LG	6
#define	UA20_EERROR_DATAFORMAT	7
#define	UA20_EERROR_NUMCHANNELS	8
#define	UA20_EERROR_SAMPLERATE	9
#define	UA20_EERROR_BITRATE	10
#define	UA20_EERROR_PARAM	11
#define	UA20_EERROR_NREADY	12
#define	UA20_EERROR_BUSY	13

struct usb_audio20_cc_alt_setting {
	uByte	bControlSize;
	uDWord	bmValidAltSettings;
} __packed;

struct usb_audio20_interrupt_message {
	uByte	bInfo;
	uByte	bAttribute;
	uDWord	wValue;
	uDWord	wIndex;
} __packed;

/* UDESCSUB_AC_CLOCK_SRC */
struct usb_audio20_clock_source_unit {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bClockId;
	uByte	bmAttributes;
	uByte	bmControls;
	uByte	bAssocTerminal;
	uByte	iClockSource;
} __packed;

/* UDESCSUB_AC_CLOCK_SEL */
struct usb_audio20_clock_selector_unit_0 {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bClockId;
	uByte	bNrInPins;
	uByte	baCSourceId[0];
} __packed;

struct usb_audio20_clock_selector_unit_1 {
	uByte	bmControls;
	uByte	iClockSelector;
} __packed;

/* UDESCSUB_AC_CLOCK_MUL */
struct usb_audio20_clock_multiplier_unit {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bClockId;
	uByte	bCSourceId;
	uByte	bmControls;
	uByte	iClockMultiplier;
} __packed;

/* UDESCSUB_AC_INPUT */
struct usb_audio20_input_terminal {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bTerminalId;
	uWord	wTerminalType;
	uByte	bAssocTerminal;
	uByte	bCSourceId;
	uByte	bNrChannels;
	uDWord	bmChannelConfig;
	uByte	iCHannelNames;
	uWord	bmControls;
	uByte	iTerminal;
} __packed;

/* UDESCSUB_AC_OUTPUT */
struct usb_audio20_output_terminal {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bTerminalId;
	uWord	wTerminalType;
	uByte	bAssocTerminal;
	uByte	bSourceId;
	uByte	bCSourceId;
	uWord	bmControls;
	uByte	iTerminal;
} __packed;

/* UDESCSUB_AC_MIXER */
struct usb_audio20_mixer_unit_0 {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bUnitId;
	uByte	bNrInPins;
	uByte	baSourceId[0];
	/* struct usb_audio20_mixer_unit_1 */
} __packed;

struct usb_audio20_mixer_unit_1 {
	uByte	bNrChannels;
	uDWord	bmChannelConfig;
	uByte	iChannelNames;
	uByte	bmControls[0];
	/* uByte	iMixer; */
} __packed;

/* UDESCSUB_AC_SELECTOR */
struct usb_audio20_selector_unit {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bUnitId;
	uByte	bNrInPins;
	uByte	baSourceId[0];
	/* uByte	iSelector; */
} __packed;

/* UDESCSUB_AC_FEATURE */
struct usb_audio20_feature_unit {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bUnitId;
	uByte	bSourceId;
	uDWord	bmaControls[0];
	/* uByte	iFeature; */
} __packed;

/* UDESCSUB_AC_SAMPLE_RT */
struct usb_audio20_sample_rate_unit {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bUnitId;
	uByte	bSourceId;
	uByte	bSourceInId;
	uByte	bSourceOutId;
	uByte	iSrc;
} __packed;

/* UDESCSUB_AC_EFFECT */
struct usb_audio20_effect_unit {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bUnitId;
	uWord	wEffectType;
	uByte	bSourceId;
	uDWord	bmaControls[0];
	uByte	iEffects;
} __packed;

/* UDESCSUB_AC_PROCESSING_V2 */
struct usb_audio20_processing_unit_0 {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bUnitId;
	uWord	wProcessType;
	uByte	bNrInPins;
	uByte	baSourceId[0];		/* [bNrInPins] */
} __packed;

struct usb_audio20_processing_unit_1 {
	uByte	bNrChannels;
	uDWord	bmChannelConfig;
	uByte	iChannelNames;
	uWord	bmControls;
	uByte	iProcessing;
} __packed;

/* UDESCSUB_AC_EXTENSION_V2 */
struct usb_audio20_extension_unit_0 {
	uByte	bLength;
	uByte	bDescriptorType;
	uByte	bDescriptorSubtype;
	uByte	bUnitId;
	uWord	wExtensionCode;
	uByte	bNrInPins;
	uByte	baSourceId[0];
} __packed;

struct usb_audio20_extension_unit_1 {
	uByte	bNrChannels;
	uDWord	bmChannelConfig;
	uByte	iChannelNames;
	uByte	bmControls;
	uByte	iExtension;
} __packed;

struct usb_audio20_cluster {
	uByte	bNrChannels;
	uDWord	bmChannelConfig;
	uByte	iChannelNames;
} __packed;

#define	UA20_TF_UNDEFINED		0x00
#define	UA20_TF_DESKTOP_SPEAKER		0x01
#define	UA20_TF_HOME_THEATER		0x02
#define	UA20_TF_MICROPHONE		0x03
#define	UA20_TF_HEADSET			0x04
#define	UA20_TF_TELEPHONE		0x05
#define	UA20_TF_CONVERTER		0x06
#define	UA20_TF_SOUND_RECORDER		0x07
#define	UA20_TF_IO_BOX			0x08
#define	UA20_TF_MUSICAL_INSTRUMENT	0x09
#define	UA20_TF_PRO_AUDIO		0x0A
#define	UA20_TF_AV			0x0B
#define	UA20_TF_CONTROL_PANEL		0x0C
#define	UA20_TF_OTHER			0xFF

#define	UA20_CS_SAM_FREQ_CONTROL	0x01
#define	UA20_CS_CLOCK_VALID_CONTROL 	0x02

#define	UA20_TE_COPY_PROTECT_CONTROL	0x01
#define	UA20_TE_CONNECTOR_CONTROL	0x02
#define	UA20_TE_OVERLOAD_CONTROL	0x03
#define	UA20_TE_CLUSTER_CONTROL		0x04
#define	UA20_TE_UNDERFLOW_CONTROL	0x05
#define	UA20_TE_OVERFLOW_CONTROL	0x06
#define	UA20_TE_LATENCY_CONTROL		0x07

#define	UA20_MU_MIXER_CONTROL		0x01
#define	UA20_MU_CLUSTER_CONTROL		0x02
#define	UA20_MU_UNDERFLOW_CONTROL	0x03
#define	UA20_MU_OVERFLOW_CONTROL	0x04
#define	UA20_MU_LATENCY_CONTROL		0x05

#define	UA20_FMT_PCM	(1U << 0)
#define	UA20_FMT_PCM8	(1U << 1)
#define	UA20_FMT_FLOAT	(1U << 2)
#define	UA20_FMT_ALAW	(1U << 3)
#define	UA20_FMT_MULAW	(1U << 4)
#define	UA20_FMT_RAW	(1U << 31)

#endif					/* _UAUDIOREG_H_ */
