/*
 *  Copyright (c) 2004 James Courtier-Dutton <James@superbug.demon.co.uk>
 *  Driver CA0106 chips. e.g. Sound Blaster Audigy LS and Live 24bit
 *  Version: 0.0.21
 *
 *  FEATURES currently supported:
 *    See ca0106_main.c for features.
 * 
 *  Changelog:
 *    Support interrupts per period.
 *    Removed noise from Center/LFE channel when in Analog mode.
 *    Rename and remove mixer controls.
 *  0.0.6
 *    Use separate card based DMA buffer for periods table list.
 *  0.0.7
 *    Change remove and rename ctrls into lists.
 *  0.0.8
 *    Try to fix capture sources.
 *  0.0.9
 *    Fix AC3 output.
 *    Enable S32_LE format support.
 *  0.0.10
 *    Enable playback 48000 and 96000 rates. (Rates other that these do not work, even with "plug:front".)
 *  0.0.11
 *    Add Model name recognition.
 *  0.0.12
 *    Correct interrupt timing. interrupt at end of period, instead of in the middle of a playback period.
 *    Remove redundent "voice" handling.
 *  0.0.13
 *    Single trigger call for multi channels.
 *  0.0.14
 *    Set limits based on what the sound card hardware can do.
 *    playback periods_min=2, periods_max=8
 *    capture hw constraints require period_size = n * 64 bytes.
 *    playback hw constraints require period_size = n * 64 bytes.
 *  0.0.15
 *    Separated ca0106.c into separate functional .c files.
 *  0.0.16
 *    Implement 192000 sample rate.
 *  0.0.17
 *    Add support for SB0410 and SB0413.
 *  0.0.18
 *    Modified Copyright message.
 *  0.0.19
 *    Added I2C and SPI registers. Filled in interrupt enable.
 *  0.0.20
 *    Added GPIO info for SB Live 24bit.
 *  0.0.21
 *   Implement support for Line-in capture on SB Live 24bit.
 *
 *
 *  This code was initally based on code from ALSA's emu10k1x.c which is:
 *  Copyright (c) by Francisco Moraes <fmoraes@nc.rr.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

/************************************************************************************************/
/* PCI function 0 registers, address = <val> + PCIBASE0						*/
/************************************************************************************************/

#define PTR			0x00		/* Indexed register set pointer register	*/
						/* NOTE: The CHANNELNUM and ADDRESS words can	*/
						/* be modified independently of each other.	*/
						/* CNL[1:0], ADDR[27:16]                        */

#define DATA			0x04		/* Indexed register set data register		*/
						/* DATA[31:0]					*/

#define IPR			0x08		/* Global interrupt pending register		*/
						/* Clear pending interrupts by writing a 1 to	*/
						/* the relevant bits and zero to the other bits	*/
#define IPR_MIDI_RX_B		0x00020000	/* MIDI UART-B Receive buffer non-empty		*/
#define IPR_MIDI_TX_B		0x00010000	/* MIDI UART-B Transmit buffer empty		*/
#define IPR_SPDIF_IN_USER	0x00004000      /* SPDIF input user data has 16 more bits	*/
#define IPR_SPDIF_OUT_USER	0x00002000      /* SPDIF output user data needs 16 more bits	*/
#define IPR_SPDIF_OUT_FRAME	0x00001000      /* SPDIF frame about to start			*/
#define IPR_SPI			0x00000800      /* SPI transaction completed			*/
#define IPR_I2C_EEPROM		0x00000400      /* I2C EEPROM transaction completed		*/
#define IPR_I2C_DAC		0x00000200      /* I2C DAC transaction completed		*/
#define IPR_AI			0x00000100      /* Audio pending register changed. See PTR reg 0x76	*/
#define IPR_GPI			0x00000080      /* General Purpose input changed		*/
#define IPR_SRC_LOCKED          0x00000040      /* SRC lock status changed			*/
#define IPR_SPDIF_STATUS        0x00000020      /* SPDIF status changed				*/
#define IPR_TIMER2              0x00000010      /* 192000Hz Timer				*/
#define IPR_TIMER1              0x00000008      /* 44100Hz Timer				*/
#define IPR_MIDI_RX_A		0x00000004	/* MIDI UART-A Receive buffer non-empty		*/
#define IPR_MIDI_TX_A		0x00000002	/* MIDI UART-A Transmit buffer empty		*/
#define IPR_PCI			0x00000001	/* PCI Bus error				*/

#define INTE			0x0c		/* Interrupt enable register			*/

#define INTE_MIDI_RX_B		0x00020000	/* MIDI UART-B Receive buffer non-empty		*/
#define INTE_MIDI_TX_B		0x00010000	/* MIDI UART-B Transmit buffer empty		*/
#define INTE_SPDIF_IN_USER	0x00004000      /* SPDIF input user data has 16 more bits	*/
#define INTE_SPDIF_OUT_USER	0x00002000      /* SPDIF output user data needs 16 more bits	*/
#define INTE_SPDIF_OUT_FRAME	0x00001000      /* SPDIF frame about to start			*/
#define INTE_SPI		0x00000800      /* SPI transaction completed			*/
#define INTE_I2C_EEPROM		0x00000400      /* I2C EEPROM transaction completed		*/
#define INTE_I2C_DAC		0x00000200      /* I2C DAC transaction completed		*/
#define INTE_AI			0x00000100      /* Audio pending register changed. See PTR reg 0x75 */
#define INTE_GPI		0x00000080      /* General Purpose input changed		*/
#define INTE_SRC_LOCKED         0x00000040      /* SRC lock status changed			*/
#define INTE_SPDIF_STATUS       0x00000020      /* SPDIF status changed				*/
#define INTE_TIMER2             0x00000010      /* 192000Hz Timer				*/
#define INTE_TIMER1             0x00000008      /* 44100Hz Timer				*/
#define INTE_MIDI_RX_A		0x00000004	/* MIDI UART-A Receive buffer non-empty		*/
#define INTE_MIDI_TX_A		0x00000002	/* MIDI UART-A Transmit buffer empty		*/
#define INTE_PCI		0x00000001	/* PCI Bus error				*/

#define UNKNOWN10		0x10		/* Unknown ??. Defaults to 0 */
#define HCFG			0x14		/* Hardware config register			*/
						/* 0x1000 causes AC3 to fails. It adds a dither bit. */

#define HCFG_STAC		0x10000000	/* Special mode for STAC9460 Codec. */
#define HCFG_CAPTURE_I2S_BYPASS	0x08000000	/* 1 = bypass I2S input async SRC. */
#define HCFG_CAPTURE_SPDIF_BYPASS 0x04000000	/* 1 = bypass SPDIF input async SRC. */
#define HCFG_PLAYBACK_I2S_BYPASS 0x02000000	/* 0 = I2S IN mixer output, 1 = I2S IN1. */
#define HCFG_FORCE_LOCK		0x01000000	/* For test only. Force input SRC tracker to lock. */
#define HCFG_PLAYBACK_ATTENUATION 0x00006000	/* Playback attenuation mask. 0 = 0dB, 1 = 6dB, 2 = 12dB, 3 = Mute. */
#define HCFG_PLAYBACK_DITHER	0x00001000	/* 1 = Add dither bit to all playback channels. */
#define HCFG_PLAYBACK_S32_LE	0x00000800	/* 1 = S32_LE, 0 = S16_LE                       */
#define HCFG_CAPTURE_S32_LE	0x00000400	/* 1 = S32_LE, 0 = S16_LE (S32_LE current not working)	*/
#define HCFG_8_CHANNEL_PLAY	0x00000200	/* 1 = 8 channels, 0 = 2 channels per substream.*/
#define HCFG_8_CHANNEL_CAPTURE	0x00000100	/* 1 = 8 channels, 0 = 2 channels per substream.*/
#define HCFG_MONO		0x00000080	/* 1 = I2S Input mono                           */
#define HCFG_I2S_OUTPUT		0x00000010	/* 1 = I2S Output disabled                      */
#define HCFG_AC97		0x00000008	/* 0 = AC97 1.0, 1 = AC97 2.0                   */
#define HCFG_LOCK_PLAYBACK_CACHE 0x00000004	/* 1 = Cancel bustmaster accesses to soundcache */
						/* NOTE: This should generally never be used.  	*/
#define HCFG_LOCK_CAPTURE_CACHE	0x00000002	/* 1 = Cancel bustmaster accesses to soundcache */
						/* NOTE: This should generally never be used.  	*/
#define HCFG_AUDIOENABLE	0x00000001	/* 0 = CODECs transmit zero-valued samples	*/
						/* Should be set to 1 when the EMU10K1 is	*/
						/* completely initialized.			*/
#define GPIO			0x18		/* Defaults: 005f03a3-Analog, 005f02a2-SPDIF.   */
						/* Here pins 0,1,2,3,4,,6 are output. 5,7 are input */
						/* For the Audigy LS, pin 0 (or bit 8) controls the SPDIF/Analog jack. */
						/* SB Live 24bit:
						 * bit 8 0 = SPDIF in and out / 1 = Analog (Mic or Line)-in.
						 * bit 9 0 = Mute / 1 = Analog out.
						 * bit 10 0 = Line-in / 1 = Mic-in.
						 * bit 11 0 = ? / 1 = ?
						 * bit 12 0 = 48 Khz / 1 = 96 Khz Analog out on SB Live 24bit.
						 * bit 13 0 = ? / 1 = ?
						 * bit 14 0 = Mute / 1 = Analog out
						 * bit 15 0 = ? / 1 = ?
						 * Both bit 9 and bit 14 have to be set for analog sound to work on the SB Live 24bit.
						 */
						/* 8 general purpose programmable In/Out pins.
						 * GPI [8:0] Read only. Default 0.
						 * GPO [15:8] Default 0x9. (Default to SPDIF jack enabled for SPDIF)
						 * GPO Enable [23:16] Default 0x0f. Setting a bit to 1, causes the pin to be an output pin.
						 */
#define AC97DATA		0x1c		/* AC97 register set data register (16 bit)	*/

#define AC97ADDRESS		0x1e		/* AC97 register set address register (8 bit)	*/

/********************************************************************************************************/
/* CA0106 pointer-offset register set, accessed through the PTR and DATA registers                     */
/********************************************************************************************************/
                                                                                                                           
/* Initally all registers from 0x00 to 0x3f have zero contents. */
#define PLAYBACK_LIST_ADDR	0x00		/* Base DMA address of a list of pointers to each period/size */
						/* One list entry: 4 bytes for DMA address, 
						 * 4 bytes for period_size << 16.
						 * One list entry is 8 bytes long.
						 * One list entry for each period in the buffer.
						 */
						/* ADDR[31:0], Default: 0x0 */
#define PLAYBACK_LIST_SIZE	0x01		/* Size of list in bytes << 16. E.g. 8 periods -> 0x00380000  */
						/* SIZE[21:16], Default: 0x8 */
#define PLAYBACK_LIST_PTR	0x02		/* Pointer to the current period being played */
						/* PTR[5:0], Default: 0x0 */
#define PLAYBACK_UNKNOWN3	0x03		/* Not used ?? */
#define PLAYBACK_DMA_ADDR	0x04		/* Playback DMA addresss */
						/* DMA[31:0], Default: 0x0 */
#define PLAYBACK_PERIOD_SIZE	0x05		/* Playback period size. win2000 uses 0x04000000 */
						/* SIZE[31:16], Default: 0x0 */
#define PLAYBACK_POINTER	0x06		/* Playback period pointer. Used with PLAYBACK_LIST_PTR to determine buffer position currently in DAC */
						/* POINTER[15:0], Default: 0x0 */
#define PLAYBACK_PERIOD_END_ADDR 0x07		/* Playback fifo end address */
						/* END_ADDR[15:0], FLAG[16] 0 = don't stop, 1 = stop */
#define PLAYBACK_FIFO_OFFSET_ADDRESS	0x08	/* Current fifo offset address [21:16] */
						/* Cache size valid [5:0] */
#define PLAYBACK_UNKNOWN9	0x09		/* 0x9 to 0xf Unused */
#define CAPTURE_DMA_ADDR	0x10		/* Capture DMA address */
						/* DMA[31:0], Default: 0x0 */
#define CAPTURE_BUFFER_SIZE	0x11		/* Capture buffer size */
						/* SIZE[31:16], Default: 0x0 */
#define CAPTURE_POINTER		0x12		/* Capture buffer pointer. Sample currently in ADC */
						/* POINTER[15:0], Default: 0x0 */
#define CAPTURE_FIFO_OFFSET_ADDRESS	0x13	/* Current fifo offset address [21:16] */
						/* Cache size valid [5:0] */
#define PLAYBACK_LAST_SAMPLE    0x20		/* The sample currently being played */
/* 0x21 - 0x3f unused */
#define BASIC_INTERRUPT         0x40		/* Used by both playback and capture interrupt handler */
						/* Playback (0x1<<channel_id) */
						/* Capture  (0x100<<channel_id) */
						/* Playback sample rate 96000 = 0x20000 */
						/* Start Playback [3:0] (one bit per channel)
						 * Start Capture [11:8] (one bit per channel)
						 * Playback rate [23:16] (2 bits per channel) (0=48kHz, 1=44.1kHz, 2=96kHz, 3=192Khz)
						 * Playback mixer in enable [27:24] (one bit per channel)
						 * Playback mixer out enable [31:28] (one bit per channel)
						 */
/* The Digital out jack is shared with the Center/LFE Analogue output. 
 * The jack has 4 poles. I will call 1 - Tip, 2 - Next to 1, 3 - Next to 2, 4 - Next to 3
 * For Analogue: 1 -> Center Speaker, 2 -> Sub Woofer, 3 -> Ground, 4 -> Ground
 * For Digital: 1 -> Front SPDIF, 2 -> Rear SPDIF, 3 -> Center/Subwoofer SPDIF, 4 -> Ground.
 * Standard 4 pole Video A/V cable with RCA outputs: 1 -> White, 2 -> Yellow, 3 -> Sheild on all three, 4 -> Red.
 * So, from this you can see that you cannot use a Standard 4 pole Video A/V cable with the SB Audigy LS card.
 */
/* The Front SPDIF PCM gets mixed with samples from the AC97 codec, so can only work for Stereo PCM and not AC3/DTS
 * The Rear SPDIF can be used for Stereo PCM and also AC3/DTS
 * The Center/LFE SPDIF cannot be used for AC3/DTS, but can be used for Stereo PCM.
 * Summary: For ALSA we use the Rear channel for SPDIF Digital AC3/DTS output
 */
/* A standard 2 pole mono mini-jack to RCA plug can be used for SPDIF Stereo PCM output from the Front channel.
 * A standard 3 pole stereo mini-jack to 2 RCA plugs can be used for SPDIF AC3/DTS and Stereo PCM output utilising the Rear channel and just one of the RCA plugs. 
 */
#define SPCS0			0x41		/* SPDIF output Channel Status 0 register. For Rear. default=0x02108004, non-audio=0x02108006	*/
#define SPCS1			0x42		/* SPDIF output Channel Status 1 register. For Front */
#define SPCS2			0x43		/* SPDIF output Channel Status 2 register. For Center/LFE */
#define SPCS3			0x44		/* SPDIF output Channel Status 3 register. Unknown */
						/* When Channel set to 0: */
#define SPCS_CLKACCYMASK	0x30000000	/* Clock accuracy				*/
#define SPCS_CLKACCY_1000PPM	0x00000000	/* 1000 parts per million			*/
#define SPCS_CLKACCY_50PPM	0x10000000	/* 50 parts per million				*/
#define SPCS_CLKACCY_VARIABLE	0x20000000	/* Variable accuracy				*/
#define SPCS_SAMPLERATEMASK	0x0f000000	/* Sample rate					*/
#define SPCS_SAMPLERATE_44	0x00000000	/* 44.1kHz sample rate				*/
#define SPCS_SAMPLERATE_48	0x02000000	/* 48kHz sample rate				*/
#define SPCS_SAMPLERATE_32	0x03000000	/* 32kHz sample rate				*/
#define SPCS_CHANNELNUMMASK	0x00f00000	/* Channel number				*/
#define SPCS_CHANNELNUM_UNSPEC	0x00000000	/* Unspecified channel number			*/
#define SPCS_CHANNELNUM_LEFT	0x00100000	/* Left channel					*/
#define SPCS_CHANNELNUM_RIGHT	0x00200000	/* Right channel				*/
#define SPCS_SOURCENUMMASK	0x000f0000	/* Source number				*/
#define SPCS_SOURCENUM_UNSPEC	0x00000000	/* Unspecified source number			*/
#define SPCS_GENERATIONSTATUS	0x00008000	/* Originality flag (see IEC-958 spec)		*/
#define SPCS_CATEGORYCODEMASK	0x00007f00	/* Category code (see IEC-958 spec)		*/
#define SPCS_MODEMASK		0x000000c0	/* Mode (see IEC-958 spec)			*/
#define SPCS_EMPHASISMASK	0x00000038	/* Emphasis					*/
#define SPCS_EMPHASIS_NONE	0x00000000	/* No emphasis					*/
#define SPCS_EMPHASIS_50_15	0x00000008	/* 50/15 usec 2 channel				*/
#define SPCS_COPYRIGHT		0x00000004	/* Copyright asserted flag -- do not modify	*/
#define SPCS_NOTAUDIODATA	0x00000002	/* 0 = Digital audio, 1 = not audio		*/
#define SPCS_PROFESSIONAL	0x00000001	/* 0 = Consumer (IEC-958), 1 = pro (AES3-1992)	*/

						/* When Channel set to 1: */
#define SPCS_WORD_LENGTH_MASK	0x0000000f	/* Word Length Mask				*/
#define SPCS_WORD_LENGTH_16	0x00000008	/* Word Length 16 bit				*/
#define SPCS_WORD_LENGTH_17	0x00000006	/* Word Length 17 bit				*/
#define SPCS_WORD_LENGTH_18	0x00000004	/* Word Length 18 bit				*/
#define SPCS_WORD_LENGTH_19	0x00000002	/* Word Length 19 bit				*/
#define SPCS_WORD_LENGTH_20A	0x0000000a	/* Word Length 20 bit				*/
#define SPCS_WORD_LENGTH_20	0x00000009	/* Word Length 20 bit (both 0xa and 0x9 are 20 bit) */
#define SPCS_WORD_LENGTH_21	0x00000007	/* Word Length 21 bit				*/
#define SPCS_WORD_LENGTH_21	0x00000007	/* Word Length 21 bit				*/
#define SPCS_WORD_LENGTH_22	0x00000005	/* Word Length 22 bit				*/
#define SPCS_WORD_LENGTH_23	0x00000003	/* Word Length 23 bit				*/
#define SPCS_WORD_LENGTH_24	0x0000000b	/* Word Length 24 bit				*/
#define SPCS_ORIGINAL_SAMPLE_RATE_MASK	0x000000f0 /* Original Sample rate			*/
#define SPCS_ORIGINAL_SAMPLE_RATE_NONE	0x00000000 /* Original Sample rate not indicated	*/
#define SPCS_ORIGINAL_SAMPLE_RATE_16000	0x00000010 /* Original Sample rate	*/
#define SPCS_ORIGINAL_SAMPLE_RATE_RES1	0x00000020 /* Original Sample rate	*/
#define SPCS_ORIGINAL_SAMPLE_RATE_32000	0x00000030 /* Original Sample rate	*/
#define SPCS_ORIGINAL_SAMPLE_RATE_12000	0x00000040 /* Original Sample rate	*/
#define SPCS_ORIGINAL_SAMPLE_RATE_11025	0x00000050 /* Original Sample rate	*/
#define SPCS_ORIGINAL_SAMPLE_RATE_8000	0x00000060 /* Original Sample rate	*/
#define SPCS_ORIGINAL_SAMPLE_RATE_RES2	0x00000070 /* Original Sample rate	*/
#define SPCS_ORIGINAL_SAMPLE_RATE_192000 0x00000080 /* Original Sample rate	*/
#define SPCS_ORIGINAL_SAMPLE_RATE_24000	0x00000090 /* Original Sample rate	*/
#define SPCS_ORIGINAL_SAMPLE_RATE_96000	0x000000a0 /* Original Sample rate	*/
#define SPCS_ORIGINAL_SAMPLE_RATE_48000	0x000000b0 /* Original Sample rate	*/
#define SPCS_ORIGINAL_SAMPLE_RATE_176400 0x000000c0 /* Original Sample rate	*/
#define SPCS_ORIGINAL_SAMPLE_RATE_22050	0x000000d0 /* Original Sample rate	*/
#define SPCS_ORIGINAL_SAMPLE_RATE_88200	0x000000e0 /* Original Sample rate	*/
#define SPCS_ORIGINAL_SAMPLE_RATE_44100	0x000000f0 /* Original Sample rate	*/

#define SPDIF_SELECT1		0x45		/* Enables SPDIF or Analogue outputs 0-SPDIF, 0xf00-Analogue */
						/* 0x100 - Front, 0x800 - Rear, 0x200 - Center/LFE.
						 * But as the jack is shared, use 0xf00.
						 * The Windows2000 driver uses 0x0000000f for both digital and analog.
						 * 0xf00 introduces interesting noises onto the Center/LFE.
						 * If you turn the volume up, you hear computer noise,
						 * e.g. mouse moving, changing between app windows etc.
						 * So, I am going to set this to 0x0000000f all the time now,
						 * same as the windows driver does.
						 * Use register SPDIF_SELECT2(0x72) to switch between SPDIF and Analog.
						 */
						/* When Channel = 0:
						 * Wide SPDIF format [3:0] (one bit for each channel) (0=20bit, 1=24bit)
						 * Tristate SPDIF Output [11:8] (one bit for each channel) (0=Not tristate, 1=Tristate)
						 * SPDIF Bypass enable [19:16] (one bit for each channel) (0=Not bypass, 1=Bypass)
						 */
						/* When Channel = 1:
						 * SPDIF 0 User data [7:0]
						 * SPDIF 1 User data [15:8]
						 * SPDIF 0 User data [23:16]
						 * SPDIF 0 User data [31:24]
						 * User data can be sent by using the SPDIF output frame pending and SPDIF output user bit interrupts.
						 */
#define WATERMARK		0x46		/* Test bit to indicate cache usage level */
#define SPDIF_INPUT_STATUS	0x49		/* SPDIF Input status register. Bits the same as SPCS.
						 * When Channel = 0: Bits the same as SPCS channel 0.
						 * When Channel = 1: Bits the same as SPCS channel 1.
						 * When Channel = 2:
						 * SPDIF Input User data [16:0]
						 * SPDIF Input Frame count [21:16]
						 */
#define CAPTURE_CACHE_DATA	0x50		/* 0x50-0x5f Recorded samples. */
#define CAPTURE_SOURCE          0x60            /* Capture Source 0 = MIC */
#define CAPTURE_SOURCE_CHANNEL0 0xf0000000	/* Mask for selecting the Capture sources */
#define CAPTURE_SOURCE_CHANNEL1 0x0f000000	/* 0 - SPDIF mixer output. */
#define CAPTURE_SOURCE_CHANNEL2 0x00f00000      /* 1 - What you hear or . 2 - ?? */
#define CAPTURE_SOURCE_CHANNEL3 0x000f0000	/* 3 - Mic in, Line in, TAD in, Aux in. */
#define CAPTURE_SOURCE_RECORD_MAP 0x0000ffff	/* Default 0x00e4 */
						/* Record Map [7:0] (2 bits per channel) 0=mapped to channel 0, 1=mapped to channel 1, 2=mapped to channel2, 3=mapped to channel3 
						 * Record source select for channel 0 [18:16]
						 * Record source select for channel 1 [22:20]
						 * Record source select for channel 2 [26:24]
						 * Record source select for channel 3 [30:28]
						 * 0 - SPDIF mixer output.
						 * 1 - i2s mixer output.
						 * 2 - SPDIF input.
						 * 3 - i2s input.
						 * 4 - AC97 capture.
						 * 5 - SRC output.
						 */
#define CAPTURE_VOLUME1         0x61            /* Capture  volume per channel 0-3 */
#define CAPTURE_VOLUME2         0x62            /* Capture  volume per channel 4-7 */

#define PLAYBACK_ROUTING1       0x63            /* Playback routing of channels 0-7. Effects AC3 output. Default 0x32765410 */
#define ROUTING1_REAR           0x77000000      /* Channel_id 0 sends to 10, Channel_id 1 sends to 32 */
#define ROUTING1_NULL           0x00770000      /* Channel_id 2 sends to 54, Channel_id 3 sends to 76 */
#define ROUTING1_CENTER_LFE     0x00007700      /* 0x32765410 means, send Channel_id 0 to FRONT, Channel_id 1 to REAR */
#define ROUTING1_FRONT          0x00000077	/* Channel_id 2 to CENTER_LFE, Channel_id 3 to NULL. */
						/* Channel_id's handle stereo channels. Channel X is a single mono channel */
						/* Host is input from the PCI bus. */
						/* Host channel 0 [2:0] -> SPDIF Mixer/Router channel 0-7.
						 * Host channel 1 [6:4] -> SPDIF Mixer/Router channel 0-7.
						 * Host channel 2 [10:8] -> SPDIF Mixer/Router channel 0-7.
						 * Host channel 3 [14:12] -> SPDIF Mixer/Router channel 0-7.
						 * Host channel 4 [18:16] -> SPDIF Mixer/Router channel 0-7.
						 * Host channel 5 [22:20] -> SPDIF Mixer/Router channel 0-7.
						 * Host channel 6 [26:24] -> SPDIF Mixer/Router channel 0-7.
						 * Host channel 7 [30:28] -> SPDIF Mixer/Router channel 0-7.
						 */

#define PLAYBACK_ROUTING2       0x64            /* Playback Routing . Feeding Capture channels back into Playback. Effects AC3 output. Default 0x76767676 */
						/* SRC is input from the capture inputs. */
						/* SRC channel 0 [2:0] -> SPDIF Mixer/Router channel 0-7.
						 * SRC channel 1 [6:4] -> SPDIF Mixer/Router channel 0-7.
						 * SRC channel 2 [10:8] -> SPDIF Mixer/Router channel 0-7.
						 * SRC channel 3 [14:12] -> SPDIF Mixer/Router channel 0-7.
						 * SRC channel 4 [18:16] -> SPDIF Mixer/Router channel 0-7.
						 * SRC channel 5 [22:20] -> SPDIF Mixer/Router channel 0-7.
						 * SRC channel 6 [26:24] -> SPDIF Mixer/Router channel 0-7.
						 * SRC channel 7 [30:28] -> SPDIF Mixer/Router channel 0-7.
						 */

#define PLAYBACK_MUTE           0x65            /* Unknown. While playing 0x0, while silent 0x00fc0000 */
						/* SPDIF Mixer input control:
						 * Invert SRC to SPDIF Mixer [7-0] (One bit per channel)
						 * Invert Host to SPDIF Mixer [15:8] (One bit per channel)
						 * SRC to SPDIF Mixer disable [23:16] (One bit per channel)
						 * Host to SPDIF Mixer disable [31:24] (One bit per channel)
						 */
#define PLAYBACK_VOLUME1        0x66            /* Playback SPDIF volume per channel. Set to the same PLAYBACK_VOLUME(0x6a) */
						/* PLAYBACK_VOLUME1 must be set to 30303030 for SPDIF AC3 Playback */
						/* SPDIF mixer input volume. 0=12dB, 0x30=0dB, 0xFE=-51.5dB, 0xff=Mute */
						/* One register for each of the 4 stereo streams. */
						/* SRC Right volume [7:0]
						 * SRC Left  volume [15:8]
						 * Host Right volume [23:16]
						 * Host Left  volume [31:24]
						 */
#define CAPTURE_ROUTING1        0x67            /* Capture Routing. Default 0x32765410 */
						/* Similar to register 0x63, except that the destination is the I2S mixer instead of the SPDIF mixer. I.E. Outputs to the Analog outputs instead of SPDIF. */
#define CAPTURE_ROUTING2        0x68            /* Unknown Routing. Default 0x76767676 */
						/* Similar to register 0x64, except that the destination is the I2S mixer instead of the SPDIF mixer. I.E. Outputs to the Analog outputs instead of SPDIF. */
#define CAPTURE_MUTE            0x69            /* Unknown. While capturing 0x0, while silent 0x00fc0000 */
						/* Similar to register 0x65, except that the destination is the I2S mixer instead of the SPDIF mixer. I.E. Outputs to the Analog outputs instead of SPDIF. */
#define PLAYBACK_VOLUME2        0x6a            /* Playback Analog volume per channel. Does not effect AC3 output */
						/* Similar to register 0x66, except that the destination is the I2S mixer instead of the SPDIF mixer. I.E. Outputs to the Analog outputs instead of SPDIF. */
#define UNKNOWN6b               0x6b            /* Unknown. Readonly. Default 00400000 00400000 00400000 00400000 */
#define MIDI_UART_A_DATA		0x6c            /* Midi Uart A Data */
#define MIDI_UART_A_CMD		0x6d            /* Midi Uart A Command/Status */
#define MIDI_UART_B_DATA		0x6e            /* Midi Uart B Data (currently unused) */
#define MIDI_UART_B_CMD		0x6f            /* Midi Uart B Command/Status (currently unused) */

/* unique channel identifier for midi->channel */

#define CA0106_MIDI_CHAN_A		0x1
#define CA0106_MIDI_CHAN_B		0x2

/* from mpu401 */

#define CA0106_MIDI_INPUT_AVAIL 	0x80
#define CA0106_MIDI_OUTPUT_READY	0x40
#define CA0106_MPU401_RESET		0xff
#define CA0106_MPU401_ENTER_UART	0x3f
#define CA0106_MPU401_ACK		0xfe

#define SAMPLE_RATE_TRACKER_STATUS 0x70         /* Readonly. Default 00108000 00108000 00500000 00500000 */
						/* Estimated sample rate [19:0] Relative to 48kHz. 0x8000 =  1.0
						 * Rate Locked [20]
						 * SPDIF Locked [21] For SPDIF channel only.
						 * Valid Audio [22] For SPDIF channel only.
						 */
#define CAPTURE_CONTROL         0x71            /* Some sort of routing. default = 40c81000 30303030 30300000 00700000 */
						/* Channel_id 0: 0x40c81000 must be changed to 0x40c80000 for SPDIF AC3 input or output. */
						/* Channel_id 1: 0xffffffff(mute) 0x30303030(max) controls CAPTURE feedback into PLAYBACK. */
						/* Sample rate output control register Channel=0
						 * Sample output rate [1:0] (0=48kHz, 1=44.1kHz, 2=96kHz, 3=192Khz)
						 * Sample input rate [3:2] (0=48kHz, 1=Not available, 2=96kHz, 3=192Khz)
						 * SRC input source select [4] 0=Audio from digital mixer, 1=Audio from analog source.
						 * Record rate [9:8] (0=48kHz, 1=Not available, 2=96kHz, 3=192Khz)
						 * Record mixer output enable [12:10] 
						 * I2S input rate master mode [15:14] (0=48kHz, 1=44.1kHz, 2=96kHz, 3=192Khz)
						 * I2S output rate [17:16] (0=48kHz, 1=44.1kHz, 2=96kHz, 3=192Khz)
						 * I2S output source select [18] (0=Audio from host, 1=Audio from SRC)
						 * Record mixer I2S enable [20:19] (enable/disable i2sin1 and i2sin0)
						 * I2S output master clock select [21] (0=256*I2S output rate, 1=512*I2S output rate.)
						 * I2S input master clock select [22] (0=256*I2S input rate, 1=512*I2S input rate.)
						 * I2S input mode [23] (0=Slave, 1=Master)
						 * SPDIF output rate [25:24] (0=48kHz, 1=44.1kHz, 2=96kHz, 3=192Khz)
						 * SPDIF output source select [26] (0=host, 1=SRC)
						 * Not used [27]
						 * Record Source 0 input [29:28] (0=SPDIF in, 1=I2S in, 2=AC97 Mic, 3=AC97 PCM)
						 * Record Source 1 input [31:30] (0=SPDIF in, 1=I2S in, 2=AC97 Mic, 3=AC97 PCM)
						 */ 
						/* Sample rate output control register Channel=1
						 * I2S Input 0 volume Right [7:0]
						 * I2S Input 0 volume Left [15:8]
						 * I2S Input 1 volume Right [23:16]
						 * I2S Input 1 volume Left [31:24]
						 */
						/* Sample rate output control register Channel=2
						 * SPDIF Input volume Right [23:16]
						 * SPDIF Input volume Left [31:24]
						 */
						/* Sample rate output control register Channel=3
						 * No used
						 */
#define SPDIF_SELECT2           0x72            /* Some sort of routing. Channel_id 0 only. default = 0x0f0f003f. Analog 0x000b0000, Digital 0x0b000000 */
#define ROUTING2_FRONT_MASK     0x00010000      /* Enable for Front speakers. */
#define ROUTING2_CENTER_LFE_MASK 0x00020000     /* Enable for Center/LFE speakers. */
#define ROUTING2_REAR_MASK      0x00080000      /* Enable for Rear speakers. */
						/* Audio output control
						 * AC97 output enable [5:0]
						 * I2S output enable [19:16]
						 * SPDIF output enable [27:24]
						 */ 
#define UNKNOWN73               0x73            /* Unknown. Readonly. Default 0x0 */
#define CHIP_VERSION            0x74            /* P17 Chip version. Channel_id 0 only. Default 00000071 */
#define EXTENDED_INT_MASK       0x75            /* Used by both playback and capture interrupt handler */
						/* Sets which Interrupts are enabled. */
						/* 0x00000001 = Half period. Playback.
						 * 0x00000010 = Full period. Playback.
						 * 0x00000100 = Half buffer. Playback.
						 * 0x00001000 = Full buffer. Playback.
						 * 0x00010000 = Half buffer. Capture.
						 * 0x00100000 = Full buffer. Capture.
						 * Capture can only do 2 periods.
						 * 0x01000000 = End audio. Playback.
						 * 0x40000000 = Half buffer Playback,Caputre xrun.
						 * 0x80000000 = Full buffer Playback,Caputre xrun.
						 */
#define EXTENDED_INT            0x76            /* Used by both playback and capture interrupt handler */
						/* Shows which interrupts are active at the moment. */
						/* Same bit layout as EXTENDED_INT_MASK */
#define COUNTER77               0x77		/* Counter range 0 to 0x3fffff, 192000 counts per second. */
#define COUNTER78               0x78		/* Counter range 0 to 0x3fffff, 44100 counts per second. */
#define EXTENDED_INT_TIMER      0x79            /* Channel_id 0 only. Used by both playback and capture interrupt handler */
						/* Causes interrupts based on timer intervals. */
#define SPI			0x7a		/* SPI: Serial Interface Register */
#define I2C_A			0x7b		/* I2C Address. 32 bit */
#define I2C_D0			0x7c		/* I2C Data Port 0. 32 bit */
#define I2C_D1			0x7d		/* I2C Data Port 1. 32 bit */
//I2C values
#define I2C_A_ADC_ADD_MASK	0x000000fe	//The address is a 7 bit address
#define I2C_A_ADC_RW_MASK	0x00000001	//bit mask for R/W
#define I2C_A_ADC_TRANS_MASK	0x00000010  	//Bit mask for I2c address DAC value
#define I2C_A_ADC_ABORT_MASK	0x00000020	//Bit mask for I2C transaction abort flag
#define I2C_A_ADC_LAST_MASK	0x00000040	//Bit mask for Last word transaction
#define I2C_A_ADC_BYTE_MASK	0x00000080	//Bit mask for Byte Mode

#define I2C_A_ADC_ADD		0x00000034	//This is the Device address for ADC 
#define I2C_A_ADC_READ		0x00000001	//To perform a read operation
#define I2C_A_ADC_START		0x00000100	//Start I2C transaction
#define I2C_A_ADC_ABORT		0x00000200	//I2C transaction abort
#define I2C_A_ADC_LAST		0x00000400	//I2C last transaction
#define I2C_A_ADC_BYTE		0x00000800	//I2C one byte mode

#define I2C_D_ADC_REG_MASK	0xfe000000  	//ADC address register 
#define I2C_D_ADC_DAT_MASK	0x01ff0000  	//ADC data register

#define ADC_TIMEOUT		0x00000007	//ADC Timeout Clock Disable
#define ADC_IFC_CTRL		0x0000000b	//ADC Interface Control
#define ADC_MASTER		0x0000000c	//ADC Master Mode Control
#define ADC_POWER		0x0000000d	//ADC PowerDown Control
#define ADC_ATTEN_ADCL		0x0000000e	//ADC Attenuation ADCL
#define ADC_ATTEN_ADCR		0x0000000f	//ADC Attenuation ADCR
#define ADC_ALC_CTRL1		0x00000010	//ADC ALC Control 1
#define ADC_ALC_CTRL2		0x00000011	//ADC ALC Control 2
#define ADC_ALC_CTRL3		0x00000012	//ADC ALC Control 3
#define ADC_NOISE_CTRL		0x00000013	//ADC Noise Gate Control
#define ADC_LIMIT_CTRL		0x00000014	//ADC Limiter Control
#define ADC_MUX			0x00000015  	//ADC Mux offset

#if 0
/* FIXME: Not tested yet. */
#define ADC_GAIN_MASK		0x000000ff	//Mask for ADC Gain
#define ADC_ZERODB		0x000000cf	//Value to set ADC to 0dB
#define ADC_MUTE_MASK		0x000000c0	//Mask for ADC mute
#define ADC_MUTE		0x000000c0	//Value to mute ADC
#define ADC_OSR			0x00000008	//Mask for ADC oversample rate select
#define ADC_TIMEOUT_DISABLE	0x00000008	//Value and mask to disable Timeout clock
#define ADC_HPF_DISABLE		0x00000100	//Value and mask to disable High pass filter
#define ADC_TRANWIN_MASK	0x00000070	//Mask for Length of Transient Window
#endif

#define ADC_MUX_MASK		0x0000000f	//Mask for ADC Mux
#define ADC_MUX_PHONE		0x00000001	//Value to select TAD at ADC Mux (Not used)
#define ADC_MUX_MIC		0x00000002	//Value to select Mic at ADC Mux
#define ADC_MUX_LINEIN		0x00000004	//Value to select LineIn at ADC Mux
#define ADC_MUX_AUX		0x00000008	//Value to select Aux at ADC Mux

#define SET_CHANNEL 0  /* Testing channel outputs 0=Front, 1=Center/LFE, 2=Unknown, 3=Rear */
#define PCM_FRONT_CHANNEL 0
#define PCM_REAR_CHANNEL 1
#define PCM_CENTER_LFE_CHANNEL 2
#define PCM_UNKNOWN_CHANNEL 3
#define CONTROL_FRONT_CHANNEL 0
#define CONTROL_REAR_CHANNEL 3
#define CONTROL_CENTER_LFE_CHANNEL 1
#define CONTROL_UNKNOWN_CHANNEL 2

#include "ca_midi.h"

struct snd_ca0106;

struct snd_ca0106_channel {
	struct snd_ca0106 *emu;
	int number;
	int use;
	void (*interrupt)(struct snd_ca0106 *emu, struct snd_ca0106_channel *channel);
	struct snd_ca0106_pcm *epcm;
};

struct snd_ca0106_pcm {
	struct snd_ca0106 *emu;
	struct snd_pcm_substream *substream;
        int channel_id;
	unsigned short running;
};

struct snd_ca0106_details {
        u32 serial;
        char * name;
        int ac97;
	int gpio_type;
	int i2c_adc;
	int spi_dac;
};

// definition of the chip-specific record
struct snd_ca0106 {
	struct snd_card *card;
	struct snd_ca0106_details *details;
	struct pci_dev *pci;

	unsigned long port;
	struct resource *res_port;
	int irq;

	unsigned int revision;		/* chip revision */
	unsigned int serial;            /* serial number */
	unsigned short model;		/* subsystem id */

	spinlock_t emu_lock;

	struct snd_ac97 *ac97;
	struct snd_pcm *pcm;

	struct snd_ca0106_channel playback_channels[4];
	struct snd_ca0106_channel capture_channels[4];
	u32 spdif_bits[4];             /* s/pdif out setup */
	int spdif_enable;
	int capture_source;
	int i2c_capture_source;
	u8 i2c_capture_volume[4][2];
	int capture_mic_line_in;

	struct snd_dma_buffer buffer;

	struct snd_ca_midi midi;
	struct snd_ca_midi midi2;
};

int snd_ca0106_mixer(struct snd_ca0106 *emu);
int snd_ca0106_proc_init(struct snd_ca0106 * emu);

unsigned int snd_ca0106_ptr_read(struct snd_ca0106 * emu, 
				 unsigned int reg, 
				 unsigned int chn);

void snd_ca0106_ptr_write(struct snd_ca0106 *emu, 
			  unsigned int reg, 
			  unsigned int chn, 
			  unsigned int data);

int snd_ca0106_i2c_write(struct snd_ca0106 *emu, u32 reg, u32 value);


