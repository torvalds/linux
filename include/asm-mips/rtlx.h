/*
 * Copyright (C) 2004, 2005 MIPS Technologies, Inc.  All rights reserved.
 *
 */

#ifndef _RTLX_H
#define _RTLX_H_

#define LX_NODE_BASE 10

#define MIPSCPU_INT_BASE       16
#define MIPS_CPU_RTLX_IRQ 0

#define RTLX_VERSION 1
#define RTLX_xID 0x12345600
#define RTLX_ID (RTLX_xID | RTLX_VERSION)
#define RTLX_CHANNELS 8

enum rtlx_state {
	RTLX_STATE_UNUSED = 0,
	RTLX_STATE_INITIALISED,
	RTLX_STATE_REMOTE_READY,
	RTLX_STATE_OPENED
};

#define RTLX_BUFFER_SIZE 1024
/* each channel supports read and write.
   linux (vpe0) reads lx_buffer  and writes rt_buffer
   SP (vpe1) reads rt_buffer and writes lx_buffer
*/
typedef struct rtlx_channel {
	enum rtlx_state rt_state;
	enum rtlx_state lx_state;

	int buffer_size;

	/* read and write indexes per buffer */
	int rt_write, rt_read;
	char *rt_buffer;

	int lx_write, lx_read;
	char *lx_buffer;

	void *queues;

} rtlx_channel_t;

typedef struct rtlx_info {
	unsigned long id;
	enum rtlx_state state;

	struct rtlx_channel channel[RTLX_CHANNELS];

} rtlx_info_t;

#endif
