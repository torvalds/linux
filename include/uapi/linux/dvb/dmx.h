/* SPDX-License-Identifier: LGPL-2.1+ WITH Linux-syscall-note */
/*
 * dmx.h
 *
 * Copyright (C) 2000 Marcus Metzler <marcus@convergence.de>
 *                  & Ralph  Metzler <ralph@convergence.de>
 *                    for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef _UAPI_DVBDMX_H_
#define _UAPI_DVBDMX_H_

#include <linux/types.h>
#ifndef __KERNEL__
#include <time.h>
#endif


#define DMX_FILTER_SIZE 16

/**
 * enum dmx_output - Output for the demux.
 *
 * @DMX_OUT_DECODER:
 *	Streaming directly to decoder.
 * @DMX_OUT_TAP:
 *	Output going to a memory buffer (to be retrieved via the read command).
 *	Delivers the stream output to the demux device on which the ioctl
 *	is called.
 * @DMX_OUT_TS_TAP:
 *	Output multiplexed into a new TS (to be retrieved by reading from the
 *	logical DVR device). Routes output to the logical DVR device
 *	``/dev/dvb/adapter?/dvr?``, which delivers a TS multiplexed from all
 *	filters for which @DMX_OUT_TS_TAP was specified.
 * @DMX_OUT_TSDEMUX_TAP:
 *	Like @DMX_OUT_TS_TAP but retrieved from the DMX device.
 */
enum dmx_output {
	DMX_OUT_DECODER,
	DMX_OUT_TAP,
	DMX_OUT_TS_TAP,
	DMX_OUT_TSDEMUX_TAP
};


/**
 * enum dmx_input - Input from the demux.
 *
 * @DMX_IN_FRONTEND:	Input from a front-end device.
 * @DMX_IN_DVR:		Input from the logical DVR device.
 */
enum dmx_input {
	DMX_IN_FRONTEND,
	DMX_IN_DVR
};

/**
 * enum dmx_ts_pes - type of the PES filter.
 *
 * @DMX_PES_AUDIO0:	first audio PID. Also referred as @DMX_PES_AUDIO.
 * @DMX_PES_VIDEO0:	first video PID. Also referred as @DMX_PES_VIDEO.
 * @DMX_PES_TELETEXT0:	first teletext PID. Also referred as @DMX_PES_TELETEXT.
 * @DMX_PES_SUBTITLE0:	first subtitle PID. Also referred as @DMX_PES_SUBTITLE.
 * @DMX_PES_PCR0:	first Program Clock Reference PID.
 *			Also referred as @DMX_PES_PCR.
 *
 * @DMX_PES_AUDIO1:	second audio PID.
 * @DMX_PES_VIDEO1:	second video PID.
 * @DMX_PES_TELETEXT1:	second teletext PID.
 * @DMX_PES_SUBTITLE1:	second subtitle PID.
 * @DMX_PES_PCR1:	second Program Clock Reference PID.
 *
 * @DMX_PES_AUDIO2:	third audio PID.
 * @DMX_PES_VIDEO2:	third video PID.
 * @DMX_PES_TELETEXT2:	third teletext PID.
 * @DMX_PES_SUBTITLE2:	third subtitle PID.
 * @DMX_PES_PCR2:	third Program Clock Reference PID.
 *
 * @DMX_PES_AUDIO3:	fourth audio PID.
 * @DMX_PES_VIDEO3:	fourth video PID.
 * @DMX_PES_TELETEXT3:	fourth teletext PID.
 * @DMX_PES_SUBTITLE3:	fourth subtitle PID.
 * @DMX_PES_PCR3:	fourth Program Clock Reference PID.
 *
 * @DMX_PES_OTHER:	any other PID.
 */

enum dmx_ts_pes {
	DMX_PES_AUDIO0,
	DMX_PES_VIDEO0,
	DMX_PES_TELETEXT0,
	DMX_PES_SUBTITLE0,
	DMX_PES_PCR0,

	DMX_PES_AUDIO1,
	DMX_PES_VIDEO1,
	DMX_PES_TELETEXT1,
	DMX_PES_SUBTITLE1,
	DMX_PES_PCR1,

	DMX_PES_AUDIO2,
	DMX_PES_VIDEO2,
	DMX_PES_TELETEXT2,
	DMX_PES_SUBTITLE2,
	DMX_PES_PCR2,

	DMX_PES_AUDIO3,
	DMX_PES_VIDEO3,
	DMX_PES_TELETEXT3,
	DMX_PES_SUBTITLE3,
	DMX_PES_PCR3,

	DMX_PES_OTHER
};

#define DMX_PES_AUDIO    DMX_PES_AUDIO0
#define DMX_PES_VIDEO    DMX_PES_VIDEO0
#define DMX_PES_TELETEXT DMX_PES_TELETEXT0
#define DMX_PES_SUBTITLE DMX_PES_SUBTITLE0
#define DMX_PES_PCR      DMX_PES_PCR0



/**
 * struct dmx_filter - Specifies a section header filter.
 *
 * @filter: bit array with bits to be matched at the section header.
 * @mask: bits that are valid at the filter bit array.
 * @mode: mode of match: if bit is zero, it will match if equal (positive
 *	  match); if bit is one, it will match if the bit is negated.
 *
 * Note: All arrays in this struct have a size of DMX_FILTER_SIZE (16 bytes).
 */
struct dmx_filter {
	__u8  filter[DMX_FILTER_SIZE];
	__u8  mask[DMX_FILTER_SIZE];
	__u8  mode[DMX_FILTER_SIZE];
};

/**
 * struct dmx_sct_filter_params - Specifies a section filter.
 *
 * @pid: PID to be filtered.
 * @filter: section header filter, as defined by &struct dmx_filter.
 * @timeout: maximum time to filter, in milliseconds.
 * @flags: extra flags for the section filter.
 *
 * Carries the configuration for a MPEG-TS section filter.
 *
 * The @flags can be:
 *
 *	- %DMX_CHECK_CRC - only deliver sections where the CRC check succeeded;
 *	- %DMX_ONESHOT - disable the section filter after one section
 *	  has been delivered;
 *	- %DMX_IMMEDIATE_START - Start filter immediately without requiring a
 *	  :ref:`DMX_START`.
 */
struct dmx_sct_filter_params {
	__u16             pid;
	struct dmx_filter filter;
	__u32             timeout;
	__u32             flags;
#define DMX_CHECK_CRC       1
#define DMX_ONESHOT         2
#define DMX_IMMEDIATE_START 4
};

/**
 * struct dmx_pes_filter_params - Specifies Packetized Elementary Stream (PES)
 *	filter parameters.
 *
 * @pid:	PID to be filtered.
 * @input:	Demux input, as specified by &enum dmx_input.
 * @output:	Demux output, as specified by &enum dmx_output.
 * @pes_type:	Type of the pes filter, as specified by &enum dmx_pes_type.
 * @flags:	Demux PES flags.
 */
struct dmx_pes_filter_params {
	__u16           pid;
	enum dmx_input  input;
	enum dmx_output output;
	enum dmx_ts_pes pes_type;
	__u32           flags;
};

/**
 * struct dmx_stc - Stores System Time Counter (STC) information.
 *
 * @num: input data: number of the STC, from 0 to N.
 * @base: output: divisor for STC to get 90 kHz clock.
 * @stc: output: stc in @base * 90 kHz units.
 */
struct dmx_stc {
	unsigned int num;
	unsigned int base;
	__u64 stc;
};

#define DMX_START                _IO('o', 41)
#define DMX_STOP                 _IO('o', 42)
#define DMX_SET_FILTER           _IOW('o', 43, struct dmx_sct_filter_params)
#define DMX_SET_PES_FILTER       _IOW('o', 44, struct dmx_pes_filter_params)
#define DMX_SET_BUFFER_SIZE      _IO('o', 45)
#define DMX_GET_PES_PIDS         _IOR('o', 47, __u16[5])
#define DMX_GET_STC              _IOWR('o', 50, struct dmx_stc)
#define DMX_ADD_PID              _IOW('o', 51, __u16)
#define DMX_REMOVE_PID           _IOW('o', 52, __u16)

#if !defined(__KERNEL__)

/* This is needed for legacy userspace support */
typedef enum dmx_output dmx_output_t;
typedef enum dmx_input dmx_input_t;
typedef enum dmx_ts_pes dmx_pes_type_t;
typedef struct dmx_filter dmx_filter_t;

#endif

#endif /* _UAPI_DVBDMX_H_ */
