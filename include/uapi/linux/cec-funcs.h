/*
 * cec - HDMI Consumer Electronics Control message functions
 *
 * Copyright 2016 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * Alternatively you can redistribute this file under the terms of the
 * BSD license as stated below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. The names of its contributors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _CEC_UAPI_FUNCS_H
#define _CEC_UAPI_FUNCS_H

#include <linux/cec.h>

/* One Touch Play Feature */
static inline void cec_msg_active_source(struct cec_msg *msg, __u16 phys_addr)
{
	msg->len = 4;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_ACTIVE_SOURCE;
	msg->msg[2] = phys_addr >> 8;
	msg->msg[3] = phys_addr & 0xff;
}

static inline void cec_ops_active_source(const struct cec_msg *msg,
					 __u16 *phys_addr)
{
	*phys_addr = (msg->msg[2] << 8) | msg->msg[3];
}

static inline void cec_msg_image_view_on(struct cec_msg *msg)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_IMAGE_VIEW_ON;
}

static inline void cec_msg_text_view_on(struct cec_msg *msg)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_TEXT_VIEW_ON;
}


/* Routing Control Feature */
static inline void cec_msg_inactive_source(struct cec_msg *msg,
					   __u16 phys_addr)
{
	msg->len = 4;
	msg->msg[1] = CEC_MSG_INACTIVE_SOURCE;
	msg->msg[2] = phys_addr >> 8;
	msg->msg[3] = phys_addr & 0xff;
}

static inline void cec_ops_inactive_source(const struct cec_msg *msg,
					   __u16 *phys_addr)
{
	*phys_addr = (msg->msg[2] << 8) | msg->msg[3];
}

static inline void cec_msg_request_active_source(struct cec_msg *msg,
						 int reply)
{
	msg->len = 2;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_REQUEST_ACTIVE_SOURCE;
	msg->reply = reply ? CEC_MSG_ACTIVE_SOURCE : 0;
}

static inline void cec_msg_routing_information(struct cec_msg *msg,
					       __u16 phys_addr)
{
	msg->len = 4;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_ROUTING_INFORMATION;
	msg->msg[2] = phys_addr >> 8;
	msg->msg[3] = phys_addr & 0xff;
}

static inline void cec_ops_routing_information(const struct cec_msg *msg,
					       __u16 *phys_addr)
{
	*phys_addr = (msg->msg[2] << 8) | msg->msg[3];
}

static inline void cec_msg_routing_change(struct cec_msg *msg,
					  int reply,
					  __u16 orig_phys_addr,
					  __u16 new_phys_addr)
{
	msg->len = 6;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_ROUTING_CHANGE;
	msg->msg[2] = orig_phys_addr >> 8;
	msg->msg[3] = orig_phys_addr & 0xff;
	msg->msg[4] = new_phys_addr >> 8;
	msg->msg[5] = new_phys_addr & 0xff;
	msg->reply = reply ? CEC_MSG_ROUTING_INFORMATION : 0;
}

static inline void cec_ops_routing_change(const struct cec_msg *msg,
					  __u16 *orig_phys_addr,
					  __u16 *new_phys_addr)
{
	*orig_phys_addr = (msg->msg[2] << 8) | msg->msg[3];
	*new_phys_addr = (msg->msg[4] << 8) | msg->msg[5];
}

static inline void cec_msg_set_stream_path(struct cec_msg *msg, __u16 phys_addr)
{
	msg->len = 4;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_SET_STREAM_PATH;
	msg->msg[2] = phys_addr >> 8;
	msg->msg[3] = phys_addr & 0xff;
}

static inline void cec_ops_set_stream_path(const struct cec_msg *msg,
					   __u16 *phys_addr)
{
	*phys_addr = (msg->msg[2] << 8) | msg->msg[3];
}


/* Standby Feature */
static inline void cec_msg_standby(struct cec_msg *msg)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_STANDBY;
}


/* One Touch Record Feature */
static inline void cec_msg_record_off(struct cec_msg *msg, int reply)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_RECORD_OFF;
	msg->reply = reply ? CEC_MSG_RECORD_STATUS : 0;
}

struct cec_op_arib_data {
	__u16 transport_id;
	__u16 service_id;
	__u16 orig_network_id;
};

struct cec_op_atsc_data {
	__u16 transport_id;
	__u16 program_number;
};

struct cec_op_dvb_data {
	__u16 transport_id;
	__u16 service_id;
	__u16 orig_network_id;
};

struct cec_op_channel_data {
	__u8 channel_number_fmt;
	__u16 major;
	__u16 minor;
};

struct cec_op_digital_service_id {
	__u8 service_id_method;
	__u8 dig_bcast_system;
	union {
		struct cec_op_arib_data arib;
		struct cec_op_atsc_data atsc;
		struct cec_op_dvb_data dvb;
		struct cec_op_channel_data channel;
	};
};

struct cec_op_record_src {
	__u8 type;
	union {
		struct cec_op_digital_service_id digital;
		struct {
			__u8 ana_bcast_type;
			__u16 ana_freq;
			__u8 bcast_system;
		} analog;
		struct {
			__u8 plug;
		} ext_plug;
		struct {
			__u16 phys_addr;
		} ext_phys_addr;
	};
};

static inline void cec_set_digital_service_id(__u8 *msg,
	      const struct cec_op_digital_service_id *digital)
{
	*msg++ = (digital->service_id_method << 7) | digital->dig_bcast_system;
	if (digital->service_id_method == CEC_OP_SERVICE_ID_METHOD_BY_CHANNEL) {
		*msg++ = (digital->channel.channel_number_fmt << 2) |
			 (digital->channel.major >> 8);
		*msg++ = digital->channel.major & 0xff;
		*msg++ = digital->channel.minor >> 8;
		*msg++ = digital->channel.minor & 0xff;
		*msg++ = 0;
		*msg++ = 0;
		return;
	}
	switch (digital->dig_bcast_system) {
	case CEC_OP_DIG_SERVICE_BCAST_SYSTEM_ATSC_GEN:
	case CEC_OP_DIG_SERVICE_BCAST_SYSTEM_ATSC_CABLE:
	case CEC_OP_DIG_SERVICE_BCAST_SYSTEM_ATSC_SAT:
	case CEC_OP_DIG_SERVICE_BCAST_SYSTEM_ATSC_T:
		*msg++ = digital->atsc.transport_id >> 8;
		*msg++ = digital->atsc.transport_id & 0xff;
		*msg++ = digital->atsc.program_number >> 8;
		*msg++ = digital->atsc.program_number & 0xff;
		*msg++ = 0;
		*msg++ = 0;
		break;
	default:
		*msg++ = digital->dvb.transport_id >> 8;
		*msg++ = digital->dvb.transport_id & 0xff;
		*msg++ = digital->dvb.service_id >> 8;
		*msg++ = digital->dvb.service_id & 0xff;
		*msg++ = digital->dvb.orig_network_id >> 8;
		*msg++ = digital->dvb.orig_network_id & 0xff;
		break;
	}
}

static inline void cec_get_digital_service_id(const __u8 *msg,
	      struct cec_op_digital_service_id *digital)
{
	digital->service_id_method = msg[0] >> 7;
	digital->dig_bcast_system = msg[0] & 0x7f;
	if (digital->service_id_method == CEC_OP_SERVICE_ID_METHOD_BY_CHANNEL) {
		digital->channel.channel_number_fmt = msg[1] >> 2;
		digital->channel.major = ((msg[1] & 3) << 6) | msg[2];
		digital->channel.minor = (msg[3] << 8) | msg[4];
		return;
	}
	digital->dvb.transport_id = (msg[1] << 8) | msg[2];
	digital->dvb.service_id = (msg[3] << 8) | msg[4];
	digital->dvb.orig_network_id = (msg[5] << 8) | msg[6];
}

static inline void cec_msg_record_on_own(struct cec_msg *msg)
{
	msg->len = 3;
	msg->msg[1] = CEC_MSG_RECORD_ON;
	msg->msg[2] = CEC_OP_RECORD_SRC_OWN;
}

static inline void cec_msg_record_on_digital(struct cec_msg *msg,
			     const struct cec_op_digital_service_id *digital)
{
	msg->len = 10;
	msg->msg[1] = CEC_MSG_RECORD_ON;
	msg->msg[2] = CEC_OP_RECORD_SRC_DIGITAL;
	cec_set_digital_service_id(msg->msg + 3, digital);
}

static inline void cec_msg_record_on_analog(struct cec_msg *msg,
					    __u8 ana_bcast_type,
					    __u16 ana_freq,
					    __u8 bcast_system)
{
	msg->len = 7;
	msg->msg[1] = CEC_MSG_RECORD_ON;
	msg->msg[2] = CEC_OP_RECORD_SRC_ANALOG;
	msg->msg[3] = ana_bcast_type;
	msg->msg[4] = ana_freq >> 8;
	msg->msg[5] = ana_freq & 0xff;
	msg->msg[6] = bcast_system;
}

static inline void cec_msg_record_on_plug(struct cec_msg *msg,
					  __u8 plug)
{
	msg->len = 4;
	msg->msg[1] = CEC_MSG_RECORD_ON;
	msg->msg[2] = CEC_OP_RECORD_SRC_EXT_PLUG;
	msg->msg[3] = plug;
}

static inline void cec_msg_record_on_phys_addr(struct cec_msg *msg,
					       __u16 phys_addr)
{
	msg->len = 5;
	msg->msg[1] = CEC_MSG_RECORD_ON;
	msg->msg[2] = CEC_OP_RECORD_SRC_EXT_PHYS_ADDR;
	msg->msg[3] = phys_addr >> 8;
	msg->msg[4] = phys_addr & 0xff;
}

static inline void cec_msg_record_on(struct cec_msg *msg,
				     int reply,
				     const struct cec_op_record_src *rec_src)
{
	switch (rec_src->type) {
	case CEC_OP_RECORD_SRC_OWN:
		cec_msg_record_on_own(msg);
		break;
	case CEC_OP_RECORD_SRC_DIGITAL:
		cec_msg_record_on_digital(msg, &rec_src->digital);
		break;
	case CEC_OP_RECORD_SRC_ANALOG:
		cec_msg_record_on_analog(msg,
					 rec_src->analog.ana_bcast_type,
					 rec_src->analog.ana_freq,
					 rec_src->analog.bcast_system);
		break;
	case CEC_OP_RECORD_SRC_EXT_PLUG:
		cec_msg_record_on_plug(msg, rec_src->ext_plug.plug);
		break;
	case CEC_OP_RECORD_SRC_EXT_PHYS_ADDR:
		cec_msg_record_on_phys_addr(msg,
					    rec_src->ext_phys_addr.phys_addr);
		break;
	}
	msg->reply = reply ? CEC_MSG_RECORD_STATUS : 0;
}

static inline void cec_ops_record_on(const struct cec_msg *msg,
				     struct cec_op_record_src *rec_src)
{
	rec_src->type = msg->msg[2];
	switch (rec_src->type) {
	case CEC_OP_RECORD_SRC_OWN:
		break;
	case CEC_OP_RECORD_SRC_DIGITAL:
		cec_get_digital_service_id(msg->msg + 3, &rec_src->digital);
		break;
	case CEC_OP_RECORD_SRC_ANALOG:
		rec_src->analog.ana_bcast_type = msg->msg[3];
		rec_src->analog.ana_freq =
			(msg->msg[4] << 8) | msg->msg[5];
		rec_src->analog.bcast_system = msg->msg[6];
		break;
	case CEC_OP_RECORD_SRC_EXT_PLUG:
		rec_src->ext_plug.plug = msg->msg[3];
		break;
	case CEC_OP_RECORD_SRC_EXT_PHYS_ADDR:
		rec_src->ext_phys_addr.phys_addr =
			(msg->msg[3] << 8) | msg->msg[4];
		break;
	}
}

static inline void cec_msg_record_status(struct cec_msg *msg, __u8 rec_status)
{
	msg->len = 3;
	msg->msg[1] = CEC_MSG_RECORD_STATUS;
	msg->msg[2] = rec_status;
}

static inline void cec_ops_record_status(const struct cec_msg *msg,
					 __u8 *rec_status)
{
	*rec_status = msg->msg[2];
}

static inline void cec_msg_record_tv_screen(struct cec_msg *msg,
					    int reply)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_RECORD_TV_SCREEN;
	msg->reply = reply ? CEC_MSG_RECORD_ON : 0;
}


/* Timer Programming Feature */
static inline void cec_msg_timer_status(struct cec_msg *msg,
					__u8 timer_overlap_warning,
					__u8 media_info,
					__u8 prog_info,
					__u8 prog_error,
					__u8 duration_hr,
					__u8 duration_min)
{
	msg->len = 3;
	msg->msg[1] = CEC_MSG_TIMER_STATUS;
	msg->msg[2] = (timer_overlap_warning << 7) |
		(media_info << 5) |
		(prog_info ? 0x10 : 0) |
		(prog_info ? prog_info : prog_error);
	if (prog_info == CEC_OP_PROG_INFO_NOT_ENOUGH_SPACE ||
	    prog_info == CEC_OP_PROG_INFO_MIGHT_NOT_BE_ENOUGH_SPACE ||
	    prog_error == CEC_OP_PROG_ERROR_DUPLICATE) {
		msg->len += 2;
		msg->msg[3] = ((duration_hr / 10) << 4) | (duration_hr % 10);
		msg->msg[4] = ((duration_min / 10) << 4) | (duration_min % 10);
	}
}

static inline void cec_ops_timer_status(const struct cec_msg *msg,
					__u8 *timer_overlap_warning,
					__u8 *media_info,
					__u8 *prog_info,
					__u8 *prog_error,
					__u8 *duration_hr,
					__u8 *duration_min)
{
	*timer_overlap_warning = msg->msg[2] >> 7;
	*media_info = (msg->msg[2] >> 5) & 3;
	if (msg->msg[2] & 0x10) {
		*prog_info = msg->msg[2] & 0xf;
		*prog_error = 0;
	} else {
		*prog_info = 0;
		*prog_error = msg->msg[2] & 0xf;
	}
	if (*prog_info == CEC_OP_PROG_INFO_NOT_ENOUGH_SPACE ||
	    *prog_info == CEC_OP_PROG_INFO_MIGHT_NOT_BE_ENOUGH_SPACE ||
	    *prog_error == CEC_OP_PROG_ERROR_DUPLICATE) {
		*duration_hr = (msg->msg[3] >> 4) * 10 + (msg->msg[3] & 0xf);
		*duration_min = (msg->msg[4] >> 4) * 10 + (msg->msg[4] & 0xf);
	} else {
		*duration_hr = *duration_min = 0;
	}
}

static inline void cec_msg_timer_cleared_status(struct cec_msg *msg,
						__u8 timer_cleared_status)
{
	msg->len = 3;
	msg->msg[1] = CEC_MSG_TIMER_CLEARED_STATUS;
	msg->msg[2] = timer_cleared_status;
}

static inline void cec_ops_timer_cleared_status(const struct cec_msg *msg,
						__u8 *timer_cleared_status)
{
	*timer_cleared_status = msg->msg[2];
}

static inline void cec_msg_clear_analogue_timer(struct cec_msg *msg,
						int reply,
						__u8 day,
						__u8 month,
						__u8 start_hr,
						__u8 start_min,
						__u8 duration_hr,
						__u8 duration_min,
						__u8 recording_seq,
						__u8 ana_bcast_type,
						__u16 ana_freq,
						__u8 bcast_system)
{
	msg->len = 13;
	msg->msg[1] = CEC_MSG_CLEAR_ANALOGUE_TIMER;
	msg->msg[2] = day;
	msg->msg[3] = month;
	/* Hours and minutes are in BCD format */
	msg->msg[4] = ((start_hr / 10) << 4) | (start_hr % 10);
	msg->msg[5] = ((start_min / 10) << 4) | (start_min % 10);
	msg->msg[6] = ((duration_hr / 10) << 4) | (duration_hr % 10);
	msg->msg[7] = ((duration_min / 10) << 4) | (duration_min % 10);
	msg->msg[8] = recording_seq;
	msg->msg[9] = ana_bcast_type;
	msg->msg[10] = ana_freq >> 8;
	msg->msg[11] = ana_freq & 0xff;
	msg->msg[12] = bcast_system;
	msg->reply = reply ? CEC_MSG_TIMER_CLEARED_STATUS : 0;
}

static inline void cec_ops_clear_analogue_timer(const struct cec_msg *msg,
						__u8 *day,
						__u8 *month,
						__u8 *start_hr,
						__u8 *start_min,
						__u8 *duration_hr,
						__u8 *duration_min,
						__u8 *recording_seq,
						__u8 *ana_bcast_type,
						__u16 *ana_freq,
						__u8 *bcast_system)
{
	*day = msg->msg[2];
	*month = msg->msg[3];
	/* Hours and minutes are in BCD format */
	*start_hr = (msg->msg[4] >> 4) * 10 + (msg->msg[4] & 0xf);
	*start_min = (msg->msg[5] >> 4) * 10 + (msg->msg[5] & 0xf);
	*duration_hr = (msg->msg[6] >> 4) * 10 + (msg->msg[6] & 0xf);
	*duration_min = (msg->msg[7] >> 4) * 10 + (msg->msg[7] & 0xf);
	*recording_seq = msg->msg[8];
	*ana_bcast_type = msg->msg[9];
	*ana_freq = (msg->msg[10] << 8) | msg->msg[11];
	*bcast_system = msg->msg[12];
}

static inline void cec_msg_clear_digital_timer(struct cec_msg *msg,
				int reply,
				__u8 day,
				__u8 month,
				__u8 start_hr,
				__u8 start_min,
				__u8 duration_hr,
				__u8 duration_min,
				__u8 recording_seq,
				const struct cec_op_digital_service_id *digital)
{
	msg->len = 16;
	msg->reply = reply ? CEC_MSG_TIMER_CLEARED_STATUS : 0;
	msg->msg[1] = CEC_MSG_CLEAR_DIGITAL_TIMER;
	msg->msg[2] = day;
	msg->msg[3] = month;
	/* Hours and minutes are in BCD format */
	msg->msg[4] = ((start_hr / 10) << 4) | (start_hr % 10);
	msg->msg[5] = ((start_min / 10) << 4) | (start_min % 10);
	msg->msg[6] = ((duration_hr / 10) << 4) | (duration_hr % 10);
	msg->msg[7] = ((duration_min / 10) << 4) | (duration_min % 10);
	msg->msg[8] = recording_seq;
	cec_set_digital_service_id(msg->msg + 9, digital);
}

static inline void cec_ops_clear_digital_timer(const struct cec_msg *msg,
				__u8 *day,
				__u8 *month,
				__u8 *start_hr,
				__u8 *start_min,
				__u8 *duration_hr,
				__u8 *duration_min,
				__u8 *recording_seq,
				struct cec_op_digital_service_id *digital)
{
	*day = msg->msg[2];
	*month = msg->msg[3];
	/* Hours and minutes are in BCD format */
	*start_hr = (msg->msg[4] >> 4) * 10 + (msg->msg[4] & 0xf);
	*start_min = (msg->msg[5] >> 4) * 10 + (msg->msg[5] & 0xf);
	*duration_hr = (msg->msg[6] >> 4) * 10 + (msg->msg[6] & 0xf);
	*duration_min = (msg->msg[7] >> 4) * 10 + (msg->msg[7] & 0xf);
	*recording_seq = msg->msg[8];
	cec_get_digital_service_id(msg->msg + 9, digital);
}

static inline void cec_msg_clear_ext_timer(struct cec_msg *msg,
					   int reply,
					   __u8 day,
					   __u8 month,
					   __u8 start_hr,
					   __u8 start_min,
					   __u8 duration_hr,
					   __u8 duration_min,
					   __u8 recording_seq,
					   __u8 ext_src_spec,
					   __u8 plug,
					   __u16 phys_addr)
{
	msg->len = 13;
	msg->msg[1] = CEC_MSG_CLEAR_EXT_TIMER;
	msg->msg[2] = day;
	msg->msg[3] = month;
	/* Hours and minutes are in BCD format */
	msg->msg[4] = ((start_hr / 10) << 4) | (start_hr % 10);
	msg->msg[5] = ((start_min / 10) << 4) | (start_min % 10);
	msg->msg[6] = ((duration_hr / 10) << 4) | (duration_hr % 10);
	msg->msg[7] = ((duration_min / 10) << 4) | (duration_min % 10);
	msg->msg[8] = recording_seq;
	msg->msg[9] = ext_src_spec;
	msg->msg[10] = plug;
	msg->msg[11] = phys_addr >> 8;
	msg->msg[12] = phys_addr & 0xff;
	msg->reply = reply ? CEC_MSG_TIMER_CLEARED_STATUS : 0;
}

static inline void cec_ops_clear_ext_timer(const struct cec_msg *msg,
					   __u8 *day,
					   __u8 *month,
					   __u8 *start_hr,
					   __u8 *start_min,
					   __u8 *duration_hr,
					   __u8 *duration_min,
					   __u8 *recording_seq,
					   __u8 *ext_src_spec,
					   __u8 *plug,
					   __u16 *phys_addr)
{
	*day = msg->msg[2];
	*month = msg->msg[3];
	/* Hours and minutes are in BCD format */
	*start_hr = (msg->msg[4] >> 4) * 10 + (msg->msg[4] & 0xf);
	*start_min = (msg->msg[5] >> 4) * 10 + (msg->msg[5] & 0xf);
	*duration_hr = (msg->msg[6] >> 4) * 10 + (msg->msg[6] & 0xf);
	*duration_min = (msg->msg[7] >> 4) * 10 + (msg->msg[7] & 0xf);
	*recording_seq = msg->msg[8];
	*ext_src_spec = msg->msg[9];
	*plug = msg->msg[10];
	*phys_addr = (msg->msg[11] << 8) | msg->msg[12];
}

static inline void cec_msg_set_analogue_timer(struct cec_msg *msg,
					      int reply,
					      __u8 day,
					      __u8 month,
					      __u8 start_hr,
					      __u8 start_min,
					      __u8 duration_hr,
					      __u8 duration_min,
					      __u8 recording_seq,
					      __u8 ana_bcast_type,
					      __u16 ana_freq,
					      __u8 bcast_system)
{
	msg->len = 13;
	msg->msg[1] = CEC_MSG_SET_ANALOGUE_TIMER;
	msg->msg[2] = day;
	msg->msg[3] = month;
	/* Hours and minutes are in BCD format */
	msg->msg[4] = ((start_hr / 10) << 4) | (start_hr % 10);
	msg->msg[5] = ((start_min / 10) << 4) | (start_min % 10);
	msg->msg[6] = ((duration_hr / 10) << 4) | (duration_hr % 10);
	msg->msg[7] = ((duration_min / 10) << 4) | (duration_min % 10);
	msg->msg[8] = recording_seq;
	msg->msg[9] = ana_bcast_type;
	msg->msg[10] = ana_freq >> 8;
	msg->msg[11] = ana_freq & 0xff;
	msg->msg[12] = bcast_system;
	msg->reply = reply ? CEC_MSG_TIMER_STATUS : 0;
}

static inline void cec_ops_set_analogue_timer(const struct cec_msg *msg,
					      __u8 *day,
					      __u8 *month,
					      __u8 *start_hr,
					      __u8 *start_min,
					      __u8 *duration_hr,
					      __u8 *duration_min,
					      __u8 *recording_seq,
					      __u8 *ana_bcast_type,
					      __u16 *ana_freq,
					      __u8 *bcast_system)
{
	*day = msg->msg[2];
	*month = msg->msg[3];
	/* Hours and minutes are in BCD format */
	*start_hr = (msg->msg[4] >> 4) * 10 + (msg->msg[4] & 0xf);
	*start_min = (msg->msg[5] >> 4) * 10 + (msg->msg[5] & 0xf);
	*duration_hr = (msg->msg[6] >> 4) * 10 + (msg->msg[6] & 0xf);
	*duration_min = (msg->msg[7] >> 4) * 10 + (msg->msg[7] & 0xf);
	*recording_seq = msg->msg[8];
	*ana_bcast_type = msg->msg[9];
	*ana_freq = (msg->msg[10] << 8) | msg->msg[11];
	*bcast_system = msg->msg[12];
}

static inline void cec_msg_set_digital_timer(struct cec_msg *msg,
			int reply,
			__u8 day,
			__u8 month,
			__u8 start_hr,
			__u8 start_min,
			__u8 duration_hr,
			__u8 duration_min,
			__u8 recording_seq,
			const struct cec_op_digital_service_id *digital)
{
	msg->len = 16;
	msg->reply = reply ? CEC_MSG_TIMER_STATUS : 0;
	msg->msg[1] = CEC_MSG_SET_DIGITAL_TIMER;
	msg->msg[2] = day;
	msg->msg[3] = month;
	/* Hours and minutes are in BCD format */
	msg->msg[4] = ((start_hr / 10) << 4) | (start_hr % 10);
	msg->msg[5] = ((start_min / 10) << 4) | (start_min % 10);
	msg->msg[6] = ((duration_hr / 10) << 4) | (duration_hr % 10);
	msg->msg[7] = ((duration_min / 10) << 4) | (duration_min % 10);
	msg->msg[8] = recording_seq;
	cec_set_digital_service_id(msg->msg + 9, digital);
}

static inline void cec_ops_set_digital_timer(const struct cec_msg *msg,
			__u8 *day,
			__u8 *month,
			__u8 *start_hr,
			__u8 *start_min,
			__u8 *duration_hr,
			__u8 *duration_min,
			__u8 *recording_seq,
			struct cec_op_digital_service_id *digital)
{
	*day = msg->msg[2];
	*month = msg->msg[3];
	/* Hours and minutes are in BCD format */
	*start_hr = (msg->msg[4] >> 4) * 10 + (msg->msg[4] & 0xf);
	*start_min = (msg->msg[5] >> 4) * 10 + (msg->msg[5] & 0xf);
	*duration_hr = (msg->msg[6] >> 4) * 10 + (msg->msg[6] & 0xf);
	*duration_min = (msg->msg[7] >> 4) * 10 + (msg->msg[7] & 0xf);
	*recording_seq = msg->msg[8];
	cec_get_digital_service_id(msg->msg + 9, digital);
}

static inline void cec_msg_set_ext_timer(struct cec_msg *msg,
					 int reply,
					 __u8 day,
					 __u8 month,
					 __u8 start_hr,
					 __u8 start_min,
					 __u8 duration_hr,
					 __u8 duration_min,
					 __u8 recording_seq,
					 __u8 ext_src_spec,
					 __u8 plug,
					 __u16 phys_addr)
{
	msg->len = 13;
	msg->msg[1] = CEC_MSG_SET_EXT_TIMER;
	msg->msg[2] = day;
	msg->msg[3] = month;
	/* Hours and minutes are in BCD format */
	msg->msg[4] = ((start_hr / 10) << 4) | (start_hr % 10);
	msg->msg[5] = ((start_min / 10) << 4) | (start_min % 10);
	msg->msg[6] = ((duration_hr / 10) << 4) | (duration_hr % 10);
	msg->msg[7] = ((duration_min / 10) << 4) | (duration_min % 10);
	msg->msg[8] = recording_seq;
	msg->msg[9] = ext_src_spec;
	msg->msg[10] = plug;
	msg->msg[11] = phys_addr >> 8;
	msg->msg[12] = phys_addr & 0xff;
	msg->reply = reply ? CEC_MSG_TIMER_STATUS : 0;
}

static inline void cec_ops_set_ext_timer(const struct cec_msg *msg,
					 __u8 *day,
					 __u8 *month,
					 __u8 *start_hr,
					 __u8 *start_min,
					 __u8 *duration_hr,
					 __u8 *duration_min,
					 __u8 *recording_seq,
					 __u8 *ext_src_spec,
					 __u8 *plug,
					 __u16 *phys_addr)
{
	*day = msg->msg[2];
	*month = msg->msg[3];
	/* Hours and minutes are in BCD format */
	*start_hr = (msg->msg[4] >> 4) * 10 + (msg->msg[4] & 0xf);
	*start_min = (msg->msg[5] >> 4) * 10 + (msg->msg[5] & 0xf);
	*duration_hr = (msg->msg[6] >> 4) * 10 + (msg->msg[6] & 0xf);
	*duration_min = (msg->msg[7] >> 4) * 10 + (msg->msg[7] & 0xf);
	*recording_seq = msg->msg[8];
	*ext_src_spec = msg->msg[9];
	*plug = msg->msg[10];
	*phys_addr = (msg->msg[11] << 8) | msg->msg[12];
}

static inline void cec_msg_set_timer_program_title(struct cec_msg *msg,
						   const char *prog_title)
{
	unsigned int len = strlen(prog_title);

	if (len > 14)
		len = 14;
	msg->len = 2 + len;
	msg->msg[1] = CEC_MSG_SET_TIMER_PROGRAM_TITLE;
	memcpy(msg->msg + 2, prog_title, len);
}

static inline void cec_ops_set_timer_program_title(const struct cec_msg *msg,
						   char *prog_title)
{
	unsigned int len = msg->len > 2 ? msg->len - 2 : 0;

	if (len > 14)
		len = 14;
	memcpy(prog_title, msg->msg + 2, len);
	prog_title[len] = '\0';
}

/* System Information Feature */
static inline void cec_msg_cec_version(struct cec_msg *msg, __u8 cec_version)
{
	msg->len = 3;
	msg->msg[1] = CEC_MSG_CEC_VERSION;
	msg->msg[2] = cec_version;
}

static inline void cec_ops_cec_version(const struct cec_msg *msg,
				       __u8 *cec_version)
{
	*cec_version = msg->msg[2];
}

static inline void cec_msg_get_cec_version(struct cec_msg *msg,
					   int reply)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_GET_CEC_VERSION;
	msg->reply = reply ? CEC_MSG_CEC_VERSION : 0;
}

static inline void cec_msg_report_physical_addr(struct cec_msg *msg,
					__u16 phys_addr, __u8 prim_devtype)
{
	msg->len = 5;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_REPORT_PHYSICAL_ADDR;
	msg->msg[2] = phys_addr >> 8;
	msg->msg[3] = phys_addr & 0xff;
	msg->msg[4] = prim_devtype;
}

static inline void cec_ops_report_physical_addr(const struct cec_msg *msg,
					__u16 *phys_addr, __u8 *prim_devtype)
{
	*phys_addr = (msg->msg[2] << 8) | msg->msg[3];
	*prim_devtype = msg->msg[4];
}

static inline void cec_msg_give_physical_addr(struct cec_msg *msg,
					      int reply)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_GIVE_PHYSICAL_ADDR;
	msg->reply = reply ? CEC_MSG_REPORT_PHYSICAL_ADDR : 0;
}

static inline void cec_msg_set_menu_language(struct cec_msg *msg,
					     const char *language)
{
	msg->len = 5;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_SET_MENU_LANGUAGE;
	memcpy(msg->msg + 2, language, 3);
}

static inline void cec_ops_set_menu_language(const struct cec_msg *msg,
					     char *language)
{
	memcpy(language, msg->msg + 2, 3);
	language[3] = '\0';
}

static inline void cec_msg_get_menu_language(struct cec_msg *msg,
					     int reply)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_GET_MENU_LANGUAGE;
	msg->reply = reply ? CEC_MSG_SET_MENU_LANGUAGE : 0;
}

/*
 * Assumes a single RC Profile byte and a single Device Features byte,
 * i.e. no extended features are supported by this helper function.
 *
 * As of CEC 2.0 no extended features are defined, should those be added
 * in the future, then this function needs to be adapted or a new function
 * should be added.
 */
static inline void cec_msg_report_features(struct cec_msg *msg,
				__u8 cec_version, __u8 all_device_types,
				__u8 rc_profile, __u8 dev_features)
{
	msg->len = 6;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_REPORT_FEATURES;
	msg->msg[2] = cec_version;
	msg->msg[3] = all_device_types;
	msg->msg[4] = rc_profile;
	msg->msg[5] = dev_features;
}

static inline void cec_ops_report_features(const struct cec_msg *msg,
			__u8 *cec_version, __u8 *all_device_types,
			const __u8 **rc_profile, const __u8 **dev_features)
{
	const __u8 *p = &msg->msg[4];

	*cec_version = msg->msg[2];
	*all_device_types = msg->msg[3];
	*rc_profile = p;
	while (p < &msg->msg[14] && (*p & CEC_OP_FEAT_EXT))
		p++;
	if (!(*p & CEC_OP_FEAT_EXT)) {
		*dev_features = p + 1;
		while (p < &msg->msg[15] && (*p & CEC_OP_FEAT_EXT))
			p++;
	}
	if (*p & CEC_OP_FEAT_EXT)
		*rc_profile = *dev_features = NULL;
}

static inline void cec_msg_give_features(struct cec_msg *msg,
					 int reply)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_GIVE_FEATURES;
	msg->reply = reply ? CEC_MSG_REPORT_FEATURES : 0;
}

/* Deck Control Feature */
static inline void cec_msg_deck_control(struct cec_msg *msg,
					__u8 deck_control_mode)
{
	msg->len = 3;
	msg->msg[1] = CEC_MSG_DECK_CONTROL;
	msg->msg[2] = deck_control_mode;
}

static inline void cec_ops_deck_control(const struct cec_msg *msg,
					__u8 *deck_control_mode)
{
	*deck_control_mode = msg->msg[2];
}

static inline void cec_msg_deck_status(struct cec_msg *msg,
				       __u8 deck_info)
{
	msg->len = 3;
	msg->msg[1] = CEC_MSG_DECK_STATUS;
	msg->msg[2] = deck_info;
}

static inline void cec_ops_deck_status(const struct cec_msg *msg,
				       __u8 *deck_info)
{
	*deck_info = msg->msg[2];
}

static inline void cec_msg_give_deck_status(struct cec_msg *msg,
					    int reply,
					    __u8 status_req)
{
	msg->len = 3;
	msg->msg[1] = CEC_MSG_GIVE_DECK_STATUS;
	msg->msg[2] = status_req;
	msg->reply = reply ? CEC_MSG_DECK_STATUS : 0;
}

static inline void cec_ops_give_deck_status(const struct cec_msg *msg,
					    __u8 *status_req)
{
	*status_req = msg->msg[2];
}

static inline void cec_msg_play(struct cec_msg *msg,
				__u8 play_mode)
{
	msg->len = 3;
	msg->msg[1] = CEC_MSG_PLAY;
	msg->msg[2] = play_mode;
}

static inline void cec_ops_play(const struct cec_msg *msg,
				__u8 *play_mode)
{
	*play_mode = msg->msg[2];
}


/* Tuner Control Feature */
struct cec_op_tuner_device_info {
	__u8 rec_flag;
	__u8 tuner_display_info;
	__u8 is_analog;
	union {
		struct cec_op_digital_service_id digital;
		struct {
			__u8 ana_bcast_type;
			__u16 ana_freq;
			__u8 bcast_system;
		} analog;
	};
};

static inline void cec_msg_tuner_device_status_analog(struct cec_msg *msg,
						      __u8 rec_flag,
						      __u8 tuner_display_info,
						      __u8 ana_bcast_type,
						      __u16 ana_freq,
						      __u8 bcast_system)
{
	msg->len = 7;
	msg->msg[1] = CEC_MSG_TUNER_DEVICE_STATUS;
	msg->msg[2] = (rec_flag << 7) | tuner_display_info;
	msg->msg[3] = ana_bcast_type;
	msg->msg[4] = ana_freq >> 8;
	msg->msg[5] = ana_freq & 0xff;
	msg->msg[6] = bcast_system;
}

static inline void cec_msg_tuner_device_status_digital(struct cec_msg *msg,
		   __u8 rec_flag, __u8 tuner_display_info,
		   const struct cec_op_digital_service_id *digital)
{
	msg->len = 10;
	msg->msg[1] = CEC_MSG_TUNER_DEVICE_STATUS;
	msg->msg[2] = (rec_flag << 7) | tuner_display_info;
	cec_set_digital_service_id(msg->msg + 3, digital);
}

static inline void cec_msg_tuner_device_status(struct cec_msg *msg,
			const struct cec_op_tuner_device_info *tuner_dev_info)
{
	if (tuner_dev_info->is_analog)
		cec_msg_tuner_device_status_analog(msg,
			tuner_dev_info->rec_flag,
			tuner_dev_info->tuner_display_info,
			tuner_dev_info->analog.ana_bcast_type,
			tuner_dev_info->analog.ana_freq,
			tuner_dev_info->analog.bcast_system);
	else
		cec_msg_tuner_device_status_digital(msg,
			tuner_dev_info->rec_flag,
			tuner_dev_info->tuner_display_info,
			&tuner_dev_info->digital);
}

static inline void cec_ops_tuner_device_status(const struct cec_msg *msg,
				struct cec_op_tuner_device_info *tuner_dev_info)
{
	tuner_dev_info->is_analog = msg->len < 10;
	tuner_dev_info->rec_flag = msg->msg[2] >> 7;
	tuner_dev_info->tuner_display_info = msg->msg[2] & 0x7f;
	if (tuner_dev_info->is_analog) {
		tuner_dev_info->analog.ana_bcast_type = msg->msg[3];
		tuner_dev_info->analog.ana_freq = (msg->msg[4] << 8) | msg->msg[5];
		tuner_dev_info->analog.bcast_system = msg->msg[6];
		return;
	}
	cec_get_digital_service_id(msg->msg + 3, &tuner_dev_info->digital);
}

static inline void cec_msg_give_tuner_device_status(struct cec_msg *msg,
						    int reply,
						    __u8 status_req)
{
	msg->len = 3;
	msg->msg[1] = CEC_MSG_GIVE_TUNER_DEVICE_STATUS;
	msg->msg[2] = status_req;
	msg->reply = reply ? CEC_MSG_TUNER_DEVICE_STATUS : 0;
}

static inline void cec_ops_give_tuner_device_status(const struct cec_msg *msg,
						    __u8 *status_req)
{
	*status_req = msg->msg[2];
}

static inline void cec_msg_select_analogue_service(struct cec_msg *msg,
						   __u8 ana_bcast_type,
						   __u16 ana_freq,
						   __u8 bcast_system)
{
	msg->len = 6;
	msg->msg[1] = CEC_MSG_SELECT_ANALOGUE_SERVICE;
	msg->msg[2] = ana_bcast_type;
	msg->msg[3] = ana_freq >> 8;
	msg->msg[4] = ana_freq & 0xff;
	msg->msg[5] = bcast_system;
}

static inline void cec_ops_select_analogue_service(const struct cec_msg *msg,
						   __u8 *ana_bcast_type,
						   __u16 *ana_freq,
						   __u8 *bcast_system)
{
	*ana_bcast_type = msg->msg[2];
	*ana_freq = (msg->msg[3] << 8) | msg->msg[4];
	*bcast_system = msg->msg[5];
}

static inline void cec_msg_select_digital_service(struct cec_msg *msg,
				const struct cec_op_digital_service_id *digital)
{
	msg->len = 9;
	msg->msg[1] = CEC_MSG_SELECT_DIGITAL_SERVICE;
	cec_set_digital_service_id(msg->msg + 2, digital);
}

static inline void cec_ops_select_digital_service(const struct cec_msg *msg,
				struct cec_op_digital_service_id *digital)
{
	cec_get_digital_service_id(msg->msg + 2, digital);
}

static inline void cec_msg_tuner_step_decrement(struct cec_msg *msg)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_TUNER_STEP_DECREMENT;
}

static inline void cec_msg_tuner_step_increment(struct cec_msg *msg)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_TUNER_STEP_INCREMENT;
}


/* Vendor Specific Commands Feature */
static inline void cec_msg_device_vendor_id(struct cec_msg *msg, __u32 vendor_id)
{
	msg->len = 5;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_DEVICE_VENDOR_ID;
	msg->msg[2] = vendor_id >> 16;
	msg->msg[3] = (vendor_id >> 8) & 0xff;
	msg->msg[4] = vendor_id & 0xff;
}

static inline void cec_ops_device_vendor_id(const struct cec_msg *msg,
					    __u32 *vendor_id)
{
	*vendor_id = (msg->msg[2] << 16) | (msg->msg[3] << 8) | msg->msg[4];
}

static inline void cec_msg_give_device_vendor_id(struct cec_msg *msg,
						 int reply)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_GIVE_DEVICE_VENDOR_ID;
	msg->reply = reply ? CEC_MSG_DEVICE_VENDOR_ID : 0;
}

static inline void cec_msg_vendor_command(struct cec_msg *msg,
					  __u8 size, const __u8 *vendor_cmd)
{
	if (size > 14)
		size = 14;
	msg->len = 2 + size;
	msg->msg[1] = CEC_MSG_VENDOR_COMMAND;
	memcpy(msg->msg + 2, vendor_cmd, size);
}

static inline void cec_ops_vendor_command(const struct cec_msg *msg,
					  __u8 *size,
					  const __u8 **vendor_cmd)
{
	*size = msg->len - 2;

	if (*size > 14)
		*size = 14;
	*vendor_cmd = msg->msg + 2;
}

static inline void cec_msg_vendor_command_with_id(struct cec_msg *msg,
						  __u32 vendor_id, __u8 size,
						  const __u8 *vendor_cmd)
{
	if (size > 11)
		size = 11;
	msg->len = 5 + size;
	msg->msg[1] = CEC_MSG_VENDOR_COMMAND_WITH_ID;
	msg->msg[2] = vendor_id >> 16;
	msg->msg[3] = (vendor_id >> 8) & 0xff;
	msg->msg[4] = vendor_id & 0xff;
	memcpy(msg->msg + 5, vendor_cmd, size);
}

static inline void cec_ops_vendor_command_with_id(const struct cec_msg *msg,
						  __u32 *vendor_id,  __u8 *size,
						  const __u8 **vendor_cmd)
{
	*size = msg->len - 5;

	if (*size > 11)
		*size = 11;
	*vendor_id = (msg->msg[2] << 16) | (msg->msg[3] << 8) | msg->msg[4];
	*vendor_cmd = msg->msg + 5;
}

static inline void cec_msg_vendor_remote_button_down(struct cec_msg *msg,
						     __u8 size,
						     const __u8 *rc_code)
{
	if (size > 14)
		size = 14;
	msg->len = 2 + size;
	msg->msg[1] = CEC_MSG_VENDOR_REMOTE_BUTTON_DOWN;
	memcpy(msg->msg + 2, rc_code, size);
}

static inline void cec_ops_vendor_remote_button_down(const struct cec_msg *msg,
						     __u8 *size,
						     const __u8 **rc_code)
{
	*size = msg->len - 2;

	if (*size > 14)
		*size = 14;
	*rc_code = msg->msg + 2;
}

static inline void cec_msg_vendor_remote_button_up(struct cec_msg *msg)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_VENDOR_REMOTE_BUTTON_UP;
}


/* OSD Display Feature */
static inline void cec_msg_set_osd_string(struct cec_msg *msg,
					  __u8 disp_ctl,
					  const char *osd)
{
	unsigned int len = strlen(osd);

	if (len > 13)
		len = 13;
	msg->len = 3 + len;
	msg->msg[1] = CEC_MSG_SET_OSD_STRING;
	msg->msg[2] = disp_ctl;
	memcpy(msg->msg + 3, osd, len);
}

static inline void cec_ops_set_osd_string(const struct cec_msg *msg,
					  __u8 *disp_ctl,
					  char *osd)
{
	unsigned int len = msg->len > 3 ? msg->len - 3 : 0;

	*disp_ctl = msg->msg[2];
	if (len > 13)
		len = 13;
	memcpy(osd, msg->msg + 3, len);
	osd[len] = '\0';
}


/* Device OSD Transfer Feature */
static inline void cec_msg_set_osd_name(struct cec_msg *msg, const char *name)
{
	unsigned int len = strlen(name);

	if (len > 14)
		len = 14;
	msg->len = 2 + len;
	msg->msg[1] = CEC_MSG_SET_OSD_NAME;
	memcpy(msg->msg + 2, name, len);
}

static inline void cec_ops_set_osd_name(const struct cec_msg *msg,
					char *name)
{
	unsigned int len = msg->len > 2 ? msg->len - 2 : 0;

	if (len > 14)
		len = 14;
	memcpy(name, msg->msg + 2, len);
	name[len] = '\0';
}

static inline void cec_msg_give_osd_name(struct cec_msg *msg,
					 int reply)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_GIVE_OSD_NAME;
	msg->reply = reply ? CEC_MSG_SET_OSD_NAME : 0;
}


/* Device Menu Control Feature */
static inline void cec_msg_menu_status(struct cec_msg *msg,
				       __u8 menu_state)
{
	msg->len = 3;
	msg->msg[1] = CEC_MSG_MENU_STATUS;
	msg->msg[2] = menu_state;
}

static inline void cec_ops_menu_status(const struct cec_msg *msg,
				       __u8 *menu_state)
{
	*menu_state = msg->msg[2];
}

static inline void cec_msg_menu_request(struct cec_msg *msg,
					int reply,
					__u8 menu_req)
{
	msg->len = 3;
	msg->msg[1] = CEC_MSG_MENU_REQUEST;
	msg->msg[2] = menu_req;
	msg->reply = reply ? CEC_MSG_MENU_STATUS : 0;
}

static inline void cec_ops_menu_request(const struct cec_msg *msg,
					__u8 *menu_req)
{
	*menu_req = msg->msg[2];
}

struct cec_op_ui_command {
	__u8 ui_cmd;
	__u8 has_opt_arg;
	union {
		struct cec_op_channel_data channel_identifier;
		__u8 ui_broadcast_type;
		__u8 ui_sound_presentation_control;
		__u8 play_mode;
		__u8 ui_function_media;
		__u8 ui_function_select_av_input;
		__u8 ui_function_select_audio_input;
	};
};

static inline void cec_msg_user_control_pressed(struct cec_msg *msg,
					const struct cec_op_ui_command *ui_cmd)
{
	msg->len = 3;
	msg->msg[1] = CEC_MSG_USER_CONTROL_PRESSED;
	msg->msg[2] = ui_cmd->ui_cmd;
	if (!ui_cmd->has_opt_arg)
		return;
	switch (ui_cmd->ui_cmd) {
	case 0x56:
	case 0x57:
	case 0x60:
	case 0x68:
	case 0x69:
	case 0x6a:
		/* The optional operand is one byte for all these ui commands */
		msg->len++;
		msg->msg[3] = ui_cmd->play_mode;
		break;
	case 0x67:
		msg->len += 4;
		msg->msg[3] = (ui_cmd->channel_identifier.channel_number_fmt << 2) |
			      (ui_cmd->channel_identifier.major >> 8);
		msg->msg[4] = ui_cmd->channel_identifier.major & 0xff;
		msg->msg[5] = ui_cmd->channel_identifier.minor >> 8;
		msg->msg[6] = ui_cmd->channel_identifier.minor & 0xff;
		break;
	}
}

static inline void cec_ops_user_control_pressed(const struct cec_msg *msg,
						struct cec_op_ui_command *ui_cmd)
{
	ui_cmd->ui_cmd = msg->msg[2];
	ui_cmd->has_opt_arg = 0;
	if (msg->len == 3)
		return;
	switch (ui_cmd->ui_cmd) {
	case 0x56:
	case 0x57:
	case 0x60:
	case 0x68:
	case 0x69:
	case 0x6a:
		/* The optional operand is one byte for all these ui commands */
		ui_cmd->play_mode = msg->msg[3];
		ui_cmd->has_opt_arg = 1;
		break;
	case 0x67:
		if (msg->len < 7)
			break;
		ui_cmd->has_opt_arg = 1;
		ui_cmd->channel_identifier.channel_number_fmt = msg->msg[3] >> 2;
		ui_cmd->channel_identifier.major = ((msg->msg[3] & 3) << 6) | msg->msg[4];
		ui_cmd->channel_identifier.minor = (msg->msg[5] << 8) | msg->msg[6];
		break;
	}
}

static inline void cec_msg_user_control_released(struct cec_msg *msg)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_USER_CONTROL_RELEASED;
}

/* Remote Control Passthrough Feature */

/* Power Status Feature */
static inline void cec_msg_report_power_status(struct cec_msg *msg,
					       __u8 pwr_state)
{
	msg->len = 3;
	msg->msg[1] = CEC_MSG_REPORT_POWER_STATUS;
	msg->msg[2] = pwr_state;
}

static inline void cec_ops_report_power_status(const struct cec_msg *msg,
					       __u8 *pwr_state)
{
	*pwr_state = msg->msg[2];
}

static inline void cec_msg_give_device_power_status(struct cec_msg *msg,
						    int reply)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_GIVE_DEVICE_POWER_STATUS;
	msg->reply = reply ? CEC_MSG_REPORT_POWER_STATUS : 0;
}

/* General Protocol Messages */
static inline void cec_msg_feature_abort(struct cec_msg *msg,
					 __u8 abort_msg, __u8 reason)
{
	msg->len = 4;
	msg->msg[1] = CEC_MSG_FEATURE_ABORT;
	msg->msg[2] = abort_msg;
	msg->msg[3] = reason;
}

static inline void cec_ops_feature_abort(const struct cec_msg *msg,
					 __u8 *abort_msg, __u8 *reason)
{
	*abort_msg = msg->msg[2];
	*reason = msg->msg[3];
}

/* This changes the current message into a feature abort message */
static inline void cec_msg_reply_feature_abort(struct cec_msg *msg, __u8 reason)
{
	cec_msg_set_reply_to(msg, msg);
	msg->len = 4;
	msg->msg[2] = msg->msg[1];
	msg->msg[3] = reason;
	msg->msg[1] = CEC_MSG_FEATURE_ABORT;
}

static inline void cec_msg_abort(struct cec_msg *msg)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_ABORT;
}


/* System Audio Control Feature */
static inline void cec_msg_report_audio_status(struct cec_msg *msg,
					       __u8 aud_mute_status,
					       __u8 aud_vol_status)
{
	msg->len = 3;
	msg->msg[1] = CEC_MSG_REPORT_AUDIO_STATUS;
	msg->msg[2] = (aud_mute_status << 7) | (aud_vol_status & 0x7f);
}

static inline void cec_ops_report_audio_status(const struct cec_msg *msg,
					       __u8 *aud_mute_status,
					       __u8 *aud_vol_status)
{
	*aud_mute_status = msg->msg[2] >> 7;
	*aud_vol_status = msg->msg[2] & 0x7f;
}

static inline void cec_msg_give_audio_status(struct cec_msg *msg,
					     int reply)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_GIVE_AUDIO_STATUS;
	msg->reply = reply ? CEC_MSG_REPORT_AUDIO_STATUS : 0;
}

static inline void cec_msg_set_system_audio_mode(struct cec_msg *msg,
						 __u8 sys_aud_status)
{
	msg->len = 3;
	msg->msg[1] = CEC_MSG_SET_SYSTEM_AUDIO_MODE;
	msg->msg[2] = sys_aud_status;
}

static inline void cec_ops_set_system_audio_mode(const struct cec_msg *msg,
						 __u8 *sys_aud_status)
{
	*sys_aud_status = msg->msg[2];
}

static inline void cec_msg_system_audio_mode_request(struct cec_msg *msg,
						     int reply,
						     __u16 phys_addr)
{
	msg->len = phys_addr == 0xffff ? 2 : 4;
	msg->msg[1] = CEC_MSG_SYSTEM_AUDIO_MODE_REQUEST;
	msg->msg[2] = phys_addr >> 8;
	msg->msg[3] = phys_addr & 0xff;
	msg->reply = reply ? CEC_MSG_SET_SYSTEM_AUDIO_MODE : 0;

}

static inline void cec_ops_system_audio_mode_request(const struct cec_msg *msg,
						     __u16 *phys_addr)
{
	if (msg->len < 4)
		*phys_addr = 0xffff;
	else
		*phys_addr = (msg->msg[2] << 8) | msg->msg[3];
}

static inline void cec_msg_system_audio_mode_status(struct cec_msg *msg,
						    __u8 sys_aud_status)
{
	msg->len = 3;
	msg->msg[1] = CEC_MSG_SYSTEM_AUDIO_MODE_STATUS;
	msg->msg[2] = sys_aud_status;
}

static inline void cec_ops_system_audio_mode_status(const struct cec_msg *msg,
						    __u8 *sys_aud_status)
{
	*sys_aud_status = msg->msg[2];
}

static inline void cec_msg_give_system_audio_mode_status(struct cec_msg *msg,
							 int reply)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_GIVE_SYSTEM_AUDIO_MODE_STATUS;
	msg->reply = reply ? CEC_MSG_SYSTEM_AUDIO_MODE_STATUS : 0;
}

static inline void cec_msg_report_short_audio_descriptor(struct cec_msg *msg,
					__u8 num_descriptors,
					const __u32 *descriptors)
{
	unsigned int i;

	if (num_descriptors > 4)
		num_descriptors = 4;
	msg->len = 2 + num_descriptors * 3;
	msg->msg[1] = CEC_MSG_REPORT_SHORT_AUDIO_DESCRIPTOR;
	for (i = 0; i < num_descriptors; i++) {
		msg->msg[2 + i * 3] = (descriptors[i] >> 16) & 0xff;
		msg->msg[3 + i * 3] = (descriptors[i] >> 8) & 0xff;
		msg->msg[4 + i * 3] = descriptors[i] & 0xff;
	}
}

static inline void cec_ops_report_short_audio_descriptor(const struct cec_msg *msg,
							 __u8 *num_descriptors,
							 __u32 *descriptors)
{
	unsigned int i;

	*num_descriptors = (msg->len - 2) / 3;
	if (*num_descriptors > 4)
		*num_descriptors = 4;
	for (i = 0; i < *num_descriptors; i++)
		descriptors[i] = (msg->msg[2 + i * 3] << 16) |
			(msg->msg[3 + i * 3] << 8) |
			msg->msg[4 + i * 3];
}

static inline void cec_msg_request_short_audio_descriptor(struct cec_msg *msg,
					int reply,
					__u8 num_descriptors,
					const __u8 *audio_format_id,
					const __u8 *audio_format_code)
{
	unsigned int i;

	if (num_descriptors > 4)
		num_descriptors = 4;
	msg->len = 2 + num_descriptors;
	msg->msg[1] = CEC_MSG_REQUEST_SHORT_AUDIO_DESCRIPTOR;
	msg->reply = reply ? CEC_MSG_REPORT_SHORT_AUDIO_DESCRIPTOR : 0;
	for (i = 0; i < num_descriptors; i++)
		msg->msg[2 + i] = (audio_format_id[i] << 6) |
				  (audio_format_code[i] & 0x3f);
}

static inline void cec_ops_request_short_audio_descriptor(const struct cec_msg *msg,
					__u8 *num_descriptors,
					__u8 *audio_format_id,
					__u8 *audio_format_code)
{
	unsigned int i;

	*num_descriptors = msg->len - 2;
	if (*num_descriptors > 4)
		*num_descriptors = 4;
	for (i = 0; i < *num_descriptors; i++) {
		audio_format_id[i] = msg->msg[2 + i] >> 6;
		audio_format_code[i] = msg->msg[2 + i] & 0x3f;
	}
}


/* Audio Rate Control Feature */
static inline void cec_msg_set_audio_rate(struct cec_msg *msg,
					  __u8 audio_rate)
{
	msg->len = 3;
	msg->msg[1] = CEC_MSG_SET_AUDIO_RATE;
	msg->msg[2] = audio_rate;
}

static inline void cec_ops_set_audio_rate(const struct cec_msg *msg,
					  __u8 *audio_rate)
{
	*audio_rate = msg->msg[2];
}


/* Audio Return Channel Control Feature */
static inline void cec_msg_report_arc_initiated(struct cec_msg *msg)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_REPORT_ARC_INITIATED;
}

static inline void cec_msg_initiate_arc(struct cec_msg *msg,
					int reply)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_INITIATE_ARC;
	msg->reply = reply ? CEC_MSG_REPORT_ARC_INITIATED : 0;
}

static inline void cec_msg_request_arc_initiation(struct cec_msg *msg,
						  int reply)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_REQUEST_ARC_INITIATION;
	msg->reply = reply ? CEC_MSG_INITIATE_ARC : 0;
}

static inline void cec_msg_report_arc_terminated(struct cec_msg *msg)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_REPORT_ARC_TERMINATED;
}

static inline void cec_msg_terminate_arc(struct cec_msg *msg,
					 int reply)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_TERMINATE_ARC;
	msg->reply = reply ? CEC_MSG_REPORT_ARC_TERMINATED : 0;
}

static inline void cec_msg_request_arc_termination(struct cec_msg *msg,
						   int reply)
{
	msg->len = 2;
	msg->msg[1] = CEC_MSG_REQUEST_ARC_TERMINATION;
	msg->reply = reply ? CEC_MSG_TERMINATE_ARC : 0;
}


/* Dynamic Audio Lipsync Feature */
/* Only for CEC 2.0 and up */
static inline void cec_msg_report_current_latency(struct cec_msg *msg,
						  __u16 phys_addr,
						  __u8 video_latency,
						  __u8 low_latency_mode,
						  __u8 audio_out_compensated,
						  __u8 audio_out_delay)
{
	msg->len = 7;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_REPORT_CURRENT_LATENCY;
	msg->msg[2] = phys_addr >> 8;
	msg->msg[3] = phys_addr & 0xff;
	msg->msg[4] = video_latency;
	msg->msg[5] = (low_latency_mode << 2) | audio_out_compensated;
	msg->msg[6] = audio_out_delay;
}

static inline void cec_ops_report_current_latency(const struct cec_msg *msg,
						  __u16 *phys_addr,
						  __u8 *video_latency,
						  __u8 *low_latency_mode,
						  __u8 *audio_out_compensated,
						  __u8 *audio_out_delay)
{
	*phys_addr = (msg->msg[2] << 8) | msg->msg[3];
	*video_latency = msg->msg[4];
	*low_latency_mode = (msg->msg[5] >> 2) & 1;
	*audio_out_compensated = msg->msg[5] & 3;
	*audio_out_delay = msg->msg[6];
}

static inline void cec_msg_request_current_latency(struct cec_msg *msg,
						   int reply,
						   __u16 phys_addr)
{
	msg->len = 4;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_REQUEST_CURRENT_LATENCY;
	msg->msg[2] = phys_addr >> 8;
	msg->msg[3] = phys_addr & 0xff;
	msg->reply = reply ? CEC_MSG_REPORT_CURRENT_LATENCY : 0;
}

static inline void cec_ops_request_current_latency(const struct cec_msg *msg,
						   __u16 *phys_addr)
{
	*phys_addr = (msg->msg[2] << 8) | msg->msg[3];
}


/* Capability Discovery and Control Feature */
static inline void cec_msg_cdc_hec_inquire_state(struct cec_msg *msg,
						 __u16 phys_addr1,
						 __u16 phys_addr2)
{
	msg->len = 9;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_CDC_MESSAGE;
	/* msg[2] and msg[3] (phys_addr) are filled in by the CEC framework */
	msg->msg[4] = CEC_MSG_CDC_HEC_INQUIRE_STATE;
	msg->msg[5] = phys_addr1 >> 8;
	msg->msg[6] = phys_addr1 & 0xff;
	msg->msg[7] = phys_addr2 >> 8;
	msg->msg[8] = phys_addr2 & 0xff;
}

static inline void cec_ops_cdc_hec_inquire_state(const struct cec_msg *msg,
						 __u16 *phys_addr,
						 __u16 *phys_addr1,
						 __u16 *phys_addr2)
{
	*phys_addr = (msg->msg[2] << 8) | msg->msg[3];
	*phys_addr1 = (msg->msg[5] << 8) | msg->msg[6];
	*phys_addr2 = (msg->msg[7] << 8) | msg->msg[8];
}

static inline void cec_msg_cdc_hec_report_state(struct cec_msg *msg,
						__u16 target_phys_addr,
						__u8 hec_func_state,
						__u8 host_func_state,
						__u8 enc_func_state,
						__u8 cdc_errcode,
						__u8 has_field,
						__u16 hec_field)
{
	msg->len = has_field ? 10 : 8;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_CDC_MESSAGE;
	/* msg[2] and msg[3] (phys_addr) are filled in by the CEC framework */
	msg->msg[4] = CEC_MSG_CDC_HEC_REPORT_STATE;
	msg->msg[5] = target_phys_addr >> 8;
	msg->msg[6] = target_phys_addr & 0xff;
	msg->msg[7] = (hec_func_state << 6) |
		      (host_func_state << 4) |
		      (enc_func_state << 2) |
		      cdc_errcode;
	if (has_field) {
		msg->msg[8] = hec_field >> 8;
		msg->msg[9] = hec_field & 0xff;
	}
}

static inline void cec_ops_cdc_hec_report_state(const struct cec_msg *msg,
						__u16 *phys_addr,
						__u16 *target_phys_addr,
						__u8 *hec_func_state,
						__u8 *host_func_state,
						__u8 *enc_func_state,
						__u8 *cdc_errcode,
						__u8 *has_field,
						__u16 *hec_field)
{
	*phys_addr = (msg->msg[2] << 8) | msg->msg[3];
	*target_phys_addr = (msg->msg[5] << 8) | msg->msg[6];
	*hec_func_state = msg->msg[7] >> 6;
	*host_func_state = (msg->msg[7] >> 4) & 3;
	*enc_func_state = (msg->msg[7] >> 4) & 3;
	*cdc_errcode = msg->msg[7] & 3;
	*has_field = msg->len >= 10;
	*hec_field = *has_field ? ((msg->msg[8] << 8) | msg->msg[9]) : 0;
}

static inline void cec_msg_cdc_hec_set_state(struct cec_msg *msg,
					     __u16 phys_addr1,
					     __u16 phys_addr2,
					     __u8 hec_set_state,
					     __u16 phys_addr3,
					     __u16 phys_addr4,
					     __u16 phys_addr5)
{
	msg->len = 10;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_CDC_MESSAGE;
	/* msg[2] and msg[3] (phys_addr) are filled in by the CEC framework */
	msg->msg[4] = CEC_MSG_CDC_HEC_INQUIRE_STATE;
	msg->msg[5] = phys_addr1 >> 8;
	msg->msg[6] = phys_addr1 & 0xff;
	msg->msg[7] = phys_addr2 >> 8;
	msg->msg[8] = phys_addr2 & 0xff;
	msg->msg[9] = hec_set_state;
	if (phys_addr3 != CEC_PHYS_ADDR_INVALID) {
		msg->msg[msg->len++] = phys_addr3 >> 8;
		msg->msg[msg->len++] = phys_addr3 & 0xff;
		if (phys_addr4 != CEC_PHYS_ADDR_INVALID) {
			msg->msg[msg->len++] = phys_addr4 >> 8;
			msg->msg[msg->len++] = phys_addr4 & 0xff;
			if (phys_addr5 != CEC_PHYS_ADDR_INVALID) {
				msg->msg[msg->len++] = phys_addr5 >> 8;
				msg->msg[msg->len++] = phys_addr5 & 0xff;
			}
		}
	}
}

static inline void cec_ops_cdc_hec_set_state(const struct cec_msg *msg,
					     __u16 *phys_addr,
					     __u16 *phys_addr1,
					     __u16 *phys_addr2,
					     __u8 *hec_set_state,
					     __u16 *phys_addr3,
					     __u16 *phys_addr4,
					     __u16 *phys_addr5)
{
	*phys_addr = (msg->msg[2] << 8) | msg->msg[3];
	*phys_addr1 = (msg->msg[5] << 8) | msg->msg[6];
	*phys_addr2 = (msg->msg[7] << 8) | msg->msg[8];
	*hec_set_state = msg->msg[9];
	*phys_addr3 = *phys_addr4 = *phys_addr5 = CEC_PHYS_ADDR_INVALID;
	if (msg->len >= 12)
		*phys_addr3 = (msg->msg[10] << 8) | msg->msg[11];
	if (msg->len >= 14)
		*phys_addr4 = (msg->msg[12] << 8) | msg->msg[13];
	if (msg->len >= 16)
		*phys_addr5 = (msg->msg[14] << 8) | msg->msg[15];
}

static inline void cec_msg_cdc_hec_set_state_adjacent(struct cec_msg *msg,
						      __u16 phys_addr1,
						      __u8 hec_set_state)
{
	msg->len = 8;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_CDC_MESSAGE;
	/* msg[2] and msg[3] (phys_addr) are filled in by the CEC framework */
	msg->msg[4] = CEC_MSG_CDC_HEC_SET_STATE_ADJACENT;
	msg->msg[5] = phys_addr1 >> 8;
	msg->msg[6] = phys_addr1 & 0xff;
	msg->msg[7] = hec_set_state;
}

static inline void cec_ops_cdc_hec_set_state_adjacent(const struct cec_msg *msg,
						      __u16 *phys_addr,
						      __u16 *phys_addr1,
						      __u8 *hec_set_state)
{
	*phys_addr = (msg->msg[2] << 8) | msg->msg[3];
	*phys_addr1 = (msg->msg[5] << 8) | msg->msg[6];
	*hec_set_state = msg->msg[7];
}

static inline void cec_msg_cdc_hec_request_deactivation(struct cec_msg *msg,
							__u16 phys_addr1,
							__u16 phys_addr2,
							__u16 phys_addr3)
{
	msg->len = 11;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_CDC_MESSAGE;
	/* msg[2] and msg[3] (phys_addr) are filled in by the CEC framework */
	msg->msg[4] = CEC_MSG_CDC_HEC_REQUEST_DEACTIVATION;
	msg->msg[5] = phys_addr1 >> 8;
	msg->msg[6] = phys_addr1 & 0xff;
	msg->msg[7] = phys_addr2 >> 8;
	msg->msg[8] = phys_addr2 & 0xff;
	msg->msg[9] = phys_addr3 >> 8;
	msg->msg[10] = phys_addr3 & 0xff;
}

static inline void cec_ops_cdc_hec_request_deactivation(const struct cec_msg *msg,
							__u16 *phys_addr,
							__u16 *phys_addr1,
							__u16 *phys_addr2,
							__u16 *phys_addr3)
{
	*phys_addr = (msg->msg[2] << 8) | msg->msg[3];
	*phys_addr1 = (msg->msg[5] << 8) | msg->msg[6];
	*phys_addr2 = (msg->msg[7] << 8) | msg->msg[8];
	*phys_addr3 = (msg->msg[9] << 8) | msg->msg[10];
}

static inline void cec_msg_cdc_hec_notify_alive(struct cec_msg *msg)
{
	msg->len = 5;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_CDC_MESSAGE;
	/* msg[2] and msg[3] (phys_addr) are filled in by the CEC framework */
	msg->msg[4] = CEC_MSG_CDC_HEC_NOTIFY_ALIVE;
}

static inline void cec_ops_cdc_hec_notify_alive(const struct cec_msg *msg,
						__u16 *phys_addr)
{
	*phys_addr = (msg->msg[2] << 8) | msg->msg[3];
}

static inline void cec_msg_cdc_hec_discover(struct cec_msg *msg)
{
	msg->len = 5;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_CDC_MESSAGE;
	/* msg[2] and msg[3] (phys_addr) are filled in by the CEC framework */
	msg->msg[4] = CEC_MSG_CDC_HEC_DISCOVER;
}

static inline void cec_ops_cdc_hec_discover(const struct cec_msg *msg,
					    __u16 *phys_addr)
{
	*phys_addr = (msg->msg[2] << 8) | msg->msg[3];
}

static inline void cec_msg_cdc_hpd_set_state(struct cec_msg *msg,
					     __u8 input_port,
					     __u8 hpd_state)
{
	msg->len = 6;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_CDC_MESSAGE;
	/* msg[2] and msg[3] (phys_addr) are filled in by the CEC framework */
	msg->msg[4] = CEC_MSG_CDC_HPD_SET_STATE;
	msg->msg[5] = (input_port << 4) | hpd_state;
}

static inline void cec_ops_cdc_hpd_set_state(const struct cec_msg *msg,
					    __u16 *phys_addr,
					    __u8 *input_port,
					    __u8 *hpd_state)
{
	*phys_addr = (msg->msg[2] << 8) | msg->msg[3];
	*input_port = msg->msg[5] >> 4;
	*hpd_state = msg->msg[5] & 0xf;
}

static inline void cec_msg_cdc_hpd_report_state(struct cec_msg *msg,
						__u8 hpd_state,
						__u8 hpd_error)
{
	msg->len = 6;
	msg->msg[0] |= 0xf; /* broadcast */
	msg->msg[1] = CEC_MSG_CDC_MESSAGE;
	/* msg[2] and msg[3] (phys_addr) are filled in by the CEC framework */
	msg->msg[4] = CEC_MSG_CDC_HPD_REPORT_STATE;
	msg->msg[5] = (hpd_state << 4) | hpd_error;
}

static inline void cec_ops_cdc_hpd_report_state(const struct cec_msg *msg,
						__u16 *phys_addr,
						__u8 *hpd_state,
						__u8 *hpd_error)
{
	*phys_addr = (msg->msg[2] << 8) | msg->msg[3];
	*hpd_state = msg->msg[5] >> 4;
	*hpd_error = msg->msg[5] & 0xf;
}

#endif
