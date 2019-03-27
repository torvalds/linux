/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-07 Applied Micro Circuits Corporation.
 * Copyright (c) 2004-05 Vinod Kashyap.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 */

/*
 * AMCC'S 3ware driver for 9000 series storage controllers.
 *
 * Author: Vinod Kashyap
 * Modifications by: Adam Radford
 */



#ifndef TW_OSL_IOCTL_H

#define TW_OSL_IOCTL_H


/*
 * Macros and structures for OS Layer/Common Layer handled ioctls.
 */



#include <dev/twa/tw_cl_fwif.h>
#include <dev/twa/tw_cl_ioctl.h>



#pragma pack(1)
/*
 * We need the structure below to ensure that the first byte of
 * data_buf is not overwritten by the kernel, after we return
 * from the ioctl call.  Note that cmd_pkt has been reduced
 * to an array of 1024 bytes even though it's actually 2048 bytes
 * in size.  This is because, we don't expect requests from user
 * land requiring 2048 (273 sg elements) byte cmd pkts.
 */
typedef struct tw_osli_ioctl_no_data_buf {
	struct tw_cl_driver_packet	driver_pkt;
	TW_VOID				*pdata; /* points to data_buf */
	TW_INT8				padding[488 - sizeof(TW_VOID *)];
	struct tw_cl_command_packet	cmd_pkt;
} TW_OSLI_IOCTL_NO_DATA_BUF;

#pragma pack()

/* ioctl cmds handled by the OS Layer */
#define TW_OSL_IOCTL_SCAN_BUS				\
	_IO('T', 200)
#define TW_OSL_IOCTL_FIRMWARE_PASS_THROUGH		\
	_IOWR('T', 202, TW_OSLI_IOCTL_NO_DATA_BUF)


#include <sys/ioccom.h>

#pragma pack(1)

typedef struct tw_osli_ioctl_with_payload {
	struct tw_cl_driver_packet	driver_pkt;
	TW_INT8				padding[488];
	struct tw_cl_command_packet	cmd_pkt;
	union {
		struct tw_cl_event_packet		event_pkt;
		struct tw_cl_lock_packet		lock_pkt;
		struct tw_cl_compatibility_packet	compat_pkt;
		TW_INT8					data_buf[1];
	} payload;
} TW_OSLI_IOCTL_WITH_PAYLOAD;

#pragma pack()

/* ioctl cmds handled by the Common Layer */
#define TW_CL_IOCTL_GET_FIRST_EVENT			\
	_IOWR('T', 203, TW_OSLI_IOCTL_WITH_PAYLOAD)
#define TW_CL_IOCTL_GET_LAST_EVENT			\
	_IOWR('T', 204, TW_OSLI_IOCTL_WITH_PAYLOAD)
#define TW_CL_IOCTL_GET_NEXT_EVENT			\
	_IOWR('T', 205, TW_OSLI_IOCTL_WITH_PAYLOAD)
#define TW_CL_IOCTL_GET_PREVIOUS_EVENT			\
	_IOWR('T', 206, TW_OSLI_IOCTL_WITH_PAYLOAD)
#define TW_CL_IOCTL_GET_LOCK				\
	_IOWR('T', 207, TW_OSLI_IOCTL_WITH_PAYLOAD)
#define TW_CL_IOCTL_RELEASE_LOCK			\
	_IOWR('T', 208, TW_OSLI_IOCTL_WITH_PAYLOAD)
#define TW_CL_IOCTL_GET_COMPATIBILITY_INFO		\
	_IOWR('T', 209, TW_OSLI_IOCTL_WITH_PAYLOAD)



#endif /* TW_OSL_IOCTL_H */
