/*
 *  The driver for the Cirrus Logic's Sound Fusion CS46XX based soundcards
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

#ifndef __CS46XX_DSP_SPOS_H__
#define __CS46XX_DSP_SPOS_H__

#include "cs46xx_dsp_scb_types.h"
#include "cs46xx_dsp_task_types.h"

#define SYMBOL_CONSTANT  0x0
#define SYMBOL_SAMPLE    0x1
#define SYMBOL_PARAMETER 0x2
#define SYMBOL_CODE      0x3

#define SEGTYPE_SP_PROGRAM              0x00000001
#define SEGTYPE_SP_PARAMETER            0x00000002
#define SEGTYPE_SP_SAMPLE               0x00000003
#define SEGTYPE_SP_COEFFICIENT          0x00000004

#define DSP_SPOS_UU      0x0deadul     /* unused */
#define DSP_SPOS_DC      0x0badul      /* don't care */
#define DSP_SPOS_DC_DC   0x0bad0badul  /* don't care */
#define DSP_SPOS_UUUU    0xdeadc0edul  /* unused */
#define DSP_SPOS_UUHI    0xdeadul
#define DSP_SPOS_UULO    0xc0edul
#define DSP_SPOS_DCDC    0x0badf1d0ul  /* don't care */
#define DSP_SPOS_DCDCHI  0x0badul
#define DSP_SPOS_DCDCLO  0xf1d0ul

#define DSP_MAX_TASK_NAME   60
#define DSP_MAX_SYMBOL_NAME 100
#define DSP_MAX_SCB_NAME    60
#define DSP_MAX_SCB_DESC    200
#define DSP_MAX_TASK_DESC   50

#define DSP_MAX_PCM_CHANNELS 32
#define DSP_MAX_SRC_NR       14

#define DSP_PCM_MAIN_CHANNEL        1
#define DSP_PCM_REAR_CHANNEL        2
#define DSP_PCM_CENTER_LFE_CHANNEL  3
#define DSP_PCM_S71_CHANNEL         4 /* surround 7.1 */
#define DSP_IEC958_CHANNEL          5

#define DSP_SPDIF_STATUS_OUTPUT_ENABLED       1
#define DSP_SPDIF_STATUS_PLAYBACK_OPEN        2
#define DSP_SPDIF_STATUS_HW_ENABLED           4
#define DSP_SPDIF_STATUS_INPUT_CTRL_ENABLED   8

struct _dsp_module_desc_t;

typedef struct _symbol_entry_t {
	u32 address;
	char symbol_name[DSP_MAX_SYMBOL_NAME];
	int symbol_type;

	/* initialized by driver */
	struct _dsp_module_desc_t * module;
	int deleted;
} symbol_entry_t;

typedef struct _symbol_desc_t {
	int nsymbols;

	symbol_entry_t * symbols;

	/* initialized by driver */
	int highest_frag_index;
} symbol_desc_t;


typedef struct _segment_desc_t {
	int segment_type;
	u32 offset;
	u32 size;
	u32 * data;
} segment_desc_t;

typedef struct _dsp_module_desc_t {
	char * module_name;
	symbol_desc_t symbol_table;
	int nsegments;
	segment_desc_t * segments;

	/* initialized by driver */
	u32 overlay_begin_address;
	u32 load_address;
	int nfixups;
} dsp_module_desc_t;

typedef struct _dsp_scb_descriptor_t {
	char scb_name[DSP_MAX_SCB_NAME];
	u32 address;
	int index;

	struct _dsp_scb_descriptor_t * sub_list_ptr;
	struct _dsp_scb_descriptor_t * next_scb_ptr;
	struct _dsp_scb_descriptor_t * parent_scb_ptr;

	symbol_entry_t * task_entry;
	symbol_entry_t * scb_symbol;

	snd_info_entry_t *proc_info;
	int ref_count;
	spinlock_t lock;

	int deleted;
} dsp_scb_descriptor_t;

typedef struct _dsp_task_descriptor_t {
	char task_name[DSP_MAX_TASK_NAME];
	int size;
	u32 address;
	int index;
} dsp_task_descriptor_t;

typedef struct _pcm_channel_descriptor_t {
	int active;
	int src_slot;
	int pcm_slot;
	u32 sample_rate;
	u32 unlinked;
	dsp_scb_descriptor_t * pcm_reader_scb;
	dsp_scb_descriptor_t * src_scb;
	dsp_scb_descriptor_t * mixer_scb;

	void * private_data;
} pcm_channel_descriptor_t;

typedef struct _dsp_spos_instance_t {
	symbol_desc_t symbol_table; /* currently availble loaded symbols in SP */

	int nmodules;
	dsp_module_desc_t * modules; /* modules loaded into SP */

	segment_desc_t code;

	/* Main PCM playback mixer */
	dsp_scb_descriptor_t * master_mix_scb;
	u16 dac_volume_right;
	u16 dac_volume_left;

	/* Rear/surround PCM playback mixer */
	dsp_scb_descriptor_t * rear_mix_scb;

	/* Center/LFE mixer */
	dsp_scb_descriptor_t * center_lfe_mix_scb;

	int npcm_channels;
	int nsrc_scb;
	pcm_channel_descriptor_t pcm_channels[DSP_MAX_PCM_CHANNELS];
	int src_scb_slots[DSP_MAX_SRC_NR];

	/* cache this symbols */
	symbol_entry_t * null_algorithm; /* used by PCMreaderSCB's */
	symbol_entry_t * s16_up;         /* used by SRCtaskSCB's */

	/* proc fs */  
	snd_card_t * snd_card;
	snd_info_entry_t * proc_dsp_dir;
	snd_info_entry_t * proc_sym_info_entry;
	snd_info_entry_t * proc_modules_info_entry;
	snd_info_entry_t * proc_parameter_dump_info_entry;
	snd_info_entry_t * proc_sample_dump_info_entry;

	/* SCB's descriptors */
	int nscb;
	int scb_highest_frag_index;
	dsp_scb_descriptor_t scbs[DSP_MAX_SCB_DESC];
	snd_info_entry_t * proc_scb_info_entry;
	dsp_scb_descriptor_t * the_null_scb;

	/* Task's descriptors */
	int ntask;
	dsp_task_descriptor_t tasks[DSP_MAX_TASK_DESC];
	snd_info_entry_t * proc_task_info_entry;

	/* SPDIF status */
	int spdif_status_out;
	int spdif_status_in;
	u16 spdif_input_volume_right;
	u16 spdif_input_volume_left;
	/* spdif channel status,
	   left right and user validity bits */
	unsigned int spdif_csuv_default;
	unsigned int spdif_csuv_stream;

	/* SPDIF input sample rate converter */
	dsp_scb_descriptor_t * spdif_in_src;
	/* SPDIF input asynch. receiver */
	dsp_scb_descriptor_t * asynch_rx_scb;

	/* Capture record mixer SCB */
	dsp_scb_descriptor_t * record_mixer_scb;
    
	/* CODEC input SCB */
	dsp_scb_descriptor_t * codec_in_scb;

	/* reference snooper */
	dsp_scb_descriptor_t * ref_snoop_scb;

	/* SPDIF output  PCM reference  */
	dsp_scb_descriptor_t * spdif_pcm_input_scb;

	/* asynch TX task */
	dsp_scb_descriptor_t * asynch_tx_scb;

	/* record sources */
	dsp_scb_descriptor_t * pcm_input;
	dsp_scb_descriptor_t * adc_input;

	int spdif_in_sample_rate;
} dsp_spos_instance_t;

#endif /* __DSP_SPOS_H__ */
