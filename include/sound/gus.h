#ifndef __SOUND_GUS_H
#define __SOUND_GUS_H

/*
 *  Global structures used for GUS part of ALSA driver
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *
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

#include "pcm.h"
#include "rawmidi.h"
#include "timer.h"
#include "seq_midi_emul.h"
#include "seq_device.h"
#include "ainstr_iw.h"
#include "ainstr_gf1.h"
#include "ainstr_simple.h"
#include <asm/io.h>

#define SNDRV_SEQ_DEV_ID_GUS			"gus-synth"

/* IO ports */

#define GUSP(gus, x)			((gus)->gf1.port + SNDRV_g_u_s_##x)

#define SNDRV_g_u_s_MIDICTRL		(0x320-0x220)
#define SNDRV_g_u_s_MIDISTAT		(0x320-0x220)
#define SNDRV_g_u_s_MIDIDATA		(0x321-0x220)

#define SNDRV_g_u_s_GF1PAGE		(0x322-0x220)
#define SNDRV_g_u_s_GF1REGSEL		(0x323-0x220)
#define SNDRV_g_u_s_GF1DATALOW		(0x324-0x220)
#define SNDRV_g_u_s_GF1DATAHIGH		(0x325-0x220)
#define SNDRV_g_u_s_IRQSTAT		(0x226-0x220)
#define SNDRV_g_u_s_TIMERCNTRL		(0x228-0x220)
#define SNDRV_g_u_s_TIMERDATA		(0x229-0x220)
#define SNDRV_g_u_s_DRAM			(0x327-0x220)
#define SNDRV_g_u_s_MIXCNTRLREG		(0x220-0x220)
#define SNDRV_g_u_s_IRQDMACNTRLREG	(0x22b-0x220)
#define SNDRV_g_u_s_REGCNTRLS		(0x22f-0x220)
#define SNDRV_g_u_s_BOARDVERSION		(0x726-0x220)
#define SNDRV_g_u_s_MIXCNTRLPORT		(0x726-0x220)
#define SNDRV_g_u_s_IVER			(0x325-0x220)
#define SNDRV_g_u_s_MIXDATAPORT		(0x326-0x220)
#define SNDRV_g_u_s_MAXCNTRLPORT		(0x326-0x220)

/* GF1 registers */

/* global registers */
#define SNDRV_GF1_GB_ACTIVE_VOICES		0x0e
#define SNDRV_GF1_GB_VOICES_IRQ			0x0f
#define SNDRV_GF1_GB_GLOBAL_MODE			0x19
#define SNDRV_GF1_GW_LFO_BASE			0x1a
#define SNDRV_GF1_GB_VOICES_IRQ_READ		0x1f
#define SNDRV_GF1_GB_DRAM_DMA_CONTROL		0x41
#define SNDRV_GF1_GW_DRAM_DMA_LOW			0x42
#define SNDRV_GF1_GW_DRAM_IO_LOW			0x43
#define SNDRV_GF1_GB_DRAM_IO_HIGH			0x44
#define SNDRV_GF1_GB_SOUND_BLASTER_CONTROL	0x45
#define SNDRV_GF1_GB_ADLIB_TIMER_1		0x46
#define SNDRV_GF1_GB_ADLIB_TIMER_2		0x47
#define SNDRV_GF1_GB_RECORD_RATE			0x48
#define SNDRV_GF1_GB_REC_DMA_CONTROL		0x49
#define SNDRV_GF1_GB_JOYSTICK_DAC_LEVEL		0x4b
#define SNDRV_GF1_GB_RESET			0x4c
#define SNDRV_GF1_GB_DRAM_DMA_HIGH		0x50
#define SNDRV_GF1_GW_DRAM_IO16			0x51
#define SNDRV_GF1_GW_MEMORY_CONFIG		0x52
#define SNDRV_GF1_GB_MEMORY_CONTROL		0x53
#define SNDRV_GF1_GW_FIFO_RECORD_BASE_ADDR	0x54
#define SNDRV_GF1_GW_FIFO_PLAY_BASE_ADDR		0x55
#define SNDRV_GF1_GW_FIFO_SIZE			0x56
#define SNDRV_GF1_GW_INTERLEAVE			0x57
#define SNDRV_GF1_GB_COMPATIBILITY		0x59
#define SNDRV_GF1_GB_DECODE_CONTROL		0x5a
#define SNDRV_GF1_GB_VERSION_NUMBER		0x5b
#define SNDRV_GF1_GB_MPU401_CONTROL_A		0x5c
#define SNDRV_GF1_GB_MPU401_CONTROL_B		0x5d
#define SNDRV_GF1_GB_EMULATION_IRQ		0x60
/* voice specific registers */
#define SNDRV_GF1_VB_ADDRESS_CONTROL		0x00
#define SNDRV_GF1_VW_FREQUENCY			0x01
#define SNDRV_GF1_VW_START_HIGH			0x02
#define SNDRV_GF1_VW_START_LOW			0x03
#define SNDRV_GF1_VA_START			SNDRV_GF1_VW_START_HIGH
#define SNDRV_GF1_VW_END_HIGH			0x04
#define SNDRV_GF1_VW_END_LOW			0x05
#define SNDRV_GF1_VA_END				SNDRV_GF1_VW_END_HIGH
#define SNDRV_GF1_VB_VOLUME_RATE			0x06
#define SNDRV_GF1_VB_VOLUME_START			0x07
#define SNDRV_GF1_VB_VOLUME_END			0x08
#define SNDRV_GF1_VW_VOLUME			0x09
#define SNDRV_GF1_VW_CURRENT_HIGH			0x0a
#define SNDRV_GF1_VW_CURRENT_LOW			0x0b
#define SNDRV_GF1_VA_CURRENT			SNDRV_GF1_VW_CURRENT_HIGH
#define SNDRV_GF1_VB_PAN				0x0c
#define SNDRV_GF1_VW_OFFSET_RIGHT			0x0c
#define SNDRV_GF1_VB_VOLUME_CONTROL		0x0d
#define SNDRV_GF1_VB_UPPER_ADDRESS		0x10
#define SNDRV_GF1_VW_EFFECT_HIGH			0x11
#define SNDRV_GF1_VW_EFFECT_LOW			0x12
#define SNDRV_GF1_VA_EFFECT			SNDRV_GF1_VW_EFFECT_HIGH
#define SNDRV_GF1_VW_OFFSET_LEFT			0x13
#define SNDRV_GF1_VB_ACCUMULATOR			0x14
#define SNDRV_GF1_VB_MODE				0x15
#define SNDRV_GF1_VW_EFFECT_VOLUME		0x16
#define SNDRV_GF1_VB_FREQUENCY_LFO		0x17
#define SNDRV_GF1_VB_VOLUME_LFO			0x18
#define SNDRV_GF1_VW_OFFSET_RIGHT_FINAL		0x1b
#define SNDRV_GF1_VW_OFFSET_LEFT_FINAL		0x1c
#define SNDRV_GF1_VW_EFFECT_VOLUME_FINAL		0x1d

/* ICS registers */

#define SNDRV_ICS_MIC_DEV		0
#define SNDRV_ICS_LINE_DEV	1
#define SNDRV_ICS_CD_DEV		2
#define SNDRV_ICS_GF1_DEV		3
#define SNDRV_ICS_NONE_DEV	4
#define SNDRV_ICS_MASTER_DEV	5

/* LFO */

#define SNDRV_LFO_TREMOLO		0
#define SNDRV_LFO_VIBRATO		1

/* misc */

#define SNDRV_GF1_DMA_UNSIGNED	0x80
#define SNDRV_GF1_DMA_16BIT	0x40
#define SNDRV_GF1_DMA_IRQ		0x20
#define SNDRV_GF1_DMA_WIDTH16	0x04
#define SNDRV_GF1_DMA_READ	0x02	/* read from GUS's DRAM */
#define SNDRV_GF1_DMA_ENABLE	0x01

/* ramp ranges */

#define SNDRV_GF1_ATTEN(x)	(snd_gf1_atten_table[x])
#define SNDRV_GF1_MIN_VOLUME	1800
#define SNDRV_GF1_MAX_VOLUME	4095
#define SNDRV_GF1_MIN_OFFSET	(SNDRV_GF1_MIN_VOLUME>>4)
#define SNDRV_GF1_MAX_OFFSET	255
#define SNDRV_GF1_MAX_TDEPTH	90

/* defines for memory manager */

#define SNDRV_GF1_MEM_BLOCK_16BIT		0x0001

#define SNDRV_GF1_MEM_OWNER_DRIVER	0x0001
#define SNDRV_GF1_MEM_OWNER_WAVE_SIMPLE	0x0002
#define SNDRV_GF1_MEM_OWNER_WAVE_GF1	0x0003
#define SNDRV_GF1_MEM_OWNER_WAVE_IWFFFF	0x0004

/* constants for interrupt handlers */

#define SNDRV_GF1_HANDLER_MIDI_OUT	0x00010000
#define SNDRV_GF1_HANDLER_MIDI_IN		0x00020000
#define SNDRV_GF1_HANDLER_TIMER1		0x00040000
#define SNDRV_GF1_HANDLER_TIMER2		0x00080000
#define SNDRV_GF1_HANDLER_VOICE		0x00100000
#define SNDRV_GF1_HANDLER_DMA_WRITE	0x00200000
#define SNDRV_GF1_HANDLER_DMA_READ	0x00400000
#define SNDRV_GF1_HANDLER_ALL		(0xffff0000&~SNDRV_GF1_HANDLER_VOICE)

/* constants for DMA flags */

#define SNDRV_GF1_DMA_TRIGGER		1

/* --- */

struct _snd_gus_card;
typedef struct _snd_gus_card snd_gus_card_t;

/* GF1 specific structure */

typedef struct _snd_gf1_bank_info {
	unsigned int address;
	unsigned int size;
} snd_gf1_bank_info_t;

typedef struct _snd_gf1_mem_block {
	unsigned short flags;	/* flags - SNDRV_GF1_MEM_BLOCK_XXXX */
	unsigned short owner;	/* owner - SNDRV_GF1_MEM_OWNER_XXXX */
	unsigned int share;	/* share count */
	unsigned int share_id[4]; /* share ID */
	unsigned int ptr;
	unsigned int size;
	char *name;
	struct _snd_gf1_mem_block *next;
	struct _snd_gf1_mem_block *prev;
} snd_gf1_mem_block_t;

typedef struct _snd_gf1_mem {
	snd_gf1_bank_info_t banks_8[4];
	snd_gf1_bank_info_t banks_16[4];
	snd_gf1_mem_block_t *first;
	snd_gf1_mem_block_t *last;
	struct semaphore memory_mutex;
} snd_gf1_mem_t;

typedef struct snd_gf1_dma_block {
	void *buffer;		/* buffer in computer's RAM */
	unsigned long buf_addr;	/* buffer address */
	unsigned int addr;	/* address in onboard memory */
	unsigned int count;	/* count in bytes */
	unsigned int cmd;	/* DMA command (format) */
	void (*ack)(snd_gus_card_t * gus, void *private_data);
	void *private_data;
	struct snd_gf1_dma_block *next;
} snd_gf1_dma_block_t;

typedef struct {
	snd_midi_channel_set_t * chset;
	snd_gus_card_t * gus;
	int mode;		/* operation mode */
	int client;		/* sequencer client number */
	int port;		/* sequencer port number */
	unsigned int midi_has_voices: 1;
} snd_gus_port_t;

typedef struct _snd_gus_voice snd_gus_voice_t;

typedef struct {
	void (*sample_start)(snd_gus_card_t *gus, snd_gus_voice_t *voice, snd_seq_position_t position);
	void (*sample_stop)(snd_gus_card_t *gus, snd_gus_voice_t *voice, snd_seq_stop_mode_t mode);
	void (*sample_freq)(snd_gus_card_t *gus, snd_gus_voice_t *voice, snd_seq_frequency_t freq);
	void (*sample_volume)(snd_gus_card_t *gus, snd_gus_voice_t *voice, snd_seq_ev_volume_t *volume);
	void (*sample_loop)(snd_gus_card_t *card, snd_gus_voice_t *voice, snd_seq_ev_loop_t *loop);
	void (*sample_pos)(snd_gus_card_t *card, snd_gus_voice_t *voice, snd_seq_position_t position);
	void (*sample_private1)(snd_gus_card_t *card, snd_gus_voice_t *voice, unsigned char *data);
} snd_gus_sample_ops_t;

#define SNDRV_GF1_VOICE_TYPE_PCM		0
#define SNDRV_GF1_VOICE_TYPE_SYNTH 	1
#define SNDRV_GF1_VOICE_TYPE_MIDI		2

#define SNDRV_GF1_VFLG_RUNNING		(1<<0)
#define SNDRV_GF1_VFLG_EFFECT_TIMER1	(1<<1)
#define SNDRV_GF1_VFLG_PAN		(1<<2)

typedef enum {
	VENV_BEFORE,
	VENV_ATTACK,
	VENV_SUSTAIN,
	VENV_RELEASE,
	VENV_DONE,
	VENV_VOLUME
} snd_gus_volume_state_t;

struct _snd_gus_voice {
	int number;
	unsigned int use: 1,
	    pcm: 1,
	    synth:1,
	    midi: 1;
	unsigned int flags;
	unsigned char client;
	unsigned char port;
	unsigned char index;
	unsigned char pad;
	
#ifdef CONFIG_SND_DEBUG
	unsigned int interrupt_stat_wave;
	unsigned int interrupt_stat_volume;
#endif
	void (*handler_wave) (snd_gus_card_t * gus, snd_gus_voice_t * voice);
	void (*handler_volume) (snd_gus_card_t * gus, snd_gus_voice_t * voice);
	void (*handler_effect) (snd_gus_card_t * gus, snd_gus_voice_t * voice);
	void (*volume_change) (snd_gus_card_t * gus);

	snd_gus_sample_ops_t *sample_ops;

	snd_seq_instr_t instr;

	/* running status / registers */

	snd_seq_ev_volume_t sample_volume;

	unsigned short fc_register;
	unsigned short fc_lfo;
	unsigned short gf1_volume;
	unsigned char control;
	unsigned char mode;
	unsigned char gf1_pan;
	unsigned char effect_accumulator;
	unsigned char volume_control;
	unsigned char venv_value_next;
	snd_gus_volume_state_t venv_state;
	snd_gus_volume_state_t venv_state_prev;
	unsigned short vlo;
	unsigned short vro;
	unsigned short gf1_effect_volume;
	
	/* --- */

	void *private_data;
	void (*private_free)(snd_gus_voice_t *voice);
};

struct _snd_gf1 {

	unsigned int enh_mode:1,	/* enhanced mode (GFA1) */
		     hw_lfo:1,		/* use hardware LFO */
		     sw_lfo:1,		/* use software LFO */
		     effect:1;		/* use effect voices */

	unsigned long port;		/* port of GF1 chip */
	struct resource *res_port1;
	struct resource *res_port2;
	int irq;			/* IRQ number */
	int dma1;			/* DMA1 number */
	int dma2;			/* DMA2 number */
	unsigned int memory;		/* GUS's DRAM size in bytes */
	unsigned int rom_memory;	/* GUS's ROM size in bytes */
	unsigned int rom_present;	/* bitmask */
	unsigned int rom_banks;		/* GUS's ROM banks */

	snd_gf1_mem_t mem_alloc;

	/* registers */
	unsigned short reg_page;
	unsigned short reg_regsel;
	unsigned short reg_data8;
	unsigned short reg_data16;
	unsigned short reg_irqstat;
	unsigned short reg_dram;
	unsigned short reg_timerctrl;
	unsigned short reg_timerdata;
	unsigned char ics_regs[6][2];
	/* --------- */

	unsigned char active_voices;	/* active voices */
	unsigned char active_voice;	/* selected voice (GF1PAGE register) */

	snd_gus_voice_t voices[32];	/* GF1 voices */

	unsigned int default_voice_address;

	unsigned short playback_freq;	/* GF1 playback (mixing) frequency */
	unsigned short mode;		/* see to SNDRV_GF1_MODE_XXXX */
	unsigned char volume_ramp;
	unsigned char smooth_pan;
	unsigned char full_range_pan;
	unsigned char pad0;

	unsigned char *lfos;

	/* interrupt handlers */

	void (*interrupt_handler_midi_out) (snd_gus_card_t * gus);
	void (*interrupt_handler_midi_in) (snd_gus_card_t * gus);
	void (*interrupt_handler_timer1) (snd_gus_card_t * gus);
	void (*interrupt_handler_timer2) (snd_gus_card_t * gus);
	void (*interrupt_handler_dma_write) (snd_gus_card_t * gus);
	void (*interrupt_handler_dma_read) (snd_gus_card_t * gus);

#ifdef CONFIG_SND_DEBUG
	unsigned int interrupt_stat_midi_out;
	unsigned int interrupt_stat_midi_in;
	unsigned int interrupt_stat_timer1;
	unsigned int interrupt_stat_timer2;
	unsigned int interrupt_stat_dma_write;
	unsigned int interrupt_stat_dma_read;
	unsigned int interrupt_stat_voice_lost;
#endif

	/* synthesizer */

	int seq_client;
	snd_gus_port_t seq_ports[4];
	snd_seq_kinstr_list_t *ilist;
	snd_iwffff_ops_t iwffff_ops;
	snd_gf1_ops_t gf1_ops;
	snd_simple_ops_t simple_ops;

	/* timer */

	unsigned short timer_enabled;
	snd_timer_t *timer1;
	snd_timer_t *timer2;

	/* midi */

	unsigned short uart_cmd;
	unsigned int uart_framing;
	unsigned int uart_overrun;

	/* dma operations */

	unsigned int dma_flags;
	unsigned int dma_shared;
	snd_gf1_dma_block_t *dma_data_pcm;
	snd_gf1_dma_block_t *dma_data_pcm_last;
	snd_gf1_dma_block_t *dma_data_synth;
	snd_gf1_dma_block_t *dma_data_synth_last;
	void (*dma_ack)(snd_gus_card_t * gus, void *private_data);
	void *dma_private_data;

	/* pcm */
	int pcm_channels;
	int pcm_alloc_voices;
        unsigned short pcm_volume_level_left;
	unsigned short pcm_volume_level_right;
	unsigned short pcm_volume_level_left1;
	unsigned short pcm_volume_level_right1;
                                
	unsigned char pcm_rcntrl_reg;
	unsigned char pad_end;
};

/* main structure for GUS card */

struct _snd_gus_card {
	snd_card_t *card;

	unsigned int
	 initialized: 1,		/* resources were initialized */
	 equal_irq:1,			/* GF1 and CODEC shares IRQ (GUS MAX only) */
	 equal_dma:1,			/* if dma channels are equal (not valid for daughter board) */
	 ics_flag:1,			/* have we ICS mixer chip */
	 ics_flipped:1,			/* ICS mixer have flipped some channels? */
	 codec_flag:1,			/* have we CODEC chip? */
	 max_flag:1,			/* have we GUS MAX card? */
	 max_ctrl_flag:1,		/* have we original GUS MAX card? */
	 daughter_flag:1,		/* have we daughter board? */
	 interwave:1,			/* hey - we have InterWave card */
	 ess_flag:1,			/* ESS chip found... GUS Extreme */
	 ace_flag:1,			/* GUS ACE detected */
	 uart_enable:1;			/* enable MIDI UART */
	unsigned short revision;	/* revision of chip */
	unsigned short max_cntrl_val;	/* GUS MAX control value */
	unsigned short mix_cntrl_reg;	/* mixer control register */
	unsigned short joystick_dac;	/* joystick DAC level */
	int timer_dev;			/* timer device */

	struct _snd_gf1 gf1;	/* gf1 specific variables */
	snd_pcm_t *pcm;
	snd_pcm_substream_t *pcm_cap_substream;
	unsigned int c_dma_size;
	unsigned int c_period_size;
	unsigned int c_pos;

	snd_rawmidi_t *midi_uart;
	snd_rawmidi_substream_t *midi_substream_output;
	snd_rawmidi_substream_t *midi_substream_input;

	snd_seq_device_t *seq_dev;

	spinlock_t reg_lock;
	spinlock_t voice_alloc;
	spinlock_t active_voice_lock;
	spinlock_t event_lock;
	spinlock_t dma_lock;
	spinlock_t pcm_volume_level_lock;
	spinlock_t uart_cmd_lock;
	struct semaphore dma_mutex;
	struct semaphore register_mutex;
};

/* I/O functions for GF1/InterWave chip - gus_io.c */

static inline void snd_gf1_select_voice(snd_gus_card_t * gus, int voice)
{
	unsigned long flags;

	spin_lock_irqsave(&gus->active_voice_lock, flags);
	if (voice != gus->gf1.active_voice) {
		gus->gf1.active_voice = voice;
		outb(voice, GUSP(gus, GF1PAGE));
	}
	spin_unlock_irqrestore(&gus->active_voice_lock, flags);
}

static inline void snd_gf1_uart_cmd(snd_gus_card_t * gus, unsigned char b)
{
	outb(gus->gf1.uart_cmd = b, GUSP(gus, MIDICTRL));
}

static inline unsigned char snd_gf1_uart_stat(snd_gus_card_t * gus)
{
	return inb(GUSP(gus, MIDISTAT));
}

static inline void snd_gf1_uart_put(snd_gus_card_t * gus, unsigned char b)
{
	outb(b, GUSP(gus, MIDIDATA));
}

static inline unsigned char snd_gf1_uart_get(snd_gus_card_t * gus)
{
	return inb(GUSP(gus, MIDIDATA));
}

extern void snd_gf1_delay(snd_gus_card_t * gus);

extern void snd_gf1_ctrl_stop(snd_gus_card_t * gus, unsigned char reg);

extern void snd_gf1_write8(snd_gus_card_t * gus, unsigned char reg, unsigned char data);
extern unsigned char snd_gf1_look8(snd_gus_card_t * gus, unsigned char reg);
static inline unsigned char snd_gf1_read8(snd_gus_card_t * gus, unsigned char reg)
{
	return snd_gf1_look8(gus, reg | 0x80);
}
extern void snd_gf1_write16(snd_gus_card_t * gus, unsigned char reg, unsigned int data);
extern unsigned short snd_gf1_look16(snd_gus_card_t * gus, unsigned char reg);
static inline unsigned short snd_gf1_read16(snd_gus_card_t * gus, unsigned char reg)
{
	return snd_gf1_look16(gus, reg | 0x80);
}
extern void snd_gf1_adlib_write(snd_gus_card_t * gus, unsigned char reg, unsigned char data);
extern void snd_gf1_dram_addr(snd_gus_card_t * gus, unsigned int addr);
extern void snd_gf1_poke(snd_gus_card_t * gus, unsigned int addr, unsigned char data);
extern unsigned char snd_gf1_peek(snd_gus_card_t * gus, unsigned int addr);
extern void snd_gf1_write_addr(snd_gus_card_t * gus, unsigned char reg, unsigned int addr, short w_16bit);
extern unsigned int snd_gf1_read_addr(snd_gus_card_t * gus, unsigned char reg, short w_16bit);
extern void snd_gf1_i_ctrl_stop(snd_gus_card_t * gus, unsigned char reg);
extern void snd_gf1_i_write8(snd_gus_card_t * gus, unsigned char reg, unsigned char data);
extern unsigned char snd_gf1_i_look8(snd_gus_card_t * gus, unsigned char reg);
extern void snd_gf1_i_write16(snd_gus_card_t * gus, unsigned char reg, unsigned int data);
static inline unsigned char snd_gf1_i_read8(snd_gus_card_t * gus, unsigned char reg)
{
	return snd_gf1_i_look8(gus, reg | 0x80);
}
extern unsigned short snd_gf1_i_look16(snd_gus_card_t * gus, unsigned char reg);
static inline unsigned short snd_gf1_i_read16(snd_gus_card_t * gus, unsigned char reg)
{
	return snd_gf1_i_look16(gus, reg | 0x80);
}

extern void snd_gf1_select_active_voices(snd_gus_card_t * gus);

/* gus_lfo.c */

struct _SND_IW_LFO_PROGRAM {
	unsigned short freq_and_control;
	unsigned char depth_final;
	unsigned char depth_inc;
	unsigned short twave;
	unsigned short depth;
};

#if 0
extern irqreturn_t snd_gf1_lfo_effect_interrupt(snd_gus_card_t * gus, snd_gf1_voice_t * voice);
#endif
extern void snd_gf1_lfo_init(snd_gus_card_t * gus);
extern void snd_gf1_lfo_done(snd_gus_card_t * gus);
extern void snd_gf1_lfo_program(snd_gus_card_t * gus, int voice, int lfo_type, struct _SND_IW_LFO_PROGRAM *program);
extern void snd_gf1_lfo_enable(snd_gus_card_t * gus, int voice, int lfo_type);
extern void snd_gf1_lfo_disable(snd_gus_card_t * gus, int voice, int lfo_type);
extern void snd_gf1_lfo_change_freq(snd_gus_card_t * gus, int voice, int lfo_type, int freq);
extern void snd_gf1_lfo_change_depth(snd_gus_card_t * gus, int voice, int lfo_type, int depth);
extern void snd_gf1_lfo_setup(snd_gus_card_t * gus, int voice, int lfo_type, int freq, int current_depth, int depth, int sweep, int shape);
extern void snd_gf1_lfo_shutdown(snd_gus_card_t * gus, int voice, int lfo_type);
#if 0
extern void snd_gf1_lfo_command(snd_gus_card_t * gus, int voice, unsigned char *command);
#endif

/* gus_mem.c */

void snd_gf1_mem_lock(snd_gf1_mem_t * alloc, int xup);
int snd_gf1_mem_xfree(snd_gf1_mem_t * alloc, snd_gf1_mem_block_t * block);
snd_gf1_mem_block_t *snd_gf1_mem_alloc(snd_gf1_mem_t * alloc, int owner,
				       char *name, int size, int w_16,
				       int align, unsigned int *share_id);
int snd_gf1_mem_free(snd_gf1_mem_t * alloc, unsigned int address);
int snd_gf1_mem_free_owner(snd_gf1_mem_t * alloc, int owner);
int snd_gf1_mem_init(snd_gus_card_t * gus);
int snd_gf1_mem_done(snd_gus_card_t * gus);

/* gus_mem_proc.c */

int snd_gf1_mem_proc_init(snd_gus_card_t * gus);

/* gus_dma.c */

int snd_gf1_dma_init(snd_gus_card_t * gus);
int snd_gf1_dma_done(snd_gus_card_t * gus);
int snd_gf1_dma_transfer_block(snd_gus_card_t * gus,
			       snd_gf1_dma_block_t * block,
			       int atomic,
			       int synth);

/* gus_volume.c */

unsigned short snd_gf1_lvol_to_gvol_raw(unsigned int vol);
unsigned short snd_gf1_translate_freq(snd_gus_card_t * gus, unsigned int freq2);

/* gus_reset.c */

void snd_gf1_set_default_handlers(snd_gus_card_t * gus, unsigned int what);
void snd_gf1_smart_stop_voice(snd_gus_card_t * gus, unsigned short voice);
void snd_gf1_stop_voice(snd_gus_card_t * gus, unsigned short voice);
void snd_gf1_stop_voices(snd_gus_card_t * gus, unsigned short v_min, unsigned short v_max);
snd_gus_voice_t *snd_gf1_alloc_voice(snd_gus_card_t * gus, int type, int client, int port);
void snd_gf1_free_voice(snd_gus_card_t * gus, snd_gus_voice_t *voice);
int snd_gf1_start(snd_gus_card_t * gus);
int snd_gf1_stop(snd_gus_card_t * gus);

/* gus_mixer.c */

int snd_gf1_new_mixer(snd_gus_card_t * gus);

/* gus_pcm.c */

int snd_gf1_pcm_new(snd_gus_card_t * gus, int pcm_dev, int control_index, snd_pcm_t ** rpcm);

#ifdef CONFIG_SND_DEBUG
extern void snd_gf1_print_voice_registers(snd_gus_card_t * gus);
#endif

/* gus.c */

int snd_gus_use_inc(snd_gus_card_t * gus);
void snd_gus_use_dec(snd_gus_card_t * gus);
int snd_gus_create(snd_card_t * card,
		   unsigned long port,
		   int irq, int dma1, int dma2,
		   int timer_dev,
		   int voices,
		   int pcm_channels,
		   int effect,
		   snd_gus_card_t ** rgus);
int snd_gus_initialize(snd_gus_card_t * gus);

/* gus_irq.c */

irqreturn_t snd_gus_interrupt(int irq, void *dev_id, struct pt_regs *regs);
#ifdef CONFIG_SND_DEBUG
void snd_gus_irq_profile_init(snd_gus_card_t *gus);
#endif

/* gus_uart.c */

int snd_gf1_rawmidi_new(snd_gus_card_t * gus, int device, snd_rawmidi_t **rrawmidi);

#if 0
extern void snd_engine_instrument_register(unsigned short mode,
		struct _SND_INSTRUMENT_VOICE_COMMANDS *voice_cmds,
		struct _SND_INSTRUMENT_NOTE_COMMANDS *note_cmds,
	      	struct _SND_INSTRUMENT_CHANNEL_COMMANDS *channel_cmds);
extern int snd_engine_instrument_register_ask(unsigned short mode);
#endif

/* gus_dram.c */
int snd_gus_dram_write(snd_gus_card_t *gus, char __user *ptr,
		       unsigned int addr, unsigned int size);
int snd_gus_dram_read(snd_gus_card_t *gus, char __user *ptr,
		      unsigned int addr, unsigned int size, int rom);

#if defined(CONFIG_SND_SEQUENCER) || defined(CONFIG_SND_SEQUENCER_MODULE)

/* gus_sample.c */
void snd_gus_sample_event(snd_seq_event_t *ev, snd_gus_port_t *p);

/* gus_simple.c */
void snd_gf1_simple_init(snd_gus_voice_t *voice);

/* gus_instr.c */
int snd_gus_iwffff_put_sample(void *private_data, iwffff_wave_t *wave,
			      char __user *data, long len, int atomic);
int snd_gus_iwffff_get_sample(void *private_data, iwffff_wave_t *wave,
			      char __user *data, long len, int atomic);
int snd_gus_iwffff_remove_sample(void *private_data, iwffff_wave_t *wave,
				 int atomic);
int snd_gus_gf1_put_sample(void *private_data, gf1_wave_t *wave,
			   char __user *data, long len, int atomic);
int snd_gus_gf1_get_sample(void *private_data, gf1_wave_t *wave,
			   char __user *data, long len, int atomic);
int snd_gus_gf1_remove_sample(void *private_data, gf1_wave_t *wave,
			      int atomic);
int snd_gus_simple_put_sample(void *private_data, simple_instrument_t *instr,
			      char __user *data, long len, int atomic);
int snd_gus_simple_get_sample(void *private_data, simple_instrument_t *instr,
			      char __user *data, long len, int atomic);
int snd_gus_simple_remove_sample(void *private_data, simple_instrument_t *instr,
				 int atomic);

#endif /* CONFIG_SND_SEQUENCER */

#endif /* __SOUND_GUS_H */
