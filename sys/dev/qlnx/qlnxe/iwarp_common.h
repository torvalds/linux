/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef __IWARP_COMMON__
#define __IWARP_COMMON__ 
/************************************************************************/
/* Add include to common rdma target for both eCore and protocol rdma driver */
/************************************************************************/
#include "rdma_common.h"
/************************/
/* IWARP FW CONSTANTS	*/
/************************/

#define IWARP_ACTIVE_MODE 0
#define IWARP_PASSIVE_MODE 1

#define IWARP_SHARED_QUEUE_PAGE_SIZE			(0x8000)		//32KB page for Shared Queue Page
#define IWARP_SHARED_QUEUE_PAGE_RQ_PBL_OFFSET	(0x4000)		//First 12KB of Shared Queue Page is reserved for FW
#define IWARP_SHARED_QUEUE_PAGE_RQ_PBL_MAX_SIZE (0x1000)		//Max RQ PBL Size is 4KB
#define IWARP_SHARED_QUEUE_PAGE_SQ_PBL_OFFSET	(0x5000)		
#define IWARP_SHARED_QUEUE_PAGE_SQ_PBL_MAX_SIZE	(0x3000)		//Max SQ PBL Size is 12KB

#define IWARP_REQ_MAX_INLINE_DATA_SIZE		(128)	//max size of inline data in single request
#define IWARP_REQ_MAX_SINGLE_SQ_WQE_SIZE	(176)	//Maximum size of single SQ WQE (rdma wqe and inline data)

#define IWARP_MAX_QPS				(64*1024)

#endif /* __IWARP_COMMON__ */
