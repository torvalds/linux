/*	$OpenBSD: s3_617.h,v 1.2 2001/07/04 09:03:04 niklas Exp $	*/

/*
 * Copyright (c) 1998 Constantine Paul Sapuntzakis
 * All rights reserved
 *
 * Author: Constantine Paul Sapuntzakis (csapuntz@cvs.openbsd.org)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The author's name or those of the contributors may be used to
 *    endorse or promote products derived from this software without 
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) AND CONTRIBUTORS
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

/*
 * PCI BIOS Configuration area ports
 */

enum {
  SV_SB_PORTBASE_SLOT = 0x10,
  SV_ENHANCED_PORTBASE_SLOT = 0x14,
  SV_FM_PORTBASE_SLOT = 0x18,
  SV_MIDI_PORTBASE_SLOT = 0x1c,
  SV_GAME_PORTBASE_SLOT = 0x20
};

/* 
 * Enhanced CODEC registers
 *     These are offset from the base specified in the PCI configuration area
 */
enum { SV_CODEC_CONTROL = 0,
       SV_CODEC_INTMASK = 1,
       SV_CODEC_STATUS = 2,
       SV_CODEC_IADDR = 4,
       SV_CODEC_IDATA = 5 };
       

/*
 * DMA Configuration register 
 */

enum {
  SV_DMAA_CONFIG_OFF = 0x40,
  SV_DMAC_CONFIG_OFF = 0x48
};

enum {
  SV_DMA_CHANNEL_ENABLE = 0x1,
  SV_DMAA_EXTENDED_ADDR = 0x8,
  SV_DMA_PORTBASE_MASK = 0xFFFFFFF0
};


enum {
  SV_DMA_ADDR0 = 0,
  SV_DMA_ADDR1 = 1,
  SV_DMA_ADDR2 = 2,
  SV_DMA_ADDR3 = 3,
  SV_DMA_COUNT0 = 4,
  SV_DMA_COUNT1 = 5,
  SV_DMA_COUNT2 = 6,
  SV_DMA_CMDSTATUS = 8,
  SV_DMA_MODE = 0xB,
  SV_DMA_MASTERCLEAR = 0xD,
  SV_DMA_MASK = 0xF
};


/*
 * DMA Mode (see reg 0xB)
 */

enum {
  SV_DMA_MODE_IOR_MASK = 0x0C,
  SV_DMA_MODE_IOW_MASK = 0x0C,
  SV_DMA_MODE_IOR = 0x04,
  SV_DMA_MODE_IOW = 0x08,
  SV_DMA_MODE_AUTOINIT = 0x10
};

#define SET_FIELD(reg, field) ((reg & ~(field##_MASK)) | field)
#define GET_FIELD(reg, field) (reg & ~(field##_MASK))

enum {
  SV_CTL_ENHANCED = 1,
  SV_CTL_FWS = 0x08,
  SV_CTL_INTA = 0x20,
  SV_CTL_RESET = 0x80
};

enum {
  SV_INTMASK_DMAA = 0x1,
  SV_INTMASK_DMAC = 0x4,
  SV_INTMASK_SINT = 0x8,
  SV_INTMASK_UD = 0x40,
  SV_INTMASK_MIDI = 0x80
};

enum {
  SV_INTSTATUS_DMAA = 0x1,
  SV_INTSTATUS_DMAC = 0x4,
  SV_INTSTATUS_SINT = 0x8,
  SV_INTSTATUS_UD = 0x40,
  SV_INTSTATUS_MIDI = 0x80
};

enum { 
  SV_IADDR_MASK = 0x3f,
  SV_IADDR_MCE = 0x40,
    /* TRD = DMA Transfer request disable */
  SV_IADDR_TRD = 0x80
};


enum {
  SV_LEFT_ADC_INPUT_CONTROL = 0x0,
  SV_RIGHT_ADC_INPUT_CONTROL = 0x1,
  SV_LEFT_AUX1_INPUT_CONTROL = 0x2,
  SV_RIGHT_AUX1_INPUT_CONTROL = 0x3,
  SV_LEFT_CD_INPUT_CONTROL = 0x4,
  SV_RIGHT_CD_INPUT_CONTROL = 0x5,
  SV_LEFT_LINE_IN_INPUT_CONTROL = 0x6,
  SV_RIGHT_LINE_IN_INPUT_CONTROL = 0x7,
  SV_MIC_INPUT_CONTROL = 0x8,
  SV_GAME_PORT_CONTROL = 0x9,
  SV_LEFT_SYNTH_INPUT_CONTROL = 0x0A,
  SV_RIGHT_SYNTH_INPUT_CONTROL = 0x0B,
  SV_LEFT_AUX2_INPUT_CONTROL = 0x0C,
  SV_RIGHT_AUX2_INPUT_CONTROL = 0x0D,
  SV_LEFT_MIXER_OUTPUT_CONTROL = 0x0E,
  SV_RIGHT_MIXER_OUTPUT_CONTROL = 0x0F,
  SV_LEFT_PCM_INPUT_CONTROL = 0x10,
  SV_RIGHT_PCM_INPUT_CONTROL = 0x11,
  SV_DMA_DATA_FORMAT = 0x12,
  SV_PLAY_RECORD_ENABLE = 0x13,
  SV_UP_DOWN_CONTROL = 0x14,
  SV_REVISION_LEVEL = 0x15,
  SV_MONITOR_CONTROL = 0x16,
  SV_DMAA_COUNT1 = 0x18,
  SV_DMAA_COUNT0 = 0x19,
  SV_DMAC_COUNT1 = 0x1C,
  SV_DMAC_COUNT0 = 0x1d,
  SV_PCM_SAMPLE_RATE_0 = 0x1e,
  SV_PCM_SAMPLE_RATE_1 = 0x1f,
  SV_SYNTH_SAMPLE_RATE_0 = 0x20,
  SV_SYNTH_SAMPLE_RATE_1 = 0x21,
  SV_ADC_CLOCK_SOURCE = 0x22,
  SV_ADC_ALT_SAMPLE_RATE = 0x23,
  SV_ADC_PLL_M = 0x24,
  SV_ADC_PLL_N = 0x25,
  SV_SYNTH_PLL_M = 0x26,
  SV_SYNTH_PLL_N = 0x27,
  SV_MPU401 = 0x2A,
  SV_DRIVE_CONTROL = 0x2B,
  SV_SRS_SPACE_CONTROL = 0x2c,
  SV_SRS_CENTER_CONTROL = 0x2d,
  SV_WAVETABLE_SOURCE_SELECT = 0x2e,
  SV_ANALOG_POWER_DOWN_CONTROL = 0x30,
  SV_DIGITAL_POWER_DOWN_CONTROL = 0x31
};

enum {
  SV_MUTE_BIT = 0x80,
  SV_AUX1_MASK = 0x1F,
  SV_CD_MASK = 0x1F,
  SV_LINE_IN_MASK = 0x1F,
  SV_MIC_MASK = 0x0F,
  SV_SYNTH_MASK = 0x1F,
  SV_AUX2_MASK = 0x1F,
  SV_MIXER_OUT_MASK = 0x1F,
  SV_PCM_MASK = 0x3F
};

enum {
  SV_DMAA_STEREO = 0x1,
  SV_DMAA_FORMAT16 = 0x2,
  SV_DMAC_STEREO = 0x10,
  SV_DMAC_FORMAT16 = 0x20
};

enum {
  SV_PLAY_ENABLE = 0x1,
  SV_RECORD_ENABLE = 0x2
};

enum {
  SV_PLL_R_SHIFT = 5
};

/* ADC input source (registers 0 & 1) */
enum {
  SV_REC_SOURCE_MASK = 0xE0,
  SV_REC_SOURCE_SHIFT = 5,
  SV_MIC_BOOST_BIT = 0x10,
  SV_REC_GAIN_MASK = 0x0F,
  SV_REC_CD = 1,
  SV_REC_DAC = 2,
  SV_REC_AUX2 = 3,
  SV_REC_LINE = 4,
  SV_REC_AUX1 = 5,
  SV_REC_MIC = 6,
  SV_REC_MIXER = 7
};

/* SRS Space control register (reg 0x2C) */

enum {
  SV_SRS_SPACE_ONOFF = 0x80
};
