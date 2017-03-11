/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __ROCE_COMMON__
#define __ROCE_COMMON__

#define ROCE_REQ_MAX_INLINE_DATA_SIZE (256)
#define ROCE_REQ_MAX_SINGLE_SQ_WQE_SIZE (288)

#define ROCE_MAX_QPS	(32 * 1024)

enum roce_async_events_type {
	ROCE_ASYNC_EVENT_NONE = 0,
	ROCE_ASYNC_EVENT_COMM_EST = 1,
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
	MAX_ROCE_ASYNC_EVENTS_TYPE
};

#endif /* __ROCE_COMMON__ */
