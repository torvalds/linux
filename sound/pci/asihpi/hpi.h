/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************

    AudioScience HPI driver
    Copyright (C) 1997-2011  AudioScience Inc. <support@audioscience.com>


*/
/** \file hpi.h

 AudioScience Hardware Programming Interface (HPI)
 public API definition.

 The HPI is a low-level hardware abstraction layer to all
 AudioScience digital audio adapters

(C) Copyright AudioScience Inc. 1998-2010
*/

#ifndef _HPI_H_
#define _HPI_H_

#include <linux/types.h>
#define HPI_BUILD_KERNEL_MODE

/******************************************************************************/
/********       HPI API DEFINITIONS                                       *****/
/******************************************************************************/

/*******************************************/
/**  Audio format types
\ingroup stream
*/
enum HPI_FORMATS {
/** Used internally on adapter. */
	HPI_FORMAT_MIXER_NATIVE = 0,
/** 8-bit unsigned PCM. Windows equivalent is WAVE_FORMAT_PCM. */
	HPI_FORMAT_PCM8_UNSIGNED = 1,
/** 16-bit signed PCM. Windows equivalent is WAVE_FORMAT_PCM. */
	HPI_FORMAT_PCM16_SIGNED = 2,
/** MPEG-1 Layer-1. */
	HPI_FORMAT_MPEG_L1 = 3,
/** MPEG-1 Layer-2.

Windows equivalent is WAVE_FORMAT_MPEG.

The following table shows what combinations of mode and bitrate are possible:

<table border=1 cellspacing=0 cellpadding=5>
<tr>
<td><p><b>Bitrate (kbs)</b></p>
<td><p><b>Mono</b></p>
<td><p><b>Stereo,<br>Joint Stereo or<br>Dual Channel</b></p>

<tr><td>32<td>X<td>_
<tr><td>40<td>_<td>_
<tr><td>48<td>X<td>_
<tr><td>56<td>X<td>_
<tr><td>64<td>X<td>X
<tr><td>80<td>X<td>_
<tr><td>96<td>X<td>X
<tr><td>112<td>X<td>X
<tr><td>128<td>X<td>X
<tr><td>160<td>X<td>X
<tr><td>192<td>X<td>X
<tr><td>224<td>_<td>X
<tr><td>256<td>-<td>X
<tr><td>320<td>-<td>X
<tr><td>384<td>_<td>X
</table>
*/
	HPI_FORMAT_MPEG_L2 = 4,
/** MPEG-1 Layer-3.
Windows equivalent is WAVE_FORMAT_MPEG.

The following table shows what combinations of mode and bitrate are possible:

<table border=1 cellspacing=0 cellpadding=5>
<tr>
<td><p><b>Bitrate (kbs)</b></p>
<td><p><b>Mono<br>Stereo @ 8,<br>11.025 and<br>12kHz*</b></p>
<td><p><b>Mono<br>Stereo @ 16,<br>22.050 and<br>24kHz*</b></p>
<td><p><b>Mono<br>Stereo @ 32,<br>44.1 and<br>48kHz</b></p>

<tr><td>16<td>X<td>X<td>_
<tr><td>24<td>X<td>X<td>_
<tr><td>32<td>X<td>X<td>X
<tr><td>40<td>X<td>X<td>X
<tr><td>48<td>X<td>X<td>X
<tr><td>56<td>X<td>X<td>X
<tr><td>64<td>X<td>X<td>X
<tr><td>80<td>_<td>X<td>X
<tr><td>96<td>_<td>X<td>X
<tr><td>112<td>_<td>X<td>X
<tr><td>128<td>_<td>X<td>X
<tr><td>144<td>_<td>X<td>_
<tr><td>160<td>_<td>X<td>X
<tr><td>192<td>_<td>_<td>X
<tr><td>224<td>_<td>_<td>X
<tr><td>256<td>-<td>_<td>X
<tr><td>320<td>-<td>_<td>X
</table>
\b * Available on the ASI6000 series only
*/
	HPI_FORMAT_MPEG_L3 = 5,
/** Dolby AC-2. */
	HPI_FORMAT_DOLBY_AC2 = 6,
/** Dolbt AC-3. */
	HPI_FORMAT_DOLBY_AC3 = 7,
/** 16-bit PCM big-endian. */
	HPI_FORMAT_PCM16_BIGENDIAN = 8,
/** TAGIT-1 algorithm - hits. */
	HPI_FORMAT_AA_TAGIT1_HITS = 9,
/** TAGIT-1 algorithm - inserts. */
	HPI_FORMAT_AA_TAGIT1_INSERTS = 10,
/** 32-bit signed PCM. Windows equivalent is WAVE_FORMAT_PCM.
Each sample is a 32bit word. The most significant 24 bits contain a 24-bit
sample and the least significant 8 bits are set to 0.
*/
	HPI_FORMAT_PCM32_SIGNED = 11,
/** Raw bitstream - unknown format. */
	HPI_FORMAT_RAW_BITSTREAM = 12,
/** TAGIT-1 algorithm hits - extended. */
	HPI_FORMAT_AA_TAGIT1_HITS_EX1 = 13,
/** 32-bit PCM as an IEEE float. Windows equivalent is WAVE_FORMAT_IEEE_FLOAT.
Each sample is a 32bit word in IEEE754 floating point format.
The range is +1.0 to -1.0, which corresponds to digital fullscale.
*/
	HPI_FORMAT_PCM32_FLOAT = 14,
/** 24-bit PCM signed. Windows equivalent is WAVE_FORMAT_PCM. */
	HPI_FORMAT_PCM24_SIGNED = 15,
/** OEM format 1 - private. */
	HPI_FORMAT_OEM1 = 16,
/** OEM format 2 - private. */
	HPI_FORMAT_OEM2 = 17,
/** Undefined format. */
	HPI_FORMAT_UNDEFINED = 0xffff
};

/*******************************************/
/** Stream States
\ingroup stream
*/
enum HPI_STREAM_STATES {
	/** State stopped - stream is stopped. */
	HPI_STATE_STOPPED = 1,
	/** State playing - stream is playing audio. */
	HPI_STATE_PLAYING = 2,
	/** State recording - stream is recording. */
	HPI_STATE_RECORDING = 3,
	/** State drained - playing stream ran out of data to play. */
	HPI_STATE_DRAINED = 4,
	/** State generate sine - to be implemented. */
	HPI_STATE_SINEGEN = 5,
	/** State wait - used for inter-card sync to mean waiting for all
		cards to be ready. */
	HPI_STATE_WAIT = 6
};
/*******************************************/
/** Source node types
\ingroup mixer
*/
enum HPI_SOURCENODES {
	/** This define can be used instead of 0 to indicate
	that there is no valid source node. A control that
	exists on a destination node can be searched for using a source
	node value of either 0, or HPI_SOURCENODE_NONE */
	HPI_SOURCENODE_NONE = 100,
	/** Out Stream (Play) node. */
	HPI_SOURCENODE_OSTREAM = 101,
	/** Line in node - could be analog, AES/EBU or network. */
	HPI_SOURCENODE_LINEIN = 102,
	HPI_SOURCENODE_AESEBU_IN = 103,	     /**< AES/EBU input node. */
	HPI_SOURCENODE_TUNER = 104,	     /**< tuner node. */
	HPI_SOURCENODE_RF = 105,	     /**< RF input node. */
	HPI_SOURCENODE_CLOCK_SOURCE = 106,   /**< clock source node. */
	HPI_SOURCENODE_RAW_BITSTREAM = 107,  /**< raw bitstream node. */
	HPI_SOURCENODE_MICROPHONE = 108,     /**< microphone node. */
	/** Cobranet input node -
	    Audio samples come from the Cobranet network and into the device. */
	HPI_SOURCENODE_COBRANET = 109,
	HPI_SOURCENODE_ANALOG = 110,	     /**< analog input node. */
	HPI_SOURCENODE_ADAPTER = 111,	     /**< adapter node. */
	/** RTP stream input node - This node is a destination for
	    packets of RTP audio samples from other devices. */
	HPI_SOURCENODE_RTP_DESTINATION = 112,
	HPI_SOURCENODE_INTERNAL = 113,	     /**< node internal to the device. */
	HPI_SOURCENODE_AVB = 114,	     /**< AVB input stream */
	HPI_SOURCENODE_BLULINK = 115,	     /**< BLU-link input channel */
	/* !!!Update this  AND hpidebug.h if you add a new sourcenode type!!! */
	HPI_SOURCENODE_LAST_INDEX = 115	     /**< largest ID */
		/* AX6 max sourcenode types = 15 */
};

/*******************************************/
/** Destination node types
\ingroup mixer
*/
enum HPI_DESTNODES {
	/** This define can be used instead of 0 to indicate
	that there is no valid destination node. A control that
	exists on a source node can be searched for using a destination
	node value of either 0, or HPI_DESTNODE_NONE */
	HPI_DESTNODE_NONE = 200,
	/** In Stream (Record) node. */
	HPI_DESTNODE_ISTREAM = 201,
	HPI_DESTNODE_LINEOUT = 202,	     /**< line out node. */
	HPI_DESTNODE_AESEBU_OUT = 203,	     /**< AES/EBU output node. */
	HPI_DESTNODE_RF = 204,		     /**< RF output node. */
	HPI_DESTNODE_SPEAKER = 205,	     /**< speaker output node. */
	/** Cobranet output node -
	    Audio samples from the device are sent out on the Cobranet network.*/
	HPI_DESTNODE_COBRANET = 206,
	HPI_DESTNODE_ANALOG = 207,	     /**< analog output node. */
	/** RTP stream output node - This node is a source for
	    packets of RTP audio samples that are sent to other devices. */
	HPI_DESTNODE_RTP_SOURCE = 208,
	HPI_DESTNODE_AVB = 209,		     /**< AVB output stream */
	HPI_DESTNODE_INTERNAL = 210,	     /**< node internal to the device. */
	HPI_DESTNODE_BLULINK = 211,	     /**< BLU-link output channel. */
	/* !!!Update this AND hpidebug.h if you add a new destnode type!!! */
	HPI_DESTNODE_LAST_INDEX = 211	     /**< largest ID */
		/* AX6 max destnode types = 15 */
};

/*******************************************/
/** Mixer control types
\ingroup mixer
*/
enum HPI_CONTROLS {
	HPI_CONTROL_GENERIC = 0,	/**< generic control. */
	HPI_CONTROL_CONNECTION = 1, /**< A connection between nodes. */
	HPI_CONTROL_VOLUME = 2,	      /**< volume control - works in dB_fs. */
	HPI_CONTROL_METER = 3,	/**< peak meter control. */
	HPI_CONTROL_MUTE = 4,	/*mute control - not used at present. */
	HPI_CONTROL_MULTIPLEXER = 5,	/**< multiplexer control. */

	HPI_CONTROL_AESEBU_TRANSMITTER = 6, /**< AES/EBU transmitter control */
	HPI_CONTROL_AESEBUTX = 6,	/* HPI_CONTROL_AESEBU_TRANSMITTER */

	HPI_CONTROL_AESEBU_RECEIVER = 7, /**< AES/EBU receiver control. */
	HPI_CONTROL_AESEBURX = 7,	/* HPI_CONTROL_AESEBU_RECEIVER */

	HPI_CONTROL_LEVEL = 8, /**< level/trim control - works in d_bu. */
	HPI_CONTROL_TUNER = 9,	/**< tuner control. */
/*      HPI_CONTROL_ONOFFSWITCH =       10 */
	HPI_CONTROL_VOX = 11,	/**< vox control. */
/*      HPI_CONTROL_AES18_TRANSMITTER = 12 */
/*      HPI_CONTROL_AES18_RECEIVER = 13 */
/*      HPI_CONTROL_AES18_BLOCKGENERATOR  = 14 */
	HPI_CONTROL_CHANNEL_MODE = 15,	/**< channel mode control. */

	HPI_CONTROL_BITSTREAM = 16,	/**< bitstream control. */
	HPI_CONTROL_SAMPLECLOCK = 17,	/**< sample clock control. */
	HPI_CONTROL_MICROPHONE = 18,	/**< microphone control. */
	HPI_CONTROL_PARAMETRIC_EQ = 19,	/**< parametric EQ control. */
	HPI_CONTROL_EQUALIZER = 19,	/*HPI_CONTROL_PARAMETRIC_EQ */

	HPI_CONTROL_COMPANDER = 20,	/**< compander control. */
	HPI_CONTROL_COBRANET = 21,	/**< cobranet control. */
	HPI_CONTROL_TONEDETECTOR = 22,	/**< tone detector control. */
	HPI_CONTROL_SILENCEDETECTOR = 23,	/**< silence detector control. */
	HPI_CONTROL_PAD = 24,	/**< tuner PAD control. */
	HPI_CONTROL_SRC = 25,	/**< samplerate converter control. */
	HPI_CONTROL_UNIVERSAL = 26,	/**< universal control. */

/*  !!! Update this AND hpidebug.h if you add a new control type!!!*/
	HPI_CONTROL_LAST_INDEX = 26 /**<highest control type ID */
/* WARNING types 256 or greater impact bit packing in all AX6 DSP code */
};

/*******************************************/
/** Adapter properties
These are used in HPI_AdapterSetProperty() and HPI_AdapterGetProperty()
\ingroup adapter
*/
enum HPI_ADAPTER_PROPERTIES {
/** \internal Used in dwProperty field of HPI_AdapterSetProperty() and
HPI_AdapterGetProperty(). This errata applies to all ASI6000 cards with both
analog and digital outputs. The CS4224 A/D+D/A has a one sample delay between
left and right channels on both its input (ADC) and output (DAC).
More details are available in Cirrus Logic errata ER284B2.
PDF available from www.cirrus.com, released by Cirrus in 2001.
*/
	HPI_ADAPTER_PROPERTY_ERRATA_1 = 1,

/** Adapter grouping property
Indicates whether the adapter supports the grouping API (for ASIO and SSX2)
*/
	HPI_ADAPTER_PROPERTY_GROUPING = 2,

/** Driver SSX2 property
Tells the kernel driver to turn on SSX2 stream mapping.
This feature is not used by the DSP. In fact the call is completely processed
by the driver and is not passed on to the DSP at all.
*/
	HPI_ADAPTER_PROPERTY_ENABLE_SSX2 = 3,

/** Adapter SSX2 property
Indicates the state of the adapter's SSX2 setting. This setting is stored in
non-volatile memory on the adapter. A typical call sequence would be to use
HPI_ADAPTER_PROPERTY_SSX2_SETTING to set SSX2 on the adapter and then to reload
the driver. The driver would query HPI_ADAPTER_PROPERTY_SSX2_SETTING during
startup and if SSX2 is set, it would then call HPI_ADAPTER_PROPERTY_ENABLE_SSX2
to enable SSX2 stream mapping within the kernel level of the driver.
*/
	HPI_ADAPTER_PROPERTY_SSX2_SETTING = 4,

/** Enables/disables PCI(e) IRQ.
A setting of 0 indicates that no interrupts are being generated. A DSP boot
this property is set to 0. Setting to a non-zero value specifies the number
of frames of audio that should be processed between interrupts. This property
should be set to multiple of the mixer interval as read back from the
HPI_ADAPTER_PROPERTY_INTERVAL property.
*/
	HPI_ADAPTER_PROPERTY_IRQ_RATE = 5,

/** Base number for readonly properties */
	HPI_ADAPTER_PROPERTY_READONLYBASE = 256,

/** Readonly adapter latency property.
This property returns in the input and output latency in samples.
Property 1 is the estimated input latency
in samples, while Property 2 is that output latency in  samples.
*/
	HPI_ADAPTER_PROPERTY_LATENCY = 256,

/** Readonly adapter granularity property.
The granulariy is the smallest size chunk of stereo samples that is processed by
the adapter.
This property returns the record granularity in samples in Property 1.
Property 2 returns the play granularity.
*/
	HPI_ADAPTER_PROPERTY_GRANULARITY = 257,

/** Readonly adapter number of current channels property.
Property 1 is the number of record channels per record device.
Property 2 is the number of play channels per playback device.*/
	HPI_ADAPTER_PROPERTY_CURCHANNELS = 258,

/** Readonly adapter software version.
The SOFTWARE_VERSION property returns the version of the software running
on the adapter as Major.Minor.Release.
Property 1 contains Major in bits 15..8 and Minor in bits 7..0.
Property 2 contains Release in bits 7..0. */
	HPI_ADAPTER_PROPERTY_SOFTWARE_VERSION = 259,

/** Readonly adapter MAC address MSBs.
The MAC_ADDRESS_MSB property returns
the most significant 32 bits of the MAC address.
Property 1 contains bits 47..32 of the MAC address.
Property 2 contains bits 31..16 of the MAC address. */
	HPI_ADAPTER_PROPERTY_MAC_ADDRESS_MSB = 260,

/** Readonly adapter MAC address LSBs
The MAC_ADDRESS_LSB property returns
the least significant 16 bits of the MAC address.
Property 1 contains bits 15..0 of the MAC address. */
	HPI_ADAPTER_PROPERTY_MAC_ADDRESS_LSB = 261,

/** Readonly extended adapter type number
The EXTENDED_ADAPTER_TYPE property returns the 4 digits of an extended
adapter type, i.e ASI8920-0022, 0022 is the extended type.
The digits are returned as ASCII characters rather than the hex digits that
are returned for the main type
Property 1 returns the 1st two (left most) digits, i.e "00"
in the example above, the upper byte being the left most digit.
Property 2 returns the 2nd two digits, i.e "22" in the example above*/
	HPI_ADAPTER_PROPERTY_EXTENDED_ADAPTER_TYPE = 262,

/** Readonly debug log buffer information */
	HPI_ADAPTER_PROPERTY_LOGTABLEN = 263,
	HPI_ADAPTER_PROPERTY_LOGTABBEG = 264,

/** Readonly adapter IP address
For 192.168.1.101
Property 1 returns the 1st two (left most) digits, i.e 192*256 + 168
in the example above, the upper byte being the left most digit.
Property 2 returns the 2nd two digits, i.e 1*256 + 101 in the example above, */
	HPI_ADAPTER_PROPERTY_IP_ADDRESS = 265,

/** Readonly adapter buffer processed count. Returns a buffer processed count
that is incremented every time all buffers for all streams are updated. This
is useful for checking completion of all stream operations across the adapter
when using grouped streams.
*/
	HPI_ADAPTER_PROPERTY_BUFFER_UPDATE_COUNT = 266,

/** Readonly mixer and stream intervals

These intervals are  measured in mixer frames.
To convert to time, divide  by the adapter samplerate.

The mixer interval is the number of frames processed in one mixer iteration.
The stream update interval is the interval at which streams check for and
process data, and BBM host buffer counters are updated.

Property 1 is the mixer interval in mixer frames.
Property 2 is the stream update interval in mixer frames.
*/
	HPI_ADAPTER_PROPERTY_INTERVAL = 267,
/** Adapter capabilities 1
Property 1 - adapter can do multichannel (SSX1)
Property 2 - adapter can do stream grouping (supports SSX2)
*/
	HPI_ADAPTER_PROPERTY_CAPS1 = 268,
/** Adapter capabilities 2
Property 1 - adapter can do samplerate conversion (MRX)
Property 2 - adapter can do timestretch (TSX)
*/
	HPI_ADAPTER_PROPERTY_CAPS2 = 269,

/** Readonly adapter sync header connection count.
*/
	HPI_ADAPTER_PROPERTY_SYNC_HEADER_CONNECTIONS = 270,
/** Readonly supports SSX2 property.
Indicates the adapter supports SSX2 in some mode setting. The
return value is true (1) or false (0). If the current adapter
mode is MONO SSX2 is disabled, even though this property will
return true.
*/
	HPI_ADAPTER_PROPERTY_SUPPORTS_SSX2 = 271,
/** Readonly supports PCI(e) IRQ.
Indicates that the adapter in it's current mode supports interrupts
across the host bus. Note, this does not imply that interrupts are
enabled. Instead it indicates that they can be enabled.
*/
	HPI_ADAPTER_PROPERTY_SUPPORTS_IRQ = 272,
/** Readonly supports firmware updating.
Indicates that the adapter implements an interface to update firmware
on the adapter.
*/
	HPI_ADAPTER_PROPERTY_SUPPORTS_FW_UPDATE = 273,
/** Readonly Firmware IDs
Identifiy firmware independent of individual adapter type.
May be used as a filter for firmware update images.
Property 1 = Bootloader ID
Property 2 = Main program ID
*/
	HPI_ADAPTER_PROPERTY_FIRMWARE_ID = 274
};

/** Adapter mode commands

Used in wQueryOrSet parameter of HPI_AdapterSetModeEx().
\ingroup adapter
*/
enum HPI_ADAPTER_MODE_CMDS {
	/** Set the mode to the given parameter */
	HPI_ADAPTER_MODE_SET = 0,
	/** Return 0 or error depending whether mode is valid,
	but don't set the mode */
	HPI_ADAPTER_MODE_QUERY = 1
};

/** Adapter Modes
 These are used by HPI_AdapterSetModeEx()

\warning - more than 16 possible modes breaks
a bitmask in the Windows WAVE DLL
\ingroup adapter
*/
enum HPI_ADAPTER_MODES {
/** 4 outstream mode.
- ASI6114: 1 instream
- ASI6044: 4 instreams
- ASI6012: 1 instream
- ASI6102: no instreams
- ASI6022, ASI6122: 2 instreams
- ASI5111, ASI5101: 2 instreams
- ASI652x, ASI662x: 2 instreams
- ASI654x, ASI664x: 4 instreams
*/
	HPI_ADAPTER_MODE_4OSTREAM = 1,

/** 6 outstream mode.
- ASI6012: 1 instream,
- ASI6022, ASI6122: 2 instreams
- ASI652x, ASI662x: 4 instreams
*/
	HPI_ADAPTER_MODE_6OSTREAM = 2,

/** 8 outstream mode.
- ASI6114: 8 instreams
- ASI6118: 8 instreams
- ASI6585: 8 instreams
*/
	HPI_ADAPTER_MODE_8OSTREAM = 3,

/** 16 outstream mode.
- ASI6416 16 instreams
- ASI6518, ASI6618 16 instreams
- ASI6118 16 mono out and in streams
*/
	HPI_ADAPTER_MODE_16OSTREAM = 4,

/** one outstream mode.
- ASI5111 1 outstream, 1 instream
*/
	HPI_ADAPTER_MODE_1OSTREAM = 5,

/** ASI504X mode 1. 12 outstream, 4 instream 0 to 48kHz sample rates
	(see ASI504X datasheet for more info).
*/
	HPI_ADAPTER_MODE_1 = 6,

/** ASI504X mode 2. 4 outstreams, 4 instreams at 0 to 192kHz sample rates
	(see ASI504X datasheet for more info).
*/
	HPI_ADAPTER_MODE_2 = 7,

/** ASI504X mode 3. 4 outstreams, 4 instreams at 0 to 192kHz sample rates
	(see ASI504X datasheet for more info).
*/
	HPI_ADAPTER_MODE_3 = 8,

/** ASI504X multichannel mode.
	2 outstreams -> 4 line outs = 1 to 8 channel streams),
	4 lineins -> 1 instream (1 to 8 channel streams) at 0-48kHz.
	For more info see the SSX Specification.
*/
	HPI_ADAPTER_MODE_MULTICHANNEL = 9,

/** 12 outstream mode.
- ASI6514, ASI6614: 2 instreams
- ASI6540,ASI6544: 8 instreams
- ASI6640,ASI6644: 8 instreams
*/
	HPI_ADAPTER_MODE_12OSTREAM = 10,

/** 9 outstream mode.
- ASI6044: 8 instreams
*/
	HPI_ADAPTER_MODE_9OSTREAM = 11,

/** mono mode.
- ASI6416: 16 outstreams/instreams
- ASI5402: 2 outstreams/instreams
*/
	HPI_ADAPTER_MODE_MONO = 12,

/** Low latency mode.
- ASI6416/ASI6316: 1 16 channel outstream and instream
*/
	HPI_ADAPTER_MODE_LOW_LATENCY = 13
};

/* Note, adapters can have more than one capability -
encoding as bitfield is recommended. */
#define HPI_CAPABILITY_NONE             (0)
#define HPI_CAPABILITY_MPEG_LAYER3      (1)

/* Set this equal to maximum capability index,
Must not be greater than 32 - see axnvdef.h */
#define HPI_CAPABILITY_MAX                      1
/* #define HPI_CAPABILITY_AAC              2 */

/******************************************* STREAM ATTRIBUTES ****/

/** MPEG Ancillary Data modes

The mode for the ancillary data insertion or extraction to operate in.
\ingroup stream
*/
enum HPI_MPEG_ANC_MODES {
	/** the MPEG frames have energy information stored in them (5 bytes per stereo frame, 3 per mono) */
	HPI_MPEG_ANC_HASENERGY = 0,
	/** the entire ancillary data field is taken up by data from the Anc data buffer
	On encode, the encoder will insert the energy bytes before filling the remainder
	of the ancillary data space with data from the ancillary data buffer.
	*/
	HPI_MPEG_ANC_RAW = 1
};

/** Ancillary Data Alignment
\ingroup instream
*/
enum HPI_ISTREAM_MPEG_ANC_ALIGNS {
	/** data is packed against the end of data, then padded to the end of frame */
	HPI_MPEG_ANC_ALIGN_LEFT = 0,
	/** data is packed against the end of the frame */
	HPI_MPEG_ANC_ALIGN_RIGHT = 1
};

/** MPEG modes
MPEG modes - can be used optionally for HPI_FormatCreate()
parameter dwAttributes.

Using any mode setting other than HPI_MPEG_MODE_DEFAULT
with single channel format will return an error.
\ingroup stream
*/
enum HPI_MPEG_MODES {
/** Causes the MPEG-1 Layer II bitstream to be recorded
in single_channel mode when the number of channels is 1 and in stereo when the
number of channels is 2. */
	HPI_MPEG_MODE_DEFAULT = 0,
	/** Standard stereo without joint-stereo compression */
	HPI_MPEG_MODE_STEREO = 1,
	/** Joint stereo  */
	HPI_MPEG_MODE_JOINTSTEREO = 2,
	/** Left and Right channels are completely independent */
	HPI_MPEG_MODE_DUALCHANNEL = 3
};
/******************************************* MIXER ATTRIBUTES ****/

/* \defgroup mixer_flags Mixer flags for HPI_MIXER_GET_CONTROL_MULTIPLE_VALUES
{
*/
#define HPI_MIXER_GET_CONTROL_MULTIPLE_CHANGED  (0)
#define HPI_MIXER_GET_CONTROL_MULTIPLE_RESET    (1)
/*}*/

/** Commands used by HPI_MixerStore()
\ingroup mixer
*/
enum HPI_MIXER_STORE_COMMAND {
/** Save all mixer control settings. */
	HPI_MIXER_STORE_SAVE = 1,
/** Restore all controls from saved. */
	HPI_MIXER_STORE_RESTORE = 2,
/** Delete saved control settings. */
	HPI_MIXER_STORE_DELETE = 3,
/** Enable auto storage of some control settings. */
	HPI_MIXER_STORE_ENABLE = 4,
/** Disable auto storage of some control settings. */
	HPI_MIXER_STORE_DISABLE = 5,
/** Unimplemented - save the attributes of a single control. */
	HPI_MIXER_STORE_SAVE_SINGLE = 6
};

/****************************/
/* CONTROL ATTRIBUTE VALUES */
/****************************/

/** Used by mixer plugin enable functions

E.g. HPI_ParametricEq_SetState()
\ingroup mixer
*/
enum HPI_SWITCH_STATES {
	HPI_SWITCH_OFF = 0,	/**< turn the mixer plugin on. */
	HPI_SWITCH_ON = 1	/**< turn the mixer plugin off. */
};

/* Volume control special gain values */

/** volumes units are 100ths of a dB
\ingroup volume
*/
#define HPI_UNITS_PER_dB                100
/** turns volume control OFF or MUTE
\ingroup volume
*/
#define HPI_GAIN_OFF                    (-100 * HPI_UNITS_PER_dB)

/** channel mask specifying all channels
\ingroup volume
*/
#define HPI_BITMASK_ALL_CHANNELS        (0xFFFFFFFF)

/** value returned for no signal
\ingroup meter
*/
#define HPI_METER_MINIMUM               (-150 * HPI_UNITS_PER_dB)

/** autofade profiles
\ingroup volume
*/
enum HPI_VOLUME_AUTOFADES {
/** log fade - dB attenuation changes linearly over time */
	HPI_VOLUME_AUTOFADE_LOG = 2,
/** linear fade - amplitude changes linearly */
	HPI_VOLUME_AUTOFADE_LINEAR = 3
};

/** The physical encoding format of the AESEBU I/O.

Used in HPI_Aesebu_Transmitter_SetFormat(), HPI_Aesebu_Receiver_SetFormat()
along with related Get and Query functions
\ingroup aestx
*/
enum HPI_AESEBU_FORMATS {
/** AES/EBU physical format - AES/EBU balanced "professional"  */
	HPI_AESEBU_FORMAT_AESEBU = 1,
/** AES/EBU physical format - S/PDIF unbalanced "consumer"      */
	HPI_AESEBU_FORMAT_SPDIF = 2
};

/** AES/EBU error status bits

Returned by HPI_Aesebu_Receiver_GetErrorStatus()
\ingroup aesrx
*/
enum HPI_AESEBU_ERRORS {
/**  bit0: 1 when PLL is not locked */
	HPI_AESEBU_ERROR_NOT_LOCKED = 0x01,
/**  bit1: 1 when signal quality is poor */
	HPI_AESEBU_ERROR_POOR_QUALITY = 0x02,
/** bit2: 1 when there is a parity error */
	HPI_AESEBU_ERROR_PARITY_ERROR = 0x04,
/**  bit3: 1 when there is a bi-phase coding violation */
	HPI_AESEBU_ERROR_BIPHASE_VIOLATION = 0x08,
/**  bit4: 1 when the validity bit is high */
	HPI_AESEBU_ERROR_VALIDITY = 0x10,
/**  bit5: 1 when the CRC error bit is high */
	HPI_AESEBU_ERROR_CRC = 0x20
};

/** \addtogroup pad
\{
*/
/** The text string containing the station/channel combination. */
#define HPI_PAD_CHANNEL_NAME_LEN        16
/** The text string containing the artist. */
#define HPI_PAD_ARTIST_LEN              64
/** The text string containing the title. */
#define HPI_PAD_TITLE_LEN               64
/** The text string containing the comment. */
#define HPI_PAD_COMMENT_LEN             256
/** The PTY when the tuner has not received any PTY. */
#define HPI_PAD_PROGRAM_TYPE_INVALID    0xffff
/** \} */

/** Data types for PTY string translation.
\ingroup rds
*/
enum eHPI_RDS_type {
	HPI_RDS_DATATYPE_RDS = 0,	/**< RDS bitstream.*/
	HPI_RDS_DATATYPE_RBDS = 1	/**< RBDS bitstream.*/
};

/** Tuner bands

Used for HPI_Tuner_SetBand(),HPI_Tuner_GetBand()
\ingroup tuner
*/
enum HPI_TUNER_BAND {
	HPI_TUNER_BAND_AM = 1,	 /**< AM band */
	HPI_TUNER_BAND_FM = 2,	 /**< FM band (mono) */
	HPI_TUNER_BAND_TV_NTSC_M = 3,	 /**< NTSC-M TV band*/
	HPI_TUNER_BAND_TV = 3,	/* use TV_NTSC_M */
	HPI_TUNER_BAND_FM_STEREO = 4,	 /**< FM band (stereo) */
	HPI_TUNER_BAND_AUX = 5,	 /**< auxiliary input */
	HPI_TUNER_BAND_TV_PAL_BG = 6,	 /**< PAL-B/G TV band*/
	HPI_TUNER_BAND_TV_PAL_I = 7,	 /**< PAL-I TV band*/
	HPI_TUNER_BAND_TV_PAL_DK = 8,	 /**< PAL-D/K TV band*/
	HPI_TUNER_BAND_TV_SECAM_L = 9,	 /**< SECAM-L TV band*/
	HPI_TUNER_BAND_DAB = 10,
	HPI_TUNER_BAND_LAST = 10 /**< the index of the last tuner band. */
};

/** Tuner mode attributes

Used by HPI_Tuner_SetMode(), HPI_Tuner_GetMode()
\ingroup tuner

*/
enum HPI_TUNER_MODES {
	HPI_TUNER_MODE_RSS = 1,	/**< control  RSS */
	HPI_TUNER_MODE_RDS = 2	/**< control  RBDS/RDS */
};

/** Tuner mode attribute values

Used by HPI_Tuner_SetMode(), HPI_Tuner_GetMode()
\ingroup tuner
*/
enum HPI_TUNER_MODE_VALUES {
/* RSS attribute values */
	HPI_TUNER_MODE_RSS_DISABLE = 0,	/**< RSS disable */
	HPI_TUNER_MODE_RSS_ENABLE = 1,	/**< RSS enable */

/* RDS mode attributes */
	HPI_TUNER_MODE_RDS_DISABLE = 0,	/**< RDS - disabled */
	HPI_TUNER_MODE_RDS_RDS = 1,  /**< RDS - RDS mode */
	HPI_TUNER_MODE_RDS_RBDS = 2 /**<  RDS - RBDS mode */
};

/** Tuner Status Bits

These bitfield values are returned by a call to HPI_Tuner_GetStatus().
Multiple fields are returned from a single call.
\ingroup tuner
*/
enum HPI_TUNER_STATUS_BITS {
	HPI_TUNER_VIDEO_COLOR_PRESENT = 0x0001,	/**< video color is present. */
	HPI_TUNER_VIDEO_IS_60HZ = 0x0020, /**< 60 hz video detected. */
	HPI_TUNER_VIDEO_HORZ_SYNC_MISSING = 0x0040, /**< video HSYNC is missing. */
	HPI_TUNER_VIDEO_STATUS_VALID = 0x0100, /**< video status is valid. */
	HPI_TUNER_DIGITAL = 0x0200, /**< tuner reports digital programming. */
	HPI_TUNER_MULTIPROGRAM = 0x0400, /**< tuner reports multiple programs. */
	HPI_TUNER_PLL_LOCKED = 0x1000, /**< the tuner's PLL is locked. */
	HPI_TUNER_FM_STEREO = 0x2000 /**< tuner reports back FM stereo. */
};

/** Channel Modes
Used for HPI_ChannelModeSet/Get()
\ingroup channelmode
*/
enum HPI_CHANNEL_MODES {
/** Left channel out = left channel in, Right channel out = right channel in. */
	HPI_CHANNEL_MODE_NORMAL = 1,
/** Left channel out = right channel in, Right channel out = left channel in. */
	HPI_CHANNEL_MODE_SWAP = 2,
/** Left channel out = left channel in, Right channel out = left channel in. */
	HPI_CHANNEL_MODE_LEFT_TO_STEREO = 3,
/** Left channel out = right channel in, Right channel out = right channel in.*/
	HPI_CHANNEL_MODE_RIGHT_TO_STEREO = 4,
/** Left channel out = (left channel in + right channel in)/2,
    Right channel out = mute. */
	HPI_CHANNEL_MODE_STEREO_TO_LEFT = 5,
/** Left channel out = mute,
    Right channel out = (right channel in + left channel in)/2. */
	HPI_CHANNEL_MODE_STEREO_TO_RIGHT = 6,
	HPI_CHANNEL_MODE_LAST = 6
};

/** SampleClock source values
\ingroup sampleclock
*/
enum HPI_SAMPLECLOCK_SOURCES {
/** The sampleclock output is derived from its local samplerate generator.
    The local samplerate may be set using HPI_SampleClock_SetLocalRate(). */
	HPI_SAMPLECLOCK_SOURCE_LOCAL = 1,
/** The adapter is clocked from a dedicated AES/EBU SampleClock input.*/
	HPI_SAMPLECLOCK_SOURCE_AESEBU_SYNC = 2,
/** From external wordclock connector */
	HPI_SAMPLECLOCK_SOURCE_WORD = 3,
/** Board-to-board header */
	HPI_SAMPLECLOCK_SOURCE_WORD_HEADER = 4,
/** FUTURE - SMPTE clock. */
	HPI_SAMPLECLOCK_SOURCE_SMPTE = 5,
/** One of the aesebu inputs */
	HPI_SAMPLECLOCK_SOURCE_AESEBU_INPUT = 6,
/** From a network interface e.g. Cobranet or Livewire at either 48 or 96kHz */
	HPI_SAMPLECLOCK_SOURCE_NETWORK = 8,
/** From previous adjacent module (ASI2416 only)*/
	HPI_SAMPLECLOCK_SOURCE_PREV_MODULE = 10,
/** Blu link sample clock*/
	HPI_SAMPLECLOCK_SOURCE_BLULINK = 11,
/*! Update this if you add a new clock source.*/
	HPI_SAMPLECLOCK_SOURCE_LAST = 11
};

/** Equalizer filter types. Used by HPI_ParametricEq_SetBand()
\ingroup parmeq
*/
enum HPI_FILTER_TYPE {
	HPI_FILTER_TYPE_BYPASS = 0,	/**< filter is turned off */

	HPI_FILTER_TYPE_LOWSHELF = 1,	/**< EQ low shelf */
	HPI_FILTER_TYPE_HIGHSHELF = 2,	/**< EQ high shelf */
	HPI_FILTER_TYPE_EQ_BAND = 3,	/**< EQ gain */

	HPI_FILTER_TYPE_LOWPASS = 4,	/**< standard low pass */
	HPI_FILTER_TYPE_HIGHPASS = 5,	/**< standard high pass */
	HPI_FILTER_TYPE_BANDPASS = 6,	/**< standard band pass */
	HPI_FILTER_TYPE_BANDSTOP = 7	/**< standard band stop/notch */
};

/** Async Event sources
\ingroup async
*/
enum ASYNC_EVENT_SOURCES {
	HPI_ASYNC_EVENT_GPIO = 1,	/**< GPIO event. */
	HPI_ASYNC_EVENT_SILENCE = 2,	/**< silence event detected. */
	HPI_ASYNC_EVENT_TONE = 3	/**< tone event detected. */
};
/*******************************************/
/** HPI Error codes

Almost all HPI functions return an error code
A return value of zero means there was no error.
Otherwise one of these error codes is returned.
Error codes can be converted to a descriptive string using HPI_GetErrorText()

\note When a new error code is added HPI_GetErrorText() MUST be updated.
\note Codes 1-100 are reserved for driver use
\ingroup utility
*/
enum HPI_ERROR_CODES {
	/** Message type does not exist. */
	HPI_ERROR_INVALID_TYPE = 100,
	/** Object type does not exist. */
	HPI_ERROR_INVALID_OBJ = 101,
	/** Function does not exist. */
	HPI_ERROR_INVALID_FUNC = 102,
	/** The specified object does not exist. */
	HPI_ERROR_INVALID_OBJ_INDEX = 103,
	/** Trying to access an object that has not been opened yet. */
	HPI_ERROR_OBJ_NOT_OPEN = 104,
	/** Trying to open an already open object. */
	HPI_ERROR_OBJ_ALREADY_OPEN = 105,
	/** PCI, ISA resource not valid. */
	HPI_ERROR_INVALID_RESOURCE = 106,
	/* HPI_ERROR_SUBSYSFINDADAPTERS_GETINFO= 107 */
	/** Default response was never updated with actual error code. */
	HPI_ERROR_INVALID_RESPONSE = 108,
	/** wSize field of response was not updated,
	indicating that the message was not processed. */
	HPI_ERROR_PROCESSING_MESSAGE = 109,
	/** The network did not respond in a timely manner. */
	HPI_ERROR_NETWORK_TIMEOUT = 110,
	/* An HPI handle is invalid (uninitialised?). */
	HPI_ERROR_INVALID_HANDLE = 111,
	/** A function or attribute has not been implemented yet. */
	HPI_ERROR_UNIMPLEMENTED = 112,
	/** There are too many clients attempting
	    to access a network resource. */
	HPI_ERROR_NETWORK_TOO_MANY_CLIENTS = 113,
	/** Response buffer passed to HPI_Message
	    was smaller than returned response.
	    wSpecificError field of hpi response contains the required size.
	*/
	HPI_ERROR_RESPONSE_BUFFER_TOO_SMALL = 114,
	/** The returned response did not match the sent message */
	HPI_ERROR_RESPONSE_MISMATCH = 115,
	/** A control setting that should have been cached was not. */
	HPI_ERROR_CONTROL_CACHING = 116,
	/** A message buffer in the path to the adapter was smaller
	    than the message size.
	    wSpecificError field of hpi response contains the actual size.
	*/
	HPI_ERROR_MESSAGE_BUFFER_TOO_SMALL = 117,

	/* HPI_ERROR_TOO_MANY_ADAPTERS= 200 */
	/** Bad adpater. */
	HPI_ERROR_BAD_ADAPTER = 201,
	/** Adapter number out of range or not set properly. */
	HPI_ERROR_BAD_ADAPTER_NUMBER = 202,
	/** 2 adapters with the same adapter number. */
	HPI_ERROR_DUPLICATE_ADAPTER_NUMBER = 203,
	/** DSP code failed to bootload. Usually a DSP memory test failure. */
	HPI_ERROR_DSP_BOOTLOAD = 204,
	/** Couldn't find or open the DSP code file. */
	HPI_ERROR_DSP_FILE_NOT_FOUND = 206,
	/** Internal DSP hardware error. */
	HPI_ERROR_DSP_HARDWARE = 207,
	/** Could not allocate memory */
	HPI_ERROR_MEMORY_ALLOC = 208,
	/** Failed to correctly load/config PLD. (unused) */
	HPI_ERROR_PLD_LOAD = 209,
	/** Unexpected end of file, block length too big etc. */
	HPI_ERROR_DSP_FILE_FORMAT = 210,

	/** Found but could not open DSP code file. */
	HPI_ERROR_DSP_FILE_ACCESS_DENIED = 211,
	/** First DSP code section header not found in DSP file. */
	HPI_ERROR_DSP_FILE_NO_HEADER = 212,
	/* HPI_ERROR_DSP_FILE_READ_ERROR= 213, */
	/** DSP code for adapter family not found. */
	HPI_ERROR_DSP_SECTION_NOT_FOUND = 214,
	/** Other OS specific error opening DSP file. */
	HPI_ERROR_DSP_FILE_OTHER_ERROR = 215,
	/** Sharing violation opening DSP code file. */
	HPI_ERROR_DSP_FILE_SHARING_VIOLATION = 216,
	/** DSP code section header had size == 0. */
	HPI_ERROR_DSP_FILE_NULL_HEADER = 217,

	/* HPI_ERROR_FLASH = 220, */

	/** Flash has bad checksum */
	HPI_ERROR_BAD_CHECKSUM = 221,
	HPI_ERROR_BAD_SEQUENCE = 222,
	HPI_ERROR_FLASH_ERASE = 223,
	HPI_ERROR_FLASH_PROGRAM = 224,
	HPI_ERROR_FLASH_VERIFY = 225,
	HPI_ERROR_FLASH_TYPE = 226,
	HPI_ERROR_FLASH_START = 227,
	HPI_ERROR_FLASH_READ = 228,
	HPI_ERROR_FLASH_READ_NO_FILE = 229,
	HPI_ERROR_FLASH_SIZE = 230,

	/** Reserved for OEMs. */
	HPI_ERROR_RESERVED_1 = 290,

	/* HPI_ERROR_INVALID_STREAM = 300 use HPI_ERROR_INVALID_OBJ_INDEX */
	/** Invalid compression format. */
	HPI_ERROR_INVALID_FORMAT = 301,
	/** Invalid format samplerate */
	HPI_ERROR_INVALID_SAMPLERATE = 302,
	/** Invalid format number of channels. */
	HPI_ERROR_INVALID_CHANNELS = 303,
	/** Invalid format bitrate. */
	HPI_ERROR_INVALID_BITRATE = 304,
	/** Invalid datasize used for stream read/write. */
	HPI_ERROR_INVALID_DATASIZE = 305,
	/* HPI_ERROR_BUFFER_FULL = 306 use HPI_ERROR_INVALID_DATASIZE */
	/* HPI_ERROR_BUFFER_EMPTY = 307 use HPI_ERROR_INVALID_DATASIZE */
	/** Null data pointer used for stream read/write. */
	HPI_ERROR_INVALID_DATA_POINTER = 308,
	/** Packet ordering error for stream read/write. */
	HPI_ERROR_INVALID_PACKET_ORDER = 309,

	/** Object can't do requested operation in its current
	    state, eg set format, change rec mux state while recording.*/
	HPI_ERROR_INVALID_OPERATION = 310,

	/** Where a SRG is shared amongst streams, an incompatible samplerate
	    is one that is different to any currently active stream. */
	HPI_ERROR_INCOMPATIBLE_SAMPLERATE = 311,
	/** Adapter mode is illegal.*/
	HPI_ERROR_BAD_ADAPTER_MODE = 312,

	/** There have been too many attempts to set the adapter's
	capabilities (using bad keys), the card should be returned
	to ASI if further capabilities updates are required */
	HPI_ERROR_TOO_MANY_CAPABILITY_CHANGE_ATTEMPTS = 313,
	/** Streams on different adapters cannot be grouped. */
	HPI_ERROR_NO_INTERADAPTER_GROUPS = 314,
	/** Streams on different DSPs cannot be grouped. */
	HPI_ERROR_NO_INTERDSP_GROUPS = 315,
	/** Stream wait cancelled before threshold reached. */
	HPI_ERROR_WAIT_CANCELLED = 316,
	/** A character string is invalid. */
	HPI_ERROR_INVALID_STRING = 317,

	/** Invalid mixer node for this adapter. */
	HPI_ERROR_INVALID_NODE = 400,
	/** Invalid control. */
	HPI_ERROR_INVALID_CONTROL = 401,
	/** Invalid control value was passed. */
	HPI_ERROR_INVALID_CONTROL_VALUE = 402,
	/** Control attribute not supported by this control. */
	HPI_ERROR_INVALID_CONTROL_ATTRIBUTE = 403,
	/** Control is disabled. */
	HPI_ERROR_CONTROL_DISABLED = 404,
	/** I2C transaction failed due to a missing ACK. */
	HPI_ERROR_CONTROL_I2C_MISSING_ACK = 405,
	HPI_ERROR_I2C_MISSING_ACK = 405,
	/** Control is busy, or coming out of
	reset and cannot be accessed at this time. */
	HPI_ERROR_CONTROL_NOT_READY = 407,

	/** Non volatile memory */
	HPI_ERROR_NVMEM_BUSY = 450,
	HPI_ERROR_NVMEM_FULL = 451,
	HPI_ERROR_NVMEM_FAIL = 452,

	/** I2C */
	HPI_ERROR_I2C_BAD_ADR = 460,

	/** Entity type did not match requested type */
	HPI_ERROR_ENTITY_TYPE_MISMATCH = 470,
	/** Entity item count did not match requested count */
	HPI_ERROR_ENTITY_ITEM_COUNT = 471,
	/** Entity type is not one of the valid types */
	HPI_ERROR_ENTITY_TYPE_INVALID = 472,
	/** Entity role is not one of the valid roles */
	HPI_ERROR_ENTITY_ROLE_INVALID = 473,
	/** Entity size doesn't match target size */
	HPI_ERROR_ENTITY_SIZE_MISMATCH = 474,

	/* AES18 specific errors were 500..507 */

	/** custom error to use for debugging */
	HPI_ERROR_CUSTOM = 600,

	/** hpioct32.c can't obtain mutex */
	HPI_ERROR_MUTEX_TIMEOUT = 700,

	/** Backend errors used to be greater than this.
	    \deprecated Now, all backends return only errors defined here in hpi.h
	*/
	HPI_ERROR_BACKEND_BASE = 900,

	/** Communication with DSP failed */
	HPI_ERROR_DSP_COMMUNICATION = 900
		/* Note that the dsp communication error is set to this value so that
		   it remains compatible with any software that expects such errors
		   to be backend errors i.e. >= 900.
		   Do not define any new error codes with values > 900.
		 */
};

/** \defgroup maximums HPI maximum values
\{
*/
/** Maximum number of PCI HPI adapters */
#define HPI_MAX_ADAPTERS                20
/** Maximum number of in or out streams per adapter */
#define HPI_MAX_STREAMS                 16
#define HPI_MAX_CHANNELS                2	/* per stream */
#define HPI_MAX_NODES                   8	/* per mixer ? */
#define HPI_MAX_CONTROLS                4	/* per node ? */
/** maximum number of ancillary bytes per MPEG frame */
#define HPI_MAX_ANC_BYTES_PER_FRAME     (64)
#define HPI_STRING_LEN                  16

/** Networked adapters have index >= 100 */
#define HPI_MIN_NETWORK_ADAPTER_IDX 100

/** Velocity units */
#define HPI_OSTREAM_VELOCITY_UNITS      4096
/** OutStream timescale units */
#define HPI_OSTREAM_TIMESCALE_UNITS     10000
/** OutStream timescale passthrough - turns timescaling on in passthough mode */
#define HPI_OSTREAM_TIMESCALE_PASSTHROUGH       99999

/**\}*/

/**************/
/* STRUCTURES */
#ifndef DISABLE_PRAGMA_PACK1
#pragma pack(push, 1)
#endif

/** Structure containing sample format information.
    See also HPI_FormatCreate().
  */
struct hpi_format {
	u32 sample_rate;
				/**< 11025, 32000, 44100 ... */
	u32 bit_rate;		  /**< for MPEG */
	u32 attributes;
				/**< Stereo/JointStereo/Mono */
	u16 mode_legacy;
				/**< Legacy ancillary mode or idle bit  */
	u16 unused;		  /**< Unused */
	u16 channels;	  /**< 1,2..., (or ancillary mode or idle bit */
	u16 format;	  /**< HPI_FORMAT_PCM16, _MPEG etc. see #HPI_FORMATS. */
};

struct hpi_anc_frame {
	u32 valid_bits_in_this_frame;
	u8 b_data[HPI_MAX_ANC_BYTES_PER_FRAME];
};

/** An object for containing a single async event.
*/
struct hpi_async_event {
	u16 event_type;	/**< type of event. \sa async_event  */
	u16 sequence; /**< Sequence number, allows lost event detection */
	u32 state; /**< New state */
	u32 h_object; /**< handle to the object returning the event. */
	union {
		struct {
			u16 index; /**< GPIO bit index. */
		} gpio;
		struct {
			u16 node_index;	/**< what node is the control on ? */
			u16 node_type; /**< what type of node is the control on ? */
		} control;
	} u;
};

#ifndef DISABLE_PRAGMA_PACK1
#pragma pack(pop)
#endif

/*****************/
/* HPI FUNCTIONS */
/*****************/

/* Stream */
u16 hpi_stream_estimate_buffer_size(struct hpi_format *pF,
	u32 host_polling_rate_in_milli_seconds, u32 *recommended_buffer_size);

/*************/
/* SubSystem */
/*************/

u16 hpi_subsys_get_version_ex(u32 *pversion_ex);

u16 hpi_subsys_get_num_adapters(int *pn_num_adapters);

u16 hpi_subsys_get_adapter(int iterator, u32 *padapter_index,
	u16 *pw_adapter_type);

/***********/
/* Adapter */
/***********/

u16 hpi_adapter_open(u16 adapter_index);

u16 hpi_adapter_close(u16 adapter_index);

u16 hpi_adapter_get_info(u16 adapter_index, u16 *pw_num_outstreams,
	u16 *pw_num_instreams, u16 *pw_version, u32 *pserial_number,
	u16 *pw_adapter_type);

u16 hpi_adapter_get_module_by_index(u16 adapter_index, u16 module_index,
	u16 *pw_num_outputs, u16 *pw_num_inputs, u16 *pw_version,
	u32 *pserial_number, u16 *pw_module_type, u32 *ph_module);

u16 hpi_adapter_set_mode(u16 adapter_index, u32 adapter_mode);

u16 hpi_adapter_set_mode_ex(u16 adapter_index, u32 adapter_mode,
	u16 query_or_set);

u16 hpi_adapter_get_mode(u16 adapter_index, u32 *padapter_mode);

u16 hpi_adapter_get_assert2(u16 adapter_index, u16 *p_assert_count,
	char *psz_assert, u32 *p_param1, u32 *p_param2,
	u32 *p_dsp_string_addr, u16 *p_processor_id);

u16 hpi_adapter_test_assert(u16 adapter_index, u16 assert_id);

u16 hpi_adapter_enable_capability(u16 adapter_index, u16 capability, u32 key);

u16 hpi_adapter_self_test(u16 adapter_index);

u16 hpi_adapter_debug_read(u16 adapter_index, u32 dsp_address, char *p_bytes,
	int *count_bytes);

u16 hpi_adapter_set_property(u16 adapter_index, u16 property, u16 paramter1,
	u16 paramter2);

u16 hpi_adapter_get_property(u16 adapter_index, u16 property,
	u16 *pw_paramter1, u16 *pw_paramter2);

u16 hpi_adapter_enumerate_property(u16 adapter_index, u16 index,
	u16 what_to_enumerate, u16 property_index, u32 *psetting);
/*************/
/* OutStream */
/*************/
u16 hpi_outstream_open(u16 adapter_index, u16 outstream_index,
	u32 *ph_outstream);

u16 hpi_outstream_close(u32 h_outstream);

u16 hpi_outstream_get_info_ex(u32 h_outstream, u16 *pw_state,
	u32 *pbuffer_size, u32 *pdata_to_play, u32 *psamples_played,
	u32 *pauxiliary_data_to_play);

u16 hpi_outstream_write_buf(u32 h_outstream, const u8 *pb_write_buf,
	u32 bytes_to_write, const struct hpi_format *p_format);

u16 hpi_outstream_start(u32 h_outstream);

u16 hpi_outstream_wait_start(u32 h_outstream);

u16 hpi_outstream_stop(u32 h_outstream);

u16 hpi_outstream_sinegen(u32 h_outstream);

u16 hpi_outstream_reset(u32 h_outstream);

u16 hpi_outstream_query_format(u32 h_outstream, struct hpi_format *p_format);

u16 hpi_outstream_set_format(u32 h_outstream, struct hpi_format *p_format);

u16 hpi_outstream_set_punch_in_out(u32 h_outstream, u32 punch_in_sample,
	u32 punch_out_sample);

u16 hpi_outstream_set_velocity(u32 h_outstream, short velocity);

u16 hpi_outstream_ancillary_reset(u32 h_outstream, u16 mode);

u16 hpi_outstream_ancillary_get_info(u32 h_outstream, u32 *pframes_available);

u16 hpi_outstream_ancillary_read(u32 h_outstream,
	struct hpi_anc_frame *p_anc_frame_buffer,
	u32 anc_frame_buffer_size_in_bytes,
	u32 number_of_ancillary_frames_to_read);

u16 hpi_outstream_set_time_scale(u32 h_outstream, u32 time_scaleX10000);

u16 hpi_outstream_host_buffer_allocate(u32 h_outstream, u32 size_in_bytes);

u16 hpi_outstream_host_buffer_free(u32 h_outstream);

u16 hpi_outstream_group_add(u32 h_outstream, u32 h_stream);

u16 hpi_outstream_group_get_map(u32 h_outstream, u32 *poutstream_map,
	u32 *pinstream_map);

u16 hpi_outstream_group_reset(u32 h_outstream);

/************/
/* InStream */
/************/
u16 hpi_instream_open(u16 adapter_index, u16 instream_index,
	u32 *ph_instream);

u16 hpi_instream_close(u32 h_instream);

u16 hpi_instream_query_format(u32 h_instream,
	const struct hpi_format *p_format);

u16 hpi_instream_set_format(u32 h_instream,
	const struct hpi_format *p_format);

u16 hpi_instream_read_buf(u32 h_instream, u8 *pb_read_buf, u32 bytes_to_read);

u16 hpi_instream_start(u32 h_instream);

u16 hpi_instream_wait_start(u32 h_instream);

u16 hpi_instream_stop(u32 h_instream);

u16 hpi_instream_reset(u32 h_instream);

u16 hpi_instream_get_info_ex(u32 h_instream, u16 *pw_state, u32 *pbuffer_size,
	u32 *pdata_recorded, u32 *psamples_recorded,
	u32 *pauxiliary_data_recorded);

u16 hpi_instream_ancillary_reset(u32 h_instream, u16 bytes_per_frame,
	u16 mode, u16 alignment, u16 idle_bit);

u16 hpi_instream_ancillary_get_info(u32 h_instream, u32 *pframe_space);

u16 hpi_instream_ancillary_write(u32 h_instream,
	const struct hpi_anc_frame *p_anc_frame_buffer,
	u32 anc_frame_buffer_size_in_bytes,
	u32 number_of_ancillary_frames_to_write);

u16 hpi_instream_host_buffer_allocate(u32 h_instream, u32 size_in_bytes);

u16 hpi_instream_host_buffer_free(u32 h_instream);

u16 hpi_instream_group_add(u32 h_instream, u32 h_stream);

u16 hpi_instream_group_get_map(u32 h_instream, u32 *poutstream_map,
	u32 *pinstream_map);

u16 hpi_instream_group_reset(u32 h_instream);

/*********/
/* Mixer */
/*********/
u16 hpi_mixer_open(u16 adapter_index, u32 *ph_mixer);

u16 hpi_mixer_close(u32 h_mixer);

u16 hpi_mixer_get_control(u32 h_mixer, u16 src_node_type,
	u16 src_node_type_index, u16 dst_node_type, u16 dst_node_type_index,
	u16 control_type, u32 *ph_control);

u16 hpi_mixer_get_control_by_index(u32 h_mixer, u16 control_index,
	u16 *pw_src_node_type, u16 *pw_src_node_index, u16 *pw_dst_node_type,
	u16 *pw_dst_node_index, u16 *pw_control_type, u32 *ph_control);

u16 hpi_mixer_store(u32 h_mixer, enum HPI_MIXER_STORE_COMMAND command,
	u16 index);
/************/
/* Controls */
/************/
/******************/
/* Volume control */
/******************/
u16 hpi_volume_set_gain(u32 h_control, short an_gain0_01dB[HPI_MAX_CHANNELS]
	);

u16 hpi_volume_get_gain(u32 h_control,
	short an_gain0_01dB_out[HPI_MAX_CHANNELS]
	);

u16 hpi_volume_set_mute(u32 h_control, u32 mute);

u16 hpi_volume_get_mute(u32 h_control, u32 *mute);

#define hpi_volume_get_range hpi_volume_query_range
u16 hpi_volume_query_range(u32 h_control, short *min_gain_01dB,
	short *max_gain_01dB, short *step_gain_01dB);

u16 hpi_volume_query_channels(const u32 h_control, u32 *p_channels);

u16 hpi_volume_auto_fade(u32 h_control,
	short an_stop_gain0_01dB[HPI_MAX_CHANNELS], u32 duration_ms);

u16 hpi_volume_auto_fade_profile(u32 h_control,
	short an_stop_gain0_01dB[HPI_MAX_CHANNELS], u32 duration_ms,
	u16 profile);

u16 hpi_volume_query_auto_fade_profile(const u32 h_control, const u32 i,
	u16 *profile);

/*****************/
/* Level control */
/*****************/
u16 hpi_level_query_range(u32 h_control, short *min_gain_01dB,
	short *max_gain_01dB, short *step_gain_01dB);

u16 hpi_level_set_gain(u32 h_control, short an_gain0_01dB[HPI_MAX_CHANNELS]
	);

u16 hpi_level_get_gain(u32 h_control,
	short an_gain0_01dB_out[HPI_MAX_CHANNELS]
	);

/*****************/
/* Meter control */
/*****************/
u16 hpi_meter_query_channels(const u32 h_meter, u32 *p_channels);

u16 hpi_meter_get_peak(u32 h_control,
	short an_peak0_01dB_out[HPI_MAX_CHANNELS]
	);

u16 hpi_meter_get_rms(u32 h_control, short an_peak0_01dB_out[HPI_MAX_CHANNELS]
	);

u16 hpi_meter_set_peak_ballistics(u32 h_control, u16 attack, u16 decay);

u16 hpi_meter_set_rms_ballistics(u32 h_control, u16 attack, u16 decay);

u16 hpi_meter_get_peak_ballistics(u32 h_control, u16 *attack, u16 *decay);

u16 hpi_meter_get_rms_ballistics(u32 h_control, u16 *attack, u16 *decay);

/************************/
/* ChannelMode control */
/************************/
u16 hpi_channel_mode_query_mode(const u32 h_mode, const u32 index,
	u16 *pw_mode);

u16 hpi_channel_mode_set(u32 h_control, u16 mode);

u16 hpi_channel_mode_get(u32 h_control, u16 *mode);

/*****************/
/* Tuner control */
/*****************/
u16 hpi_tuner_query_band(const u32 h_tuner, const u32 index, u16 *pw_band);

u16 hpi_tuner_set_band(u32 h_control, u16 band);

u16 hpi_tuner_get_band(u32 h_control, u16 *pw_band);

u16 hpi_tuner_query_frequency(const u32 h_tuner, const u32 index,
	const u16 band, u32 *pfreq);

u16 hpi_tuner_set_frequency(u32 h_control, u32 freq_ink_hz);

u16 hpi_tuner_get_frequency(u32 h_control, u32 *pw_freq_ink_hz);

u16 hpi_tuner_get_rf_level(u32 h_control, short *pw_level);

u16 hpi_tuner_get_raw_rf_level(u32 h_control, short *pw_level);

u16 hpi_tuner_query_gain(const u32 h_tuner, const u32 index, u16 *pw_gain);

u16 hpi_tuner_set_gain(u32 h_control, short gain);

u16 hpi_tuner_get_gain(u32 h_control, short *pn_gain);

u16 hpi_tuner_get_status(u32 h_control, u16 *pw_status_mask, u16 *pw_status);

u16 hpi_tuner_set_mode(u32 h_control, u32 mode, u32 value);

u16 hpi_tuner_get_mode(u32 h_control, u32 mode, u32 *pn_value);

u16 hpi_tuner_get_rds(u32 h_control, char *p_rds_data);

u16 hpi_tuner_query_deemphasis(const u32 h_tuner, const u32 index,
	const u16 band, u32 *pdeemphasis);

u16 hpi_tuner_set_deemphasis(u32 h_control, u32 deemphasis);
u16 hpi_tuner_get_deemphasis(u32 h_control, u32 *pdeemphasis);

u16 hpi_tuner_query_program(const u32 h_tuner, u32 *pbitmap_program);

u16 hpi_tuner_set_program(u32 h_control, u32 program);

u16 hpi_tuner_get_program(u32 h_control, u32 *pprogram);

u16 hpi_tuner_get_hd_radio_dsp_version(u32 h_control, char *psz_dsp_version,
	const u32 string_size);

u16 hpi_tuner_get_hd_radio_sdk_version(u32 h_control, char *psz_sdk_version,
	const u32 string_size);

u16 hpi_tuner_get_hd_radio_signal_quality(u32 h_control, u32 *pquality);

u16 hpi_tuner_get_hd_radio_signal_blend(u32 h_control, u32 *pblend);

u16 hpi_tuner_set_hd_radio_signal_blend(u32 h_control, const u32 blend);

/***************/
/* PAD control */
/***************/

u16 hpi_pad_get_channel_name(u32 h_control, char *psz_string,
	const u32 string_length);

u16 hpi_pad_get_artist(u32 h_control, char *psz_string,
	const u32 string_length);

u16 hpi_pad_get_title(u32 h_control, char *psz_string,
	const u32 string_length);

u16 hpi_pad_get_comment(u32 h_control, char *psz_string,
	const u32 string_length);

u16 hpi_pad_get_program_type(u32 h_control, u32 *ppTY);

u16 hpi_pad_get_rdsPI(u32 h_control, u32 *ppI);

u16 hpi_pad_get_program_type_string(u32 h_control, const u32 data_type,
	const u32 pTY, char *psz_string, const u32 string_length);

/****************************/
/* AES/EBU Receiver control */
/****************************/
u16 hpi_aesebu_receiver_query_format(const u32 h_aes_rx, const u32 index,
	u16 *pw_format);

u16 hpi_aesebu_receiver_set_format(u32 h_control, u16 source);

u16 hpi_aesebu_receiver_get_format(u32 h_control, u16 *pw_source);

u16 hpi_aesebu_receiver_get_sample_rate(u32 h_control, u32 *psample_rate);

u16 hpi_aesebu_receiver_get_user_data(u32 h_control, u16 index, u16 *pw_data);

u16 hpi_aesebu_receiver_get_channel_status(u32 h_control, u16 index,
	u16 *pw_data);

u16 hpi_aesebu_receiver_get_error_status(u32 h_control, u16 *pw_error_data);

/*******************************/
/* AES/EBU Transmitter control */
/*******************************/
u16 hpi_aesebu_transmitter_set_sample_rate(u32 h_control, u32 sample_rate);

u16 hpi_aesebu_transmitter_set_user_data(u32 h_control, u16 index, u16 data);

u16 hpi_aesebu_transmitter_set_channel_status(u32 h_control, u16 index,
	u16 data);

u16 hpi_aesebu_transmitter_get_channel_status(u32 h_control, u16 index,
	u16 *pw_data);

u16 hpi_aesebu_transmitter_query_format(const u32 h_aes_tx, const u32 index,
	u16 *pw_format);

u16 hpi_aesebu_transmitter_set_format(u32 h_control, u16 output_format);

u16 hpi_aesebu_transmitter_get_format(u32 h_control, u16 *pw_output_format);

/***********************/
/* Multiplexer control */
/***********************/
u16 hpi_multiplexer_set_source(u32 h_control, u16 source_node_type,
	u16 source_node_index);

u16 hpi_multiplexer_get_source(u32 h_control, u16 *source_node_type,
	u16 *source_node_index);

u16 hpi_multiplexer_query_source(u32 h_control, u16 index,
	u16 *source_node_type, u16 *source_node_index);

/***************/
/* Vox control */
/***************/
u16 hpi_vox_set_threshold(u32 h_control, short an_gain0_01dB);

u16 hpi_vox_get_threshold(u32 h_control, short *an_gain0_01dB);

/*********************/
/* Bitstream control */
/*********************/
u16 hpi_bitstream_set_clock_edge(u32 h_control, u16 edge_type);

u16 hpi_bitstream_set_data_polarity(u32 h_control, u16 polarity);

u16 hpi_bitstream_get_activity(u32 h_control, u16 *pw_clk_activity,
	u16 *pw_data_activity);

/***********************/
/* SampleClock control */
/***********************/

u16 hpi_sample_clock_query_source(const u32 h_clock, const u32 index,
	u16 *pw_source);

u16 hpi_sample_clock_set_source(u32 h_control, u16 source);

u16 hpi_sample_clock_get_source(u32 h_control, u16 *pw_source);

u16 hpi_sample_clock_query_source_index(const u32 h_clock, const u32 index,
	const u32 source, u16 *pw_source_index);

u16 hpi_sample_clock_set_source_index(u32 h_control, u16 source_index);

u16 hpi_sample_clock_get_source_index(u32 h_control, u16 *pw_source_index);

u16 hpi_sample_clock_get_sample_rate(u32 h_control, u32 *psample_rate);

u16 hpi_sample_clock_query_local_rate(const u32 h_clock, const u32 index,
	u32 *psource);

u16 hpi_sample_clock_set_local_rate(u32 h_control, u32 sample_rate);

u16 hpi_sample_clock_get_local_rate(u32 h_control, u32 *psample_rate);

u16 hpi_sample_clock_set_auto(u32 h_control, u32 enable);

u16 hpi_sample_clock_get_auto(u32 h_control, u32 *penable);

u16 hpi_sample_clock_set_local_rate_lock(u32 h_control, u32 lock);

u16 hpi_sample_clock_get_local_rate_lock(u32 h_control, u32 *plock);

/***********************/
/* Microphone control */
/***********************/
u16 hpi_microphone_set_phantom_power(u32 h_control, u16 on_off);

u16 hpi_microphone_get_phantom_power(u32 h_control, u16 *pw_on_off);

/********************************/
/* Parametric Equalizer control */
/********************************/
u16 hpi_parametric_eq_get_info(u32 h_control, u16 *pw_number_of_bands,
	u16 *pw_enabled);

u16 hpi_parametric_eq_set_state(u32 h_control, u16 on_off);

u16 hpi_parametric_eq_set_band(u32 h_control, u16 index, u16 type,
	u32 frequency_hz, short q100, short gain0_01dB);

u16 hpi_parametric_eq_get_band(u32 h_control, u16 index, u16 *pn_type,
	u32 *pfrequency_hz, short *pnQ100, short *pn_gain0_01dB);

u16 hpi_parametric_eq_get_coeffs(u32 h_control, u16 index, short coeffs[5]
	);

/*******************************/
/* Compressor Expander control */
/*******************************/

u16 hpi_compander_set_enable(u32 h_control, u32 on);

u16 hpi_compander_get_enable(u32 h_control, u32 *pon);

u16 hpi_compander_set_makeup_gain(u32 h_control, short makeup_gain0_01dB);

u16 hpi_compander_get_makeup_gain(u32 h_control, short *pn_makeup_gain0_01dB);

u16 hpi_compander_set_attack_time_constant(u32 h_control, u32 index,
	u32 attack);

u16 hpi_compander_get_attack_time_constant(u32 h_control, u32 index,
	u32 *pw_attack);

u16 hpi_compander_set_decay_time_constant(u32 h_control, u32 index,
	u32 decay);

u16 hpi_compander_get_decay_time_constant(u32 h_control, u32 index,
	u32 *pw_decay);

u16 hpi_compander_set_threshold(u32 h_control, u32 index,
	short threshold0_01dB);

u16 hpi_compander_get_threshold(u32 h_control, u32 index,
	short *pn_threshold0_01dB);

u16 hpi_compander_set_ratio(u32 h_control, u32 index, u32 ratio100);

u16 hpi_compander_get_ratio(u32 h_control, u32 index, u32 *pw_ratio100);

/********************/
/* Cobranet control */
/********************/
u16 hpi_cobranet_hmi_write(u32 h_control, u32 hmi_address, u32 byte_count,
	u8 *pb_data);

u16 hpi_cobranet_hmi_read(u32 h_control, u32 hmi_address, u32 max_byte_count,
	u32 *pbyte_count, u8 *pb_data);

u16 hpi_cobranet_hmi_get_status(u32 h_control, u32 *pstatus,
	u32 *preadable_size, u32 *pwriteable_size);

u16 hpi_cobranet_get_ip_address(u32 h_control, u32 *pdw_ip_address);

u16 hpi_cobranet_set_ip_address(u32 h_control, u32 dw_ip_address);

u16 hpi_cobranet_get_static_ip_address(u32 h_control, u32 *pdw_ip_address);

u16 hpi_cobranet_set_static_ip_address(u32 h_control, u32 dw_ip_address);

u16 hpi_cobranet_get_macaddress(u32 h_control, u32 *p_mac_msbs,
	u32 *p_mac_lsbs);

/*************************/
/* Tone Detector control */
/*************************/
u16 hpi_tone_detector_get_state(u32 hC, u32 *state);

u16 hpi_tone_detector_set_enable(u32 hC, u32 enable);

u16 hpi_tone_detector_get_enable(u32 hC, u32 *enable);

u16 hpi_tone_detector_set_event_enable(u32 hC, u32 event_enable);

u16 hpi_tone_detector_get_event_enable(u32 hC, u32 *event_enable);

u16 hpi_tone_detector_set_threshold(u32 hC, int threshold);

u16 hpi_tone_detector_get_threshold(u32 hC, int *threshold);

u16 hpi_tone_detector_get_frequency(u32 hC, u32 index, u32 *frequency);

/****************************/
/* Silence Detector control */
/****************************/
u16 hpi_silence_detector_get_state(u32 hC, u32 *state);

u16 hpi_silence_detector_set_enable(u32 hC, u32 enable);

u16 hpi_silence_detector_get_enable(u32 hC, u32 *enable);

u16 hpi_silence_detector_set_event_enable(u32 hC, u32 event_enable);

u16 hpi_silence_detector_get_event_enable(u32 hC, u32 *event_enable);

u16 hpi_silence_detector_set_delay(u32 hC, u32 delay);

u16 hpi_silence_detector_get_delay(u32 hC, u32 *delay);

u16 hpi_silence_detector_set_threshold(u32 hC, int threshold);

u16 hpi_silence_detector_get_threshold(u32 hC, int *threshold);
/*********************/
/* Utility functions */
/*********************/

u16 hpi_format_create(struct hpi_format *p_format, u16 channels, u16 format,
	u32 sample_rate, u32 bit_rate, u32 attributes);

#endif	 /*_HPI_H_ */
