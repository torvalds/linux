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

#ifndef __ROCE_COMMON__
#define __ROCE_COMMON__ 
/************************************************************************/
/* Add include to common rdma target for both eCore and protocol rdma driver */
/************************************************************************/
#include "rdma_common.h"
/************************/
/* ROCE FW CONSTANTS */
/************************/

#define ROCE_REQ_MAX_INLINE_DATA_SIZE (256)	//max size of inline data in single request
#define ROCE_REQ_MAX_SINGLE_SQ_WQE_SIZE	(288)	//Maximum size of single SQ WQE (rdma wqe and inline data)

#define ROCE_MAX_QPS				(32*1024)
#define ROCE_DCQCN_NP_MAX_QPS  (64)	/* notification point max QPs*/
#define ROCE_DCQCN_RP_MAX_QPS  (64)		/* reaction point max QPs*/


/*
 * Affiliated asynchronous events / errors enumeration
 */
enum roce_async_events_type
{
	ROCE_ASYNC_EVENT_NONE=0,
	ROCE_ASYNC_EVENT_COMM_EST=1,
	ROCE_ASYNC_EVENT_SQ_DRAINED,
	ROCE_ASYNC_EVENT_SRQ_LIMIT,
	ROCE_ASYNC_EVENT_LAST_WQE_REACHED,
	ROCE_ASYNC_EVENT_CQ_ERR,
	ROCE_ASYNC_EVENT_LOCAL_INVALID_REQUEST_ERR,
	ROCE_ASYNC_EVENT_LOCAL_CATASTROPHIC_ERR,
	ROCE_ASYNC_EVENT_LOCAL_ACCESS_ERR,
	ROCE_ASYNC_EVENT_QP_CATASTROPHIC_ERR,
	ROCE_ASYNC_EVENT_CQ_OVERFLOW_ERR,
	ROCE_ASYNC_EVENT_SRQ_EMPTY,
	ROCE_ASYNC_EVENT_DESTROY_QP_DONE,
	ROCE_ASYNC_EVENT_XRC_DOMAIN_ERR,
	ROCE_ASYNC_EVENT_INVALID_XRCETH_ERR,
	ROCE_ASYNC_EVENT_XRC_SRQ_CATASTROPHIC_ERR,
	MAX_ROCE_ASYNC_EVENTS_TYPE
};

#endif /* __ROCE_COMMON__ */
