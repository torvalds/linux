#ifndef __SOUND_PCM_H
#define __SOUND_PCM_H

/*
 *  Digital Audio (PCM) abstract layer
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *                   Abramo Bagnara <abramo@alsa-project.org>
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

#include <sound/asound.h>
#include <sound/memalloc.h>
#include <sound/minors.h>
#include <linux/poll.h>
#include <linux/mm.h>
#include <linux/bitops.h>
#include <linux/pm_qos.h>

#define snd_pcm_substream_chip(substream) ((substream)->private_data)
#define snd_pcm_chip(pcm) ((pcm)->private_data)

#if defined(CONFIG_SND_PCM_OSS) || defined(CONFIG_SND_PCM_OSS_MODULE)
#include "pcm_oss.h"
#endif

/*
 *  Hardware (lowlevel) section
 */

struct snd_pcm_hardware {
	unsigned int info;		/* SNDRV_PCM_INFO_* */
	u64 formats;			/* SNDRV_PCM_FMTBIT_* */
	unsigned int rates;		/* SNDRV_PCM_RATE_* */
	unsigned int rate_min;		/* min rate */
	unsigned int rate_max;		/* max rate */
	unsigned int channels_min;	/* min channels */
	unsigned int channels_max;	/* max channels */
	size_t buffer_bytes_max;	/* max buffer size */
	size_t period_bytes_min;	/* min period size */
	size_t period_bytes_max;	/* max period size */
	unsigned int periods_min;	/* min # of periods */
	unsigned int periods_max;	/* max # of periods */
	size_t fifo_size;		/* fifo size in bytes */
};

struct snd_pcm_substream;

struct snd_pcm_ops {
	int (*open)(struct snd_pcm_substream *substream);
	int (*close)(struct snd_pcm_substream *substream);
	int (*ioctl)(struct snd_pcm_substream * substream,
		     unsigned int cmd, void *arg);
	int (*hw_params)(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params);
	int (*hw_free)(struct snd_pcm_substream *substream);
	int (*prepare)(struct snd_pcm_substream *substream);
	int (*trigger)(struct snd_pcm_substream *substream, int cmd);
	snd_pcm_uframes_t (*pointer)(struct snd_pcm_substream *substream);
	int (*copy)(struct snd_pcm_substream *substream, int channel,
		    snd_pcm_uframes_t pos,
		    void __user *buf, snd_pcm_uframes_t count);
	int (*silence)(struct snd_pcm_substream *substream, int channel, 
		       snd_pcm_uframes_t pos, snd_pcm_uframes_t count);
	struct page *(*page)(struct snd_pcm_substream *substream,
			     unsigned long offset);
	int (*mmap)(struct snd_pcm_substream *substream, struct vm_area_struct *vma);
	int (*ack)(struct snd_pcm_substream *substream);
};

/*
 *
 */

#if defined(CONFIG_SND_DYNAMIC_MINORS)
#define SNDRV_PCM_DEVICES	(SNDRV_OS_MINORS-2)
#else
#define SNDRV_PCM_DEVICES	8
#endif

#define SNDRV_PCM_IOCTL1_FALSE		((void *)0)
#define SNDRV_PCM_IOCTL1_TRUE		((void *)1)

#define SNDRV_PCM_IOCTL1_RESET		0
#define SNDRV_PCM_IOCTL1_INFO		1
#define SNDRV_PCM_IOCTL1_CHANNEL_INFO	2
#define SNDRV_PCM_IOCTL1_GSTATE		3
#define SNDRV_PCM_IOCTL1_FIFO_SIZE	4

#define SNDRV_PCM_TRIGGER_STOP		0
#define SNDRV_PCM_TRIGGER_START		1
#define SNDRV_PCM_TRIGGER_PAUSE_PUSH	3
#define SNDRV_PCM_TRIGGER_PAUSE_RELEASE	4
#define SNDRV_PCM_TRIGGER_SUSPEND	5
#define SNDRV_PCM_TRIGGER_RESUME	6

#define SNDRV_PCM_POS_XRUN		((snd_pcm_uframes_t)-1)

/* If you change this don't forget to change rates[] table in pcm_native.c */
#define SNDRV_PCM_RATE_5512		(1<<0)		/* 5512Hz */
#define SNDRV_PCM_RATE_8000		(1<<1)		/* 8000Hz */
#define SNDRV_PCM_RATE_11025		(1<<2)		/* 11025Hz */
#define SNDRV_PCM_RATE_16000		(1<<3)		/* 16000Hz */
#define SNDRV_PCM_RATE_22050		(1<<4)		/* 22050Hz */
#define SNDRV_PCM_RATE_32000		(1<<5)		/* 32000Hz */
#define SNDRV_PCM_RATE_44100		(1<<6)		/* 44100Hz */
#define SNDRV_PCM_RATE_48000		(1<<7)		/* 48000Hz */
#define SNDRV_PCM_RATE_64000		(1<<8)		/* 64000Hz */
#define SNDRV_PCM_RATE_88200		(1<<9)		/* 88200Hz */
#define SNDRV_PCM_RATE_96000		(1<<10)		/* 96000Hz */
#define SNDRV_PCM_RATE_176400		(1<<11)		/* 176400Hz */
#define SNDRV_PCM_RATE_192000		(1<<12)		/* 192000Hz */

#define SNDRV_PCM_RATE_CONTINUOUS	(1<<30)		/* continuous range */
#define SNDRV_PCM_RATE_KNOT		(1<<31)		/* supports more non-continuos rates */

#define SNDRV_PCM_RATE_8000_44100	(SNDRV_PCM_RATE_8000|SNDRV_PCM_RATE_11025|\
					 SNDRV_PCM_RATE_16000|SNDRV_PCM_RATE_22050|\
					 SNDRV_PCM_RATE_32000|SNDRV_PCM_RATE_44100)
#define SNDRV_PCM_RATE_8000_48000	(SNDRV_PCM_RATE_8000_44100|SNDRV_PCM_RATE_48000)
#define SNDRV_PCM_RATE_8000_96000	(SNDRV_PCM_RATE_8000_48000|SNDRV_PCM_RATE_64000|\
					 SNDRV_PCM_RATE_88200|SNDRV_PCM_RATE_96000)
#define SNDRV_PCM_RATE_8000_192000	(SNDRV_PCM_RATE_8000_96000|SNDRV_PCM_RATE_176400|\
					 SNDRV_PCM_RATE_192000)
#define _SNDRV_PCM_FMTBIT(fmt)		(1ULL << (__force int)SNDRV_PCM_FORMAT_##fmt)
#define SNDRV_PCM_FMTBIT_S8		_SNDRV_PCM_FMTBIT(S8)
#define SNDRV_PCM_FMTBIT_U8		_SNDRV_PCM_FMTBIT(U8)
#define SNDRV_PCM_FMTBIT_S16_LE		_SNDRV_PCM_FMTBIT(S16_LE)
#define SNDRV_PCM_FMTBIT_S16_BE		_SNDRV_PCM_FMTBIT(S16_BE)
#define SNDRV_PCM_FMTBIT_U16_LE		_SNDRV_PCM_FMTBIT(U16_LE)
#define SNDRV_PCM_FMTBIT_U16_BE		_SNDRV_PCM_FMTBIT(U16_BE)
#define SNDRV_PCM_FMTBIT_S24_LE		_SNDRV_PCM_FMTBIT(S24_LE)
#define SNDRV_PCM_FMTBIT_S24_BE		_SNDRV_PCM_FMTBIT(S24_BE)
#define SNDRV_PCM_FMTBIT_U24_LE		_SNDRV_PCM_FMTBIT(U24_LE)
#define SNDRV_PCM_FMTBIT_U24_BE		_SNDRV_PCM_FMTBIT(U24_BE)
#define SNDRV_PCM_FMTBIT_S32_LE		_SNDRV_PCM_FMTBIT(S32_LE)
#define SNDRV_PCM_FMTBIT_S32_BE		_SNDRV_PCM_FMTBIT(S32_BE)
#define SNDRV_PCM_FMTBIT_U32_LE		_SNDRV_PCM_FMTBIT(U32_LE)
#define SNDRV_PCM_FMTBIT_U32_BE		_SNDRV_PCM_FMTBIT(U32_BE)
#define SNDRV_PCM_FMTBIT_FLOAT_LE	_SNDRV_PCM_FMTBIT(FLOAT_LE)
#define SNDRV_PCM_FMTBIT_FLOAT_BE	_SNDRV_PCM_FMTBIT(FLOAT_BE)
#define SNDRV_PCM_FMTBIT_FLOAT64_LE	_SNDRV_PCM_FMTBIT(FLOAT64_LE)
#define SNDRV_PCM_FMTBIT_FLOAT64_BE	_SNDRV_PCM_FMTBIT(FLOAT64_BE)
#define SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE _SNDRV_PCM_FMTBIT(IEC958_SUBFRAME_LE)
#define SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_BE _SNDRV_PCM_FMTBIT(IEC958_SUBFRAME_BE)
#define SNDRV_PCM_FMTBIT_MU_LAW		_SNDRV_PCM_FMTBIT(MU_LAW)
#define SNDRV_PCM_FMTBIT_A_LAW		_SNDRV_PCM_FMTBIT(A_LAW)
#define SNDRV_PCM_FMTBIT_IMA_ADPCM	_SNDRV_PCM_FMTBIT(IMA_ADPCM)
#define SNDRV_PCM_FMTBIT_MPEG		_SNDRV_PCM_FMTBIT(MPEG)
#define SNDRV_PCM_FMTBIT_GSM		_SNDRV_PCM_FMTBIT(GSM)
#define SNDRV_PCM_FMTBIT_SPECIAL	_SNDRV_PCM_FMTBIT(SPECIAL)
#define SNDRV_PCM_FMTBIT_S24_3LE	_SNDRV_PCM_FMTBIT(S24_3LE)
#define SNDRV_PCM_FMTBIT_U24_3LE	_SNDRV_PCM_FMTBIT(U24_3LE)
#define SNDRV_PCM_FMTBIT_S24_3BE	_SNDRV_PCM_FMTBIT(S24_3BE)
#define SNDRV_PCM_FMTBIT_U24_3BE	_SNDRV_PCM_FMTBIT(U24_3BE)
#define SNDRV_PCM_FMTBIT_S20_3LE	_SNDRV_PCM_FMTBIT(S20_3LE)
#define SNDRV_PCM_FMTBIT_U20_3LE	_SNDRV_PCM_FMTBIT(U20_3LE)
#define SNDRV_PCM_FMTBIT_S20_3BE	_SNDRV_PCM_FMTBIT(S20_3BE)
#define SNDRV_PCM_FMTBIT_U20_3BE	_SNDRV_PCM_FMTBIT(U20_3BE)
#define SNDRV_PCM_FMTBIT_S18_3LE	_SNDRV_PCM_FMTBIT(S18_3LE)
#define SNDRV_PCM_FMTBIT_U18_3LE	_SNDRV_PCM_FMTBIT(U18_3LE)
#define SNDRV_PCM_FMTBIT_S18_3BE	_SNDRV_PCM_FMTBIT(S18_3BE)
#define SNDRV_PCM_FMTBIT_U18_3BE	_SNDRV_PCM_FMTBIT(U18_3BE)
#define SNDRV_PCM_FMTBIT_G723_24	_SNDRV_PCM_FMTBIT(G723_24)
#define SNDRV_PCM_FMTBIT_G723_24_1B	_SNDRV_PCM_FMTBIT(G723_24_1B)
#define SNDRV_PCM_FMTBIT_G723_40	_SNDRV_PCM_FMTBIT(G723_40)
#define SNDRV_PCM_FMTBIT_G723_40_1B	_SNDRV_PCM_FMTBIT(G723_40_1B)

#ifdef SNDRV_LITTLE_ENDIAN
#define SNDRV_PCM_FMTBIT_S16		SNDRV_PCM_FMTBIT_S16_LE
#define SNDRV_PCM_FMTBIT_U16		SNDRV_PCM_FMTBIT_U16_LE
#define SNDRV_PCM_FMTBIT_S24		SNDRV_PCM_FMTBIT_S24_LE
#define SNDRV_PCM_FMTBIT_U24		SNDRV_PCM_FMTBIT_U24_LE
#define SNDRV_PCM_FMTBIT_S32		SNDRV_PCM_FMTBIT_S32_LE
#define SNDRV_PCM_FMTBIT_U32		SNDRV_PCM_FMTBIT_U32_LE
#define SNDRV_PCM_FMTBIT_FLOAT		SNDRV_PCM_FMTBIT_FLOAT_LE
#define SNDRV_PCM_FMTBIT_FLOAT64	SNDRV_PCM_FMTBIT_FLOAT64_LE
#define SNDRV_PCM_FMTBIT_IEC958_SUBFRAME SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE
#endif
#ifdef SNDRV_BIG_ENDIAN
#define SNDRV_PCM_FMTBIT_S16		SNDRV_PCM_FMTBIT_S16_BE
#define SNDRV_PCM_FMTBIT_U16		SNDRV_PCM_FMTBIT_U16_BE
#define SNDRV_PCM_FMTBIT_S24		SNDRV_PCM_FMTBIT_S24_BE
#define SNDRV_PCM_FMTBIT_U24		SNDRV_PCM_FMTBIT_U24_BE
#define SNDRV_PCM_FMTBIT_S32		SNDRV_PCM_FMTBIT_S32_BE
#define SNDRV_PCM_FMTBIT_U32		SNDRV_PCM_FMTBIT_U32_BE
#define SNDRV_PCM_FMTBIT_FLOAT		SNDRV_PCM_FMTBIT_FLOAT_BE
#define SNDRV_PCM_FMTBIT_FLOAT64	SNDRV_PCM_FMTBIT_FLOAT64_BE
#define SNDRV_PCM_FMTBIT_IEC958_SUBFRAME SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_BE
#endif

struct snd_pcm_file {
	struct snd_pcm_substream *substream;
	int no_compat_mmap;
};

struct snd_pcm_hw_rule;
typedef int (*snd_pcm_hw_rule_func_t)(struct snd_pcm_hw_params *params,
				      struct snd_pcm_hw_rule *rule);

struct snd_pcm_hw_rule {
	unsigned int cond;
	snd_pcm_hw_rule_func_t func;
	int var;
	int deps[4];
	void *private;
};

struct snd_pcm_hw_constraints {
	struct snd_mask masks[SNDRV_PCM_HW_PARAM_LAST_MASK - 
			 SNDRV_PCM_HW_PARAM_FIRST_MASK + 1];
	struct snd_interval intervals[SNDRV_PCM_HW_PARAM_LAST_INTERVAL -
			     SNDRV_PCM_HW_PARAM_FIRST_INTERVAL + 1];
	unsigned int rules_num;
	unsigned int rules_all;
	struct snd_pcm_hw_rule *rules;
};

static inline struct snd_mask *constrs_mask(struct snd_pcm_hw_constraints *constrs,
					    snd_pcm_hw_param_t var)
{
	return &constrs->masks[var - SNDRV_PCM_HW_PARAM_FIRST_MASK];
}

static inline struct snd_interval *constrs_interval(struct snd_pcm_hw_constraints *constrs,
						    snd_pcm_hw_param_t var)
{
	return &constrs->intervals[var - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
}

struct snd_ratnum {
	unsigned int num;
	unsigned int den_min, den_max, den_step;
};

struct snd_ratden {
	unsigned int num_min, num_max, num_step;
	unsigned int den;
};

struct snd_pcm_hw_constraint_ratnums {
	int nrats;
	struct snd_ratnum *rats;
};

struct snd_pcm_hw_constraint_ratdens {
	int nrats;
	struct snd_ratden *rats;
};

struct snd_pcm_hw_constraint_list {
	unsigned int count;
	const unsigned int *list;
	unsigned int mask;
};

struct snd_pcm_hwptr_log;

struct snd_pcm_runtime {
	/* -- Status -- */
	struct snd_pcm_substream *trigger_master;
	struct timespec trigger_tstamp;	/* trigger timestamp */
	int overrange;
	snd_pcm_uframes_t avail_max;
	snd_pcm_uframes_t hw_ptr_base;	/* Position at buffer restart */
	snd_pcm_uframes_t hw_ptr_interrupt; /* Position at interrupt time */
	unsigned long hw_ptr_jiffies;	/* Time when hw_ptr is updated */
	unsigned long hw_ptr_buffer_jiffies; /* buffer time in jiffies */
	snd_pcm_sframes_t delay;	/* extra delay; typically FIFO size */

	/* -- HW params -- */
	snd_pcm_access_t access;	/* access mode */
	snd_pcm_format_t format;	/* SNDRV_PCM_FORMAT_* */
	snd_pcm_subformat_t subformat;	/* subformat */
	unsigned int rate;		/* rate in Hz */
	unsigned int channels;		/* channels */
	snd_pcm_uframes_t period_size;	/* period size */
	unsigned int periods;		/* periods */
	snd_pcm_uframes_t buffer_size;	/* buffer size */
	snd_pcm_uframes_t min_align;	/* Min alignment for the format */
	size_t byte_align;
	unsigned int frame_bits;
	unsigned int sample_bits;
	unsigned int info;
	unsigned int rate_num;
	unsigned int rate_den;
	unsigned int no_period_wakeup: 1;

	/* -- SW params -- */
	int tstamp_mode;		/* mmap timestamp is updated */
  	unsigned int period_step;
	snd_pcm_uframes_t start_threshold;
	snd_pcm_uframes_t stop_threshold;
	snd_pcm_uframes_t silence_threshold; /* Silence filling happens when
						noise is nearest than this */
	snd_pcm_uframes_t silence_size;	/* Silence filling size */
	snd_pcm_uframes_t boundary;	/* pointers wrap point */

	snd_pcm_uframes_t silence_start; /* starting pointer to silence area */
	snd_pcm_uframes_t silence_filled; /* size filled with silence */

	union snd_pcm_sync_id sync;	/* hardware synchronization ID */

	/* -- mmap -- */
	struct snd_pcm_mmap_status *status;
	struct snd_pcm_mmap_control *control;

	/* -- locking / scheduling -- */
	snd_pcm_uframes_t twake; 	/* do transfer (!poll) wakeup if non-zero */
	wait_queue_head_t sleep;	/* poll sleep */
	wait_queue_head_t tsleep;	/* transfer sleep */
	struct fasync_struct *fasync;

	/* -- private section -- */
	void *private_data;
	void (*private_free)(struct snd_pcm_runtime *runtime);

	/* -- hardware description -- */
	struct snd_pcm_hardware hw;
	struct snd_pcm_hw_constraints hw_constraints;

	/* -- interrupt callbacks -- */
	void (*transfer_ack_begin)(struct snd_pcm_substream *substream);
	void (*transfer_ack_end)(struct snd_pcm_substream *substream);

	/* -- timer -- */
	unsigned int timer_resolution;	/* timer resolution */
	int tstamp_type;		/* timestamp type */

	/* -- DMA -- */           
	unsigned char *dma_area;	/* DMA area */
	dma_addr_t dma_addr;		/* physical bus address (not accessible from main CPU) */
	size_t dma_bytes;		/* size of DMA area */

	struct snd_dma_buffer *dma_buffer_p;	/* allocated buffer */

#if defined(CONFIG_SND_PCM_OSS) || defined(CONFIG_SND_PCM_OSS_MODULE)
	/* -- OSS things -- */
	struct snd_pcm_oss_runtime oss;
#endif

#ifdef CONFIG_SND_PCM_XRUN_DEBUG
	struct snd_pcm_hwptr_log *hwptr_log;
#endif
};

struct snd_pcm_group {		/* keep linked substreams */
	spinlock_t lock;
	struct list_head substreams;
	int count;
};

struct pid;

struct snd_pcm_substream {
	struct snd_pcm *pcm;
	struct snd_pcm_str *pstr;
	void *private_data;		/* copied from pcm->private_data */
	int number;
	char name[32];			/* substream name */
	int stream;			/* stream (direction) */
	struct pm_qos_request latency_pm_qos_req; /* pm_qos request */
	size_t buffer_bytes_max;	/* limit ring buffer size */
	struct snd_dma_buffer dma_buffer;
	unsigned int dma_buf_id;
	size_t dma_max;
	/* -- hardware operations -- */
	struct snd_pcm_ops *ops;
	/* -- runtime information -- */
	struct snd_pcm_runtime *runtime;
        /* -- timer section -- */
	struct snd_timer *timer;		/* timer */
	unsigned timer_running: 1;	/* time is running */
	/* -- next substream -- */
	struct snd_pcm_substream *next;
	/* -- linked substreams -- */
	struct list_head link_list;	/* linked list member */
	struct snd_pcm_group self_group;	/* fake group for non linked substream (with substream lock inside) */
	struct snd_pcm_group *group;		/* pointer to current group */
	/* -- assigned files -- */
	void *file;
	int ref_count;
	atomic_t mmap_count;
	unsigned int f_flags;
	void (*pcm_release)(struct snd_pcm_substream *);
	struct pid *pid;
#if defined(CONFIG_SND_PCM_OSS) || defined(CONFIG_SND_PCM_OSS_MODULE)
	/* -- OSS things -- */
	struct snd_pcm_oss_substream oss;
#endif
#ifdef CONFIG_SND_VERBOSE_PROCFS
	struct snd_info_entry *proc_root;
	struct snd_info_entry *proc_info_entry;
	struct snd_info_entry *proc_hw_params_entry;
	struct snd_info_entry *proc_sw_params_entry;
	struct snd_info_entry *proc_status_entry;
	struct snd_info_entry *proc_prealloc_entry;
	struct snd_info_entry *proc_prealloc_max_entry;
#endif
	/* misc flags */
	unsigned int hw_opened: 1;
};

#define SUBSTREAM_BUSY(substream) ((substream)->ref_count > 0)


struct snd_pcm_str {
	int stream;				/* stream (direction) */
	struct snd_pcm *pcm;
	/* -- substreams -- */
	unsigned int substream_count;
	unsigned int substream_opened;
	struct snd_pcm_substream *substream;
#if defined(CONFIG_SND_PCM_OSS) || defined(CONFIG_SND_PCM_OSS_MODULE)
	/* -- OSS things -- */
	struct snd_pcm_oss_stream oss;
#endif
#ifdef CONFIG_SND_VERBOSE_PROCFS
	struct snd_info_entry *proc_root;
	struct snd_info_entry *proc_info_entry;
#ifdef CONFIG_SND_PCM_XRUN_DEBUG
	unsigned int xrun_debug;	/* 0 = disabled, 1 = verbose, 2 = stacktrace */
	struct snd_info_entry *proc_xrun_debug_entry;
#endif
#endif
};

struct snd_pcm {
	struct snd_card *card;
	struct list_head list;
	int device; /* device number */
	unsigned int info_flags;
	unsigned short dev_class;
	unsigned short dev_subclass;
	char id[64];
	char name[80];
	struct snd_pcm_str streams[2];
	struct mutex open_mutex;
	wait_queue_head_t open_wait;
	void *private_data;
	void (*private_free) (struct snd_pcm *pcm);
	struct device *dev; /* actual hw device this belongs to */
	bool internal; /* pcm is for internal use only */
#if defined(CONFIG_SND_PCM_OSS) || defined(CONFIG_SND_PCM_OSS_MODULE)
	struct snd_pcm_oss oss;
#endif
};

struct snd_pcm_notify {
	int (*n_register) (struct snd_pcm * pcm);
	int (*n_disconnect) (struct snd_pcm * pcm);
	int (*n_unregister) (struct snd_pcm * pcm);
	struct list_head list;
};

/*
 *  Registering
 */

extern const struct file_operations snd_pcm_f_ops[2];

int snd_pcm_new(struct snd_card *card, const char *id, int device,
		int playback_count, int capture_count,
		struct snd_pcm **rpcm);
int snd_pcm_new_internal(struct snd_card *card, const char *id, int device,
		int playback_count, int capture_count,
		struct snd_pcm **rpcm);
int snd_pcm_new_stream(struct snd_pcm *pcm, int stream, int substream_count);

int snd_pcm_notify(struct snd_pcm_notify *notify, int nfree);

/*
 *  Native I/O
 */

extern rwlock_t snd_pcm_link_rwlock;

int snd_pcm_info(struct snd_pcm_substream *substream, struct snd_pcm_info *info);
int snd_pcm_info_user(struct snd_pcm_substream *substream,
		      struct snd_pcm_info __user *info);
int snd_pcm_status(struct snd_pcm_substream *substream,
		   struct snd_pcm_status *status);
int snd_pcm_start(struct snd_pcm_substream *substream);
int snd_pcm_stop(struct snd_pcm_substream *substream, snd_pcm_state_t status);
int snd_pcm_drain_done(struct snd_pcm_substream *substream);
#ifdef CONFIG_PM
int snd_pcm_suspend(struct snd_pcm_substream *substream);
int snd_pcm_suspend_all(struct snd_pcm *pcm);
#endif
int snd_pcm_kernel_ioctl(struct snd_pcm_substream *substream, unsigned int cmd, void *arg);
int snd_pcm_open_substream(struct snd_pcm *pcm, int stream, struct file *file,
			   struct snd_pcm_substream **rsubstream);
void snd_pcm_release_substream(struct snd_pcm_substream *substream);
int snd_pcm_attach_substream(struct snd_pcm *pcm, int stream, struct file *file,
			     struct snd_pcm_substream **rsubstream);
void snd_pcm_detach_substream(struct snd_pcm_substream *substream);
void snd_pcm_vma_notify_data(void *client, void *data);
int snd_pcm_mmap_data(struct snd_pcm_substream *substream, struct file *file, struct vm_area_struct *area);


#ifdef CONFIG_SND_DEBUG
void snd_pcm_debug_name(struct snd_pcm_substream *substream,
			   char *name, size_t len);
#else
static inline void
snd_pcm_debug_name(struct snd_pcm_substream *substream, char *buf, size_t size)
{
	*buf = 0;
}
#endif

/*
 *  PCM library
 */

static inline int snd_pcm_stream_linked(struct snd_pcm_substream *substream)
{
	return substream->group != &substream->self_group;
}

static inline void snd_pcm_stream_lock(struct snd_pcm_substream *substream)
{
	read_lock(&snd_pcm_link_rwlock);
	spin_lock(&substream->self_group.lock);
}

static inline void snd_pcm_stream_unlock(struct snd_pcm_substream *substream)
{
	spin_unlock(&substream->self_group.lock);
	read_unlock(&snd_pcm_link_rwlock);
}

static inline void snd_pcm_stream_lock_irq(struct snd_pcm_substream *substream)
{
	read_lock_irq(&snd_pcm_link_rwlock);
	spin_lock(&substream->self_group.lock);
}

static inline void snd_pcm_stream_unlock_irq(struct snd_pcm_substream *substream)
{
	spin_unlock(&substream->self_group.lock);
	read_unlock_irq(&snd_pcm_link_rwlock);
}

#define snd_pcm_stream_lock_irqsave(substream, flags) \
do { \
	read_lock_irqsave(&snd_pcm_link_rwlock, (flags)); \
	spin_lock(&substream->self_group.lock); \
} while (0)

#define snd_pcm_stream_unlock_irqrestore(substream, flags) \
do { \
	spin_unlock(&substream->self_group.lock); \
	read_unlock_irqrestore(&snd_pcm_link_rwlock, (flags)); \
} while (0)

#define snd_pcm_group_for_each_entry(s, substream) \
	list_for_each_entry(s, &substream->group->substreams, link_list)

static inline int snd_pcm_running(struct snd_pcm_substream *substream)
{
	return (substream->runtime->status->state == SNDRV_PCM_STATE_RUNNING ||
		(substream->runtime->status->state == SNDRV_PCM_STATE_DRAINING &&
		 substream->stream == SNDRV_PCM_STREAM_PLAYBACK));
}

static inline ssize_t bytes_to_samples(struct snd_pcm_runtime *runtime, ssize_t size)
{
	return size * 8 / runtime->sample_bits;
}

static inline snd_pcm_sframes_t bytes_to_frames(struct snd_pcm_runtime *runtime, ssize_t size)
{
	return size * 8 / runtime->frame_bits;
}

static inline ssize_t samples_to_bytes(struct snd_pcm_runtime *runtime, ssize_t size)
{
	return size * runtime->sample_bits / 8;
}

static inline ssize_t frames_to_bytes(struct snd_pcm_runtime *runtime, snd_pcm_sframes_t size)
{
	return size * runtime->frame_bits / 8;
}

static inline int frame_aligned(struct snd_pcm_runtime *runtime, ssize_t bytes)
{
	return bytes % runtime->byte_align == 0;
}

static inline size_t snd_pcm_lib_buffer_bytes(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	return frames_to_bytes(runtime, runtime->buffer_size);
}

static inline size_t snd_pcm_lib_period_bytes(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	return frames_to_bytes(runtime, runtime->period_size);
}

/*
 *  result is: 0 ... (boundary - 1)
 */
static inline snd_pcm_uframes_t snd_pcm_playback_avail(struct snd_pcm_runtime *runtime)
{
	snd_pcm_sframes_t avail = runtime->status->hw_ptr + runtime->buffer_size - runtime->control->appl_ptr;
	if (avail < 0)
		avail += runtime->boundary;
	else if ((snd_pcm_uframes_t) avail >= runtime->boundary)
		avail -= runtime->boundary;
	return avail;
}

/*
 *  result is: 0 ... (boundary - 1)
 */
static inline snd_pcm_uframes_t snd_pcm_capture_avail(struct snd_pcm_runtime *runtime)
{
	snd_pcm_sframes_t avail = runtime->status->hw_ptr - runtime->control->appl_ptr;
	if (avail < 0)
		avail += runtime->boundary;
	return avail;
}

static inline snd_pcm_sframes_t snd_pcm_playback_hw_avail(struct snd_pcm_runtime *runtime)
{
	return runtime->buffer_size - snd_pcm_playback_avail(runtime);
}

static inline snd_pcm_sframes_t snd_pcm_capture_hw_avail(struct snd_pcm_runtime *runtime)
{
	return runtime->buffer_size - snd_pcm_capture_avail(runtime);
}

/**
 * snd_pcm_playback_ready - check whether the playback buffer is available
 * @substream: the pcm substream instance
 *
 * Checks whether enough free space is available on the playback buffer.
 *
 * Returns non-zero if available, or zero if not.
 */
static inline int snd_pcm_playback_ready(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	return snd_pcm_playback_avail(runtime) >= runtime->control->avail_min;
}

/**
 * snd_pcm_capture_ready - check whether the capture buffer is available
 * @substream: the pcm substream instance
 *
 * Checks whether enough capture data is available on the capture buffer.
 *
 * Returns non-zero if available, or zero if not.
 */
static inline int snd_pcm_capture_ready(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	return snd_pcm_capture_avail(runtime) >= runtime->control->avail_min;
}

/**
 * snd_pcm_playback_data - check whether any data exists on the playback buffer
 * @substream: the pcm substream instance
 *
 * Checks whether any data exists on the playback buffer. If stop_threshold
 * is bigger or equal to boundary, then this function returns always non-zero.
 *
 * Returns non-zero if exists, or zero if not.
 */
static inline int snd_pcm_playback_data(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	
	if (runtime->stop_threshold >= runtime->boundary)
		return 1;
	return snd_pcm_playback_avail(runtime) < runtime->buffer_size;
}

/**
 * snd_pcm_playback_empty - check whether the playback buffer is empty
 * @substream: the pcm substream instance
 *
 * Checks whether the playback buffer is empty.
 *
 * Returns non-zero if empty, or zero if not.
 */
static inline int snd_pcm_playback_empty(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	return snd_pcm_playback_avail(runtime) >= runtime->buffer_size;
}

/**
 * snd_pcm_capture_empty - check whether the capture buffer is empty
 * @substream: the pcm substream instance
 *
 * Checks whether the capture buffer is empty.
 *
 * Returns non-zero if empty, or zero if not.
 */
static inline int snd_pcm_capture_empty(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	return snd_pcm_capture_avail(runtime) == 0;
}

static inline void snd_pcm_trigger_done(struct snd_pcm_substream *substream, 
					struct snd_pcm_substream *master)
{
	substream->runtime->trigger_master = master;
}

static inline int hw_is_mask(int var)
{
	return var >= SNDRV_PCM_HW_PARAM_FIRST_MASK &&
		var <= SNDRV_PCM_HW_PARAM_LAST_MASK;
}

static inline int hw_is_interval(int var)
{
	return var >= SNDRV_PCM_HW_PARAM_FIRST_INTERVAL &&
		var <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL;
}

static inline struct snd_mask *hw_param_mask(struct snd_pcm_hw_params *params,
				     snd_pcm_hw_param_t var)
{
	return &params->masks[var - SNDRV_PCM_HW_PARAM_FIRST_MASK];
}

static inline struct snd_interval *hw_param_interval(struct snd_pcm_hw_params *params,
					     snd_pcm_hw_param_t var)
{
	return &params->intervals[var - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
}

static inline const struct snd_mask *hw_param_mask_c(const struct snd_pcm_hw_params *params,
					     snd_pcm_hw_param_t var)
{
	return &params->masks[var - SNDRV_PCM_HW_PARAM_FIRST_MASK];
}

static inline const struct snd_interval *hw_param_interval_c(const struct snd_pcm_hw_params *params,
						     snd_pcm_hw_param_t var)
{
	return &params->intervals[var - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
}

#define params_channels(p) \
	(hw_param_interval_c((p), SNDRV_PCM_HW_PARAM_CHANNELS)->min)
#define params_rate(p) \
	(hw_param_interval_c((p), SNDRV_PCM_HW_PARAM_RATE)->min)
#define params_period_size(p) \
	(hw_param_interval_c((p), SNDRV_PCM_HW_PARAM_PERIOD_SIZE)->min)
#define params_periods(p) \
	(hw_param_interval_c((p), SNDRV_PCM_HW_PARAM_PERIODS)->min)
#define params_buffer_size(p) \
	(hw_param_interval_c((p), SNDRV_PCM_HW_PARAM_BUFFER_SIZE)->min)
#define params_buffer_bytes(p) \
	(hw_param_interval_c((p), SNDRV_PCM_HW_PARAM_BUFFER_BYTES)->min)

int snd_interval_refine(struct snd_interval *i, const struct snd_interval *v);
void snd_interval_mul(const struct snd_interval *a, const struct snd_interval *b, struct snd_interval *c);
void snd_interval_div(const struct snd_interval *a, const struct snd_interval *b, struct snd_interval *c);
void snd_interval_muldivk(const struct snd_interval *a, const struct snd_interval *b, 
			  unsigned int k, struct snd_interval *c);
void snd_interval_mulkdiv(const struct snd_interval *a, unsigned int k,
			  const struct snd_interval *b, struct snd_interval *c);
int snd_interval_list(struct snd_interval *i, unsigned int count,
		      const unsigned int *list, unsigned int mask);
int snd_interval_ratnum(struct snd_interval *i,
			unsigned int rats_count, struct snd_ratnum *rats,
			unsigned int *nump, unsigned int *denp);

void _snd_pcm_hw_params_any(struct snd_pcm_hw_params *params);
void _snd_pcm_hw_param_setempty(struct snd_pcm_hw_params *params, snd_pcm_hw_param_t var);
int snd_pcm_hw_params_choose(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params);

int snd_pcm_hw_refine(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params);

int snd_pcm_hw_constraints_init(struct snd_pcm_substream *substream);
int snd_pcm_hw_constraints_complete(struct snd_pcm_substream *substream);

int snd_pcm_hw_constraint_mask(struct snd_pcm_runtime *runtime, snd_pcm_hw_param_t var,
			       u_int32_t mask);
int snd_pcm_hw_constraint_mask64(struct snd_pcm_runtime *runtime, snd_pcm_hw_param_t var,
				 u_int64_t mask);
int snd_pcm_hw_constraint_minmax(struct snd_pcm_runtime *runtime, snd_pcm_hw_param_t var,
				 unsigned int min, unsigned int max);
int snd_pcm_hw_constraint_integer(struct snd_pcm_runtime *runtime, snd_pcm_hw_param_t var);
int snd_pcm_hw_constraint_list(struct snd_pcm_runtime *runtime, 
			       unsigned int cond,
			       snd_pcm_hw_param_t var,
			       const struct snd_pcm_hw_constraint_list *l);
int snd_pcm_hw_constraint_ratnums(struct snd_pcm_runtime *runtime, 
				  unsigned int cond,
				  snd_pcm_hw_param_t var,
				  struct snd_pcm_hw_constraint_ratnums *r);
int snd_pcm_hw_constraint_ratdens(struct snd_pcm_runtime *runtime, 
				  unsigned int cond,
				  snd_pcm_hw_param_t var,
				  struct snd_pcm_hw_constraint_ratdens *r);
int snd_pcm_hw_constraint_msbits(struct snd_pcm_runtime *runtime, 
				 unsigned int cond,
				 unsigned int width,
				 unsigned int msbits);
int snd_pcm_hw_constraint_step(struct snd_pcm_runtime *runtime,
			       unsigned int cond,
			       snd_pcm_hw_param_t var,
			       unsigned long step);
int snd_pcm_hw_constraint_pow2(struct snd_pcm_runtime *runtime,
			       unsigned int cond,
			       snd_pcm_hw_param_t var);
int snd_pcm_hw_rule_noresample(struct snd_pcm_runtime *runtime,
			       unsigned int base_rate);
int snd_pcm_hw_rule_add(struct snd_pcm_runtime *runtime,
			unsigned int cond,
			int var,
			snd_pcm_hw_rule_func_t func, void *private,
			int dep, ...);

int snd_pcm_format_signed(snd_pcm_format_t format);
int snd_pcm_format_unsigned(snd_pcm_format_t format);
int snd_pcm_format_linear(snd_pcm_format_t format);
int snd_pcm_format_little_endian(snd_pcm_format_t format);
int snd_pcm_format_big_endian(snd_pcm_format_t format);
#if 0 /* just for DocBook */
/**
 * snd_pcm_format_cpu_endian - Check the PCM format is CPU-endian
 * @format: the format to check
 *
 * Returns 1 if the given PCM format is CPU-endian, 0 if
 * opposite, or a negative error code if endian not specified.
 */
int snd_pcm_format_cpu_endian(snd_pcm_format_t format);
#endif /* DocBook */
#ifdef SNDRV_LITTLE_ENDIAN
#define snd_pcm_format_cpu_endian(format) snd_pcm_format_little_endian(format)
#else
#define snd_pcm_format_cpu_endian(format) snd_pcm_format_big_endian(format)
#endif
int snd_pcm_format_width(snd_pcm_format_t format);			/* in bits */
int snd_pcm_format_physical_width(snd_pcm_format_t format);		/* in bits */
ssize_t snd_pcm_format_size(snd_pcm_format_t format, size_t samples);
const unsigned char *snd_pcm_format_silence_64(snd_pcm_format_t format);
int snd_pcm_format_set_silence(snd_pcm_format_t format, void *buf, unsigned int frames);
snd_pcm_format_t snd_pcm_build_linear_format(int width, int unsignd, int big_endian);

void snd_pcm_set_ops(struct snd_pcm * pcm, int direction, struct snd_pcm_ops *ops);
void snd_pcm_set_sync(struct snd_pcm_substream *substream);
int snd_pcm_lib_interleave_len(struct snd_pcm_substream *substream);
int snd_pcm_lib_ioctl(struct snd_pcm_substream *substream,
		      unsigned int cmd, void *arg);                      
int snd_pcm_update_state(struct snd_pcm_substream *substream,
			 struct snd_pcm_runtime *runtime);
int snd_pcm_update_hw_ptr(struct snd_pcm_substream *substream);
int snd_pcm_playback_xrun_check(struct snd_pcm_substream *substream);
int snd_pcm_capture_xrun_check(struct snd_pcm_substream *substream);
int snd_pcm_playback_xrun_asap(struct snd_pcm_substream *substream);
int snd_pcm_capture_xrun_asap(struct snd_pcm_substream *substream);
void snd_pcm_playback_silence(struct snd_pcm_substream *substream, snd_pcm_uframes_t new_hw_ptr);
void snd_pcm_period_elapsed(struct snd_pcm_substream *substream);
snd_pcm_sframes_t snd_pcm_lib_write(struct snd_pcm_substream *substream,
				    const void __user *buf,
				    snd_pcm_uframes_t frames);
snd_pcm_sframes_t snd_pcm_lib_read(struct snd_pcm_substream *substream,
				   void __user *buf, snd_pcm_uframes_t frames);
snd_pcm_sframes_t snd_pcm_lib_writev(struct snd_pcm_substream *substream,
				     void __user **bufs, snd_pcm_uframes_t frames);
snd_pcm_sframes_t snd_pcm_lib_readv(struct snd_pcm_substream *substream,
				    void __user **bufs, snd_pcm_uframes_t frames);

extern const struct snd_pcm_hw_constraint_list snd_pcm_known_rates;

int snd_pcm_limit_hw_rates(struct snd_pcm_runtime *runtime);
unsigned int snd_pcm_rate_to_rate_bit(unsigned int rate);
unsigned int snd_pcm_rate_bit_to_rate(unsigned int rate_bit);

static inline void snd_pcm_set_runtime_buffer(struct snd_pcm_substream *substream,
					      struct snd_dma_buffer *bufp)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	if (bufp) {
		runtime->dma_buffer_p = bufp;
		runtime->dma_area = bufp->area;
		runtime->dma_addr = bufp->addr;
		runtime->dma_bytes = bufp->bytes;
	} else {
		runtime->dma_buffer_p = NULL;
		runtime->dma_area = NULL;
		runtime->dma_addr = 0;
		runtime->dma_bytes = 0;
	}
}

/*
 *  Timer interface
 */

void snd_pcm_timer_resolution_change(struct snd_pcm_substream *substream);
void snd_pcm_timer_init(struct snd_pcm_substream *substream);
void snd_pcm_timer_done(struct snd_pcm_substream *substream);

static inline void snd_pcm_gettime(struct snd_pcm_runtime *runtime,
				   struct timespec *tv)
{
	if (runtime->tstamp_type == SNDRV_PCM_TSTAMP_TYPE_MONOTONIC)
		do_posix_clock_monotonic_gettime(tv);
	else
		getnstimeofday(tv);
}

/*
 *  Memory
 */

int snd_pcm_lib_preallocate_free(struct snd_pcm_substream *substream);
int snd_pcm_lib_preallocate_free_for_all(struct snd_pcm *pcm);
int snd_pcm_lib_preallocate_pages(struct snd_pcm_substream *substream,
				  int type, struct device *data,
				  size_t size, size_t max);
int snd_pcm_lib_preallocate_pages_for_all(struct snd_pcm *pcm,
					  int type, void *data,
					  size_t size, size_t max);
int snd_pcm_lib_malloc_pages(struct snd_pcm_substream *substream, size_t size);
int snd_pcm_lib_free_pages(struct snd_pcm_substream *substream);

int _snd_pcm_lib_alloc_vmalloc_buffer(struct snd_pcm_substream *substream,
				      size_t size, gfp_t gfp_flags);
int snd_pcm_lib_free_vmalloc_buffer(struct snd_pcm_substream *substream);
struct page *snd_pcm_lib_get_vmalloc_page(struct snd_pcm_substream *substream,
					  unsigned long offset);
#if 0 /* for kernel-doc */
/**
 * snd_pcm_lib_alloc_vmalloc_buffer - allocate virtual DMA buffer
 * @substream: the substream to allocate the buffer to
 * @size: the requested buffer size, in bytes
 *
 * Allocates the PCM substream buffer using vmalloc(), i.e., the memory is
 * contiguous in kernel virtual space, but not in physical memory.  Use this
 * if the buffer is accessed by kernel code but not by device DMA.
 *
 * Returns 1 if the buffer was changed, 0 if not changed, or a negative error
 * code.
 */
static int snd_pcm_lib_alloc_vmalloc_buffer
			(struct snd_pcm_substream *substream, size_t size);
/**
 * snd_pcm_lib_alloc_vmalloc_32_buffer - allocate 32-bit-addressable buffer
 * @substream: the substream to allocate the buffer to
 * @size: the requested buffer size, in bytes
 *
 * This function works like snd_pcm_lib_alloc_vmalloc_buffer(), but uses
 * vmalloc_32(), i.e., the pages are allocated from 32-bit-addressable memory.
 */
static int snd_pcm_lib_alloc_vmalloc_32_buffer
			(struct snd_pcm_substream *substream, size_t size);
#endif
#define snd_pcm_lib_alloc_vmalloc_buffer(subs, size) \
	_snd_pcm_lib_alloc_vmalloc_buffer \
			(subs, size, GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO)
#define snd_pcm_lib_alloc_vmalloc_32_buffer(subs, size) \
	_snd_pcm_lib_alloc_vmalloc_buffer \
			(subs, size, GFP_KERNEL | GFP_DMA32 | __GFP_ZERO)

#ifdef CONFIG_SND_DMA_SGBUF
/*
 * SG-buffer handling
 */
#define snd_pcm_substream_sgbuf(substream) \
	((substream)->runtime->dma_buffer_p->private_data)

static inline dma_addr_t
snd_pcm_sgbuf_get_addr(struct snd_pcm_substream *substream, unsigned int ofs)
{
	struct snd_sg_buf *sg = snd_pcm_substream_sgbuf(substream);
	return snd_sgbuf_get_addr(sg, ofs);
}

static inline void *
snd_pcm_sgbuf_get_ptr(struct snd_pcm_substream *substream, unsigned int ofs)
{
	struct snd_sg_buf *sg = snd_pcm_substream_sgbuf(substream);
	return snd_sgbuf_get_ptr(sg, ofs);
}

struct page *snd_pcm_sgbuf_ops_page(struct snd_pcm_substream *substream,
				    unsigned long offset);
unsigned int snd_pcm_sgbuf_get_chunk_size(struct snd_pcm_substream *substream,
					  unsigned int ofs, unsigned int size);

#else /* !SND_DMA_SGBUF */
/*
 * fake using a continuous buffer
 */
static inline dma_addr_t
snd_pcm_sgbuf_get_addr(struct snd_pcm_substream *substream, unsigned int ofs)
{
	return substream->runtime->dma_addr + ofs;
}

static inline void *
snd_pcm_sgbuf_get_ptr(struct snd_pcm_substream *substream, unsigned int ofs)
{
	return substream->runtime->dma_area + ofs;
}

#define snd_pcm_sgbuf_ops_page	NULL

#define snd_pcm_sgbuf_get_chunk_size(subs, ofs, size)	(size)

#endif /* SND_DMA_SGBUF */

/* handle mmap counter - PCM mmap callback should handle this counter properly */
static inline void snd_pcm_mmap_data_open(struct vm_area_struct *area)
{
	struct snd_pcm_substream *substream = (struct snd_pcm_substream *)area->vm_private_data;
	atomic_inc(&substream->mmap_count);
}

static inline void snd_pcm_mmap_data_close(struct vm_area_struct *area)
{
	struct snd_pcm_substream *substream = (struct snd_pcm_substream *)area->vm_private_data;
	atomic_dec(&substream->mmap_count);
}

int snd_pcm_lib_default_mmap(struct snd_pcm_substream *substream,
			     struct vm_area_struct *area);
/* mmap for io-memory area */
#if defined(CONFIG_X86) || defined(CONFIG_PPC) || defined(CONFIG_ALPHA)
#define SNDRV_PCM_INFO_MMAP_IOMEM	SNDRV_PCM_INFO_MMAP
int snd_pcm_lib_mmap_iomem(struct snd_pcm_substream *substream, struct vm_area_struct *area);
#else
#define SNDRV_PCM_INFO_MMAP_IOMEM	0
#define snd_pcm_lib_mmap_iomem	NULL
#endif

#define snd_pcm_lib_mmap_vmalloc NULL

static inline void snd_pcm_limit_isa_dma_size(int dma, size_t *max)
{
	*max = dma < 4 ? 64 * 1024 : 128 * 1024;
}

/*
 *  Misc
 */

#define SNDRV_PCM_DEFAULT_CON_SPDIF	(IEC958_AES0_CON_EMPHASIS_NONE|\
					 (IEC958_AES1_CON_ORIGINAL<<8)|\
					 (IEC958_AES1_CON_PCM_CODER<<8)|\
					 (IEC958_AES3_CON_FS_48000<<24))

#define PCM_RUNTIME_CHECK(sub) snd_BUG_ON(!(sub) || !(sub)->runtime)

const char *snd_pcm_format_name(snd_pcm_format_t format);

/**
 * snd_pcm_stream_str - Get a string naming the direction of a stream
 * @substream: the pcm substream instance
 */
static inline const char *snd_pcm_stream_str(struct snd_pcm_substream *substream)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return "Playback";
	else
		return "Capture";
}

#endif /* __SOUND_PCM_H */
