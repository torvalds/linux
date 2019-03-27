/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Adaptec Inc.
 * All rights reserved.
 *
 * Written by: David Jeffery
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
 * $FreeBSD$
 */

#ifndef _IPS_IOCTL_H
#define _IPS_IOCTL_H

#include <sys/ioccom.h>

#define IPS_USER_CMD _IOWR(0x81,0x21,ips_user_request)

#define IPS_IOCTL_READ	1
#define IPS_IOCTL_WRITE 2

#define IPS_REBUILD_STAT_SIZE 116
#define IPS_SUBSYS_PARAM_SIZE 128
#define IPS_RW_NVRAM_SIZE 128

#define IPS_IOCTL_BUFFER_SIZE 4096

typedef struct ips_user_request{
	void *	command_buffer;
	void *	data_buffer;
	u_int32_t	status;
}ips_user_request;

#ifdef _KERNEL

typedef struct ips_ioctl{
	ips_generic_cmd *	command_buffer;
	void *			data_buffer;
	ips_cmd_status_t	status;
	int			datasize;
	int			readwrite;
	bus_dma_tag_t 		dmatag;
	bus_dmamap_t  		dmamap;
}ips_ioctl_t;

#endif
#endif

