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
#ifndef __IWARP_COMMON__
#define __IWARP_COMMON__
#include <linux/qed/rdma_common.h>
/************************/
/* IWARP FW CONSTANTS	*/
/************************/

#define IWARP_ACTIVE_MODE 0
#define IWARP_PASSIVE_MODE 1

#define IWARP_SHARED_QUEUE_PAGE_SIZE		(0x8000)
#define IWARP_SHARED_QUEUE_PAGE_RQ_PBL_OFFSET   (0x4000)
#define IWARP_SHARED_QUEUE_PAGE_RQ_PBL_MAX_SIZE (0x1000)
#define IWARP_SHARED_QUEUE_PAGE_SQ_PBL_OFFSET   (0x5000)
#define IWARP_SHARED_QUEUE_PAGE_SQ_PBL_MAX_SIZE (0x3000)

#define IWARP_REQ_MAX_INLINE_DATA_SIZE          (128)
#define IWARP_REQ_MAX_SINGLE_SQ_WQE_SIZE        (176)

#define IWARP_MAX_QPS                           (64 * 1024)

#endif /* __IWARP_COMMON__ */
