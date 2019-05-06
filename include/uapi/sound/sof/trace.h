/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 */

#ifndef __INCLUDE_UAPI_SOUND_SOF_USER_TRACE_H__
#define __INCLUDE_UAPI_SOUND_SOF_USER_TRACE_H__

/*
 * Host system time.
 *
 * This property is used by the driver to pass down information about
 * current system time. It is expressed in us.
 * FW translates timestamps (in log entries, probe pockets) to this time
 * domain.
 *
 * (cavs: SystemTime).
 */
struct system_time {
	uint32_t val_l;  /* Lower dword of current host time value */
	uint32_t val_u;  /* Upper dword of current host time value */
} __packed;

#define LOG_ENABLE		1  /* Enable logging */
#define LOG_DISABLE		0  /* Disable logging */

#define LOG_LEVEL_CRITICAL	1  /* (FDK fatal) */
#define LOG_LEVEL_VERBOSE	2

/*
 * Layout of a log fifo.
 */
struct log_buffer_layout {
	uint32_t read_ptr;  /*read pointer */
	uint32_t write_ptr; /* write pointer */
	uint32_t buffer[0]; /* buffer */
} __packed;

/*
 * Log buffer status reported by FW.
 */
struct log_buffer_status {
	uint32_t core_id;  /* ID of core that logged to other half */
} __packed;

#define TRACE_ID_LENGTH 12

/*
 *  Log entry header.
 *
 * The header is followed by an array of arguments (uint32_t[]).
 * Number of arguments is specified by the params_num field of log_entry
 */
struct log_entry_header {
	uint32_t id_0 : TRACE_ID_LENGTH;	/* e.g. Pipeline ID */
	uint32_t id_1 : TRACE_ID_LENGTH;	/* e.g. Component ID */
	uint32_t core_id : 8;		/* Reporting core's id */

	uint64_t timestamp;		/* Timestamp (in dsp ticks) */
	uint32_t log_entry_address;	/* Address of log entry in ELF */
} __packed;

#endif
