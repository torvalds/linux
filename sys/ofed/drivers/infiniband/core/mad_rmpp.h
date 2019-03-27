/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2005 Intel Inc. All rights reserved.
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
 *        disclaimer in the documentation and/or other materials
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
 *
 * $FreeBSD$
 */

#ifndef __MAD_RMPP_H__
#define __MAD_RMPP_H__

enum {
	IB_RMPP_RESULT_PROCESSED,
	IB_RMPP_RESULT_CONSUMED,
	IB_RMPP_RESULT_INTERNAL,
	IB_RMPP_RESULT_UNHANDLED
};

int ib_send_rmpp_mad(struct ib_mad_send_wr_private *mad_send_wr);

struct ib_mad_recv_wc *
ib_process_rmpp_recv_wc(struct ib_mad_agent_private *agent,
			struct ib_mad_recv_wc *mad_recv_wc);

int ib_process_rmpp_send_wc(struct ib_mad_send_wr_private *mad_send_wr,
			    struct ib_mad_send_wc *mad_send_wc);

void ib_rmpp_send_handler(struct ib_mad_send_wc *mad_send_wc);

void ib_cancel_rmpp_recvs(struct ib_mad_agent_private *agent);

int ib_retry_rmpp(struct ib_mad_send_wr_private *mad_send_wr);

#endif	/* __MAD_RMPP_H__ */
