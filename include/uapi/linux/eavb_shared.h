/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __EAVB_SHARED_H__
#define __EAVB_SHARED_H__


#include <linux/types.h>

#define EAVB_IOCTL_MAGIC    'B'

/* ioctl request */
/* EAVB_IOCTL_CREATE_STREAM DEPRECATED Do not use this in new code */
#define EAVB_IOCTL_CREATE_STREAM \
	_IOWR(EAVB_IOCTL_MAGIC, 1, struct eavb_ioctl_create_stream)
#define EAVB_IOCTL_GET_STREAM_INFO \
	_IOWR(EAVB_IOCTL_MAGIC, 2, struct eavb_ioctl_get_stream_info)
#define EAVB_IOCTL_CONNECT_STREAM \
	_IOWR(EAVB_IOCTL_MAGIC, 3, struct eavb_ioctl_connect_stream)
#define EAVB_IOCTL_RECEIVE \
	_IOWR(EAVB_IOCTL_MAGIC, 4, struct eavb_ioctl_receive)
#define EAVB_IOCTL_RECV_DONE \
	_IOWR(EAVB_IOCTL_MAGIC, 5, struct eavb_ioctl_recv_done)
#define EAVB_IOCTL_TRANSMIT \
	_IOWR(EAVB_IOCTL_MAGIC, 6, struct eavb_ioctl_transmit)
#define EAVB_IOCTL_DISCONNECT_STREAM \
	_IOW(EAVB_IOCTL_MAGIC, 9, struct eavb_ioctl_disconnect_stream)
#define EAVB_IOCTL_DESTROY_STREAM \
	_IOW(EAVB_IOCTL_MAGIC, 10, struct eavb_ioctl_destroy_stream)
#define EAVB_IOCTL_CREATE_STREAM_WITH_PATH \
	_IOWR(EAVB_IOCTL_MAGIC, 11, struct eavb_ioctl_create_stream_with_path)
#define EAVB_IOCTL_GET_CRF_TS \
	_IOWR(EAVB_IOCTL_MAGIC, 12, __u64)

/* default value */
#define STATION_ADDR_SIZE	8
#define IF_NAMESIZE		16
#define MAX_CONFIG_FILE_PATH	512

/* Invalid value for config file */
#define AVB_INVALID_ADDR	(0xFF)
#define AVB_INVALID_INTEGER	(-1)
#define AVB_INVALID_UINT	(-1)


enum avb_role {
	AVB_ROLE_TALKER = 0,
	AVB_ROLE_LISTENER,
	AVB_ROLE_CRF_TALKER,
	AVB_ROLE_INVALID = -1
};

enum stream_mapping_type {
	NONE = 0,
	PCM,
	H264,
	MPEG2TS,
	MJPEG,
	CRF,
	MAPPING_TYPE_INVALID = -1
};

enum ring_buffer_mode {
	/* Return error when new data doesn't fit in ring buffer */
	RING_BUFFER_MODE_FILL = 0,
	/* Drop oldest samples to make room for new data */
	RING_BUFFER_MODE_DROP_OLD = 1,
	/* Drop oldest samples to make room for new data */
	RING_BUFFER_MODE_INVALID = -1
};

enum avb_class {
	CLASS_A = 0,
	CLASS_B = 1,
	CLASS_AAF = 2,
	CLASS_INVALID = -1
};

enum data_endianness {
	ENDIAN_BIG = 0,
	ENDIAN_LITTLE = 1,
	ENDIAN_INVALID = -1
};

enum avb_ieee1722_version {
	QAVB_IEEE_1722_ver_2010 = 0,
	QAVB_IEEE_1722_ver_2016,
	QAVB_IEEE_1722_ver_INVALID = -1,
};

enum avtp_crf_mode {
	/* CRF disabled */
	QAVB_AVTP_CRF_MODE_DISABLED = 0,
	/* CRF talker */
	QAVB_AVTP_CRF_MODE_TALKER,
	/* CRF listener, PPM = local - remote */
	QAVB_AVTP_CRF_MODE_LISTENER,
	/* CRF listener, PPM = nominal - remote */
	QAVB_AVTP_CRF_MODE_LISTENER_NOMINAL,
	QAVB_AVTP_CRF_MODE_MAX
};

enum avtp_crf_type {
	/* user specified */
	QAVB_AVTP_CRF_TYPE_USER = 0,
	/* audio ample timestamp */
	QAVB_AVTP_CRF_TYPE_AUDIO_SAMPLE,
	/* video frame sync timestamp */
	QAVB_AVTP_CRF_TYPE_VIDEO_FRAME,
	/* video line timestamp */
	QAVB_AVTP_CRF_TYPE_VIDEO_LINE,
	/* machine cycle timestamp */
	QAVB_AVTP_CRF_TYPE_MACHINE_CYCLE,
	QAVB_AVTP_CRF_TYPE_MAX
};

enum avtp_crf_pull {
	/* Multiply base_frequency field by 1.0 */
	QAVB_AVTP_CRF_PULL_1_DIV_1_0 = 0,
	/* Multiply base_frequency field by 1/1.1001 */
	QAVB_AVTP_CRF_PULL_1_DIV_1_DOT_1001,
	/* Multiply base_frequency field by 1.1001 */
	QAVB_AVTP_CRF_PULL_1_DOT_1001,
	/* Multiply base_frequency field by 24/25 */
	QAVB_AVTP_CRF_PULL_24_DIV_25,
	/* Multiply base_frequency field by 25/24 */
	QAVB_AVTP_CRF_PULL_25_DIV_24,
	/* Multiply base_frequency field by 8 */
	QAVB_AVTP_CRF_PULL_8,
	QAVB_AVTP_CRF_PULL_MAX
};

struct eavb_ioctl_hdr {
	__u64 streamCtx;
};


/*
 * EAVB_IOCTL_CREATE_STREAM
 */

/* DEPRECATED Do not use this in new code */
struct eavb_ioctl_stream_config {
	__u16 stream_id;
	char eth_interface[IF_NAMESIZE];
	__u16 vlan_id;
	__u16 ring_buffer_elem_count;
	enum ring_buffer_mode ring_buffer_mode;
	/* talker = 0 or listener = 1 */
	enum avb_role avb_role;
	__u8 dest_macaddr[STATION_ADDR_SIZE];
	__u8 stream_addr[STATION_ADDR_SIZE];
	/* "crf_macaddr" or "crf_dest_macaddr" */
	__u8 crf_dest_macaddr[STATION_ADDR_SIZE];
	__u8 crf_stream_addr[STATION_ADDR_SIZE];
	enum stream_mapping_type mapping_type;
	int wakeup_interval; /* "wakeup_interval" or "tx_interval" */
	int tx_pkts_per_sec; /* if not set, do default */
	int max_stale_ms; /* int max_stale_ns = max_stale_ms*1000; */
	int presentation_time_ms; /* if not set, do default */
	int enforce_presentation_time;
	int sr_class_type; /* A = 0  B = 1 AAF = 2 */
	/* sets number of items to se sent on each tx / rx */
	int packing_factor;
	int bandwidth;

	/* H.264 */
	int max_payload; /* "max_payload" or "max_video_payload" */

	int mrp_enabled;

	/* Audio Specific */
	int pcm_bit_depth;
	int pcm_channels;
	int sample_rate; /* in hz */
	unsigned char endianness; /* 0 = big 1 = little */

	int ieee1722_standard;

	/* Thread priority in QNX side */
	int talker_priority;
	int listener_priority;
	int crf_priority;

	/* CRF */

	/* 0 - disabled
	 * 1 - CRF talker (listener drives reference)
	 * 2 - CRF with talker reference (talker has CRF talker)
	 */
	int crf_mode;

	/* 0 - custom
	 * 1- audio
	 * 2- video frame
	 * 3 - video line
	 * 4 - machine cycle
	 */
	int crf_type;

	/* time interval after how many events timestamp is to be produced
	 * (base_frequency * pull) / timestamp_interval =
	 * # of timestamps per second
	 */
	int crf_timestamping_interval;
	int crf_timestamping_interval_remote;
	int crf_timestamping_interval_local;
	/* enables/disables dynamic IPG adjustments */
	int crf_allow_dynamic_tx_adjust;
	/* indicates how many CRF timestamps per each CRF packet */
	int crf_num_timestamps_per_pkt;

	__s64 crf_mcr_adjust_min_ppm;
	__s64 crf_mcr_adjust_max_ppm;

	__u16 crf_stream_id;     /* CRF stream ID */
	__s32 crf_base_frequency; /* CRF base frequency */

	__s32 crf_listener_ts_smoothing;
	__s32 crf_talker_ts_smoothing;

	/* multiplier for the base frequency */
	int crf_pull;
	/* indicates how often to issue MCR callback events
	 * how many packets will generate one callback.
	 */
	int crf_event_callback_interval;
	/* Indicates how often to update IPG */
	int crf_dynamic_tx_adjust_interval;

	/* stats */
	__s32 enable_stats_reporting;
	__s32 stats_reporting_interval;
	__s32 stats_reporting_samples;

	/* packet tracking */
	__s32 enable_packet_tracking;
	__s32 packet_tracking_interval;
	int blocking_write_enabled;
	double blocking_write_fill_level;
	int app_tx_block_enabled;
	int stream_interleaving_enabled;
	int create_talker_thread;
	int create_crf_threads;
	int listener_bpf_pkts_per_buff;
} __packed;

/* DEPRECATED Do not use this in new code */
struct eavb_ioctl_create_stream {
	struct eavb_ioctl_stream_config config;	/* IN */
	struct eavb_ioctl_hdr hdr;		/* OUT */
};

struct eavb_ioctl_create_stream_with_path {
	char path[MAX_CONFIG_FILE_PATH];	/* IN */
	struct eavb_ioctl_hdr hdr;		/* OUT */
};

/*
 * EAVB_IOCTL_GET_STREAM_INFO
 */

struct eavb_ioctl_stream_info {
	enum avb_role role;
	enum stream_mapping_type mapping_type;
	/* Max packet payload size */
	unsigned int max_payload;
	/* Number of packets sent per wake */
	unsigned int pkts_per_wake;
	/* Time to sleep between wakes */
	unsigned int wakeup_period_us;

	/* Audio Specific */
	/* Audio bit depth 8/16/24/32 */
	int pcm_bit_depth;
	/* Audio channels 1/2 */
	int num_pcm_channels;
	/* Audio sample rate in hz */
	int sample_rate;
	/* Audio sample endianness 0(big)/1(little) */
	unsigned char endianness;

	/* Max buffer size (Bytes) allowed */
	unsigned int max_buffer_size;
	/* qavb ring buffer size */
	__u32 ring_buffer_size;
} __packed;

struct eavb_ioctl_get_stream_info {
	struct eavb_ioctl_hdr hdr;           /* IN */
	struct eavb_ioctl_stream_info info;  /* OUT */
};


/*
 * EAVB_IOCTL_CONNECT_STREAM
 */

struct eavb_ioctl_connect_stream {
	struct eavb_ioctl_hdr hdr;   /* IN */
};


/*
 * EAVB_IOCTL_RECEIVE
 */

struct eavb_ioctl_buf_hdr {
	/* This flag is used for H.264 and MJPEG streams:
	 * 1. H.264: Set for the very last packet of an access unit.
	 * 2. MJPEG  Set the last packet of a video frame.
	 */
	__u32 flag_end_of_frame:1;
	/* This flag is used in file transfer only:
	 * Set for the last packet in the file
	 */
	__u32 flag_end_of_file:1;
	__u32 flag_reserved:30;
	/* Audio event      Layout D3scription      Valid Channels
	 * event value
	 *  0               Static layout           Based on config
	 *  1               Mono                    0
	 *  2               Stereo                  0, 1
	 *  3               5.1                     0,1,2,3,4,5
	 *  4               7.1                     0,1,2,3,4,5,6,7
	 *  5-15            Custom                  Defined by System
	 *                                          Integrator
	 */
	__u32 event;
	__u32 reserved;
	/* Size of the payload (bytes) */
	__u32 payload_size;
	__u32 buf_ele_count;
} __packed;

struct eavb_ioctl_buf_data {
	struct eavb_ioctl_buf_hdr hdr;
	/* virtual address of buffer */
	__u64 pbuf;
} __packed;

struct eavb_ioctl_receive {
	struct eavb_ioctl_hdr hdr;       /* IN */
	struct eavb_ioctl_buf_data data; /* IN/OUT */
	__s32 received;	/* OUT */
};


/*
 * EAVB_IOCTL_RECV_DONE
 */

struct eavb_ioctl_recv_done {
	struct eavb_ioctl_hdr hdr;       /* IN */
	struct eavb_ioctl_buf_data data; /* IN */
};


/*
 * EAVB_IOCTL_TRANSMIT
 */

struct eavb_ioctl_transmit {
	struct eavb_ioctl_hdr hdr;       /* IN */
	struct eavb_ioctl_buf_data data; /* IN/OUT */
	__s32 written;
};


/*
 * EAVB_IOCTL_DISCONNECT_STREAM
 */

struct eavb_ioctl_disconnect_stream {
	struct eavb_ioctl_hdr hdr;       /* IN */
};


/*
 * EAVB_IOCTL_DESTROY_STREAM
 */

struct eavb_ioctl_destroy_stream {
	struct eavb_ioctl_hdr hdr;       /* IN */
};

#endif /*__EAVB_SHARED_H__*/
