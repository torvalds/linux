/*          
 *  Cx2070x ASoc codec driver.   
 *
 * Copyright:   (C) 2010/2011 Conexant Systems
 *
 * Based on sound/soc/codecs/tlv320aic2x.c by Vladimir Barinov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The software is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY 
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License 
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along with 
 * this software.  If not, see <http//www.gnu.org/licenses/>
 *
 * All copies of the software must include all unaltered copyright notices, 
 * disclaimers of warranty, all notices that refer to the General Public License 
 * and the absence of any warranty.
 *
 *  History
 *  Added support for CX2070x codec [www.conexant.com]
*/


// force to enable TX/RX on 2nd PCM interface.

#ifndef _PASS1_COMPLETE_
# ifdef CONFIG_SND_DIGICOLOR_SOC_CHANNEL_VER_3_13F
# endif
# ifdef CONFIG_SND_DIGICOLOR_SOC_CHANNEL_VER_4_30F
# endif
#endif

  __REG(PLAYBACK_REGISTER, 0xffff, 0xffff, 0x00, 0, DM, B)
  __REG(CAPTURE_REGISTER, 0xffff, 0xffff, 0x00, 0, DM, B)
  __REG(MICBIAS_REGISTER, 0xffff, 0xffff, 0x00, 0, DM, B)

/////////////////////////////////////////////////////////////////////////
//  General codec operations registers
/////////////////////////////////////////////////////////////////////////
//      id					addr	data     bias type
  __REG(ABORT_CODE,			0x1000,	0x1000, 0x00,       0, RO,B)
  __REG(FIRMWARE_VERSION,		0x1001,	0x1001, 0x00,       0, RO,W)
  __REG(PATCH_VERSION,			0x1003,	0x1003, 0x00,       0, RO,W)
  __REG(CHIP_VERSION,			0x1005,	0x1005, 0x00,       0, RO,B)
  __REG(RELEASE_TYPE,			0x1006,	0x1006, 0x00,       0, RO,B)
  __REG(USB_LOCAL_VOLUME,       0x004E, 0x004E, 0x42,       0, RW,B)
  __REG(ROM_PATCH_VER_HB,		0x1584,	0xFFFF, 0x00,       0, RO,B)
  __REG(ROM_PATCH_VER_MB,		0x1585,	0xFFFF, 0x00,       0, RO,B)
  __REG(ROM_PATCH_VER_LB,		0x1586, 0xFFFF, 0x00,       0, RO,B)
  
  __REG(DAC1_GAIN_LEFT,			0x100D,	0x100D, 0x00,    0x00, RW,B)
  __REG(DAC2_GAIN_RIGHT,		0x100E,	0x100E, 0x00,    0x00, RW,B)
  __REG(DSP_MAX_VOLUME,			0x100F,	0x100F, 0x00,       0, RW,B)

  __REG(CLASS_D_GAIN,			0x1011,	0x1010, b_00000000, 0, RW,B)
#ifndef _PASS1_COMPLETE_
#define CLASS_D_GAIN_2W8				b_00000000	// 2.8W
#define CLASS_D_GAIN_2W6				b_00000001	// 2.6W
#define CLASS_D_GAIN_2W5				b_00000010	// 2.5W
#define CLASS_D_GAIN_2W4				b_00000011	// 2.4W
#define CLASS_D_GAIN_2W3				b_00000100	// 2.3W
#define CLASS_D_GAIN_2W2				b_00000101	// 2.2W
#define CLASS_D_GAIN_2W1				b_00000110	// 2.1W
#define CLASS_D_GAIN_2W0				b_00000111	// 2.0W
#define CLASS_D_GAIN_1W3				b_00001000	// 1.3W
#define CLASS_D_GAIN_1W25				b_00001001	// 1.25W
#define CLASS_D_GAIN_1W2				b_00001010	// 1.2W
#define CLASS_D_GAIN_1W15				b_00001011	// 1.15W
#define CLASS_D_GAIN_1W1				b_00001100	// 1.1W
#define CLASS_D_GAIN_1W05				b_00001101	// 1.05W
#define CLASS_D_GAIN_1W0				b_00001110	// 1.0W
#define CLASS_D_GAIN_0W9				b_00001111	// 0.9W
#endif

  __REG(DAC3_GAIN_SUB,			0x1012,	0x1011, 0x00,    0x4A, RW,B)

  __REG(ADC1_GAIN_LEFT,			0x1013,	0x1012,	0x00,    0x4A, RW,B)
  __REG(ADC1_GAIN_RIGHT,		0x1014,	0x1013,	0x00,    0x4A, RW,B)
  __REG(ADC2_GAIN_LEFT,			0x1015,	0x1014,	0x00,    0x00, RW,B)
  __REG(ADC2_GAIN_RIGHT,		0x1016,	0x1015,	0x00,    0x00, RW,B)
  __REG(DSP_MAX_MIC_GAIN,		0x1017,	0x1016,	0x00,       0, RW,B)

  __REG(VOLUME_MUTE,			0x1018,	0x1017,	0, 0, WI,B)
#ifndef _PASS1_COMPLETE_
#define LEFT_AUX_MUTE					b_01000000
#define RIGH_AUX_MUTE					b_00100000
#define LEFT_MIC_MUTE					b_00010000
#define RIGH_MIC_MUTE					b_00001000
#define SUB_SPEAKER_MUTE				b_00000100
#define LEFT_SPEAKER_MUTE				b_00000010
#define RIGH_SPEAKER_MUTE				b_00000001
#define VOLUME_MUTE_ALL					b_01111111
#endif

//since the playback path is determined in register value,we have to enable one port. 
  __REG(OUTPUT_CONTROL,			0x1019,	0x1018, b_10000000, 0, WC,B)  //class -d is selected by default. 
#ifndef _PASS1_COMPLETE_
#define OUT_CTRL_AUTO					b_10000000	// Automatic FW Control base on Jack Sense and DAC enables, 1= Auto, 0= Manual
#define OUT_CTRL_SUB_DIFF				b_01000000	// Sub Differential control, 1=Differential, 0=Single Ended
#define OUT_CTRL_LO_DIFF				b_00100000	// Line Out Differential control, 1=Differential, 0=Single Ended
#define OUT_CTRL_CLASSD_OUT				b_00010000	// ClassD Output, 1=PWM, 0=Speakers
#define OUT_CTRL_CLASSD_MONO			 b_00001000	// ClassD Mono, 1=Mono, 0=Stereo
#define OUT_CTRL_CLASSD_EN				b_00000100	// If OutCTL[7]=0, 1=Enable ClassD Speakers, 0=Disable ClassD Speakers
#define OUT_CTRL_LO_EN					b_00000010	// If OutCTL[7]=0, 1=Enable Line Out, 0=Disable Line Out
#define OUT_CTRL_HP_EN					b_00000001	// If OutCTL[7]=0, 1=Enable Headphone, 0=Disable Headphone
#endif

  __REG(INPUT_CONTROL,			0x101A,	0x1019, b_10000000, 0, WI,B)
#ifndef _PASS1_COMPLETE_
#define IN_CTRL_AUTO					b_10000000	// Automatic FW Control base on Jack Sense and ADC enables, 1=Auto, 0=Manual
#define IN_CTRL_L1_DIFF					b_00001000	// Line In 1 Differential control, 1=Differential, 0=Single Ended
#define IN_CTRL_L3_EN					b_00000100	// If LineCTL[7]=0, 1=Enable Line In 3, 0=Disable Line In 3
#define IN_CTRL_L2_EN					b_00000010	// If LineCTL[7]=0, 1=Enable Line In 2, 0=Disable Line In 2
#define IN_CTRL_L1_EN					b_00000001	// If LineCTL[7]=0, 1=Enable Line In 1, 0=Disable Line In 1
#endif

  __REG(LINE1_GAIN,			0x101B,	0x101A, b_00000000, 0, RW,B)
#ifndef _PASS1_COMPLETE_
#define LINE1_MUTE					b_10000000	// 1=mute, 0=unmute
#define LINE1_GAIN_MASK					b_00011111	// range 0x00-0x1F (-35.5dB to +12dB)
#endif

  __REG(LINE2_GAIN,			0x101C,	0x101B, b_00000000, 0, RW,B)
#ifndef _PASS1_COMPLETE_
#define LINE2_MUTE					b_10000000	// 1=mute, 0=unmute
#define LINE2_GAIN_MASK					b_00011111	// range 0x00-0x1F (-35.5dB to +12dB)
#endif

  __REG(LINE3_GAIN,			0x101D,	0x101C, b_00000000, 0, RW,B)
#ifndef _PASS1_COMPLETE_
#define LINE3_MUTE					b_10000000	// 1=mute, 0=unmute
#define LINE3_GAIN_MASK					b_00011111	// range 0x00-0x1F (-35.5dB to +12dB)
#endif

  __REG(MIC_CONTROL,			0x101E,	0x101D,	b_00000110, 0, WC,B)
#ifndef _PASS1_COMPLETE_
#define MICROPHONE_POWER_ALWAYS				b_00010000	// 1 = leave microphone and bias always on to avoid pops (burns power), 0 = microphone powered up as needed, mute for 400ms to remove pops
#define	MICROPHONE_BIAS SELECT				b_00001000	// 1= 80%, 0= 50%
#define	MICROPHONE_BOOST_MASK				b_00000111	// 2:0 MicCTL [2:0]	Microphone Boost in 6dB Steps, 0= 0dB, 7= +42dB
#endif

#if defined(CONFIG_SND_DIGICOLOR_SOC_CHANNEL_VER_3_13E)
// adc
  __REG(STREAM1_MIX,			0xffff,	0x101E,	b_00000000, 0, WI,B)
#ifndef _PASS1_COMPLETE_
#define STREAM1_MUTE					b_10000000	// 1=mute, 0=unmute
#define STREAM1_GAIN_MASK				b_00011111	// range 0x00-0x4A (0dB to -74dB)
#endif
// i2s
  __REG(STREAM3_MIX,			0xffff,	0x101F,	b_00000000, 0, WI,B)
#ifndef _PASS1_COMPLETE_
#define STREAM3_MUTE					b_10000000	// 1=mute, 0=unmute
#define STREAM3_GAIN_MASK				b_00011111	// range 0x00-0x4A (0dB to -74dB)
#endif
// usb?
  __REG(STREAM4_MIX,			0xffff,	0x1020,	b_00000000, 0, WI,B)
#ifndef _PASS1_COMPLETE_
#define STREAM4_MUTE					b_10000000	// 1=mute, 0=unmute
#define STREAM4_GAIN_MASK				b_00011111	// range 0x00-0x4A (0dB to -74dB)
#endif
#endif
#if defined(CONFIG_SND_DIGICOLOR_SOC_CHANNEL_VER_4_30F)
  __REG(MIX0_INPUT0,			0x101F, 0xffff, b_00000000, 0, WI,B)	// stream1 out
  __REG(MIX0_INPUT1,			0x1020, 0xffff, b_00000000, 0, WI,B)	// stream3 out
  __REG(MIX0_INPUT2,			0x1021, 0xffff, b_00000000, 0, WI,B)	// stream4 out
  __REG(MIX0_INPUT3,			0x1022, 0xffff, b_10000000, 0, WI,B)	// none
  __REG(MIX1_INPUT0,			0x1023, 0xffff, b_10000000, 0, WI,B)	// none
  __REG(MIX1_INPUT1,			0x1024, 0xffff, b_10000000, 0, WI,B)	// none
  __REG(MIX1_INPUT2,			0x1025, 0xffff, b_10000000, 0, WI,B)	// none
  __REG(MIX1_INPUT3,			0x1026, 0xffff, b_10000000, 0, WI,B)	// nonw
  __REG(MIX0_SOURCE0,			0x1184, 0xffff, b_00000000, 0, WI,B)	// stream1 out
  __REG(MIX0_SOURCE1,			0x1185, 0xffff, b_00000011, 0, WI,B)	// stream3 out
  __REG(MIX0_SOURCE2,			0x1186, 0xffff, b_00000100, 0, WI,B)	// stream4 out
  __REG(MIX0_SOURCE3,			0x1187, 0xffff, b_00000000, 0, WI,B)	// none
  __REG(MIX1_SOURCE0,			0x1188, 0xffff, b_00000001, 0, WI,B)	// none
  __REG(MIX1_SOURCE1,			0x1189, 0xffff, b_00000000, 0, WI,B)	// none
  __REG(MIX1_SOURCE2,			0x118a, 0xffff, b_00000000, 0, WI,B)	// none
  __REG(MIX1_SOURCE3,			0x118b, 0xffff, b_00000000, 0, WI,B)	// none
  __REG(VOICE_IN_SOURCE,		0x118c, 0xffff, b_00000010, 0, WI,B)	// stream2
  //__REG(VOICE_IN_SOURCE,		0x118c, 0xffff, 0x04, 0, WI,B)	// stream2
#endif

/////////////////////////////////////////////////////////////////////////
// Hardware registers
/////////////////////////////////////////////////////////////////////////
//      id					addr	data     bias type
//  __REG(CLOCK_DIVIDER,			0x0F50,	0x0F50,	b_00001111, 0, WI,B)  // Port1 external clock enabled
    __REG(CLOCK_DIVIDER,			0x0F50,	0x0F50,	0xFF, 0, WI,B)  // Port1 slave, Port2 Master 2.048 MHz
#ifndef _PASS1_COMPLETE_
#define PORT2_DIV_SEL_6_144MHz				b_00000000	// 0x0 = 6.144 MHz
#define PORT2_DIV_SEL_4_096MHz				b_00010000	// 0x1 = 4.096 MHz
#define PORT2_DIV_SEL_3_072MHz				b_00100000	// 0x2 = 3.072 MHz
#define PORT2_DIV_SEL_2_048MHz				b_00110000	// 0x3 = 2.048 MHz
#define PORT2_DIV_SEL_1_536MHz				b_01000000	// 0x4 = 1.536 MHz
#define PORT2_DIV_SEL_1_024MHz				b_01010000	// 0x5 = 1.024 MHz
#define PORT2_DIV_SEL_768kHz				b_01100000	// 0x6 = 768kHz
#define PORT2_DIV_SEL_512kHz				b_01110000	// 0x7 = 512 kHz
#define PORT2_DIV_SEL_384kHz				b_10000000	// 0x8 = 384 kHz
#define PORT2_DIV_SEL_256kHz				b_10010000	// 0x9 = 256 kHz
#define PORT2_DIV_SEL_5_644MHz				b_10100000	// 0xa = 5.644 MHz
#define PORT2_DIV_SEL_2_822MHz				b_10110000	// 0xb = 2.822 MHz
#define PORT2_DIV_SEL_1_411MHz				b_11000000	// 0xc = 1.411 MHz
#define PORT2_DIV_SEL_705kHz				b_11010000	// 0xd = 705 kHz
#define PORT2_DIV_SEL_352kHz				b_11100000	// 0xe = 352 kHz
#define PORT2_DIV_SEL_EXT				b_11110000	// 0xf = external clock enabled
#define PORT1_DIV_SEL_6_144MHz				b_00000000	// 0x0 = 6.144 MHz
#define PORT1_DIV_SEL_4_096MHz				b_00000001	// 0x1 = 4.096 MHz
#define PORT1_DIV_SEL_3_072MHz				b_00000010	// 0x2 = 3.072 MHz
#define PORT1_DIV_SEL_2_048MHz				b_00000011	// 0x3 = 2.048 MHz
#define PORT1_DIV_SEL_1_536MHz				b_00000100	// 0x4 = 1.536 MHz
#define PORT1_DIV_SEL_1_024MHz				b_00000101	// 0x5 = 1.024 MHz
#define PORT1_DIV_SEL_768kHz				b_00000110	// 0x6 = 768kHz
#define PORT1_DIV_SEL_512kHz				b_00000111	// 0x7 = 512 kHz
#define PORT1_DIV_SEL_384kHz				b_00001000	// 0x8 = 384 kHz
#define PORT1_DIV_SEL_256kHz				b_00001001	// 0x9 = 256 kHz
#define PORT1_DIV_SEL_5_644MHz				b_00001010	// 0xa = 5.644 MHz
#define PORT1_DIV_SEL_2_822MHz				b_00001011	// 0xb = 2.822 MHz
#define PORT1_DIV_SEL_1_411MHz				b_00001100	// 0xc = 1.411 MHz
#define PORT1_DIV_SEL_705kHz				b_00001101	// 0xd = 705 kHz
#define PORT1_DIV_SEL_352kHz				b_00001110	// 0xe = 352 kHz
#define PORT1_DIV_SEL_EXT				b_00001111	// 0xf = external clock enabled
#endif

  __REG(PORT1_CONTROL,			0x0F51,	0x0F51,	b_10110000, 0, WI,B)
#ifndef _PASS1_COMPLETE_
#define PORT1_DELAY					b_10000000	// 1=Data delayed 1 bit (I2S standard), 0=no delay (sony mode)
#define PORT1_JUSTR_LSBF			b_01000000	// [1/0]=Right/Left Justify (I2S) or LSB/MSB First (PCM)
#define PORT1_RX_EN					b_00100000	// 1=RX Clock Enable, 0=RX Clock Disabled
#define PORT1_TX_EN					b_00010000	// 1=TX Clock Enable, 0=TX Clock Disabled
//#define PORT1_					b_00001000	//
#define PORT1_BITCLK_POL				b_00000100	// 0=Normal clock, 1=Inverted clock
#define PORT1_WS_POL					b_00000010	// 0=Rising Edge Active for Word Strobe, 1=Falling Edge Active for Word Strobe
#define PORT1_PCM_MODE					b_00000001	// 0=I2S mode, 1=PCM Mode
#endif

  __REG(PORT1_TX_CLOCKS_PER_FRAME_PHASE,0x0F52,	0x0F52,	b_00000011, 0, WI,B)  // clocks/frame=(N+1)*8
  __REG(PORT1_RX_CLOCKS_PER_FRAME_PHASE,0x0F53,	0x0F53,	b_00000011, 0, WI,B)  // clocks/frame=(N+1)*8
  __REG(PORT1_TX_SYNC_WIDTH,		0x0F54,	0x0F54,	b_00001111, 0, WI,B)  // clocks=(N+1)
  __REG(PORT1_RX_SYNC_WIDTH,		0x0F55,	0x0F55,	b_00001111, 0, WI,B)  // clocks=(N+1)

  __REG(PORT1_CONTROL_2,		0x0F56,	0x0F56,	b_00000101, 0, WI,B)
#ifndef _PASS1_COMPLETE_
#define PORT1_CTRL_TX_PT				b_00100000	// Tx passthrough mode,  0=off, 1=on
#define PORT1_CTRL_RX_PT				b_00010000	// Rx passthrough mode,  0=off, 1=on
#define PORT1_CTRL_RX_SIZE_8				b_00000000	// RX Sample Size, 00=8 bits
#define PORT1_CTRL_RX_SIZE_16				b_00000100	// RX Sample Size, 01=16 bit
#define PORT1_CTRL_RX_SIZE_24T				b_00001000	// RX Sample Size, 10=24 bit truncated to 16 bits
#define PORT1_CTRL_RX_SIZE_24				b_00001100	// RX Sample Size, 11=24 bit
#define PORT1_CTRL_TX_SIZE_8				b_00000000	// TX Sample Size, 00=8 bits
#define PORT1_CTRL_TX_SIZE_16				b_00000001	// TX Sample Size, 01=16 bit
#define PORT1_CTRL_TX_SIZE_24T				b_00000010	// TX Sample Size, 10=24 bit truncated to 16 bits
#define PORT1_CTRL_TX_SIZE_24				b_00000011	// TX Sample Size, 11=24 bit
#endif

/////////////////////////////////////////////////////////////////////////
// Codec registers, most need NEWC to be set
/////////////////////////////////////////////////////////////////////////
//      id					addr	data     bias type
  __REG(STREAM2_RATE,			0x116b, 0xffff, 0xa2,       0, WI,B) // Mic
#ifndef _PASS1_COMPLETE_
#define STREAM2_STREAM_MONO_LEFT			0x00		// 
#define STREAM2_STREAM_MONO_RIGHT			0x40		// 
#define STREAM2_STREAM_STEREO				0x80		// 
#define STREAM2_SAMPLE_A_LAW				0x00		// 8-bit A law
#define STREAM2_SAMPLE_U_LAW				0x10		// 8-bit µ law
#define STREAM2_SAMPLE_16_LIN				0x20		// 16 bit linear
#define STREAM2_SAMPLE_24_LIN				0x30		// 24 bit linear
#define STREAM2_RATE_8000   				0x00		//  8000 samples/sec
#define STREAM2_RATE_11025  				0x01		// 11025 samples/sec
#define STREAM2_RATE_16000  				0x02		// 16000 samples/sec
#define STREAM2_RATE_22050  				0x03		// 22050 samples/sec
#define STREAM2_RATE_24000  				0x04		// 24000 samples/sec
#define STREAM2_RATE_32000  				0x05		// 32000 samples/sec
#define STREAM2_RATE_44100  				0x06		// 44100 samples/sec
#define STREAM2_RATE_48000  				0x07		// 48000 samples/sec
#define STREAM2_RATE_88200  				0x08		// 88200 samples/sec
#define STREAM2_RATE_96000				0x09		// 96000 samples/sec
#endif

  __REG(STREAM5_RATE,			0x1171,	0x112D, 0x26,       0, WI,B) // Mic -> I2S (5 wire)
#ifndef _PASS1_COMPLETE_
#define STREAM5_SAMPLE_A_LAW				0x00		// 8-bit A law
#define STREAM5_SAMPLE_U_LAW				0x10		// 8-bit µ law
#define STREAM5_SAMPLE_16_LIN				0x20		// 16 bit linear
#define STREAM5_SAMPLE_24_LIN				0x30		// 24 bit linear
#define STREAM5_RATE_8000   				0x00		//  8000 samples/sec
#define STREAM5_RATE_11025  				0x01		// 11025 samples/sec
#define STREAM5_RATE_16000  				0x02		// 16000 samples/sec
#define STREAM5_RATE_22050  				0x03		// 22050 samples/sec
#define STREAM5_RATE_24000  				0x04		// 24000 samples/sec
#define STREAM5_RATE_32000  				0x05		// 32000 samples/sec
#define STREAM5_RATE_44100  				0x06		// 44100 samples/sec
#define STREAM5_RATE_48000  				0x07		// 48000 samples/sec
#define STREAM5_RATE_88200  				0x08		// 88200 samples/sec
#define STREAM5_RATE_96000				0x09		// 96000 samples/sec
#endif

  __REG(STREAM3_RATE,			0x116D,	0x112F,	0xA6,       0, WI,B) // 44.1kHz, 16 bit linear
#ifndef _PASS1_COMPLETE_
#define STREAM3_STREAM_MONO_LEFT			0x00		// 
#define STREAM3_STREAM_MONO_RIGHT			0x40		// 
#define STREAM3_STREAM_STEREO				0x80		// 
#define STREAM3_SAMPLE_A_LAW				0x00		// 8-bit A law
#define STREAM3_SAMPLE_U_LAW				0x10		// 8-bit µ law
#define STREAM3_SAMPLE_16_LIN				0x20		// 16 bit linear
#define STREAM3_SAMPLE_24_LIN				0x30		// 24 bit linear
#define STREAM3_RATE_8000   				0x00		//  8000 samples/sec
#define STREAM3_RATE_11025  				0x01		// 11025 samples/sec
#define STREAM3_RATE_16000  				0x02		// 16000 samples/sec
#define STREAM3_RATE_22050  				0x03		// 22050 samples/sec
#define STREAM3_RATE_24000  				0x04		// 24000 samples/sec
#define STREAM3_RATE_32000  				0x05		// 32000 samples/sec
#define STREAM3_RATE_44100  				0x06		// 44100 samples/sec
#define STREAM3_RATE_48000  				0x07		// 48000 samples/sec
#define STREAM3_RATE_88200  				0x08		// 88200 samples/sec
#define STREAM3_RATE_96000				0x09		// 96000 samples/sec
#endif

  __REG(STREAM_3_ROUTING,		0x116E,	0x1130,	0x02,       0, WI,B)
#ifndef _PASS1_COMPLETE_
#define STREAM3_ROUTE_SRC_D1				0x00	      // Source = Digital Port1
#define STREAM3_ROUTE_SRC_D2				0x10	      // Source = Digital Port2
#define STREAM3_ROUTE_DST_D1				0x00	      // Destination = Digital Port1
#define STREAM3_ROUTE_DST_D2    			0x01	      // Destination = Digital Port2
#define STREAM3_ROUTE_DST_DAC				0x02	      // Destination = DAC
#define STREAM3_ROUTE_DST_SUB				0x03	      // Destination = DAC (sub)
#define STREAM3_ROUTE_DST_SPDIF				0x04	      // Destination = SPDIF
#define STREAM3_ROUTE_DST_USB				0x05	      // Destination = USB
#endif

  __REG(EQ_GAIN,			0x10D7,	0x10D1, 0x1000,     0, WI,W)
  __REG(EQS_GAIN,			0x10D9,	0x10D3, 0x1000,     0, WI,W)

//  __REG(SPDIF_CODE,			0x1178,	0x1134,	0x00,       0, WI,B)
//  __REG(SPDIF_CONTROL,		0x1179,	0x1135,	0x00,       0, WI,B)

  __REG(DSP_PROCESSING_ENABLE_1,	0x117A,	0x1136,	0x00,       0, WC,B)
#ifndef _PASS1_COMPLETE_
#define	RIGHT_MIKE					b_01000000
#define	IN_NOISE_REDUCTION				b_00100000
#define	MIC_AGC						b_00010000
#define	BEAM_FORMING					b_00001000
#define	NOICE_REDUCTION					b_00000100
#define	LEC						b_00000010
#define	AEC						b_00000001
#endif
  __REG(DSP_PROCESSING_ENABLE_2,	0x117B,	0x1137,	0x00,       0, WC,B)
#ifndef _PASS1_COMPLETE_
#define	DSP_MONO_OUTPUT					b_00100000	// 0=Stereo, 1=Mono (L+R)=L (L+R)=R
#define	LOUDNESS_ADAPTER				b_00010000	// 1=Enable, 0=Disable
#define	STAGE_ENHANCER					b_00001000	// 1=Enable 3D processing, 0=Disable
#define	DYNAMIC_RANGE_COMPRESSION			b_00000100	// 1=Enable, 0=Disable
#define	SUBWOOFER_CROSSOVER				b_00000010	// 1=Enable, 0=Disable
#define	EQUALIZER_10_BAND				b_00000001	// 1=Enable, 0=Disable
#endif

#if defined(CONFIG_SND_DIGICOLOR_SOC_CHANNEL_VER_4_30F)
  __REG(DSP_INIT_H,			0x117C,	0xffff,	0x00,       0, WI,B) // special
  __REG(DSP_INIT,			0x117D,	0xffff,	0x00,       0, WC,B) // special
  __REG(DSP_POWER,			0x117E,	0xffff,	0xE0,       0, WC,B) // special

#ifndef _PASS1_COMPLETE_
#define DSP_INIT_NEWC					b_00000001
#define DSP_INIT_STREAM_OFF				b_00000001
#define DSP_INIT_STREAM_3				b_10001001	// enable stream 3 and 7
#define DSP_INIT_STREAM_5				b_00100101	// enable stream 2 and 5
#define DSP_INIT_STREAM_5_3				b_10101101	// enable streams 2,3,5,7
#define DSP_NO_SOURCE					b_00000000
#define DSP_ENABLE_STREAM_3             b_00001000
#define DSP_ENABLE_STREAM_4             b_00010000
#define DSP_ENABLE_STREAM_3_4           b_00011000
#endif
#else
  __REG(DSP_INIT,			0xffff, 0x1138,	0x00,       0, WI,B) // special
#ifndef _PASS1_COMPLETE_000000000000000000000000
#define DSP_INIT_NEWC					b_00000001
#define DSP_INIT_STREAM_OFF				b_00000001
#define DSP_INIT_STREAM_3				b_00001001
#define DSP_INIT_STREAM_5				b_00100001
#define DSP_INIT_STREAM_5_3				b_00101001
#endif
#endif

#ifdef CX20709_TRISTATE_EEPROM
  __REG(PAD,				0x0004,	0x0004,	0x00,       0, WO,B)
  __REG(PBD,				0x0005,	0x0005,	0x00,       0, W0,B)
#endif
// temp code.
//  added rerouting render stream to second I2S output ( 16 Kbps/ 16BITS)
  __REG(STREAM4_RATE,			0x116F,	0xffff, 0xA7,       0, WI,B) // dsp -> PCM-2 8Kbps output
  __REG(STREAM4_ROUTING,		0x1170,	0xffff,	0x12,       0, WI,B)
  __REG(STREAM6_RATE,			0x1172,	0xffff, 0x27,       0, WI,B) // dsp -> PCM-2 8Kbps output
  __REG(STREAM7_RATE,			0x1173, 0xffff, 0x07,       0, WC,B) // dsp -> I2S-2 (5 wire)
  __REG(STREAMOP_ROUTING,		0x1176,	0xffff,	0x60,       0, WC,B) // AEC narrow band. 48 KHz
  __REG(STREAM6_ROUTING,		0x1182,	0xffff,	0x06,       0, WI,B)
  __REG(STREAM7_SOURCE,			0x117F, 0xffff, 0x05,       0, WI,B) // dsp -> I2S-2 (5 wire)
  __REG(STREAM8_SOURCE,         0x1180, 0xffff, 0x05,        0, WC,B)
  __REG(STREAM8_RATE,			0x1175, 0xffff, 0x07,       0, WC,B) 
  __REG(PORT2_CONTROL,      		0x0F5E,	0x0F5E,	0XB0,        0, WI,B)  // Delay 1 bit, RX/TX en, mode =i2s
  __REG(PORT2_CLOCK_PER_FRAME,     	0x0F5F,	0x0F5F,	0X07,        0, WI,B)  // 64-bits per frame.
  __REG(PORT2_SYNC_WIDTH,      		0x0F60,	0x0F60,	0X0f,        0, WI,B)  // clocks=(N+1)
  __REG(PORT2_SAMPLE_WIDTH,        	0x0F61,	0x0F61,	0X01,        0, WI,B)  // 16 bits.
  __REG(PORT2_RX_STREAM1,        	0x0F62,	0x0F62,	0X20,        0, WI,B)  // RX 1 <- Slot 0
  __REG(PORT2_RX_STREAM2,        	0x0F63,	0x0F63,	0X24,        0, WI,B)  // RX 2 <- Slot 4
  __REG(PORT2_TX_STREAM1,        	0x0F65,	0x0F65,	0X20,        0, WI,B)  // TX 1 -> Slot 0
  __REG(PORT2_TX_STREAM2,        	0x0F66,	0x0F66,	0X24,        0, WI,B)  // TX 2 -> Slot 4

#ifndef _PASS1_COMPLETE_
#define _PASS1_COMPLETE_
#endif
