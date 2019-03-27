/*
 * Copyright (c) 2012, Broadcom Europe Ltd
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *    3. Neither the name of the copyright holder nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _VC_AUDIO_DEFS_H_
#define _VC_AUDIO_DEFS_H_

#define VC_AUDIOSERV_MIN_VER 1
#define VC_AUDIOSERV_VER 2

/* FourCC code used for VCHI connection */
#define VC_AUDIO_SERVER_NAME  MAKE_FOURCC("AUDS")

/* Maximum message length */
#define VC_AUDIO_MAX_MSG_LEN  (sizeof( VC_AUDIO_MSG_T ))

/* 
 * List of screens that are currently supported
 * All message types supported for HOST->VC direction
 */
typedef enum
{
	VC_AUDIO_MSG_TYPE_RESULT,	/* Generic result */
	VC_AUDIO_MSG_TYPE_COMPLETE,	/* playback of samples complete */
	VC_AUDIO_MSG_TYPE_CONFIG,	/* Configure */
	VC_AUDIO_MSG_TYPE_CONTROL,	/* control  */
	VC_AUDIO_MSG_TYPE_OPEN,		/*  open */
	VC_AUDIO_MSG_TYPE_CLOSE,	/* close/shutdown */
	VC_AUDIO_MSG_TYPE_START,	/* start output (i.e. resume) */
	VC_AUDIO_MSG_TYPE_STOP,		/* stop output (i.e. pause) */
	VC_AUDIO_MSG_TYPE_WRITE,	/* write samples */
	VC_AUDIO_MSG_TYPE_MAX

} VC_AUDIO_MSG_TYPE;

static const char *vc_audio_msg_type_names[] = {
	"VC_AUDIO_MSG_TYPE_RESULT",
	"VC_AUDIO_MSG_TYPE_COMPLETE",
	"VC_AUDIO_MSG_TYPE_CONFIG",
	"VC_AUDIO_MSG_TYPE_CONTROL",
	"VC_AUDIO_MSG_TYPE_OPEN",
	"VC_AUDIO_MSG_TYPE_CLOSE",
	"VC_AUDIO_MSG_TYPE_START",
	"VC_AUDIO_MSG_TYPE_STOP",
	"VC_AUDIO_MSG_TYPE_WRITE",
	"VC_AUDIO_MSG_TYPE_MAX"
};

/* configure the audio */
typedef struct
{
	uint32_t channels;
	uint32_t samplerate;
	uint32_t bps;

} VC_AUDIO_CONFIG_T;

typedef struct
{
	uint32_t volume;
	uint32_t dest;

} VC_AUDIO_CONTROL_T;

typedef struct
{
	uint32_t dummy;

} VC_AUDIO_OPEN_T;

typedef struct
{
	uint32_t dummy;

} VC_AUDIO_CLOSE_T;

typedef struct
{
	uint32_t dummy;

} VC_AUDIO_START_T;

typedef struct
{
	uint32_t draining;

} VC_AUDIO_STOP_T;

typedef struct
{
	uint32_t count; /* in bytes */
	void *callback;
	void *cookie;
	uint16_t silence;
	uint16_t max_packet;
} VC_AUDIO_WRITE_T;

/* Generic result for a request (VC->HOST) */
typedef struct
{
	int32_t success;  /* Success value */

} VC_AUDIO_RESULT_T;

/* Generic result for a request (VC->HOST) */
typedef struct
{
	int32_t count;  /* Success value */
	void *callback;
	void *cookie;
} VC_AUDIO_COMPLETE_T;

/* Message header for all messages in HOST->VC direction */
typedef struct
{
	int32_t type;     /* Message type (VC_AUDIO_MSG_TYPE) */
	union
	{
		VC_AUDIO_CONFIG_T	config;
		VC_AUDIO_CONTROL_T	control;
		VC_AUDIO_OPEN_T		open;
		VC_AUDIO_CLOSE_T	close;
		VC_AUDIO_START_T	start;
		VC_AUDIO_STOP_T		stop;
		VC_AUDIO_WRITE_T	write;
		VC_AUDIO_RESULT_T	result;
		VC_AUDIO_COMPLETE_T	complete;
	} u;
} VC_AUDIO_MSG_T;

#endif /* _VC_AUDIO_DEFS_H_ */
