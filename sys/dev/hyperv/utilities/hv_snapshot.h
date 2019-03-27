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

#ifndef _VSS_H
#define _VSS_H
#include <sys/ioccom.h>
#define FS_VSS_DEV_NAME		"hv_fsvss_dev"
#define APP_VSS_DEV_NAME	"hv_appvss_dev"

#define VSS_DEV(VSS)		"/dev/"VSS

#define VSS_SUCCESS		0x00000000
#define VSS_FAIL		0x00000001

enum hv_vss_op_t {
	HV_VSS_NONE = 0,
	HV_VSS_CHECK,
	HV_VSS_FREEZE,
	HV_VSS_THAW,
	HV_VSS_COUNT
};

struct hv_vss_opt_msg {
	uint32_t	opt;		/* operation */
	uint32_t	status;		/* 0 for success, 1 for error */
	uint64_t	msgid;		/* an ID used to identify the transaction */
	uint8_t		reserved[48];	/* reserved values are all zeroes */
};
#define IOCHVVSSREAD		_IOR('v', 2, struct hv_vss_opt_msg)
#define IOCHVVSSWRITE		_IOW('v', 3, struct hv_vss_opt_msg)
#endif
