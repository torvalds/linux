/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (c) by James Courtier-Dutton <James@superbug.demon.co.uk>
 *  Driver p16v chips
 *
 *  This code was initially based on code from ALSA's emu10k1x.c which is:
 *  Copyright (c) by Francisco Moraes <fmoraes@nc.rr.com>
 */

/********************************************************************************************************/
/* Audigy2 P16V pointer-offset register set, accessed through the PTR2 and DATA2 registers              */
/********************************************************************************************************/
                                                                                                                           
/* The sample rate of the SPDIF outputs is set by modifying a register in the EMU10K2 PTR register A_SPDIF_SAMPLERATE.
 * The sample rate is also controlled by the same registers that control the rate of the EMU10K2 sample rate converters.
 */

/* Initially all registers from 0x00 to 0x3f have zero contents. */
#define PLAYBACK_LIST_ADDR	0x00		/* Base DMA address of a list of pointers to each period/size */
						/* One list entry: 4 bytes for DMA address, 
						 * 4 bytes for period_size << 16.
						 * One list entry is 8 bytes long.
						 * One list entry for each period in the buffer.
						 */
#define PLAYBACK_LIST_SIZE	0x01		/* Size of list in bytes << 16. E.g. 8 periods -> 0x00380000  */
#define PLAYBACK_LIST_PTR	0x02		/* Pointer to the current period being played */
#define PLAYBACK_UNKNOWN3	0x03		/* Not used */
#define PLAYBACK_DMA_ADDR	0x04		/* Playback DMA address */
#define PLAYBACK_PERIOD_SIZE	0x05		/* Playback period size. win2000 uses 0x04000000 */
#define PLAYBACK_POINTER	0x06		/* Playback period pointer. Used with PLAYBACK_LIST_PTR to determine buffer position currently in DAC */
#define PLAYBACK_FIFO_END_ADDRESS	0x07		/* Playback FIFO end address */
#define PLAYBACK_FIFO_POINTER	0x08		/* Playback FIFO pointer and number of valid sound samples in cache */
#define PLAYBACK_UNKNOWN9	0x09		/* Not used */
#define CAPTURE_DMA_ADDR	0x10		/* Capture DMA address */
#define CAPTURE_BUFFER_SIZE	0x11		/* Capture buffer size */
#define CAPTURE_POINTER		0x12		/* Capture buffer pointer. Sample currently in ADC */
#define CAPTURE_FIFO_POINTER	0x13		/* Capture FIFO pointer and number of valid sound samples in cache */
#define CAPTURE_P16V_VOLUME1	0x14		/* Low: Capture volume 0xXXXX3030 */
#define CAPTURE_P16V_VOLUME2	0x15		/* High:Has no effect on capture volume */
#define CAPTURE_P16V_SOURCE     0x16            /* P16V source select. Set to 0x0700E4E5 for AC97 CAPTURE */
						/* [0:1] Capture input 0 channel select. 0 = Capture output 0.
						 *                                       1 = Capture output 1.
						 *                                       2 = Capture output 2.
						 *                                       3 = Capture output 3.
						 * [3:2] Capture input 1 channel select. 0 = Capture output 0.
						 *                                       1 = Capture output 1.
						 *                                       2 = Capture output 2.
						 *                                       3 = Capture output 3.
						 * [5:4] Capture input 2 channel select. 0 = Capture output 0.
						 *                                       1 = Capture output 1.
						 *                                       2 = Capture output 2.
						 *                                       3 = Capture output 3.
						 * [7:6] Capture input 3 channel select. 0 = Capture output 0.
						 *                                       1 = Capture output 1.
						 *                                       2 = Capture output 2.
						 *                                       3 = Capture output 3.
						 * [9:8] Playback input 0 channel select. 0 = Play output 0.
						 *                                        1 = Play output 1.
						 *                                        2 = Play output 2.
						 *                                        3 = Play output 3.
						 * [11:10] Playback input 1 channel select. 0 = Play output 0.
						 *                                          1 = Play output 1.
						 *                                          2 = Play output 2.
						 *                                          3 = Play output 3.
						 * [13:12] Playback input 2 channel select. 0 = Play output 0.
						 *                                          1 = Play output 1.
						 *                                          2 = Play output 2.
						 *                                          3 = Play output 3.
						 * [15:14] Playback input 3 channel select. 0 = Play output 0.
						 *                                          1 = Play output 1.
						 *                                          2 = Play output 2.
						 *                                          3 = Play output 3.
						 * [19:16] Playback mixer output enable. 1 bit per channel.
						 * [23:20] Capture mixer output enable. 1 bit per channel.
						 * [26:24] FX engine channel capture 0 = 0x60-0x67.
						 *                                   1 = 0x68-0x6f.
						 *                                   2 = 0x70-0x77.
						 *                                   3 = 0x78-0x7f.
						 *                                   4 = 0x80-0x87.
						 *                                   5 = 0x88-0x8f.
						 *                                   6 = 0x90-0x97.
						 *                                   7 = 0x98-0x9f.
						 * [31:27] Not used.
						 */

						/* 0x1 = capture on.
						 * 0x100 = capture off.
						 * 0x200 = capture off.
						 * 0x1000 = capture off.
						 */
#define CAPTURE_RATE_STATUS		0x17		/* Capture sample rate. Read only */
						/* [15:0] Not used.
						 * [18:16] Channel 0 Detected sample rate. 0 - 44.1khz
						 *                               1 - 48 khz
						 *                               2 - 96 khz
						 *                               3 - 192 khz
						 *                               7 - undefined rate.
						 * [19] Channel 0. 1 - Valid, 0 - Not Valid.
						 * [22:20] Channel 1 Detected sample rate. 
						 * [23] Channel 1. 1 - Valid, 0 - Not Valid.
						 * [26:24] Channel 2 Detected sample rate. 
						 * [27] Channel 2. 1 - Valid, 0 - Not Valid.
						 * [30:28] Channel 3 Detected sample rate. 
						 * [31] Channel 3. 1 - Valid, 0 - Not Valid.
						 */
/* 0x18 - 0x1f unused */
#define PLAYBACK_LAST_SAMPLE    0x20		/* The sample currently being played. Read only */
/* 0x21 - 0x3f unused */
#define BASIC_INTERRUPT         0x40		/* Used by both playback and capture interrupt handler */
						/* Playback (0x1<<channel_id) Don't touch high 16bits. */
						/* Capture  (0x100<<channel_id). not tested */
						/* Start Playback [3:0] (one bit per channel)
						 * Start Capture [11:8] (one bit per channel)
						 * Record source select for channel 0 [18:16]
						 * Record source select for channel 1 [22:20]
						 * Record source select for channel 2 [26:24]
						 * Record source select for channel 3 [30:28]
						 * 0 - SPDIF channel.
						 * 1 - I2S channel.
						 * 2 - SRC48 channel.
						 * 3 - SRCMulti_SPDIF channel.
						 * 4 - SRCMulti_I2S channel.
						 * 5 - SPDIF channel.
						 * 6 - fxengine capture.
						 * 7 - AC97 capture.
						 */
						/* Default 41110000.
						 * Writing 0xffffffff hangs the PC.
						 * Writing 0xffff0000 -> 77770000 so it must be some sort of route.
						 * bit 0x1 starts DMA playback on channel_id 0
						 */
/* 0x41,42 take values from 0 - 0xffffffff, but have no effect on playback */
/* 0x43,0x48 do not remember settings */
/* 0x41-45 unused */
#define WATERMARK            0x46		/* Test bit to indicate cache level usage */
						/* Values it can have while playing on channel 0. 
						 * 0000f000, 0000f004, 0000f008, 0000f00c.
						 * Readonly.
						 */
/* 0x47-0x4f unused */
/* 0x50-0x5f Capture cache data */
#define SRCSel			0x60            /* SRCSel. Default 0x4. Bypass P16V 0x14 */
						/* [0] 0 = 10K2 audio, 1 = SRC48 mixer output.
						 * [2] 0 = 10K2 audio, 1 = SRCMulti SPDIF mixer output.
						 * [4] 0 = 10K2 audio, 1 = SRCMulti I2S mixer output.
						 */
						/* SRC48 converts samples rates 44.1, 48, 96, 192 to 48 khz. */
						/* SRCMulti converts 48khz samples rates to 44.1, 48, 96, 192 to 48. */
						/* SRC48 and SRCMULTI sample rate select and output select. */
						/* 0xffffffff -> 0xC0000015
						 * 0xXXXXXXX4 = Enable Front Left/Right
						 * Enable PCMs
						 */

/* 0x61 -> 0x6c are Volume controls */
#define PLAYBACK_VOLUME_MIXER1  0x61		/* SRC48 Low to mixer input volume control. */
#define PLAYBACK_VOLUME_MIXER2  0x62		/* SRC48 High to mixer input volume control. */
#define PLAYBACK_VOLUME_MIXER3  0x63		/* SRCMULTI SPDIF Low to mixer input volume control. */
#define PLAYBACK_VOLUME_MIXER4  0x64		/* SRCMULTI SPDIF High to mixer input volume control. */
#define PLAYBACK_VOLUME_MIXER5  0x65		/* SRCMULTI I2S Low to mixer input volume control. */
#define PLAYBACK_VOLUME_MIXER6  0x66		/* SRCMULTI I2S High to mixer input volume control. */
#define PLAYBACK_VOLUME_MIXER7  0x67		/* P16V Low to SRCMULTI SPDIF mixer input volume control. */
#define PLAYBACK_VOLUME_MIXER8  0x68		/* P16V High to SRCMULTI SPDIF mixer input volume control. */
#define PLAYBACK_VOLUME_MIXER9  0x69		/* P16V Low to SRCMULTI I2S mixer input volume control. */
						/* 0xXXXX3030 = PCM0 Volume (Front).
						 * 0x3030XXXX = PCM1 Volume (Center)
						 */
#define PLAYBACK_VOLUME_MIXER10  0x6a		/* P16V High to SRCMULTI I2S mixer input volume control. */
						/* 0x3030XXXX = PCM3 Volume (Rear). */
#define PLAYBACK_VOLUME_MIXER11  0x6b		/* E10K2 Low to SRC48 mixer input volume control. */
#define PLAYBACK_VOLUME_MIXER12 0x6c		/* E10K2 High to SRC48 mixer input volume control. */

#define SRC48_ENABLE            0x6d            /* SRC48 input audio enable */
						/* SRC48 converts samples rates 44.1, 48, 96, 192 to 48 khz. */
						/* [23:16] The corresponding P16V channel to SRC48 enabled if == 1.
						 * [31:24] The corresponding E10K2 channel to SRC48 enabled.
						 */
#define SRCMULTI_ENABLE         0x6e            /* SRCMulti input audio enable. Default 0xffffffff */
						/* SRCMulti converts 48khz samples rates to 44.1, 48, 96, 192 to 48. */
						/* [7:0] The corresponding P16V channel to SRCMulti_I2S enabled if == 1.
						 * [15:8] The corresponding E10K2 channel to SRCMulti I2S enabled.
						 * [23:16] The corresponding P16V channel to SRCMulti SPDIF enabled.
						 * [31:24] The corresponding E10K2 channel to SRCMulti SPDIF enabled.
						 */
						/* Bypass P16V 0xff00ff00 
						 * Bitmap. 0 = Off, 1 = On.
						 * P16V playback outputs:
						 * 0xXXXXXXX1 = PCM0 Left. (Front)
						 * 0xXXXXXXX2 = PCM0 Right.
						 * 0xXXXXXXX4 = PCM1 Left. (Center/LFE)
						 * 0xXXXXXXX8 = PCM1 Right.
						 * 0xXXXXXX1X = PCM2 Left. (Unknown)
						 * 0xXXXXXX2X = PCM2 Right.
						 * 0xXXXXXX4X = PCM3 Left. (Rear)
						 * 0xXXXXXX8X = PCM3 Right.
						 */
#define AUDIO_OUT_ENABLE        0x6f            /* Default: 000100FF */
						/* [3:0] Does something, but not documented. Probably capture enable.
						 * [7:4] Playback channels enable. not documented.
						 * [16] AC97 output enable if == 1
						 * [30] 0 = SRCMulti_I2S input from fxengine 0x68-0x6f.
						 *      1 = SRCMulti_I2S input from SRC48 output.
						 * [31] 0 = SRCMulti_SPDIF input from fxengine 0x60-0x67.
						 *      1 = SRCMulti_SPDIF input from SRC48 output.
						 */
						/* 0xffffffff -> C00100FF */
						/* 0 -> Not playback sound, irq still running */
						/* 0xXXXXXX10 = PCM0 Left/Right On. (Front)
						 * 0xXXXXXX20 = PCM1 Left/Right On. (Center/LFE)
						 * 0xXXXXXX40 = PCM2 Left/Right On. (Unknown)
						 * 0xXXXXXX80 = PCM3 Left/Right On. (Rear)
						 */
#define PLAYBACK_SPDIF_SELECT     0x70          /* Default: 12030F00 */
						/* 0xffffffff -> 3FF30FFF */
						/* 0x00000001 pauses stream/irq fail. */
						/* All other bits do not effect playback */
#define PLAYBACK_SPDIF_SRC_SELECT 0x71          /* Default: 0000E4E4 */
						/* 0xffffffff -> F33FFFFF */
						/* All bits do not effect playback */
#define PLAYBACK_SPDIF_USER_DATA0 0x72		/* SPDIF out user data 0 */
#define PLAYBACK_SPDIF_USER_DATA1 0x73		/* SPDIF out user data 1 */
/* 0x74-0x75 unknown */
#define CAPTURE_SPDIF_CONTROL	0x76		/* SPDIF in control setting */
#define CAPTURE_SPDIF_STATUS	0x77		/* SPDIF in status */
#define CAPURE_SPDIF_USER_DATA0 0x78		/* SPDIF in user data 0 */
#define CAPURE_SPDIF_USER_DATA1 0x79		/* SPDIF in user data 1 */
#define CAPURE_SPDIF_USER_DATA2 0x7a		/* SPDIF in user data 2 */

