/*
 * Intel Baytrail SST IPC Support
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __SST_BYT_IPC_H
#define __SST_BYT_IPC_H

#include <linux/types.h>

struct sst_byt;
struct sst_byt_stream;
struct sst_pdata;
extern struct sst_ops sst_byt_ops;


#define SST_BYT_MAILBOX_OFFSET		0x144000
#define SST_BYT_TIMESTAMP_OFFSET	(SST_BYT_MAILBOX_OFFSET + 0x800)

/**
 * Upfront defined maximum message size that is
 * expected by the in/out communication pipes in FW.
 */
#define SST_BYT_IPC_MAX_PAYLOAD_SIZE	200

/* stream API */
struct sst_byt_stream *sst_byt_stream_new(struct sst_byt *byt, int id,
	uint32_t (*get_write_position)(struct sst_byt_stream *stream,
				       void *data),
	void *data);

/* stream configuration */
int sst_byt_stream_set_bits(struct sst_byt *byt, struct sst_byt_stream *stream,
			    int bits);
int sst_byt_stream_set_channels(struct sst_byt *byt,
				struct sst_byt_stream *stream, u8 channels);
int sst_byt_stream_set_rate(struct sst_byt *byt, struct sst_byt_stream *stream,
			    unsigned int rate);
int sst_byt_stream_type(struct sst_byt *byt, struct sst_byt_stream *stream,
			int codec_type, int stream_type, int operation);
int sst_byt_stream_buffer(struct sst_byt *byt, struct sst_byt_stream *stream,
			  uint32_t buffer_addr, uint32_t buffer_size);
int sst_byt_stream_commit(struct sst_byt *byt, struct sst_byt_stream *stream);
int sst_byt_stream_free(struct sst_byt *byt, struct sst_byt_stream *stream);

/* stream ALSA trigger operations */
int sst_byt_stream_start(struct sst_byt *byt, struct sst_byt_stream *stream,
			 u32 start_offset);
int sst_byt_stream_stop(struct sst_byt *byt, struct sst_byt_stream *stream);
int sst_byt_stream_pause(struct sst_byt *byt, struct sst_byt_stream *stream);
int sst_byt_stream_resume(struct sst_byt *byt, struct sst_byt_stream *stream);

int sst_byt_get_dsp_position(struct sst_byt *byt,
			     struct sst_byt_stream *stream, int buffer_size);

/* init */
int sst_byt_dsp_init(struct device *dev, struct sst_pdata *pdata);
void sst_byt_dsp_free(struct device *dev, struct sst_pdata *pdata);
struct sst_dsp *sst_byt_get_dsp(struct sst_byt *byt);
int sst_byt_dsp_suspend_late(struct device *dev, struct sst_pdata *pdata);
int sst_byt_dsp_boot(struct device *dev, struct sst_pdata *pdata);
int sst_byt_dsp_wait_for_ready(struct device *dev, struct sst_pdata *pdata);

#endif
