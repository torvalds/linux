/*-
 * Copyright (c) 2016 Microsoft Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _HV_UTILREG_H_
#define _HV_UTILREG_H_

/*
 * Some Hyper-V status codes.
 */
#define HV_S_OK				0x00000000
#define HV_E_FAIL			0x80004005
#define HV_S_CONT			0x80070103
#define HV_ERROR_NOT_SUPPORTED		0x80070032
#define HV_ERROR_MACHINE_LOCKED		0x800704F7
#define HV_ERROR_DEVICE_NOT_CONNECTED	0x8007048F
#define HV_INVALIDARG			0x80070057
#define HV_GUID_NOTFOUND		0x80041002

/*
 * Common defines for Hyper-V ICs
 */
#define HV_ICMSGTYPE_NEGOTIATE		0
#define HV_ICMSGTYPE_HEARTBEAT		1
#define HV_ICMSGTYPE_KVPEXCHANGE	2
#define HV_ICMSGTYPE_SHUTDOWN		3
#define HV_ICMSGTYPE_TIMESYNC		4
#define HV_ICMSGTYPE_VSS		5

#define HV_ICMSGHDRFLAG_TRANSACTION	1
#define HV_ICMSGHDRFLAG_REQUEST		2
#define HV_ICMSGHDRFLAG_RESPONSE	4

typedef struct hv_vmbus_pipe_hdr {
	uint32_t flags;
	uint32_t msgsize;
} __packed hv_vmbus_pipe_hdr;

typedef struct hv_vmbus_ic_version {
	uint16_t major;
	uint16_t minor;
} __packed hv_vmbus_ic_version;

typedef struct hv_vmbus_icmsg_hdr {
	hv_vmbus_ic_version	icverframe;
	uint16_t		icmsgtype;
	hv_vmbus_ic_version	icvermsg;
	uint16_t		icmsgsize;
	uint32_t		status;
	uint8_t			ictransaction_id;
	uint8_t			icflags;
	uint8_t			reserved[2];
} __packed hv_vmbus_icmsg_hdr;

typedef struct hv_vmbus_icmsg_negotiate {
	uint16_t		icframe_vercnt;
	uint16_t		icmsg_vercnt;
	uint32_t		reserved;
	hv_vmbus_ic_version	icversion_data[1]; /* any size array */
} __packed hv_vmbus_icmsg_negotiate;

#endif	/* !_HV_UTILREG_H_ */
