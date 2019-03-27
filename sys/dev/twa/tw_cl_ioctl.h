/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-07 Applied Micro Circuits Corporation.
 * Copyright (c) 2004-05 Vinod Kashyap
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



#ifndef TW_CL_IOCTL_H

#define TW_CL_IOCTL_H


/*
 * Macros and structures for Common Layer handled ioctls.
 */


#define TW_CL_AEN_NOT_RETRIEVED	0x1
#define TW_CL_AEN_RETRIEVED	0x2

#define TW_CL_ERROR_AEN_NO_EVENTS	0x1003	/* No more events */
#define TW_CL_ERROR_AEN_OVERFLOW	0x1004	/* AEN overflow occurred */

#define TW_CL_ERROR_IOCTL_LOCK_NOT_HELD		0x1001   /* Not locked */
#define TW_CL_ERROR_IOCTL_LOCK_ALREADY_HELD	0x1002   /* Already locked */


#pragma pack(1)

/* Structure used to handle GET/RELEASE LOCK ioctls. */
struct tw_cl_lock_packet {
	TW_UINT32	timeout_msec;
	TW_UINT32	time_remaining_msec;
	TW_UINT32	force_flag;
};


/* Structure used to handle GET COMPATIBILITY INFO ioctl. */
struct tw_cl_compatibility_packet {
	TW_UINT8	driver_version[32];/* driver version */
	TW_UINT16	working_srl;	/* driver & firmware negotiated srl */
	TW_UINT16	working_branch;	/* branch # of the firmware that the
					driver is compatible with */
	TW_UINT16	working_build;	/* build # of the firmware that the
					driver is compatible with */
	TW_UINT16	driver_srl_high;/* highest driver supported srl */
	TW_UINT16	driver_branch_high;/* highest driver supported branch */
	TW_UINT16	driver_build_high;/* highest driver supported build */
	TW_UINT16	driver_srl_low;/* lowest driver supported srl */
	TW_UINT16	driver_branch_low;/* lowest driver supported branch */
	TW_UINT16	driver_build_low;/* lowest driver supported build */
	TW_UINT16	fw_on_ctlr_srl;	/* srl of running firmware */
	TW_UINT16	fw_on_ctlr_branch;/* branch # of running firmware */
	TW_UINT16	fw_on_ctlr_build;/* build # of running firmware */
};


/* Driver understandable part of the ioctl packet built by the API. */
struct tw_cl_driver_packet {
	TW_UINT32	control_code;
	TW_UINT32	status;
	TW_UINT32	unique_id;
	TW_UINT32	sequence_id;
	TW_UINT32	os_status;
	TW_UINT32	buffer_length;
};


/* ioctl packet built by the API. */
struct tw_cl_ioctl_packet {
	struct tw_cl_driver_packet	driver_pkt;
	TW_INT8				padding[488];
	struct tw_cl_command_packet	cmd_pkt;
	TW_INT8				data_buf[1];
};

#pragma pack()



#endif /* TW_CL_IOCTL_H */
