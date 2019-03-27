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

#ifndef __ECORE_SP_API_H__
#define __ECORE_SP_API_H__

#include "ecore_status.h"

enum spq_mode {
	ECORE_SPQ_MODE_BLOCK,   /* Client will poll a designated mem. address */
	ECORE_SPQ_MODE_CB,  /* Client supplies a callback */
	ECORE_SPQ_MODE_EBLOCK,  /* ECORE should block until completion */
};

struct ecore_hwfn;
union event_ring_data;
struct eth_slow_path_rx_cqe;

struct ecore_spq_comp_cb {
	void	(*function)(struct ecore_hwfn *,
			 void *,
			 union event_ring_data *,
			 u8 fw_return_code);
	void	*cookie;
};


/**
 * @brief ecore_eth_cqe_completion - handles the completion of a
 *        ramrod on the cqe ring
 *
 * @param p_hwfn
 * @param cqe
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_eth_cqe_completion(struct ecore_hwfn *p_hwfn,
					      struct eth_slow_path_rx_cqe *cqe);
/**
 * @brief ecore_sp_pf_update_tunn_cfg - PF Function Tunnel configuration
 *					update  Ramrod
 *
 * This ramrod is sent to update a tunneling configuration
 * for a physical function (PF).
 *
 * @param p_hwfn
 * @param p_ptt
 * @param p_tunn - pf update tunneling parameters
 * @param comp_mode - completion mode
 * @param p_comp_data - callback function
 *
 * @return enum _ecore_status_t
 */

enum _ecore_status_t
ecore_sp_pf_update_tunn_cfg(struct ecore_hwfn *p_hwfn,
			    struct ecore_ptt *p_ptt,
			    struct ecore_tunnel_info *p_tunn,
			    enum spq_mode comp_mode,
			    struct ecore_spq_comp_cb *p_comp_data);
#endif
